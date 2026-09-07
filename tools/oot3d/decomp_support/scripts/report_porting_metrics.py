#!/usr/bin/env python3
"""Summarize current compile, match, and structured-port metrics."""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter
from pathlib import Path
from typing import Any

from audit_c_conversion_readiness import BASELINE_DEFINES, source_style


ROOT = Path(__file__).resolve().parents[1]
TARGET_HEADER_RE = re.compile(r"^//\s+\S+\s+@\s+[0-9a-fA-F]+\s*$")
TARGET_INSN_RE = re.compile(r"^[0-9a-fA-F]+:\s+.+$")


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def read_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    return json.loads(path.read_text(encoding="utf-8"))


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def read_source_list(path: Path) -> list[str]:
    if not path.is_file():
        return []
    sources = []
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped and not stripped.startswith("#"):
            sources.append(stripped)
    return sources


def percent(part: int | float, total: int | float) -> float:
    if not total:
        return 0.0
    return round((float(part) / float(total)) * 100.0, 4)


def count_nonblank_lines(paths: list[str]) -> int:
    total = 0
    for source in sorted(set(paths)):
        path = ROOT / source
        if not path.is_file():
            continue
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.strip():
                total += 1
    return total


def object_stem(source: str) -> str:
    without_extension = re.sub(r"\.[^\\/\.]+$", "", source)
    return re.sub(r"[\\/:\s]+", "_", without_extension)


def target_binary_summary() -> dict[str, Any]:
    exported_functions = len(read_csv(ROOT / "ghidra_export" / "functions.csv"))
    disassembly_path = ROOT / "ghidra_export" / "disassembly.txt"
    disassembly_functions = 0
    target_instructions = 0
    in_function = False

    if disassembly_path.is_file():
        for raw_line in disassembly_path.read_text(encoding="utf-8", errors="replace").splitlines():
            if TARGET_HEADER_RE.match(raw_line):
                disassembly_functions += 1
                in_function = True
                continue
            if in_function and not raw_line.strip():
                in_function = False
                continue
            if in_function and TARGET_INSN_RE.match(raw_line):
                target_instructions += 1

    return {
        "exported_function_rows": exported_functions,
        "disassembly_functions": disassembly_functions,
        "target_instructions": target_instructions,
    }


def dump_to_source_map(manifest: dict[str, Any]) -> dict[str, str]:
    sources = [str(source) for source in manifest.get("sources", [])]
    by_dump = {f"{object_stem(source)}.dump": source for source in sources}

    objects = [str(obj) for obj in manifest.get("objects", [])]
    for source, obj in zip(sources, objects):
        by_dump[Path(obj).with_suffix(".dump").name] = source
    return by_dump


def compare_summary(compare_rows: list[dict[str, Any]], dump_sources: dict[str, str]) -> dict[str, Any]:
    exact_rows = [row for row in compare_rows if row.get("exact_match")]
    compared_dumps = {Path(str(row.get("compiled_dump", ""))).name for row in compare_rows}
    exact_dumps = {Path(str(row.get("compiled_dump", ""))).name for row in exact_rows}
    compared_sources = {dump_sources.get(dump, dump) for dump in compared_dumps if dump}
    exact_sources = {dump_sources.get(dump, dump) for dump in exact_dumps if dump}
    exact_styles = Counter()
    exact_style_rows = []
    for row in exact_rows:
        dump = Path(str(row.get("compiled_dump", ""))).name
        source = dump_sources.get(dump, "")
        name = str(row.get("name", ""))
        style = source_style(source, name, set(BASELINE_DEFINES)) if source else "unknown"
        exact_styles[style] += 1
        exact_style_rows.append(
            {
                "name": name,
                "source": source,
                "style": style,
                "target_instruction_count": int(row.get("target_instruction_count", 0) or 0),
            }
        )

    return {
        "compared_functions": len(compare_rows),
        "exact_functions": len(exact_rows),
        "compared_files": len(compared_sources),
        "exact_files": len(exact_sources),
        "exact_target_instructions": sum(int(row.get("target_instruction_count", 0)) for row in exact_rows),
        "exact_plain_c_functions": int(exact_styles["plain-c"]),
        "exact_inline_asm_functions": int(exact_styles["c-inline-asm"]),
        "exact_naked_asm_functions": int(exact_styles["naked-asm"]),
        "exact_unknown_style_functions": sum(
            count for style, count in exact_styles.items() if style not in {"plain-c", "c-inline-asm", "naked-asm"}
        ),
        "exact_style_rows": sorted(
            exact_style_rows,
            key=lambda item: (item["style"], item["source"], item["name"]),
        ),
    }


def symbol_summary() -> dict[str, Any]:
    overlay = read_json(ROOT / "analysis" / "symbol_overlay.json", {})
    manual_rows = read_csv(ROOT / "symbols" / "manual_symbols.csv")
    data_rows = read_csv(ROOT / "symbols" / "data_symbols.csv")
    return {
        "manual_function_symbols": int(overlay.get("manual_symbol_count", len(manual_rows))),
        "manual_function_symbols_applied": int(overlay.get("applied_count", 0)),
        "manual_function_symbols_pending_export": int(overlay.get("pending_count", 0)),
        "data_symbols": len(data_rows),
    }


def map_summary() -> dict[str, Any]:
    rows = read_csv(ROOT / "metadata" / "n64_port_map.csv")
    statuses = Counter(row.get("status", "") for row in rows)
    return {
        "mapped_functions": len(rows),
        "mapped_port_files": len({row.get("port_file", "") for row in rows if row.get("port_file")}),
        "mapped_n64_sources": len({row.get("n64_source", "") for row in rows if row.get("n64_source")}),
        "statuses": dict(sorted(statuses.items())),
    }


def n64_reuse_summary() -> dict[str, Any]:
    port_map = read_csv(ROOT / "metadata" / "n64_port_map.csv")
    adapter_coverage = read_json(ROOT / "analysis" / "n64_adapter_coverage.json", {})
    adapter_summary = adapter_coverage.get("summary", {}) if isinstance(adapter_coverage, dict) else {}
    conversion_patterns = read_json(ROOT / "analysis" / "n64_conversion_patterns.json", {})
    pattern_rows = conversion_patterns.get("patterns", []) if isinstance(conversion_patterns, dict) else []
    reused_lanes = (
        conversion_patterns.get("duplicated_n64_sources", [])
        if isinstance(conversion_patterns, dict)
        else []
    )
    convertible = read_json(ROOT / "analysis" / "convertible_ports.json", {})
    convertible_summary = convertible.get("summary", {}) if isinstance(convertible, dict) else {}
    convertible_statuses = (
        convertible_summary.get("status_counts", {}) if isinstance(convertible_summary, dict) else {}
    )
    readiness = read_json(ROOT / "analysis" / "c_conversion_readiness.json", {})
    readiness_summary = readiness.get("summary", {}) if isinstance(readiness, dict) else {}
    structured_gate = read_json(ROOT / "analysis" / "structured_c_match_gate.json", {})

    if not isinstance(pattern_rows, list):
        pattern_rows = []
    if not isinstance(reused_lanes, list):
        reused_lanes = []
    if not isinstance(convertible_statuses, dict):
        convertible_statuses = {}
    readiness_summary = readiness_summary if isinstance(readiness_summary, dict) else {}
    structured_gate = structured_gate if isinstance(structured_gate, dict) else {}

    return {
        "mapped_functions": len(port_map),
        "mapped_port_files": len({row.get("port_file", "") for row in port_map if row.get("port_file")}),
        "mapped_n64_sources": len({row.get("n64_source", "") for row in port_map if row.get("n64_source")}),
        "adapter_entries": int(adapter_summary.get("adapter_entries", 0) or 0),
        "rows_with_text_adapter_hits": int(adapter_summary.get("rows_with_text_hits", 0) or 0),
        "rows_with_source_lane_hits": int(adapter_summary.get("rows_with_source_lane_hits", 0) or 0),
        "total_text_adapter_hits": int(adapter_summary.get("total_text_hits", 0) or 0),
        "total_source_lane_hits": int(adapter_summary.get("total_source_lane_hits", 0) or 0),
        "reused_source_lanes": len(reused_lanes),
        "reused_source_lane_rows": sum(
            int(row.get("mapped_rows", 0) or 0) for row in reused_lanes if isinstance(row, dict)
        ),
        "high_confidence_patterns": sum(
            1 for row in pattern_rows if isinstance(row, dict) and row.get("confidence") == "high"
        ),
        "medium_confidence_patterns": sum(
            1 for row in pattern_rows if isinstance(row, dict) and row.get("confidence") == "medium"
        ),
        "convertible_functions_checked": int(convertible_summary.get("functions", 0) or 0),
        "already_converted_real_c": int(convertible_summary.get("already_converted", 0) or 0),
        "already_converted_inline_asm": int(
            convertible_summary.get("already_converted_inline_asm", 0) or 0
        ),
        "exact_seed_needs_c": int(
            readiness_summary.get("matched_exact_seed_asm_functions", 0)
            or convertible_summary.get("exact_seed_needs_c", 0)
            or 0
        ),
        "inline_exact_seed_needs_c": int(readiness_summary.get("matched_inline_seed_asm_functions", 0) or 0),
        "needs_batch_reshaping": int(convertible_summary.get("needs_batch_reshaping", 0) or 0),
        "not_yet_convertible": int(convertible_statuses.get("not-yet-convertible", 0) or 0),
        "structured_exact_functions": int(structured_gate.get("exact_c_functions", 0) or 0),
        "structured_exact_real_c_functions": int(
            structured_gate.get("exact_real_c_functions", 0) or 0
        ),
        "structured_exact_inline_asm_functions": int(
            structured_gate.get("exact_inline_asm_functions", 0) or 0
        ),
        "structured_exact_naked_seed_functions": int(
            structured_gate.get("exact_naked_asm_seed_functions", 0) or 0
        ),
    }


def exact_seed_summary(default_compare: dict[str, dict[str, Any]]) -> dict[str, Any]:
    exact_seed_statuses = {"exact-seed-started", "target-split-exact-seed"}
    rows = [
        row
        for row in read_csv(ROOT / "metadata" / "n64_port_map.csv")
        if row.get("status") in exact_seed_statuses
    ]
    compared = [row for row in rows if row.get("oot3d_name") in default_compare]
    exact = [row for row in compared if default_compare[row["oot3d_name"]].get("exact_match")]
    return {
        "mapped_functions": len(rows),
        "port_files": len({row.get("port_file", "") for row in rows if row.get("port_file")}),
        "n64_sources": len({row.get("n64_source", "") for row in rows if row.get("n64_source")}),
        "default_compared": len(compared),
        "default_exact": len(exact),
        "exact_target_instructions": sum(
            int(default_compare[row["oot3d_name"]].get("target_instruction_count", 0)) for row in exact
        ),
    }


def direct_probe_summary() -> dict[str, Any]:
    compile_probe = read_json(ROOT / "analysis" / "direct_packet_compile_probe.json", {})
    probe_match = read_json(ROOT / "analysis" / "direct_packet_probe_match.json", {})
    template_materialized = read_json(ROOT / "analysis" / "direct_template_materialized" / "index.json", {})
    template_compile_probe = read_json(ROOT / "analysis" / "direct_template_compile_probe.json", {})
    template_probe_match = read_json(ROOT / "analysis" / "direct_template_probe_match.json", {})
    symbol_scan = read_json(ROOT / "analysis" / "direct_packet_symbol_scan.json", {})
    split_workorders = read_json(ROOT / "analysis" / "direct_split_workorders.json", {})
    split_candidate_probe = read_json(ROOT / "analysis" / "direct_split_candidate_probe.json", {})
    target_split_suggestions = read_json(ROOT / "analysis" / "direct_target_split_suggestions.json", {})
    compile_summary = compile_probe.get("summary", {}) if isinstance(compile_probe, dict) else {}
    match_summary = probe_match.get("summary", {}) if isinstance(probe_match, dict) else {}
    template_materialized_summary = (
        template_materialized.get("summary", {}) if isinstance(template_materialized, dict) else {}
    )
    template_compile_summary = (
        template_compile_probe.get("summary", {}) if isinstance(template_compile_probe, dict) else {}
    )
    template_match_summary = (
        template_probe_match.get("summary", {}) if isinstance(template_probe_match, dict) else {}
    )
    scan_summary = symbol_scan.get("summary", {}) if isinstance(symbol_scan, dict) else {}
    split_summary = split_workorders.get("summary", {}) if isinstance(split_workorders, dict) else {}
    split_candidate_summary = (
        split_candidate_probe.get("summary", {}) if isinstance(split_candidate_probe, dict) else {}
    )
    target_split_summary = (
        target_split_suggestions.get("summary", {}) if isinstance(target_split_suggestions, dict) else {}
    )
    span_statuses = split_summary.get("span_statuses", {}) if isinstance(split_summary, dict) else {}
    split_reasons = split_summary.get("reasons", {}) if isinstance(split_summary, dict) else {}
    target_split_kinds = target_split_summary.get("kinds", {}) if isinstance(target_split_summary, dict) else {}
    return {
        "packets": int(compile_summary.get("packets", 0) or 0),
        "compiled": int(compile_summary.get("compiled", 0) or 0),
        "dumped": int(compile_summary.get("dumped", 0) or 0),
        "failed": int(compile_summary.get("failed", 0) or 0),
        "compile_errors": int(compile_summary.get("total_errors", 0) or 0),
        "compared": int(match_summary.get("compared_probes", 0) or 0),
        "exact_c": int(match_summary.get("exact_c", 0) or 0),
        "codegen_near": int(match_summary.get("codegen_near", 0) or 0),
        "structural_near": int(match_summary.get("structural_near", 0) or 0),
        "semantic_started": int(match_summary.get("semantic_started", 0) or 0),
        "semantic_gap": int(match_summary.get("semantic_gap", 0) or 0),
        "template_lanes": int(template_materialized_summary.get("lanes", 0) or 0),
        "template_targets": int(template_materialized_summary.get("targets", 0) or 0),
        "template_rewrite_hits": int(template_materialized_summary.get("rewrite_hits", 0) or 0),
        "template_compiled": int(template_compile_summary.get("compiled", 0) or 0),
        "template_dumped": int(template_compile_summary.get("dumped", 0) or 0),
        "template_failed": int(template_compile_summary.get("failed", 0) or 0),
        "template_compile_errors": int(template_compile_summary.get("total_errors", 0) or 0),
        "template_compared": int(template_match_summary.get("compared_probes", 0) or 0),
        "template_exact_c": int(template_match_summary.get("exact_c", 0) or 0),
        "template_codegen_near": int(template_match_summary.get("codegen_near", 0) or 0),
        "template_structural_near": int(template_match_summary.get("structural_near", 0) or 0),
        "template_semantic_started": int(template_match_summary.get("semantic_started", 0) or 0),
        "template_semantic_gap": int(template_match_summary.get("semantic_gap", 0) or 0),
        "symbol_scan_packets": int(scan_summary.get("packets_scanned", 0) or 0),
        "symbol_scan_symbols": int(scan_summary.get("symbols_scanned", 0) or 0),
        "symbol_scan_exact": int(scan_summary.get("best_exact_symbol", 0) or 0),
        "symbol_scan_slice_exact": int(scan_summary.get("best_target_slice_exact", 0) or 0)
        + int(scan_summary.get("best_symbol_slice_exact", 0) or 0),
        "symbol_scan_structural_near": int(scan_summary.get("best_structural_near", 0) or 0),
        "symbol_scan_semantic_started": int(scan_summary.get("best_semantic_started", 0) or 0),
        "symbol_scan_semantic_gap": int(scan_summary.get("best_semantic_gap", 0) or 0),
        "symbol_scan_best_differs": int(scan_summary.get("best_symbol_differs_from_map", 0) or 0),
        "split_workorders": int(split_summary.get("workorders", 0) or 0),
        "split_entries": int(split_summary.get("entries", 0) or 0),
        "split_candidate_symbols": int(split_summary.get("candidate_symbols", 0) or 0),
        "split_source_spans_found": int(split_summary.get("source_spans_found", 0) or 0),
        "split_complete_spans": int(span_statuses.get("complete", 0) or 0),
        "split_truncated_spans": int(span_statuses.get("truncated", 0) or 0),
        "split_best_helpers": int(split_reasons.get("best-helper", 0) or 0),
        "split_alternate_helpers": int(split_reasons.get("alternate-helper", 0) or 0),
        "split_mapped_symbol_reshape": int(split_reasons.get("mapped-symbol-reshape", 0) or 0),
        "split_candidate_spans": int(split_candidate_summary.get("candidates", 0) or 0),
        "split_candidate_compiled": int(split_candidate_summary.get("compiled", 0) or 0),
        "split_candidate_compile_blocked": int(split_candidate_summary.get("compile_blocked", 0) or 0),
        "split_candidate_exact": int(split_candidate_summary.get("exact_c", 0) or 0),
        "split_candidate_codegen_near": int(split_candidate_summary.get("codegen_near", 0) or 0),
        "split_candidate_structural_near": int(split_candidate_summary.get("structural_near", 0) or 0),
        "split_candidate_semantic_started": int(split_candidate_summary.get("semantic_started", 0) or 0),
        "split_candidate_semantic_gap": int(split_candidate_summary.get("semantic_gap", 0) or 0),
        "target_split_suggestions": int(target_split_summary.get("suggestions", 0) or 0),
        "target_split_entries": int(target_split_summary.get("entries", 0) or 0),
        "target_split_inferred_addresses": int(target_split_summary.get("inferred_addresses", 0) or 0),
        "target_split_symbol_candidates": int(target_split_summary.get("symbol_candidates", 0) or 0),
        "target_split_existing_symbols": int(target_split_summary.get("existing_manual_symbols", 0) or 0),
        "target_split_function_splits": int(target_split_kinds.get("function-split", 0) or 0),
        "target_split_block_anchors": int(target_split_kinds.get("block-anchor", 0) or 0),
        "target_split_body_anchors": int(target_split_kinds.get("body-anchor", 0) or 0),
        "target_split_entry_adjacent": int(target_split_kinds.get("entry-adjacent", 0) or 0),
        "target_split_reshape": int(target_split_kinds.get("reshape-anchor", 0) or 0),
    }


def leaf_pseudocode_summary() -> dict[str, Any]:
    probe = read_json(ROOT / "analysis" / "leaf_pseudocode_c_probe.json", {})
    summary = probe.get("summary", {}) if isinstance(probe, dict) else {}
    results = probe.get("results", []) if isinstance(probe, dict) else []
    if not isinstance(results, list):
        results = []
    exact_rows = [row for row in results if row.get("category") == "exact"]
    return {
        "candidates": sum(int(value or 0) for value in summary.values()),
        "exact": int(summary.get("exact", 0) or 0),
        "codegen_near": int(summary.get("codegen-near", 0) or 0),
        "structural_near": int(summary.get("structural-near", 0) or 0),
        "semantic_started": int(summary.get("semantic-started", 0) or 0),
        "semantic_gap": int(summary.get("semantic-gap", 0) or 0),
        "compile_failed": int(summary.get("compile-fail", 0) or 0),
        "skip_inreg": int(summary.get("skip-inreg", 0) or 0),
        "compare_blocked": int(summary.get("compare-blocked", 0) or 0),
        "exact_target_instructions": sum(int(row.get("target_instruction_count", 0) or 0) for row in exact_rows),
        "promoted_source": "src/leaf_pseudocode_matches.c" if exact_rows else "",
    }


def boss_va_helper_lowering_summary() -> dict[str, Any]:
    probe = read_json(ROOT / "analysis" / "boss_va_helper_lowering_probe.json", {})
    summary = probe.get("summary", {}) if isinstance(probe, dict) else {}
    rows = probe.get("rows", []) if isinstance(probe, dict) else []
    baseline = probe.get("baseline", []) if isinstance(probe, dict) else []
    if not isinstance(rows, list):
        rows = []
    if not isinstance(baseline, list):
        baseline = []
    instruction_rows = [row for row in rows if isinstance(row, dict) and row.get("improved")]
    count_only_rows = [
        row
        for row in rows
        if isinstance(row, dict) and row.get("count_distance_improved") and not row.get("improved")
    ]
    exact_rows = [row for row in rows if isinstance(row, dict) and row.get("exact_match")]
    best_instruction_rows = sorted(
        instruction_rows,
        key=lambda row: (
            -int(row.get("delta_lcs_instruction_count", 0) or 0),
            -int(row.get("delta_matching_prefix", 0) or 0),
            str(row.get("function", "")),
            str(row.get("variant", "")),
        ),
    )[:5]
    return {
        "variants": int(summary.get("variants", 0) or 0),
        "functions": int(summary.get("functions", 0) or 0),
        "rows": int(summary.get("rows", len(rows)) or 0),
        "exact_rows": int(summary.get("exact_rows", len(exact_rows)) or 0),
        "instruction_improved_rows": int(summary.get("instruction_improved_rows", len(instruction_rows)) or 0),
        "count_distance_improved_rows": int(
            summary.get("count_distance_improved_rows", 0) or 0
        ),
        "count_only_improved_rows": int(
            summary.get("count_only_improved_rows", len(count_only_rows)) or 0
        ),
        "build_failed_rows": int(summary.get("build_failed_rows", 0) or 0),
        "baseline_exact_rows": int(summary.get("baseline_exact_rows", 0) or 0),
        "baseline_functions": len(baseline),
        "best_instruction_rows": [
            {
                "variant": str(row.get("variant", "")),
                "function": str(row.get("function", "")),
                "reason": str(row.get("improvement_reason", "")),
                "category": str(row.get("category", "")),
                "delta_lcs_instruction_count": int(row.get("delta_lcs_instruction_count", 0) or 0),
                "delta_matching_prefix": int(row.get("delta_matching_prefix", 0) or 0),
                "delta_count_distance": int(row.get("delta_count_distance", 0) or 0),
                "lcs_target_ratio": float(row.get("lcs_target_ratio", 0.0) or 0.0),
            }
            for row in best_instruction_rows
        ],
    }


def compile_profile_summary() -> dict[str, Any]:
    index = read_json(ROOT / "analysis" / "compile_profile_search" / "function_index.json", {})
    rows = index.get("rows", []) if isinstance(index, dict) else []
    if not isinstance(rows, list):
        rows = []
    profile_rows = [row for row in rows if isinstance(row, dict)]
    categories = Counter(str(row.get("category", "")) for row in profile_rows)
    category_order = {
        "exact-c": 0,
        "codegen-near": 1,
        "structural-near": 2,
        "semantic-started": 3,
        "semantic-gap": 4,
    }
    top_rows = sorted(
        profile_rows,
        key=lambda row: (
            category_order.get(str(row.get("category", "")), 99),
            -float(row.get("lcs_target_ratio", 0.0) or 0.0),
            -int(row.get("matching_prefix", 0) or 0),
            str(row.get("unit", "")),
            str(row.get("function", "")),
        ),
    )[:8]
    return {
        "profiled_units": len({str(row.get("unit", "")) for row in profile_rows if row.get("unit")}),
        "profiled_functions": len(profile_rows),
        "exact_rows": int(categories["exact-c"]),
        "codegen_near_rows": int(categories["codegen-near"]),
        "structural_near_rows": int(categories["structural-near"]),
        "semantic_started_rows": int(categories["semantic-started"]),
        "semantic_gap_rows": int(categories["semantic-gap"]),
        "top_rows": [
            {
                "unit": str(row.get("unit", "")),
                "compiler": str(row.get("compiler", "")),
                "function": str(row.get("function", "")),
                "profile": str(row.get("profile", "")),
                "optimization": str(row.get("optimization", "")),
                "cflags": str(row.get("cflags", "")),
                "category": str(row.get("category", "")),
                "target_instruction_count": int(row.get("target_instruction_count", 0) or 0),
                "compiled_instruction_count": int(row.get("compiled_instruction_count", 0) or 0),
                "matching_prefix": int(row.get("matching_prefix", 0) or 0),
                "lcs_instruction_count": int(row.get("lcs_instruction_count", 0) or 0),
                "lcs_target_ratio": float(row.get("lcs_target_ratio", 0.0) or 0.0),
            }
            for row in top_rows
        ],
    }


def c_reconstruction_frontier_summary() -> dict[str, Any]:
    frontier = read_json(ROOT / "analysis" / "c_reconstruction_frontier.json", {})
    summary = frontier.get("summary", {}) if isinstance(frontier, dict) else {}
    batches = frontier.get("file_batches", []) if isinstance(frontier, dict) else []
    top_batches = []
    if isinstance(batches, list):
        for batch in batches[:5]:
            if not isinstance(batch, dict):
                continue
            top_batches.append(
                {
                    "port_file": str(batch.get("port_file", "")),
                    "open_functions": int(batch.get("open_functions", 0) or 0),
                    "open_target_instructions": int(batch.get("open_target_instructions", 0) or 0),
                    "compiled_packets": int(batch.get("compiled_packets", 0) or 0),
                    "in_source_c_probes": int(batch.get("in_source_c_probes", 0) or 0),
                    "complete_split_spans": int(batch.get("complete_split_spans", 0) or 0),
                    "recommended_batch": str(batch.get("recommended_batch", "")),
                }
            )
    return {
        "functions": int(summary.get("functions", 0) or 0),
        "files": int(summary.get("files", 0) or 0),
        "open_functions": int(summary.get("open_functions", 0) or 0),
        "open_target_instructions": int(summary.get("open_target_instructions", 0) or 0),
        "ready_to_promote_functions": int(summary.get("ready_to_promote_functions", 0) or 0),
        "matched_real_c_functions": int(summary.get("matched_real_c_functions", 0) or 0),
        "matched_inline_c_functions": int(summary.get("matched_inline_c_functions", 0) or 0),
        "structured_c_batch_functions": int(summary.get("structured_c_batch_functions", 0) or 0),
        "compiled_packet_seed_functions": int(summary.get("compiled_packet_seed_functions", 0) or 0),
        "in_source_c_seed_functions": int(summary.get("in_source_c_seed_functions", 0) or 0),
        "compile_blocked_seed_functions": int(summary.get("compile_blocked_seed_functions", 0) or 0),
        "seed_needs_materialized_c_functions": int(
            summary.get("seed_needs_materialized_c_functions", 0) or 0
        ),
        "complete_split_spans": int(summary.get("complete_split_spans", 0) or 0),
        "top_batches": top_batches,
    }


def structured_summary() -> dict[str, Any]:
    units = read_csv(ROOT / "metadata" / "structured_port_units.csv")
    index_rows = {row.get("unit", ""): row for row in read_csv(ROOT / "analysis" / "structured_port_status" / "index.csv")}
    gate = read_json(ROOT / "analysis" / "structured_c_match_gate.json", {})
    gate_rows = gate.get("rows", []) if isinstance(gate, dict) else []
    exact_gate_rows = [
        row for row in gate_rows if isinstance(row, dict) and row.get("category") == "exact-c"
    ]
    unit_metrics = []

    for unit in units:
        unit_name = unit.get("unit", "")
        index = index_rows.get(unit_name, {})
        compare_rows = read_json(ROOT / unit.get("out_dir", "") / "compare_matched_objects.json", [])
        source = unit.get("source", "")
        source_exists = bool(source) and (ROOT / source).is_file()
        unit_metrics.append(
            {
                "unit": unit_name,
                "source": source,
                "source_exists": source_exists,
                "mapped_functions": int(index.get("mapped_functions", 0) or 0),
                "structured_started": int(index.get("structured_started", 0) or 0),
                "extracted_functions": int(index.get("extracted_functions", 0) or 0),
                "compiled_functions": len(compare_rows),
                "exact_functions": sum(1 for row in compare_rows if row.get("exact_match")),
                "source_nonblank_lines": count_nonblank_lines([source]) if source_exists else 0,
                "status_report": unit.get("status_report", ""),
            }
        )

    return {
        "units": len(unit_metrics),
        "source_files": len({row["source"] for row in unit_metrics if row["source_exists"]}),
        "mapped_functions": sum(row["mapped_functions"] for row in unit_metrics),
        "structured_started": sum(row["structured_started"] for row in unit_metrics),
        "extracted_functions": sum(row["extracted_functions"] for row in unit_metrics),
        "compiled_functions": sum(row["compiled_functions"] for row in unit_metrics),
        "exact_functions": sum(row["exact_functions"] for row in unit_metrics),
        "exact_real_c_functions": sum(1 for row in exact_gate_rows if row.get("implementation_style") == "plain-c"),
        "exact_inline_asm_functions": sum(
            1 for row in exact_gate_rows if row.get("implementation_style") == "c-inline-asm"
        ),
        "exact_naked_asm_seed_functions": sum(
            1 for row in exact_gate_rows if row.get("implementation_style") == "naked-asm"
        ),
        "source_nonblank_lines": sum(row["source_nonblank_lines"] for row in unit_metrics),
        "unit_rows": unit_metrics,
    }


def build_metrics() -> dict[str, Any]:
    manifest = read_json(ROOT / "build" / "matched" / "manifest.json", {})
    matched_sources = read_source_list(ROOT / "metadata" / "matched_sources.txt")
    compare_rows = read_json(ROOT / "build" / "matched" / "compare_matched_objects.json", [])
    default_compare = {str(row.get("name")): row for row in compare_rows}
    matched = {
        "source_files": len(matched_sources),
        "source_nonblank_lines": count_nonblank_lines(matched_sources),
        **compare_summary(compare_rows, dump_to_source_map(manifest)),
    }

    target = target_binary_summary()
    total_functions = int(target.get("disassembly_functions", 0) or 0)
    total_instructions = int(target.get("target_instructions", 0) or 0)
    matched.update(
        {
            "compared_function_percent_of_target": percent(matched["compared_functions"], total_functions),
            "exact_function_percent_of_target": percent(matched["exact_functions"], total_functions),
            "exact_plain_c_function_percent_of_target": percent(
                matched["exact_plain_c_functions"], total_functions
            ),
            "exact_inline_asm_function_percent_of_target": percent(
                matched["exact_inline_asm_functions"], total_functions
            ),
            "exact_naked_asm_function_percent_of_target": percent(
                matched["exact_naked_asm_functions"], total_functions
            ),
            "exact_instruction_percent_of_target": percent(
                matched["exact_target_instructions"], total_instructions
            ),
            "exact_file_percent_of_matched_sources": percent(matched["exact_files"], matched["source_files"]),
        }
    )

    return {
        "compiler": {
            "compiler": manifest.get("compiler", "gcc" if manifest.get("gcc") else ""),
            "requested_compiler": manifest.get("requested_compiler", ""),
            "compiler_executable": manifest.get("compiler_executable", manifest.get("gcc", "")),
            "armcc_probe_error": manifest.get("armcc_probe_error", ""),
            "objdump": manifest.get("objdump", ""),
            "optimization": manifest.get("optimization", ""),
            "matched_compare": rel(ROOT / "build" / "matched" / "compare_matched_objects.json"),
        },
        "target": target,
        "matched": matched,
        "structured": structured_summary(),
        "n64_reuse": n64_reuse_summary(),
        "n64_exact_seed": exact_seed_summary(default_compare),
        "direct_probe": direct_probe_summary(),
        "leaf_pseudocode_probe": leaf_pseudocode_summary(),
        "boss_va_helper_lowering_probe": boss_va_helper_lowering_summary(),
        "compile_profile_search": compile_profile_summary(),
        "c_reconstruction_frontier": c_reconstruction_frontier_summary(),
        "symbols": symbol_summary(),
        "n64_port_map": map_summary(),
    }


def write_markdown(path: Path, metrics: dict[str, Any]) -> None:
    target = metrics["target"]
    matched = metrics["matched"]
    structured = metrics["structured"]
    n64_reuse = metrics["n64_reuse"]
    exact_seed = metrics["n64_exact_seed"]
    direct_probe = metrics["direct_probe"]
    leaf_probe = metrics["leaf_pseudocode_probe"]
    boss_va_helper = metrics["boss_va_helper_lowering_probe"]
    compile_profiles = metrics["compile_profile_search"]
    c_frontier = metrics["c_reconstruction_frontier"]
    symbols = metrics["symbols"]
    port_map = metrics["n64_port_map"]
    compiler = metrics["compiler"]

    status_rows = ", ".join(f"{key}: {value}" for key, value in port_map["statuses"].items()) or "none"

    lines = [
        "# Porting Metrics",
        "",
        "Generated from the current compile/compare artifacts.",
        "",
        "## Compiler lane",
        "",
        f"- Compiler: `{compiler['compiler'] or 'unknown'}`",
        f"- Requested compiler: `{compiler['requested_compiler'] or 'unknown'}`",
        f"- Compiler executable: `{compiler['compiler_executable'] or 'missing'}`",
        f"- Objdump: `{compiler['objdump'] or 'missing'}`",
        f"- Optimization: `{compiler['optimization'] or 'unknown'}`",
        f"- ARMCC probe error: `{compiler['armcc_probe_error'] or 'none'}`",
        f"- Matched compare: `{compiler['matched_compare']}`",
        "",
        "## Target totals",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Ghidra exported function rows | {target['exported_function_rows']} |",
        f"| Target functions with disassembly | {target['disassembly_functions']} |",
        f"| Target instructions in disassembly | {target['target_instructions']} |",
        "",
        "## Matched baseline",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Source files in `metadata/matched_sources.txt` | {matched['source_files']} |",
        f"| Source files with compared functions | {matched['compared_files']} |",
        f"| Source files with exact functions | {matched['exact_files']} |",
        f"| Compared functions | {matched['compared_functions']} |",
        f"| Exact matched functions | {matched['exact_functions']} |",
        f"| Exact plain-C functions | {matched['exact_plain_c_functions']} |",
        f"| Exact inline-asm functions | {matched['exact_inline_asm_functions']} |",
        f"| Exact naked-asm functions | {matched['exact_naked_asm_functions']} |",
        f"| Exact unknown-style functions | {matched['exact_unknown_style_functions']} |",
        f"| Exact target instructions | {matched['exact_target_instructions']} |",
        f"| Nonblank source lines in matched files | {matched['source_nonblank_lines']} |",
        f"| Compared functions / target functions | {matched['compared_function_percent_of_target']:.4f}% |",
        f"| Exact functions / target functions | {matched['exact_function_percent_of_target']:.4f}% |",
        f"| Exact plain-C functions / target functions | {matched['exact_plain_c_function_percent_of_target']:.4f}% |",
        f"| Exact inline-asm functions / target functions | {matched['exact_inline_asm_function_percent_of_target']:.4f}% |",
        f"| Exact naked-asm functions / target functions | {matched['exact_naked_asm_function_percent_of_target']:.4f}% |",
        f"| Exact target instructions / target instructions | {matched['exact_instruction_percent_of_target']:.4f}% |",
        f"| Exact files / matched source files | {matched['exact_file_percent_of_matched_sources']:.4f}% |",
        "",
        "## Structured N64-derived ports",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Structured units | {structured['units']} |",
        f"| Structured source files | {structured['source_files']} |",
        f"| Mapped OOT3D functions | {structured['mapped_functions']} |",
        f"| Structured functions started | {structured['structured_started']} |",
        f"| Extracted N64 source functions | {structured['extracted_functions']} |",
        f"| Compiled structured functions | {structured['compiled_functions']} |",
        f"| Exact structured functions | {structured['exact_functions']} |",
        f"| Exact structured real-C functions | {structured['exact_real_c_functions']} |",
        f"| Exact structured inline-asm functions | {structured['exact_inline_asm_functions']} |",
        f"| Exact structured naked-asm seed functions | {structured['exact_naked_asm_seed_functions']} |",
        f"| Nonblank source lines in structured files | {structured['source_nonblank_lines']} |",
        "",
        "## N64 reuse vs real C",
        "",
        "This separates evidence that the N64 decompilation is useful from the stricter question of whether target-shaped C is already exact.",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Explicit N64/OOT3D mapped functions | {n64_reuse['mapped_functions']} |",
        f"| Mapped OOT3D port files | {n64_reuse['mapped_port_files']} |",
        f"| Explicit N64 source files used | {n64_reuse['mapped_n64_sources']} |",
        f"| Reused N64 source lanes | {n64_reuse['reused_source_lanes']} |",
        f"| OOT3D rows covered by reused lanes | {n64_reuse['reused_source_lane_rows']} |",
        f"| Adapter entries | {n64_reuse['adapter_entries']} |",
        f"| Rows with text adapter hits | {n64_reuse['rows_with_text_adapter_hits']} |",
        f"| Rows with source-lane hits | {n64_reuse['rows_with_source_lane_hits']} |",
        f"| Total text adapter hits | {n64_reuse['total_text_adapter_hits']} |",
        f"| Total source-lane hits | {n64_reuse['total_source_lane_hits']} |",
        f"| High-confidence conversion patterns | {n64_reuse['high_confidence_patterns']} |",
        f"| Medium-confidence conversion patterns | {n64_reuse['medium_confidence_patterns']} |",
        f"| N64-derived functions checked for C promotion | {n64_reuse['convertible_functions_checked']} |",
        f"| Already converted real C | {n64_reuse['already_converted_real_c']} |",
        f"| Already exact inline-asm C | {n64_reuse['already_converted_inline_asm']} |",
        f"| Exact asm seeds still needing C | {n64_reuse['exact_seed_needs_c']} |",
        f"| Exact asm seeds already moved to full inline asm | {n64_reuse['inline_exact_seed_needs_c']} |",
        f"| N64-derived functions needing batch reshaping | {n64_reuse['needs_batch_reshaping']} |",
        f"| N64-derived functions not yet convertible | {n64_reuse['not_yet_convertible']} |",
        f"| Structured gate exact functions | {n64_reuse['structured_exact_functions']} |",
        f"| Structured gate exact real-C functions | {n64_reuse['structured_exact_real_c_functions']} |",
        f"| Structured gate exact inline-asm functions | {n64_reuse['structured_exact_inline_asm_functions']} |",
        f"| Structured gate exact naked-seed functions | {n64_reuse['structured_exact_naked_seed_functions']} |",
        "",
        "Interpretation: the N64 decompilation is already useful as mappings, source lanes, adapters, and semantic structure. Exact asm seeds are matched assembly baselines, not recovered real C, whether they are still naked or have been moved to full inline asm; the current bottleneck is lowering those N64-derived lanes into the target ARM compiler shape.",
        "",
        "## BossVa Helper-Lowering Probe",
        "",
        "This N64-derived lane tests mechanical helper/accessor rewrites against the OOT3D target. It is diagnostic; rows are promoted only when they become exact.",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Variants built | {boss_va_helper['variants']} |",
        f"| Functions checked per variant | {boss_va_helper['functions']} |",
        f"| Variant rows | {boss_va_helper['rows']} |",
        f"| Exact rows | {boss_va_helper['exact_rows']} |",
        f"| Instruction-improved rows | {boss_va_helper['instruction_improved_rows']} |",
        f"| Count-distance-improved rows | {boss_va_helper['count_distance_improved_rows']} |",
        f"| Count-only improved rows | {boss_va_helper['count_only_improved_rows']} |",
        f"| Build-failed rows | {boss_va_helper['build_failed_rows']} |",
        f"| Baseline exact rows | {boss_va_helper['baseline_exact_rows']} |",
        "",
        "| Variant | Function | Reason | Category | dLCS | dPrefix | dCountDist | Ratio |",
        "| --- | --- | --- | --- | ---: | ---: | ---: | ---: |",
    ]
    for row in boss_va_helper["best_instruction_rows"]:
        lines.append(
            f"| `{row['variant']}` | `{row['function']}` | `{row['reason']}` | `{row['category']}` | "
            f"{row['delta_lcs_instruction_count']} | {row['delta_matching_prefix']} | "
            f"{row['delta_count_distance']} | {row['lcs_target_ratio']:.4f} |"
        )
    if not boss_va_helper["best_instruction_rows"]:
        lines.append("| - | - | - | - | 0 | 0 | 0 | 0.0000 |")

    lines.extend(
        [
        "",
        "## Compile profile search",
        "",
        "This ranks compiler profiles for N64-derived structured C before manual source reshaping.",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Profiled units | {compile_profiles['profiled_units']} |",
        f"| Profiled functions | {compile_profiles['profiled_functions']} |",
        f"| Exact profile rows | {compile_profiles['exact_rows']} |",
        f"| Codegen-near profile rows | {compile_profiles['codegen_near_rows']} |",
        f"| Structural-near profile rows | {compile_profiles['structural_near_rows']} |",
        f"| Semantic-started profile rows | {compile_profiles['semantic_started_rows']} |",
        f"| Semantic-gap profile rows | {compile_profiles['semantic_gap_rows']} |",
        "",
        "| Unit | Function | Profile | Opt | Flags | Category | Target | Compiled | Prefix | LCS | Ratio |",
        "| --- | --- | --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: |",
        ]
    )
    for row in compile_profiles["top_rows"]:
        flags = row["cflags"] or "-"
        lines.append(
            f"| `{row['unit']}` | `{row['function']}` | `{row['profile']}` | `{row['optimization']}` | "
            f"`{flags}` | `{row['category']}` | {row['target_instruction_count']} | "
            f"{row['compiled_instruction_count']} | {row['matching_prefix']} | "
            f"{row['lcs_instruction_count']} | {row['lcs_target_ratio']:.4f} |"
        )
    if not compile_profiles["top_rows"]:
        lines.append("| - | - | - | - | - | - | 0 | 0 | 0 | 0 | 0.0000 |")

    lines.extend(
        [
        "",
        "## Direct C probe batch",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Materialized packets probed | {direct_probe['packets']} |",
        f"| Probe packets compiled | {direct_probe['compiled']} |",
        f"| Probe packets dumped | {direct_probe['dumped']} |",
        f"| Probe packets failed | {direct_probe['failed']} |",
        f"| Probe compile errors | {direct_probe['compile_errors']} |",
        f"| Probe target comparisons | {direct_probe['compared']} |",
        f"| Probe exact C functions | {direct_probe['exact_c']} |",
        f"| Probe codegen-near functions | {direct_probe['codegen_near']} |",
        f"| Probe structural-near functions | {direct_probe['structural_near']} |",
        f"| Probe semantic-started functions | {direct_probe['semantic_started']} |",
        f"| Probe semantic-gap functions | {direct_probe['semantic_gap']} |",
        f"| Template lanes materialized | {direct_probe['template_lanes']} |",
        f"| Template targets materialized | {direct_probe['template_targets']} |",
        f"| Template rewrite hits | {direct_probe['template_rewrite_hits']} |",
        f"| Template packets compiled | {direct_probe['template_compiled']} |",
        f"| Template packets dumped | {direct_probe['template_dumped']} |",
        f"| Template packets failed | {direct_probe['template_failed']} |",
        f"| Template compile errors | {direct_probe['template_compile_errors']} |",
        f"| Template target comparisons | {direct_probe['template_compared']} |",
        f"| Template exact C functions | {direct_probe['template_exact_c']} |",
        f"| Template codegen-near functions | {direct_probe['template_codegen_near']} |",
        f"| Template structural-near functions | {direct_probe['template_structural_near']} |",
        f"| Template semantic-started functions | {direct_probe['template_semantic_started']} |",
        f"| Template semantic-gap functions | {direct_probe['template_semantic_gap']} |",
        f"| Symbol-scan packets | {direct_probe['symbol_scan_packets']} |",
        f"| Symbol-scan compiled symbols | {direct_probe['symbol_scan_symbols']} |",
        f"| Symbol-scan exact/slice-exact bests | {direct_probe['symbol_scan_exact'] + direct_probe['symbol_scan_slice_exact']} |",
        f"| Symbol-scan structural-near bests | {direct_probe['symbol_scan_structural_near']} |",
        f"| Symbol-scan semantic-started bests | {direct_probe['symbol_scan_semantic_started']} |",
        f"| Symbol-scan semantic-gap bests | {direct_probe['symbol_scan_semantic_gap']} |",
        f"| Symbol-scan best differs from mapped N64 name | {direct_probe['symbol_scan_best_differs']} |",
        f"| Split/helper workorders | {direct_probe['split_workorders']} |",
        f"| Split/helper OOT3D entries covered | {direct_probe['split_entries']} |",
        f"| Split/helper candidate symbols | {direct_probe['split_candidate_symbols']} |",
        f"| Split/helper source spans found | {direct_probe['split_source_spans_found']} |",
        f"| Split/helper complete source spans | {direct_probe['split_complete_spans']} |",
        f"| Split/helper truncated source spans | {direct_probe['split_truncated_spans']} |",
        f"| Split/helper best-helper rows | {direct_probe['split_best_helpers']} |",
        f"| Split/helper alternate-helper rows | {direct_probe['split_alternate_helpers']} |",
        f"| Split/helper mapped-symbol reshape rows | {direct_probe['split_mapped_symbol_reshape']} |",
        f"| Split/helper source candidates probed | {direct_probe['split_candidate_spans']} |",
        f"| Split/helper source candidates compiled | {direct_probe['split_candidate_compiled']} |",
        f"| Split/helper source candidates compile-blocked | {direct_probe['split_candidate_compile_blocked']} |",
        f"| Split/helper source candidates exact | {direct_probe['split_candidate_exact']} |",
        f"| Split/helper source candidates codegen-near | {direct_probe['split_candidate_codegen_near']} |",
        f"| Split/helper source candidates structural-near | {direct_probe['split_candidate_structural_near']} |",
        f"| Split/helper source candidates semantic-started | {direct_probe['split_candidate_semantic_started']} |",
        f"| Split/helper source candidates semantic-gap | {direct_probe['split_candidate_semantic_gap']} |",
        f"| Target split suggestions | {direct_probe['target_split_suggestions']} |",
        f"| Target split OOT3D entries covered | {direct_probe['target_split_entries']} |",
        f"| Target split inferred addresses | {direct_probe['target_split_inferred_addresses']} |",
        f"| Target split manual-symbol candidates | {direct_probe['target_split_symbol_candidates']} |",
        f"| Target split existing manual symbols | {direct_probe['target_split_existing_symbols']} |",
        f"| Target split function-split rows | {direct_probe['target_split_function_splits']} |",
        f"| Target split block-anchor rows | {direct_probe['target_split_block_anchors']} |",
        f"| Target split body-anchor rows | {direct_probe['target_split_body_anchors']} |",
        f"| Target split entry-adjacent rows | {direct_probe['target_split_entry_adjacent']} |",
        f"| Target split reshape-anchor rows | {direct_probe['target_split_reshape']} |",
        "",
        "## Leaf Pseudocode C Probe",
        "",
        "This lane is generated from Ghidra leaf pseudocode and is tracked separately from N64-derived ports.",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Leaf candidates probed | {leaf_probe['candidates']} |",
        f"| Exact plain-C leaf candidates | {leaf_probe['exact']} |",
        f"| Exact leaf target instructions | {leaf_probe['exact_target_instructions']} |",
        f"| Codegen-near leaf candidates | {leaf_probe['codegen_near']} |",
        f"| Structural-near leaf candidates | {leaf_probe['structural_near']} |",
        f"| Semantic-started leaf candidates | {leaf_probe['semantic_started']} |",
        f"| Semantic-gap leaf candidates | {leaf_probe['semantic_gap']} |",
        f"| Compile-failed leaf candidates | {leaf_probe['compile_failed']} |",
        f"| Register-input skipped leaf candidates | {leaf_probe['skip_inreg']} |",
        f"| Compare-blocked leaf candidates | {leaf_probe['compare_blocked']} |",
        f"| Promoted leaf source | `{leaf_probe['promoted_source'] or '-'}` |",
        "",
        "## C Reconstruction Frontier",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Functions tracked | {c_frontier['functions']} |",
        f"| Files tracked | {c_frontier['files']} |",
        f"| Open C-reconstruction functions | {c_frontier['open_functions']} |",
        f"| Open target instructions | {c_frontier['open_target_instructions']} |",
        f"| Ready-to-promote C functions | {c_frontier['ready_to_promote_functions']} |",
        f"| Matched real C functions | {c_frontier['matched_real_c_functions']} |",
        f"| Matched inline-C functions | {c_frontier['matched_inline_c_functions']} |",
        f"| Structured C batch functions | {c_frontier['structured_c_batch_functions']} |",
        f"| Exact-seed functions with compiled packets | {c_frontier['compiled_packet_seed_functions']} |",
        f"| Exact-seed functions with in-source C probes | {c_frontier['in_source_c_seed_functions']} |",
        f"| Exact-seed functions with compile-blocked packets | {c_frontier['compile_blocked_seed_functions']} |",
        f"| Exact-seed functions needing materialized C | {c_frontier['seed_needs_materialized_c_functions']} |",
        f"| Complete split/helper source spans | {c_frontier['complete_split_spans']} |",
        "",
        "| Top file batch | Open funcs | Open insns | Packets | In-source C | Split spans | Action |",
        "| --- | ---: | ---: | ---: | ---: | ---: | --- |",
        ]
    )
    for batch in c_frontier["top_batches"]:
        lines.append(
            f"| `{batch['port_file']}` | {batch['open_functions']} | {batch['open_target_instructions']} | "
            f"{batch['compiled_packets']} | {batch['in_source_c_probes']} | "
            f"{batch['complete_split_spans']} | {batch['recommended_batch']} |"
        )

    lines.extend(
        [
            "",
        "## N64 Exact-Seed Batch",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Exact-seed mapped functions | {exact_seed['mapped_functions']} |",
        f"| Exact-seed port files | {exact_seed['port_files']} |",
        f"| Exact-seed N64 source files | {exact_seed['n64_sources']} |",
        f"| Compared exact-seed functions | {exact_seed['default_compared']} |",
        f"| Matched exact-seed functions | {exact_seed['default_exact']} |",
        f"| Matched exact-seed target instructions | {exact_seed['exact_target_instructions']} |",
        "",
        "| Unit | Source | Mapped | Compiled | Exact | Report |",
        "| --- | --- | ---: | ---: | ---: | --- |",
    ]
    )

    for row in structured["unit_rows"]:
        source_state = "yes" if row["source_exists"] else "missing"
        lines.append(
            f"| `{row['unit']}` | `{row['source']}` ({source_state}) | "
            f"{row['mapped_functions']} | {row['compiled_functions']} | {row['exact_functions']} | "
            f"`{row['status_report']}` |"
        )

    if not structured["unit_rows"]:
        lines.append("| - | - | 0 | 0 | 0 | - |")

    lines.extend(
        [
            "",
            "## Symbols and N64 map",
            "",
            "| Metric | Value |",
            "| --- | ---: |",
            f"| Manual function symbols | {symbols['manual_function_symbols']} |",
            f"| Manual function symbols applied in Ghidra export | {symbols['manual_function_symbols_applied']} |",
            f"| Manual function symbols pending Ghidra export | {symbols['manual_function_symbols_pending_export']} |",
            f"| Data symbols | {symbols['data_symbols']} |",
            f"| Explicit N64/OOT3D mapped functions | {port_map['mapped_functions']} |",
            f"| Explicit N64/OOT3D mapped port files | {port_map['mapped_port_files']} |",
            f"| Explicit N64 source files used | {port_map['mapped_n64_sources']} |",
            "",
            f"Map statuses: {status_rows}.",
            "",
        ]
    )

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-json", type=Path, default=ROOT / "analysis" / "porting_metrics.json")
    parser.add_argument("--out-md", type=Path, default=ROOT / "analysis" / "porting_metrics.md")
    args = parser.parse_args()

    metrics = build_metrics()
    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps(metrics, indent=2) + "\n", encoding="utf-8")
    write_markdown(args.out_md, metrics)

    matched = metrics["matched"]
    structured = metrics["structured"]
    print(
        "matched exact: "
        f"{matched['exact_functions']}/{matched['compared_functions']} functions, "
        f"{matched['exact_function_percent_of_target']:.4f}% of target functions, "
        f"{matched['exact_files']}/{matched['compared_files']} files; "
        f"plain-C exact: {matched['exact_plain_c_functions']}; "
        f"instructions: {matched['exact_instruction_percent_of_target']:.4f}% of target; "
        "structured compiled: "
        f"{structured['compiled_functions']} functions, "
        f"{structured['source_files']} files, "
        f"{structured['exact_functions']} exact"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
