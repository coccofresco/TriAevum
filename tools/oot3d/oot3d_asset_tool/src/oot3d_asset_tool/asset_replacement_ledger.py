from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any

from .binary import ParseError


LEDGER_FORMAT = "oot3d_asset_replacement_readiness_ledger_v1"
RUNTIME_SAFE_STATUSES = {"runtime_route_enabled", "runtime_draw_dump_valid"}
REPLACEMENT_PROGRESS_STATUSES = {
    "decomp_frontier_open",
    "fallback_only",
    "offline_export_ready",
    "offline_export_partial",
    "offline_package_ready",
    "offline_package_invalid",
    "runtime_capture_pending",
    "runtime_blocked",
    "runtime_contract_invalid",
    "runtime_parity_invalid",
}


def audit_asset_replacement_ledger(
    work_root: Path,
    output_path: Path,
    *,
    decomp_support_root: Path | None = None,
    code_bin_path: Path | None = None,
) -> dict[str, object]:
    if not work_root.is_dir():
        raise ParseError(f"{work_root}: expected generated OOT3D work root")

    ctx = LedgerContext(work_root)
    decomp_ctx = DecompSupportContext(decomp_support_root) if decomp_support_root is not None else None
    records = [
        extraction_record(ctx),
        exefs_code_decompilation_record(decomp_ctx, code_bin_path),
        romfs_inventory_record(ctx),
        static_actor_record(ctx),
        rigid_multibone_record(ctx),
        skinned_bind_pose_record(ctx),
        skinned_animation_record(ctx),
        ctxb_record(ctx),
        kankyo_record(ctx),
        prerendered_room_record(ctx),
        collision_record(ctx),
        kokiri_route_record(ctx),
        link_child_character_record(ctx),
        media_record(ctx),
        audio_record(ctx),
        q_format_record(ctx),
        moflex_record(ctx),
    ]
    records = [record for record in records if record is not None]
    status_counts: dict[str, int] = {}
    for record in records:
        status = str(record.get("status", "unknown"))
        status_counts[status] = status_counts.get(status, 0) + 1
    runtime_safety_summary = summarize_runtime_safety(records)
    next_action_queue = build_next_action_queue(runtime_safety_summary)

    ledger = {
        "format": LEDGER_FORMAT,
        "work_root": str(work_root),
        "decomp_support_root": str(decomp_support_root) if decomp_support_root is not None else None,
        "code_bin_path": str(code_bin_path) if code_bin_path is not None else None,
        "record_count": len(records),
        "status_counts": {key: status_counts[key] for key in sorted(status_counts)},
        "runtime_safety_summary": runtime_safety_summary,
        "next_action_queue": next_action_queue,
        "records": records,
        "policy": (
            "A family is runtime-ready only when a mounted runtime contract or route gate exists. "
            "Offline exports/packages are counted as progress but remain fallback-only until a runtime "
            "loader, install path, and verifier prove safe substitution."
        ),
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(ledger, indent=2) + "\n", encoding="utf-8", newline="\n")
    return ledger


class LedgerContext:
    def __init__(self, work_root: Path) -> None:
        self.work_root = work_root

    def read(self, relative_path: str) -> dict[str, Any] | None:
        path = self.work_root / relative_path
        if not path.is_file():
            return None
        value = json.loads(path.read_text(encoding="utf-8-sig"))
        return value if isinstance(value, dict) else None

    def path(self, relative_path: str) -> str:
        return str(self.work_root / relative_path)


class DecompSupportContext:
    def __init__(self, root: Path) -> None:
        self.root = root

    def read(self, relative_path: str) -> dict[str, Any] | None:
        path = self.root / relative_path
        if not path.is_file():
            return None
        value = json.loads(path.read_text(encoding="utf-8-sig"))
        return value if isinstance(value, dict) else None

    def path(self, relative_path: str) -> str:
        return str(self.root / relative_path)


def base_record(
    asset_family: str,
    status: str,
    summary_path: str,
    metrics: dict[str, object],
    *,
    runtime_scope: str,
    next_gate: str,
) -> dict[str, object]:
    return {
        "asset_family": asset_family,
        "status": status,
        "summary_path": summary_path,
        "runtime_scope": runtime_scope,
        "metrics": metrics,
        "next_gate": next_gate,
    }


def extraction_record(ctx: LedgerContext) -> dict[str, object] | None:
    rel = "extraction_classification/oot3d_extraction_classification.summary.json"
    data = ctx.read(rel)
    if data is None:
        return missing_record("extraction_classification", ctx.path(rel))
    return base_record(
        "extraction_classification",
        "inventory_ready",
        ctx.path(rel),
        {
            "romfs_interpret_or_convert_file_count": data.get("romfs_interpret_or_convert_file_count", 0),
            "exefs_decompile_code_file_count": data.get("exefs_decompile_code_file_count", 0),
            "effective_gameplay_script_file_count": data.get("effective_gameplay_script_file_count", 0),
            "issue_count": len(data.get("issues", [])) if isinstance(data.get("issues"), list) else 0,
        },
        runtime_scope="global_extraction",
        next_gate="Keep this baseline current when the extraction changes.",
    )


def exefs_code_decompilation_record(
    ctx: DecompSupportContext | None,
    code_bin_path: Path | None,
) -> dict[str, object] | None:
    if ctx is None:
        return missing_record("exefs_code_decompilation", "<decomp_support_root not provided>")
    build = ctx.read("metadata/build_summary.json")
    readiness = ctx.read("analysis/c_conversion_readiness.json")
    frontier = ctx.read("analysis/c_reconstruction_frontier.json")
    convertible = ctx.read("analysis/convertible_ports.json")
    materialized = ctx.read("analysis/direct_conversion_materialized/index.json")
    compile_probe = ctx.read("analysis/direct_packet_compile_probe.json")
    probe_match = ctx.read("analysis/direct_packet_probe_match.json")
    symbol_scan = ctx.read("analysis/direct_packet_symbol_scan.json")
    split_promotion_queue = ctx.read("analysis/direct_split_promotion_queue.json")
    split_gap_report = ctx.read("analysis/direct_split_gap_report.json")
    target_split_suggestions = ctx.read("analysis/direct_target_split_suggestions.json")
    target_envelope_plan = ctx.read("analysis/direct_target_envelope_plan.json")
    target_window_probe = ctx.read("analysis/direct_target_window_probe_match.json")
    target_window_refinement = ctx.read("analysis/direct_target_window_refinement.json")
    window_adapter_queue = ctx.read("analysis/direct_window_adapter_queue.json")
    window_adapter_probes = ctx.read("analysis/direct_window_adapter_probes.json")
    window_adapter_probe_diagnostics = ctx.read("analysis/direct_window_adapter_probe_diagnostics.json")
    data_like_boundary_refinement = ctx.read("analysis/direct_data_like_boundary_refinement.json")
    semantic_call_shape = ctx.read("analysis/direct_semantic_call_shape.json")
    source_subregion_plan = ctx.read("analysis/direct_source_subregion_plan.json")
    source_subregion_probe = ctx.read("analysis/direct_source_subregion_probe.json")
    source_subregion_sweep = ctx.read("analysis/direct_source_subregion_sweep.json")
    source_subregion_seed_qualification = ctx.read("analysis/direct_source_subregion_seed_qualification.json")
    control_flow_isolation_workorders = ctx.read("analysis/direct_control_flow_isolation_workorders.json")
    control_flow_subblock_probe = ctx.read("analysis/direct_control_flow_subblock_probe.json")
    control_flow_subblock_seed_qualification = ctx.read(
        "analysis/direct_control_flow_subblock_seed_qualification.json"
    )
    tail_call_seed_targets = ctx.read("analysis/direct_tail_call_seed_targets.json")
    tail_call_target_identity = ctx.read("analysis/direct_tail_call_target_identity.json")
    tail_call_target_body_shapes = ctx.read("analysis/direct_tail_call_target_body_shapes.json")
    state_mode_table = ctx.read("analysis/direct_state_mode_table.json")
    state_mode_switch_map = ctx.read("analysis/direct_state_mode_switch_map.json")
    state_mode_handler_context = ctx.read("analysis/direct_state_mode_handler_context.json")
    state_mode_source_correlation = ctx.read("analysis/direct_state_mode_source_correlation.json")
    state_mode_source_workorders = ctx.read("analysis/direct_state_mode_source_workorders.json")
    state_mode_source_workorder_probe = ctx.read("analysis/direct_state_mode_source_workorder_probe.json")
    state_mode_source_workorder_seed_qualification = ctx.read(
        "analysis/direct_state_mode_source_workorder_seed_qualification.json"
    )
    state_mode_source_call_shape = ctx.read("analysis/direct_state_mode_source_call_shape.json")
    frontier_blocker_synthesis = ctx.read("analysis/direct_frontier_blocker_synthesis.json")
    structured_lowering_blockers = ctx.read("analysis/direct_structured_lowering_blockers.json")
    structured_lowering_probe = ctx.read("analysis/direct_structured_lowering_probe.json")
    structured_lowering_seed_qualification = ctx.read(
        "analysis/direct_structured_lowering_seed_qualification.json"
    )
    structured_lowering_isolation = ctx.read("analysis/direct_structured_lowering_isolation.json")
    structured_state_gate_variants = ctx.read("analysis/direct_structured_state_gate_variants.json")
    structured_smooth_variants = ctx.read("analysis/direct_structured_smooth_variants.json")
    smooth_tail_codegen_probe = ctx.read("analysis/direct_smooth_tail_codegen_probe.json")
    state_gate_codegen_probe = ctx.read("analysis/direct_state_gate_codegen_probe.json")
    if build is None:
        return missing_record("exefs_code_decompilation", ctx.path("metadata/build_summary.json"))
    expected_code_hash = str(build.get("code_bin_sha256") or "")
    actual_code_hash = sha256_file(code_bin_path) if code_bin_path is not None and code_bin_path.is_file() else None
    code_hash_matches = actual_code_hash is not None and actual_code_hash.upper() == expected_code_hash.upper()
    readiness_summary = require_dict(readiness.get("summary")) if isinstance(readiness, dict) else {}
    frontier_summary = require_dict(frontier.get("summary")) if isinstance(frontier, dict) else {}
    convertible_summary = require_dict(convertible.get("summary")) if isinstance(convertible, dict) else {}
    materialized_summary = require_dict(materialized.get("summary")) if isinstance(materialized, dict) else {}
    compile_probe_summary = require_dict(compile_probe.get("summary")) if isinstance(compile_probe, dict) else {}
    compile_probe_blockers = require_dict(compile_probe_summary.get("primary_blockers"))
    probe_match_summary = require_dict(probe_match.get("summary")) if isinstance(probe_match, dict) else {}
    symbol_scan_summary = require_dict(symbol_scan.get("summary")) if isinstance(symbol_scan, dict) else {}
    split_promotion_summary = (
        require_dict(split_promotion_queue.get("summary"))
        if isinstance(split_promotion_queue, dict)
        else {}
    )
    split_gap_summary = (
        require_dict(split_gap_report.get("summary"))
        if isinstance(split_gap_report, dict)
        else {}
    )
    target_split_summary = (
        require_dict(target_split_suggestions.get("summary"))
        if isinstance(target_split_suggestions, dict)
        else {}
    )
    target_envelope_summary = (
        require_dict(target_envelope_plan.get("summary"))
        if isinstance(target_envelope_plan, dict)
        else {}
    )
    target_window_summary = (
        require_dict(target_window_probe.get("summary"))
        if isinstance(target_window_probe, dict)
        else {}
    )
    target_window_refinement_summary = (
        require_dict(target_window_refinement.get("summary"))
        if isinstance(target_window_refinement, dict)
        else {}
    )
    window_adapter_summary = (
        require_dict(window_adapter_queue.get("summary"))
        if isinstance(window_adapter_queue, dict)
        else {}
    )
    window_adapter_probe_summary = (
        require_dict(window_adapter_probes.get("summary"))
        if isinstance(window_adapter_probes, dict)
        else {}
    )
    window_adapter_probe_diagnostics_summary = (
        require_dict(window_adapter_probe_diagnostics.get("summary"))
        if isinstance(window_adapter_probe_diagnostics, dict)
        else {}
    )
    data_like_boundary_refinement_summary = (
        require_dict(data_like_boundary_refinement.get("summary"))
        if isinstance(data_like_boundary_refinement, dict)
        else {}
    )
    semantic_call_shape_summary = (
        require_dict(semantic_call_shape.get("summary"))
        if isinstance(semantic_call_shape, dict)
        else {}
    )
    source_subregion_plan_summary = (
        require_dict(source_subregion_plan.get("summary"))
        if isinstance(source_subregion_plan, dict)
        else {}
    )
    source_subregion_probe_summary = (
        require_dict(source_subregion_probe.get("summary"))
        if isinstance(source_subregion_probe, dict)
        else {}
    )
    source_subregion_sweep_summary = (
        require_dict(source_subregion_sweep.get("summary"))
        if isinstance(source_subregion_sweep, dict)
        else {}
    )
    source_subregion_seed_qualification_summary = (
        require_dict(source_subregion_seed_qualification.get("summary"))
        if isinstance(source_subregion_seed_qualification, dict)
        else {}
    )
    control_flow_isolation_workorders_summary = (
        require_dict(control_flow_isolation_workorders.get("summary"))
        if isinstance(control_flow_isolation_workorders, dict)
        else {}
    )
    control_flow_subblock_probe_summary = (
        require_dict(control_flow_subblock_probe.get("summary"))
        if isinstance(control_flow_subblock_probe, dict)
        else {}
    )
    control_flow_subblock_seed_qualification_summary = (
        require_dict(control_flow_subblock_seed_qualification.get("summary"))
        if isinstance(control_flow_subblock_seed_qualification, dict)
        else {}
    )
    tail_call_seed_targets_summary = (
        require_dict(tail_call_seed_targets.get("summary"))
        if isinstance(tail_call_seed_targets, dict)
        else {}
    )
    tail_call_target_identity_summary = (
        require_dict(tail_call_target_identity.get("summary"))
        if isinstance(tail_call_target_identity, dict)
        else {}
    )
    tail_call_target_body_shapes_summary = (
        require_dict(tail_call_target_body_shapes.get("summary"))
        if isinstance(tail_call_target_body_shapes, dict)
        else {}
    )
    state_mode_table_summary = (
        require_dict(state_mode_table.get("summary"))
        if isinstance(state_mode_table, dict)
        else {}
    )
    state_mode_switch_map_summary = (
        require_dict(state_mode_switch_map.get("summary"))
        if isinstance(state_mode_switch_map, dict)
        else {}
    )
    state_mode_handler_context_summary = (
        require_dict(state_mode_handler_context.get("summary"))
        if isinstance(state_mode_handler_context, dict)
        else {}
    )
    state_mode_source_correlation_summary = (
        require_dict(state_mode_source_correlation.get("summary"))
        if isinstance(state_mode_source_correlation, dict)
        else {}
    )
    state_mode_source_workorders_summary = (
        require_dict(state_mode_source_workorders.get("summary"))
        if isinstance(state_mode_source_workorders, dict)
        else {}
    )
    state_mode_source_workorder_probe_summary = (
        require_dict(state_mode_source_workorder_probe.get("summary"))
        if isinstance(state_mode_source_workorder_probe, dict)
        else {}
    )
    state_mode_source_workorder_seed_qualification_summary = (
        require_dict(state_mode_source_workorder_seed_qualification.get("summary"))
        if isinstance(state_mode_source_workorder_seed_qualification, dict)
        else {}
    )
    state_mode_source_call_shape_summary = (
        require_dict(state_mode_source_call_shape.get("summary"))
        if isinstance(state_mode_source_call_shape, dict)
        else {}
    )
    frontier_blocker_synthesis_summary = (
        require_dict(frontier_blocker_synthesis.get("summary"))
        if isinstance(frontier_blocker_synthesis, dict)
        else {}
    )
    structured_lowering_blockers_summary = (
        require_dict(structured_lowering_blockers.get("summary"))
        if isinstance(structured_lowering_blockers, dict)
        else {}
    )
    structured_lowering_probe_summary = (
        require_dict(structured_lowering_probe.get("summary")) if isinstance(structured_lowering_probe, dict) else {}
    )
    structured_lowering_seed_qualification_summary = (
        require_dict(structured_lowering_seed_qualification.get("summary"))
        if isinstance(structured_lowering_seed_qualification, dict)
        else {}
    )
    structured_lowering_isolation_summary = (
        require_dict(structured_lowering_isolation.get("summary"))
        if isinstance(structured_lowering_isolation, dict)
        else {}
    )
    structured_state_gate_variants_summary = (
        require_dict(structured_state_gate_variants.get("summary"))
        if isinstance(structured_state_gate_variants, dict)
        else {}
    )
    structured_smooth_variants_summary = (
        require_dict(structured_smooth_variants.get("summary"))
        if isinstance(structured_smooth_variants, dict)
        else {}
    )
    smooth_tail_codegen_probe_summary = (
        require_dict(smooth_tail_codegen_probe.get("summary"))
        if isinstance(smooth_tail_codegen_probe, dict)
        else {}
    )
    state_gate_codegen_probe_summary = (
        require_dict(state_gate_codegen_probe.get("summary")) if isinstance(state_gate_codegen_probe, dict) else {}
    )
    frontier_rows = frontier.get("rows") if isinstance(frontier, dict) else []
    frontier_samples = frontier_open_function_samples(frontier_rows, ctx.root)
    open_functions = int(frontier_summary.get("open_functions", 0) or 0)
    ready_to_promote = int(frontier_summary.get("ready_to_promote_functions", 0) or 0)
    status = "decomp_frontier_open" if open_functions > 0 or ready_to_promote == 0 else "decomp_ready_to_promote"
    return base_record(
        "exefs_code_decompilation",
        status,
        ctx.path("metadata/build_summary.json"),
        {
            "code_bin_sha256": build.get("code_bin_sha256"),
            "actual_code_bin_path": str(code_bin_path) if code_bin_path is not None else None,
            "actual_code_bin_sha256": actual_code_hash,
            "code_bin_hash_matches_extract": code_hash_matches,
            "code_base": build.get("code_base"),
            "ghidra_functions": build.get("ghidra_functions", 0),
            "decompiled_c_like_files": build.get("decompiled_files", 0),
            "readiness_rows": readiness_summary.get("rows", 0),
            "matched_c_functions": readiness_summary.get("matched_c_functions", 0),
            "matched_inline_seed_asm_functions": readiness_summary.get("matched_inline_seed_asm_functions", 0),
            "batch_lowering_functions": readiness_summary.get("batch_lowering_functions", 0),
            "frontier_open_functions": frontier_summary.get("open_functions", 0),
            "frontier_open_target_instructions": frontier_summary.get("open_target_instructions", 0),
            "frontier_ready_to_promote_functions": frontier_summary.get("ready_to_promote_functions", 0),
            "frontier_structured_c_batch_functions": frontier_summary.get("structured_c_batch_functions", 0),
            "frontier_compiled_packet_seed_functions": frontier_summary.get("compiled_packet_seed_functions", 0),
            "frontier_in_source_c_seed_functions": frontier_summary.get("in_source_c_seed_functions", 0),
            "frontier_compile_blocked_seed_functions": frontier_summary.get("compile_blocked_seed_functions", 0),
            "frontier_complete_split_spans": frontier_summary.get("complete_split_spans", 0),
            "convertible_ports_functions": convertible_summary.get("functions", 0),
            "convertible_ports_needs_batch_reshaping": convertible_summary.get("needs_batch_reshaping", 0),
            "convertible_ports_ready_to_promote": convertible_summary.get("ready_to_promote", 0),
            "direct_materialized_lanes": materialized_summary.get("lanes", 0),
            "direct_materialized_targets": materialized_summary.get("targets", 0),
            "direct_materialized_rewrite_hits": materialized_summary.get("rewrite_hits", 0),
            "direct_compile_probe_packets": compile_probe_summary.get("packets", 0),
            "direct_compile_probe_compiled": compile_probe_summary.get("compiled", 0),
            "direct_compile_probe_dumped": compile_probe_summary.get("dumped", 0),
            "direct_compile_probe_failed": compile_probe_summary.get("failed", 0),
            "direct_compile_probe_total_errors": compile_probe_summary.get("total_errors", 0),
            "direct_compile_probe_primary_blockers": compile_probe_blockers,
            "direct_probe_match_compared": probe_match_summary.get("compared_probes", 0),
            "direct_probe_match_exact_c": probe_match_summary.get("exact_c", 0),
            "direct_probe_match_codegen_near": probe_match_summary.get("codegen_near", 0),
            "direct_probe_match_structural_near": probe_match_summary.get("structural_near", 0),
            "direct_probe_match_semantic_started": probe_match_summary.get("semantic_started", 0),
            "direct_probe_match_semantic_gap": probe_match_summary.get("semantic_gap", 0),
            "direct_symbol_scan_symbols": symbol_scan_summary.get("symbols_scanned", 0),
            "direct_symbol_scan_semantic_started": symbol_scan_summary.get("best_semantic_started", 0),
            "direct_symbol_scan_semantic_gap": symbol_scan_summary.get("best_semantic_gap", 0),
            "direct_split_promotion_workorders": split_promotion_summary.get("workorders", 0),
            "direct_split_promotion_complete_spans": split_promotion_summary.get("complete_spans", 0),
            "direct_split_promotion_truncated_spans": split_promotion_summary.get("truncated_spans", 0),
            "direct_split_promotion_best_candidate_complete_spans": split_promotion_summary.get(
                "best_candidate_complete_spans",
                0,
            ),
            "direct_split_promotion_ready_spans": split_promotion_summary.get("promotion_ready_spans", 0),
            "direct_split_promotion_blocked_spans": split_promotion_summary.get("promotion_blocked_spans", 0),
            "direct_split_gap_report_rows": split_gap_summary.get("rows", 0),
            "direct_split_gap_blocked_rows": split_gap_summary.get("blocked_rows", 0),
            "direct_split_gap_complete_blocked_rows": split_gap_summary.get("complete_blocked_rows", 0),
            "direct_split_gap_truncated_blocked_rows": split_gap_summary.get("truncated_blocked_rows", 0),
            "direct_split_gap_best_candidate_blocked_rows": split_gap_summary.get("best_candidate_blocked_rows", 0),
            "direct_split_gap_classes": split_gap_summary.get("gap_classes", {}),
            "direct_split_gap_top_blocker": split_gap_summary.get("top_blocker", ""),
            "direct_split_gap_top_entry": split_gap_summary.get("top_entry", ""),
            "direct_split_gap_top_candidate_symbol": split_gap_summary.get("top_candidate_symbol", ""),
            "direct_target_split_suggestions": target_split_summary.get("suggestions", 0),
            "direct_target_split_suggestion_entries": target_split_summary.get("entries", 0),
            "direct_target_split_inferred_addresses": target_split_summary.get("inferred_addresses", 0),
            "direct_target_split_symbol_candidates": target_split_summary.get("symbol_candidates", 0),
            "direct_target_split_existing_manual_symbols": target_split_summary.get("existing_manual_symbols", 0),
            "direct_target_split_kinds": target_split_summary.get("kinds", {}),
            "direct_target_envelope_plan_entries": target_envelope_summary.get("entries", 0),
            "direct_target_envelope_plan_anchors": target_envelope_summary.get("anchors", 0),
            "direct_target_envelope_plan_body_anchors": target_envelope_summary.get("body_anchors", 0),
            "direct_target_envelope_plan_clusters": target_envelope_summary.get("clusters", 0),
            "direct_target_envelope_plan_multi_anchor_envelopes": target_envelope_summary.get(
                "multi_anchor_envelopes",
                0,
            ),
            "direct_target_envelope_plan_top_entry": target_envelope_summary.get("top_entry", ""),
            "direct_target_envelope_plan_top_classification": target_envelope_summary.get("top_classification", ""),
            "direct_target_window_probe_windows": target_window_summary.get("windows", 0),
            "direct_target_window_probe_compared_windows": target_window_summary.get("compared_windows", 0),
            "direct_target_window_probe_near_or_exact_windows": target_window_summary.get("near_or_exact_windows", 0),
            "direct_target_window_probe_exact_windows": target_window_summary.get("exact_windows", 0),
            "direct_target_window_probe_codegen_near_windows": target_window_summary.get("codegen_near_windows", 0),
            "direct_target_window_probe_structural_near_windows": target_window_summary.get(
                "structural_near_windows",
                0,
            ),
            "direct_target_window_probe_semantic_windows": target_window_summary.get("semantic_windows", 0),
            "direct_target_window_probe_weak_windows": target_window_summary.get("weak_windows", 0),
            "direct_target_window_probe_best_entry": target_window_summary.get("best_window_entry", ""),
            "direct_target_window_probe_best_candidate": target_window_summary.get("best_window_candidate", ""),
            "direct_target_window_probe_best_category": target_window_summary.get("best_window_category", ""),
            "direct_target_window_probe_best_lcs_window_ratio": target_window_summary.get(
                "best_window_lcs_window_ratio",
                0,
            ),
            "direct_target_window_refinement_windows": target_window_refinement_summary.get("windows", 0),
            "direct_target_window_refinement_compared_windows": target_window_refinement_summary.get(
                "compared_windows",
                0,
            ),
            "direct_target_window_refinement_improved_windows": target_window_refinement_summary.get(
                "improved_windows",
                0,
            ),
            "direct_target_window_refinement_near_or_exact_windows": target_window_refinement_summary.get(
                "near_or_exact_windows",
                0,
            ),
            "direct_target_window_refinement_semantic_windows": target_window_refinement_summary.get(
                "semantic_windows",
                0,
            ),
            "direct_target_window_refinement_weak_windows": target_window_refinement_summary.get("weak_windows", 0),
            "direct_target_window_refinement_best_entry": target_window_refinement_summary.get("best_entry", ""),
            "direct_target_window_refinement_best_candidate": target_window_refinement_summary.get(
                "best_candidate",
                "",
            ),
            "direct_target_window_refinement_best_category": target_window_refinement_summary.get("best_category", ""),
            "direct_target_window_refinement_best_lcs_window_ratio": target_window_refinement_summary.get(
                "best_lcs_window_ratio",
                0,
            ),
            "direct_target_window_refinement_best_lcs_delta": target_window_refinement_summary.get(
                "best_lcs_delta",
                0,
            ),
            "direct_window_adapter_queue_workorders": window_adapter_summary.get("workorders", 0),
            "direct_window_adapter_queue_entries": window_adapter_summary.get("entries", 0),
            "direct_window_adapter_queue_promotion_ready_workorders": window_adapter_summary.get(
                "promotion_ready_workorders",
                0,
            ),
            "direct_window_adapter_queue_blocked_workorders": window_adapter_summary.get("blocked_workorders", 0),
            "direct_window_adapter_queue_improved_workorders": window_adapter_summary.get("improved_workorders", 0),
            "direct_window_adapter_queue_blocker_classes": window_adapter_summary.get("blocker_classes", {}),
            "direct_window_adapter_queue_top_entry": window_adapter_summary.get("top_entry", ""),
            "direct_window_adapter_queue_top_candidate_symbol": window_adapter_summary.get(
                "top_candidate_symbol",
                "",
            ),
            "direct_window_adapter_queue_top_blocker_class": window_adapter_summary.get("top_blocker_class", ""),
            "direct_window_adapter_queue_top_refined_lcs_window_ratio": window_adapter_summary.get(
                "top_refined_lcs_window_ratio",
                0,
            ),
            "direct_window_adapter_probe_workorders_considered": window_adapter_probe_summary.get(
                "workorders_considered",
                0,
            ),
            "direct_window_adapter_probe_packets": window_adapter_probe_summary.get("packets", 0),
            "direct_window_adapter_probe_materialized_packets": window_adapter_probe_summary.get(
                "materialized_packets",
                0,
            ),
            "direct_window_adapter_probe_blocked_packets": window_adapter_probe_summary.get("blocked_packets", 0),
            "direct_window_adapter_probe_entries": window_adapter_probe_summary.get("entries", 0),
            "direct_window_adapter_probe_top_entry": window_adapter_probe_summary.get("top_entry", ""),
            "direct_window_adapter_probe_top_candidate_symbol": window_adapter_probe_summary.get(
                "top_candidate_symbol",
                "",
            ),
            "direct_window_adapter_probe_top_lcs_window_ratio": window_adapter_probe_summary.get(
                "top_lcs_window_ratio",
                0,
            ),
            "direct_window_adapter_probe_diagnostic_packets": window_adapter_probe_diagnostics_summary.get(
                "packets",
                0,
            ),
            "direct_window_adapter_probe_diagnostic_classes": window_adapter_probe_diagnostics_summary.get(
                "diagnostic_classes",
                {},
            ),
            "direct_window_adapter_probe_diagnostic_data_like_start_packets": (
                window_adapter_probe_diagnostics_summary.get("data_like_start_packets", 0)
            ),
            "direct_window_adapter_probe_diagnostic_call_shape_packets": (
                window_adapter_probe_diagnostics_summary.get("call_shape_packets", 0)
            ),
            "direct_window_adapter_probe_diagnostic_low_body_signal_packets": (
                window_adapter_probe_diagnostics_summary.get("low_body_signal_packets", 0)
            ),
            "direct_window_adapter_probe_diagnostic_top_entry": window_adapter_probe_diagnostics_summary.get(
                "top_entry",
                "",
            ),
            "direct_window_adapter_probe_diagnostic_top_candidate_symbol": (
                window_adapter_probe_diagnostics_summary.get("top_candidate_symbol", "")
            ),
            "direct_window_adapter_probe_diagnostic_top_class": window_adapter_probe_diagnostics_summary.get(
                "top_diagnostic_class",
                "",
            ),
            "direct_data_like_boundary_refinement_packets": data_like_boundary_refinement_summary.get("packets", 0),
            "direct_data_like_boundary_refinement_candidate_rows": data_like_boundary_refinement_summary.get(
                "candidate_rows",
                0,
            ),
            "direct_data_like_boundary_refinement_corrected_candidates": data_like_boundary_refinement_summary.get(
                "corrected_candidates",
                0,
            ),
            "direct_data_like_boundary_refinement_improved_packets": data_like_boundary_refinement_summary.get(
                "improved_packets",
                0,
            ),
            "direct_data_like_boundary_refinement_near_or_exact_packets": data_like_boundary_refinement_summary.get(
                "near_or_exact_packets",
                0,
            ),
            "direct_data_like_boundary_refinement_best_entry": data_like_boundary_refinement_summary.get(
                "best_entry",
                "",
            ),
            "direct_data_like_boundary_refinement_best_candidate_symbol": (
                data_like_boundary_refinement_summary.get("best_candidate_symbol", "")
            ),
            "direct_data_like_boundary_refinement_best_category": data_like_boundary_refinement_summary.get(
                "best_category",
                "",
            ),
            "direct_data_like_boundary_refinement_best_start_addr": data_like_boundary_refinement_summary.get(
                "best_start_addr",
                "",
            ),
            "direct_data_like_boundary_refinement_best_lcs_window_ratio": (
                data_like_boundary_refinement_summary.get("best_lcs_window_ratio", 0)
            ),
            "direct_data_like_boundary_refinement_best_lcs_delta": data_like_boundary_refinement_summary.get(
                "best_lcs_delta",
                0,
            ),
            "direct_semantic_call_shape_packets": semantic_call_shape_summary.get("packets", 0),
            "direct_semantic_call_shape_blocked_packets": semantic_call_shape_summary.get("blocked_packets", 0),
            "direct_semantic_call_shape_promotion_ready_packets": semantic_call_shape_summary.get(
                "promotion_ready_packets",
                0,
            ),
            "direct_semantic_call_shape_blocker_classes": semantic_call_shape_summary.get("blocker_classes", {}),
            "direct_semantic_call_shape_compiled_subregion_alignment_packets": semantic_call_shape_summary.get(
                "compiled_subregion_alignment_packets",
                0,
            ),
            "direct_semantic_call_shape_call_count_delta_packets": semantic_call_shape_summary.get(
                "call_count_delta_packets",
                0,
            ),
            "direct_semantic_call_shape_memory_shape_delta_packets": semantic_call_shape_summary.get(
                "memory_shape_delta_packets",
                0,
            ),
            "direct_semantic_call_shape_best_entry": semantic_call_shape_summary.get("best_entry", ""),
            "direct_semantic_call_shape_best_candidate_symbol": semantic_call_shape_summary.get(
                "best_candidate_symbol",
                "",
            ),
            "direct_semantic_call_shape_best_blocker_class": semantic_call_shape_summary.get(
                "best_blocker_class",
                "",
            ),
            "direct_semantic_call_shape_best_lcs_window_ratio": semantic_call_shape_summary.get(
                "best_lcs_window_ratio",
                0,
            ),
            "direct_source_subregion_plan_packets": source_subregion_plan_summary.get("packets", 0),
            "direct_source_subregion_plan_candidates": source_subregion_plan_summary.get(
                "source_subregion_candidates",
                0,
            ),
            "direct_source_subregion_plan_anchored_packets": source_subregion_plan_summary.get(
                "anchored_packets",
                0,
            ),
            "direct_source_subregion_plan_best_entry": source_subregion_plan_summary.get("best_entry", ""),
            "direct_source_subregion_plan_best_candidate_symbol": source_subregion_plan_summary.get(
                "best_candidate_symbol",
                "",
            ),
            "direct_source_subregion_plan_best_anchor_source_line": source_subregion_plan_summary.get(
                "best_anchor_source_line",
                0,
            ),
            "direct_source_subregion_probe_probes": source_subregion_probe_summary.get("subregion_probes", 0),
            "direct_source_subregion_probe_compiled": source_subregion_probe_summary.get("compiled", 0),
            "direct_source_subregion_probe_dumped": source_subregion_probe_summary.get("dumped", 0),
            "direct_source_subregion_probe_failed": source_subregion_probe_summary.get("failed", 0),
            "direct_source_subregion_probe_near_or_exact": source_subregion_probe_summary.get("near_or_exact", 0),
            "direct_source_subregion_probe_categories": source_subregion_probe_summary.get("categories", {}),
            "direct_source_subregion_probe_best_entry": source_subregion_probe_summary.get("best_entry", ""),
            "direct_source_subregion_probe_best_candidate_symbol": source_subregion_probe_summary.get(
                "best_candidate_symbol",
                "",
            ),
            "direct_source_subregion_probe_best_source_range": source_subregion_probe_summary.get(
                "best_source_range",
                "",
            ),
            "direct_source_subregion_probe_best_category": source_subregion_probe_summary.get("best_category", ""),
            "direct_source_subregion_probe_best_lcs_window_ratio": source_subregion_probe_summary.get(
                "best_lcs_window_ratio",
                0,
            ),
            "direct_source_subregion_sweep_ranges": source_subregion_sweep_summary.get("sweep_ranges", 0),
            "direct_source_subregion_sweep_compiled": source_subregion_sweep_summary.get("compiled", 0),
            "direct_source_subregion_sweep_dumped": source_subregion_sweep_summary.get("dumped", 0),
            "direct_source_subregion_sweep_failed": source_subregion_sweep_summary.get("failed", 0),
            "direct_source_subregion_sweep_near_or_exact": source_subregion_sweep_summary.get("near_or_exact", 0),
            "direct_source_subregion_sweep_categories": source_subregion_sweep_summary.get("categories", {}),
            "direct_source_subregion_sweep_best_entry": source_subregion_sweep_summary.get("best_entry", ""),
            "direct_source_subregion_sweep_best_candidate_symbol": source_subregion_sweep_summary.get(
                "best_candidate_symbol",
                "",
            ),
            "direct_source_subregion_sweep_best_source_range": source_subregion_sweep_summary.get(
                "best_source_range",
                "",
            ),
            "direct_source_subregion_sweep_best_category": source_subregion_sweep_summary.get("best_category", ""),
            "direct_source_subregion_sweep_best_lcs_window_ratio": source_subregion_sweep_summary.get(
                "best_lcs_window_ratio",
                0,
            ),
            "direct_source_subregion_sweep_best_target_start_addr": source_subregion_sweep_summary.get(
                "best_target_start_addr",
                "",
            ),
            "direct_source_subregion_sweep_best_compiled_skip": source_subregion_sweep_summary.get(
                "best_compiled_skip",
                0,
            ),
            "direct_source_subregion_seed_qualification_near_or_exact_rows": (
                source_subregion_seed_qualification_summary.get("near_or_exact_rows", 0)
            ),
            "direct_source_subregion_seed_qualification_qualified_seed_rows": (
                source_subregion_seed_qualification_summary.get("qualified_seed_rows", 0)
            ),
            "direct_source_subregion_seed_qualification_risk_rows": (
                source_subregion_seed_qualification_summary.get("risk_rows", 0)
            ),
            "direct_source_subregion_seed_qualification_classes": (
                source_subregion_seed_qualification_summary.get("classes", {})
            ),
            "direct_source_subregion_seed_qualification_best_seed_source_range": (
                source_subregion_seed_qualification_summary.get("best_seed_source_range", "")
            ),
            "direct_source_subregion_seed_qualification_best_seed_target_start_addr": (
                source_subregion_seed_qualification_summary.get("best_seed_target_start_addr", "")
            ),
            "direct_source_subregion_seed_qualification_best_seed_compiled_skip": (
                source_subregion_seed_qualification_summary.get("best_seed_compiled_skip", 0)
            ),
            "direct_source_subregion_seed_qualification_best_seed_class": (
                source_subregion_seed_qualification_summary.get("best_seed_class", "")
            ),
            "direct_source_subregion_seed_qualification_best_seed_lcs_window_ratio": (
                source_subregion_seed_qualification_summary.get("best_seed_lcs_window_ratio", 0)
            ),
            "direct_control_flow_isolation_workorders": control_flow_isolation_workorders_summary.get(
                "workorders",
                0,
            ),
            "direct_control_flow_isolation_candidate_seed_workorders": (
                control_flow_isolation_workorders_summary.get("candidate_control_flow_seed_workorders", 0)
            ),
            "direct_control_flow_isolation_best_workorder": control_flow_isolation_workorders_summary.get(
                "best_workorder",
                "",
            ),
            "direct_control_flow_isolation_best_workorder_path": control_flow_isolation_workorders_summary.get(
                "best_workorder_path",
                "",
            ),
            "direct_control_flow_isolation_best_source_range": control_flow_isolation_workorders_summary.get(
                "best_source_range",
                "",
            ),
            "direct_control_flow_isolation_best_target_range": control_flow_isolation_workorders_summary.get(
                "best_target_range",
                "",
            ),
            "direct_control_flow_isolation_best_lcs_window_ratio": control_flow_isolation_workorders_summary.get(
                "best_lcs_window_ratio",
                0,
            ),
            "direct_control_flow_subblock_probe_subblocks": control_flow_subblock_probe_summary.get(
                "subblock_probes",
                0,
            ),
            "direct_control_flow_subblock_probe_compiled": control_flow_subblock_probe_summary.get(
                "compiled",
                0,
            ),
            "direct_control_flow_subblock_probe_near_or_exact": control_flow_subblock_probe_summary.get(
                "near_or_exact",
                0,
            ),
            "direct_control_flow_subblock_probe_categories": control_flow_subblock_probe_summary.get(
                "categories",
                {},
            ),
            "direct_control_flow_subblock_probe_best_workorder": control_flow_subblock_probe_summary.get(
                "best_workorder",
                "",
            ),
            "direct_control_flow_subblock_probe_best_source_range": control_flow_subblock_probe_summary.get(
                "best_source_range",
                "",
            ),
            "direct_control_flow_subblock_probe_best_category": control_flow_subblock_probe_summary.get(
                "best_category",
                "",
            ),
            "direct_control_flow_subblock_probe_best_target_start_addr": control_flow_subblock_probe_summary.get(
                "best_target_start_addr",
                "",
            ),
            "direct_control_flow_subblock_probe_best_compiled_skip": control_flow_subblock_probe_summary.get(
                "best_compiled_skip",
                0,
            ),
            "direct_control_flow_subblock_probe_best_lcs_window_ratio": control_flow_subblock_probe_summary.get(
                "best_lcs_window_ratio",
                0,
            ),
            "direct_control_flow_subblock_seed_qualification_near_or_exact_rows": (
                control_flow_subblock_seed_qualification_summary.get("near_or_exact_rows", 0)
            ),
            "direct_control_flow_subblock_seed_qualification_qualified_seed_rows": (
                control_flow_subblock_seed_qualification_summary.get("qualified_seed_rows", 0)
            ),
            "direct_control_flow_subblock_seed_qualification_risk_rows": (
                control_flow_subblock_seed_qualification_summary.get("risk_rows", 0)
            ),
            "direct_control_flow_subblock_seed_qualification_classes": (
                control_flow_subblock_seed_qualification_summary.get("classes", {})
            ),
            "direct_control_flow_subblock_seed_qualification_best_seed_workorder": (
                control_flow_subblock_seed_qualification_summary.get("best_seed_workorder", "")
            ),
            "direct_control_flow_subblock_seed_qualification_best_seed_source_range": (
                control_flow_subblock_seed_qualification_summary.get("best_seed_source_range", "")
            ),
            "direct_control_flow_subblock_seed_qualification_best_seed_target_start_addr": (
                control_flow_subblock_seed_qualification_summary.get("best_seed_target_start_addr", "")
            ),
            "direct_control_flow_subblock_seed_qualification_best_seed_compiled_skip": (
                control_flow_subblock_seed_qualification_summary.get("best_seed_compiled_skip", 0)
            ),
            "direct_control_flow_subblock_seed_qualification_best_seed_class": (
                control_flow_subblock_seed_qualification_summary.get("best_seed_class", "")
            ),
            "direct_control_flow_subblock_seed_qualification_best_seed_lcs_window_ratio": (
                control_flow_subblock_seed_qualification_summary.get("best_seed_lcs_window_ratio", 0)
            ),
            "direct_tail_call_seed_targets_seed_source_range": tail_call_seed_targets_summary.get(
                "seed_source_range",
                "",
            ),
            "direct_tail_call_seed_targets_seed_target_range": tail_call_seed_targets_summary.get(
                "seed_target_range",
                "",
            ),
            "direct_tail_call_seed_targets_branch_or_call_rows": tail_call_seed_targets_summary.get(
                "target_branch_or_call_rows",
                0,
            ),
            "direct_tail_call_seed_targets_external_call_targets": tail_call_seed_targets_summary.get(
                "target_external_call_targets",
                0,
            ),
            "direct_tail_call_seed_targets_unknown_external_call_targets": tail_call_seed_targets_summary.get(
                "target_unknown_external_call_targets",
                0,
            ),
            "direct_tail_call_seed_targets_unknown_external_call_addrs": tail_call_seed_targets_summary.get(
                "target_unknown_external_call_addrs",
                [],
            ),
            "direct_tail_call_seed_targets_local_branch_targets": tail_call_seed_targets_summary.get(
                "target_local_branch_targets",
                0,
            ),
            "direct_tail_call_seed_targets_compiled_relocation_symbols": tail_call_seed_targets_summary.get(
                "compiled_relocation_symbols",
                [],
            ),
            "direct_tail_call_seed_targets_normalized_match_rows": tail_call_seed_targets_summary.get(
                "normalized_match_rows",
                0,
            ),
            "direct_tail_call_target_identity_targets": tail_call_target_identity_summary.get("targets", 0),
            "direct_tail_call_target_identity_known_targets": tail_call_target_identity_summary.get(
                "known_targets",
                0,
            ),
            "direct_tail_call_target_identity_unresolved_targets": tail_call_target_identity_summary.get(
                "unresolved_targets",
                0,
            ),
            "direct_tail_call_target_identity_conflicting_targets": tail_call_target_identity_summary.get(
                "conflicting_targets",
                0,
            ),
            "direct_tail_call_target_identity_classes": tail_call_target_identity_summary.get(
                "identity_classes",
                {},
            ),
            "direct_tail_call_target_body_shapes_targets": tail_call_target_body_shapes_summary.get(
                "targets",
                0,
            ),
            "direct_tail_call_target_body_shapes_unsafe_positional_identities": (
                tail_call_target_body_shapes_summary.get("unsafe_positional_identities", 0)
            ),
            "direct_tail_call_target_body_shapes_partial_wrapper_candidates": (
                tail_call_target_body_shapes_summary.get("partial_wrapper_candidates", 0)
            ),
            "direct_tail_call_target_body_shapes_state_mode_helpers": tail_call_target_body_shapes_summary.get(
                "state_mode_helpers",
                0,
            ),
            "direct_tail_call_target_body_shapes_tail_to_state_mode_helpers": (
                tail_call_target_body_shapes_summary.get("tail_to_state_mode_helpers", 0)
            ),
            "direct_state_mode_table_addr": state_mode_table_summary.get("table_addr", ""),
            "direct_state_mode_table_known_state_values": state_mode_table_summary.get(
                "known_state_values",
                0,
            ),
            "direct_state_mode_table_player_state_values": state_mode_table_summary.get(
                "player_state_values",
                0,
            ),
            "direct_state_mode_table_nonzero_rows": state_mode_table_summary.get(
                "nonzero_table_rows",
                0,
            ),
            "direct_state_mode_table_player_nonzero_rows": state_mode_table_summary.get(
                "player_nonzero_table_rows",
                0,
            ),
            "direct_state_mode_table_player_unique_flag_patterns": state_mode_table_summary.get(
                "player_unique_flag_patterns",
                [],
            ),
            "direct_state_mode_switch_map_states": state_mode_switch_map_summary.get("switch_states", 0),
            "direct_state_mode_switch_map_unique_handlers": state_mode_switch_map_summary.get(
                "unique_handlers",
                0,
            ),
            "direct_state_mode_switch_map_shared_handlers": state_mode_switch_map_summary.get(
                "shared_handlers",
                0,
            ),
            "direct_state_mode_switch_map_player_used_states": state_mode_switch_map_summary.get(
                "player_used_switch_states",
                0,
            ),
            "direct_state_mode_switch_map_transition_edges": state_mode_switch_map_summary.get(
                "transition_edges",
                0,
            ),
            "direct_state_mode_switch_map_unique_transition_edges": state_mode_switch_map_summary.get(
                "unique_transition_edges",
                0,
            ),
            "direct_state_mode_handler_context_handlers": state_mode_handler_context_summary.get("handlers", 0),
            "direct_state_mode_handler_context_classified_handlers": state_mode_handler_context_summary.get(
                "classified_handlers",
                0,
            ),
            "direct_state_mode_handler_context_unclassified_handlers": state_mode_handler_context_summary.get(
                "unclassified_handlers",
                0,
            ),
            "direct_state_mode_handler_context_handlers_with_outgoing_transitions": (
                state_mode_handler_context_summary.get("handlers_with_outgoing_transitions", 0)
            ),
            "direct_state_mode_handler_context_feature_counts": state_mode_handler_context_summary.get(
                "feature_counts",
                {},
            ),
            "direct_state_mode_source_correlation_handlers": state_mode_source_correlation_summary.get(
                "handlers",
                0,
            ),
            "direct_state_mode_source_correlation_candidate_source_windows": (
                state_mode_source_correlation_summary.get("candidate_source_windows", 0)
            ),
            "direct_state_mode_source_correlation_weak_source_windows": (
                state_mode_source_correlation_summary.get("weak_source_windows", 0)
            ),
            "direct_state_mode_source_correlation_unmatched_handlers": (
                state_mode_source_correlation_summary.get("unmatched_handlers", 0)
            ),
            "direct_state_mode_source_correlation_candidate_functions": (
                state_mode_source_correlation_summary.get("candidate_functions", 0)
            ),
            "direct_state_mode_source_correlation_promotion_ready_handlers": (
                state_mode_source_correlation_summary.get("promotion_ready_handlers", 0)
            ),
            "direct_state_mode_source_correlation_top_candidate_functions": (
                state_mode_source_correlation_summary.get("top_candidate_functions", {})
            ),
            "direct_state_mode_source_workorders": state_mode_source_workorders_summary.get("workorders", 0),
            "direct_state_mode_source_workorders_candidate_rows": state_mode_source_workorders_summary.get(
                "candidate_rows",
                0,
            ),
            "direct_state_mode_source_workorders_promotion_ready": (
                state_mode_source_workorders_summary.get("promotion_ready_workorders", 0)
            ),
            "direct_state_mode_source_workorders_source_functions": state_mode_source_workorders_summary.get(
                "source_functions",
                [],
            ),
            "direct_state_mode_source_workorder_probe_workorders": state_mode_source_workorder_probe_summary.get(
                "workorders",
                0,
            ),
            "direct_state_mode_source_workorder_probe_compiled": state_mode_source_workorder_probe_summary.get(
                "compiled",
                0,
            ),
            "direct_state_mode_source_workorder_probe_near_or_exact": (
                state_mode_source_workorder_probe_summary.get("near_or_exact", 0)
            ),
            "direct_state_mode_source_workorder_probe_split_seed_candidates": (
                state_mode_source_workorder_probe_summary.get("split_seed_candidates", 0)
            ),
            "direct_state_mode_source_workorder_probe_categories": state_mode_source_workorder_probe_summary.get(
                "categories",
                {},
            ),
            "direct_state_mode_source_workorder_probe_best_workorder": (
                state_mode_source_workorder_probe_summary.get("best_workorder", "")
            ),
            "direct_state_mode_source_workorder_probe_best_lcs_window_ratio": (
                state_mode_source_workorder_probe_summary.get("best_lcs_window_ratio", 0.0)
            ),
            "direct_state_mode_source_workorder_seed_qualification_rows": (
                state_mode_source_workorder_seed_qualification_summary.get("probe_rows", 0)
            ),
            "direct_state_mode_source_workorder_seed_qualification_near_or_exact": (
                state_mode_source_workorder_seed_qualification_summary.get("near_or_exact_rows", 0)
            ),
            "direct_state_mode_source_workorder_seed_qualification_qualified": (
                state_mode_source_workorder_seed_qualification_summary.get("qualified_seed_rows", 0)
            ),
            "direct_state_mode_source_workorder_seed_qualification_risk": (
                state_mode_source_workorder_seed_qualification_summary.get("risk_rows", 0)
            ),
            "direct_state_mode_source_workorder_seed_qualification_classes": (
                state_mode_source_workorder_seed_qualification_summary.get("classes", {})
            ),
            "direct_state_mode_source_workorder_seed_qualification_best_seed": (
                state_mode_source_workorder_seed_qualification_summary.get("best_seed_workorder", "")
            ),
            "direct_state_mode_source_call_shape_workorders": state_mode_source_call_shape_summary.get(
                "workorders",
                0,
            ),
            "direct_state_mode_source_call_shape_semantic_supported": (
                state_mode_source_call_shape_summary.get("semantic_supported", 0)
            ),
            "direct_state_mode_source_call_shape_semantic_risk_confirmed": (
                state_mode_source_call_shape_summary.get("semantic_risk_confirmed", 0)
            ),
            "direct_state_mode_source_call_shape_semantic_orientation": (
                state_mode_source_call_shape_summary.get("semantic_orientation", 0)
            ),
            "direct_state_mode_source_call_shape_semantic_weak": (
                state_mode_source_call_shape_summary.get("semantic_weak", 0)
            ),
            "direct_state_mode_source_call_shape_promotion_ready": (
                state_mode_source_call_shape_summary.get("promotion_ready", 0)
            ),
            "direct_state_mode_source_call_shape_classes": state_mode_source_call_shape_summary.get(
                "support_classes",
                {},
            ),
            "direct_frontier_blocker_synthesis_rows": frontier_blocker_synthesis_summary.get(
                "frontier_rows",
                0,
            ),
            "direct_frontier_blocker_synthesis_state_mode_exhausted_entry": (
                frontier_blocker_synthesis_summary.get("state_mode_exhausted_entry", "")
            ),
            "direct_frontier_blocker_synthesis_state_mode_qualified_seed_rows": (
                frontier_blocker_synthesis_summary.get("state_mode_qualified_seed_rows", 0)
            ),
            "direct_frontier_blocker_synthesis_next_recommended_entry": (
                frontier_blocker_synthesis_summary.get("next_recommended_entry", "")
            ),
            "direct_frontier_blocker_synthesis_next_recommended_name": (
                frontier_blocker_synthesis_summary.get("next_recommended_name", "")
            ),
            "direct_frontier_blocker_synthesis_next_recommended_blocker": (
                frontier_blocker_synthesis_summary.get("next_recommended_blocker", "")
            ),
            "direct_frontier_blocker_synthesis_blocker_counts": frontier_blocker_synthesis_summary.get(
                "blocker_counts",
                {},
            ),
            "direct_structured_lowering_blockers_rows": structured_lowering_blockers_summary.get(
                "structured_rows",
                0,
            ),
            "direct_structured_lowering_blockers_promotion_ready": structured_lowering_blockers_summary.get(
                "promotion_ready",
                0,
            ),
            "direct_structured_lowering_blockers_next_entry": structured_lowering_blockers_summary.get(
                "next_entry",
                "",
            ),
            "direct_structured_lowering_blockers_next_blocker": structured_lowering_blockers_summary.get(
                "next_blocker",
                "",
            ),
            "direct_structured_lowering_blockers_blocker_counts": structured_lowering_blockers_summary.get(
                "blocker_counts",
                {},
            ),
            "direct_structured_lowering_probe_variants": structured_lowering_probe_summary.get("variants", 0),
            "direct_structured_lowering_probe_compiled": structured_lowering_probe_summary.get("compiled", 0),
            "direct_structured_lowering_probe_best_lcs_target_ratio": structured_lowering_probe_summary.get(
                "best_lcs_target_ratio",
                0,
            ),
            "direct_structured_lowering_probe_best_category": structured_lowering_probe_summary.get(
                "best_category",
                "",
            ),
            "direct_structured_lowering_probe_best_variant": structured_lowering_probe_summary.get(
                "best_variant",
                "",
            ),
            "direct_structured_lowering_probe_best_profile": structured_lowering_probe_summary.get(
                "best_profile",
                "",
            ),
            "direct_structured_lowering_probe_promotion_ready": structured_lowering_probe_summary.get(
                "promotion_ready",
                0,
            ),
            "direct_structured_lowering_seed_qualification_rows": (
                structured_lowering_seed_qualification_summary.get("rows", 0)
            ),
            "direct_structured_lowering_seed_qualification_qualified_seeds": (
                structured_lowering_seed_qualification_summary.get("qualified_seeds", 0)
            ),
            "direct_structured_lowering_seed_qualification_exact_adapter_controls": (
                structured_lowering_seed_qualification_summary.get("exact_adapter_controls", 0)
            ),
            "direct_structured_lowering_seed_qualification_promotion_ready": (
                structured_lowering_seed_qualification_summary.get("promotion_ready", 0)
            ),
            "direct_structured_lowering_seed_qualification_best_blockers": (
                structured_lowering_seed_qualification_summary.get("best_blockers", "")
            ),
            "direct_structured_lowering_isolation_segments": structured_lowering_isolation_summary.get(
                "segments",
                0,
            ),
            "direct_structured_lowering_isolation_blocked_segments": structured_lowering_isolation_summary.get(
                "blocked_segments",
                0,
            ),
            "direct_structured_lowering_isolation_top_segment": structured_lowering_isolation_summary.get(
                "top_segment",
                "",
            ),
            "direct_structured_lowering_isolation_top_blocker": structured_lowering_isolation_summary.get(
                "top_blocker",
                "",
            ),
            "direct_structured_lowering_isolation_top_lcs_target_ratio": structured_lowering_isolation_summary.get(
                "top_lcs_target_ratio",
                0,
            ),
            "direct_structured_lowering_isolation_next_gate": structured_lowering_isolation_summary.get(
                "next_gate",
                "",
            ),
            "direct_structured_state_gate_variant_rows": structured_state_gate_variants_summary.get("rows", 0),
            "direct_structured_state_gate_range_check_avoided_rows": structured_state_gate_variants_summary.get(
                "range_check_avoided_rows",
                0,
            ),
            "direct_structured_state_gate_cmpne_chain_recovered_rows": structured_state_gate_variants_summary.get(
                "cmpne_chain_recovered_rows",
                0,
            ),
            "direct_structured_state_gate_best_variant": structured_state_gate_variants_summary.get(
                "best_variant",
                "",
            ),
            "direct_structured_state_gate_best_profile": structured_state_gate_variants_summary.get(
                "best_profile",
                "",
            ),
            "direct_structured_state_gate_best_compiled_branch_shape": structured_state_gate_variants_summary.get(
                "best_compiled_branch_shape",
                "",
            ),
            "direct_structured_state_gate_best_lcs_target_ratio": structured_state_gate_variants_summary.get(
                "best_state_gate_lcs_target_ratio",
                0,
            ),
            "direct_structured_state_gate_next_gate": structured_state_gate_variants_summary.get("next_gate", ""),
            "direct_structured_smooth_variant_rows": structured_smooth_variants_summary.get("rows", 0),
            "direct_structured_smooth_rot_unsigned_load_rows": structured_smooth_variants_summary.get(
                "rot_unsigned_load_rows",
                0,
            ),
            "direct_structured_smooth_rot_split_high_base_rows": structured_smooth_variants_summary.get(
                "rot_split_high_base_rows",
                0,
            ),
            "direct_structured_smooth_rot_compact_high_base_rows": structured_smooth_variants_summary.get(
                "rot_compact_high_base_rows",
                0,
            ),
            "direct_structured_smooth_rot_target_offset_order_rows": structured_smooth_variants_summary.get(
                "rot_target_offset_order_rows",
                0,
            ),
            "direct_structured_smooth_rot_target_load_register_order_rows": structured_smooth_variants_summary.get(
                "rot_target_load_register_order_rows",
                0,
            ),
            "direct_structured_smooth_zero_stack_arg_uses_r3_rows": structured_smooth_variants_summary.get(
                "zero_stack_arg_uses_r3_rows",
                0,
            ),
            "direct_structured_smooth_joint_split_high_base_rows": structured_smooth_variants_summary.get(
                "joint_split_high_base_rows",
                0,
            ),
            "direct_structured_smooth_flag_before_action_rows": structured_smooth_variants_summary.get(
                "flag_before_action_rows",
                0,
            ),
            "direct_structured_smooth_best_variant": structured_smooth_variants_summary.get("best_variant", ""),
            "direct_structured_smooth_best_profile": structured_smooth_variants_summary.get("best_profile", ""),
            "direct_structured_smooth_best_feature_count": structured_smooth_variants_summary.get(
                "best_feature_count",
                0,
            ),
            "direct_structured_smooth_best_global_lcs_target_ratio": structured_smooth_variants_summary.get(
                "best_global_lcs_target_ratio",
                0,
            ),
            "direct_structured_smooth_next_gate": structured_smooth_variants_summary.get("next_gate", ""),
            "direct_smooth_tail_codegen_probe_rows": smooth_tail_codegen_probe_summary.get("rows", 0),
            "direct_smooth_tail_codegen_probe_compiled": smooth_tail_codegen_probe_summary.get("compiled", 0),
            "direct_smooth_tail_codegen_probe_pure_c_rows": smooth_tail_codegen_probe_summary.get(
                "pure_c_rows",
                0,
            ),
            "direct_smooth_tail_codegen_probe_best_variant": smooth_tail_codegen_probe_summary.get(
                "best_variant",
                "",
            ),
            "direct_smooth_tail_codegen_probe_best_profile": smooth_tail_codegen_probe_summary.get(
                "best_profile",
                "",
            ),
            "direct_smooth_tail_codegen_probe_best_feature_count": smooth_tail_codegen_probe_summary.get(
                "best_feature_count",
                0,
            ),
            "direct_smooth_tail_codegen_probe_best_lcs_target_ratio": smooth_tail_codegen_probe_summary.get(
                "best_lcs_target_ratio",
                0,
            ),
            "direct_smooth_tail_codegen_probe_best_pure_c_variant": smooth_tail_codegen_probe_summary.get(
                "best_pure_c_variant",
                "",
            ),
            "direct_smooth_tail_codegen_probe_best_pure_c_profile": smooth_tail_codegen_probe_summary.get(
                "best_pure_c_profile",
                "",
            ),
            "direct_smooth_tail_codegen_probe_best_pure_c_feature_count": smooth_tail_codegen_probe_summary.get(
                "best_pure_c_feature_count",
                0,
            ),
            "direct_smooth_tail_codegen_probe_best_overall_variant": smooth_tail_codegen_probe_summary.get(
                "best_overall_variant",
                "",
            ),
            "direct_smooth_tail_codegen_probe_best_overall_profile": smooth_tail_codegen_probe_summary.get(
                "best_overall_profile",
                "",
            ),
            "direct_smooth_tail_codegen_probe_best_overall_feature_count": smooth_tail_codegen_probe_summary.get(
                "best_overall_feature_count",
                0,
            ),
            "direct_smooth_tail_codegen_probe_best_overall_lcs_target_ratio": smooth_tail_codegen_probe_summary.get(
                "best_overall_lcs_target_ratio",
                0,
            ),
            "direct_smooth_tail_codegen_probe_exact_tail_adapter_rows": smooth_tail_codegen_probe_summary.get(
                "exact_tail_adapter_rows",
                0,
            ),
            "direct_smooth_tail_codegen_probe_no_saved_r6_rows": smooth_tail_codegen_probe_summary.get(
                "no_saved_r6_rows",
                0,
            ),
            "direct_smooth_tail_codegen_probe_stack_12_rows": smooth_tail_codegen_probe_summary.get(
                "stack_12_rows",
                0,
            ),
            "direct_smooth_tail_codegen_probe_zero_stack_arg_uses_r3_rows": (
                smooth_tail_codegen_probe_summary.get("zero_stack_arg_uses_r3_rows", 0)
            ),
            "direct_smooth_tail_codegen_probe_rot_target_load_register_order_rows": (
                smooth_tail_codegen_probe_summary.get("rot_target_load_register_order_rows", 0)
            ),
            "direct_smooth_tail_codegen_probe_pure_c_rot_target_load_register_order_rows": (
                smooth_tail_codegen_probe_summary.get("pure_c_rot_target_load_register_order_rows", 0)
            ),
            "direct_smooth_tail_codegen_probe_no_saved_r6_and_rot_target_load_register_order_rows": (
                smooth_tail_codegen_probe_summary.get(
                    "no_saved_r6_and_rot_target_load_register_order_rows",
                    0,
                )
            ),
            "direct_smooth_tail_codegen_probe_next_gate": smooth_tail_codegen_probe_summary.get(
                "next_gate",
                "",
            ),
            "direct_state_gate_codegen_probe_rows": state_gate_codegen_probe_summary.get("rows", 0),
            "direct_state_gate_codegen_probe_compiled": state_gate_codegen_probe_summary.get("compiled", 0),
            "direct_state_gate_codegen_probe_pure_c_rows": state_gate_codegen_probe_summary.get("pure_c_rows", 0),
            "direct_state_gate_codegen_probe_pure_c_cmpne_chain_recovered_rows": (
                state_gate_codegen_probe_summary.get("pure_c_cmpne_chain_recovered_rows", 0)
            ),
            "direct_state_gate_codegen_probe_pure_c_range_check_avoided_rows": (
                state_gate_codegen_probe_summary.get("pure_c_range_check_avoided_rows", 0)
            ),
            "direct_state_gate_codegen_probe_inline_asm_cmpne_chain_recovered_rows": (
                state_gate_codegen_probe_summary.get("inline_asm_cmpne_chain_recovered_rows", 0)
            ),
            "direct_state_gate_codegen_probe_best_pure_c_variant": state_gate_codegen_probe_summary.get(
                "best_pure_c_variant",
                "",
            ),
            "direct_state_gate_codegen_probe_best_pure_c_profile": state_gate_codegen_probe_summary.get(
                "best_pure_c_profile",
                "",
            ),
            "direct_state_gate_codegen_probe_best_pure_c_branch_shape": state_gate_codegen_probe_summary.get(
                "best_pure_c_branch_shape",
                "",
            ),
            "direct_state_gate_codegen_probe_next_gate": state_gate_codegen_probe_summary.get("next_gate", ""),
            "frontier_open_function_samples": frontier_samples,
            "frontier_open_sample_materialized_artifact_count": sum(
                1 for sample in frontier_samples
                if bool(sample.get("materialized_output_exists"))
            ),
            "frontier_open_sample_missing_artifact_count": sum(
                1 for sample in frontier_samples
                if not bool(sample.get("all_referenced_artifacts_exist"))
            ),
        },
        runtime_scope="exefs_code_bin_behavior",
        next_gate="Close the C reconstruction frontier for behavior deltas needed by routed asset replacement.",
    )


def romfs_inventory_record(ctx: LedgerContext) -> dict[str, object] | None:
    rel = "inventory/oot3d_romfs_inventory_summary.json"
    data = ctx.read(rel)
    if data is None:
        return missing_record("romfs_inventory", ctx.path(rel))
    model_counts = require_dict(data.get("model_counts"))
    return base_record(
        "romfs_inventory",
        "inventory_ready",
        ctx.path(rel),
        {
            "file_count": data.get("file_count", 0),
            "total_bytes": data.get("total_bytes", 0),
            "parsed_cmb_models": model_counts.get("parsed", 0),
            "static_candidate": model_counts.get("static_candidate", 0),
            "rigid_multibone_candidate": model_counts.get("rigid_multibone_candidate", 0),
            "skinned_or_animated_candidate": (
                int(model_counts.get("nonstatic_candidate", 0) or 0)
                - int(model_counts.get("rigid_multibone_candidate", 0) or 0)
            ),
            "parse_error_count": data.get("parse_error_count", 0),
        },
        runtime_scope="global_romfs",
        next_gate="Use per-family package/runtime gates before replacing N64 assets.",
    )


def static_actor_record(ctx: LedgerContext) -> dict[str, object] | None:
    rel = "static_actor_export/static_actor_package_summary.json"
    data = ctx.read(rel)
    if data is None:
        return missing_record("static_actor_exports", ctx.path(rel))
    package_counts = require_dict(data.get("package_audit_counts"))
    issue_count = int(package_counts.get("total", 0) or 0)
    return base_record(
        "static_actor_exports",
        "offline_package_ready" if issue_count == 0 else "offline_package_invalid",
        ctx.path(rel),
        {
            "converted": data.get("converted", 0),
            "skipped": data.get("skipped", 0),
            "failed": data.get("failed", 0),
            "archive_byte_length": data.get("archive_byte_length", 0),
            "package_issue_count": issue_count,
        },
        runtime_scope="offline_package_only",
        next_gate="Bind ready actor families to runtime draw paths with N64 fallback.",
    )


def rigid_multibone_record(ctx: LedgerContext) -> dict[str, object] | None:
    rel = "rigid_multibone_batch/rigid_multibone_batch_summary.json"
    data = ctx.read(rel)
    if data is None:
        return missing_record("rigid_multibone_exports", ctx.path(rel))
    return base_record(
        "rigid_multibone_exports",
        "offline_export_ready" if int(data.get("failed", 0) or 0) == 0 else "offline_export_partial",
        ctx.path(rel),
        {
            "converted": data.get("converted", 0),
            "failed": data.get("failed", 0),
            "skipped": data.get("skipped", 0),
            "archive_byte_length": data.get("archive_byte_length", 0),
        },
        runtime_scope="offline_export_only",
        next_gate="Package and route these exports through actor/object runtime bindings.",
    )


def skinned_bind_pose_record(ctx: LedgerContext) -> dict[str, object] | None:
    rel = "skinned_bind_pose_batch/skinned_bind_pose_package_summary.json"
    data = ctx.read(rel)
    if data is None:
        return missing_record("skinned_bind_pose_exports", ctx.path(rel))
    counts = require_dict(data.get("package_audit_counts"))
    export_counts = require_dict(data.get("counts"))
    issue_count = int(counts.get("total", 0) or 0)
    return base_record(
        "skinned_bind_pose_exports",
        "offline_package_ready" if issue_count == 0 else "offline_package_invalid",
        ctx.path(rel),
        {
            "archive_byte_length": data.get("archive_byte_length", 0),
            "package_issue_count": issue_count,
            "exported": export_counts.get("exported", data.get("exported", 0)),
        },
        runtime_scope="offline_package_only",
        next_gate="Promote per-character runtime profiles with selected draw contracts.",
    )


def skinned_animation_record(ctx: LedgerContext) -> dict[str, object] | None:
    rel = "skinned_animation_batch/skinned_animation_package_summary.json"
    data = ctx.read(rel)
    if data is None:
        return missing_record("skinned_animation_tracks", ctx.path(rel))
    counts = require_dict(data.get("package_audit_counts"))
    issue_count = int(counts.get("total", 0) or 0)
    return base_record(
        "skinned_animation_tracks",
        "offline_package_ready" if issue_count == 0 else "offline_package_invalid",
        ctx.path(rel),
        {
            "considered_csab": data.get("considered_csab", 0),
            "exported": data.get("exported", 0),
            "target_unresolved_or_missing": data.get("target_unresolved_or_missing", 0),
            "target_not_skinned": data.get("target_not_skinned", 0),
            "package_issue_count": issue_count,
        },
        runtime_scope="offline_package_only",
        next_gate="Consume through character runtime profiles and per-actor animation drivers.",
    )


def ctxb_record(ctx: LedgerContext) -> dict[str, object] | None:
    rel = "ctxb_texture_export/ctxb_texture_export_summary.json"
    data = ctx.read(rel)
    if data is None:
        return missing_record("ctxb_textures", ctx.path(rel))
    return base_record(
        "ctxb_textures",
        "offline_export_ready" if int(data.get("export_error_count", 0) or 0) == 0 else "offline_export_partial",
        ctx.path(rel),
        {
            "ctxb_count": data.get("ctxb_count", 0),
            "parsed_count": data.get("parsed_count", 0),
            "exported_count": data.get("exported_count", 0),
            "resource_count": data.get("resource_count", 0),
            "parse_error_count": data.get("parse_error_count", 0),
            "export_error_count": data.get("export_error_count", 0),
        },
        runtime_scope="offline_export_only",
        next_gate="Bind decoded textures to material/runtime resource replacement.",
    )


def kankyo_record(ctx: LedgerContext) -> dict[str, object] | None:
    rel = "kankyo_environment_export/kankyo_environment_package_summary.json"
    data = ctx.read(rel)
    if data is None:
        return missing_record("kankyo_environment", ctx.path(rel))
    counts = require_dict(data.get("package_audit_counts"))
    issue_count = int(counts.get("total", 0) or 0)
    return base_record(
        "kankyo_environment",
        "offline_package_ready" if issue_count == 0 else "offline_package_invalid",
        ctx.path(rel),
        {
            "archive_count": data.get("archive_count", 0),
            "converted": data.get("converted", 0),
            "failed": data.get("failed", 0),
            "package_issue_count": issue_count,
        },
        runtime_scope="route_binding_metadata_ready",
        next_gate="Enable route-local sky/environment binding only with explicit runtime manifest.",
    )


def prerendered_room_record(ctx: LedgerContext) -> dict[str, object] | None:
    rel = "prerendered_room_replacement_export/prerendered_room_export_package_summary.json"
    data = ctx.read(rel)
    if data is None:
        return missing_record("prerendered_room_replacements", ctx.path(rel))
    issue_counts = require_dict(data.get("package_issue_counts"))
    issue_count = int(issue_counts.get("total", 0) or 0)
    return base_record(
        "prerendered_room_replacements",
        "offline_package_ready" if issue_count == 0 else "offline_package_invalid",
        ctx.path(rel),
        {
            "replacement_background_family_count": data.get("replacement_background_family_count", 0),
            "replacement_ready_family_count": data.get("replacement_ready_family_count", 0),
            "converted": data.get("converted", 0),
            "archive_entry_count": data.get("archive_entry_count", 0),
            "package_issue_count": issue_count,
        },
        runtime_scope="offline_package_only",
        next_gate="Route replacement rooms behind scene/room fallback gates.",
    )


def collision_record(ctx: LedgerContext) -> dict[str, object] | None:
    rel = "collision_activation/collision_activation_full_visual_package_summary.json"
    data = ctx.read(rel)
    if data is None:
        return missing_record("scene_collision_activation", ctx.path(rel))
    counts = require_dict(data.get("package_audit_counts"))
    issue_count = int(counts.get("total", 0) or 0)
    return base_record(
        "scene_collision_activation",
        "offline_package_ready" if issue_count == 0 else "offline_package_invalid",
        ctx.path(rel),
        {
            "accepted_scene_count": data.get("accepted_scene_count", 0),
            "fallback_scene_count": data.get("fallback_scene_count", 0),
            "collision_resource_count": data.get("collision_resource_count", 0),
            "package_issue_count": issue_count,
        },
        runtime_scope="offline_package_only",
        next_gate="Promote scene collision per route with floor/ceiling runtime proof.",
    )


def kokiri_route_record(ctx: LedgerContext) -> dict[str, object] | None:
    rel = "kokiri_runtime_route/oot3d_kokiri_runtime_manifest_enabled_summary.json"
    data = ctx.read(rel)
    if data is None:
        return missing_record("kokiri_route_runtime_manifest", ctx.path(rel))
    runtime_blockers = int(data.get("runtime_blocker_total", 0) or 0)
    status = "runtime_route_enabled" if data.get("runtime_enablement_status") == "oot3d_runtime_enabled" else "fallback_only"
    if runtime_blockers:
        status = "runtime_blocked"
    return base_record(
        "kokiri_route_runtime_manifest",
        status,
        ctx.path(rel),
        {
            "route_id": data.get("route_id"),
            "runtime_enablement_status": data.get("runtime_enablement_status"),
            "resource_target_count": data.get("resource_target_count", 0),
            "runtime_blocker_total": runtime_blockers,
            "selected_room_actor_entry_total": data.get("selected_room_actor_entry_total", 0),
        },
        runtime_scope="link_house_to_kokiri_forest",
        next_gate="Prove each routed actor/collision/audio/environment family at runtime.",
    )


def link_child_character_record(ctx: LedgerContext) -> dict[str, object] | None:
    rel = "character_conversion/link_child_character_package_summary.json"
    data = ctx.read(rel)
    if data is None:
        return missing_record("link_child_character_profile", ctx.path(rel))
    issue_counts = require_dict(data.get("issue_counts"))
    issue_count = int(issue_counts.get("total", 0) or 0)
    parity = require_dict(data.get("minimal_runtime_player_parity"))
    anb_semantic = require_dict(data.get("runtime_anb_semantic_candidate_contract"))
    anb_channel_contract = require_dict(ctx.read("anb_export/link_child_anb_semantic_channel_contract.json"))
    anb_channel_component_family_counts = require_dict(anb_channel_contract.get("component_family_counts"))
    anb_raw_csab_correlation = require_dict(anb_channel_contract.get("raw_layout_csab_correlation"))
    anb_raw_csab_status_counts = require_dict(anb_raw_csab_correlation.get("status_counts"))
    anb_raw_csab_divergence_class_counts = require_dict(
        anb_raw_csab_correlation.get("divergence_class_counts")
    )
    anb_raw_csab_risk_class_counts = require_dict(anb_raw_csab_correlation.get("risk_class_counts"))
    anb_unresolved_closure = require_dict(
        ctx.read("anb_export/link_child_anb_unresolved_semantic_closure_ledger.json")
    )
    anb_unresolved_closure_action_counts = require_dict(
        anb_unresolved_closure.get("closure_action_class_counts")
    )
    animation_semantic_mapping = require_dict(
        ctx.read("character_conversion/link_child_animation_semantic_mapping_summary.json")
    )
    animation_semantic_resolution = require_dict(animation_semantic_mapping.get("semantic_resolution_contract"))
    animation_semantic_resolution_status_counts = require_dict(
        animation_semantic_resolution.get("semantic_resolution_status_counts")
    )
    animation_semantic_blocked_frontier = require_dict(
        animation_semantic_mapping.get("semantic_blocked_resolution_frontier")
    )
    animation_semantic_blocked_frontier_class_counts = require_dict(
        animation_semantic_blocked_frontier.get("frontier_class_counts")
    )
    animation_semantic_blocked_frontier_gate_counts = require_dict(
        animation_semantic_blocked_frontier.get("frontier_gate_counts")
    )
    animation_semantic_materialization_closure = require_dict(
        animation_semantic_resolution.get("materialization_closure")
    )
    animation_semantic_materialized_pose_metric = require_dict(
        animation_semantic_mapping.get("semantic_materialized_pose_metric")
    )
    animation_semantic_materialized_pose_metric_status_counts = require_dict(
        animation_semantic_materialized_pose_metric.get("status_counts")
    )
    animation_semantic_materialized_pose_metric_reference_envelope_counts = require_dict(
        animation_semantic_materialized_pose_metric.get("reference_envelope_status_counts")
    )
    animation_semantic_ownership_derivative_plan = require_dict(
        animation_semantic_mapping.get("semantic_ownership_derivative_plan")
    )
    animation_semantic_ownership_derivative_track_manifest = require_dict(
        animation_semantic_mapping.get("semantic_ownership_derivative_track_manifest")
    )
    animation_semantic_ownership_derivative_pose_metric = require_dict(
        animation_semantic_mapping.get("semantic_ownership_derivative_pose_metric")
    )
    animation_semantic_ownership_derivative_pose_metric_status_counts = require_dict(
        animation_semantic_ownership_derivative_pose_metric.get("status_counts")
    )
    animation_semantic_ownership_derivative_pose_metric_reference_envelope_counts = require_dict(
        animation_semantic_ownership_derivative_pose_metric.get("reference_envelope_status_counts")
    )
    animation_semantic_ownership_derivative_frontier = require_dict(
        animation_semantic_mapping.get("semantic_ownership_derivative_frontier")
    )
    animation_semantic_ownership_derivative_frontier_class_counts = require_dict(
        animation_semantic_ownership_derivative_frontier.get("derivative_frontier_class_counts")
    )
    animation_semantic_ownership_derivative_frontier_gate_counts = require_dict(
        animation_semantic_ownership_derivative_frontier.get("post_derivative_gate_counts")
    )
    animation_semantic_ownership_derivative_frontier_source_counts = require_dict(
        animation_semantic_ownership_derivative_frontier.get("source_frontier_class_counts")
    )
    animation_semantic_ownership_derivative_frontier_reference_counts = require_dict(
        animation_semantic_ownership_derivative_frontier.get("reference_envelope_status_counts")
    )
    animation_semantic_ownership_reuse_arbitration = require_dict(
        animation_semantic_mapping.get("semantic_ownership_reuse_arbitration")
    )
    animation_semantic_ownership_reuse_class_counts = require_dict(
        animation_semantic_ownership_reuse_arbitration.get("ownership_reuse_arbitration_class_counts")
    )
    animation_semantic_ownership_reuse_route_counts = require_dict(
        animation_semantic_ownership_reuse_arbitration.get("route_evidence_status_counts")
    )
    animation_semantic_ownership_reuse_scope_counts = require_dict(
        animation_semantic_ownership_reuse_arbitration.get("policy_decision_scope_counts")
    )
    animation_semantic_ownership_reuse_source_group_counts = require_dict(
        animation_semantic_ownership_reuse_arbitration.get("source_group_class_counts")
    )
    animation_semantic_route_proven_ownership_callsite = require_dict(
        animation_semantic_mapping.get("semantic_route_proven_ownership_callsite")
    )
    animation_semantic_route_proven_ownership_context_counts = require_dict(
        animation_semantic_route_proven_ownership_callsite.get("context_name_counts")
    )
    animation_semantic_route_proven_ownership_callsite_class_counts = require_dict(
        animation_semantic_route_proven_ownership_callsite.get("callsite_review_class_counts")
    )
    animation_semantic_route_proven_ownership_package_counts = require_dict(
        animation_semantic_route_proven_ownership_callsite.get("package_candidate_status_counts")
    )
    animation_semantic_route_proven_ownership_package = require_dict(
        ctx.read("character_conversion/route_proven_ownership_package/link_child_route_proven_ownership_package_summary.json")
    )
    animation_semantic_route_proven_ownership_package_track_counts = require_dict(
        animation_semantic_route_proven_ownership_package.get("counts")
    )
    animation_semantic_route_proven_ownership_package_audit_counts = require_dict(
        animation_semantic_route_proven_ownership_package.get("package_audit_counts")
    )
    animation_semantic_route_proven_ownership_package_issue_counts = require_dict(
        animation_semantic_route_proven_ownership_package_audit_counts.get("issue_counts")
    )
    animation_semantic_route_proven_ownership_runtime_parity = require_dict(
        ctx.read(
            "character_conversion/route_proven_ownership_runtime_parity/"
            "link_child_route_proven_ownership_runtime_parity_summary.json"
        )
    )
    animation_semantic_route_proven_ownership_runtime_parity_counts = require_dict(
        animation_semantic_route_proven_ownership_runtime_parity.get("counts")
    )
    ownership_reuse_diagnostic_package = require_dict(
        ctx.read("character_conversion/ownership_reuse_diagnostic_package/link_child_ownership_reuse_diagnostic_package_summary.json")
    )
    ownership_reuse_diagnostic_package_counts = require_dict(
        ownership_reuse_diagnostic_package.get("counts")
    )
    ownership_reuse_diagnostic_package_audit_counts = require_dict(
        ownership_reuse_diagnostic_package.get("package_audit_counts")
    )
    ownership_reuse_diagnostic_package_issue_counts = require_dict(
        ownership_reuse_diagnostic_package_audit_counts.get("issue_counts")
    )
    ownership_reuse_diagnostic_runtime_parity = require_dict(
        ctx.read(
            "character_conversion/ownership_reuse_diagnostic_runtime_parity/"
            "link_child_ownership_reuse_diagnostic_runtime_parity_summary.json"
        )
    )
    ownership_reuse_diagnostic_runtime_parity_counts = require_dict(
        ownership_reuse_diagnostic_runtime_parity.get("counts")
    )
    ownership_source_risk_diagnostic_package = require_dict(
        ctx.read(
            "character_conversion/ownership_source_risk_diagnostic_package/"
            "link_child_ownership_source_risk_diagnostic_package_summary.json"
        )
    )
    ownership_source_risk_diagnostic_package_counts = require_dict(
        ownership_source_risk_diagnostic_package.get("counts")
    )
    ownership_source_risk_diagnostic_package_audit_counts = require_dict(
        ownership_source_risk_diagnostic_package.get("package_audit_counts")
    )
    ownership_source_risk_diagnostic_package_issue_counts = require_dict(
        ownership_source_risk_diagnostic_package_audit_counts.get("issue_counts")
    )
    ownership_source_risk_diagnostic_runtime_parity = require_dict(
        ctx.read(
            "character_conversion/ownership_source_risk_diagnostic_runtime_parity/"
            "link_child_ownership_source_risk_diagnostic_runtime_parity_summary.json"
        )
    )
    ownership_source_risk_diagnostic_runtime_parity_counts = require_dict(
        ownership_source_risk_diagnostic_runtime_parity.get("counts")
    )
    anonymous_numeric_diagnostic_package = require_dict(
        ctx.read(
            "character_conversion/anonymous_numeric_diagnostic_package/"
            "link_child_anonymous_numeric_diagnostic_package_summary.json"
        )
    )
    anonymous_numeric_diagnostic_package_counts = require_dict(
        anonymous_numeric_diagnostic_package.get("counts")
    )
    anonymous_numeric_diagnostic_package_audit_counts = require_dict(
        anonymous_numeric_diagnostic_package.get("package_audit_counts")
    )
    anonymous_numeric_diagnostic_package_issue_counts = require_dict(
        anonymous_numeric_diagnostic_package_audit_counts.get("issue_counts")
    )
    anonymous_numeric_diagnostic_runtime_parity = require_dict(
        ctx.read(
            "character_conversion/anonymous_numeric_diagnostic_runtime_parity/"
            "link_child_anonymous_numeric_diagnostic_runtime_parity_summary.json"
        )
    )
    anonymous_numeric_diagnostic_runtime_parity_counts = require_dict(
        anonymous_numeric_diagnostic_runtime_parity.get("counts")
    )
    anonymous_identity_matrix = require_dict(
        ctx.read(
            "character_conversion/anonymous_identity_matrix/"
            "link_child_anonymous_identity_matrix_summary.json"
        )
    )
    anonymous_identity_decision_status_counts = require_dict(
        anonymous_identity_matrix.get("decision_status_counts")
    )
    anonymous_identity_promotion_review_counts = require_dict(
        anonymous_identity_matrix.get("promotion_review_class_counts")
    )
    anonymous_identity_source_contract_counts = require_dict(
        anonymous_identity_matrix.get("source_contract_counts")
    )
    anonymous_n64_table_context = require_dict(
        ctx.read(
            "character_conversion/anonymous_n64_table_context/"
            "link_child_anonymous_n64_table_context_summary.json"
        )
    )
    anonymous_n64_table_identity_source_usage_counts = require_dict(
        anonymous_n64_table_context.get("identity_source_usage_status_counts")
    )
    anonymous_n64_table_neighbor_gap_counts = require_dict(
        anonymous_n64_table_context.get("neighbor_gap_class_counts")
    )
    anonymous_owner_capture_matrix = require_dict(
        ctx.read(
            "character_conversion/anonymous_owner_capture_matrix/"
            "link_child_anonymous_owner_capture_matrix_summary.json"
        )
    )
    anonymous_owner_capture_status_counts = require_dict(
        anonymous_owner_capture_matrix.get("capture_status_counts")
    )
    anonymous_owner_capture_context_class_counts = require_dict(
        anonymous_owner_capture_matrix.get("owner_context_class_counts")
    )
    anonymous_owner_runtime_config_matrix = require_dict(
        ctx.read(
            "character_conversion/anonymous_owner_runtime_capture_config/"
            "link_child_anonymous_owner_runtime_config_matrix_summary.json"
        )
    )
    anonymous_identity_owner_dependency_policy = require_dict(
        ctx.read(
            "character_conversion/anonymous_identity_owner_dependency_policy/"
            "link_child_anonymous_identity_owner_dependency_policy_summary.json"
        )
    )
    anonymous_identity_owner_dependency_policy_status_counts = require_dict(
        anonymous_identity_owner_dependency_policy.get("dependency_decision_status_counts")
    )
    anonymous_identity_owner_dependency_policy_identity_class_counts = require_dict(
        anonymous_identity_owner_dependency_policy.get("identity_class_counts")
    )
    anonymous_identity_owner_dependency_policy_capture_status_counts = require_dict(
        anonymous_identity_owner_dependency_policy.get("capture_status_counts")
    )
    anonymous_identity_owner_dependency_policy_context_class_counts = require_dict(
        anonymous_identity_owner_dependency_policy.get("owner_context_class_counts")
    )
    anonymous_identity_owner_dependency_policy_source_contract_counts = require_dict(
        anonymous_identity_owner_dependency_policy.get("source_contract_counts")
    )
    ownership_reuse_runtime_config_matrix = require_dict(
        ctx.read(
            "character_conversion/ownership_reuse_runtime_capture_config/"
            "link_child_ownership_reuse_runtime_config_matrix_summary.json"
        )
    )
    ownership_source_risk_runtime_config_matrix = require_dict(
        ctx.read(
            "character_conversion/ownership_source_risk_runtime_capture_config/"
            "link_child_ownership_source_risk_runtime_config_matrix_summary.json"
        )
    )
    semantic_frontier_closure_matrix = require_dict(
        ctx.read(
            "character_conversion/semantic_frontier_closure_matrix/"
            "link_child_semantic_frontier_closure_matrix_summary.json"
        )
    )
    semantic_frontier_closure_workorder_kind_counts = require_dict(
        semantic_frontier_closure_matrix.get("workorder_kind_counts")
    )
    semantic_frontier_closure_frontier_gate_counts = require_dict(
        semantic_frontier_closure_matrix.get("frontier_gate_counts")
    )
    semantic_frontier_closure_coverage_status_counts = require_dict(
        semantic_frontier_closure_matrix.get("coverage_status_counts")
    )
    semantic_frontier_closure_harness_evidence_status_counts = require_dict(
        semantic_frontier_closure_matrix.get("harness_evidence_status_counts")
    )
    semantic_frontier_closure_post_harness_blocker_counts = require_dict(
        semantic_frontier_closure_matrix.get("post_harness_blocker_counts")
    )
    promoted_owner_alias_policy = require_dict(
        ctx.read(
            "character_conversion/promoted_owner_alias_policy/"
            "link_child_promoted_owner_alias_policy_summary.json"
        )
    )
    promoted_owner_alias_policy_status_counts = require_dict(
        promoted_owner_alias_policy.get("policy_decision_status_counts")
    )
    route_proven_ownership_policy = require_dict(
        ctx.read(
            "character_conversion/route_proven_ownership_policy/"
            "link_child_route_proven_ownership_policy_summary.json"
        )
    )
    route_proven_ownership_policy_status_counts = require_dict(
        route_proven_ownership_policy.get("policy_decision_status_counts")
    )
    route_absent_ownership_reuse_policy = require_dict(
        ctx.read(
            "character_conversion/route_absent_ownership_reuse_policy/"
            "link_child_route_absent_ownership_reuse_policy_summary.json"
        )
    )
    route_absent_ownership_reuse_policy_status_counts = require_dict(
        route_absent_ownership_reuse_policy.get("policy_decision_status_counts")
    )
    route_absent_ownership_reuse_policy_source_group_class_counts = require_dict(
        route_absent_ownership_reuse_policy.get("source_group_class_counts")
    )
    ownership_source_risk_dependency_policy = require_dict(
        ctx.read(
            "character_conversion/ownership_source_risk_dependency_policy/"
            "link_child_ownership_source_risk_dependency_policy_summary.json"
        )
    )
    ownership_source_risk_dependency_policy_status_counts = require_dict(
        ownership_source_risk_dependency_policy.get("dependency_decision_status_counts")
    )
    runtime_capture_execution_plan = require_dict(
        ctx.read(
            "character_conversion/runtime_capture_execution_plan/"
            "link_child_runtime_capture_execution_plan_summary.json"
        )
    )
    runtime_capture_execution_plan_phase_counts = require_dict(
        runtime_capture_execution_plan.get("phase_counts")
    )
    runtime_capture_execution_plan_runner_status_counts = require_dict(
        runtime_capture_execution_plan.get("runner_status_counts")
    )
    runtime_capture_execution_plan_execution_status_counts = require_dict(
        runtime_capture_execution_plan.get("execution_status_counts")
    )
    runtime_capture_execution_plan_target_install = require_dict(
        runtime_capture_execution_plan.get("target_install")
    )
    residual_closure_kit = require_dict(
        ctx.read(
            "character_conversion/residual_closure_kit/"
            "link_child_residual_closure_kit_summary.json"
        )
    )
    residual_closure_kit_lane_counts = require_dict(
        residual_closure_kit.get("lane_counts")
    )
    residual_closure_kit_config_status_counts = require_dict(
        residual_closure_kit.get("config_status_counts")
    )
    pose_source_risk_diagnostic_package = require_dict(
        ctx.read(
            "character_conversion/pose_source_risk_diagnostic_package/"
            "link_child_pose_source_risk_diagnostic_package_summary.json"
        )
    )
    pose_source_risk_diagnostic_package_counts = require_dict(
        pose_source_risk_diagnostic_package.get("counts")
    )
    pose_source_risk_diagnostic_package_audit_counts = require_dict(
        pose_source_risk_diagnostic_package.get("package_audit_counts")
    )
    pose_source_risk_diagnostic_package_issue_counts = require_dict(
        pose_source_risk_diagnostic_package_audit_counts.get("issue_counts")
    )
    pose_source_risk_diagnostic_runtime_parity = require_dict(
        ctx.read(
            "character_conversion/pose_source_risk_diagnostic_runtime_parity/"
            "link_child_pose_source_risk_diagnostic_runtime_parity_summary.json"
        )
    )
    pose_source_risk_diagnostic_runtime_parity_counts = require_dict(
        pose_source_risk_diagnostic_runtime_parity.get("counts")
    )
    pose_source_callsite_context = require_dict(
        ctx.read("character_conversion/link_child_animation_pose_source_callsite_context.json")
    )
    pose_source_callsite_context_class_counts = require_dict(
        pose_source_callsite_context.get("callsite_review_class_counts")
    )
    pose_source_callsite_next_evidence_counts = require_dict(
        pose_source_callsite_context.get("next_evidence_counts")
    )
    pose_source_runtime_capture_matrix = require_dict(
        ctx.read(
            "character_conversion/pose_source_runtime_capture_matrix/"
            "link_child_pose_source_runtime_capture_matrix_summary.json"
        )
    )
    pose_source_runtime_capture_status_counts = require_dict(
        pose_source_runtime_capture_matrix.get("capture_status_counts")
    )
    pose_source_runtime_capture_scope_counts = require_dict(
        pose_source_runtime_capture_matrix.get("capture_scope_counts")
    )
    pose_source_runtime_capture_config_matrix = require_dict(
        ctx.read(
            "character_conversion/pose_source_runtime_capture_config/"
            "link_child_pose_source_runtime_config_matrix_summary.json"
        )
    )
    pose_source_direct_callsite_dependency_policy = require_dict(
        ctx.read(
            "character_conversion/pose_source_direct_callsite_dependency_policy/"
            "link_child_pose_source_direct_callsite_dependency_policy_summary.json"
        )
    )
    pose_source_direct_callsite_dependency_status_counts = require_dict(
        pose_source_direct_callsite_dependency_policy.get("dependency_decision_status_counts")
    )
    pose_source_direct_callsite_dependency_class_counts = require_dict(
        pose_source_direct_callsite_dependency_policy.get("callsite_review_class_counts")
    )
    pose_source_direct_callsite_dependency_next_evidence_counts = require_dict(
        pose_source_direct_callsite_dependency_policy.get("next_evidence_counts")
    )
    pose_source_direct_callsite_dependency_capture_status_counts = require_dict(
        pose_source_direct_callsite_dependency_policy.get("capture_status_counts")
    )
    pose_source_direct_callsite_dependency_forced_status_counts = require_dict(
        pose_source_direct_callsite_dependency_policy.get("forced_animation_only_status_counts")
    )
    direct_callsite_source_identity_review = require_dict(
        ctx.read(
            "character_conversion/direct_callsite_source_identity_review/"
            "link_child_direct_callsite_source_identity_review_summary.json"
        )
    )
    direct_callsite_source_identity_review_status_counts = require_dict(
        direct_callsite_source_identity_review.get("source_identity_review_status_counts")
    )
    direct_callsite_source_identity_runtime_variant_counts = require_dict(
        direct_callsite_source_identity_review.get("runtime_variant_evidence_status_counts")
    )
    route_runtime_capture_matrix = require_dict(
        ctx.read(
            "character_conversion/route_runtime_capture_matrix/"
            "link_child_route_runtime_capture_matrix_summary.json"
        )
    )
    route_runtime_capture_context_class_counts = require_dict(
        route_runtime_capture_matrix.get("route_context_class_counts")
    )
    route_runtime_capture_status_counts = require_dict(
        route_runtime_capture_matrix.get("capture_status_counts")
    )
    route_runtime_diagnostic_package = require_dict(
        ctx.read(
            "character_conversion/route_runtime_diagnostic_package/"
            "link_child_route_runtime_diagnostic_package_summary.json"
        )
    )
    route_runtime_diagnostic_package_counts = require_dict(
        route_runtime_diagnostic_package.get("counts")
    )
    route_runtime_diagnostic_package_audit_counts = require_dict(
        route_runtime_diagnostic_package.get("package_audit_counts")
    )
    route_runtime_diagnostic_package_issue_counts = require_dict(
        route_runtime_diagnostic_package_audit_counts.get("issue_counts")
    )
    route_runtime_diagnostic_runtime_parity = require_dict(
        ctx.read(
            "character_conversion/route_runtime_diagnostic_runtime_parity/"
            "link_child_route_runtime_diagnostic_runtime_parity_summary.json"
        )
    )
    route_runtime_diagnostic_runtime_parity_counts = require_dict(
        route_runtime_diagnostic_runtime_parity.get("counts")
    )
    route_runtime_capture_config_matrix = require_dict(
        ctx.read(
            "character_conversion/route_runtime_capture_config/"
            "link_child_route_runtime_config_matrix_summary.json"
        )
    )
    route_runtime_n64_usage_context = require_dict(
        ctx.read(
            "character_conversion/route_runtime_n64_usage_context/"
            "link_child_route_runtime_n64_usage_context_summary.json"
        )
    )
    route_runtime_n64_usage_evidence_kind_counts = require_dict(
        route_runtime_n64_usage_context.get("evidence_kind_counts")
    )
    route_runtime_n64_usage_runner_scope_counts = require_dict(
        route_runtime_n64_usage_context.get("context_runner_scope_counts")
    )
    route_runtime_dependency_policy = require_dict(
        ctx.read(
            "character_conversion/route_runtime_dependency_policy/"
            "link_child_route_runtime_dependency_policy_summary.json"
        )
    )
    route_runtime_dependency_policy_status_counts = require_dict(
        route_runtime_dependency_policy.get("dependency_decision_status_counts")
    )
    route_runtime_dependency_policy_context_class_counts = require_dict(
        route_runtime_dependency_policy.get("route_context_class_counts")
    )
    route_runtime_dependency_policy_evidence_kind_counts = require_dict(
        route_runtime_dependency_policy.get("evidence_kind_counts")
    )
    n64_route_proof_priority_matrix = require_dict(
        ctx.read(
            "character_conversion/n64_route_proof_priority_matrix/"
            "link_child_n64_route_proof_priority_matrix_summary.json"
        )
    )
    n64_route_proof_priority_bucket_counts = require_dict(
        n64_route_proof_priority_matrix.get("route_proof_bucket_counts")
    )
    n64_route_proof_priority_status_counts = require_dict(
        n64_route_proof_priority_matrix.get("route_proof_status_counts")
    )
    n64_route_proof_priority_evidence_kind_counts = require_dict(
        n64_route_proof_priority_matrix.get("n64_route_evidence_kind_counts")
    )
    n64_route_proof_priority_blocker_counts = require_dict(
        n64_route_proof_priority_matrix.get("route_proof_blocker_counts")
    )
    n64_route_proof_acceptance_policy = require_dict(
        ctx.read(
            "character_conversion/n64_route_proof_acceptance_policy/"
            "link_child_n64_route_proof_acceptance_policy_summary.json"
        )
    )
    n64_route_proof_acceptance_status_counts = require_dict(
        n64_route_proof_acceptance_policy.get("n64_route_acceptance_status_counts")
    )
    n64_route_proof_acceptance_bucket_counts = require_dict(
        n64_route_proof_acceptance_policy.get("route_proof_bucket_counts")
    )
    n64_route_proof_acceptance_policy_gate_counts = require_dict(
        n64_route_proof_acceptance_policy.get("required_policy_gate_counts")
    )
    n64_route_proof_acceptance_source_identity_review_status_counts = require_dict(
        n64_route_proof_acceptance_policy.get("source_identity_review_status_counts")
    )
    in_game_replacement_gate = require_dict(
        ctx.read(
            "character_conversion/in_game_replacement_gate/"
            "link_child_in_game_replacement_gate_summary.json"
        )
    )
    in_game_replacement_gate_bucket_counts = require_dict(
        in_game_replacement_gate.get("route_proof_bucket_counts")
    )
    in_game_replacement_gate_status_counts = require_dict(
        in_game_replacement_gate.get("replacement_gate_status_counts")
    )
    in_game_replacement_gate_next_blocker_counts = require_dict(
        in_game_replacement_gate.get("next_replacement_blocker_counts")
    )
    in_game_replacement_gate_installed_draw_status_counts = require_dict(
        in_game_replacement_gate.get("installed_draw_gate_status_counts")
    )
    in_game_replacement_gate_route_or_owner_status_counts = require_dict(
        in_game_replacement_gate.get("route_or_owner_policy_gate_status_counts")
    )
    in_game_replacement_gate_source_identity_status_counts = require_dict(
        in_game_replacement_gate.get("source_identity_gate_status_counts")
    )
    installed_draw_proof_queue = require_dict(
        ctx.read(
            "character_conversion/installed_draw_proof_queue/"
            "link_child_installed_draw_proof_queue_summary.json"
        )
    )
    installed_draw_proof_queue_wave_counts = require_dict(
        installed_draw_proof_queue.get("proof_wave_counts")
    )
    installed_draw_proof_queue_status_counts = require_dict(
        installed_draw_proof_queue.get("proof_status_counts")
    )
    installed_draw_proof_queue_automation_status_counts = require_dict(
        installed_draw_proof_queue.get("automation_status_counts")
    )
    installed_draw_proof_queue_route_bucket_counts = require_dict(
        installed_draw_proof_queue.get("route_proof_bucket_counts")
    )
    replay_trigger_seed_matrix = require_dict(
        ctx.read(
            "character_conversion/replay_trigger_seed_matrix/"
            "link_child_replay_trigger_seed_matrix_summary.json"
        )
    )
    replay_trigger_seed_matrix_session_kind_counts = require_dict(
        replay_trigger_seed_matrix.get("session_kind_counts")
    )
    replay_trigger_seed_matrix_automation_class_counts = require_dict(
        replay_trigger_seed_matrix.get("trigger_automation_class_counts")
    )
    replay_trigger_seed_matrix_seed_status_counts = require_dict(
        replay_trigger_seed_matrix.get("seed_status_counts")
    )
    replay_trigger_seed_matrix_blocking_kind_counts = require_dict(
        replay_trigger_seed_matrix.get("blocking_kind_counts")
    )
    first_wave_replay_seed_specs = require_dict(
        ctx.read(
            "character_conversion/first_wave_replay_seed_specs/"
            "link_child_first_wave_replay_seed_specs_summary.json"
        )
    )
    first_wave_replay_seed_specs_evidence_counts = require_dict(
        first_wave_replay_seed_specs.get("n64_route_evidence_kind_counts")
    )
    first_wave_replay_seed_specs_seed_status_counts = require_dict(
        first_wave_replay_seed_specs.get("seed_authoring_status_counts")
    )
    first_wave_replay_seed_specs_session_status_counts = require_dict(
        first_wave_replay_seed_specs.get("session_status_counts")
    )
    ledge_climb_route_plan = require_dict(
        ctx.read(
            "character_conversion/ledge_climb_route_plan/"
            "link_child_ledge_climb_route_plan_summary.json"
        )
    )
    ledge_climb_route_plan_status_counts = require_dict(
        ledge_climb_route_plan.get("status_counts")
    )
    ledge_climb_route_plan_evidence_counts = require_dict(
        ledge_climb_route_plan.get("evidence_kind_counts")
    )
    ledge_climb_state_capture_kit = require_dict(
        ctx.read(
            "character_conversion/ledge_climb_state_capture_kit/"
            "link_child_ledge_climb_state_capture_kit_summary.json"
        )
    )
    ledge_climb_hold_free_capture_status = require_dict(
        ctx.read(
            "character_conversion/runtime_config_capture_status/"
            "link_child_runtime_config_capture_status_"
            "gPlayerAnim_link_normal_jump_climb_hold_free.json"
        )
    )
    natural_replacement_smoke = require_dict(
        ctx.read(
            "character_conversion/natural_replacement_smoke/"
            "link_child_natural_replacement_smoke_summary.json"
        )
    )
    ownership_decision_matrix = require_dict(
        ctx.read(
            "character_conversion/ownership_decision_matrix/"
            "link_child_ownership_decision_matrix_summary.json"
        )
    )
    ownership_decision_status_counts = require_dict(
        ownership_decision_matrix.get("decision_status_counts")
    )
    ownership_decision_acceptance_gate_counts = require_dict(
        ownership_decision_matrix.get("acceptance_gate_counts")
    )
    capture = ctx.read("character_conversion/link_child_climb_runtime_capture_status.json")
    route_capture = ctx.read(
        "character_conversion/route_proven_ownership_runtime_capture/"
        "link_child_route_proven_runtime_capture_status.json"
    )
    route_proven_runtime_capture_matrix = require_dict(
        ctx.read(
            "character_conversion/route_proven_ownership_runtime_capture/"
            "link_child_route_proven_runtime_capture_matrix_summary.json"
        )
    )
    status = "runtime_capture_pending"
    if issue_count != 0:
        status = "runtime_contract_invalid"
    elif parity.get("status") != "valid":
        status = "runtime_parity_invalid"
    elif (
        isinstance(capture, dict)
        and capture.get("status") == "dump_valid"
        or isinstance(route_capture, dict)
        and route_capture.get("status") == "dump_valid"
    ):
        status = "runtime_draw_dump_valid"
    capture_metrics = capture_status_metrics(capture)
    route_capture_metrics = capture_status_metrics(route_capture)
    return base_record(
        "link_child_character_profile",
        status,
        ctx.path(rel),
        {
            "profile_id": data.get("profile_id"),
            "archive_entry_count": data.get("archive_entry_count", 0),
            "resource_entry_count": data.get("resource_entry_count", 0),
            "package_issue_count": issue_count,
            "minimal_runtime_parity_status": parity.get("status"),
            "minimal_runtime_parity_animation_count": parity.get("animation_count", 0),
            "animation_semantic_resolution_contract_row_count": animation_semantic_resolution.get("row_count", 0),
            "animation_semantic_source_identified_count": animation_semantic_resolution.get(
                "semantic_source_identified_count",
                0,
            ),
            "animation_semantic_source_identified_percent": animation_semantic_resolution.get(
                "semantic_source_identified_percent",
                0,
            ),
            "animation_semantic_accepted_or_runtime_mapping_count": animation_semantic_resolution.get(
                "accepted_or_runtime_mapping_count",
                0,
            ),
            "animation_semantic_materialization_pending_source_count": animation_semantic_resolution.get(
                "materialization_pending_source_count",
                0,
            ),
            "animation_semantic_offline_materialization_status": animation_semantic_resolution.get(
                "offline_materialization_status"
            ),
            "animation_semantic_offline_materialized_source_count": animation_semantic_resolution.get(
                "offline_materialized_source_count",
                0,
            ),
            "animation_semantic_offline_unmaterialized_source_count": animation_semantic_resolution.get(
                "offline_unmaterialized_source_count",
                0,
            ),
            "animation_semantic_materialization_closure_ready_contract_count": animation_semantic_materialization_closure.get(
                "ready_contract_count",
                0,
            ),
            "animation_semantic_materialization_closure_manifest_issue_count": animation_semantic_materialization_closure.get(
                "manifest_issue_count",
                0,
            ),
            "animation_semantic_materialization_closure_extra_track_count": animation_semantic_materialization_closure.get(
                "extra_materialized_track_count",
                0,
            ),
            "animation_semantic_source_sample_parity_passed_count": animation_semantic_materialization_closure.get(
                "source_sample_parity_passed_count",
                0,
            ),
            "animation_semantic_source_sample_parity_checked_channel_count": animation_semantic_materialization_closure.get(
                "source_sample_parity_checked_channel_count",
                0,
            ),
            "animation_semantic_source_sample_parity_checked_key_count": animation_semantic_materialization_closure.get(
                "source_sample_parity_checked_key_count",
                0,
            ),
            "animation_semantic_source_sample_parity_zero_tangent_mismatch_count": (
                animation_semantic_materialization_closure.get(
                    "source_sample_parity_zero_tangent_mismatch_count",
                    0,
                )
            ),
            "animation_semantic_source_sample_parity_issue_count": animation_semantic_materialization_closure.get(
                "source_sample_parity_issue_count",
                0,
            ),
            "animation_semantic_source_sample_parity_max_abs_value_delta": animation_semantic_materialization_closure.get(
                "source_sample_parity_max_abs_value_delta",
                0,
            ),
            "animation_semantic_output_frame_dense_validation_passed_count": (
                animation_semantic_materialization_closure.get(
                    "output_frame_dense_validation_passed_count",
                    0,
                )
            ),
            "animation_semantic_output_frame_dense_validation_sampled_frame_count": (
                animation_semantic_materialization_closure.get(
                    "output_frame_dense_validation_sampled_frame_count",
                    0,
                )
            ),
            "animation_semantic_output_frame_dense_validation_checked_key_count": (
                animation_semantic_materialization_closure.get(
                    "output_frame_dense_validation_checked_key_count",
                    0,
                )
            ),
            "animation_semantic_output_frame_dense_validation_zero_tangent_mismatch_count": (
                animation_semantic_materialization_closure.get(
                    "output_frame_dense_validation_zero_tangent_mismatch_count",
                    0,
                )
            ),
            "animation_semantic_output_frame_dense_validation_issue_count": (
                animation_semantic_materialization_closure.get(
                    "output_frame_dense_validation_issue_count",
                    0,
                )
            ),
            "animation_semantic_materialized_pose_parity_pending_count": require_dict(
                animation_semantic_materialization_closure.get("materialized_pose_parity_status_counts")
            ).get(
                "pending_runtime_package_pose_parity",
                0,
            ),
            "animation_semantic_materialized_pose_metric_status": animation_semantic_materialized_pose_metric.get(
                "status"
            ),
            "animation_semantic_materialized_pose_metric_row_count": animation_semantic_materialized_pose_metric.get(
                "row_count",
                0,
            ),
            "animation_semantic_materialized_pose_metric_measured_row_count": (
                animation_semantic_materialized_pose_metric.get("measured_row_count", 0)
            ),
            "animation_semantic_materialized_pose_metric_frame_pair_count": (
                animation_semantic_materialized_pose_metric.get("frame_pair_count", 0)
            ),
            "animation_semantic_materialized_pose_metric_measured_frame_pair_count": (
                animation_semantic_materialized_pose_metric.get("measured_frame_pair_count", 0)
            ),
            "animation_semantic_materialized_pose_metric_issue_count": (
                animation_semantic_materialized_pose_metric.get("issue_count", 0)
            ),
            "animation_semantic_materialized_pose_metric_inside_reference_envelope_count": (
                animation_semantic_materialized_pose_metric_reference_envelope_counts.get(
                    "inside_reference_envelope",
                    0,
                )
            ),
            "animation_semantic_materialized_pose_metric_outside_reference_envelope_count": (
                animation_semantic_materialized_pose_metric_reference_envelope_counts.get(
                    "outside_reference_envelope",
                    0,
                )
            ),
            "animation_semantic_materialized_pose_metric_status_counts": (
                animation_semantic_materialized_pose_metric_status_counts
            ),
            "animation_semantic_materialized_pose_metric_reference_envelope_status_counts": (
                animation_semantic_materialized_pose_metric_reference_envelope_counts
            ),
            "animation_semantic_materialized_pose_metric_scale_factor_oot3d_per_n64": (
                animation_semantic_materialized_pose_metric.get("scale_factor_oot3d_per_n64", {})
            ),
            "animation_semantic_materialized_pose_metric_center_delta_normalized": (
                animation_semantic_materialized_pose_metric.get("center_delta_normalized", {})
            ),
            "animation_semantic_materialized_pose_metric_normalized_extent_max_abs_delta": (
                animation_semantic_materialized_pose_metric.get("normalized_extent_max_abs_delta", {})
            ),
            "animation_semantic_ownership_derivative_plan_status": (
                animation_semantic_ownership_derivative_plan.get("status")
            ),
            "animation_semantic_ownership_derivative_plan_kind": (
                animation_semantic_ownership_derivative_plan.get("plan_kind")
            ),
            "animation_semantic_ownership_derivative_frontier_row_count": (
                animation_semantic_ownership_derivative_plan.get("ownership_frontier_row_count", 0)
            ),
            "animation_semantic_ownership_derivative_candidate_count": (
                animation_semantic_ownership_derivative_plan.get("diagnostic_derivative_candidate_count", 0)
            ),
            "animation_semantic_ownership_derivative_plan_row_count": (
                animation_semantic_ownership_derivative_plan.get("plan_row_count", 0)
            ),
            "animation_semantic_ownership_derivative_resolved_source_resource_count": (
                animation_semantic_ownership_derivative_plan.get("resolved_source_resource_count", 0)
            ),
            "animation_semantic_ownership_derivative_missing_source_resource_count": (
                animation_semantic_ownership_derivative_plan.get("missing_source_resource_count", 0)
            ),
            "animation_semantic_ownership_derivative_source_frame_count_mismatch_count": (
                animation_semantic_ownership_derivative_plan.get("source_frame_count_mismatch_count", 0)
            ),
            "animation_semantic_ownership_derivative_output_resource_path_duplicate_count": (
                animation_semantic_ownership_derivative_plan.get("output_resource_path_duplicate_count", 0)
            ),
            "animation_semantic_ownership_derivative_unique_source_resource_count": (
                animation_semantic_ownership_derivative_plan.get("unique_source_resource_count", 0)
            ),
            "animation_semantic_ownership_derivative_unique_output_resource_path_count": (
                animation_semantic_ownership_derivative_plan.get("unique_output_resource_path_count", 0)
            ),
            "animation_semantic_ownership_derivative_total_target_frame_count": (
                animation_semantic_ownership_derivative_plan.get("total_target_frame_count", 0)
            ),
            "animation_semantic_ownership_derivative_total_source_frame_slot_count": (
                animation_semantic_ownership_derivative_plan.get("total_source_frame_slot_count", 0)
            ),
            "animation_semantic_ownership_derivative_total_frame_sample_count": (
                animation_semantic_ownership_derivative_plan.get("total_frame_sample_count", 0)
            ),
            "animation_semantic_ownership_derivative_plan_issue_count": (
                animation_semantic_ownership_derivative_plan.get("issue_count", 0)
            ),
            "animation_semantic_ownership_derivative_materialization_class_counts": (
                require_dict(animation_semantic_ownership_derivative_plan.get("materialization_class_counts"))
            ),
            "animation_semantic_ownership_derivative_frame_mapping_status_counts": (
                require_dict(animation_semantic_ownership_derivative_plan.get("frame_mapping_status_counts"))
            ),
            "animation_semantic_ownership_derivative_source_resolution_status_counts": (
                require_dict(animation_semantic_ownership_derivative_plan.get("source_resolution_status_counts"))
            ),
            "animation_semantic_ownership_derivative_track_manifest_status": (
                animation_semantic_ownership_derivative_track_manifest.get("status")
            ),
            "animation_semantic_ownership_derivative_track_manifest_plan_row_count": (
                animation_semantic_ownership_derivative_track_manifest.get("plan_row_count", 0)
            ),
            "animation_semantic_ownership_derivative_materialized_track_count": (
                animation_semantic_ownership_derivative_track_manifest.get("materialized_track_count", 0)
            ),
            "animation_semantic_ownership_derivative_written_file_count": (
                animation_semantic_ownership_derivative_track_manifest.get("written_file_count", 0)
            ),
            "animation_semantic_ownership_derivative_track_manifest_issue_count": (
                animation_semantic_ownership_derivative_track_manifest.get("issue_count", 0)
            ),
            "animation_semantic_ownership_derivative_track_manifest_output_path_duplicate_count": (
                animation_semantic_ownership_derivative_track_manifest.get("output_path_duplicate_count", 0)
            ),
            "animation_semantic_ownership_derivative_total_output_track_count": (
                animation_semantic_ownership_derivative_track_manifest.get("total_output_track_count", 0)
            ),
            "animation_semantic_ownership_derivative_total_output_channel_count": (
                animation_semantic_ownership_derivative_track_manifest.get("total_output_channel_count", 0)
            ),
            "animation_semantic_ownership_derivative_total_output_keyframe_count": (
                animation_semantic_ownership_derivative_track_manifest.get("total_output_keyframe_count", 0)
            ),
            "animation_semantic_ownership_derivative_source_sample_parity_passed_count": (
                animation_semantic_ownership_derivative_track_manifest.get("source_sample_parity_passed_count", 0)
            ),
            "animation_semantic_ownership_derivative_source_sample_parity_checked_channel_count": (
                animation_semantic_ownership_derivative_track_manifest.get(
                    "source_sample_parity_checked_channel_count",
                    0,
                )
            ),
            "animation_semantic_ownership_derivative_source_sample_parity_checked_key_count": (
                animation_semantic_ownership_derivative_track_manifest.get(
                    "source_sample_parity_checked_key_count",
                    0,
                )
            ),
            "animation_semantic_ownership_derivative_source_sample_parity_zero_tangent_mismatch_count": (
                animation_semantic_ownership_derivative_track_manifest.get(
                    "source_sample_parity_zero_tangent_mismatch_count",
                    0,
                )
            ),
            "animation_semantic_ownership_derivative_source_sample_parity_issue_count": (
                animation_semantic_ownership_derivative_track_manifest.get("source_sample_parity_issue_count", 0)
            ),
            "animation_semantic_ownership_derivative_source_sample_parity_max_abs_value_delta": (
                animation_semantic_ownership_derivative_track_manifest.get(
                    "source_sample_parity_max_abs_value_delta",
                    0,
                )
            ),
            "animation_semantic_ownership_derivative_output_frame_dense_validation_passed_count": (
                animation_semantic_ownership_derivative_track_manifest.get(
                    "output_frame_dense_validation_passed_count",
                    0,
                )
            ),
            "animation_semantic_ownership_derivative_output_frame_dense_validation_sampled_frame_count": (
                animation_semantic_ownership_derivative_track_manifest.get(
                    "output_frame_dense_validation_sampled_frame_count",
                    0,
                )
            ),
            "animation_semantic_ownership_derivative_output_frame_dense_validation_checked_key_count": (
                animation_semantic_ownership_derivative_track_manifest.get(
                    "output_frame_dense_validation_checked_key_count",
                    0,
                )
            ),
            "animation_semantic_ownership_derivative_output_frame_dense_validation_zero_tangent_mismatch_count": (
                animation_semantic_ownership_derivative_track_manifest.get(
                    "output_frame_dense_validation_zero_tangent_mismatch_count",
                    0,
                )
            ),
            "animation_semantic_ownership_derivative_materialized_pose_parity_pending_count": (
                require_dict(
                    animation_semantic_ownership_derivative_track_manifest.get(
                        "materialized_pose_parity_status_counts"
                    )
                ).get("pending_runtime_package_pose_parity", 0)
            ),
            "animation_semantic_ownership_derivative_track_manifest_materialization_class_counts": (
                require_dict(animation_semantic_ownership_derivative_track_manifest.get("materialization_class_counts"))
            ),
            "animation_semantic_ownership_derivative_pose_metric_status": (
                animation_semantic_ownership_derivative_pose_metric.get("status")
            ),
            "animation_semantic_ownership_derivative_pose_metric_row_count": (
                animation_semantic_ownership_derivative_pose_metric.get("row_count", 0)
            ),
            "animation_semantic_ownership_derivative_pose_metric_measured_row_count": (
                animation_semantic_ownership_derivative_pose_metric.get("measured_row_count", 0)
            ),
            "animation_semantic_ownership_derivative_pose_metric_frame_pair_count": (
                animation_semantic_ownership_derivative_pose_metric.get("frame_pair_count", 0)
            ),
            "animation_semantic_ownership_derivative_pose_metric_measured_frame_pair_count": (
                animation_semantic_ownership_derivative_pose_metric.get("measured_frame_pair_count", 0)
            ),
            "animation_semantic_ownership_derivative_pose_metric_issue_count": (
                animation_semantic_ownership_derivative_pose_metric.get("issue_count", 0)
            ),
            "animation_semantic_ownership_derivative_pose_metric_inside_reference_envelope_count": (
                animation_semantic_ownership_derivative_pose_metric_reference_envelope_counts.get(
                    "inside_reference_envelope",
                    0,
                )
            ),
            "animation_semantic_ownership_derivative_pose_metric_outside_reference_envelope_count": (
                animation_semantic_ownership_derivative_pose_metric_reference_envelope_counts.get(
                    "outside_reference_envelope",
                    0,
                )
            ),
            "animation_semantic_ownership_derivative_pose_metric_status_counts": (
                animation_semantic_ownership_derivative_pose_metric_status_counts
            ),
            "animation_semantic_ownership_derivative_pose_metric_reference_envelope_status_counts": (
                animation_semantic_ownership_derivative_pose_metric_reference_envelope_counts
            ),
            "animation_semantic_ownership_derivative_pose_metric_scale_factor_oot3d_per_n64": (
                animation_semantic_ownership_derivative_pose_metric.get("scale_factor_oot3d_per_n64", {})
            ),
            "animation_semantic_ownership_derivative_pose_metric_center_delta_normalized": (
                animation_semantic_ownership_derivative_pose_metric.get("center_delta_normalized", {})
            ),
            "animation_semantic_ownership_derivative_frontier_status": (
                animation_semantic_ownership_derivative_frontier.get("status")
            ),
            "animation_semantic_ownership_derivative_post_frontier_row_count": (
                animation_semantic_ownership_derivative_frontier.get("row_count", 0)
            ),
            "animation_semantic_ownership_derivative_ownership_only_pose_ready_count": (
                animation_semantic_ownership_derivative_frontier.get("ownership_only_pose_ready_count", 0)
            ),
            "animation_semantic_ownership_derivative_ownership_plus_pose_or_source_risk_count": (
                animation_semantic_ownership_derivative_frontier.get("ownership_plus_pose_or_source_risk_count", 0)
            ),
            "animation_semantic_ownership_derivative_frontier_issue_count": (
                animation_semantic_ownership_derivative_frontier.get("issue_count", 0)
            ),
            "animation_semantic_ownership_derivative_frontier_missing_plan_count": (
                animation_semantic_ownership_derivative_frontier.get("missing_plan_count", 0)
            ),
            "animation_semantic_ownership_derivative_frontier_missing_pose_metric_count": (
                animation_semantic_ownership_derivative_frontier.get("missing_pose_metric_count", 0)
            ),
            "animation_semantic_ownership_derivative_frontier_class_counts": (
                animation_semantic_ownership_derivative_frontier_class_counts
            ),
            "animation_semantic_ownership_derivative_frontier_gate_counts": (
                animation_semantic_ownership_derivative_frontier_gate_counts
            ),
            "animation_semantic_ownership_derivative_frontier_source_class_counts": (
                animation_semantic_ownership_derivative_frontier_source_counts
            ),
            "animation_semantic_ownership_derivative_frontier_reference_envelope_status_counts": (
                animation_semantic_ownership_derivative_frontier_reference_counts
            ),
            "animation_semantic_ownership_reuse_arbitration_status": (
                animation_semantic_ownership_reuse_arbitration.get("status")
            ),
            "animation_semantic_ownership_reuse_arbitration_row_count": (
                animation_semantic_ownership_reuse_arbitration.get("row_count", 0)
            ),
            "animation_semantic_ownership_reuse_arbitration_source_group_count": (
                animation_semantic_ownership_reuse_arbitration.get("source_group_count", 0)
            ),
            "animation_semantic_ownership_reuse_arbitration_policy_decision_count": (
                animation_semantic_ownership_reuse_arbitration.get("policy_decision_count", 0)
            ),
            "animation_semantic_ownership_reuse_arbitration_direct_source_reference_count": (
                animation_semantic_ownership_reuse_arbitration.get("direct_source_reference_count", 0)
            ),
            "animation_semantic_ownership_reuse_arbitration_direct_player_actor_route_reference_count": (
                animation_semantic_ownership_reuse_arbitration.get("direct_player_actor_route_reference_count", 0)
            ),
            "animation_semantic_ownership_reuse_arbitration_route_proven_row_count": (
                animation_semantic_ownership_reuse_arbitration.get("route_proven_row_count", 0)
            ),
            "animation_semantic_ownership_reuse_arbitration_route_absent_row_count": (
                animation_semantic_ownership_reuse_arbitration.get("route_absent_row_count", 0)
            ),
            "animation_semantic_ownership_reuse_arbitration_class_counts": (
                animation_semantic_ownership_reuse_class_counts
            ),
            "animation_semantic_ownership_reuse_arbitration_route_evidence_status_counts": (
                animation_semantic_ownership_reuse_route_counts
            ),
            "animation_semantic_ownership_reuse_arbitration_policy_decision_scope_counts": (
                animation_semantic_ownership_reuse_scope_counts
            ),
            "animation_semantic_ownership_reuse_arbitration_source_group_class_counts": (
                animation_semantic_ownership_reuse_source_group_counts
            ),
            "animation_semantic_route_proven_ownership_callsite_status": (
                animation_semantic_route_proven_ownership_callsite.get("status")
            ),
            "animation_semantic_route_proven_ownership_callsite_row_count": (
                animation_semantic_route_proven_ownership_callsite.get("row_count", 0)
            ),
            "animation_semantic_route_proven_ownership_callsite_package_candidate_count": (
                animation_semantic_route_proven_ownership_callsite.get("package_candidate_count", 0)
            ),
            "animation_semantic_route_proven_ownership_callsite_direct_player_actor_source_reference_count": (
                animation_semantic_route_proven_ownership_callsite.get("direct_player_actor_source_reference_count", 0)
            ),
            "animation_semantic_route_proven_ownership_callsite_local_player_actor_source_reference_count": (
                animation_semantic_route_proven_ownership_callsite.get("local_player_actor_source_reference_count", 0)
            ),
            "animation_semantic_route_proven_ownership_callsite_external_player_actor_mirror_reference_count": (
                animation_semantic_route_proven_ownership_callsite.get(
                    "external_player_actor_mirror_reference_count",
                    0,
                )
            ),
            "animation_semantic_route_proven_ownership_callsite_unique_callsite_signature_count": (
                animation_semantic_route_proven_ownership_callsite.get("unique_callsite_signature_count", 0)
            ),
            "animation_semantic_route_proven_ownership_callsite_unique_context_signature_count": (
                animation_semantic_route_proven_ownership_callsite.get("unique_context_signature_count", 0)
            ),
            "animation_semantic_route_proven_ownership_callsite_context_name_counts": (
                animation_semantic_route_proven_ownership_context_counts
            ),
            "animation_semantic_route_proven_ownership_callsite_class_counts": (
                animation_semantic_route_proven_ownership_callsite_class_counts
            ),
            "animation_semantic_route_proven_ownership_callsite_package_candidate_status_counts": (
                animation_semantic_route_proven_ownership_package_counts
            ),
            "animation_semantic_route_proven_ownership_package_status": (
                animation_semantic_route_proven_ownership_package.get("status")
            ),
            "animation_semantic_route_proven_ownership_package_exported_count": (
                animation_semantic_route_proven_ownership_package.get("exported", 0)
            ),
            "animation_semantic_route_proven_ownership_package_failed_count": (
                animation_semantic_route_proven_ownership_package.get("failed", 0)
            ),
            "animation_semantic_route_proven_ownership_package_archive_byte_length": (
                animation_semantic_route_proven_ownership_package.get("archive_byte_length", 0)
            ),
            "animation_semantic_route_proven_ownership_package_track_counts": (
                animation_semantic_route_proven_ownership_package_track_counts
            ),
            "animation_semantic_route_proven_ownership_package_archive_entry_count": (
                animation_semantic_route_proven_ownership_package_audit_counts.get("archive_entry_count", 0)
            ),
            "animation_semantic_route_proven_ownership_package_track_entry_count": (
                animation_semantic_route_proven_ownership_package_audit_counts.get("track_entry_count", 0)
            ),
            "animation_semantic_route_proven_ownership_package_issue_count": (
                animation_semantic_route_proven_ownership_package_issue_counts.get("total", 0)
            ),
            "animation_semantic_route_proven_ownership_package_runtime_acceptance_status": (
                animation_semantic_route_proven_ownership_package.get("runtime_acceptance_status")
            ),
            "animation_semantic_route_proven_ownership_runtime_parity_status": (
                animation_semantic_route_proven_ownership_runtime_parity.get("status")
            ),
            "animation_semantic_route_proven_ownership_runtime_parity_runtime_acceptance_status": (
                animation_semantic_route_proven_ownership_runtime_parity.get("runtime_acceptance_status")
            ),
            "animation_semantic_route_proven_ownership_runtime_parity_exported_count": (
                animation_semantic_route_proven_ownership_runtime_parity.get("exported", 0)
            ),
            "animation_semantic_route_proven_ownership_runtime_parity_failed_count": (
                animation_semantic_route_proven_ownership_runtime_parity.get("failed", 0)
            ),
            "animation_semantic_route_proven_ownership_runtime_parity_counts": (
                animation_semantic_route_proven_ownership_runtime_parity_counts
            ),
            "animation_semantic_route_proven_runtime_capture_matrix_status": (
                route_proven_runtime_capture_matrix.get("status")
            ),
            "animation_semantic_route_proven_runtime_capture_matrix_row_count": (
                route_proven_runtime_capture_matrix.get("record_count", 0)
            ),
            "animation_semantic_route_proven_runtime_capture_ready_count": (
                route_proven_runtime_capture_matrix.get("ready_for_capture_count", 0)
            ),
            "animation_semantic_route_proven_runtime_capture_dump_valid_count": (
                route_proven_runtime_capture_matrix.get("dump_valid_count", 0)
            ),
            "animation_semantic_route_proven_runtime_capture_expected_n64_filter_configured_count": (
                route_proven_runtime_capture_matrix.get("expected_n64_filter_configured_count", 0)
            ),
            "animation_semantic_route_proven_runtime_capture_expected_n64_filter_mismatch_count": (
                route_proven_runtime_capture_matrix.get("expected_n64_filter_mismatch_count", 0)
            ),
            "animation_semantic_route_proven_runtime_capture_context_trace_configured_count": (
                route_proven_runtime_capture_matrix.get("context_trace_configured_count", 0)
            ),
            "animation_semantic_route_proven_runtime_capture_context_trace_present_count": (
                route_proven_runtime_capture_matrix.get("context_trace_present_count", 0)
            ),
            "animation_semantic_route_proven_runtime_capture_context_trace_invalid_count": (
                route_proven_runtime_capture_matrix.get("context_trace_invalid_count", 0)
            ),
            "animation_semantic_route_proven_runtime_capture_issue_count": (
                route_proven_runtime_capture_matrix.get("issue_count", 0)
            ),
            "animation_semantic_ownership_reuse_diagnostic_package_status": (
                ownership_reuse_diagnostic_package.get("status")
            ),
            "animation_semantic_ownership_reuse_diagnostic_package_exported_count": (
                ownership_reuse_diagnostic_package.get("exported", 0)
            ),
            "animation_semantic_ownership_reuse_diagnostic_package_failed_count": (
                ownership_reuse_diagnostic_package.get("failed", 0)
            ),
            "animation_semantic_ownership_reuse_diagnostic_package_issue_count": (
                ownership_reuse_diagnostic_package_issue_counts.get("total", 0)
            ),
            "animation_semantic_ownership_reuse_diagnostic_package_track_counts": (
                ownership_reuse_diagnostic_package_counts
            ),
            "animation_semantic_ownership_reuse_diagnostic_runtime_parity_status": (
                ownership_reuse_diagnostic_runtime_parity.get("status")
            ),
            "animation_semantic_ownership_reuse_diagnostic_runtime_acceptance_status": (
                ownership_reuse_diagnostic_runtime_parity.get("runtime_acceptance_status")
            ),
            "animation_semantic_ownership_reuse_diagnostic_runtime_parity_exported_count": (
                ownership_reuse_diagnostic_runtime_parity.get("exported", 0)
            ),
            "animation_semantic_ownership_reuse_diagnostic_runtime_parity_failed_count": (
                ownership_reuse_diagnostic_runtime_parity.get("failed", 0)
            ),
            "animation_semantic_ownership_reuse_diagnostic_runtime_parity_counts": (
                ownership_reuse_diagnostic_runtime_parity_counts
            ),
            "animation_semantic_ownership_source_risk_diagnostic_package_status": (
                ownership_source_risk_diagnostic_package.get("status")
            ),
            "animation_semantic_ownership_source_risk_diagnostic_package_exported_count": (
                ownership_source_risk_diagnostic_package.get("exported", 0)
            ),
            "animation_semantic_ownership_source_risk_diagnostic_package_failed_count": (
                ownership_source_risk_diagnostic_package.get("failed", 0)
            ),
            "animation_semantic_ownership_source_risk_diagnostic_package_issue_count": (
                ownership_source_risk_diagnostic_package_issue_counts.get("total", 0)
            ),
            "animation_semantic_ownership_source_risk_diagnostic_package_track_counts": (
                ownership_source_risk_diagnostic_package_counts
            ),
            "animation_semantic_ownership_source_risk_diagnostic_runtime_parity_status": (
                ownership_source_risk_diagnostic_runtime_parity.get("status")
            ),
            "animation_semantic_ownership_source_risk_diagnostic_runtime_acceptance_status": (
                ownership_source_risk_diagnostic_runtime_parity.get("runtime_acceptance_status")
            ),
            "animation_semantic_ownership_source_risk_diagnostic_runtime_parity_exported_count": (
                ownership_source_risk_diagnostic_runtime_parity.get("exported", 0)
            ),
            "animation_semantic_ownership_source_risk_diagnostic_runtime_parity_failed_count": (
                ownership_source_risk_diagnostic_runtime_parity.get("failed", 0)
            ),
            "animation_semantic_ownership_source_risk_diagnostic_runtime_parity_counts": (
                ownership_source_risk_diagnostic_runtime_parity_counts
            ),
            "animation_semantic_anonymous_numeric_diagnostic_package_status": (
                anonymous_numeric_diagnostic_package.get("status")
            ),
            "animation_semantic_anonymous_numeric_diagnostic_package_exported_count": (
                anonymous_numeric_diagnostic_package.get("exported", 0)
            ),
            "animation_semantic_anonymous_numeric_diagnostic_package_failed_count": (
                anonymous_numeric_diagnostic_package.get("failed", 0)
            ),
            "animation_semantic_anonymous_numeric_diagnostic_package_issue_count": (
                anonymous_numeric_diagnostic_package_issue_counts.get("total", 0)
            ),
            "animation_semantic_anonymous_numeric_diagnostic_package_track_counts": (
                anonymous_numeric_diagnostic_package_counts
            ),
            "animation_semantic_anonymous_numeric_diagnostic_runtime_parity_status": (
                anonymous_numeric_diagnostic_runtime_parity.get("status")
            ),
            "animation_semantic_anonymous_numeric_diagnostic_runtime_acceptance_status": (
                anonymous_numeric_diagnostic_runtime_parity.get("runtime_acceptance_status")
            ),
            "animation_semantic_anonymous_numeric_diagnostic_runtime_parity_exported_count": (
                anonymous_numeric_diagnostic_runtime_parity.get("exported", 0)
            ),
            "animation_semantic_anonymous_numeric_diagnostic_runtime_parity_failed_count": (
                anonymous_numeric_diagnostic_runtime_parity.get("failed", 0)
            ),
            "animation_semantic_anonymous_numeric_diagnostic_runtime_parity_counts": (
                anonymous_numeric_diagnostic_runtime_parity_counts
            ),
            "animation_semantic_anonymous_identity_matrix_status": (
                anonymous_identity_matrix.get("status")
            ),
            "animation_semantic_anonymous_identity_matrix_row_count": (
                anonymous_identity_matrix.get("record_count", 0)
            ),
            "animation_semantic_anonymous_identity_matrix_issue_count": (
                anonymous_identity_matrix.get("issue_count", 0)
            ),
            "animation_semantic_anonymous_identity_candidate_queue_match_count": (
                anonymous_identity_matrix.get("candidate_queue_match_count", 0)
            ),
            "animation_semantic_anonymous_identity_pose_metric_match_count": (
                anonymous_identity_matrix.get("pose_metric_match_count", 0)
            ),
            "animation_semantic_anonymous_identity_valid_runtime_parity_count": (
                anonymous_identity_matrix.get("valid_runtime_parity_count", 0)
            ),
            "animation_semantic_anonymous_identity_direct_player_actor_reference_count": (
                anonymous_identity_matrix.get("direct_player_actor_reference_count", 0)
            ),
            "animation_semantic_anonymous_identity_candidate_source_reuse_count": (
                anonymous_identity_matrix.get("candidate_source_reuse_count", 0)
            ),
            "animation_semantic_anonymous_identity_promoted_source_reuse_count": (
                anonymous_identity_matrix.get("promoted_source_reuse_count", 0)
            ),
            "animation_semantic_anonymous_identity_runtime_csab_collision_present_count": (
                anonymous_identity_matrix.get("runtime_csab_collision_present_count", 0)
            ),
            "animation_semantic_anonymous_identity_candidate_csab_collision_present_count": (
                anonymous_identity_matrix.get("candidate_csab_collision_present_count", 0)
            ),
            "animation_semantic_anonymous_identity_accepted_count": (
                anonymous_identity_matrix.get("identity_accepted_count", 0)
            ),
            "animation_semantic_anonymous_identity_ownership_accepted_count": (
                anonymous_identity_matrix.get("ownership_accepted_count", 0)
            ),
            "animation_semantic_anonymous_identity_accepted_reuse_policy_count": (
                anonymous_identity_matrix.get("accepted_reuse_policy_count", 0)
            ),
            "animation_semantic_anonymous_identity_installed_dump_valid_count": (
                anonymous_identity_matrix.get("installed_runtime_dump_valid_count", 0)
            ),
            "animation_semantic_anonymous_identity_decision_status_counts": (
                anonymous_identity_decision_status_counts
            ),
            "animation_semantic_anonymous_identity_promotion_review_counts": (
                anonymous_identity_promotion_review_counts
            ),
            "animation_semantic_anonymous_identity_source_contract_counts": (
                anonymous_identity_source_contract_counts
            ),
            "animation_semantic_anonymous_n64_table_context_status": (
                anonymous_n64_table_context.get("status")
            ),
            "animation_semantic_anonymous_n64_table_context_row_count": (
                anonymous_n64_table_context.get("record_count", 0)
            ),
            "animation_semantic_anonymous_n64_table_context_issue_count": (
                anonymous_n64_table_context.get("issue_count", 0)
            ),
            "animation_semantic_anonymous_n64_table_context_xml_match_count": (
                anonymous_n64_table_context.get("xml_table_match_count", 0)
            ),
            "animation_semantic_anonymous_n64_table_context_data_order_match_count": (
                anonymous_n64_table_context.get("data_xml_order_match_count", 0)
            ),
            "animation_semantic_anonymous_n64_table_context_frame_count_match_count": (
                anonymous_n64_table_context.get("frame_count_match_count", 0)
            ),
            "animation_semantic_anonymous_n64_table_context_numeric_offset_match_count": (
                anonymous_n64_table_context.get("numeric_name_offset_match_count", 0)
            ),
            "animation_semantic_anonymous_n64_table_context_direct_symbol_reference_count": (
                anonymous_n64_table_context.get("direct_anonymous_symbol_reference_count", 0)
            ),
            "animation_semantic_anonymous_n64_table_context_candidate_owner_reference_count": (
                anonymous_n64_table_context.get("candidate_owner_symbol_reference_count", 0)
            ),
            "animation_semantic_anonymous_n64_table_context_candidate_owner_reference_present_count": (
                anonymous_n64_table_context.get("candidate_owner_reference_present_count", 0)
            ),
            "animation_semantic_anonymous_n64_table_context_identity_accepted_count": (
                anonymous_n64_table_context.get("identity_accepted_count", 0)
            ),
            "animation_semantic_anonymous_n64_table_context_ownership_accepted_count": (
                anonymous_n64_table_context.get("ownership_accepted_count", 0)
            ),
            "animation_semantic_anonymous_n64_table_context_installed_dump_valid_count": (
                anonymous_n64_table_context.get("installed_runtime_dump_valid_count", 0)
            ),
            "animation_semantic_anonymous_n64_table_context_identity_source_usage_counts": (
                anonymous_n64_table_identity_source_usage_counts
            ),
            "animation_semantic_anonymous_n64_table_context_neighbor_gap_counts": (
                anonymous_n64_table_neighbor_gap_counts
            ),
            "animation_semantic_anonymous_owner_capture_matrix_status": (
                anonymous_owner_capture_matrix.get("status")
            ),
            "animation_semantic_anonymous_owner_capture_matrix_row_count": (
                anonymous_owner_capture_matrix.get("record_count", 0)
            ),
            "animation_semantic_anonymous_owner_capture_matrix_issue_count": (
                anonymous_owner_capture_matrix.get("issue_count", 0)
            ),
            "animation_semantic_anonymous_owner_capture_direct_symbol_reference_count": (
                anonymous_owner_capture_matrix.get("direct_anonymous_symbol_reference_count", 0)
            ),
            "animation_semantic_anonymous_owner_capture_candidate_owner_reference_count": (
                anonymous_owner_capture_matrix.get("candidate_owner_symbol_reference_count", 0)
            ),
            "animation_semantic_anonymous_owner_capture_candidate_owner_reference_present_count": (
                anonymous_owner_capture_matrix.get("candidate_owner_reference_present_count", 0)
            ),
            "animation_semantic_anonymous_owner_capture_accepted_identity_count": (
                anonymous_owner_capture_matrix.get("accepted_identity_count", 0)
            ),
            "animation_semantic_anonymous_owner_capture_accepted_ownership_count": (
                anonymous_owner_capture_matrix.get("accepted_ownership_count", 0)
            ),
            "animation_semantic_anonymous_owner_capture_accepted_reuse_policy_count": (
                anonymous_owner_capture_matrix.get("accepted_reuse_policy_count", 0)
            ),
            "animation_semantic_anonymous_owner_capture_installed_dump_valid_count": (
                anonymous_owner_capture_matrix.get("installed_runtime_dump_valid_count", 0)
            ),
            "animation_semantic_anonymous_owner_capture_status_counts": (
                anonymous_owner_capture_status_counts
            ),
            "animation_semantic_anonymous_owner_capture_context_class_counts": (
                anonymous_owner_capture_context_class_counts
            ),
            "animation_semantic_anonymous_owner_runtime_config_matrix_status": (
                anonymous_owner_runtime_config_matrix.get("status")
            ),
            "animation_semantic_anonymous_owner_runtime_config_matrix_row_count": (
                anonymous_owner_runtime_config_matrix.get("record_count", 0)
            ),
            "animation_semantic_anonymous_owner_runtime_config_ready_count": (
                anonymous_owner_runtime_config_matrix.get("ready_for_capture_count", 0)
            ),
            "animation_semantic_anonymous_owner_runtime_config_dump_valid_count": (
                anonymous_owner_runtime_config_matrix.get("dump_valid_count", 0)
            ),
            "animation_semantic_anonymous_owner_runtime_config_expected_n64_filter_configured_count": (
                anonymous_owner_runtime_config_matrix.get("expected_n64_filter_configured_count", 0)
            ),
            "animation_semantic_anonymous_owner_runtime_config_expected_n64_filter_mismatch_count": (
                anonymous_owner_runtime_config_matrix.get("expected_n64_filter_mismatch_count", 0)
            ),
            "animation_semantic_anonymous_owner_runtime_config_context_trace_configured_count": (
                anonymous_owner_runtime_config_matrix.get("context_trace_configured_count", 0)
            ),
            "animation_semantic_anonymous_owner_runtime_config_context_trace_present_count": (
                anonymous_owner_runtime_config_matrix.get("context_trace_present_count", 0)
            ),
            "animation_semantic_anonymous_owner_runtime_config_context_trace_invalid_count": (
                anonymous_owner_runtime_config_matrix.get("context_trace_invalid_count", 0)
            ),
            "animation_semantic_anonymous_owner_runtime_config_issue_count": (
                anonymous_owner_runtime_config_matrix.get("issue_count", 0)
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_status": (
                anonymous_identity_owner_dependency_policy.get("status")
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_row_count": (
                anonymous_identity_owner_dependency_policy.get("record_count", 0)
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_ready_count": (
                anonymous_identity_owner_dependency_policy.get("dependency_ready_count", 0)
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_identity_matrix_ready_count": (
                anonymous_identity_owner_dependency_policy.get("identity_matrix_ready_count", 0)
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_table_context_ready_count": (
                anonymous_identity_owner_dependency_policy.get("table_context_ready_count", 0)
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_owner_capture_workorder_ready_count": (
                anonymous_identity_owner_dependency_policy.get("owner_capture_workorder_ready_count", 0)
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_package_ready_count": (
                anonymous_identity_owner_dependency_policy.get("package_ready_count", 0)
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_offline_parity_valid_count": (
                anonymous_identity_owner_dependency_policy.get("offline_runtime_parity_valid_count", 0)
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_runtime_config_ready_count": (
                anonymous_identity_owner_dependency_policy.get("runtime_config_ready_count", 0)
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_harness_valid_count": (
                anonymous_identity_owner_dependency_policy.get("runtime_harness_dump_valid_count", 0)
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_natural_capture_required_count": (
                anonymous_identity_owner_dependency_policy.get("natural_capture_required_count", 0)
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_numeric_offset_match_count": (
                anonymous_identity_owner_dependency_policy.get("numeric_name_offset_match_count", 0)
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_data_order_match_count": (
                anonymous_identity_owner_dependency_policy.get("data_xml_order_match_count", 0)
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_frame_count_match_count": (
                anonymous_identity_owner_dependency_policy.get("frame_count_match_count", 0)
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_candidate_owner_reference_present_count": (
                anonymous_identity_owner_dependency_policy.get("candidate_owner_reference_present_count", 0)
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_candidate_owner_reference_count": (
                anonymous_identity_owner_dependency_policy.get("candidate_owner_symbol_reference_count", 0)
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_direct_anonymous_reference_count": (
                anonymous_identity_owner_dependency_policy.get("direct_anonymous_symbol_reference_count", 0)
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_candidate_source_reuse_count": (
                anonymous_identity_owner_dependency_policy.get("candidate_source_reuse_count", 0)
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_promoted_source_reuse_count": (
                anonymous_identity_owner_dependency_policy.get("promoted_source_reuse_count", 0)
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_identity_accepted_count": (
                anonymous_identity_owner_dependency_policy.get("identity_accepted_count", 0)
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_ownership_accepted_count": (
                anonymous_identity_owner_dependency_policy.get("ownership_accepted_count", 0)
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_accepted_reuse_policy_count": (
                anonymous_identity_owner_dependency_policy.get("accepted_reuse_policy_count", 0)
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_semantic_accepted_count": (
                anonymous_identity_owner_dependency_policy.get("semantic_accepted_count", 0)
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_mapping_promotion_allowed_count": (
                anonymous_identity_owner_dependency_policy.get("mapping_promotion_allowed_count", 0)
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_issue_count": (
                anonymous_identity_owner_dependency_policy.get("issue_count", 0)
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_status_counts": (
                anonymous_identity_owner_dependency_policy_status_counts
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_identity_class_counts": (
                anonymous_identity_owner_dependency_policy_identity_class_counts
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_capture_status_counts": (
                anonymous_identity_owner_dependency_policy_capture_status_counts
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_context_class_counts": (
                anonymous_identity_owner_dependency_policy_context_class_counts
            ),
            "animation_semantic_anonymous_identity_owner_dependency_policy_source_contract_counts": (
                anonymous_identity_owner_dependency_policy_source_contract_counts
            ),
            "animation_semantic_ownership_reuse_runtime_config_matrix_status": (
                ownership_reuse_runtime_config_matrix.get("status")
            ),
            "animation_semantic_ownership_reuse_runtime_config_matrix_row_count": (
                ownership_reuse_runtime_config_matrix.get("record_count", 0)
            ),
            "animation_semantic_ownership_reuse_runtime_config_ready_count": (
                ownership_reuse_runtime_config_matrix.get("ready_for_capture_count", 0)
            ),
            "animation_semantic_ownership_reuse_runtime_config_dump_valid_count": (
                ownership_reuse_runtime_config_matrix.get("dump_valid_count", 0)
            ),
            "animation_semantic_ownership_reuse_runtime_config_expected_n64_filter_configured_count": (
                ownership_reuse_runtime_config_matrix.get("expected_n64_filter_configured_count", 0)
            ),
            "animation_semantic_ownership_reuse_runtime_config_expected_n64_filter_mismatch_count": (
                ownership_reuse_runtime_config_matrix.get("expected_n64_filter_mismatch_count", 0)
            ),
            "animation_semantic_ownership_reuse_runtime_config_context_trace_configured_count": (
                ownership_reuse_runtime_config_matrix.get("context_trace_configured_count", 0)
            ),
            "animation_semantic_ownership_reuse_runtime_config_context_trace_present_count": (
                ownership_reuse_runtime_config_matrix.get("context_trace_present_count", 0)
            ),
            "animation_semantic_ownership_reuse_runtime_config_context_trace_invalid_count": (
                ownership_reuse_runtime_config_matrix.get("context_trace_invalid_count", 0)
            ),
            "animation_semantic_ownership_reuse_runtime_config_issue_count": (
                ownership_reuse_runtime_config_matrix.get("issue_count", 0)
            ),
            "animation_semantic_ownership_source_risk_runtime_config_matrix_status": (
                ownership_source_risk_runtime_config_matrix.get("status")
            ),
            "animation_semantic_ownership_source_risk_runtime_config_matrix_row_count": (
                ownership_source_risk_runtime_config_matrix.get("record_count", 0)
            ),
            "animation_semantic_ownership_source_risk_runtime_config_ready_count": (
                ownership_source_risk_runtime_config_matrix.get("ready_for_capture_count", 0)
            ),
            "animation_semantic_ownership_source_risk_runtime_config_dump_valid_count": (
                ownership_source_risk_runtime_config_matrix.get("dump_valid_count", 0)
            ),
            "animation_semantic_ownership_source_risk_runtime_config_expected_n64_filter_configured_count": (
                ownership_source_risk_runtime_config_matrix.get("expected_n64_filter_configured_count", 0)
            ),
            "animation_semantic_ownership_source_risk_runtime_config_expected_n64_filter_mismatch_count": (
                ownership_source_risk_runtime_config_matrix.get("expected_n64_filter_mismatch_count", 0)
            ),
            "animation_semantic_ownership_source_risk_runtime_config_context_trace_configured_count": (
                ownership_source_risk_runtime_config_matrix.get("context_trace_configured_count", 0)
            ),
            "animation_semantic_ownership_source_risk_runtime_config_context_trace_present_count": (
                ownership_source_risk_runtime_config_matrix.get("context_trace_present_count", 0)
            ),
            "animation_semantic_ownership_source_risk_runtime_config_context_trace_invalid_count": (
                ownership_source_risk_runtime_config_matrix.get("context_trace_invalid_count", 0)
            ),
            "animation_semantic_ownership_source_risk_runtime_config_issue_count": (
                ownership_source_risk_runtime_config_matrix.get("issue_count", 0)
            ),
            "animation_semantic_frontier_closure_matrix_status": (
                semantic_frontier_closure_matrix.get("status")
            ),
            "animation_semantic_frontier_closure_matrix_row_count": (
                semantic_frontier_closure_matrix.get("record_count", 0)
            ),
            "animation_semantic_frontier_closure_matrix_issue_count": (
                semantic_frontier_closure_matrix.get("issue_count", 0)
            ),
            "animation_semantic_frontier_closure_covered_workorder_count": (
                semantic_frontier_closure_matrix.get("covered_workorder_count", 0)
            ),
            "animation_semantic_frontier_closure_ready_workorder_count": (
                semantic_frontier_closure_matrix.get("ready_workorder_count", 0)
            ),
            "animation_semantic_frontier_closure_missing_workorder_count": (
                semantic_frontier_closure_matrix.get("missing_workorder_count", 0)
            ),
            "animation_semantic_frontier_closure_semantic_accepted_count": (
                semantic_frontier_closure_matrix.get("semantic_accepted_count", 0)
            ),
            "animation_semantic_frontier_closure_installed_dump_valid_count": (
                semantic_frontier_closure_matrix.get("installed_runtime_dump_valid_count", 0)
            ),
            "animation_semantic_frontier_closure_runtime_harness_dump_valid_count": (
                semantic_frontier_closure_matrix.get("runtime_harness_dump_valid_count", 0)
            ),
            "animation_semantic_frontier_closure_runtime_harness_geometry_gap_count": (
                semantic_frontier_closure_matrix.get("runtime_harness_geometry_gap_count", 0)
            ),
            "animation_semantic_frontier_closure_post_harness_semantic_blocked_count": (
                semantic_frontier_closure_matrix.get("post_harness_semantic_blocked_count", 0)
            ),
            "animation_semantic_frontier_closure_mapping_promotion_allowed_count": (
                semantic_frontier_closure_matrix.get("mapping_promotion_allowed_count", 0)
            ),
            "animation_semantic_frontier_closure_workorder_kind_counts": (
                semantic_frontier_closure_workorder_kind_counts
            ),
            "animation_semantic_frontier_closure_frontier_gate_counts": (
                semantic_frontier_closure_frontier_gate_counts
            ),
            "animation_semantic_frontier_closure_coverage_status_counts": (
                semantic_frontier_closure_coverage_status_counts
            ),
            "animation_semantic_frontier_closure_harness_evidence_status_counts": (
                semantic_frontier_closure_harness_evidence_status_counts
            ),
            "animation_semantic_frontier_closure_post_harness_blocker_counts": (
                semantic_frontier_closure_post_harness_blocker_counts
            ),
            "animation_semantic_promoted_owner_alias_policy_status": (
                promoted_owner_alias_policy.get("status")
            ),
            "animation_semantic_promoted_owner_alias_policy_row_count": (
                promoted_owner_alias_policy.get("record_count", 0)
            ),
            "animation_semantic_promoted_owner_alias_policy_ready_count": (
                promoted_owner_alias_policy.get("policy_ready_count", 0)
            ),
            "animation_semantic_promoted_owner_alias_policy_unique_owner_count": (
                promoted_owner_alias_policy.get("unique_promoted_owner_count", 0)
            ),
            "animation_semantic_promoted_owner_alias_policy_harness_valid_count": (
                promoted_owner_alias_policy.get("runtime_harness_dump_valid_count", 0)
            ),
            "animation_semantic_promoted_owner_alias_policy_natural_capture_required_count": (
                promoted_owner_alias_policy.get("natural_capture_required_count", 0)
            ),
            "animation_semantic_promoted_owner_alias_policy_semantic_accepted_count": (
                promoted_owner_alias_policy.get("semantic_accepted_count", 0)
            ),
            "animation_semantic_promoted_owner_alias_policy_mapping_promotion_allowed_count": (
                promoted_owner_alias_policy.get("mapping_promotion_allowed_count", 0)
            ),
            "animation_semantic_promoted_owner_alias_policy_issue_count": (
                promoted_owner_alias_policy.get("issue_count", 0)
            ),
            "animation_semantic_promoted_owner_alias_policy_status_counts": (
                promoted_owner_alias_policy_status_counts
            ),
            "animation_semantic_route_proven_ownership_policy_status": (
                route_proven_ownership_policy.get("status")
            ),
            "animation_semantic_route_proven_ownership_policy_row_count": (
                route_proven_ownership_policy.get("record_count", 0)
            ),
            "animation_semantic_route_proven_ownership_policy_ready_count": (
                route_proven_ownership_policy.get("policy_ready_count", 0)
            ),
            "animation_semantic_route_proven_ownership_policy_callsite_ready_count": (
                route_proven_ownership_policy.get("route_callsite_ready_count", 0)
            ),
            "animation_semantic_route_proven_ownership_policy_package_ready_count": (
                route_proven_ownership_policy.get("route_package_ready_count", 0)
            ),
            "animation_semantic_route_proven_ownership_policy_offline_parity_valid_count": (
                route_proven_ownership_policy.get("offline_runtime_parity_valid_count", 0)
            ),
            "animation_semantic_route_proven_ownership_policy_harness_valid_count": (
                route_proven_ownership_policy.get("runtime_harness_dump_valid_count", 0)
            ),
            "animation_semantic_route_proven_ownership_policy_natural_capture_required_count": (
                route_proven_ownership_policy.get("natural_capture_required_count", 0)
            ),
            "animation_semantic_route_proven_ownership_policy_direct_reference_count": (
                route_proven_ownership_policy.get("direct_player_actor_source_reference_count", 0)
            ),
            "animation_semantic_route_proven_ownership_policy_semantic_accepted_count": (
                route_proven_ownership_policy.get("semantic_accepted_count", 0)
            ),
            "animation_semantic_route_proven_ownership_policy_mapping_promotion_allowed_count": (
                route_proven_ownership_policy.get("mapping_promotion_allowed_count", 0)
            ),
            "animation_semantic_route_proven_ownership_policy_issue_count": (
                route_proven_ownership_policy.get("issue_count", 0)
            ),
            "animation_semantic_route_proven_ownership_policy_status_counts": (
                route_proven_ownership_policy_status_counts
            ),
            "animation_semantic_route_absent_ownership_reuse_policy_status": (
                route_absent_ownership_reuse_policy.get("status")
            ),
            "animation_semantic_route_absent_ownership_reuse_policy_row_count": (
                route_absent_ownership_reuse_policy.get("record_count", 0)
            ),
            "animation_semantic_route_absent_ownership_reuse_policy_ready_count": (
                route_absent_ownership_reuse_policy.get("policy_ready_count", 0)
            ),
            "animation_semantic_route_absent_ownership_reuse_policy_source_group_row_count": (
                route_absent_ownership_reuse_policy.get("source_group_policy_row_count", 0)
            ),
            "animation_semantic_route_absent_ownership_reuse_policy_single_candidate_row_count": (
                route_absent_ownership_reuse_policy.get(
                    "single_candidate_policy_row_count", 0
                )
            ),
            "animation_semantic_route_absent_ownership_reuse_policy_source_group_ready_count": (
                route_absent_ownership_reuse_policy.get(
                    "source_group_policy_ready_count", 0
                )
            ),
            "animation_semantic_route_absent_ownership_reuse_policy_single_candidate_ready_count": (
                route_absent_ownership_reuse_policy.get(
                    "single_candidate_policy_ready_count", 0
                )
            ),
            "animation_semantic_route_absent_ownership_reuse_policy_unique_source_group_count": (
                route_absent_ownership_reuse_policy.get("unique_source_group_count", 0)
            ),
            "animation_semantic_route_absent_ownership_reuse_policy_unique_single_candidate_source_count": (
                route_absent_ownership_reuse_policy.get(
                    "unique_single_candidate_source_count", 0
                )
            ),
            "animation_semantic_route_absent_ownership_reuse_policy_ownership_decision_ready_count": (
                route_absent_ownership_reuse_policy.get(
                    "ownership_decision_ready_count", 0
                )
            ),
            "animation_semantic_route_absent_ownership_reuse_policy_arbitration_ready_count": (
                route_absent_ownership_reuse_policy.get("arbitration_ready_count", 0)
            ),
            "animation_semantic_route_absent_ownership_reuse_policy_offline_parity_valid_count": (
                route_absent_ownership_reuse_policy.get(
                    "offline_runtime_parity_valid_count", 0
                )
            ),
            "animation_semantic_route_absent_ownership_reuse_policy_runtime_config_ready_count": (
                route_absent_ownership_reuse_policy.get("runtime_config_ready_count", 0)
            ),
            "animation_semantic_route_absent_ownership_reuse_policy_harness_valid_count": (
                route_absent_ownership_reuse_policy.get(
                    "runtime_harness_dump_valid_count", 0
                )
            ),
            "animation_semantic_route_absent_ownership_reuse_policy_natural_capture_required_count": (
                route_absent_ownership_reuse_policy.get(
                    "natural_capture_required_count", 0
                )
            ),
            "animation_semantic_route_absent_ownership_reuse_policy_direct_reference_count": (
                route_absent_ownership_reuse_policy.get(
                    "direct_player_actor_source_reference_count", 0
                )
            ),
            "animation_semantic_route_absent_ownership_reuse_policy_candidate_owner_reference_count": (
                route_absent_ownership_reuse_policy.get(
                    "candidate_or_runtime_owner_reference_count", 0
                )
            ),
            "animation_semantic_route_absent_ownership_reuse_policy_semantic_accepted_count": (
                route_absent_ownership_reuse_policy.get("semantic_accepted_count", 0)
            ),
            "animation_semantic_route_absent_ownership_reuse_policy_mapping_promotion_allowed_count": (
                route_absent_ownership_reuse_policy.get(
                    "mapping_promotion_allowed_count", 0
                )
            ),
            "animation_semantic_route_absent_ownership_reuse_policy_issue_count": (
                route_absent_ownership_reuse_policy.get("issue_count", 0)
            ),
            "animation_semantic_route_absent_ownership_reuse_policy_status_counts": (
                route_absent_ownership_reuse_policy_status_counts
            ),
            "animation_semantic_route_absent_ownership_reuse_policy_source_group_class_counts": (
                route_absent_ownership_reuse_policy_source_group_class_counts
            ),
            "animation_semantic_ownership_source_risk_dependency_policy_status": (
                ownership_source_risk_dependency_policy.get("status")
            ),
            "animation_semantic_ownership_source_risk_dependency_policy_row_count": (
                ownership_source_risk_dependency_policy.get("record_count", 0)
            ),
            "animation_semantic_ownership_source_risk_dependency_policy_ready_count": (
                ownership_source_risk_dependency_policy.get("dependency_ready_count", 0)
            ),
            "animation_semantic_ownership_source_risk_dependency_policy_ownership_blocked_count": (
                ownership_source_risk_dependency_policy.get("ownership_blocked_count", 0)
            ),
            "animation_semantic_ownership_source_risk_dependency_policy_outside_envelope_count": (
                ownership_source_risk_dependency_policy.get(
                    "outside_reference_envelope_count", 0
                )
            ),
            "animation_semantic_ownership_source_risk_dependency_policy_package_ready_count": (
                ownership_source_risk_dependency_policy.get("package_ready_count", 0)
            ),
            "animation_semantic_ownership_source_risk_dependency_policy_offline_parity_valid_count": (
                ownership_source_risk_dependency_policy.get(
                    "offline_runtime_parity_valid_count", 0
                )
            ),
            "animation_semantic_ownership_source_risk_dependency_policy_runtime_config_ready_count": (
                ownership_source_risk_dependency_policy.get(
                    "runtime_config_ready_count", 0
                )
            ),
            "animation_semantic_ownership_source_risk_dependency_policy_harness_valid_count": (
                ownership_source_risk_dependency_policy.get(
                    "runtime_harness_dump_valid_count", 0
                )
            ),
            "animation_semantic_ownership_source_risk_dependency_policy_natural_capture_required_count": (
                ownership_source_risk_dependency_policy.get(
                    "natural_capture_required_count", 0
                )
            ),
            "animation_semantic_ownership_source_risk_dependency_policy_direct_reference_count": (
                ownership_source_risk_dependency_policy.get(
                    "direct_player_actor_source_reference_count", 0
                )
            ),
            "animation_semantic_ownership_source_risk_dependency_policy_ownership_accepted_count": (
                ownership_source_risk_dependency_policy.get("ownership_accepted_count", 0)
            ),
            "animation_semantic_ownership_source_risk_dependency_policy_semantic_accepted_count": (
                ownership_source_risk_dependency_policy.get("semantic_accepted_count", 0)
            ),
            "animation_semantic_ownership_source_risk_dependency_policy_mapping_promotion_allowed_count": (
                ownership_source_risk_dependency_policy.get(
                    "mapping_promotion_allowed_count", 0
                )
            ),
            "animation_semantic_ownership_source_risk_dependency_policy_issue_count": (
                ownership_source_risk_dependency_policy.get("issue_count", 0)
            ),
            "animation_semantic_ownership_source_risk_dependency_policy_status_counts": (
                ownership_source_risk_dependency_policy_status_counts
            ),
            "animation_semantic_runtime_capture_execution_plan_status": (
                runtime_capture_execution_plan.get("status")
            ),
            "animation_semantic_runtime_capture_execution_plan_row_count": (
                runtime_capture_execution_plan.get("record_count", 0)
            ),
            "animation_semantic_runtime_capture_execution_plan_issue_count": (
                runtime_capture_execution_plan.get("issue_count", 0)
            ),
            "animation_semantic_runtime_capture_execution_plan_target_install_ready": (
                runtime_capture_execution_plan_target_install.get("ready", False)
            ),
            "animation_semantic_runtime_capture_execution_plan_existing_runner_ready_count": (
                runtime_capture_execution_plan.get("ready_existing_runner_count", 0)
            ),
            "animation_semantic_runtime_capture_execution_plan_context_runner_required_count": (
                runtime_capture_execution_plan.get("context_runner_required_count", 0)
            ),
            "animation_semantic_runtime_capture_execution_plan_policy_review_required_count": (
                runtime_capture_execution_plan.get("policy_review_required_count", 0)
            ),
            "animation_semantic_runtime_capture_execution_plan_pose_source_dependency_count": (
                runtime_capture_execution_plan.get("pose_source_dependency_count", 0)
            ),
            "animation_semantic_runtime_capture_execution_plan_installed_dump_present_count": (
                runtime_capture_execution_plan.get("installed_dump_present_count", 0)
            ),
            "animation_semantic_runtime_capture_execution_plan_installed_dump_valid_count": (
                runtime_capture_execution_plan.get("installed_runtime_dump_valid_count", 0)
            ),
            "animation_semantic_runtime_capture_execution_plan_semantic_accepted_count": (
                runtime_capture_execution_plan.get("semantic_accepted_count", 0)
            ),
            "animation_semantic_runtime_capture_execution_plan_mapping_promotion_allowed_count": (
                runtime_capture_execution_plan.get("mapping_promotion_allowed_count", 0)
            ),
            "animation_semantic_runtime_capture_execution_plan_ownership_reuse_config_ready_count": (
                runtime_capture_execution_plan.get("ownership_reuse_passive_runtime_config_ready_count", 0)
            ),
            "animation_semantic_runtime_capture_execution_plan_ownership_reuse_config_issue_count": (
                runtime_capture_execution_plan.get("ownership_reuse_passive_runtime_config_issue_count", 0)
            ),
            "animation_semantic_runtime_capture_execution_plan_ownership_reuse_config_expected_n64_filter_configured_count": (
                runtime_capture_execution_plan.get(
                    "ownership_reuse_passive_runtime_config_expected_n64_filter_configured_count", 0
                )
            ),
            "animation_semantic_runtime_capture_execution_plan_ownership_reuse_config_context_trace_configured_count": (
                runtime_capture_execution_plan.get(
                    "ownership_reuse_passive_runtime_config_context_trace_configured_count", 0
                )
            ),
            "animation_semantic_runtime_capture_execution_plan_ownership_reuse_config_dump_valid_count": (
                runtime_capture_execution_plan.get("ownership_reuse_passive_runtime_config_dump_valid_count", 0)
            ),
            "animation_semantic_runtime_capture_execution_plan_ownership_source_risk_config_ready_count": (
                runtime_capture_execution_plan.get("ownership_source_risk_passive_runtime_config_ready_count", 0)
            ),
            "animation_semantic_runtime_capture_execution_plan_ownership_source_risk_config_issue_count": (
                runtime_capture_execution_plan.get("ownership_source_risk_passive_runtime_config_issue_count", 0)
            ),
            "animation_semantic_runtime_capture_execution_plan_ownership_source_risk_config_expected_n64_filter_configured_count": (
                runtime_capture_execution_plan.get(
                    "ownership_source_risk_passive_runtime_config_expected_n64_filter_configured_count", 0
                )
            ),
            "animation_semantic_runtime_capture_execution_plan_ownership_source_risk_config_context_trace_configured_count": (
                runtime_capture_execution_plan.get(
                    "ownership_source_risk_passive_runtime_config_context_trace_configured_count", 0
                )
            ),
            "animation_semantic_runtime_capture_execution_plan_ownership_source_risk_config_dump_valid_count": (
                runtime_capture_execution_plan.get("ownership_source_risk_passive_runtime_config_dump_valid_count", 0)
            ),
            "animation_semantic_runtime_capture_execution_plan_phase_counts": (
                runtime_capture_execution_plan_phase_counts
            ),
            "animation_semantic_runtime_capture_execution_plan_runner_status_counts": (
                runtime_capture_execution_plan_runner_status_counts
            ),
            "animation_semantic_runtime_capture_execution_plan_execution_status_counts": (
                runtime_capture_execution_plan_execution_status_counts
            ),
            "animation_semantic_residual_closure_kit_status": (
                residual_closure_kit.get("status")
            ),
            "animation_semantic_residual_closure_kit_row_count": (
                residual_closure_kit.get("record_count", 0)
            ),
            "animation_semantic_residual_closure_kit_issue_count": (
                residual_closure_kit.get("issue_count", 0)
            ),
            "animation_semantic_residual_closure_kit_capture_config_ready_count": (
                residual_closure_kit.get("capture_config_ready_count", 0)
            ),
            "animation_semantic_residual_closure_kit_context_capture_passive_config_ready_count": (
                residual_closure_kit.get("context_capture_passive_config_ready_count", 0)
            ),
            "animation_semantic_residual_closure_kit_route_proven_existing_capture_config_ready_count": (
                residual_closure_kit.get("route_proven_existing_capture_config_ready_count", 0)
            ),
            "animation_semantic_residual_closure_kit_ownership_reuse_policy_config_ready_count": (
                residual_closure_kit.get("ownership_reuse_policy_config_ready_count", 0)
            ),
            "animation_semantic_residual_closure_kit_pose_source_dependency_config_ready_count": (
                residual_closure_kit.get("pose_source_dependency_config_ready_count", 0)
            ),
            "animation_semantic_residual_closure_kit_ownership_policy_queue_count": (
                residual_closure_kit.get("ownership_policy_queue_count", 0)
            ),
            "animation_semantic_residual_closure_kit_pose_source_dependency_count": (
                residual_closure_kit.get("pose_source_dependency_count", 0)
            ),
            "animation_semantic_residual_closure_kit_installed_dump_valid_count": (
                residual_closure_kit.get("installed_runtime_dump_valid_count", 0)
            ),
            "animation_semantic_residual_closure_kit_semantic_accepted_count": (
                residual_closure_kit.get("semantic_accepted_count", 0)
            ),
            "animation_semantic_residual_closure_kit_mapping_promotion_allowed_count": (
                residual_closure_kit.get("mapping_promotion_allowed_count", 0)
            ),
            "animation_semantic_residual_closure_kit_lane_counts": (
                residual_closure_kit_lane_counts
            ),
            "animation_semantic_residual_closure_kit_config_status_counts": (
                residual_closure_kit_config_status_counts
            ),
            "animation_semantic_pose_source_risk_diagnostic_package_status": (
                pose_source_risk_diagnostic_package.get("status")
            ),
            "animation_semantic_pose_source_risk_diagnostic_package_exported_count": (
                pose_source_risk_diagnostic_package.get("exported", 0)
            ),
            "animation_semantic_pose_source_risk_diagnostic_package_failed_count": (
                pose_source_risk_diagnostic_package.get("failed", 0)
            ),
            "animation_semantic_pose_source_risk_diagnostic_package_issue_count": (
                pose_source_risk_diagnostic_package_issue_counts.get("total", 0)
            ),
            "animation_semantic_pose_source_risk_diagnostic_package_track_counts": (
                pose_source_risk_diagnostic_package_counts
            ),
            "animation_semantic_pose_source_risk_diagnostic_runtime_parity_status": (
                pose_source_risk_diagnostic_runtime_parity.get("status")
            ),
            "animation_semantic_pose_source_risk_diagnostic_runtime_acceptance_status": (
                pose_source_risk_diagnostic_runtime_parity.get("runtime_acceptance_status")
            ),
            "animation_semantic_pose_source_risk_diagnostic_runtime_parity_exported_count": (
                pose_source_risk_diagnostic_runtime_parity.get("exported", 0)
            ),
            "animation_semantic_pose_source_risk_diagnostic_runtime_parity_failed_count": (
                pose_source_risk_diagnostic_runtime_parity.get("failed", 0)
            ),
            "animation_semantic_pose_source_risk_diagnostic_runtime_parity_counts": (
                pose_source_risk_diagnostic_runtime_parity_counts
            ),
            "animation_semantic_pose_source_callsite_context_status": (
                pose_source_callsite_context.get("status")
            ),
            "animation_semantic_pose_source_callsite_context_row_count": (
                pose_source_callsite_context.get("row_count", 0)
            ),
            "animation_semantic_pose_source_callsite_context_direct_player_actor_reference_count": (
                pose_source_callsite_context.get("direct_player_actor_source_reference_count", 0)
            ),
            "animation_semantic_pose_source_callsite_context_semantic_resolution_change_count": (
                pose_source_callsite_context.get("semantic_resolution_change_count", 0)
            ),
            "animation_semantic_pose_source_callsite_context_class_counts": (
                pose_source_callsite_context_class_counts
            ),
            "animation_semantic_pose_source_callsite_context_next_evidence_counts": (
                pose_source_callsite_next_evidence_counts
            ),
            "animation_semantic_pose_source_runtime_capture_matrix_status": (
                pose_source_runtime_capture_matrix.get("status")
            ),
            "animation_semantic_pose_source_runtime_capture_matrix_row_count": (
                pose_source_runtime_capture_matrix.get("record_count", 0)
            ),
            "animation_semantic_pose_source_runtime_capture_matrix_issue_count": (
                pose_source_runtime_capture_matrix.get("issue_count", 0)
            ),
            "animation_semantic_pose_source_runtime_capture_matrix_valid_runtime_parity_count": (
                pose_source_runtime_capture_matrix.get("runtime_parity_record_count", 0)
            ),
            "animation_semantic_pose_source_runtime_capture_context_runner_implemented_count": (
                pose_source_runtime_capture_matrix.get("context_runner_implemented_count", 0)
            ),
            "animation_semantic_pose_source_runtime_capture_installed_dump_valid_count": (
                pose_source_runtime_capture_matrix.get("installed_runtime_dump_valid_count", 0)
            ),
            "animation_semantic_pose_source_runtime_capture_status_counts": (
                pose_source_runtime_capture_status_counts
            ),
            "animation_semantic_pose_source_runtime_capture_scope_counts": (
                pose_source_runtime_capture_scope_counts
            ),
            "animation_semantic_pose_source_runtime_config_matrix_status": (
                pose_source_runtime_capture_config_matrix.get("status")
            ),
            "animation_semantic_pose_source_runtime_config_matrix_row_count": (
                pose_source_runtime_capture_config_matrix.get("record_count", 0)
            ),
            "animation_semantic_pose_source_runtime_config_ready_count": (
                pose_source_runtime_capture_config_matrix.get("ready_for_capture_count", 0)
            ),
            "animation_semantic_pose_source_runtime_config_dump_valid_count": (
                pose_source_runtime_capture_config_matrix.get("dump_valid_count", 0)
            ),
            "animation_semantic_pose_source_runtime_config_expected_n64_filter_configured_count": (
                pose_source_runtime_capture_config_matrix.get("expected_n64_filter_configured_count", 0)
            ),
            "animation_semantic_pose_source_runtime_config_expected_n64_filter_mismatch_count": (
                pose_source_runtime_capture_config_matrix.get("expected_n64_filter_mismatch_count", 0)
            ),
            "animation_semantic_pose_source_runtime_config_context_trace_configured_count": (
                pose_source_runtime_capture_config_matrix.get("context_trace_configured_count", 0)
            ),
            "animation_semantic_pose_source_runtime_config_context_trace_present_count": (
                pose_source_runtime_capture_config_matrix.get("context_trace_present_count", 0)
            ),
            "animation_semantic_pose_source_runtime_config_context_trace_invalid_count": (
                pose_source_runtime_capture_config_matrix.get("context_trace_invalid_count", 0)
            ),
            "animation_semantic_pose_source_runtime_config_issue_count": (
                pose_source_runtime_capture_config_matrix.get("issue_count", 0)
            ),
            "animation_semantic_pose_source_direct_callsite_dependency_policy_status": (
                pose_source_direct_callsite_dependency_policy.get("status")
            ),
            "animation_semantic_pose_source_direct_callsite_dependency_policy_row_count": (
                pose_source_direct_callsite_dependency_policy.get("record_count", 0)
            ),
            "animation_semantic_pose_source_direct_callsite_dependency_policy_ready_count": (
                pose_source_direct_callsite_dependency_policy.get("dependency_ready_count", 0)
            ),
            "animation_semantic_pose_source_direct_callsite_dependency_policy_direct_callsite_ready_count": (
                pose_source_direct_callsite_dependency_policy.get("direct_callsite_ready_count", 0)
            ),
            "animation_semantic_pose_source_direct_callsite_dependency_policy_workorder_ready_count": (
                pose_source_direct_callsite_dependency_policy.get("workorder_ready_count", 0)
            ),
            "animation_semantic_pose_source_direct_callsite_dependency_policy_package_ready_count": (
                pose_source_direct_callsite_dependency_policy.get("package_ready_count", 0)
            ),
            "animation_semantic_pose_source_direct_callsite_dependency_policy_offline_parity_valid_count": (
                pose_source_direct_callsite_dependency_policy.get("offline_runtime_parity_valid_count", 0)
            ),
            "animation_semantic_pose_source_direct_callsite_dependency_policy_runtime_config_ready_count": (
                pose_source_direct_callsite_dependency_policy.get("runtime_config_ready_count", 0)
            ),
            "animation_semantic_pose_source_direct_callsite_dependency_policy_harness_valid_count": (
                pose_source_direct_callsite_dependency_policy.get("runtime_harness_dump_valid_count", 0)
            ),
            "animation_semantic_pose_source_direct_callsite_dependency_policy_natural_capture_required_count": (
                pose_source_direct_callsite_dependency_policy.get("natural_capture_required_count", 0)
            ),
            "animation_semantic_pose_source_direct_callsite_dependency_policy_outside_envelope_count": (
                pose_source_direct_callsite_dependency_policy.get("outside_reference_envelope_count", 0)
            ),
            "animation_semantic_pose_source_direct_callsite_dependency_policy_direct_source_reference_count": (
                pose_source_direct_callsite_dependency_policy.get("direct_source_reference_count", 0)
            ),
            "animation_semantic_pose_source_direct_callsite_dependency_policy_direct_player_actor_reference_count": (
                pose_source_direct_callsite_dependency_policy.get(
                    "direct_player_actor_source_reference_count", 0
                )
            ),
            "animation_semantic_pose_source_direct_callsite_dependency_policy_runtime_context_trigger_count": (
                pose_source_direct_callsite_dependency_policy.get("runtime_context_trigger_count", 0)
            ),
            "animation_semantic_pose_source_direct_callsite_dependency_policy_source_identity_accepted_count": (
                pose_source_direct_callsite_dependency_policy.get("source_identity_accepted_count", 0)
            ),
            "animation_semantic_pose_source_direct_callsite_dependency_policy_semantic_accepted_count": (
                pose_source_direct_callsite_dependency_policy.get("semantic_accepted_count", 0)
            ),
            "animation_semantic_pose_source_direct_callsite_dependency_policy_mapping_promotion_allowed_count": (
                pose_source_direct_callsite_dependency_policy.get("mapping_promotion_allowed_count", 0)
            ),
            "animation_semantic_pose_source_direct_callsite_dependency_policy_issue_count": (
                pose_source_direct_callsite_dependency_policy.get("issue_count", 0)
            ),
            "animation_semantic_pose_source_direct_callsite_dependency_policy_status_counts": (
                pose_source_direct_callsite_dependency_status_counts
            ),
            "animation_semantic_pose_source_direct_callsite_dependency_policy_class_counts": (
                pose_source_direct_callsite_dependency_class_counts
            ),
            "animation_semantic_pose_source_direct_callsite_dependency_policy_next_evidence_counts": (
                pose_source_direct_callsite_dependency_next_evidence_counts
            ),
            "animation_semantic_pose_source_direct_callsite_dependency_policy_capture_status_counts": (
                pose_source_direct_callsite_dependency_capture_status_counts
            ),
            "animation_semantic_pose_source_direct_callsite_dependency_policy_forced_status_counts": (
                pose_source_direct_callsite_dependency_forced_status_counts
            ),
            "animation_semantic_direct_callsite_source_identity_review_status": (
                direct_callsite_source_identity_review.get("status")
            ),
            "animation_semantic_direct_callsite_source_identity_review_row_count": (
                direct_callsite_source_identity_review.get("record_count", 0)
            ),
            "animation_semantic_direct_callsite_source_identity_review_installed_dynamic_candidate_count": (
                direct_callsite_source_identity_review.get(
                    "installed_dynamic_review_candidate_count", 0
                )
            ),
            "animation_semantic_direct_callsite_source_identity_review_runtime_trace_valid_count": (
                direct_callsite_source_identity_review.get("runtime_trace_valid_count", 0)
            ),
            "animation_semantic_direct_callsite_source_identity_review_runtime_variant_ready_count": (
                direct_callsite_source_identity_review.get(
                    "runtime_variant_context_ready_count", 0
                )
            ),
            "animation_semantic_direct_callsite_source_identity_review_source_alias_accepted_count": (
                direct_callsite_source_identity_review.get(
                    "source_alias_evidence_accepted_count", 0
                )
            ),
            "animation_semantic_direct_callsite_source_identity_review_source_identity_accepted_count": (
                direct_callsite_source_identity_review.get("source_identity_accepted_count", 0)
            ),
            "animation_semantic_direct_callsite_source_identity_review_semantic_accepted_count": (
                direct_callsite_source_identity_review.get("semantic_accepted_count", 0)
            ),
            "animation_semantic_direct_callsite_source_identity_review_mapping_promotion_allowed_count": (
                direct_callsite_source_identity_review.get("mapping_promotion_allowed_count", 0)
            ),
            "animation_semantic_direct_callsite_source_identity_review_issue_count": (
                direct_callsite_source_identity_review.get("issue_count", 0)
            ),
            "animation_semantic_direct_callsite_source_identity_review_status_counts": (
                direct_callsite_source_identity_review_status_counts
            ),
            "animation_semantic_direct_callsite_source_identity_review_runtime_variant_counts": (
                direct_callsite_source_identity_runtime_variant_counts
            ),
            "animation_semantic_route_runtime_capture_matrix_status": (
                route_runtime_capture_matrix.get("status")
            ),
            "animation_semantic_route_runtime_capture_matrix_row_count": (
                route_runtime_capture_matrix.get("record_count", 0)
            ),
            "animation_semantic_route_runtime_capture_matrix_issue_count": (
                route_runtime_capture_matrix.get("issue_count", 0)
            ),
            "animation_semantic_route_runtime_capture_exact_direct_player_actor_reference_count": (
                route_runtime_capture_matrix.get("exact_direct_player_actor_reference_count", 0)
            ),
            "animation_semantic_route_runtime_capture_family_route_present_count": (
                route_runtime_capture_matrix.get("family_route_present_count", 0)
            ),
            "animation_semantic_route_runtime_capture_accepted_route_count": (
                route_runtime_capture_matrix.get("accepted_route_count", 0)
            ),
            "animation_semantic_route_runtime_capture_installed_dump_valid_count": (
                route_runtime_capture_matrix.get("installed_runtime_dump_valid_count", 0)
            ),
            "animation_semantic_route_runtime_capture_context_class_counts": (
                route_runtime_capture_context_class_counts
            ),
            "animation_semantic_route_runtime_capture_status_counts": (
                route_runtime_capture_status_counts
            ),
            "animation_semantic_route_runtime_diagnostic_package_status": (
                route_runtime_diagnostic_package.get("status")
            ),
            "animation_semantic_route_runtime_diagnostic_package_exported_count": (
                route_runtime_diagnostic_package.get("exported", 0)
            ),
            "animation_semantic_route_runtime_diagnostic_package_failed_count": (
                route_runtime_diagnostic_package.get("failed", 0)
            ),
            "animation_semantic_route_runtime_diagnostic_package_issue_count": (
                route_runtime_diagnostic_package_issue_counts.get("total", 0)
            ),
            "animation_semantic_route_runtime_diagnostic_package_track_counts": (
                route_runtime_diagnostic_package_counts
            ),
            "animation_semantic_route_runtime_diagnostic_runtime_parity_status": (
                route_runtime_diagnostic_runtime_parity.get("status")
            ),
            "animation_semantic_route_runtime_diagnostic_runtime_acceptance_status": (
                route_runtime_diagnostic_runtime_parity.get("runtime_acceptance_status")
            ),
            "animation_semantic_route_runtime_diagnostic_runtime_parity_exported_count": (
                route_runtime_diagnostic_runtime_parity.get("exported", 0)
            ),
            "animation_semantic_route_runtime_diagnostic_runtime_parity_failed_count": (
                route_runtime_diagnostic_runtime_parity.get("failed", 0)
            ),
            "animation_semantic_route_runtime_diagnostic_runtime_parity_counts": (
                route_runtime_diagnostic_runtime_parity_counts
            ),
            "animation_semantic_route_runtime_config_matrix_status": (
                route_runtime_capture_config_matrix.get("status")
            ),
            "animation_semantic_route_runtime_config_matrix_row_count": (
                route_runtime_capture_config_matrix.get("record_count", 0)
            ),
            "animation_semantic_route_runtime_config_ready_count": (
                route_runtime_capture_config_matrix.get("ready_for_capture_count", 0)
            ),
            "animation_semantic_route_runtime_config_dump_valid_count": (
                route_runtime_capture_config_matrix.get("dump_valid_count", 0)
            ),
            "animation_semantic_route_runtime_config_expected_n64_filter_configured_count": (
                route_runtime_capture_config_matrix.get("expected_n64_filter_configured_count", 0)
            ),
            "animation_semantic_route_runtime_config_expected_n64_filter_mismatch_count": (
                route_runtime_capture_config_matrix.get("expected_n64_filter_mismatch_count", 0)
            ),
            "animation_semantic_route_runtime_config_context_trace_configured_count": (
                route_runtime_capture_config_matrix.get("context_trace_configured_count", 0)
            ),
            "animation_semantic_route_runtime_config_context_trace_present_count": (
                route_runtime_capture_config_matrix.get("context_trace_present_count", 0)
            ),
            "animation_semantic_route_runtime_config_context_trace_invalid_count": (
                route_runtime_capture_config_matrix.get("context_trace_invalid_count", 0)
            ),
            "animation_semantic_route_runtime_config_issue_count": (
                route_runtime_capture_config_matrix.get("issue_count", 0)
            ),
            "animation_semantic_route_runtime_n64_usage_context_status": (
                route_runtime_n64_usage_context.get("status")
            ),
            "animation_semantic_route_runtime_n64_usage_context_row_count": (
                route_runtime_n64_usage_context.get("record_count", 0)
            ),
            "animation_semantic_route_runtime_n64_usage_context_issue_count": (
                route_runtime_n64_usage_context.get("issue_count", 0)
            ),
            "animation_semantic_route_runtime_n64_usage_context_exact_symbol_count": (
                route_runtime_n64_usage_context.get("exact_symbol_context_count", 0)
            ),
            "animation_semantic_route_runtime_n64_usage_context_family_or_sibling_count": (
                route_runtime_n64_usage_context.get("family_or_sibling_context_count", 0)
            ),
            "animation_semantic_route_runtime_n64_usage_context_runtime_config_linked_count": (
                route_runtime_n64_usage_context.get("runtime_config_linked_count", 0)
            ),
            "animation_semantic_route_runtime_n64_usage_context_expected_n64_filter_linked_count": (
                route_runtime_n64_usage_context.get("expected_n64_filter_linked_count", 0)
            ),
            "animation_semantic_route_runtime_n64_usage_context_context_trace_linked_count": (
                route_runtime_n64_usage_context.get("context_trace_linked_count", 0)
            ),
            "animation_semantic_route_runtime_n64_usage_context_source_window_count": (
                route_runtime_n64_usage_context.get("source_window_count", 0)
            ),
            "animation_semantic_route_runtime_n64_usage_context_trace_required_field_count": (
                route_runtime_n64_usage_context.get("runtime_trace_required_field_count", 0)
            ),
            "animation_semantic_route_runtime_n64_usage_context_trace_required_field_present_count": (
                route_runtime_n64_usage_context.get("runtime_trace_required_field_present_count", 0)
            ),
            "animation_semantic_route_runtime_n64_usage_context_trace_missing_required_field_count": (
                len(route_runtime_n64_usage_context.get("runtime_trace_missing_required_fields", []))
            ),
            "animation_semantic_route_runtime_n64_usage_context_runner_implemented_count": (
                route_runtime_n64_usage_context.get("context_runner_implemented_count", 0)
            ),
            "animation_semantic_route_runtime_n64_usage_context_accepted_route_count": (
                route_runtime_n64_usage_context.get("accepted_route_count", 0)
            ),
            "animation_semantic_route_runtime_n64_usage_context_installed_dump_valid_count": (
                route_runtime_n64_usage_context.get("installed_runtime_dump_valid_count", 0)
            ),
            "animation_semantic_route_runtime_n64_usage_context_semantic_accepted_count": (
                route_runtime_n64_usage_context.get("semantic_accepted_count", 0)
            ),
            "animation_semantic_route_runtime_n64_usage_context_mapping_promotion_allowed_count": (
                route_runtime_n64_usage_context.get("mapping_promotion_allowed_count", 0)
            ),
            "animation_semantic_route_runtime_n64_usage_context_evidence_kind_counts": (
                route_runtime_n64_usage_evidence_kind_counts
            ),
            "animation_semantic_route_runtime_n64_usage_context_runner_scope_counts": (
                route_runtime_n64_usage_runner_scope_counts
            ),
            "animation_semantic_route_runtime_dependency_policy_status": (
                route_runtime_dependency_policy.get("status")
            ),
            "animation_semantic_route_runtime_dependency_policy_row_count": (
                route_runtime_dependency_policy.get("record_count", 0)
            ),
            "animation_semantic_route_runtime_dependency_policy_ready_count": (
                route_runtime_dependency_policy.get("dependency_ready_count", 0)
            ),
            "animation_semantic_route_runtime_dependency_policy_workorder_ready_count": (
                route_runtime_dependency_policy.get("route_workorder_ready_count", 0)
            ),
            "animation_semantic_route_runtime_dependency_policy_n64_usage_ready_count": (
                route_runtime_dependency_policy.get("n64_usage_ready_count", 0)
            ),
            "animation_semantic_route_runtime_dependency_policy_package_ready_count": (
                route_runtime_dependency_policy.get("package_ready_count", 0)
            ),
            "animation_semantic_route_runtime_dependency_policy_offline_parity_valid_count": (
                route_runtime_dependency_policy.get("offline_runtime_parity_valid_count", 0)
            ),
            "animation_semantic_route_runtime_dependency_policy_runtime_config_ready_count": (
                route_runtime_dependency_policy.get("runtime_config_ready_count", 0)
            ),
            "animation_semantic_route_runtime_dependency_policy_harness_valid_count": (
                route_runtime_dependency_policy.get("runtime_harness_dump_valid_count", 0)
            ),
            "animation_semantic_route_runtime_dependency_policy_natural_capture_required_count": (
                route_runtime_dependency_policy.get("natural_capture_required_count", 0)
            ),
            "animation_semantic_route_runtime_dependency_policy_exact_symbol_route_count": (
                route_runtime_dependency_policy.get("exact_symbol_route_count", 0)
            ),
            "animation_semantic_route_runtime_dependency_policy_family_or_sibling_route_count": (
                route_runtime_dependency_policy.get("family_or_sibling_route_count", 0)
            ),
            "animation_semantic_route_runtime_dependency_policy_family_or_sibling_reference_count": (
                route_runtime_dependency_policy.get("family_or_sibling_reference_count", 0)
            ),
            "animation_semantic_route_runtime_dependency_policy_source_window_count": (
                route_runtime_dependency_policy.get("source_window_count", 0)
            ),
            "animation_semantic_route_runtime_dependency_policy_trace_required_field_count": (
                route_runtime_dependency_policy.get("runtime_trace_required_field_count", 0)
            ),
            "animation_semantic_route_runtime_dependency_policy_trace_required_field_present_count": (
                route_runtime_dependency_policy.get(
                    "runtime_trace_required_field_present_count", 0
                )
            ),
            "animation_semantic_route_runtime_dependency_policy_route_accepted_count": (
                route_runtime_dependency_policy.get("route_accepted_count", 0)
            ),
            "animation_semantic_route_runtime_dependency_policy_semantic_accepted_count": (
                route_runtime_dependency_policy.get("semantic_accepted_count", 0)
            ),
            "animation_semantic_route_runtime_dependency_policy_mapping_promotion_allowed_count": (
                route_runtime_dependency_policy.get("mapping_promotion_allowed_count", 0)
            ),
            "animation_semantic_route_runtime_dependency_policy_issue_count": (
                route_runtime_dependency_policy.get("issue_count", 0)
            ),
            "animation_semantic_route_runtime_dependency_policy_status_counts": (
                route_runtime_dependency_policy_status_counts
            ),
            "animation_semantic_route_runtime_dependency_policy_context_class_counts": (
                route_runtime_dependency_policy_context_class_counts
            ),
            "animation_semantic_route_runtime_dependency_policy_evidence_kind_counts": (
                route_runtime_dependency_policy_evidence_kind_counts
            ),
            "animation_semantic_n64_route_proof_priority_matrix_status": (
                n64_route_proof_priority_matrix.get("status")
            ),
            "animation_semantic_n64_route_proof_priority_matrix_row_count": (
                n64_route_proof_priority_matrix.get("record_count", 0)
            ),
            "animation_semantic_n64_route_proof_priority_exact_direct_count": (
                n64_route_proof_priority_matrix.get("exact_direct_route_proven_count", 0)
            ),
            "animation_semantic_n64_route_proof_priority_direct_callsite_context_count": (
                n64_route_proof_priority_matrix.get("direct_player_actor_callsite_context_count", 0)
            ),
            "animation_semantic_n64_route_proof_priority_direct_total_count": (
                n64_route_proof_priority_matrix.get(
                    "direct_n64_route_or_callsite_context_count", 0
                )
            ),
            "animation_semantic_n64_route_proof_priority_family_or_sibling_count": (
                n64_route_proof_priority_matrix.get("family_or_sibling_route_only_count", 0)
            ),
            "animation_semantic_n64_route_proof_priority_anonymous_table_owner_count": (
                n64_route_proof_priority_matrix.get("anonymous_table_owner_context_count", 0)
            ),
            "animation_semantic_n64_route_proof_priority_promoted_alias_count": (
                n64_route_proof_priority_matrix.get("promoted_owner_alias_delegated_count", 0)
            ),
            "animation_semantic_n64_route_proof_priority_route_absent_policy_count": (
                n64_route_proof_priority_matrix.get("route_absent_policy_only_count", 0)
            ),
            "animation_semantic_n64_route_proof_priority_source_risk_no_route_count": (
                n64_route_proof_priority_matrix.get("source_risk_no_route_proof_count", 0)
            ),
            "animation_semantic_n64_route_proof_priority_direct_reference_count": (
                n64_route_proof_priority_matrix.get("direct_player_actor_reference_count", 0)
            ),
            "animation_semantic_n64_route_proof_priority_family_reference_count": (
                n64_route_proof_priority_matrix.get("family_or_sibling_reference_count", 0)
            ),
            "animation_semantic_n64_route_proof_priority_candidate_owner_reference_count": (
                n64_route_proof_priority_matrix.get("candidate_owner_reference_count", 0)
            ),
            "animation_semantic_n64_route_proof_priority_source_window_count": (
                n64_route_proof_priority_matrix.get("source_window_count", 0)
            ),
            "animation_semantic_n64_route_proof_priority_harness_valid_count": (
                n64_route_proof_priority_matrix.get("runtime_harness_dump_valid_count", 0)
            ),
            "animation_semantic_n64_route_proof_priority_installed_runtime_dump_valid_count": (
                n64_route_proof_priority_matrix.get("installed_runtime_dump_valid_count", 0)
            ),
            "animation_semantic_n64_route_proof_priority_installed_static_frame_dump_valid_count": (
                n64_route_proof_priority_matrix.get("installed_static_frame_dump_valid_count", 0)
            ),
            "animation_semantic_n64_route_proof_priority_runtime_valid_evidence_count": (
                n64_route_proof_priority_matrix.get("runtime_valid_evidence_count", 0)
            ),
            "animation_semantic_n64_route_proof_priority_semantic_accepted_count": (
                n64_route_proof_priority_matrix.get("semantic_accepted_count", 0)
            ),
            "animation_semantic_n64_route_proof_priority_mapping_promotion_allowed_count": (
                n64_route_proof_priority_matrix.get("mapping_promotion_allowed_count", 0)
            ),
            "animation_semantic_n64_route_proof_priority_issue_count": (
                n64_route_proof_priority_matrix.get("issue_count", 0)
            ),
            "animation_semantic_n64_route_proof_priority_bucket_counts": (
                n64_route_proof_priority_bucket_counts
            ),
            "animation_semantic_n64_route_proof_priority_status_counts": (
                n64_route_proof_priority_status_counts
            ),
            "animation_semantic_n64_route_proof_priority_evidence_kind_counts": (
                n64_route_proof_priority_evidence_kind_counts
            ),
            "animation_semantic_n64_route_proof_priority_blocker_counts": (
                n64_route_proof_priority_blocker_counts
            ),
            "animation_semantic_n64_route_proof_acceptance_policy_status": (
                n64_route_proof_acceptance_policy.get("status")
            ),
            "animation_semantic_n64_route_proof_acceptance_policy_row_count": (
                n64_route_proof_acceptance_policy.get("record_count", 0)
            ),
            "animation_semantic_n64_route_proof_acceptance_accepted_count": (
                n64_route_proof_acceptance_policy.get("n64_route_accepted_count", 0)
            ),
            "animation_semantic_n64_route_proof_acceptance_route_policy_ready_count": (
                n64_route_proof_acceptance_policy.get("route_or_owner_policy_ready_count", 0)
            ),
            "animation_semantic_n64_route_proof_acceptance_direct_context_ready_count": (
                n64_route_proof_acceptance_policy.get("direct_callsite_context_ready_count", 0)
            ),
            "animation_semantic_n64_route_proof_acceptance_accepted_or_direct_context_count": (
                n64_route_proof_acceptance_policy.get("accepted_route_or_direct_context_count", 0)
            ),
            "animation_semantic_n64_route_proof_acceptance_family_policy_required_count": (
                n64_route_proof_acceptance_policy.get("family_route_policy_required_count", 0)
            ),
            "animation_semantic_n64_route_proof_acceptance_anonymous_owner_required_count": (
                n64_route_proof_acceptance_policy.get("anonymous_owner_identity_required_count", 0)
            ),
            "animation_semantic_n64_route_proof_acceptance_promoted_alias_required_count": (
                n64_route_proof_acceptance_policy.get("promoted_owner_alias_policy_required_count", 0)
            ),
            "animation_semantic_n64_route_proof_acceptance_route_absent_policy_required_count": (
                n64_route_proof_acceptance_policy.get("route_absent_owner_policy_required_count", 0)
            ),
            "animation_semantic_n64_route_proof_acceptance_alternate_source_required_count": (
                n64_route_proof_acceptance_policy.get("alternate_source_required_count", 0)
            ),
            "animation_semantic_n64_route_proof_acceptance_installed_draw_required_count": (
                n64_route_proof_acceptance_policy.get("installed_runtime_draw_required_count", 0)
            ),
            "animation_semantic_n64_route_proof_acceptance_installed_runtime_dump_valid_count": (
                n64_route_proof_acceptance_policy.get("installed_runtime_dump_valid_count", 0)
            ),
            "animation_semantic_n64_route_proof_acceptance_installed_static_frame_dump_valid_count": (
                n64_route_proof_acceptance_policy.get("installed_static_frame_dump_valid_count", 0)
            ),
            "animation_semantic_n64_route_proof_acceptance_runtime_harness_dump_valid_count": (
                n64_route_proof_acceptance_policy.get("runtime_harness_dump_valid_count", 0)
            ),
            "animation_semantic_n64_route_proof_acceptance_source_identity_accepted_count": (
                n64_route_proof_acceptance_policy.get("source_identity_accepted_count", 0)
            ),
            "animation_semantic_n64_route_proof_acceptance_source_identity_review_issue_count": (
                n64_route_proof_acceptance_policy.get("source_identity_review_issue_count", 0)
            ),
            "animation_semantic_n64_route_proof_acceptance_semantic_accepted_count": (
                n64_route_proof_acceptance_policy.get("semantic_accepted_count", 0)
            ),
            "animation_semantic_n64_route_proof_acceptance_mapping_promotion_allowed_count": (
                n64_route_proof_acceptance_policy.get("mapping_promotion_allowed_count", 0)
            ),
            "animation_semantic_n64_route_proof_acceptance_issue_count": (
                n64_route_proof_acceptance_policy.get("issue_count", 0)
            ),
            "animation_semantic_n64_route_proof_acceptance_status_counts": (
                n64_route_proof_acceptance_status_counts
            ),
            "animation_semantic_n64_route_proof_acceptance_bucket_counts": (
                n64_route_proof_acceptance_bucket_counts
            ),
            "animation_semantic_n64_route_proof_acceptance_policy_gate_counts": (
                n64_route_proof_acceptance_policy_gate_counts
            ),
            "animation_semantic_n64_route_proof_acceptance_source_identity_review_status_counts": (
                n64_route_proof_acceptance_source_identity_review_status_counts
            ),
            "animation_semantic_in_game_replacement_gate_status": (
                in_game_replacement_gate.get("status")
            ),
            "animation_semantic_in_game_replacement_gate_row_count": (
                in_game_replacement_gate.get("record_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_route_proof_row_count": (
                in_game_replacement_gate.get("route_proof_matrix_row_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_route_acceptance_policy_row_count": (
                in_game_replacement_gate.get("route_acceptance_policy_row_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_runtime_capture_row_count": (
                in_game_replacement_gate.get("runtime_capture_row_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_closure_row_count": (
                in_game_replacement_gate.get("closure_record_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_route_dependency_count": (
                in_game_replacement_gate.get("route_proof_ready_or_dependency_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_n64_route_accepted_count": (
                in_game_replacement_gate.get("n64_route_accepted_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_route_policy_ready_count": (
                in_game_replacement_gate.get("route_or_owner_policy_ready_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_exact_direct_count": (
                in_game_replacement_gate.get("exact_direct_route_proven_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_direct_callsite_context_count": (
                in_game_replacement_gate.get("direct_player_callsite_context_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_direct_total_count": (
                in_game_replacement_gate.get("direct_route_or_callsite_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_family_or_sibling_count": (
                in_game_replacement_gate.get("family_or_sibling_route_only_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_anonymous_table_owner_count": (
                in_game_replacement_gate.get("anonymous_table_owner_context_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_promoted_alias_count": (
                in_game_replacement_gate.get("promoted_owner_alias_delegated_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_route_absent_policy_count": (
                in_game_replacement_gate.get("route_absent_policy_only_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_source_risk_no_route_count": (
                in_game_replacement_gate.get("source_risk_no_route_proof_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_direct_reference_count": (
                in_game_replacement_gate.get("direct_player_actor_reference_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_family_reference_count": (
                in_game_replacement_gate.get("family_or_sibling_reference_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_candidate_owner_reference_count": (
                in_game_replacement_gate.get("candidate_owner_reference_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_source_window_count": (
                in_game_replacement_gate.get("source_window_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_harness_valid_count": (
                in_game_replacement_gate.get("harness_valid_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_harness_only_count": (
                in_game_replacement_gate.get("harness_only_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_installed_draw_missing_count": (
                in_game_replacement_gate.get("installed_draw_missing_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_installed_runtime_dump_valid_count": (
                in_game_replacement_gate.get("installed_runtime_dump_valid_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_installed_static_frame_dump_valid_count": (
                in_game_replacement_gate.get("installed_static_frame_dump_valid_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_source_identity_accepted_count": (
                in_game_replacement_gate.get("source_identity_accepted_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_semantic_accepted_count": (
                in_game_replacement_gate.get("semantic_accepted_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_mapping_promotion_allowed_count": (
                in_game_replacement_gate.get("mapping_promotion_allowed_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_replacement_ready_count": (
                in_game_replacement_gate.get("replacement_ready_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_allowed_count": (
                in_game_replacement_gate.get("in_game_replacement_allowed_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_unsafe_count": (
                in_game_replacement_gate.get("unsafe_to_replace_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_issue_count": (
                in_game_replacement_gate.get("issue_count", 0)
            ),
            "animation_semantic_in_game_replacement_gate_bucket_counts": (
                in_game_replacement_gate_bucket_counts
            ),
            "animation_semantic_in_game_replacement_gate_status_counts": (
                in_game_replacement_gate_status_counts
            ),
            "animation_semantic_in_game_replacement_gate_next_blocker_counts": (
                in_game_replacement_gate_next_blocker_counts
            ),
            "animation_semantic_in_game_replacement_gate_installed_draw_status_counts": (
                in_game_replacement_gate_installed_draw_status_counts
            ),
            "animation_semantic_in_game_replacement_gate_route_or_owner_status_counts": (
                in_game_replacement_gate_route_or_owner_status_counts
            ),
            "animation_semantic_in_game_replacement_gate_source_identity_status_counts": (
                in_game_replacement_gate_source_identity_status_counts
            ),
            "animation_semantic_installed_draw_proof_queue_status": (
                installed_draw_proof_queue.get("status")
            ),
            "animation_semantic_installed_draw_proof_queue_row_count": (
                installed_draw_proof_queue.get("record_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_replacement_gate_row_count": (
                installed_draw_proof_queue.get("replacement_gate_row_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_campaign_row_count": (
                installed_draw_proof_queue.get("campaign_row_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_campaign_session_count": (
                installed_draw_proof_queue.get("campaign_session_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_runbook_row_count": (
                installed_draw_proof_queue.get("runbook_row_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_runbook_session_count": (
                installed_draw_proof_queue.get("runbook_session_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_linked_session_count": (
                installed_draw_proof_queue.get("linked_session_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_first_wave_session_count": (
                installed_draw_proof_queue.get("first_wave_direct_route_session_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_row_to_campaign_linked_count": (
                installed_draw_proof_queue.get("row_to_campaign_linked_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_row_to_runbook_linked_count": (
                installed_draw_proof_queue.get("row_to_runbook_linked_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_row_runner_ready_count": (
                installed_draw_proof_queue.get("row_capture_runner_ready_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_session_runner_ready_row_count": (
                installed_draw_proof_queue.get("session_capture_runner_ready_row_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_preflight_ready_count": (
                installed_draw_proof_queue.get("preflight_ready_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_no_launch_preflight_ready_count": (
                installed_draw_proof_queue.get("no_launch_preflight_ready_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_non_harness_command_count": (
                installed_draw_proof_queue.get("non_harness_capture_command_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_harness_command_count": (
                installed_draw_proof_queue.get("harness_capture_command_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_expected_dump_path_count": (
                installed_draw_proof_queue.get("expected_dump_path_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_expected_context_trace_path_count": (
                installed_draw_proof_queue.get("expected_context_trace_path_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_installed_draw_valid_count": (
                installed_draw_proof_queue.get("installed_draw_valid_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_harness_only_count": (
                installed_draw_proof_queue.get("harness_only_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_replacement_ready_count": (
                installed_draw_proof_queue.get("replacement_ready_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_trigger_required_count": (
                installed_draw_proof_queue.get("trigger_required_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_external_trigger_required_count": (
                installed_draw_proof_queue.get("external_trigger_required_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_automatic_replay_ready_count": (
                installed_draw_proof_queue.get("automatic_replay_ready_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_replay_seed_matrix_automatic_replay_ready_session_count": (
                installed_draw_proof_queue.get(
                    "replay_seed_matrix_automatic_replay_ready_session_count", 0
                )
            ),
            "animation_semantic_installed_draw_proof_queue_replay_seed_matrix_external_trigger_required_session_count": (
                installed_draw_proof_queue.get(
                    "replay_seed_matrix_external_trigger_required_session_count", 0
                )
            ),
            "animation_semantic_installed_draw_proof_queue_first_wave_automatic_replay_ready_count": (
                installed_draw_proof_queue.get("first_wave_automatic_replay_ready_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_runtime_config_dynamic_playback_trace_ready_count": (
                installed_draw_proof_queue.get(
                    "runtime_config_dynamic_playback_trace_ready_count", 0
                )
            ),
            "animation_semantic_installed_draw_proof_queue_runtime_config_dynamic_playback_trace_incomplete_count": (
                installed_draw_proof_queue.get(
                    "runtime_config_dynamic_playback_trace_incomplete_count", 0
                )
            ),
            "animation_semantic_installed_draw_proof_queue_dynamic_playback_trace_ready_count": (
                installed_draw_proof_queue.get("dynamic_playback_trace_ready_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_dynamic_playback_trace_incomplete_count": (
                installed_draw_proof_queue.get(
                    "dynamic_playback_trace_incomplete_count", 0
                )
            ),
            "animation_semantic_installed_draw_proof_queue_dynamic_playback_trace_missing_count": (
                installed_draw_proof_queue.get("dynamic_playback_trace_missing_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_first_wave_dynamic_playback_trace_ready_count": (
                installed_draw_proof_queue.get(
                    "first_wave_dynamic_playback_trace_ready_count", 0
                )
            ),
            "animation_semantic_installed_draw_proof_queue_first_wave_dynamic_playback_trace_incomplete_count": (
                installed_draw_proof_queue.get(
                    "first_wave_dynamic_playback_trace_incomplete_count", 0
                )
            ),
            "animation_semantic_installed_draw_proof_queue_first_wave_direct_route_row_count": (
                installed_draw_proof_queue.get("first_wave_direct_route_row_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_direct_callsite_row_count": (
                installed_draw_proof_queue.get(
                    "direct_callsite_source_identity_row_count", 0
                )
            ),
            "animation_semantic_installed_draw_proof_queue_family_route_row_count": (
                installed_draw_proof_queue.get("family_route_policy_row_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_anonymous_owner_row_count": (
                installed_draw_proof_queue.get("anonymous_owner_identity_row_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_alias_owner_row_count": (
                installed_draw_proof_queue.get("alias_owner_policy_row_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_route_absent_row_count": (
                installed_draw_proof_queue.get("route_absent_owner_policy_row_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_alternate_source_row_count": (
                installed_draw_proof_queue.get("alternate_source_row_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_issue_count": (
                installed_draw_proof_queue.get("issue_count", 0)
            ),
            "animation_semantic_installed_draw_proof_queue_wave_counts": (
                installed_draw_proof_queue_wave_counts
            ),
            "animation_semantic_installed_draw_proof_queue_status_counts": (
                installed_draw_proof_queue_status_counts
            ),
            "animation_semantic_installed_draw_proof_queue_automation_status_counts": (
                installed_draw_proof_queue_automation_status_counts
            ),
            "animation_semantic_installed_draw_proof_queue_route_bucket_counts": (
                installed_draw_proof_queue_route_bucket_counts
            ),
            "animation_semantic_replay_trigger_seed_matrix_status": (
                replay_trigger_seed_matrix.get("status")
            ),
            "animation_semantic_replay_trigger_seed_matrix_session_count": (
                replay_trigger_seed_matrix.get("session_count", 0)
            ),
            "animation_semantic_replay_trigger_seed_matrix_runbook_session_count": (
                replay_trigger_seed_matrix.get("runbook_session_count", 0)
            ),
            "animation_semantic_replay_trigger_seed_matrix_runbook_row_count": (
                replay_trigger_seed_matrix.get("runbook_row_count", 0)
            ),
            "animation_semantic_replay_trigger_seed_matrix_proof_queue_row_count": (
                replay_trigger_seed_matrix.get("proof_queue_row_count", 0)
            ),
            "animation_semantic_replay_trigger_seed_matrix_session_row_total": (
                replay_trigger_seed_matrix.get("session_row_total", 0)
            ),
            "animation_semantic_replay_trigger_seed_matrix_session_with_proof_rows_count": (
                replay_trigger_seed_matrix.get("session_with_proof_rows_count", 0)
            ),
            "animation_semantic_replay_trigger_seed_matrix_target_save_file_count": (
                replay_trigger_seed_matrix.get("target_save_file_count", 0)
            ),
            "animation_semantic_replay_trigger_seed_matrix_target_replay_seed_file_count": (
                replay_trigger_seed_matrix.get("target_replay_seed_file_count", 0)
            ),
            "animation_semantic_replay_trigger_seed_matrix_target_input_macro_file_count": (
                replay_trigger_seed_matrix.get("target_input_macro_file_count", 0)
            ),
            "animation_semantic_replay_trigger_seed_matrix_global_save_present": (
                replay_trigger_seed_matrix.get("global_save_present", False)
            ),
            "animation_semantic_replay_trigger_seed_matrix_base_save_available_session_count": (
                replay_trigger_seed_matrix.get("base_save_available_session_count", 0)
            ),
            "animation_semantic_replay_trigger_seed_matrix_session_replay_seed_present_count": (
                replay_trigger_seed_matrix.get("session_replay_seed_present_count", 0)
            ),
            "animation_semantic_replay_trigger_seed_matrix_input_macro_present_count": (
                replay_trigger_seed_matrix.get("input_macro_present_count", 0)
            ),
            "animation_semantic_replay_trigger_seed_matrix_automatic_replay_ready_count": (
                replay_trigger_seed_matrix.get("automatic_replay_ready_count", 0)
            ),
            "animation_semantic_replay_trigger_seed_matrix_external_trigger_required_count": (
                replay_trigger_seed_matrix.get("external_trigger_required_count", 0)
            ),
            "animation_semantic_replay_trigger_seed_matrix_session_seed_missing_count": (
                replay_trigger_seed_matrix.get("session_seed_missing_count", 0)
            ),
            "animation_semantic_replay_trigger_seed_matrix_input_macro_missing_count": (
                replay_trigger_seed_matrix.get("input_macro_missing_count", 0)
            ),
            "animation_semantic_replay_trigger_seed_matrix_policy_first_session_count": (
                replay_trigger_seed_matrix.get("policy_first_session_count", 0)
            ),
            "animation_semantic_replay_trigger_seed_matrix_alternate_source_first_session_count": (
                replay_trigger_seed_matrix.get("alternate_source_first_session_count", 0)
            ),
            "animation_semantic_replay_trigger_seed_matrix_first_wave_session_count": (
                replay_trigger_seed_matrix.get("first_wave_direct_route_session_count", 0)
            ),
            "animation_semantic_replay_trigger_seed_matrix_first_wave_row_count": (
                replay_trigger_seed_matrix.get("first_wave_direct_route_row_count", 0)
            ),
            "animation_semantic_replay_trigger_seed_matrix_row_runner_ready_session_count": (
                replay_trigger_seed_matrix.get("row_runner_ready_session_count", 0)
            ),
            "animation_semantic_replay_trigger_seed_matrix_installed_draw_valid_session_count": (
                replay_trigger_seed_matrix.get("installed_draw_valid_session_count", 0)
            ),
            "animation_semantic_replay_trigger_seed_matrix_issue_count": (
                replay_trigger_seed_matrix.get("issue_count", 0)
            ),
            "animation_semantic_replay_trigger_seed_matrix_session_kind_counts": (
                replay_trigger_seed_matrix_session_kind_counts
            ),
            "animation_semantic_replay_trigger_seed_matrix_automation_class_counts": (
                replay_trigger_seed_matrix_automation_class_counts
            ),
            "animation_semantic_replay_trigger_seed_matrix_seed_status_counts": (
                replay_trigger_seed_matrix_seed_status_counts
            ),
            "animation_semantic_replay_trigger_seed_matrix_blocking_kind_counts": (
                replay_trigger_seed_matrix_blocking_kind_counts
            ),
            "animation_semantic_first_wave_replay_seed_specs_status": (
                first_wave_replay_seed_specs.get("status")
            ),
            "animation_semantic_first_wave_replay_seed_specs_session_count": (
                first_wave_replay_seed_specs.get("session_count", 0)
            ),
            "animation_semantic_first_wave_replay_seed_specs_covered_row_count": (
                first_wave_replay_seed_specs.get("covered_row_count", 0)
            ),
            "animation_semantic_first_wave_replay_seed_specs_direct_route_session_count": (
                first_wave_replay_seed_specs.get("direct_route_session_count", 0)
            ),
            "animation_semantic_first_wave_replay_seed_specs_direct_route_row_count": (
                first_wave_replay_seed_specs.get("direct_route_row_count", 0)
            ),
            "animation_semantic_first_wave_replay_seed_specs_template_count": (
                first_wave_replay_seed_specs.get("spec_template_count", 0)
            ),
            "animation_semantic_first_wave_replay_seed_specs_pending_authoring_count": (
                first_wave_replay_seed_specs.get("pending_seed_authoring_count", 0)
            ),
            "animation_semantic_first_wave_replay_seed_specs_automatic_replay_ready_count": (
                first_wave_replay_seed_specs.get("automatic_replay_ready_count", 0)
            ),
            "animation_semantic_first_wave_replay_seed_specs_installed_draw_valid_count": (
                first_wave_replay_seed_specs.get("installed_draw_valid_count", 0)
            ),
            "animation_semantic_first_wave_replay_seed_specs_replay_seed_present_count": (
                first_wave_replay_seed_specs.get("replay_seed_present_count", 0)
            ),
            "animation_semantic_first_wave_replay_seed_specs_input_macro_present_count": (
                first_wave_replay_seed_specs.get("input_macro_present_count", 0)
            ),
            "animation_semantic_first_wave_replay_seed_specs_row_runner_ready_count": (
                first_wave_replay_seed_specs.get("row_runner_ready_count", 0)
            ),
            "animation_semantic_first_wave_replay_seed_specs_session_runner_ready_count": (
                first_wave_replay_seed_specs.get("session_runner_ready_count", 0)
            ),
            "animation_semantic_first_wave_replay_seed_specs_expected_dump_path_count": (
                first_wave_replay_seed_specs.get("expected_dump_path_count", 0)
            ),
            "animation_semantic_first_wave_replay_seed_specs_expected_context_trace_path_count": (
                first_wave_replay_seed_specs.get("expected_context_trace_path_count", 0)
            ),
            "animation_semantic_first_wave_replay_seed_specs_policy_first_count": (
                first_wave_replay_seed_specs.get("policy_first_count", 0)
            ),
            "animation_semantic_first_wave_replay_seed_specs_alternate_source_first_count": (
                first_wave_replay_seed_specs.get("alternate_source_first_count", 0)
            ),
            "animation_semantic_first_wave_replay_seed_specs_issue_count": (
                first_wave_replay_seed_specs.get("issue_count", 0)
            ),
            "animation_semantic_first_wave_replay_seed_specs_evidence_kind_counts": (
                first_wave_replay_seed_specs_evidence_counts
            ),
            "animation_semantic_first_wave_replay_seed_specs_seed_authoring_status_counts": (
                first_wave_replay_seed_specs_seed_status_counts
            ),
            "animation_semantic_first_wave_replay_seed_specs_session_status_counts": (
                first_wave_replay_seed_specs_session_status_counts
            ),
            "animation_semantic_ledge_climb_route_plan_status": (
                ledge_climb_route_plan.get("status")
            ),
            "animation_semantic_ledge_climb_route_plan_record_count": (
                ledge_climb_route_plan.get("record_count", 0)
            ),
            "animation_semantic_ledge_climb_route_plan_positive_state_gate_record_count": (
                ledge_climb_route_plan.get("positive_state_gate_record_count", 0)
            ),
            "animation_semantic_ledge_climb_route_plan_dynamic_trace_ready_count": (
                ledge_climb_route_plan.get("dynamic_trace_ready_count", 0)
            ),
            "animation_semantic_ledge_climb_route_plan_failed_position_sweep_record_count": (
                ledge_climb_route_plan.get("failed_position_sweep_record_count", 0)
            ),
            "animation_semantic_ledge_climb_route_plan_sentinel_wall_too_high_or_invalid_count": (
                ledge_climb_route_plan.get("sentinel_wall_too_high_or_invalid_count", 0)
            ),
            "animation_semantic_ledge_climb_route_plan_state_aware_seed_candidate_count": (
                ledge_climb_route_plan.get("state_aware_seed_candidate_count", 0)
            ),
            "animation_semantic_ledge_climb_route_plan_best_positive_n64_name": (
                ledge_climb_route_plan.get("best_positive_n64_name", "")
            ),
            "animation_semantic_ledge_climb_route_plan_best_positive_ledge_climb_type": (
                ledge_climb_route_plan.get("best_positive_ledge_climb_type", 0)
            ),
            "animation_semantic_ledge_climb_route_plan_best_positive_y_dist_to_ledge": (
                ledge_climb_route_plan.get("best_positive_y_dist_to_ledge")
            ),
            "animation_semantic_ledge_climb_route_plan_semantic_accepted_count": (
                ledge_climb_route_plan.get("semantic_accepted_count", 0)
            ),
            "animation_semantic_ledge_climb_route_plan_installed_draw_valid_count": (
                ledge_climb_route_plan.get("installed_draw_valid_count", 0)
            ),
            "animation_semantic_ledge_climb_route_plan_mapping_promotion_allowed_count": (
                ledge_climb_route_plan.get("mapping_promotion_allowed_count", 0)
            ),
            "animation_semantic_ledge_climb_route_plan_status_counts": (
                ledge_climb_route_plan_status_counts
            ),
            "animation_semantic_ledge_climb_route_plan_evidence_kind_counts": (
                ledge_climb_route_plan_evidence_counts
            ),
            "animation_semantic_ledge_climb_state_capture_kit_status": (
                ledge_climb_state_capture_kit.get("status")
            ),
            "animation_semantic_ledge_climb_state_capture_kit_seed_generation_status": (
                ledge_climb_state_capture_kit.get("seed_generation_status")
            ),
            "animation_semantic_ledge_climb_state_capture_kit_pre_ledge_source_gameplay_frame": (
                ledge_climb_state_capture_kit.get("pre_ledge_source_gameplay_frame")
            ),
            "animation_semantic_ledge_climb_state_capture_kit_positive_target_gameplay_frame": (
                ledge_climb_state_capture_kit.get("positive_target_gameplay_frame")
            ),
            "animation_semantic_ledge_climb_state_capture_kit_approach_stick_stop_frame": (
                ledge_climb_state_capture_kit.get("approach_stick_stop_frame")
            ),
            "animation_semantic_ledge_climb_state_capture_kit_input_driver_stop_frame": (
                ledge_climb_state_capture_kit.get("input_driver_stop_frame")
            ),
            "animation_semantic_ledge_climb_state_capture_kit_expected_installed_frame": (
                ledge_climb_state_capture_kit.get("expected_installed_frame")
            ),
            "animation_semantic_ledge_climb_state_capture_kit_session_replay_seed_applied": (
                ledge_climb_state_capture_kit.get("session_replay_seed_applied")
            ),
            "animation_semantic_ledge_climb_state_capture_kit_natural_input_driver_enabled": (
                ledge_climb_state_capture_kit.get("natural_input_driver_enabled")
            ),
            "animation_semantic_ledge_climb_state_capture_kit_natural_input_driver_mode_name": (
                ledge_climb_state_capture_kit.get("natural_input_driver_mode_name")
            ),
            "animation_semantic_ledge_climb_state_capture_kit_natural_input_driver_macro_exists": (
                ledge_climb_state_capture_kit.get("natural_input_driver_macro_exists")
            ),
            "animation_semantic_ledge_climb_state_capture_kit_boot_sequence": (
                ledge_climb_state_capture_kit.get("boot_sequence")
            ),
            "animation_semantic_ledge_climb_state_capture_kit_launched": (
                ledge_climb_state_capture_kit.get("launched")
            ),
            "animation_semantic_ledge_climb_hold_free_capture_status": (
                ledge_climb_hold_free_capture_status.get("status")
            ),
            "animation_semantic_ledge_climb_hold_free_capture_dump_exists": (
                ledge_climb_hold_free_capture_status.get("dump_exists")
            ),
            "animation_semantic_ledge_climb_hold_free_capture_dump_uses_dynamic_frame": (
                ledge_climb_hold_free_capture_status.get("dump_uses_dynamic_frame")
            ),
            "animation_semantic_ledge_climb_hold_free_capture_target_row_count": (
                ledge_climb_hold_free_capture_status.get(
                    "context_trace_target_row_count", 0
                )
            ),
            "animation_semantic_ledge_climb_hold_free_capture_unique_selected_frame_count": (
                ledge_climb_hold_free_capture_status.get(
                    "context_trace_target_unique_selected_frame_count", 0
                )
            ),
            "animation_semantic_ledge_climb_hold_free_capture_selected_frame_min": (
                ledge_climb_hold_free_capture_status.get(
                    "context_trace_target_selected_frame_min"
                )
            ),
            "animation_semantic_ledge_climb_hold_free_capture_selected_frame_max": (
                ledge_climb_hold_free_capture_status.get(
                    "context_trace_target_selected_frame_max"
                )
            ),
            "animation_semantic_ledge_climb_hold_free_capture_unique_skel_frame_count": (
                ledge_climb_hold_free_capture_status.get(
                    "context_trace_target_unique_skel_frame_count", 0
                )
            ),
            "animation_semantic_ledge_climb_hold_free_capture_skel_frame_min": (
                ledge_climb_hold_free_capture_status.get(
                    "context_trace_target_skel_frame_min"
                )
            ),
            "animation_semantic_ledge_climb_hold_free_capture_skel_frame_max": (
                ledge_climb_hold_free_capture_status.get(
                    "context_trace_target_skel_frame_max"
                )
            ),
            "animation_semantic_ledge_climb_hold_free_capture_dynamic_playback_ready": (
                ledge_climb_hold_free_capture_status.get(
                    "context_trace_dynamic_playback_ready"
                )
            ),
            "animation_semantic_ledge_climb_hold_free_capture_audit_status": (
                ledge_climb_hold_free_capture_status.get("audit_status")
            ),
            "animation_semantic_ledge_climb_hold_free_capture_audit_issue_count": (
                ledge_climb_hold_free_capture_status.get("audit_issue_count")
            ),
            "animation_semantic_ledge_climb_hold_free_capture_installed_runtime_dump_valid_count": (
                ledge_climb_hold_free_capture_status.get(
                    "installed_runtime_dump_valid_count", 0
                )
            ),
            "animation_semantic_ledge_climb_hold_free_capture_runtime_harness_dump_valid_count": (
                ledge_climb_hold_free_capture_status.get(
                    "runtime_harness_dump_valid_count", 0
                )
            ),
            "animation_semantic_ledge_climb_hold_free_capture_mapping_promotion_allowed_count": (
                ledge_climb_hold_free_capture_status.get(
                    "mapping_promotion_allowed_count", 0
                )
            ),
            "animation_semantic_ledge_climb_hold_free_capture_issue_count": (
                ledge_climb_hold_free_capture_status.get("issue_count", 0)
            ),
            "animation_semantic_natural_replacement_smoke_status": (
                natural_replacement_smoke.get("status")
            ),
            "animation_semantic_natural_replacement_smoke_valid_count": (
                natural_replacement_smoke.get("replacement_smoke_valid_count", 0)
            ),
            "animation_semantic_natural_replacement_smoke_runtime_harness_enabled": (
                natural_replacement_smoke.get("runtime_harness_enabled", True)
            ),
            "animation_semantic_natural_replacement_smoke_n64_animation_name": (
                natural_replacement_smoke.get("n64_animation_name")
            ),
            "animation_semantic_natural_replacement_smoke_csab_name": (
                natural_replacement_smoke.get("csab_name")
            ),
            "animation_semantic_natural_replacement_smoke_selection_source": (
                natural_replacement_smoke.get("selection_source")
            ),
            "animation_semantic_natural_replacement_smoke_vertex_rows": (
                natural_replacement_smoke.get("vertex_rows", 0)
            ),
            "animation_semantic_natural_replacement_smoke_finite_pose_rows": (
                natural_replacement_smoke.get("finite_pose_rows", 0)
            ),
            "animation_semantic_natural_replacement_smoke_validation_error_count": (
                natural_replacement_smoke.get("validation_error_count", 0)
            ),
            "animation_semantic_natural_replacement_smoke_context_trace_row_count": (
                natural_replacement_smoke.get("context_trace_row_count", 0)
            ),
            "animation_semantic_ownership_decision_matrix_status": (
                ownership_decision_matrix.get("status")
            ),
            "animation_semantic_ownership_decision_matrix_row_count": (
                ownership_decision_matrix.get("record_count", 0)
            ),
            "animation_semantic_ownership_decision_matrix_issue_count": (
                ownership_decision_matrix.get("issue_count", 0)
            ),
            "animation_semantic_ownership_decision_route_proven_candidate_count": (
                ownership_decision_matrix.get("route_proven_package_candidate_count", 0)
            ),
            "animation_semantic_ownership_decision_route_absent_policy_candidate_count": (
                ownership_decision_matrix.get("route_absent_policy_candidate_count", 0)
            ),
            "animation_semantic_ownership_decision_pose_source_risk_count": (
                ownership_decision_matrix.get("ownership_plus_pose_source_risk_count", 0)
            ),
            "animation_semantic_ownership_decision_unique_source_csab_count": (
                ownership_decision_matrix.get("unique_source_csab_count", 0)
            ),
            "animation_semantic_ownership_decision_accepted_ownership_count": (
                ownership_decision_matrix.get("accepted_ownership_count", 0)
            ),
            "animation_semantic_ownership_decision_accepted_reuse_policy_count": (
                ownership_decision_matrix.get("accepted_reuse_policy_count", 0)
            ),
            "animation_semantic_ownership_decision_installed_dump_valid_count": (
                ownership_decision_matrix.get("installed_runtime_dump_valid_count", 0)
            ),
            "animation_semantic_ownership_decision_status_counts": (
                ownership_decision_status_counts
            ),
            "animation_semantic_ownership_decision_acceptance_gate_counts": (
                ownership_decision_acceptance_gate_counts
            ),
            "animation_semantic_blocked_or_unresolved_count": animation_semantic_resolution.get(
                "blocked_or_unresolved_count",
                0,
            ),
            "animation_semantic_blocked_frontier_status": animation_semantic_blocked_frontier.get("status"),
            "animation_semantic_blocked_frontier_row_count": animation_semantic_blocked_frontier.get(
                "row_count",
                0,
            ),
            "animation_semantic_blocked_frontier_candidate_source_present_count": (
                animation_semantic_blocked_frontier.get("candidate_source_present_count", 0)
            ),
            "animation_semantic_blocked_frontier_class_counts": animation_semantic_blocked_frontier_class_counts,
            "animation_semantic_blocked_frontier_gate_counts": animation_semantic_blocked_frontier_gate_counts,
            "animation_semantic_resolution_status_counts": animation_semantic_resolution_status_counts,
            "anb_semantic_candidate_channel_count": anb_semantic.get("candidate_channel_count", 0),
            "anb_semantic_strong_candidate_count": anb_semantic.get("strong_candidate_count", 0),
            "anb_semantic_stable_candidate_channel_count": anb_semantic.get("stable_candidate_channel_count", 0),
            "anb_semantic_channel_contract_status": anb_channel_contract.get("contract_status"),
            "anb_semantic_channel_contract_row_count": anb_channel_contract.get("row_count", 0),
            "anb_semantic_resolved_transform_channel_count": anb_channel_contract.get("resolved_transform_channel_count", 0),
            "anb_semantic_resolved_static_zero_channel_count": anb_channel_contract.get("resolved_static_zero_channel_count", 0),
            "anb_semantic_resolved_control_tuple_channel_count": anb_channel_contract.get("resolved_control_tuple_channel_count", 0),
            "anb_semantic_effective_resolved_channel_count": anb_channel_contract.get("effective_resolved_channel_count", 0),
            "anb_semantic_effective_unresolved_channel_count": anb_channel_contract.get("effective_unresolved_channel_count", 0),
            "anb_semantic_raw_translation_channel_count": anb_channel_component_family_counts.get("translation", 0),
            "anb_semantic_raw_rotation_channel_count": anb_channel_component_family_counts.get("rotation", 0),
            "anb_semantic_raw_trailing_channel_count": anb_channel_component_family_counts.get("none", 0),
            "anb_semantic_raw_csab_correlation_row_count": anb_raw_csab_correlation.get("row_count", 0),
            "anb_semantic_raw_csab_exact_match_count": anb_raw_csab_correlation.get(
                "exact_raw_layout_match_count",
                0,
            ),
            "anb_semantic_raw_csab_divergent_or_raw_only_count": anb_raw_csab_correlation.get(
                "divergent_or_raw_only_count",
                0,
            ),
            "anb_semantic_raw_csab_diverges_from_layout_count": anb_raw_csab_status_counts.get(
                "diverges_from_raw_n64_player_limb_layout",
                0,
            ),
            "anb_semantic_raw_csab_no_stable_correlation_count": anb_raw_csab_divergence_class_counts.get(
                "no_stable_csab_correlation",
                0,
            ),
            "anb_semantic_raw_csab_high_risk_translation_for_rotation_count": anb_raw_csab_divergence_class_counts.get(
                "csab_translation_correlation_for_raw_rotation",
                0,
            ),
            "anb_semantic_raw_csab_high_risk_realtime_count": anb_raw_csab_risk_class_counts.get(
                "high_false_root_or_model_translation_risk",
                0,
            ),
            "anb_semantic_unresolved_closure_ledger_row_count": anb_unresolved_closure.get("row_count", 0),
            "anb_semantic_unresolved_closure_action_class_counts": anb_unresolved_closure_action_counts,
            "anb_semantic_unresolved_closure_cross_form_cleanup_count": anb_unresolved_closure_action_counts.get(
                "cross_form_candidate_fix_direct_or_resampled_evidence",
                0,
            ),
            "anb_semantic_unresolved_closure_single_direct_second_hit_count": anb_unresolved_closure_action_counts.get(
                "cross_form_single_direct_needs_second_child_hit",
                0,
            ),
            "anb_semantic_unresolved_closure_adult_only_hold_count": anb_unresolved_closure_action_counts.get(
                "adult_only_hold_needs_child_source_evidence",
                0,
            ),
            "anb_semantic_unresolved_closure_shared_tied_count": anb_unresolved_closure_action_counts.get(
                "shared_tied_component_disambiguation",
                0,
            ),
            "anb_semantic_unresolved_closure_semantic_source_count": anb_unresolved_closure_action_counts.get(
                "semantic_source_required_control_or_curve",
                0,
            ),
            **capture_metrics,
            "route_proven_capture": route_capture_metrics,
            "route_proven_capture_status": route_capture_metrics.get("capture_status"),
            "route_proven_capture_issue_count": route_capture_metrics.get("capture_issue_count"),
            "route_proven_capture_dump_exists": route_capture_metrics.get("dump_exists"),
            "route_proven_capture_audit_status": route_capture_metrics.get("audit_status"),
            "route_proven_capture_audit_issue_count": route_capture_metrics.get("audit_issue_count"),
        },
        runtime_scope="link_child_profile_mounted_but_capture_pending",
        next_gate="Capture real installed runtime draw dumps for the climb sample and the 5 route-proven ownership derivatives.",
    )


def media_record(ctx: LedgerContext) -> dict[str, object] | None:
    rel = "media_asset_audit/oot3d_media_asset_summary.json"
    data = ctx.read(rel)
    if data is None:
        return missing_record("media_support_assets", ctx.path(rel))
    return base_record(
        "media_support_assets",
        "inventory_ready",
        ctx.path(rel),
        {
            "file_count": data.get("file_count", 0),
            "issue_count": data.get("issue_count", 0),
        },
        runtime_scope="metadata_only",
        next_gate="Promote per-format decoders or fallback handoff paths.",
    )


def audio_record(ctx: LedgerContext) -> dict[str, object] | None:
    rel = "audio_asset_audit/oot3d_audio_asset_summary.json"
    data = ctx.read(rel)
    if data is None:
        return missing_record("audio_assets", ctx.path(rel))
    return base_record(
        "audio_assets",
        "fallback_only",
        ctx.path(rel),
        {
            "audio_file_count": data.get("audio_file_count", data.get("file_count", 0)),
            "issue_count": data.get("issue_count", 0),
        },
        runtime_scope="n64_audio_fallback",
        next_gate="Implement BCSAR/BCSTM decoder or explicit audio handoff.",
    )


def q_format_record(ctx: LedgerContext) -> dict[str, object] | None:
    rel = "q_format_asset_audit/oot3d_q_format_asset_summary.json"
    data = ctx.read(rel)
    if data is None:
        return missing_record("q_format_ui_assets", ctx.path(rel))
    return base_record(
        "q_format_ui_assets",
        "inventory_ready",
        ctx.path(rel),
        {
            "file_count": data.get("file_count", 0),
            "issue_count": data.get("issue_count", 0),
        },
        runtime_scope="metadata_only",
        next_gate="Decode and route UI/layout/font resources where they replace N64 homologs.",
    )


def moflex_record(ctx: LedgerContext) -> dict[str, object] | None:
    rel = "moflex_movie_audit/oot3d_moflex_movie_summary.json"
    data = ctx.read(rel)
    if data is None:
        return missing_record("moflex_movies", ctx.path(rel))
    return base_record(
        "moflex_movies",
        "fallback_only",
        ctx.path(rel),
        {
            "movie_file_count": data.get("movie_file_count", data.get("file_count", 0)),
            "issue_count": data.get("issue_count", 0),
        },
        runtime_scope="not_routed",
        next_gate="Implement movie playback/handoff policy for hint and cutscene movies.",
    )


def missing_record(asset_family: str, summary_path: str) -> dict[str, object]:
    return base_record(
        asset_family,
        "missing_evidence",
        summary_path,
        {},
        runtime_scope="unknown",
        next_gate="Run the corresponding audit/export script.",
    )


def require_dict(value: object) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def sha256_file(path: Path | None) -> str | None:
    if path is None or not path.is_file():
        return None
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def capture_status_metrics(capture: dict[str, Any] | None) -> dict[str, object]:
    if not isinstance(capture, dict):
        return {
            "capture_status": "missing",
            "capture_issue_count": None,
            "dump_exists": False,
            "dump_status": "unknown",
            "audit_status": "unknown",
        }
    return {
        "capture_status": capture.get("status"),
        "capture_issue_count": capture.get("issue_count"),
        "dump_exists": capture.get("dump_exists"),
        "dump_status": capture.get("dump_status"),
        "audit_status": capture.get("audit_status"),
        "audit_issue_count": capture.get("audit_issue_count"),
        "expected_csab_name": capture.get("expected_csab_name"),
        "expected_animation_resource_path": capture.get("expected_animation_resource_path"),
        "expected_track_export": capture.get("expected_track_export"),
        "expected_frame": capture.get("expected_frame"),
        "soh_hash": capture.get("soh_hash"),
        "expected_soh_hash": capture.get("expected_soh_hash"),
        "package_hash": capture.get("package_hash"),
        "expected_package_hash": capture.get("expected_package_hash"),
        "route_package_hash": capture.get("route_package_hash"),
        "expected_route_package_hash": capture.get("expected_route_package_hash"),
    }


def summarize_runtime_safety(records: list[dict[str, object]]) -> dict[str, object]:
    runtime_safe_records = [
        record for record in records if str(record.get("status")) in RUNTIME_SAFE_STATUSES
    ]
    replacement_progress_records = [
        record for record in records if str(record.get("status")) in REPLACEMENT_PROGRESS_STATUSES
    ]
    blocker_records = [
        {
            "asset_family": record.get("asset_family"),
            "status": record.get("status"),
            "runtime_scope": record.get("runtime_scope"),
            "next_gate": record.get("next_gate"),
        }
        for record in replacement_progress_records
        if str(record.get("status")) not in RUNTIME_SAFE_STATUSES
    ]
    return {
        "runtime_safe_family_count": len(runtime_safe_records),
        "replacement_progress_family_count": len(replacement_progress_records),
        "replacement_blocker_family_count": len(blocker_records),
        "runtime_safe_families": [record.get("asset_family") for record in runtime_safe_records],
        "replacement_blocker_records": blocker_records,
    }


def build_next_action_queue(runtime_safety_summary: dict[str, object]) -> list[dict[str, object]]:
    blocker_records = runtime_safety_summary.get("replacement_blocker_records")
    if not isinstance(blocker_records, list):
        return []
    priority_by_status = {
        "decomp_frontier_open": 10,
        "runtime_capture_pending": 20,
        "runtime_blocked": 30,
        "runtime_contract_invalid": 40,
        "runtime_parity_invalid": 45,
        "offline_package_ready": 60,
        "offline_export_ready": 70,
        "offline_export_partial": 75,
        "fallback_only": 90,
    }
    queue: list[dict[str, object]] = []
    for record in blocker_records:
        if not isinstance(record, dict):
            continue
        status = str(record.get("status") or "")
        queue.append(
            {
                "priority": priority_by_status.get(status, 100),
                "asset_family": record.get("asset_family"),
                "status": status,
                "runtime_scope": record.get("runtime_scope"),
                "next_gate": record.get("next_gate"),
            }
        )
    return sorted(queue, key=lambda item: (int(item["priority"]), str(item.get("asset_family") or "")))


def frontier_open_function_samples(rows: object, root: Path, limit: int = 10) -> list[dict[str, object]]:
    if not isinstance(rows, list):
        return []
    open_rows = [
        row for row in rows
        if isinstance(row, dict) and str(row.get("frontier_status") or "") != "matched-c-inline-asm"
    ]
    open_rows.sort(key=lambda row: int(row.get("target_instruction_count", 0) or 0), reverse=True)
    samples: list[dict[str, object]] = []
    for row in open_rows[:limit]:
        materialized_output = row.get("materialized_output")
        probe_source = row.get("probe_source")
        best_split_source = row.get("best_split_source")
        materialized_output_exists = reference_exists(root, materialized_output)
        probe_source_exists = reference_exists(root, probe_source)
        best_split_source_exists = reference_exists(root, best_split_source)
        samples.append(
            {
                "entry": row.get("entry"),
                "oot3d_name": row.get("oot3d_name"),
                "n64_name": row.get("n64_name"),
                "port_file": row.get("port_file"),
                "frontier_status": row.get("frontier_status"),
                "target_instruction_count": row.get("target_instruction_count"),
                "materialized_output": materialized_output,
                "materialized_output_exists": materialized_output_exists,
                "probe_category": row.get("probe_category"),
                "probe_source": probe_source,
                "probe_source_exists": probe_source_exists,
                "best_split_source": best_split_source,
                "best_split_source_exists": best_split_source_exists,
                "all_referenced_artifacts_exist": (
                    materialized_output_exists
                    and probe_source_exists
                    and best_split_source_exists
                ),
                "next_action": row.get("next_action"),
            }
        )
    return samples


def reference_exists(root: Path, value: object) -> bool:
    if not isinstance(value, str) or not value.strip():
        return False
    return (root / strip_source_line_reference(value.strip())).is_file()


def strip_source_line_reference(value: str) -> str:
    head, separator, tail = value.rpartition(":")
    if not separator:
        return value
    normalized_tail = tail.replace("-", "")
    if normalized_tail.isdigit():
        return head
    return value
