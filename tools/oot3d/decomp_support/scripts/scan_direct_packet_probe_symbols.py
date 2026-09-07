#!/usr/bin/env python3
"""Scan every compiled symbol in direct packet probes against each OOT3D target.

The normal direct packet gate compares the mapped N64 function with the OOT3D
target. This scanner keeps that mapping intact, but also checks all symbols
emitted by the throwaway packet object. That catches split/helper candidates
inside large N64 lanes without round-tripping through a disassembler UI.
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path
from typing import Any

from compare_direct_packet_probe_matches import (
    classify_compare,
    dump_path_for,
    first_difference_text,
    float_value,
    int_value,
    mapped_rows_by_entry,
    read_csv,
    read_json,
    read_objdump_file,
    rel,
    run_text,
)
from compare_runtime_objects import (
    apply_target_aliases,
    compare_ops,
    read_manual_symbol_aliases,
    read_target_functions,
)


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PROBE_JSON = ROOT / "analysis" / "direct_packet_compile_probe.json"
DEFAULT_PORT_MAP = ROOT / "metadata" / "n64_port_map.csv"
DEFAULT_TARGET_DISASSEMBLY = ROOT / "ghidra_export" / "disassembly.txt"
DEFAULT_MANUAL_SYMBOLS = ROOT / "symbols" / "manual_symbols.csv"
DEFAULT_DUMP_DIR = ROOT / "build" / "direct_packet_compile_probe" / "dumps"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_packet_symbol_scan.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_packet_symbol_scan.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_packet_symbol_scan.md"

SCAN_CATEGORIES = (
    "exact-symbol",
    "target-slice-exact",
    "symbol-slice-exact",
    "codegen-near",
    "structural-near",
    "semantic-started",
    "semantic-gap",
)


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    fieldnames = [
        "scan_category",
        "entry",
        "oot3d_name",
        "mapped_n64_name",
        "best_symbol",
        "best_symbol_is_mapped",
        "domain",
        "target_instruction_count",
        "best_instruction_count",
        "matching_prefix",
        "matching_suffix",
        "lcs_instruction_count",
        "lcs_target_ratio",
        "lcs_symbol_ratio",
        "longest_common_run",
        "target_in_symbol_offset",
        "symbol_in_target_offset",
        "first_difference",
        "symbols_scanned",
        "top_alternatives",
        "packet",
        "dump",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def contiguous_index(haystack: list[str], needle: list[str]) -> int:
    if not needle:
        return 0
    if len(needle) > len(haystack):
        return -1
    first = needle[0]
    limit = len(haystack) - len(needle) + 1
    for index in range(limit):
        if haystack[index] == first and haystack[index : index + len(needle)] == needle:
            return index
    return -1


def scan_category(compare: dict[str, Any], target_ops: list[str], compiled_ops: list[str], min_slice_insns: int) -> str:
    if compare.get("exact_match"):
        return "exact-symbol"
    if len(target_ops) >= min_slice_insns and contiguous_index(compiled_ops, target_ops) >= 0:
        return "target-slice-exact"
    if len(compiled_ops) >= min_slice_insns and contiguous_index(target_ops, compiled_ops) >= 0:
        return "symbol-slice-exact"
    return classify_compare(compare)


def run_length(compare: dict[str, Any]) -> int:
    run = compare.get("longest_common_run")
    if not isinstance(run, dict):
        return 0
    return int_value(run.get("length"))


def category_rank(category: str) -> int:
    try:
        return SCAN_CATEGORIES.index(category)
    except ValueError:
        return len(SCAN_CATEGORIES)


def candidate_sort_key(candidate: dict[str, Any]) -> tuple[int, float, int, int, int, str]:
    count_delta = abs(
        int_value(candidate.get("target_instruction_count")) - int_value(candidate.get("compiled_instruction_count"))
    )
    return (
        category_rank(str(candidate.get("scan_category", ""))),
        -float_value(candidate.get("lcs_target_ratio")),
        -run_length(candidate.get("compare", {})),
        -int_value(candidate.get("matching_prefix")),
        count_delta,
        str(candidate.get("symbol", "")),
    )


def compact_candidate(candidate: dict[str, Any]) -> dict[str, Any]:
    return {
        "symbol": candidate.get("symbol", ""),
        "scan_category": candidate.get("scan_category", ""),
        "compiled_instruction_count": int_value(candidate.get("compiled_instruction_count")),
        "lcs_target_ratio": float_value(candidate.get("lcs_target_ratio")),
        "lcs_symbol_ratio": float_value(candidate.get("lcs_symbol_ratio")),
        "longest_common_run": candidate.get("longest_common_run", ""),
        "matching_prefix": int_value(candidate.get("matching_prefix")),
        "first_difference": candidate.get("first_difference", ""),
    }


def build_candidate(symbol: str, target_ops: list[str], compiled_ops: list[str], min_slice_insns: int) -> dict[str, Any]:
    compare = compare_ops(target_ops, compiled_ops)
    category = scan_category(compare, target_ops, compiled_ops, min_slice_insns)
    lcs_count = int_value(compare.get("lcs_instruction_count"))
    compiled_count = int_value(compare.get("compiled_instruction_count"))
    return {
        "symbol": symbol,
        "scan_category": category,
        "target_instruction_count": int_value(compare.get("target_instruction_count")),
        "compiled_instruction_count": int_value(compare.get("compiled_instruction_count")),
        "matching_prefix": int_value(compare.get("matching_prefix")),
        "matching_suffix": int_value(compare.get("matching_suffix")),
        "lcs_instruction_count": lcs_count,
        "lcs_target_ratio": float_value(compare.get("lcs_target_ratio")),
        "lcs_symbol_ratio": round(lcs_count / compiled_count, 4) if compiled_count else 0.0,
        "longest_common_run": run_text(compare),
        "target_in_symbol_offset": contiguous_index(compiled_ops, target_ops),
        "symbol_in_target_offset": contiguous_index(target_ops, compiled_ops),
        "first_difference": first_difference_text(compare),
        "compare": compare,
    }


def build_rows(args: argparse.Namespace) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    probe_data = read_json(args.probe_json, {})
    probe_rows = list(probe_data.get("rows", [])) if isinstance(probe_data, dict) else []
    port_map = mapped_rows_by_entry(read_csv(args.port_map))
    target_functions = apply_target_aliases(
        read_target_functions(args.target_disassembly),
        read_manual_symbol_aliases(args.manual_symbols),
    )
    compiled_by_dump: dict[Path, dict[str, dict[str, object]]] = {}
    rows: list[dict[str, Any]] = []
    total_symbols_scanned = 0

    for probe in probe_rows:
        if not probe.get("compiled"):
            continue
        entry = str(probe.get("entry", "")).lower()
        mapped = port_map.get(entry, {})
        target_name = mapped.get("oot3d_name") or str(probe.get("name", ""))
        mapped_n64_name = mapped.get("n64_name", "")
        dump_path = dump_path_for(probe, args.dump_dir)
        if target_name not in target_functions or not dump_path.is_file():
            continue
        if dump_path not in compiled_by_dump:
            compiled_by_dump[dump_path] = read_objdump_file(dump_path)

        target_ops = list(target_functions[target_name]["ops"])
        candidates: list[dict[str, Any]] = []
        for symbol, function in compiled_by_dump[dump_path].items():
            compiled_ops = list(function.get("ops", []))
            if not compiled_ops:
                continue
            candidates.append(build_candidate(symbol, target_ops, compiled_ops, args.min_slice_insns))

        total_symbols_scanned += len(candidates)
        if not candidates:
            continue
        candidates.sort(key=candidate_sort_key)
        best = candidates[0]
        top = [compact_candidate(candidate) for candidate in candidates[: args.top]]
        rows.append(
            {
                "scan_category": best["scan_category"],
                "entry": entry,
                "oot3d_name": target_name,
                "mapped_n64_name": mapped_n64_name,
                "best_symbol": best["symbol"],
                "best_symbol_is_mapped": "yes" if best["symbol"] == mapped_n64_name else "no",
                "domain": probe.get("domain", ""),
                "target_instruction_count": best["target_instruction_count"],
                "best_instruction_count": best["compiled_instruction_count"],
                "matching_prefix": best["matching_prefix"],
                "matching_suffix": best["matching_suffix"],
                "lcs_instruction_count": best["lcs_instruction_count"],
                "lcs_target_ratio": best["lcs_target_ratio"],
                "lcs_symbol_ratio": best["lcs_symbol_ratio"],
                "longest_common_run": best["longest_common_run"],
                "target_in_symbol_offset": best["target_in_symbol_offset"],
                "symbol_in_target_offset": best["symbol_in_target_offset"],
                "first_difference": best["first_difference"],
                "symbols_scanned": len(candidates),
                "top_alternatives": " ".join(
                    f"{candidate['symbol']}:{candidate['scan_category']}:{candidate['lcs_target_ratio']:.4f}"
                    for candidate in top
                ),
                "top_candidates": top,
                "packet": probe.get("packet", ""),
                "dump": rel(dump_path),
            }
        )

    rows.sort(key=lambda row: (category_rank(row["scan_category"]), row["best_symbol_is_mapped"] != "yes", row["entry"]))
    counts = Counter(str(row.get("scan_category", "")) for row in rows)
    summary = {
        "packets_scanned": len(rows),
        "symbols_scanned": total_symbols_scanned,
        "best_exact_symbol": counts["exact-symbol"],
        "best_target_slice_exact": counts["target-slice-exact"],
        "best_symbol_slice_exact": counts["symbol-slice-exact"],
        "best_codegen_near": counts["codegen-near"],
        "best_structural_near": counts["structural-near"],
        "best_semantic_started": counts["semantic-started"],
        "best_semantic_gap": counts["semantic-gap"],
        "best_symbol_differs_from_map": sum(1 for row in rows if row["best_symbol_is_mapped"] != "yes"),
        "categories": dict(counts),
    }
    return rows, summary


def write_markdown(path: Path, rows: list[dict[str, Any]], summary: dict[str, Any]) -> None:
    lines = [
        "# Direct Packet Symbol Scan",
        "",
        "This report compares each OOT3D target against every compiled symbol emitted by its direct C probe packet.",
        "It is meant to find split/helper candidates inside large N64 source lanes before maintained-source promotion.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Packets scanned | {summary['packets_scanned']} |",
        f"| Compiled symbols scanned | {summary['symbols_scanned']} |",
        f"| Best exact symbols | {summary['best_exact_symbol']} |",
        f"| Best target-slice exact | {summary['best_target_slice_exact']} |",
        f"| Best symbol-slice exact | {summary['best_symbol_slice_exact']} |",
        f"| Best codegen-near | {summary['best_codegen_near']} |",
        f"| Best structural-near | {summary['best_structural_near']} |",
        f"| Best semantic-started | {summary['best_semantic_started']} |",
        f"| Best semantic-gap | {summary['best_semantic_gap']} |",
        f"| Best symbol differs from mapped N64 name | {summary['best_symbol_differs_from_map']} |",
        "",
        "## Best Symbol Queue",
        "",
        "| Category | OOT3D | Mapped N64 | Best symbol | Mapped? | Insns | LCS target/symbol | Run | Slice | First difference |",
        "| --- | --- | --- | --- | --- | ---: | ---: | --- | --- | --- |",
    ]
    for row in rows:
        slice_text = ""
        if int_value(row.get("target_in_symbol_offset")) >= 0:
            slice_text = f"target@compiled+{row['target_in_symbol_offset']}"
        elif int_value(row.get("symbol_in_target_offset")) >= 0:
            slice_text = f"symbol@target+{row['symbol_in_target_offset']}"
        lines.append(
            f"| `{row['scan_category']}` | `{row['entry']}` `{row['oot3d_name']}` | `{row['mapped_n64_name']}` | "
            f"`{row['best_symbol']}` | `{row['best_symbol_is_mapped']}` | "
            f"{row['target_instruction_count']}/{row['best_instruction_count']} | "
            f"{float_value(row['lcs_target_ratio']):.4f}/{float_value(row['lcs_symbol_ratio']):.4f} | "
            f"{row['longest_common_run']} | `{slice_text}` | "
            f"`{row['first_difference']}` |"
        )

    lines.extend(["", "## Top Alternatives"])
    for row in rows:
        lines.extend(["", f"### `{row['entry']}` `{row['oot3d_name']}`", ""])
        for candidate in row.get("top_candidates", []):
            lines.append(
                f"- `{candidate['symbol']}` `{candidate['scan_category']}` "
                f"{candidate['compiled_instruction_count']} insns, "
                f"LCS {float_value(candidate['lcs_target_ratio']):.4f}/"
                f"{float_value(candidate['lcs_symbol_ratio']):.4f}, "
                f"run {candidate['longest_common_run']}"
            )

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe-json", type=Path, default=DEFAULT_PROBE_JSON)
    parser.add_argument("--port-map", type=Path, default=DEFAULT_PORT_MAP)
    parser.add_argument("--target-disassembly", type=Path, default=DEFAULT_TARGET_DISASSEMBLY)
    parser.add_argument("--manual-symbols", type=Path, default=DEFAULT_MANUAL_SYMBOLS)
    parser.add_argument("--dump-dir", type=Path, default=DEFAULT_DUMP_DIR)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--top", type=int, default=5)
    parser.add_argument("--min-slice-insns", type=int, default=5)
    args = parser.parse_args()

    rows, summary = build_rows(args)
    write_json(args.out_json, {"summary": summary, "rows": rows})
    write_csv(args.out_csv, rows)
    write_markdown(args.out_md, rows, summary)
    print(
        "direct packet symbol scan: "
        f"{summary['packets_scanned']} packets, {summary['symbols_scanned']} symbols, "
        f"{summary['best_exact_symbol']} exact, {summary['best_target_slice_exact']} target-slice exact, "
        f"{summary['best_structural_near']} structural-near, {summary['best_semantic_started']} semantic-started, "
        f"{summary['best_semantic_gap']} semantic-gap"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
