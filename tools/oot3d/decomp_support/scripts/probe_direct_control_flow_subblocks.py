#!/usr/bin/env python3
"""Probe focused source subblocks from direct control-flow isolation workorders."""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path
from typing import Any

from compare_direct_packet_probe_matches import read_objdump_file
from compare_direct_target_window_probes import category_order
from probe_direct_packet_compilability import find_tool, probe_prelude
from probe_direct_source_subregions import compile_probe, rel, safe_name, source_for_probe
from sweep_direct_source_subregion_ranges import best_sliding_compare, float_value, int_value, list_value, repo_path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_WORKORDERS = ROOT / "analysis" / "direct_control_flow_isolation_workorders.json"
DEFAULT_BUILD = ROOT / "build" / "direct_control_flow_subblocks"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_control_flow_subblock_probe.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_control_flow_subblock_probe.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_control_flow_subblock_probe.md"
NEAR_CATEGORIES = {"exact-window", "codegen-near-window", "structural-near-window"}


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


def source_dict(workorder: dict[str, Any]) -> dict[int, str]:
    result: dict[int, str] = {}
    for row in list_value(workorder.get("source_lines", [])):
        if isinstance(row, dict):
            result[int_value(row.get("line"))] = str(row.get("text", ""))
    return result


def target_rows(workorder: dict[str, Any]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for index, row in enumerate(list_value(workorder.get("target_slice", []))):
        if isinstance(row, dict):
            rows.append(
                {
                    "ordinal": index,
                    "index": row.get("index", ""),
                    "addr": row.get("addr", ""),
                    "op": row.get("op", ""),
                }
            )
    return rows


def candidate_ranges(workorder: dict[str, Any], max_line_count: int) -> list[dict[str, Any]]:
    lines = source_dict(workorder)
    if not lines:
        return []
    controls = [
        int_value(row.get("line"))
        for row in list_value(workorder.get("source_control_points", []))
        if isinstance(row, dict)
    ]
    starts = set(controls)
    ends = set(controls)
    all_lines = sorted(lines)
    for line in controls:
        starts.add(max(min(all_lines), line - 1))
        ends.add(min(max(all_lines), line + 1))
    # Include known structured spans in the magic action branch.
    for start, end in (
        (755, 759),
        (757, 759),
        (762, 771),
        (763, 771),
        (765, 771),
        (766, 768),
        (767, 768),
        (773, 781),
        (774, 781),
        (776, 781),
        (777, 781),
        (762, 781),
        (765, 781),
    ):
        starts.add(start)
        ends.add(end)
    ranges: set[tuple[int, int]] = set()
    for start in starts:
        for end in ends:
            if start not in lines or end not in lines or start > end:
                continue
            count = end - start + 1
            if 2 <= count <= max_line_count:
                ranges.add((start, end))
    return [
        {
            "source_start_line": start,
            "source_end_line": end,
            "source_line_count": end - start + 1,
        }
        for start, end in sorted(ranges)
    ]


def compiled_ops_for(dump_path: Path, function_name: str) -> list[str]:
    if not dump_path.is_file():
        return []
    symbol = read_objdump_file(dump_path).get(function_name, {})
    return list(symbol.get("ops", [])) if isinstance(symbol, dict) else []


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    index = read_json(args.workorders, {})
    prelude = probe_prelude()
    gcc = find_tool("gcc", args.tool_prefix)
    objdump = find_tool("objdump", args.tool_prefix)
    rows: list[dict[str, Any]] = []

    for index_row in list_value(index.get("rows", []))[: args.workorder_limit]:
        if not isinstance(index_row, dict):
            continue
        workorder_path = repo_path(index_row.get("workorder_path", ""))
        workorder = read_json(workorder_path, {})
        sources = source_dict(workorder)
        targets = target_rows(workorder)
        for candidate in candidate_ranges(workorder, args.max_line_count)[: args.max_ranges_per_workorder]:
            start_line = int_value(candidate.get("source_start_line"))
            end_line = int_value(candidate.get("source_end_line"))
            function_name = safe_name(
                f"oot3d_cfsub_{workorder.get('entry', '')}_{workorder.get('candidate_symbol', '')}_{start_line}_{end_line}_{workorder.get('target_range', '')}"
            )
            source_text = source_for_probe(prelude, function_name, sources, start_line, end_line)
            compile_row = compile_probe(source_text, function_name, args.build_dir, gcc, objdump, args.optimization)
            compiled_ops = (
                compiled_ops_for(repo_path(compile_row.get("dump", "")), function_name)
                if compile_row.get("dumped")
                else []
            )
            compare_row = (
                best_sliding_compare(targets, compiled_ops, args.min_slice_insns, args.max_compiled_skip)
                if compiled_ops
                else {}
            )
            rows.append(
                {
                    "workorder": workorder.get("workorder", ""),
                    "entry": workorder.get("entry", ""),
                    "oot3d_name": workorder.get("oot3d_name", ""),
                    "candidate_symbol": workorder.get("candidate_symbol", ""),
                    "function_name": function_name,
                    "source_start_line": start_line,
                    "source_end_line": end_line,
                    "source_line_count": candidate.get("source_line_count", 0),
                    "target_range": workorder.get("target_range", ""),
                    "compiled": bool(compile_row.get("compiled")),
                    "dumped": bool(compile_row.get("dumped")),
                    "primary_blocker": compile_row.get("primary_blocker", ""),
                    "error_count": int_value(compile_row.get("error_count")),
                    "warning_count": int_value(compile_row.get("warning_count")),
                    "compiled_instruction_count": len(compiled_ops),
                    **compare_row,
                    "probe_source": compile_row.get("probe_source", ""),
                    "object": compile_row.get("object", ""),
                    "dump": compile_row.get("dump", ""),
                    "stderr_log": compile_row.get("stderr_log", ""),
                }
            )

    rows.sort(
        key=lambda row: (
            not bool(row.get("compiled")),
            category_order(str(row.get("category", ""))),
            -float_value(row.get("lcs_window_ratio")),
            -int_value(row.get("compare_instruction_count")),
            int_value(row.get("source_start_line")),
            int_value(row.get("source_end_line")),
        )
    )
    categories = Counter(str(row.get("category", "")) for row in rows if row.get("category"))
    best = rows[0] if rows else {}
    summary = {
        "subblock_probes": len(rows),
        "compiled": sum(1 for row in rows if row.get("compiled")),
        "dumped": sum(1 for row in rows if row.get("dumped")),
        "failed": sum(1 for row in rows if not row.get("compiled")),
        "near_or_exact": sum(1 for row in rows if str(row.get("category", "")) in NEAR_CATEGORIES),
        "categories": dict(sorted(categories.items())),
        "best_workorder": best.get("workorder", ""),
        "best_entry": best.get("entry", ""),
        "best_candidate_symbol": best.get("candidate_symbol", ""),
        "best_source_range": f"{best.get('source_start_line', '')}-{best.get('source_end_line', '')}" if best else "",
        "best_category": best.get("category", ""),
        "best_target_start_addr": best.get("target_start_addr", ""),
        "best_compiled_skip": best.get("compiled_skip", 0),
        "best_lcs_window_ratio": best.get("lcs_window_ratio", 0.0),
        "gcc": gcc,
        "objdump": objdump,
        "optimization": args.optimization,
        "build_dir": rel(args.build_dir),
        "next_gate": "Use near/exact subblocks to split the control-flow workorder; otherwise inspect the best weak semantic subblock.",
    }
    return {
        "format": "oot3d_direct_control_flow_subblock_probe_v1",
        "inputs": {
            "workorders": rel(args.workorders),
        },
        "summary": summary,
        "rows": rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Control-Flow Subblock Probe",
        "",
        "This report compiles smaller source ranges from control-flow isolation workorders and slides them across the indexed target slice.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Subblock probes | {summary['subblock_probes']} |",
        f"| Compiled | {summary['compiled']} |",
        f"| Dumped | {summary['dumped']} |",
        f"| Near/exact | {summary['near_or_exact']} |",
        f"| Best LCS | {float_value(summary['best_lcs_window_ratio']):.4f} |",
        "",
        "## Top Subblocks",
        "",
        "| Category | Workorder | Source | Target start | Skip | Compare | LCS | Run | First difference |",
        "| --- | --- | ---: | --- | ---: | ---: | ---: | --- | --- |",
    ]
    for row in data["rows"][:25]:
        lines.append(
            f"| `{row.get('category', '')}` | `{row.get('workorder', '')}` | "
            f"{row.get('source_start_line', '')}-{row.get('source_end_line', '')} | "
            f"`{row.get('target_start_addr', '')}` | {row.get('compiled_skip', '')} | "
            f"{row.get('compare_instruction_count', '')} | {float_value(row.get('lcs_window_ratio')):.4f} | "
            f"{row.get('longest_common_run', '')} | `{row.get('first_difference', '')}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workorders", type=Path, default=DEFAULT_WORKORDERS)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--optimization", default="-O2")
    parser.add_argument("--tool-prefix", default="arm-none-eabi")
    parser.add_argument("--workorder-limit", type=int, default=1)
    parser.add_argument("--max-ranges-per-workorder", type=int, default=140)
    parser.add_argument("--max-line-count", type=int, default=24)
    parser.add_argument("--min-slice-insns", type=int, default=4)
    parser.add_argument("--max-compiled-skip", type=int, default=28)
    args = parser.parse_args()

    data = build_report(args)
    fields = [
        "compiled",
        "dumped",
        "category",
        "workorder",
        "entry",
        "oot3d_name",
        "candidate_symbol",
        "function_name",
        "source_start_line",
        "source_end_line",
        "source_line_count",
        "target_start_addr",
        "target_end_addr",
        "target_window_start_offset",
        "target_window_end_offset",
        "compiled_skip",
        "compiled_instruction_count",
        "compare_instruction_count",
        "lcs_instruction_count",
        "lcs_window_ratio",
        "matching_prefix",
        "matching_suffix",
        "longest_common_run",
        "first_difference",
        "primary_blocker",
        "error_count",
        "warning_count",
        "probe_source",
        "object",
        "dump",
        "stderr_log",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct control-flow subblock probe: "
        f"{summary['compiled']}/{summary['subblock_probes']} compiled, "
        f"{summary['near_or_exact']} near/exact, "
        f"best {summary['best_source_range']} {summary['best_category']} "
        f"{summary['best_lcs_window_ratio']}"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
