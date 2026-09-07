#!/usr/bin/env python3
"""Audit N64-derived C ports that are already convertible/promotable."""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path
from typing import Any

from audit_c_conversion_readiness import BASELINE_DEFINES, cflag_defines, source_style


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_GATE = ROOT / "analysis" / "structured_c_match_gate.csv"
DEFAULT_MATCHED = ROOT / "metadata" / "matched_sources.txt"
DEFAULT_MATCHED_COMPARE = ROOT / "build" / "matched" / "compare_matched_objects.json"
DEFAULT_SHAPE_ANCHORS = ROOT / "analysis" / "direct_shape_anchor_windows.csv"
DEFAULT_SHAPE_APPLY_PLAN = ROOT / "analysis" / "direct_shape_anchor_apply_plan.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "convertible_ports.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "convertible_ports.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "convertible_ports.md"


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def read_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "status",
        "category",
        "unit",
        "oot3d_entry",
        "oot3d_name",
        "port_file",
        "implementation_style",
        "in_matched_sources",
        "matched_baseline_exact",
        "target_instruction_count",
        "compiled_instruction_count",
        "lcs_instruction_count",
        "lcs_target_ratio",
        "shape_anchor_count",
        "shape_rewrite_sites",
        "shape_pending_replacements",
        "shape_already_shaped_sites",
        "next_action",
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row.get(key, "") for key in fieldnames})


def read_matched_sources(path: Path) -> set[str]:
    sources: set[str] = set()
    if not path.is_file():
        return sources
    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        sources.add(line.replace("\\", "/"))
    return sources


def matched_compare_index(path: Path) -> dict[str, bool]:
    rows = read_json(path, [])
    if not isinstance(rows, list):
        return {}
    return {str(row.get("name", "")): bool(row.get("exact_match")) for row in rows}


def anchor_index(path: Path) -> dict[str, dict[str, int]]:
    index: dict[str, dict[str, int]] = {}
    for row in read_csv(path):
        entry = str(row.get("oot3d_entry", "")).lower()
        if not entry:
            continue
        bucket = index.setdefault(entry, {"shape_anchor_count": 0, "shape_rewrite_sites": 0})
        bucket["shape_anchor_count"] += 1
        try:
            bucket["shape_rewrite_sites"] += int(row.get("source_rewrite_count", 0) or 0)
        except ValueError:
            pass
    return index


def shape_apply_index(path: Path) -> dict[str, dict[str, Any]]:
    plan = read_json(path, {})
    rows = plan.get("rows", []) if isinstance(plan, dict) else []
    index: dict[str, dict[str, Any]] = {}
    for row in rows:
        if not isinstance(row, dict):
            continue
        entry = str(row.get("entry", "")).lower()
        if entry:
            index[entry] = row
    return index


def classify_row(
    row: dict[str, str],
    matched_sources: set[str],
    exact_by_name: dict[str, bool],
    implementation_style: str,
    shape_apply: dict[str, Any] | None = None,
) -> tuple[str, str]:
    category = row.get("category", "")
    port_file = row.get("port_file", "").replace("\\", "/")
    in_matched = port_file in matched_sources
    exact_in_baseline = exact_by_name.get(row.get("oot3d_name", ""), False)

    if category == "exact-c":
        if implementation_style == "naked-asm":
            return (
                "exact-seed-needs-c",
                "Exact compare is produced by a naked asm seed; reconstruct target-shaped C before counting it as converted.",
            )
        if implementation_style == "unknown":
            return (
                "verify-source-shape",
                "Exact compare exists, but the function body could not be classified in the source file.",
            )
        if implementation_style == "c-inline-asm":
            if in_matched and exact_in_baseline:
                return (
                    "already-converted-inline-asm",
                    "Exact in the matched lane, but still depends on inline asm constraints.",
                )
            return (
                "ready-to-promote-inline-asm",
                "Exact compiled C body with inline asm constraints is outside matched_sources; promote only as an interim C lane.",
            )
        if in_matched and exact_in_baseline:
            return "already-converted", "No action: exact compiler-shaped C is already in the maintained matched lane."
        if in_matched and not exact_in_baseline:
            return "verify-mixed-file", "File is in matched_sources, but this exact row is not exact in the baseline compare."
        return "ready-to-promote", "Add the source file to metadata/matched_sources.txt and rebuild the matched lane."

    if category == "codegen-near":
        return "next-convertible", "Run compile-profile search or local source-shape rewrites; semantics are already close."

    if category == "structural-near":
        if shape_apply:
            replacements = int(shape_apply.get("replacements", 0) or 0)
            already_shaped = int(shape_apply.get("already_shaped_sites", 0) or 0)
            if replacements == 0 and already_shaped > 0:
                return (
                    "needs-batch-reshaping",
                    "Target shape anchors are already present; continue register-pressure/source reshaping before promotion.",
                )
        return "shape-convertible", "Apply target shape anchors and re-run the structured gate."

    if category == "semantic-started":
        return "needs-batch-reshaping", "Use direct conversion workorders and target shape anchors before promotion."

    return "not-yet-convertible", "Needs semantic reconstruction before promotion."


def analyze(args: argparse.Namespace) -> dict[str, Any]:
    gate_rows = read_csv(args.structured_gate)
    matched_sources = read_matched_sources(args.matched_sources)
    exact_by_name = matched_compare_index(args.matched_compare)
    anchors = anchor_index(args.shape_anchors)
    shape_apply = shape_apply_index(args.shape_anchor_apply_plan)

    rows: list[dict[str, Any]] = []
    for row in gate_rows:
        entry = str(row.get("oot3d_entry", "")).lower()
        shape_apply_row = shape_apply.get(entry, {})
        defines = set(BASELINE_DEFINES) | cflag_defines(row.get("extra_cflag", ""))
        implementation_style = source_style(row.get("source", ""), row.get("oot3d_name", ""), defines)
        status, next_action = classify_row(
            row,
            matched_sources,
            exact_by_name,
            implementation_style,
            shape_apply_row,
        )
        anchor_counts = anchors.get(entry, {"shape_anchor_count": 0, "shape_rewrite_sites": 0})
        port_file = row.get("port_file", "").replace("\\", "/")
        rows.append(
            {
                "status": status,
                "category": row.get("category", ""),
                "unit": row.get("unit", ""),
                "oot3d_entry": entry,
                "oot3d_name": row.get("oot3d_name", ""),
                "port_file": port_file,
                "implementation_style": implementation_style,
                "in_matched_sources": "yes" if port_file in matched_sources else "no",
                "matched_baseline_exact": "yes" if exact_by_name.get(row.get("oot3d_name", ""), False) else "no",
                "target_instruction_count": row.get("target_instruction_count", "0"),
                "compiled_instruction_count": row.get("compiled_instruction_count", "0"),
                "lcs_instruction_count": row.get("lcs_instruction_count", "0"),
                "lcs_target_ratio": row.get("lcs_target_ratio", "0"),
                "shape_anchor_count": anchor_counts["shape_anchor_count"],
                "shape_rewrite_sites": anchor_counts["shape_rewrite_sites"],
                "shape_pending_replacements": int(shape_apply_row.get("replacements", 0) or 0),
                "shape_already_shaped_sites": int(shape_apply_row.get("already_shaped_sites", 0) or 0),
                "next_action": next_action,
            }
        )

    summary = {
        "functions": len(rows),
        "status_counts": dict(Counter(row["status"] for row in rows)),
        "category_counts": dict(Counter(row["category"] for row in rows)),
        "ready_to_promote": sum(1 for row in rows if row["status"] == "ready-to-promote"),
        "ready_to_promote_inline_asm": sum(1 for row in rows if row["status"] == "ready-to-promote-inline-asm"),
        "already_converted": sum(1 for row in rows if row["status"] == "already-converted"),
        "already_converted_inline_asm": sum(
            1 for row in rows if row["status"] == "already-converted-inline-asm"
        ),
        "exact_seed_needs_c": sum(1 for row in rows if row["status"] == "exact-seed-needs-c"),
        "shape_convertible": sum(1 for row in rows if row["status"] == "shape-convertible"),
        "needs_batch_reshaping": sum(1 for row in rows if row["status"] == "needs-batch-reshaping"),
    }
    rows.sort(key=sort_key)
    return {"summary": summary, "rows": rows}


def sort_key(row: dict[str, Any]) -> tuple[int, int, str]:
    order = {
        "ready-to-promote": 0,
        "ready-to-promote-inline-asm": 1,
        "verify-mixed-file": 2,
        "verify-source-shape": 3,
        "next-convertible": 4,
        "shape-convertible": 5,
        "needs-batch-reshaping": 6,
        "exact-seed-needs-c": 7,
        "already-converted": 8,
        "already-converted-inline-asm": 9,
        "not-yet-convertible": 10,
    }
    return (
        order.get(row["status"], 99),
        -int(row.get("shape_rewrite_sites", 0) or 0),
        str(row.get("oot3d_entry", "")),
    )


def write_md(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Convertible Ports Audit",
        "",
        "This report separates N64-derived C ports that are already promotable from ports that still need batch reshaping.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Functions checked | {summary['functions']} |",
        f"| Already converted real C | {summary['already_converted']} |",
        f"| Already exact inline-asm C | {summary['already_converted_inline_asm']} |",
        f"| Exact naked seeds still needing C | {summary['exact_seed_needs_c']} |",
        f"| Ready to promote now | {summary['ready_to_promote']} |",
        f"| Ready to promote inline-asm now | {summary['ready_to_promote_inline_asm']} |",
        f"| Shape-convertible next | {summary['shape_convertible']} |",
        f"| Needs batch reshaping | {summary['needs_batch_reshaping']} |",
        "",
        "## Queue",
        "",
        "| Status | Category | OOT3D | Unit | Style | Matched | Baseline exact | Shape anchors | Rewrite sites | Pending | Shaped | Next action |",
        "| --- | --- | --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: | --- |",
    ]
    for row in data["rows"]:
        lines.append(
            f"| `{row['status']}` | `{row['category']}` | `{row['oot3d_entry']}` `{row['oot3d_name']}` | "
            f"`{row['unit']}` | `{row['implementation_style']}` | `{row['in_matched_sources']}` | "
            f"`{row['matched_baseline_exact']}` | "
            f"{row['shape_anchor_count']} | {row['shape_rewrite_sites']} | {row['shape_pending_replacements']} | "
            f"{row['shape_already_shaped_sites']} | {row['next_action']} |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--structured-gate", type=Path, default=DEFAULT_GATE)
    parser.add_argument("--matched-sources", type=Path, default=DEFAULT_MATCHED)
    parser.add_argument("--matched-compare", type=Path, default=DEFAULT_MATCHED_COMPARE)
    parser.add_argument("--shape-anchors", type=Path, default=DEFAULT_SHAPE_ANCHORS)
    parser.add_argument("--shape-anchor-apply-plan", type=Path, default=DEFAULT_SHAPE_APPLY_PLAN)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()

    data = analyze(args)
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"])
    write_md(args.out_md, data)

    summary = data["summary"]
    print(
        "convertible audit: "
        f"{summary['already_converted']} already converted real C, "
        f"{summary['already_converted_inline_asm']} inline-asm exact, "
        f"{summary['exact_seed_needs_c']} exact seeds needing C, "
        f"{summary['ready_to_promote']} ready to promote, "
        f"{summary['shape_convertible']} shape-convertible, "
        f"{summary['needs_batch_reshaping']} needing batch reshaping"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
