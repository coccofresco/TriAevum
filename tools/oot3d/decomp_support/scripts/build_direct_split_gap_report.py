#!/usr/bin/env python3
"""Explain why direct split spans are not yet safe to promote.

The promotion queue intentionally keeps compiled direct packets conservative:
complete spans are only promotable after the packet compare reaches an exact or
near category. This report keeps the blocked rows actionable by classifying the
current gap and preserving the source/probe evidence needed for the next pass.
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
PROMOTION_READY_CATEGORIES = {"exact-c", "codegen-near", "structural-near"}


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


def bool_value(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    return str(value).strip().lower() in {"1", "true", "yes"}


def index_rows(data: dict[str, Any], key: str) -> dict[str, dict[str, Any]]:
    rows = data.get("rows", [])
    if not isinstance(rows, list):
        return {}
    return {
        str(row.get(key, "")).lower(): row
        for row in rows
        if isinstance(row, dict) and row.get(key)
    }


def gap_class(row: dict[str, Any], match: dict[str, Any]) -> str:
    if bool_value(row.get("promotion_ready")):
        return "promotion-ready"
    if str(row.get("span_status", "")) != "complete":
        return "truncated-source-span"

    category = str(row.get("probe_category", ""))
    target_count = int_value(row.get("target_instruction_count"))
    compiled_count = int_value(match.get("compiled_instruction_count"))
    lcs_target = float_value(match.get("lcs_target_ratio", row.get("lcs_target_ratio")))
    lcs_symbol = float_value(row.get("lcs_symbol_ratio"))
    adapter_count = int_value(match.get("adapter_count"))
    shape_anchors = int_value(match.get("shape_anchors"))
    first_difference = str(match.get("first_difference", ""))

    if category in PROMOTION_READY_CATEGORIES:
        return "promotion-gate-inconsistent"
    if target_count and compiled_count and target_count >= compiled_count * 8:
        return "target-envelope-much-larger-than-candidate"
    if first_difference.startswith("0: stmdb") and target_count and compiled_count and target_count > compiled_count:
        return "target-prologue-wider-than-candidate"
    if adapter_count == 0 and shape_anchors == 0 and lcs_target < 0.05:
        return "missing-shape-adapters"
    if lcs_symbol >= 0.30 and lcs_target < 0.10:
        return "helper-shape-present-but-embedded"
    if category == "semantic-started":
        return "semantic-started-needs-source-shape"
    return "semantic-gap-needs-better-candidate"


def next_gate(gap: str) -> str:
    gates = {
        "promotion-ready": "Review the generated source and promote behind a guarded build gate.",
        "promotion-gate-inconsistent": "Refresh promotion queue inputs before manual promotion.",
        "truncated-source-span": "Regenerate or narrow the source split until the candidate span is complete.",
        "target-envelope-much-larger-than-candidate": "Split the OOT3D target or locate enclosing helpers before comparing this candidate as a whole function.",
        "target-prologue-wider-than-candidate": "Treat the N64 function as an embedded helper and find the OOT3D wrapper/state envelope.",
        "missing-shape-adapters": "Mine/apply direct shape anchors and adapter lowering, then rerun compile and compare.",
        "helper-shape-present-but-embedded": "Promote only after isolating the embedded helper span from the larger OOT3D action.",
        "semantic-started-needs-source-shape": "Add source-shape adapters or a narrower split and require structural-near or better.",
        "semantic-gap-needs-better-candidate": "Search alternate symbols or public helper variants before attempting promotion.",
    }
    return gates.get(gap, "Keep as evidence until the compare reaches exact/codegen-near/structural-near.")


def build_report(queue: dict[str, Any], probe_match: dict[str, Any]) -> dict[str, Any]:
    match_by_entry = index_rows(probe_match, "entry")
    queue_rows = queue.get("rows", [])
    if not isinstance(queue_rows, list):
        queue_rows = []

    rows: list[dict[str, Any]] = []
    for row in queue_rows:
        if not isinstance(row, dict):
            continue
        entry = str(row.get("entry", "")).lower()
        match = match_by_entry.get(entry, {})
        gap = gap_class(row, match)
        rows.append(
            {
                "entry": entry,
                "oot3d_name": row.get("oot3d_name", ""),
                "domain": row.get("domain", ""),
                "candidate_symbol": row.get("candidate_symbol", ""),
                "candidate_is_best": bool_value(row.get("candidate_is_best")),
                "priority": int_value(row.get("priority")),
                "span_status": row.get("span_status", ""),
                "probe_category": row.get("probe_category", ""),
                "gap_class": gap,
                "target_instruction_count": int_value(row.get("target_instruction_count")),
                "candidate_instruction_count": int_value(row.get("candidate_instruction_count")),
                "compiled_instruction_count": int_value(match.get("compiled_instruction_count")),
                "lcs_target_ratio": float_value(match.get("lcs_target_ratio", row.get("lcs_target_ratio"))),
                "lcs_symbol_ratio": float_value(row.get("lcs_symbol_ratio")),
                "longest_common_run": match.get("longest_common_run", ""),
                "first_difference": match.get("first_difference", ""),
                "rewrite_count": int_value(match.get("rewrite_count")),
                "adapter_count": int_value(match.get("adapter_count")),
                "shape_anchors": int_value(match.get("shape_anchors")),
                "source_nonblank_lines": int_value(row.get("source_nonblank_lines")),
                "source": row.get("source", ""),
                "packet": row.get("packet", ""),
                "compiled_dump": match.get("compiled_dump", match.get("dump", "")),
                "next_gate": next_gate(gap),
            }
        )

    rows.sort(
        key=lambda item: (
            item["gap_class"] == "promotion-ready",
            item["span_status"] != "complete",
            not bool(item["candidate_is_best"]),
            -int(item["priority"]),
            str(item["entry"]),
        )
    )
    classes = Counter(str(row["gap_class"]) for row in rows)
    blocked = [row for row in rows if row["gap_class"] != "promotion-ready"]
    summary = {
        "rows": len(rows),
        "entries": len({row["entry"] for row in rows}),
        "blocked_rows": len(blocked),
        "promotion_ready_rows": len(rows) - len(blocked),
        "complete_blocked_rows": sum(1 for row in blocked if row["span_status"] == "complete"),
        "truncated_blocked_rows": sum(1 for row in blocked if row["span_status"] == "truncated"),
        "best_candidate_blocked_rows": sum(1 for row in blocked if bool(row["candidate_is_best"])),
        "gap_classes": dict(sorted(classes.items())),
        "top_blocker": blocked[0]["gap_class"] if blocked else "",
        "top_entry": blocked[0]["entry"] if blocked else "",
        "top_candidate_symbol": blocked[0]["candidate_symbol"] if blocked else "",
        "next_gate": "Resolve top gap classes until at least one direct split row reaches a near/exact compare category.",
    }
    return {
        "format": "oot3d_direct_split_gap_report_v1",
        "summary": summary,
        "rows": rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Split Gap Report",
        "",
        "This report explains why complete direct split spans remain reconstruction evidence instead of maintained C.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Rows | {summary['rows']} |",
        f"| Entries | {summary['entries']} |",
        f"| Blocked rows | {summary['blocked_rows']} |",
        f"| Promotion-ready rows | {summary['promotion_ready_rows']} |",
        f"| Complete blocked rows | {summary['complete_blocked_rows']} |",
        f"| Truncated blocked rows | {summary['truncated_blocked_rows']} |",
        f"| Best-candidate blocked rows | {summary['best_candidate_blocked_rows']} |",
        "",
        "## Gap Classes",
        "",
        "| Gap | Rows |",
        "| --- | ---: |",
    ]
    for gap, count in summary["gap_classes"].items():
        lines.append(f"| `{gap}` | {count} |")
    lines.extend(
        [
            "",
            "## Queue",
            "",
            "| Gap | OOT3D | Candidate | Target/compiled | LCS target/symbol | First difference | Source | Next gate |",
            "| --- | --- | --- | ---: | ---: | --- | --- | --- |",
        ]
    )
    for row in data["rows"]:
        lines.append(
            f"| `{row['gap_class']}` | `{row['entry']}` `{row['oot3d_name']}` | "
            f"`{row['candidate_symbol']}` | {row['target_instruction_count']}/{row['compiled_instruction_count']} | "
            f"{row['lcs_target_ratio']:.4f}/{row['lcs_symbol_ratio']:.4f} | "
            f"`{row['first_difference']}` | `{row['source']}` | {row['next_gate']} |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--queue", type=Path, default=ROOT / "analysis" / "direct_split_promotion_queue.json")
    parser.add_argument("--probe-match", type=Path, default=ROOT / "analysis" / "direct_packet_probe_match.json")
    parser.add_argument("--out-json", type=Path, default=ROOT / "analysis" / "direct_split_gap_report.json")
    parser.add_argument("--out-csv", type=Path, default=ROOT / "analysis" / "direct_split_gap_report.csv")
    parser.add_argument("--out-md", type=Path, default=ROOT / "analysis" / "direct_split_gap_report.md")
    args = parser.parse_args()

    data = build_report(read_json(args.queue, {}), read_json(args.probe_match, {}))
    fields = [
        "entry",
        "oot3d_name",
        "domain",
        "candidate_symbol",
        "candidate_is_best",
        "priority",
        "span_status",
        "probe_category",
        "gap_class",
        "target_instruction_count",
        "candidate_instruction_count",
        "compiled_instruction_count",
        "lcs_target_ratio",
        "lcs_symbol_ratio",
        "longest_common_run",
        "first_difference",
        "rewrite_count",
        "adapter_count",
        "shape_anchors",
        "source_nonblank_lines",
        "source",
        "packet",
        "compiled_dump",
        "next_gate",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct split gap report: "
        f"{summary['blocked_rows']} blocked rows, {summary['promotion_ready_rows']} promotion-ready, "
        f"top gap {summary['top_blocker'] or 'none'}"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
