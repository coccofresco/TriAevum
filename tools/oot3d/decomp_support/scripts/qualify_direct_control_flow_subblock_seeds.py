#!/usr/bin/env python3
"""Qualify near/exact control-flow subblock probe rows before split promotion."""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path
from typing import Any

from compare_direct_packet_probe_matches import read_objdump_file
from probe_direct_source_subregions import rel
from qualify_direct_source_subregion_seeds import (
    NEAR_CATEGORIES,
    classify_seed,
)
from sweep_direct_source_subregion_ranges import float_value, int_value, list_value, repo_path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PROBE = ROOT / "analysis" / "direct_control_flow_subblock_probe.json"
DEFAULT_WORKORDERS = ROOT / "analysis" / "direct_control_flow_isolation_workorders.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_control_flow_subblock_seed_qualification.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_control_flow_subblock_seed_qualification.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_control_flow_subblock_seed_qualification.md"


def read_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    return value if isinstance(value, dict) else default


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


def workorder_index(index: dict[str, Any]) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for row in list_value(index.get("rows", [])):
        if not isinstance(row, dict):
            continue
        path = repo_path(row.get("workorder_path", ""))
        data = read_json(path, {})
        name = str(data.get("workorder", "") or row.get("workorder", ""))
        if name:
            result[name] = data
    return result


def compiled_ops_for(row: dict[str, Any]) -> list[str]:
    dump_path = repo_path(row.get("dump", ""))
    if not dump_path.is_file():
        return []
    symbol = read_objdump_file(dump_path).get(str(row.get("function_name", "")), {})
    return list(symbol.get("ops", [])) if isinstance(symbol, dict) else []


def target_ops_for(row: dict[str, Any], workorders: dict[str, dict[str, Any]]) -> list[str]:
    workorder = workorders.get(str(row.get("workorder", "")), {})
    target_slice = list_value(workorder.get("target_slice", []))
    start = int_value(row.get("target_window_start_offset"))
    count = int_value(row.get("compare_instruction_count"))
    return [str(item.get("op", "")) for item in target_slice[start : start + count] if isinstance(item, dict)]


def qualify_rows(probe: dict[str, Any], workorders: dict[str, dict[str, Any]]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for row in list_value(probe.get("rows", [])):
        if not isinstance(row, dict):
            continue
        count = int_value(row.get("compare_instruction_count"))
        compiled_all_ops = compiled_ops_for(row)
        compiled_skip = int_value(row.get("compiled_skip"))
        compiled_ops = compiled_all_ops[compiled_skip : compiled_skip + count]
        target_ops = target_ops_for(row, workorders)
        seed_class, seed_reason, metrics = classify_seed(row, target_ops, compiled_ops)
        rows.append(
            {
                "seed_class": seed_class,
                "seed_reason": seed_reason,
                "workorder": row.get("workorder", ""),
                "entry": row.get("entry", ""),
                "oot3d_name": row.get("oot3d_name", ""),
                "candidate_symbol": row.get("candidate_symbol", ""),
                "function_name": row.get("function_name", ""),
                "source_start_line": row.get("source_start_line", ""),
                "source_end_line": row.get("source_end_line", ""),
                "source_range": f"{row.get('source_start_line', '')}-{row.get('source_end_line', '')}",
                "category": row.get("category", ""),
                "target_start_addr": row.get("target_start_addr", ""),
                "target_end_addr": row.get("target_end_addr", ""),
                "compiled_skip": row.get("compiled_skip", 0),
                "compare_instruction_count": row.get("compare_instruction_count", 0),
                "lcs_instruction_count": row.get("lcs_instruction_count", 0),
                "lcs_window_ratio": row.get("lcs_window_ratio", 0.0),
                "longest_common_run": row.get("longest_common_run", ""),
                "matching_prefix": row.get("matching_prefix", 0),
                "matching_suffix": row.get("matching_suffix", 0),
                "first_difference": row.get("first_difference", ""),
                "target_ops": "; ".join(target_ops),
                "compiled_ops": "; ".join(compiled_ops),
                **metrics,
                "probe_source": row.get("probe_source", ""),
                "dump": row.get("dump", ""),
            }
        )

    class_rank = {
        "candidate-control-flow-seed": 0,
        "tail-call-pattern-seed": 1,
        "structural-orientation-seed": 2,
        "wide-weak-orientation-seed": 3,
        "short-generic-near-risk": 4,
        "near-window-risk": 5,
        "not-near-window": 6,
    }
    rows.sort(
        key=lambda row: (
            class_rank.get(str(row.get("seed_class", "")), 99),
            -float_value(row.get("lcs_window_ratio")),
            -int_value(row.get("longest_common_run_length")),
            -int_value(row.get("compare_instruction_count")),
            int_value(row.get("source_start_line")),
            int_value(row.get("source_end_line")),
        )
    )
    return rows


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    probe = read_json(args.probe, {})
    workorders = workorder_index(read_json(args.workorders, {}))
    rows = qualify_rows(probe, workorders)
    classes = Counter(str(row.get("seed_class", "")) for row in rows)
    near_rows = [row for row in rows if str(row.get("category", "")) in NEAR_CATEGORIES]
    qualified_classes = {"candidate-control-flow-seed", "tail-call-pattern-seed", "structural-orientation-seed"}
    risk_classes = {"short-generic-near-risk", "near-window-risk"}
    qualified_rows = [row for row in rows if str(row.get("seed_class", "")) in qualified_classes]
    risk_rows = [row for row in rows if str(row.get("seed_class", "")) in risk_classes]
    best = qualified_rows[0] if qualified_rows else (near_rows[0] if near_rows else (rows[0] if rows else {}))
    summary = {
        "subblock_rows": len(rows),
        "near_or_exact_rows": len(near_rows),
        "qualified_seed_rows": len(qualified_rows),
        "risk_rows": len(risk_rows),
        "classes": dict(sorted(classes.items())),
        "best_seed_workorder": best.get("workorder", ""),
        "best_seed_entry": best.get("entry", ""),
        "best_seed_candidate_symbol": best.get("candidate_symbol", ""),
        "best_seed_source_range": best.get("source_range", ""),
        "best_seed_target_start_addr": best.get("target_start_addr", ""),
        "best_seed_compiled_skip": best.get("compiled_skip", 0),
        "best_seed_class": best.get("seed_class", ""),
        "best_seed_lcs_window_ratio": best.get("lcs_window_ratio", 0.0),
        "best_seed_longest_common_run": best.get("longest_common_run", ""),
        "next_gate": "Use qualified subblock seeds for a smaller branch-isolation probe; keep risk rows out of promotion.",
    }
    return {
        "format": "oot3d_direct_control_flow_subblock_seed_qualification_v1",
        "inputs": {
            "probe": rel(args.probe),
            "workorders": rel(args.workorders),
        },
        "summary": summary,
        "rows": rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Control-Flow Subblock Seed Qualification",
        "",
        "This report qualifies near/exact control-flow subblock rows before they are used as split seeds.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Subblock rows | {summary['subblock_rows']} |",
        f"| Near/exact rows | {summary['near_or_exact_rows']} |",
        f"| Qualified seed rows | {summary['qualified_seed_rows']} |",
        f"| Risk rows | {summary['risk_rows']} |",
        f"| Best seed LCS | {float_value(summary['best_seed_lcs_window_ratio']):.4f} |",
        "",
        "## Classes",
        "",
        "| Class | Rows |",
        "| --- | ---: |",
    ]
    for seed_class, count in summary["classes"].items():
        lines.append(f"| `{seed_class}` | {count} |")
    lines.extend(
        [
            "",
            "## Top Seeds",
            "",
            "| Class | Workorder | Source | Target start | Skip | Compare | LCS | Run | Generic | Reason |",
            "| --- | --- | ---: | --- | ---: | ---: | ---: | --- | ---: | --- |",
        ]
    )
    for row in data["rows"][:25]:
        lines.append(
            f"| `{row.get('seed_class', '')}` | `{row.get('workorder', '')}` | "
            f"{row.get('source_range', '')} | `{row.get('target_start_addr', '')}` | "
            f"{row.get('compiled_skip', '')} | {row.get('compare_instruction_count', '')} | "
            f"{float_value(row.get('lcs_window_ratio')):.4f} | {row.get('longest_common_run', '')} | "
            f"{float_value(row.get('generic_op_ratio')):.4f} | {row.get('seed_reason', '')} |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, default=DEFAULT_PROBE)
    parser.add_argument("--workorders", type=Path, default=DEFAULT_WORKORDERS)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()

    data = build_report(args)
    fields = [
        "seed_class",
        "seed_reason",
        "workorder",
        "entry",
        "oot3d_name",
        "candidate_symbol",
        "source_range",
        "category",
        "target_start_addr",
        "target_end_addr",
        "compiled_skip",
        "compare_instruction_count",
        "lcs_instruction_count",
        "lcs_window_ratio",
        "longest_common_run",
        "longest_common_run_length",
        "matching_prefix",
        "matching_suffix",
        "generic_op_ratio",
        "branch_call_ratio",
        "shared_specific_opcodes",
        "first_difference",
        "probe_source",
        "dump",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct control-flow subblock seed qualification: "
        f"{summary['qualified_seed_rows']}/{summary['near_or_exact_rows']} qualified, "
        f"{summary['risk_rows']} risk, "
        f"best {summary['best_seed_source_range']} {summary['best_seed_class']} "
        f"{summary['best_seed_lcs_window_ratio']}"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
