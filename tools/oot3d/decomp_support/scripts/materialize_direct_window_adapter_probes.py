#!/usr/bin/env python3
"""Materialize focused adapter probe packets for refined target windows."""

from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path
from typing import Any

from build_direct_target_split_suggestions import read_target_functions_with_addresses
from compare_direct_packet_probe_matches import read_objdump_file
from compare_direct_target_window_probes import first_difference_text, gap_index, repo_path, run_text, target_by_entry
from compare_runtime_objects import compare_ops


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ADAPTER_QUEUE = ROOT / "analysis" / "direct_window_adapter_queue.json"
DEFAULT_GAP_REPORT = ROOT / "analysis" / "direct_split_gap_report.json"
DEFAULT_TARGET_DISASSEMBLY = ROOT / "ghidra_export" / "disassembly.txt"
DEFAULT_OUT_DIR = ROOT / "build" / "direct_window_adapter_probes"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_window_adapter_probes.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_window_adapter_probes.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_window_adapter_probes.md"
ACTIONABLE_BLOCKERS = {
    "boundary-and-prologue-adapter",
    "compiled-prologue-adapter",
    "target-boundary-refinement",
    "partial-prefix-shape",
    "call-shape-mismatch",
}


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


def parse_window(value: Any) -> tuple[str, str]:
    text = str(value or "")
    if "-" not in text:
        return "", ""
    start, end = text.split("-", 1)
    return start.lower(), end.lower()


def slug(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("_") or "probe"


def address_index(addresses: list[str], address: str) -> int:
    try:
        return [str(item).lower() for item in addresses].index(address.lower())
    except ValueError:
        return -1


def op_lines(addresses: list[str], ops: list[str], start: int, end: int) -> list[str]:
    lines: list[str] = []
    for index in range(start, end + 1):
        addr = addresses[index] if 0 <= index < len(addresses) else f"+{index:04x}"
        op = ops[index] if 0 <= index < len(ops) else ""
        lines.append(f"{index:04d} {addr}: {op}")
    return lines


def compiled_lines(ops: list[str], skip: int, count: int) -> list[str]:
    return [f"{index:04d}: {op}" for index, op in enumerate(ops[skip : skip + count], start=skip)]


def source_excerpt(source: str) -> str:
    match = re.match(r"^(?P<path>.+):(?P<start>\d+)-(?P<end>\d+)$", source)
    if not match:
        return ""
    path = repo_path(match.group("path"))
    if not path.is_file():
        return ""
    start = int(match.group("start"))
    end = int(match.group("end"))
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    excerpt = []
    for line_no in range(max(1, start), min(end, len(lines)) + 1):
        excerpt.append(f"{line_no:05d}: {lines[line_no - 1]}")
    return "\n".join(excerpt) + ("\n" if excerpt else "")


def build_packets(args: argparse.Namespace) -> dict[str, Any]:
    queue = read_json(args.adapter_queue, {})
    gap_report = read_json(args.gap_report, {})
    gap_by_key = gap_index(gap_report)
    targets_by_entry = target_by_entry(read_target_functions_with_addresses(args.target_disassembly))
    compiled_cache: dict[Path, dict[str, dict[str, object]]] = {}
    rows: list[dict[str, Any]] = []

    source_rows = [
        row for row in list_value(queue.get("rows", []))
        if isinstance(row, dict) and str(row.get("blocker_class", "")) in ACTIONABLE_BLOCKERS
    ]
    source_rows.sort(key=lambda row: (-int_value(row.get("work_priority")), str(row.get("entry", ""))))
    if args.limit > 0:
        source_rows = source_rows[: args.limit]

    for row in source_rows:
        entry = str(row.get("entry", "")).lower()
        candidate = str(row.get("candidate_symbol", ""))
        target = targets_by_entry.get(entry)
        gap = gap_by_key.get((entry, candidate), {})
        dump_path = repo_path(gap.get("compiled_dump", ""))
        packet_dir = args.out_dir / f"{entry}_{slug(candidate)}"
        base = {
            "entry": entry,
            "oot3d_name": row.get("oot3d_name", ""),
            "candidate_symbol": candidate,
            "blocker_class": row.get("blocker_class", ""),
            "work_priority": int_value(row.get("work_priority")),
            "refined_window": row.get("refined_window", ""),
            "compiled_skip": int_value(row.get("compiled_skip")),
            "compiled_slice_instruction_count": int_value(row.get("compiled_slice_instruction_count")),
            "source": row.get("source", ""),
            "packet_dir": rel(packet_dir),
        }
        if target is None:
            rows.append({**base, "status": "target-missing"})
            continue
        if not dump_path.is_file():
            rows.append({**base, "status": "compiled-dump-missing"})
            continue
        if dump_path not in compiled_cache:
            compiled_cache[dump_path] = read_objdump_file(dump_path)
        compiled = compiled_cache[dump_path].get(candidate)
        if compiled is None:
            rows.append({**base, "status": "compiled-symbol-missing"})
            continue

        start_addr, end_addr = parse_window(row.get("refined_window"))
        addresses = [str(item).lower() for item in list_value(target.get("addresses", []))]
        target_ops = list_value(target.get("ops", []))
        compiled_ops = list_value(compiled.get("ops", []))
        start_index = address_index(addresses, start_addr)
        end_index = address_index(addresses, end_addr)
        compiled_skip = int_value(row.get("compiled_skip"))
        compiled_count = int_value(row.get("compiled_slice_instruction_count"))
        if start_index < 0 or end_index < start_index or compiled_skip + compiled_count > len(compiled_ops):
            rows.append({**base, "status": "slice-out-of-range"})
            continue

        packet_dir.mkdir(parents=True, exist_ok=True)
        target_slice = target_ops[start_index : end_index + 1]
        compiled_slice = compiled_ops[compiled_skip : compiled_skip + compiled_count]
        compare = compare_ops(target_slice, compiled_slice)
        target_path = packet_dir / "target_window.ops.txt"
        compiled_path = packet_dir / "compiled_helper_body.ops.txt"
        source_path = packet_dir / "source_excerpt.c.txt"
        manifest_path = packet_dir / "probe_manifest.json"
        target_path.write_text("\n".join(op_lines(addresses, target_ops, start_index, end_index)) + "\n", encoding="utf-8", newline="\n")
        compiled_path.write_text("\n".join(compiled_lines(compiled_ops, compiled_skip, compiled_count)) + "\n", encoding="utf-8", newline="\n")
        source_path.write_text(source_excerpt(str(row.get("source", ""))), encoding="utf-8", newline="\n")
        manifest = {
            **base,
            "status": "materialized",
            "target_start_index": start_index,
            "target_end_index": end_index,
            "target_instruction_count": int_value(compare.get("target_instruction_count")),
            "compiled_instruction_count": int_value(compare.get("compiled_instruction_count")),
            "lcs_instruction_count": int_value(compare.get("lcs_instruction_count")),
            "lcs_window_ratio": float_value(compare.get("lcs_target_ratio")),
            "matching_prefix": int_value(compare.get("matching_prefix")),
            "matching_suffix": int_value(compare.get("matching_suffix")),
            "longest_common_run": run_text(compare),
            "first_difference": first_difference_text(compare),
            "exact_match": bool(compare.get("exact_match")),
            "target_window_ops": rel(target_path),
            "compiled_helper_body_ops": rel(compiled_path),
            "source_excerpt": rel(source_path),
        }
        write_json(manifest_path, manifest)
        rows.append({**manifest, "probe_manifest": rel(manifest_path)})

    materialized = [row for row in rows if row.get("status") == "materialized"]
    summary = {
        "workorders_considered": len(source_rows),
        "packets": len(rows),
        "materialized_packets": len(materialized),
        "blocked_packets": len(rows) - len(materialized),
        "entries": len({row.get("entry") for row in materialized}),
        "top_entry": materialized[0].get("entry", "") if materialized else "",
        "top_candidate_symbol": materialized[0].get("candidate_symbol", "") if materialized else "",
        "top_lcs_window_ratio": materialized[0].get("lcs_window_ratio", 0.0) if materialized else 0.0,
        "next_gate": "Inspect focused packet ops and implement adapter/body probes only when the packet reaches near/exact quality.",
    }
    return {
        "format": "oot3d_direct_window_adapter_probes_v1",
        "summary": summary,
        "rows": rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct Window Adapter Probes",
        "",
        "These packets materialize target-window ops, compiled helper-body ops, and source excerpts for adapter workorders.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Workorders considered | {summary['workorders_considered']} |",
        f"| Packets | {summary['packets']} |",
        f"| Materialized packets | {summary['materialized_packets']} |",
        f"| Blocked packets | {summary['blocked_packets']} |",
        "",
        "## Packets",
        "",
        "| Status | OOT3D | Candidate | Blocker | Window | Skip | LCS | Packet |",
        "| --- | --- | --- | --- | --- | ---: | ---: | --- |",
    ]
    for row in data["rows"]:
        lines.append(
            f"| `{row.get('status', '')}` | `{row.get('entry', '')}` `{row.get('oot3d_name', '')}` | "
            f"`{row.get('candidate_symbol', '')}` | `{row.get('blocker_class', '')}` | "
            f"`{row.get('refined_window', '')}` | {row.get('compiled_skip', '')} | "
            f"{float_value(row.get('lcs_window_ratio')):.4f} | `{row.get('probe_manifest', row.get('packet_dir', ''))}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adapter-queue", type=Path, default=DEFAULT_ADAPTER_QUEUE)
    parser.add_argument("--gap-report", type=Path, default=DEFAULT_GAP_REPORT)
    parser.add_argument("--target-disassembly", type=Path, default=DEFAULT_TARGET_DISASSEMBLY)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--limit", type=int, default=0)
    args = parser.parse_args()

    data = build_packets(args)
    fields = [
        "status",
        "entry",
        "oot3d_name",
        "candidate_symbol",
        "blocker_class",
        "work_priority",
        "refined_window",
        "compiled_skip",
        "compiled_slice_instruction_count",
        "target_instruction_count",
        "compiled_instruction_count",
        "lcs_instruction_count",
        "lcs_window_ratio",
        "matching_prefix",
        "matching_suffix",
        "longest_common_run",
        "first_difference",
        "exact_match",
        "probe_manifest",
        "target_window_ops",
        "compiled_helper_body_ops",
        "source_excerpt",
        "source",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct window adapter probes: "
        f"{summary['materialized_packets']}/{summary['packets']} materialized, "
        f"top {summary['top_entry']} {summary['top_candidate_symbol']}"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
