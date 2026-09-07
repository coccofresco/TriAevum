#!/usr/bin/env python3
"""Analyze semantic/call-shape blockers for refined direct-window packets."""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter
from pathlib import Path
from typing import Any

from analyze_direct_window_adapter_probe_diagnostics import op_class_counts
from compare_runtime_objects import compare_ops
from refine_direct_data_like_boundaries import read_indexed_ops


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BOUNDARIES = ROOT / "analysis" / "direct_data_like_boundary_refinement.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_semantic_call_shape.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_semantic_call_shape.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_semantic_call_shape.md"
CALL_RE = re.compile(r"^bl\b")
BRANCH_RE = re.compile(r"^b(?!l)\w*\b")
MEM_OFFSET_RE = re.compile(r"\[(?P<base>r\d+|sp)(?:,#(?P<offset>-?\d+))?\]")


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


def op_text(rows: list[dict[str, Any]]) -> list[str]:
    return [str(row.get("op", "")) for row in rows]


def slice_rows(rows: list[dict[str, Any]], start: int, count: int) -> list[dict[str, Any]]:
    return rows[start : start + count]


def call_contexts(rows: list[dict[str, Any]], limit: int) -> list[str]:
    contexts: list[str] = []
    for index, row in enumerate(rows):
        op = str(row.get("op", ""))
        if not CALL_RE.match(op):
            continue
        prev_op = str(rows[index - 1].get("op", "")) if index > 0 else ""
        next_op = str(rows[index + 1].get("op", "")) if index + 1 < len(rows) else ""
        addr = str(row.get("addr", ""))
        contexts.append(f"{index}@{addr}: {prev_op} | {op} | {next_op}")
        if len(contexts) >= limit:
            break
    return contexts


def branch_contexts(rows: list[dict[str, Any]], limit: int) -> list[str]:
    contexts: list[str] = []
    for index, row in enumerate(rows):
        op = str(row.get("op", ""))
        if not BRANCH_RE.match(op):
            continue
        prev_op = str(rows[index - 1].get("op", "")) if index > 0 else ""
        addr = str(row.get("addr", ""))
        contexts.append(f"{index}@{addr}: {prev_op} | {op}")
        if len(contexts) >= limit:
            break
    return contexts


def memory_offset_counts(ops: list[str]) -> dict[str, int]:
    counts: Counter[str] = Counter()
    for op in ops:
        for match in MEM_OFFSET_RE.finditer(op):
            base = match.group("base")
            offset = match.group("offset") or "0"
            counts[f"{base}:{offset}"] += 1
    return dict(sorted(counts.items()))


def common_runs(left: list[str], right: list[str], min_length: int, limit: int) -> list[dict[str, Any]]:
    runs: list[dict[str, Any]] = []
    previous = [0] * (len(right) + 1)
    for left_index, left_op in enumerate(left, start=1):
        current = [0] * (len(right) + 1)
        for right_index, right_op in enumerate(right, start=1):
            if left_op != right_op:
                continue
            length = previous[right_index - 1] + 1
            current[right_index] = length
            if length >= min_length:
                runs.append(
                    {
                        "length": length,
                        "target_index": left_index - length,
                        "compiled_index": right_index - length,
                    }
                )
        previous = current
    dedup: dict[tuple[int, int], dict[str, Any]] = {}
    for run in runs:
        key = (int_value(run["target_index"]), int_value(run["compiled_index"]))
        if key not in dedup or int_value(run["length"]) > int_value(dedup[key]["length"]):
            dedup[key] = run
    result = sorted(dedup.values(), key=lambda row: (-int_value(row["length"]), int_value(row["target_index"])))
    return result[:limit]


def blocker_class(row: dict[str, Any], target_classes: dict[str, int], compiled_classes: dict[str, int]) -> str:
    compiled_extra_skip = int_value(row.get("compiled_extra_skip"))
    call_delta = abs(target_classes.get("call", 0) - compiled_classes.get("call", 0))
    store_delta = abs(target_classes.get("store", 0) - compiled_classes.get("store", 0))
    load_delta = abs(target_classes.get("load", 0) - compiled_classes.get("load", 0))
    if compiled_extra_skip >= 16:
        return "compiled-subregion-alignment"
    if call_delta >= 3:
        return "call-count-delta"
    if load_delta + store_delta >= 8:
        return "memory-shape-delta"
    return "semantic-body-shape-gap"


def next_gate(blocker: str) -> str:
    gates = {
        "compiled-subregion-alignment": "Split the N64 source/helper body around the aligned subregion before comparing again.",
        "call-count-delta": "Resolve target calls against N64 helper calls or add explicit call adapters.",
        "memory-shape-delta": "Map struct/register offsets before source-shape promotion.",
        "semantic-body-shape-gap": "Mine smaller exact/common runs and split the target window further.",
    }
    return gates.get(blocker, "Keep blocked until semantic and call-shape evidence reaches near/exact quality.")


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    boundary = read_json(args.boundaries, {})
    rows: list[dict[str, Any]] = []
    for source in list_value(boundary.get("rows", [])):
        if not isinstance(source, dict):
            continue
        if source.get("status") == "promotion-candidate":
            continue
        manifest = read_json(repo_path(source.get("probe_manifest", "")), {})
        target_rows = read_indexed_ops(repo_path(manifest.get("target_window_ops", "")))
        compiled_rows = read_indexed_ops(repo_path(manifest.get("compiled_helper_body_ops", "")))
        target_skip = int_value(source.get("target_skip"))
        compiled_extra_skip = int_value(source.get("compiled_extra_skip"))
        count = int_value(source.get("slice_instruction_count"))
        target_slice = slice_rows(target_rows, target_skip, count)
        compiled_slice = slice_rows(compiled_rows, compiled_extra_skip, count)
        target_ops = op_text(target_slice)
        compiled_ops = op_text(compiled_slice)
        compare = compare_ops(target_ops, compiled_ops)
        target_classes = op_class_counts(target_ops)
        compiled_classes = op_class_counts(compiled_ops)
        blocker = blocker_class(source, target_classes, compiled_classes)
        lcs_count = int_value(compare.get("lcs_instruction_count"))
        rows.append(
            {
                "entry": source.get("entry", ""),
                "oot3d_name": source.get("oot3d_name", ""),
                "candidate_symbol": source.get("candidate_symbol", ""),
                "blocker_class": blocker,
                "category": source.get("category", ""),
                "target_window": f"{source.get('target_start_addr', '')}-{source.get('target_end_addr', '')}",
                "target_skip": target_skip,
                "compiled_extra_skip": compiled_extra_skip,
                "effective_compiled_skip": int_value(source.get("effective_compiled_skip")),
                "slice_instruction_count": count,
                "lcs_instruction_count": lcs_count,
                "lcs_window_ratio": float_value(compare.get("lcs_target_ratio")),
                "matching_prefix": int_value(compare.get("matching_prefix")),
                "matching_suffix": int_value(compare.get("matching_suffix")),
                "longest_common_run": compare.get("longest_common_run", {}),
                "first_difference": compare.get("first_difference", {}),
                "target_call_count": target_classes.get("call", 0),
                "compiled_call_count": compiled_classes.get("call", 0),
                "call_count_delta": target_classes.get("call", 0) - compiled_classes.get("call", 0),
                "target_branch_count": target_classes.get("branch", 0),
                "compiled_branch_count": compiled_classes.get("branch", 0),
                "branch_count_delta": target_classes.get("branch", 0) - compiled_classes.get("branch", 0),
                "target_load_count": target_classes.get("load", 0),
                "compiled_load_count": compiled_classes.get("load", 0),
                "load_count_delta": target_classes.get("load", 0) - compiled_classes.get("load", 0),
                "target_store_count": target_classes.get("store", 0),
                "compiled_store_count": compiled_classes.get("store", 0),
                "store_count_delta": target_classes.get("store", 0) - compiled_classes.get("store", 0),
                "target_classes": target_classes,
                "compiled_classes": compiled_classes,
                "target_memory_offsets": memory_offset_counts(target_ops),
                "compiled_memory_offsets": memory_offset_counts(compiled_ops),
                "target_call_contexts": call_contexts(target_slice, args.context_limit),
                "compiled_call_contexts": call_contexts(compiled_slice, args.context_limit),
                "target_branch_contexts": branch_contexts(target_slice, args.context_limit),
                "compiled_branch_contexts": branch_contexts(compiled_slice, args.context_limit),
                "common_runs": common_runs(target_ops, compiled_ops, args.min_run_length, args.run_limit),
                "source_excerpt": source.get("source_excerpt", ""),
                "next_gate": next_gate(blocker),
            }
        )

    rows.sort(
        key=lambda item: (
            item.get("blocker_class") != "compiled-subregion-alignment",
            -float_value(item.get("lcs_window_ratio")),
            str(item.get("entry", "")),
        )
    )
    blockers = Counter(str(row.get("blocker_class", "")) for row in rows)
    summary = {
        "packets": len(rows),
        "blocked_packets": len(rows),
        "promotion_ready_packets": 0,
        "blocker_classes": dict(sorted(blockers.items())),
        "compiled_subregion_alignment_packets": blockers["compiled-subregion-alignment"],
        "call_count_delta_packets": blockers["call-count-delta"],
        "memory_shape_delta_packets": blockers["memory-shape-delta"],
        "best_entry": rows[0].get("entry", "") if rows else "",
        "best_candidate_symbol": rows[0].get("candidate_symbol", "") if rows else "",
        "best_blocker_class": rows[0].get("blocker_class", "") if rows else "",
        "best_lcs_window_ratio": rows[0].get("lcs_window_ratio", 0.0) if rows else 0.0,
        "next_gate": "Split high-skip N64 helper subregions first; then rerun window/probe comparison.",
    }
    return {
        "format": "oot3d_direct_semantic_call_shape_v1",
        "summary": summary,
        "rows": rows,
    }


def compact(value: Any) -> str:
    if isinstance(value, (dict, list)):
        return json.dumps(value, separators=(",", ":"))
    return str(value)


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Semantic Call-Shape Analysis",
        "",
        "This report diagnoses refined packets that still fail near/exact comparison after boundary correction.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Packets | {summary['packets']} |",
        f"| Promotion-ready packets | {summary['promotion_ready_packets']} |",
        f"| Compiled subregion alignment packets | {summary['compiled_subregion_alignment_packets']} |",
        f"| Call-count delta packets | {summary['call_count_delta_packets']} |",
        f"| Memory-shape delta packets | {summary['memory_shape_delta_packets']} |",
        "",
        "## Packet Blockers",
        "",
        "| Blocker | OOT3D | Candidate | Window | Skip T/C | LCS | Calls T/C | Branches T/C | Loads T/C | Stores T/C | Next gate |",
        "| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |",
    ]
    for row in data["rows"]:
        lines.append(
            f"| `{row.get('blocker_class', '')}` | `{row.get('entry', '')}` `{row.get('oot3d_name', '')}` | "
            f"`{row.get('candidate_symbol', '')}` | `{row.get('target_window', '')}` | "
            f"{row.get('target_skip', '')}/{row.get('compiled_extra_skip', '')} | "
            f"{float_value(row.get('lcs_window_ratio')):.4f} | "
            f"{row.get('target_call_count', 0)}/{row.get('compiled_call_count', 0)} | "
            f"{row.get('target_branch_count', 0)}/{row.get('compiled_branch_count', 0)} | "
            f"{row.get('target_load_count', 0)}/{row.get('compiled_load_count', 0)} | "
            f"{row.get('target_store_count', 0)}/{row.get('compiled_store_count', 0)} | "
            f"{row.get('next_gate', '')} |"
        )
    lines.extend(["", "## Call Context Samples", ""])
    for row in data["rows"]:
        lines.append(f"### `{row.get('entry', '')}` `{row.get('candidate_symbol', '')}`")
        lines.append("")
        lines.append("Target calls:")
        for context in row.get("target_call_contexts", []):
            lines.append(f"- `{context}`")
        lines.append("")
        lines.append("Compiled calls:")
        for context in row.get("compiled_call_contexts", []):
            lines.append(f"- `{context}`")
        lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--boundaries", type=Path, default=DEFAULT_BOUNDARIES)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--context-limit", type=int, default=10)
    parser.add_argument("--min-run-length", type=int, default=2)
    parser.add_argument("--run-limit", type=int, default=12)
    args = parser.parse_args()

    data = build_report(args)
    fields = [
        "entry",
        "oot3d_name",
        "candidate_symbol",
        "blocker_class",
        "category",
        "target_window",
        "target_skip",
        "compiled_extra_skip",
        "effective_compiled_skip",
        "slice_instruction_count",
        "lcs_instruction_count",
        "lcs_window_ratio",
        "matching_prefix",
        "matching_suffix",
        "longest_common_run",
        "first_difference",
        "target_call_count",
        "compiled_call_count",
        "call_count_delta",
        "target_branch_count",
        "compiled_branch_count",
        "branch_count_delta",
        "target_load_count",
        "compiled_load_count",
        "load_count_delta",
        "target_store_count",
        "compiled_store_count",
        "store_count_delta",
        "target_classes",
        "compiled_classes",
        "target_memory_offsets",
        "compiled_memory_offsets",
        "target_call_contexts",
        "compiled_call_contexts",
        "target_branch_contexts",
        "compiled_branch_contexts",
        "common_runs",
        "source_excerpt",
        "next_gate",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct semantic call-shape: "
        f"{summary['packets']} packets, "
        f"{summary['compiled_subregion_alignment_packets']} compiled-subregion, "
        f"{summary['call_count_delta_packets']} call-delta, "
        f"best {summary['best_entry']} {summary['best_candidate_symbol']} {summary['best_blocker_class']}"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
