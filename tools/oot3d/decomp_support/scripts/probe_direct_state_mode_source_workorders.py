#!/usr/bin/env python3
"""Compile source-correlation workorders and compare them to OOT3D handler ranges."""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter
from pathlib import Path
from typing import Any

from compare_direct_packet_probe_matches import read_objdump_file
from compare_direct_target_window_probes import category_order
from compare_runtime_objects import normalize_op
from probe_direct_packet_compilability import find_tool, probe_prelude
from probe_direct_source_subregions import compile_probe, read_source_lines, rel, safe_name, source_for_probe
from sweep_direct_source_subregion_ranges import best_sliding_compare, float_value, int_value, list_value, repo_path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_WORKORDERS = ROOT / "analysis" / "direct_state_mode_source_workorders.json"
DEFAULT_DISASSEMBLY = ROOT / "ghidra_export" / "disassembly.txt"
DEFAULT_BUILD = ROOT / "build" / "direct_state_mode_source_workorder_probes"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_state_mode_source_workorder_probe.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_state_mode_source_workorder_probe.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_state_mode_source_workorder_probe.md"
INSN_RE = re.compile(r"^(?P<addr>[0-9a-fA-F]{8}):\s+(?P<op>.+?)\s*$")
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


def read_disassembly(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = INSN_RE.match(raw)
        if not match:
            continue
        op = match.group("op").strip()
        if op.startswith(".word"):
            continue
        rows.append({"addr": match.group("addr").lower(), "op": normalize_op(op)})
    return rows


def target_rows_for(disasm: list[dict[str, Any]], handler_range: str) -> list[dict[str, Any]]:
    match = re.match(r"^(?P<start>[0-9a-fA-F]+)-(?P<end>[0-9a-fA-F]+)$", handler_range)
    if not match:
        return []
    start = int(match.group("start"), 16)
    end = int(match.group("end"), 16)
    rows: list[dict[str, Any]] = []
    for ordinal, row in enumerate(disasm):
        addr = int(str(row.get("addr", "0")), 16)
        if start <= addr < end:
            rows.append({"ordinal": ordinal, "addr": row["addr"], "op": row["op"]})
    return rows


def compiled_ops_for(dump_path: Path, function_name: str) -> list[str]:
    if not dump_path.is_file():
        return []
    symbol = read_objdump_file(dump_path).get(function_name, {})
    return list(symbol.get("ops", [])) if isinstance(symbol, dict) else []


def parse_range(value: Any) -> tuple[int, int]:
    match = re.match(r"^(?P<start>\d+)-(?P<end>\d+)$", str(value or ""))
    if not match:
        return (0, 0)
    return int(match.group("start")), int(match.group("end"))


def source_lines_without_enclosing_function(source_lines: dict[int, str], start: int, end: int) -> dict[int, str]:
    selected = {line: source_lines[line] for line in range(start, end + 1) if line in source_lines}
    if not selected:
        return selected
    nonblank = [line for line, text in selected.items() if text.strip()]
    if not nonblank:
        return selected
    first = nonblank[0]
    last = nonblank[-1]
    if re.search(r"\b(Player_[A-Za-z0-9_]+|func_808[0-9A-Fa-f]+)\s*\([^;]*\)\s*\{\s*$", selected[first].strip()):
        selected[first] = ""
        if selected[last].strip() == "}":
            selected[last] = ""
    return selected


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    workorders = read_json(args.workorders, {})
    disasm = read_disassembly(args.disassembly)
    prelude = probe_prelude()
    gcc = find_tool("gcc", args.tool_prefix)
    objdump = find_tool("objdump", args.tool_prefix)
    rows: list[dict[str, Any]] = []

    for index_row in list_value(workorders.get("rows", []))[: args.limit]:
        if not isinstance(index_row, dict):
            continue
        workorder_dir = repo_path(index_row.get("workorder_dir", ""))
        manifest = read_json(workorder_dir / "workorder.json", {})
        source_excerpt = workorder_dir / "source_excerpt.c.txt"
        source_lines = read_source_lines(source_excerpt)
        source_start, source_end = parse_range(manifest.get("best_source_range"))
        source_lines = source_lines_without_enclosing_function(source_lines, source_start, source_end)
        target_rows = target_rows_for(disasm, str(manifest.get("handler_range", "")))
        function_name = safe_name(
            f"oot3d_smwo_{manifest.get('handler', '')}_{manifest.get('states', '')}_{manifest.get('best_source_function', '')}_{source_start}_{source_end}"
        )
        source_text = source_for_probe(prelude, function_name, source_lines, source_start, source_end)
        compile_row = compile_probe(source_text, function_name, args.build_dir, gcc, objdump, args.optimization)
        compiled_ops = (
            compiled_ops_for(repo_path(compile_row.get("dump", "")), function_name)
            if compile_row.get("dumped")
            else []
        )
        compare_row = (
            best_sliding_compare(target_rows, compiled_ops, args.min_slice_insns, args.max_compiled_skip)
            if compiled_ops and target_rows
            else {"category": "no-valid-slice"}
        )
        rows.append(
            {
                "workorder_id": manifest.get("workorder_id", ""),
                "function_name": function_name,
                "handler": manifest.get("handler", ""),
                "handler_range": manifest.get("handler_range", ""),
                "states": manifest.get("states", ""),
                "feature_tags": manifest.get("feature_tags", ""),
                "best_source_function": manifest.get("best_source_function", ""),
                "best_source_range": manifest.get("best_source_range", ""),
                "source_start_line": source_start,
                "source_end_line": source_end,
                "source_line_count": max(0, source_end - source_start + 1),
                "correlation_score": manifest.get("best_score", 0.0),
                "matched_weight": manifest.get("matched_weight", 0),
                "matched_tokens": manifest.get("matched_tokens", ""),
                "target_instruction_count": len(target_rows),
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
                "promotion_state": (
                    "split-seed-candidate"
                    if str(compare_row.get("category", "")) in NEAR_CATEGORIES
                    else "probe-evidence-only"
                ),
            }
        )

    rows.sort(
        key=lambda row: (
            not bool(row.get("compiled")),
            category_order(str(row.get("category", ""))),
            -float_value(row.get("lcs_window_ratio")),
            -int_value(row.get("compare_instruction_count")),
            str(row.get("workorder_id", "")),
        )
    )
    counts = Counter(str(row.get("category", "")) for row in rows if row.get("category"))
    promotion_counts = Counter(str(row.get("promotion_state", "")) for row in rows if row.get("promotion_state"))
    best = rows[0] if rows else {}
    summary = {
        "workorders": len(rows),
        "compiled": sum(1 for row in rows if row.get("compiled")),
        "dumped": sum(1 for row in rows if row.get("dumped")),
        "failed": sum(1 for row in rows if not row.get("compiled")),
        "near_or_exact": sum(1 for row in rows if str(row.get("category", "")) in NEAR_CATEGORIES),
        "categories": dict(sorted(counts.items())),
        "promotion_states": dict(sorted(promotion_counts.items())),
        "split_seed_candidates": promotion_counts.get("split-seed-candidate", 0),
        "best_workorder": best.get("workorder_id", ""),
        "best_handler": best.get("handler", ""),
        "best_source_function": best.get("best_source_function", ""),
        "best_source_range": best.get("best_source_range", ""),
        "best_category": best.get("category", ""),
        "best_target_start_addr": best.get("target_start_addr", ""),
        "best_compiled_skip": best.get("compiled_skip", 0),
        "best_lcs_window_ratio": best.get("lcs_window_ratio", 0.0),
        "gcc": gcc,
        "objdump": objdump,
        "optimization": args.optimization,
        "build_dir": rel(args.build_dir),
        "next_gate": "Qualify near/exact handler/source probes before assigning state names or promoting C.",
    }
    return {
        "format": "oot3d_direct_state_mode_source_workorder_probe_v1",
        "inputs": {
            "workorders": rel(args.workorders),
            "disassembly": rel(args.disassembly),
        },
        "summary": summary,
        "rows": rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct State/Mode Source Workorder Probe",
        "",
        "This report compiles the materialized handler/source workorders and slides each compiled source body across the OOT3D handler range.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Workorders | {summary['workorders']} |",
        f"| Compiled | {summary['compiled']} |",
        f"| Dumped | {summary['dumped']} |",
        f"| Failed | {summary['failed']} |",
        f"| Near/exact | {summary['near_or_exact']} |",
        f"| Split seed candidates | {summary['split_seed_candidates']} |",
        f"| Best LCS | {float_value(summary['best_lcs_window_ratio']):.4f} |",
        "",
        f"Next gate: {summary['next_gate']}",
        "",
        "## Top Probes",
        "",
        "| Category | State | Workorder | Handler | Source | Target start | Skip | Compare | LCS | Run | First difference |",
        "| --- | --- | --- | --- | --- | --- | ---: | ---: | ---: | --- | --- |",
    ]
    for row in data["rows"]:
        source = f"{row.get('best_source_function', '')}:{row.get('best_source_range', '')}"
        lines.append(
            f"| `{row.get('category', '')}` | `{row.get('promotion_state', '')}` | "
            f"`{row.get('workorder_id', '')}` | `{row.get('handler', '')}` | `{source}` | "
            f"`{row.get('target_start_addr', '')}` | {row.get('compiled_skip', '')} | "
            f"{row.get('compare_instruction_count', '')} | {float_value(row.get('lcs_window_ratio')):.4f} | "
            f"{row.get('longest_common_run', '')} | `{row.get('first_difference', '')}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workorders", type=Path, default=DEFAULT_WORKORDERS)
    parser.add_argument("--disassembly", type=Path, default=DEFAULT_DISASSEMBLY)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--optimization", default="-O2")
    parser.add_argument("--tool-prefix", default="arm-none-eabi")
    parser.add_argument("--limit", type=int, default=12)
    parser.add_argument("--min-slice-insns", type=int, default=6)
    parser.add_argument("--max-compiled-skip", type=int, default=32)
    args = parser.parse_args()
    for path in (args.workorders, args.disassembly):
        if not path.is_file():
            raise SystemExit(f"missing input: {path}")
    data = build_report(args)
    fields = [
        "workorder_id",
        "function_name",
        "handler",
        "handler_range",
        "states",
        "feature_tags",
        "best_source_function",
        "best_source_range",
        "source_start_line",
        "source_end_line",
        "source_line_count",
        "correlation_score",
        "matched_weight",
        "matched_tokens",
        "target_instruction_count",
        "compiled",
        "dumped",
        "category",
        "promotion_state",
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
        "direct state/mode source workorder probe: "
        f"{summary['compiled']}/{summary['workorders']} compiled, "
        f"{summary['near_or_exact']} near/exact, "
        f"best {summary['best_workorder']} {summary['best_category']} {summary['best_lcs_window_ratio']}"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
