#!/usr/bin/env python3
"""Synthesize C frontier blockers and the next conservative decompilation action."""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path
from typing import Any

from probe_direct_source_subregions import rel


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_FRONTIER_CSV = ROOT / "analysis" / "c_reconstruction_frontier.csv"
DEFAULT_FRONTIER_JSON = ROOT / "analysis" / "c_reconstruction_frontier.json"
DEFAULT_SPLIT_GAP = ROOT / "analysis" / "direct_split_gap_report.json"
DEFAULT_STATE_MODE_QUAL = ROOT / "analysis" / "direct_state_mode_source_workorder_seed_qualification.json"
DEFAULT_STATE_MODE_CALL_SHAPE = ROOT / "analysis" / "direct_state_mode_source_call_shape.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_frontier_blocker_synthesis.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_frontier_blocker_synthesis.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_frontier_blocker_synthesis.md"
EXHAUSTED_ENTRY = "00473ef8"


def read_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    return value if isinstance(value, dict) else default


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


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


def best_frontier_rows(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    best: dict[str, dict[str, str]] = {}
    for row in rows:
        entry = str(row.get("entry", "")).lower()
        if not entry:
            continue
        current = best.get(entry)
        if current is None:
            best[entry] = row
            continue
        row_score = (
            frontier_rank(str(row.get("frontier_status", ""))),
            int_value(row.get("target_instruction_count")),
            -int_value(row.get("complete_split_spans")),
        )
        current_score = (
            frontier_rank(str(current.get("frontier_status", ""))),
            int_value(current.get("target_instruction_count")),
            -int_value(current.get("complete_split_spans")),
        )
        if row_score < current_score:
            best[entry] = row
    return list(best.values())


def frontier_rank(status: str) -> int:
    return {
        "ready-to-promote-c": 0,
        "compiled-packet-ready-to-promote": 0,
        "in-source-c-ready-to-promote": 0,
        "structured-c-needs-lowering": 1,
        "inline-c-needs-lowering": 2,
        "compiled-packet-near-seed": 3,
        "in-source-c-near-seed": 3,
        "compiled-packet-split-seed": 4,
        "compiled-packet-started-seed": 5,
        "in-source-c-started-seed": 5,
        "materialized-packet-compile-blocked": 6,
        "seed-needs-materialized-c": 7,
    }.get(status, 99)


def exhausted_state_mode(state_qual: dict[str, Any], call_shape: dict[str, Any]) -> dict[str, Any]:
    qual = state_qual.get("summary", {}) if isinstance(state_qual, dict) else {}
    calls = call_shape.get("summary", {}) if isinstance(call_shape, dict) else {}
    return {
        "probe_rows": int_value(qual.get("probe_rows")),
        "near_or_exact_rows": int_value(qual.get("near_or_exact_rows")),
        "qualified_seed_rows": int_value(qual.get("qualified_seed_rows")),
        "risk_rows": int_value(qual.get("risk_rows")),
        "semantic_supported": int_value(calls.get("semantic_supported")),
        "semantic_risk_confirmed": int_value(calls.get("semantic_risk_confirmed")),
        "semantic_weak": int_value(calls.get("semantic_weak")),
        "promotion_ready": int_value(calls.get("promotion_ready")),
        "exhausted": (
            int_value(qual.get("probe_rows")) > 0
            and int_value(qual.get("qualified_seed_rows")) == 0
            and int_value(calls.get("promotion_ready")) == 0
        ),
    }


def blocker_for(row: dict[str, str], exhausted: dict[str, Any]) -> tuple[str, str, str]:
    entry = str(row.get("entry", "")).lower()
    status = str(row.get("frontier_status", ""))
    if entry == EXHAUSTED_ENTRY and exhausted.get("exhausted"):
        return (
            "state-mode-workorder-branch-exhausted",
            "high",
            "Stop using the current state/mode source workorders as promotion evidence; pivot to state helper semantics or another target envelope.",
        )
    if status == "structured-c-needs-lowering":
        return (
            "structured-c-lowering-ready",
            "medium",
            "Maintained C already exists; apply direct-offset/source-shape lowering and rerun structured comparison.",
        )
    if status == "inline-c-needs-lowering":
        return (
            "inline-c-lowering-needed",
            "medium",
            "Reduce inline asm constraints while preserving the exact baseline.",
        )
    if status == "compiled-packet-split-seed":
        return (
            "target-envelope-too-large",
            "high",
            "Split the OOT3D target or locate enclosing helpers before comparing this candidate as a whole function.",
        )
    if status == "compiled-packet-started-seed":
        return (
            "compiled-packet-semantic-started",
            "medium",
            "Use the compiled packet as C baseline evidence, then improve adapters or slicing.",
        )
    return ("frontier-open", "medium", row.get("next_action", "Continue frontier-specific decompilation."))


def queue_rank(row: dict[str, Any]) -> tuple[int, int, int, str]:
    blocker = str(row.get("blocker_class", ""))
    status = str(row.get("frontier_status", ""))
    if blocker == "state-mode-workorder-branch-exhausted":
        return (98, int_value(row.get("target_instruction_count")), 0, str(row.get("entry", "")))
    return (
        frontier_rank(status),
        int_value(row.get("target_instruction_count")),
        -int_value(row.get("complete_split_spans")),
        str(row.get("entry", "")),
    )


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    frontier_json = read_json(args.frontier_json, {})
    split_gap = read_json(args.split_gap, {})
    state_qual = read_json(args.state_mode_seed_qualification, {})
    call_shape = read_json(args.state_mode_call_shape, {})
    exhausted = exhausted_state_mode(state_qual, call_shape)
    rows: list[dict[str, Any]] = []
    for source in best_frontier_rows(read_csv(args.frontier_csv)):
        entry = str(source.get("entry", "")).lower()
        status = str(source.get("frontier_status", ""))
        if status.startswith("matched-"):
            continue
        blocker_class, severity, next_gate = blocker_for(source, exhausted)
        rows.append(
            {
                "entry": entry,
                "oot3d_name": source.get("oot3d_name", ""),
                "n64_name": source.get("n64_name", ""),
                "port_file": source.get("port_file", ""),
                "frontier_status": status,
                "target_instruction_count": int_value(source.get("target_instruction_count")),
                "probe_category": source.get("probe_category", ""),
                "probe_lcs_ratio": float_value(source.get("probe_lcs_ratio")),
                "split_workorders": int_value(source.get("split_workorders")),
                "complete_split_spans": int_value(source.get("complete_split_spans")),
                "best_split_symbol": source.get("best_split_symbol", ""),
                "best_split_source": source.get("best_split_source", ""),
                "blocker_class": blocker_class,
                "blocker_severity": severity,
                "next_gate": next_gate,
                "queue_state": "defer-current-branch" if blocker_class == "state-mode-workorder-branch-exhausted" else "actionable",
            }
        )
    rows.sort(key=queue_rank)
    status_counts = Counter(str(row.get("frontier_status", "")) for row in rows)
    blocker_counts = Counter(str(row.get("blocker_class", "")) for row in rows)
    actionable = [row for row in rows if row.get("queue_state") == "actionable"]
    best = actionable[0] if actionable else (rows[0] if rows else {})
    frontier_summary = frontier_json.get("summary", {}) if isinstance(frontier_json, dict) else {}
    summary = {
        "frontier_rows": len(rows),
        "open_functions": int_value(frontier_summary.get("open_functions")),
        "ready_to_promote_functions": int_value(frontier_summary.get("ready_to_promote_functions")),
        "status_counts": dict(sorted(status_counts.items())),
        "blocker_counts": dict(sorted(blocker_counts.items())),
        "state_mode_exhausted_entry": EXHAUSTED_ENTRY if exhausted.get("exhausted") else "",
        "state_mode_probe_rows": exhausted.get("probe_rows", 0),
        "state_mode_near_or_exact_rows": exhausted.get("near_or_exact_rows", 0),
        "state_mode_qualified_seed_rows": exhausted.get("qualified_seed_rows", 0),
        "state_mode_semantic_supported": exhausted.get("semantic_supported", 0),
        "state_mode_promotion_ready": exhausted.get("promotion_ready", 0),
        "next_recommended_entry": best.get("entry", ""),
        "next_recommended_name": best.get("oot3d_name", ""),
        "next_recommended_blocker": best.get("blocker_class", ""),
        "next_recommended_gate": best.get("next_gate", ""),
        "split_gap_top_blocker": (
            split_gap.get("summary", {}).get("top_blocker", "")
            if isinstance(split_gap.get("summary"), dict)
            else ""
        ),
        "promotion_ready": 0,
    }
    return {
        "format": "oot3d_direct_frontier_blocker_synthesis_v1",
        "inputs": {
            "frontier_csv": rel(args.frontier_csv),
            "frontier_json": rel(args.frontier_json),
            "split_gap": rel(args.split_gap),
            "state_mode_seed_qualification": rel(args.state_mode_seed_qualification),
            "state_mode_call_shape": rel(args.state_mode_call_shape),
        },
        "summary": summary,
        "state_mode_exhaustion": exhausted,
        "rows": rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Frontier Blocker Synthesis",
        "",
        "This report combines the C reconstruction frontier with negative branch evidence from the state/mode source workorder probes.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Frontier rows | {summary['frontier_rows']} |",
        f"| Open functions | {summary['open_functions']} |",
        f"| Ready to promote | {summary['ready_to_promote_functions']} |",
        f"| State/mode near/exact rows | {summary['state_mode_near_or_exact_rows']} |",
        f"| State/mode qualified seed rows | {summary['state_mode_qualified_seed_rows']} |",
        f"| Promotion-ready rows | {summary['promotion_ready']} |",
        "",
        "## Next Recommended Action",
        "",
        f"- Entry: `{summary['next_recommended_entry']}` `{summary['next_recommended_name']}`",
        f"- Blocker: `{summary['next_recommended_blocker']}`",
        f"- Gate: {summary['next_recommended_gate']}",
        "",
        "## Blockers",
        "",
        "| Blocker | Rows |",
        "| --- | ---: |",
    ]
    for blocker, count in summary["blocker_counts"].items():
        lines.append(f"| `{blocker}` | {count} |")
    lines.extend(
        [
            "",
            "## Queue",
            "",
            "| State | Entry | Name | Status | Blocker | Target insns | Next gate |",
            "| --- | --- | --- | --- | --- | ---: | --- |",
        ]
    )
    for row in data["rows"]:
        lines.append(
            f"| `{row.get('queue_state', '')}` | `{row.get('entry', '')}` | `{row.get('oot3d_name', '')}` | "
            f"`{row.get('frontier_status', '')}` | `{row.get('blocker_class', '')}` | "
            f"{row.get('target_instruction_count', 0)} | {row.get('next_gate', '')} |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frontier-csv", type=Path, default=DEFAULT_FRONTIER_CSV)
    parser.add_argument("--frontier-json", type=Path, default=DEFAULT_FRONTIER_JSON)
    parser.add_argument("--split-gap", type=Path, default=DEFAULT_SPLIT_GAP)
    parser.add_argument("--state-mode-seed-qualification", type=Path, default=DEFAULT_STATE_MODE_QUAL)
    parser.add_argument("--state-mode-call-shape", type=Path, default=DEFAULT_STATE_MODE_CALL_SHAPE)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()
    data = build_report(args)
    fields = [
        "queue_state",
        "entry",
        "oot3d_name",
        "n64_name",
        "port_file",
        "frontier_status",
        "target_instruction_count",
        "probe_category",
        "probe_lcs_ratio",
        "split_workorders",
        "complete_split_spans",
        "best_split_symbol",
        "best_split_source",
        "blocker_class",
        "blocker_severity",
        "next_gate",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct frontier blocker synthesis: "
        f"{summary['frontier_rows']} rows, next "
        f"{summary['next_recommended_entry']} {summary['next_recommended_name']}"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
