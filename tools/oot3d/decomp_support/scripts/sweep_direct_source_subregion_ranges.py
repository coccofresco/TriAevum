#!/usr/bin/env python3
"""Sweep wider source subregions and slide them across the corrected target window."""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter
from pathlib import Path
from typing import Any

from compare_direct_packet_probe_matches import read_objdump_file
from compare_direct_target_window_probes import category_order, classify_window, first_difference_text, run_text
from compare_runtime_objects import compare_ops
from probe_direct_packet_compilability import find_tool, probe_prelude
from probe_direct_source_subregions import (
    compile_probe,
    read_source_lines,
    rel,
    safe_name,
    source_for_probe,
)
from refine_direct_data_like_boundaries import read_indexed_ops


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PLAN = ROOT / "analysis" / "direct_source_subregion_plan.json"
DEFAULT_SEMANTIC = ROOT / "analysis" / "direct_semantic_call_shape.json"
DEFAULT_BUILD = ROOT / "build" / "direct_source_subregion_sweep"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_source_subregion_sweep.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_source_subregion_sweep.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_source_subregion_sweep.md"


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


def repo_path(value: Any) -> Path:
    path = Path(str(value or ""))
    return path if path.is_absolute() else ROOT / path


def semantic_index(data: dict[str, Any]) -> dict[tuple[str, str], dict[str, Any]]:
    result: dict[tuple[str, str], dict[str, Any]] = {}
    for row in list_value(data.get("rows", [])):
        if isinstance(row, dict):
            result[(str(row.get("entry", "")).lower(), str(row.get("candidate_symbol", "")))] = row
    return result


def target_rows_for(row: dict[str, Any]) -> list[dict[str, Any]]:
    source_excerpt = repo_path(row.get("source_excerpt", ""))
    manifest = read_json(source_excerpt.with_name("probe_manifest.json"), {})
    target_rows = read_indexed_ops(repo_path(manifest.get("target_window_ops", "")))
    target_skip = int_value(row.get("target_skip"))
    count = int_value(row.get("slice_instruction_count"))
    return target_rows[target_skip : target_skip + count]


def control_lines(source_lines: dict[int, str]) -> list[int]:
    lines: set[int] = set()
    for line, text in source_lines.items():
        stripped = text.strip()
        if not stripped:
            continue
        if (
            stripped.startswith("if ")
            or stripped.startswith("if (")
            or stripped.startswith("} else")
            or stripped.startswith("else ")
            or re.search(r"\w+\(.*\);$", stripped)
            or "++" in stripped
            or "+=" in stripped
        ):
            lines.add(line)
    return sorted(lines)


def candidate_ranges(source_lines: dict[int, str], anchor_line: int, max_line_count: int) -> list[dict[str, int]]:
    lines = sorted(source_lines)
    if not lines:
        return []
    min_line = min(lines)
    max_line = max(lines)
    starts = {min_line, anchor_line}
    ends = {max_line}
    controls = control_lines(source_lines)
    for line in controls:
        if anchor_line - 36 <= line <= anchor_line + 56:
            starts.add(line)
            ends.add(line)
            ends.add(line - 1)
    # Include known enclosing spans around the magic action helper.
    for start, end in ((754, 782), (754, 803), (754, 839), (776, 803), (776, 839), (784, 803), (804, 839)):
        starts.add(start)
        ends.add(end)
    ranges: set[tuple[int, int]] = set()
    for start in starts:
        for end in ends:
            if start < min_line or end > max_line or start > end:
                continue
            count = end - start + 1
            if 2 <= count <= max_line_count:
                ranges.add((start, end))
    return [{"source_start_line": start, "source_end_line": end} for start, end in sorted(ranges)]


def best_sliding_compare(
    target_rows: list[dict[str, Any]],
    compiled_ops: list[str],
    min_slice_insns: int,
    max_compiled_skip: int,
) -> dict[str, Any]:
    target_ops = [str(row.get("op", "")) for row in target_rows]
    if not target_ops or not compiled_ops:
        return {"category": "no-ops"}
    best: dict[str, Any] | None = None
    best_score: tuple[int, float, int, int, int, int] | None = None
    for compiled_skip in range(0, min(max_compiled_skip, len(compiled_ops) - min_slice_insns) + 1):
        compiled_slice = compiled_ops[compiled_skip:]
        max_start = max(0, len(target_ops) - min(min_slice_insns, len(compiled_slice)))
        for target_start in range(0, max_start + 1):
            available = min(len(compiled_slice), len(target_ops) - target_start)
            if available < min_slice_insns:
                continue
            compare = compare_ops(target_ops[target_start : target_start + available], compiled_slice[:available])
            category = classify_window(compare)
            run = compare.get("longest_common_run", {})
            run_length = int_value(run.get("length")) if isinstance(run, dict) else 0
            score = (
                -category_order(category),
                float_value(compare.get("lcs_target_ratio")),
                run_length,
                int_value(compare.get("matching_prefix")),
                int_value(compare.get("matching_suffix")),
                -(target_start + compiled_skip),
            )
            if best_score is None or score > best_score:
                target_end = target_start + available - 1
                best_score = score
                best = {
                    "category": category,
                    "target_window_start_offset": target_start,
                    "target_window_end_offset": target_end,
                    "target_start_addr": target_rows[target_start].get("addr", ""),
                    "target_end_addr": target_rows[target_end].get("addr", ""),
                    "compiled_skip": compiled_skip,
                    "compare_instruction_count": available,
                    "matching_prefix": int_value(compare.get("matching_prefix")),
                    "matching_suffix": int_value(compare.get("matching_suffix")),
                    "lcs_instruction_count": int_value(compare.get("lcs_instruction_count")),
                    "lcs_window_ratio": float_value(compare.get("lcs_target_ratio")),
                    "longest_common_run": run_text(compare),
                    "first_difference": first_difference_text(compare),
                    "exact_match": bool(compare.get("exact_match")),
                }
    return best or {"category": "no-valid-slice"}


def compiled_ops_for(dump_path: Path, function_name: str) -> list[str]:
    if not dump_path.is_file():
        return []
    symbols = read_objdump_file(dump_path)
    row = symbols.get(function_name)
    if not isinstance(row, dict):
        return []
    return list(row.get("ops", []))


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    plan = read_json(args.plan, {})
    semantic_rows = semantic_index(read_json(args.semantic, {}))
    prelude = probe_prelude()
    gcc = find_tool("gcc", args.tool_prefix)
    objdump = find_tool("objdump", args.tool_prefix)
    rows: list[dict[str, Any]] = []

    for plan_row in list_value(plan.get("rows", [])):
        if not isinstance(plan_row, dict):
            continue
        key = (str(plan_row.get("entry", "")).lower(), str(plan_row.get("candidate_symbol", "")))
        semantic_row = semantic_rows.get(key, {})
        target_rows = target_rows_for(semantic_row)
        source_lines = read_source_lines(repo_path(plan_row.get("source_excerpt", "")))
        anchor_line = int_value(plan_row.get("anchor_source_line"))
        ranges = candidate_ranges(source_lines, anchor_line, args.max_line_count)
        for suggested in ranges[: args.max_ranges]:
            start_line = int_value(suggested.get("source_start_line"))
            end_line = int_value(suggested.get("source_end_line"))
            function_name = safe_name(
                f"oot3d_sweep_{plan_row.get('entry', '')}_{plan_row.get('candidate_symbol', '')}_{start_line}_{end_line}"
            )
            source_text = source_for_probe(prelude, function_name, source_lines, start_line, end_line)
            compile_row = compile_probe(source_text, function_name, args.build_dir, gcc, objdump, args.optimization)
            compiled_ops = compiled_ops_for(repo_path(compile_row.get("dump", "")), function_name) if compile_row.get("dumped") else []
            compare_row = (
                best_sliding_compare(target_rows, compiled_ops, args.min_slice_insns, args.max_compiled_skip)
                if compiled_ops
                else {}
            )
            rows.append(
                {
                    "entry": plan_row.get("entry", ""),
                    "oot3d_name": plan_row.get("oot3d_name", ""),
                    "candidate_symbol": plan_row.get("candidate_symbol", ""),
                    "function_name": function_name,
                    "source_start_line": start_line,
                    "source_end_line": end_line,
                    "source_line_count": max(0, end_line - start_line + 1),
                    "anchor_source_line": anchor_line,
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
            int_value(row.get("source_line_count")),
            int_value(row.get("source_start_line")),
        )
    )
    near_categories = {"exact-window", "codegen-near-window", "structural-near-window"}
    counts = Counter(str(row.get("category", "")) for row in rows if row.get("category"))
    summary = {
        "sweep_ranges": len(rows),
        "compiled": sum(1 for row in rows if row.get("compiled")),
        "dumped": sum(1 for row in rows if row.get("dumped")),
        "failed": sum(1 for row in rows if not row.get("compiled")),
        "near_or_exact": sum(1 for row in rows if str(row.get("category", "")) in near_categories),
        "categories": dict(sorted(counts.items())),
        "best_entry": rows[0].get("entry", "") if rows else "",
        "best_candidate_symbol": rows[0].get("candidate_symbol", "") if rows else "",
        "best_source_range": f"{rows[0].get('source_start_line', '')}-{rows[0].get('source_end_line', '')}" if rows else "",
        "best_category": rows[0].get("category", "") if rows else "",
        "best_lcs_window_ratio": rows[0].get("lcs_window_ratio", 0.0) if rows else 0.0,
        "best_target_start_addr": rows[0].get("target_start_addr", "") if rows else "",
        "best_compiled_skip": rows[0].get("compiled_skip", 0) if rows else 0,
        "gcc": gcc,
        "objdump": objdump,
        "optimization": args.optimization,
        "build_dir": rel(args.build_dir),
        "max_compiled_skip": args.max_compiled_skip,
        "next_gate": "Promote only near/exact sweep ranges; otherwise use best weak/semantic range to relocate source search.",
    }
    return {
        "format": "oot3d_direct_source_subregion_sweep_v1",
        "summary": summary,
        "rows": rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Source Subregion Sweep",
        "",
        "This report compiles wider source ranges and slides each compiled body across the corrected target window.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Sweep ranges | {summary['sweep_ranges']} |",
        f"| Compiled | {summary['compiled']} |",
        f"| Dumped | {summary['dumped']} |",
        f"| Near/exact | {summary['near_or_exact']} |",
        f"| Best LCS | {float_value(summary['best_lcs_window_ratio']):.4f} |",
        "",
        "## Top Ranges",
        "",
        "| Category | OOT3D | Candidate | Source range | Target start | Skip | LCS | Run | First difference |",
        "| --- | --- | --- | ---: | --- | ---: | ---: | --- | --- |",
    ]
    for row in data["rows"][:25]:
        lines.append(
            f"| `{row.get('category', '')}` | `{row.get('entry', '')}` `{row.get('oot3d_name', '')}` | "
            f"`{row.get('candidate_symbol', '')}` | {row.get('source_start_line', '')}-{row.get('source_end_line', '')} | "
            f"`{row.get('target_start_addr', '')}` | {row.get('compiled_skip', '')} | "
            f"{float_value(row.get('lcs_window_ratio')):.4f} | "
            f"{row.get('longest_common_run', '')} | `{row.get('first_difference', '')}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, default=DEFAULT_PLAN)
    parser.add_argument("--semantic", type=Path, default=DEFAULT_SEMANTIC)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--optimization", default="-O2")
    parser.add_argument("--tool-prefix", default="arm-none-eabi")
    parser.add_argument("--max-ranges", type=int, default=80)
    parser.add_argument("--max-line-count", type=int, default=90)
    parser.add_argument("--min-slice-insns", type=int, default=8)
    parser.add_argument("--max-compiled-skip", type=int, default=24)
    args = parser.parse_args()

    data = build_report(args)
    fields = [
        "compiled",
        "dumped",
        "category",
        "entry",
        "oot3d_name",
        "candidate_symbol",
        "function_name",
        "source_start_line",
        "source_end_line",
        "source_line_count",
        "anchor_source_line",
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
        "direct source subregion sweep: "
        f"{summary['compiled']}/{summary['sweep_ranges']} compiled, "
        f"{summary['near_or_exact']} near/exact, "
        f"best {summary['best_entry']} {summary['best_candidate_symbol']} "
        f"{summary['best_source_range']} {summary['best_category']} {summary['best_lcs_window_ratio']}"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
