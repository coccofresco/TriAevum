#!/usr/bin/env python3
"""Compare inferred OOT3D target windows against compiled helper symbols."""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path
from typing import Any

from build_direct_target_split_suggestions import read_target_functions_with_addresses
from compare_direct_packet_probe_matches import read_objdump_file
from compare_runtime_objects import compare_ops


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ENVELOPE_PLAN = ROOT / "analysis" / "direct_target_envelope_plan.json"
DEFAULT_TARGET_DISASSEMBLY = ROOT / "ghidra_export" / "disassembly.txt"
DEFAULT_GAP_REPORT = ROOT / "analysis" / "direct_split_gap_report.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_target_window_probe_match.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_target_window_probe_match.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_target_window_probe_match.md"


def read_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def int_value(value: Any) -> int:
    try:
        return int(value or 0)
    except (TypeError, ValueError):
        return 0


def float_value(value: Any) -> float:
    try:
        return float(value or 0.0)
    except (TypeError, ValueError):
        return 0.0


def list_value(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def rel(path: Path | str) -> str:
    p = Path(path)
    try:
        return str(p.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(p).replace("\\", "/")


def gap_index(data: dict[str, Any]) -> dict[tuple[str, str], dict[str, Any]]:
    rows = list_value(data.get("rows", []))
    result: dict[tuple[str, str], dict[str, Any]] = {}
    for row in rows:
        if not isinstance(row, dict):
            continue
        entry = str(row.get("entry", "")).lower()
        symbol = str(row.get("candidate_symbol", ""))
        if entry and symbol:
            result[(entry, symbol)] = row
    return result


def repo_path(value: Any) -> Path:
    path = Path(str(value or ""))
    return path if path.is_absolute() else ROOT / path


def classify_window(compare: dict[str, Any]) -> str:
    if compare.get("exact_match"):
        return "exact-window"
    window_count = int_value(compare.get("target_instruction_count"))
    compiled_count = int_value(compare.get("compiled_instruction_count"))
    count_delta = abs(window_count - compiled_count)
    prefix = int_value(compare.get("matching_prefix"))
    suffix = int_value(compare.get("matching_suffix"))
    lcs_ratio = float_value(compare.get("lcs_target_ratio"))
    longest_run = compare.get("longest_common_run", {})
    run_length = int_value(longest_run.get("length")) if isinstance(longest_run, dict) else 0
    if count_delta <= 3 and (lcs_ratio >= 0.50 or prefix >= 5 or suffix >= 5 or run_length >= 8):
        return "codegen-near-window"
    if lcs_ratio >= 0.35 or run_length >= 5:
        return "structural-near-window"
    if lcs_ratio >= 0.15 or run_length >= 3:
        return "semantic-window"
    return "weak-window"


def first_difference_text(compare: dict[str, Any]) -> str:
    diff = compare.get("first_difference")
    if not isinstance(diff, dict):
        return ""
    return f"{diff.get('index')}: {diff.get('target')} vs {diff.get('compiled')}"


def run_text(compare: dict[str, Any]) -> str:
    run = compare.get("longest_common_run")
    if not isinstance(run, dict):
        return ""
    return f"{run.get('length', '')}@{run.get('target_index', '')}/{run.get('compiled_index', '')}"


def target_by_entry(functions: dict[str, dict[str, Any]]) -> dict[str, dict[str, Any]]:
    return {
        str(function.get("entry", "")).lower(): function
        for function in functions.values()
        if function.get("entry")
    }


def build_rows(args: argparse.Namespace) -> dict[str, Any]:
    envelope = read_json(args.envelope_plan, {})
    gap_report = read_json(args.gap_report, {})
    gap_by_key = gap_index(gap_report)
    targets_by_entry = target_by_entry(read_target_functions_with_addresses(args.target_disassembly))
    compiled_cache: dict[Path, dict[str, dict[str, object]]] = {}
    rows: list[dict[str, Any]] = []

    for anchor in list_value(envelope.get("anchor_rows", [])):
        if not isinstance(anchor, dict):
            continue
        entry = str(anchor.get("entry", "")).lower()
        candidate = str(anchor.get("candidate_symbol", ""))
        target = targets_by_entry.get(entry)
        gap = gap_by_key.get((entry, candidate), {})
        dump_path = repo_path(gap.get("compiled_dump", ""))
        base = {
            "entry": entry,
            "oot3d_name": anchor.get("oot3d_name", ""),
            "cluster_index": int_value(anchor.get("cluster_index")),
            "cluster_start_addr": anchor.get("cluster_start_addr", ""),
            "cluster_end_addr": anchor.get("cluster_end_addr", ""),
            "cluster_anchor_count": int_value(anchor.get("cluster_anchor_count")),
            "candidate_symbol": candidate,
            "kind": anchor.get("kind", ""),
            "gap_class": anchor.get("gap_class", ""),
            "priority": int_value(anchor.get("priority")),
            "target_function_instruction_count": int_value(anchor.get("target_instruction_count")),
            "inferred_start_index": int_value(anchor.get("inferred_start_index")),
            "inferred_end_index": int_value(anchor.get("inferred_end_index")),
            "inferred_start_addr": anchor.get("inferred_start_addr", ""),
            "inferred_end_addr": anchor.get("inferred_end_addr", ""),
            "source": anchor.get("source", ""),
            "compiled_dump": rel(dump_path) if str(gap.get("compiled_dump", "")) else "",
        }
        if target is None:
            rows.append({**base, "category": "target-missing"})
            continue
        if not dump_path.is_file():
            rows.append({**base, "category": "compiled-dump-missing"})
            continue
        if dump_path not in compiled_cache:
            compiled_cache[dump_path] = read_objdump_file(dump_path)
        compiled = compiled_cache[dump_path].get(candidate)
        if compiled is None:
            rows.append({**base, "category": "compiled-symbol-missing"})
            continue

        ops = list(target.get("ops", []))
        start = int_value(anchor.get("inferred_start_index"))
        end = int_value(anchor.get("inferred_end_index"))
        if start < 0 or end < start or end >= len(ops):
            rows.append({**base, "category": "window-out-of-range"})
            continue
        target_window_ops = ops[start : end + 1]
        compare = compare_ops(target_window_ops, list(compiled.get("ops", [])))
        category = classify_window(compare)
        lcs_instruction_count = int_value(compare.get("lcs_instruction_count"))
        compiled_instruction_count = int_value(compare.get("compiled_instruction_count"))
        rows.append(
            {
                **base,
                "category": category,
                "window_instruction_count": int_value(compare.get("target_instruction_count")),
                "compiled_instruction_count": compiled_instruction_count,
                "matching_prefix": int_value(compare.get("matching_prefix")),
                "matching_suffix": int_value(compare.get("matching_suffix")),
                "lcs_instruction_count": lcs_instruction_count,
                "lcs_window_ratio": float_value(compare.get("lcs_target_ratio")),
                "lcs_compiled_ratio": (
                    round(lcs_instruction_count / compiled_instruction_count, 4)
                    if compiled_instruction_count
                    else 0.0
                ),
                "longest_common_run": run_text(compare),
                "first_difference": first_difference_text(compare),
                "exact_match": bool(compare.get("exact_match")),
            }
        )

    rows.sort(
        key=lambda row: (
            category_order(str(row.get("category", ""))),
            -float_value(row.get("lcs_window_ratio")),
            -int_value(row.get("priority")),
            str(row.get("entry", "")),
        )
    )
    counts = Counter(str(row.get("category", "")) for row in rows)
    compared_categories = {"exact-window", "codegen-near-window", "structural-near-window", "semantic-window", "weak-window"}
    compared_rows = [row for row in rows if str(row.get("category", "")) in compared_categories]
    near_rows = [
        row for row in rows
        if str(row.get("category", "")) in {"exact-window", "codegen-near-window", "structural-near-window"}
    ]
    summary = {
        "windows": len(rows),
        "compared_windows": len(compared_rows),
        "near_or_exact_windows": len(near_rows),
        "exact_windows": counts["exact-window"],
        "codegen_near_windows": counts["codegen-near-window"],
        "structural_near_windows": counts["structural-near-window"],
        "semantic_windows": counts["semantic-window"],
        "weak_windows": counts["weak-window"],
        "compare_blocked_windows": len(rows) - len(compared_rows),
        "best_window_entry": rows[0].get("entry", "") if rows else "",
        "best_window_candidate": rows[0].get("candidate_symbol", "") if rows else "",
        "best_window_category": rows[0].get("category", "") if rows else "",
        "best_window_lcs_window_ratio": rows[0].get("lcs_window_ratio", 0.0) if rows else 0.0,
        "categories": dict(sorted(counts.items())),
        "next_gate": "Use structural-near/exact target windows as split seeds; otherwise improve target-window boundaries.",
    }
    return {
        "format": "oot3d_direct_target_window_probe_match_v1",
        "summary": summary,
        "rows": rows,
    }


def category_order(category: str) -> int:
    order = {
        "exact-window": 0,
        "codegen-near-window": 1,
        "structural-near-window": 2,
        "semantic-window": 3,
        "weak-window": 4,
        "compiled-symbol-missing": 5,
        "compiled-dump-missing": 6,
        "target-missing": 7,
        "window-out-of-range": 8,
    }
    return order.get(category, 99)


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Target Window Probe Match",
        "",
        "This report compares inferred OOT3D target windows, not whole target functions, against compiled helper symbols.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Windows | {summary['windows']} |",
        f"| Compared windows | {summary['compared_windows']} |",
        f"| Near/exact windows | {summary['near_or_exact_windows']} |",
        f"| Exact windows | {summary['exact_windows']} |",
        f"| Codegen-near windows | {summary['codegen_near_windows']} |",
        f"| Structural-near windows | {summary['structural_near_windows']} |",
        f"| Semantic windows | {summary['semantic_windows']} |",
        f"| Weak windows | {summary['weak_windows']} |",
        "",
        "## Windows",
        "",
        "| Category | OOT3D | Candidate | Window | Insns | LCS window/compiled | Run | First difference | Source |",
        "| --- | --- | --- | --- | ---: | ---: | --- | --- | --- |",
    ]
    for row in data["rows"]:
        lines.append(
            f"| `{row['category']}` | `{row['entry']}` `{row['oot3d_name']}` | "
            f"`{row['candidate_symbol']}` | `{row['inferred_start_addr']}`-`{row['inferred_end_addr']}` "
            f"(cluster {row['cluster_index']}) | {row.get('window_instruction_count', '')}/{row.get('compiled_instruction_count', '')} | "
            f"{float_value(row.get('lcs_window_ratio')):.4f}/{float_value(row.get('lcs_compiled_ratio')):.4f} | "
            f"{row.get('longest_common_run', '')} | `{row.get('first_difference', '')}` | `{row.get('source', '')}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--envelope-plan", type=Path, default=DEFAULT_ENVELOPE_PLAN)
    parser.add_argument("--target-disassembly", type=Path, default=DEFAULT_TARGET_DISASSEMBLY)
    parser.add_argument("--gap-report", type=Path, default=DEFAULT_GAP_REPORT)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()

    data = build_rows(args)
    fields = [
        "category",
        "entry",
        "oot3d_name",
        "cluster_index",
        "cluster_start_addr",
        "cluster_end_addr",
        "cluster_anchor_count",
        "candidate_symbol",
        "kind",
        "gap_class",
        "priority",
        "target_function_instruction_count",
        "inferred_start_index",
        "inferred_end_index",
        "inferred_start_addr",
        "inferred_end_addr",
        "window_instruction_count",
        "compiled_instruction_count",
        "matching_prefix",
        "matching_suffix",
        "lcs_instruction_count",
        "lcs_window_ratio",
        "lcs_compiled_ratio",
        "longest_common_run",
        "first_difference",
        "exact_match",
        "source",
        "compiled_dump",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct target window probe match: "
        f"{summary['compared_windows']}/{summary['windows']} compared, "
        f"{summary['near_or_exact_windows']} near/exact, "
        f"{summary['semantic_windows']} semantic"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
