#!/usr/bin/env python3
"""Build adapter/boundary workorders from refined target-window matches."""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_REFINEMENT = ROOT / "analysis" / "direct_target_window_refinement.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_window_adapter_queue.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_window_adapter_queue.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_window_adapter_queue.md"
PROMOTION_READY_CATEGORIES = {"exact-window", "codegen-near-window", "structural-near-window"}


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


def blocker_class(row: dict[str, Any]) -> str:
    category = str(row.get("refined_category", ""))
    if category in PROMOTION_READY_CATEGORIES:
        return "promotion-ready-window"
    if category == "weak-window":
        return "candidate-or-boundary-weak"

    compiled_skip = int_value(row.get("compiled_skip"))
    start_delta = int_value(row.get("target_start_delta"))
    lcs = float_value(row.get("refined_lcs_window_ratio"))
    delta = float_value(row.get("lcs_window_ratio_delta"))
    prefix = int_value(row.get("matching_prefix"))
    first_difference = str(row.get("first_difference", ""))

    if compiled_skip >= 16 and abs(start_delta) >= 16 and delta > 0.0:
        return "boundary-and-prologue-adapter"
    if compiled_skip >= 16 and delta > 0.0:
        return "compiled-prologue-adapter"
    if abs(start_delta) >= 16 and delta > 0.0:
        return "target-boundary-refinement"
    if prefix > 0 and lcs >= 0.18:
        return "partial-prefix-shape"
    if first_difference.startswith("0:") and (" vs bl <target>" in first_difference or "bl <target> vs " in first_difference):
        return "call-shape-mismatch"
    if lcs >= 0.20:
        return "semantic-shape-gap"
    return "low-signal-window"


def next_gate(blocker: str) -> str:
    gates = {
        "promotion-ready-window": "Review as a split seed; promote only behind the maintained C gate.",
        "candidate-or-boundary-weak": "Search alternate candidate symbols or widen/narrow the target window before adapter work.",
        "boundary-and-prologue-adapter": "Materialize a focused probe with the refined target boundary and helper prologue skipped.",
        "compiled-prologue-adapter": "Add a helper-body comparison path that strips compiler prologue/epilogue noise.",
        "target-boundary-refinement": "Use the refined target start/end as the next target-window seed.",
        "partial-prefix-shape": "Inspect local control-flow shape around the matching prefix and add shape anchors.",
        "call-shape-mismatch": "Resolve inlined call/helper differences before attempting source promotion.",
        "semantic-shape-gap": "Mine shape anchors and adapter rewrites for the matched helper body.",
        "low-signal-window": "Keep as evidence only; improve candidate selection first.",
    }
    return gates.get(blocker, "Keep blocked until the refined compare reaches structural-near or better.")


def priority_score(row: dict[str, Any], blocker: str) -> int:
    score = int_value(row.get("priority"))
    score += int(round(float_value(row.get("refined_lcs_window_ratio")) * 100))
    score += max(0, int(round(float_value(row.get("lcs_window_ratio_delta")) * 100)))
    if blocker in {"boundary-and-prologue-adapter", "compiled-prologue-adapter", "target-boundary-refinement"}:
        score += 15
    if str(row.get("entry")) in {"00473ef8", "00250ad0"}:
        score += 5
    return score


def build_queue(refinement: dict[str, Any]) -> dict[str, Any]:
    rows: list[dict[str, Any]] = []
    for row in list_value(refinement.get("rows", [])):
        if not isinstance(row, dict):
            continue
        blocker = blocker_class(row)
        rows.append(
            {
                "entry": str(row.get("entry", "")).lower(),
                "oot3d_name": row.get("oot3d_name", ""),
                "candidate_symbol": row.get("candidate_symbol", ""),
                "cluster_index": int_value(row.get("cluster_index")),
                "blocker_class": blocker,
                "work_priority": priority_score(row, blocker),
                "refined_category": row.get("refined_category", ""),
                "original_category": row.get("original_category", ""),
                "original_lcs_window_ratio": float_value(row.get("original_lcs_window_ratio")),
                "refined_lcs_window_ratio": float_value(row.get("refined_lcs_window_ratio")),
                "lcs_window_ratio_delta": float_value(row.get("lcs_window_ratio_delta")),
                "target_start_delta": int_value(row.get("target_start_delta")),
                "compiled_skip": int_value(row.get("compiled_skip")),
                "matching_prefix": int_value(row.get("matching_prefix")),
                "matching_suffix": int_value(row.get("matching_suffix")),
                "longest_common_run": row.get("longest_common_run", ""),
                "first_difference": row.get("first_difference", ""),
                "original_window": f"{row.get('original_window_start_addr', '')}-{row.get('original_window_end_addr', '')}",
                "refined_window": f"{row.get('refined_start_addr', '')}-{row.get('refined_end_addr', '')}",
                "compiled_slice_instruction_count": int_value(row.get("compiled_slice_instruction_count")),
                "refined_window_instruction_count": int_value(row.get("refined_window_instruction_count")),
                "source": row.get("source", ""),
                "next_gate": next_gate(blocker),
                "promotion_ready": blocker == "promotion-ready-window",
            }
        )
    rows.sort(
        key=lambda row: (
            bool(row["promotion_ready"]) is False,
            -int(row["work_priority"]),
            str(row["entry"]),
            str(row["candidate_symbol"]),
        )
    )
    blockers = Counter(str(row["blocker_class"]) for row in rows)
    ready = [row for row in rows if row["promotion_ready"]]
    summary = {
        "workorders": len(rows),
        "entries": len({row["entry"] for row in rows if row["entry"]}),
        "promotion_ready_workorders": len(ready),
        "blocked_workorders": len(rows) - len(ready),
        "improved_workorders": sum(1 for row in rows if float_value(row["lcs_window_ratio_delta"]) > 0.0),
        "blocker_classes": dict(sorted(blockers.items())),
        "top_entry": rows[0]["entry"] if rows else "",
        "top_candidate_symbol": rows[0]["candidate_symbol"] if rows else "",
        "top_blocker_class": rows[0]["blocker_class"] if rows else "",
        "top_refined_lcs_window_ratio": rows[0]["refined_lcs_window_ratio"] if rows else 0.0,
        "next_gate": "Run focused adapter/body probes for boundary/prologue workorders; do not promote semantic-only windows.",
    }
    return {
        "format": "oot3d_direct_window_adapter_queue_v1",
        "summary": summary,
        "rows": rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Window Adapter Queue",
        "",
        "This queue turns refined target-window matches into concrete boundary/adapter workorders.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Workorders | {summary['workorders']} |",
        f"| Entries | {summary['entries']} |",
        f"| Promotion-ready workorders | {summary['promotion_ready_workorders']} |",
        f"| Blocked workorders | {summary['blocked_workorders']} |",
        f"| Improved workorders | {summary['improved_workorders']} |",
        "",
        "## Blocker Classes",
        "",
        "| Class | Rows |",
        "| --- | ---: |",
    ]
    for blocker, count in summary["blocker_classes"].items():
        lines.append(f"| `{blocker}` | {count} |")
    lines.extend(
        [
            "",
            "## Queue",
            "",
            "| Blocker | OOT3D | Candidate | Priority | Refined window | Shift/skip | LCS old/new | First difference | Next gate |",
            "| --- | --- | --- | ---: | --- | ---: | ---: | --- | --- |",
        ]
    )
    for row in data["rows"]:
        lines.append(
            f"| `{row['blocker_class']}` | `{row['entry']}` `{row['oot3d_name']}` | "
            f"`{row['candidate_symbol']}` | {row['work_priority']} | `{row['refined_window']}` | "
            f"{row['target_start_delta']}/{row['compiled_skip']} | "
            f"{row['original_lcs_window_ratio']:.4f}/{row['refined_lcs_window_ratio']:.4f} | "
            f"`{row['first_difference']}` | {row['next_gate']} |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--refinement", type=Path, default=DEFAULT_REFINEMENT)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()

    data = build_queue(read_json(args.refinement, {}))
    fields = [
        "entry",
        "oot3d_name",
        "candidate_symbol",
        "cluster_index",
        "blocker_class",
        "work_priority",
        "refined_category",
        "original_category",
        "original_lcs_window_ratio",
        "refined_lcs_window_ratio",
        "lcs_window_ratio_delta",
        "target_start_delta",
        "compiled_skip",
        "matching_prefix",
        "matching_suffix",
        "longest_common_run",
        "first_difference",
        "original_window",
        "refined_window",
        "compiled_slice_instruction_count",
        "refined_window_instruction_count",
        "source",
        "next_gate",
        "promotion_ready",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct window adapter queue: "
        f"{summary['workorders']} workorders, {summary['promotion_ready_workorders']} promotion-ready, "
        f"top {summary['top_entry']} {summary['top_candidate_symbol']} {summary['top_blocker_class']}"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
