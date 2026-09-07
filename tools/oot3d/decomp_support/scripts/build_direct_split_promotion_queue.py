#!/usr/bin/env python3
"""Build a promotion queue for complete direct split/helper spans.

The direct split workorder report can find useful helper spans inside compiled
direct packets. This queue keeps those spans separate from real promotion-ready
C: a span is actionable reconstruction evidence, but it is not promotable until
the packet comparison reaches an exact or near category.
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


def probe_match_index(data: dict[str, Any]) -> dict[str, dict[str, Any]]:
    rows = data.get("rows", [])
    if not isinstance(rows, list):
        return {}
    return {
        str(row.get("entry", "")).lower(): row
        for row in rows
        if isinstance(row, dict) and row.get("entry")
    }


def build_queue(workorders: dict[str, Any], probe_matches: dict[str, Any]) -> dict[str, Any]:
    match_by_entry = probe_match_index(probe_matches)
    source_rows = workorders.get("rows", [])
    rows: list[dict[str, Any]] = []
    if not isinstance(source_rows, list):
        source_rows = []

    for row in source_rows:
        if not isinstance(row, dict):
            continue
        entry = str(row.get("entry", "")).lower()
        match = match_by_entry.get(entry, {})
        probe_category = str(match.get("category", ""))
        span_status = str(row.get("source_span_status", ""))
        candidate_is_best = str(row.get("candidate_is_best", "")).lower() == "yes"
        span_complete = span_status == "complete"
        promotion_ready = span_complete and probe_category in PROMOTION_READY_CATEGORIES
        rows.append(
            {
                "entry": entry,
                "oot3d_name": row.get("oot3d_name", ""),
                "domain": row.get("domain", ""),
                "mapped_n64_name": row.get("mapped_n64_name", ""),
                "candidate_symbol": row.get("candidate_symbol", ""),
                "candidate_is_best": candidate_is_best,
                "reason": row.get("reason", ""),
                "priority": int_value(row.get("priority")),
                "target_instruction_count": int_value(row.get("target_instruction_count")),
                "candidate_instruction_count": int_value(row.get("candidate_instruction_count")),
                "lcs_target_ratio": float_value(row.get("lcs_target_ratio")),
                "lcs_symbol_ratio": float_value(row.get("lcs_symbol_ratio")),
                "probe_category": probe_category,
                "span_status": span_status,
                "source_nonblank_lines": int_value(row.get("source_nonblank_lines")),
                "source": row.get("source", ""),
                "packet": row.get("packet", ""),
                "promotion_ready": promotion_ready,
                "next_gate": next_gate(span_complete, probe_category),
            }
        )

    rows.sort(
        key=lambda item: (
            not bool(item["promotion_ready"]),
            item["span_status"] != "complete",
            not bool(item["candidate_is_best"]),
            -int(item["priority"]),
            str(item["entry"]),
        )
    )
    complete_rows = [row for row in rows if row["span_status"] == "complete"]
    ready_rows = [row for row in rows if row["promotion_ready"]]
    summary = {
        "workorders": len(rows),
        "entries": len({row["entry"] for row in rows}),
        "complete_spans": len(complete_rows),
        "truncated_spans": sum(1 for row in rows if row["span_status"] == "truncated"),
        "best_candidate_complete_spans": sum(
            1 for row in complete_rows if bool(row["candidate_is_best"])
        ),
        "promotion_ready_spans": len(ready_rows),
        "promotion_blocked_spans": len(rows) - len(ready_rows),
        "probe_categories": dict(Counter(str(row["probe_category"]) for row in rows)),
        "next_gate": (
            "Improve direct packet source shape until at least one complete span reaches exact/codegen-near/structural-near."
        ),
    }
    return {
        "format": "oot3d_direct_split_promotion_queue_v1",
        "summary": summary,
        "rows": rows,
    }


def next_gate(span_complete: bool, probe_category: str) -> str:
    if not span_complete:
        return "Complete or narrow the materialized source span before promotion."
    if probe_category in PROMOTION_READY_CATEGORIES:
        return "Review and promote behind a guarded build gate."
    return "Keep as reconstruction evidence; improve source shape/adapters before promotion."


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Split Promotion Queue",
        "",
        "This queue ranks complete direct split/helper spans. Complete spans are not treated as promotable unless their compiled packet compare is exact or near.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Workorders | {summary['workorders']} |",
        f"| Entries | {summary['entries']} |",
        f"| Complete spans | {summary['complete_spans']} |",
        f"| Truncated spans | {summary['truncated_spans']} |",
        f"| Best-candidate complete spans | {summary['best_candidate_complete_spans']} |",
        f"| Promotion-ready spans | {summary['promotion_ready_spans']} |",
        f"| Promotion-blocked spans | {summary['promotion_blocked_spans']} |",
        "",
        "## Queue",
        "",
        "| Ready | Status | OOT3D | Candidate | Priority | LCS target/symbol | Source | Next gate |",
        "| --- | --- | --- | --- | ---: | ---: | --- | --- |",
    ]
    for row in data["rows"]:
        ready = "yes" if row["promotion_ready"] else "no"
        lines.append(
            f"| `{ready}` | `{row['span_status']}` `{row['probe_category']}` | "
            f"`{row['entry']}` `{row['oot3d_name']}` | `{row['candidate_symbol']}` | "
            f"{row['priority']} | {row['lcs_target_ratio']:.4f}/{row['lcs_symbol_ratio']:.4f} | "
            f"`{row['source']}` | {row['next_gate']} |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workorders", type=Path, default=ROOT / "analysis" / "direct_split_workorders.json")
    parser.add_argument("--probe-match", type=Path, default=ROOT / "analysis" / "direct_packet_probe_match.json")
    parser.add_argument("--out-json", type=Path, default=ROOT / "analysis" / "direct_split_promotion_queue.json")
    parser.add_argument("--out-csv", type=Path, default=ROOT / "analysis" / "direct_split_promotion_queue.csv")
    parser.add_argument("--out-md", type=Path, default=ROOT / "analysis" / "direct_split_promotion_queue.md")
    args = parser.parse_args()

    data = build_queue(read_json(args.workorders, {}), read_json(args.probe_match, {}))
    fields = [
        "entry",
        "oot3d_name",
        "domain",
        "mapped_n64_name",
        "candidate_symbol",
        "candidate_is_best",
        "reason",
        "priority",
        "target_instruction_count",
        "candidate_instruction_count",
        "lcs_target_ratio",
        "lcs_symbol_ratio",
        "probe_category",
        "span_status",
        "source_nonblank_lines",
        "source",
        "packet",
        "promotion_ready",
        "next_gate",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct split promotion queue: "
        f"{summary['workorders']} workorders, {summary['complete_spans']} complete spans, "
        f"{summary['promotion_ready_spans']} promotion-ready"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
