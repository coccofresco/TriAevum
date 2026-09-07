from __future__ import annotations

from collections import Counter
import copy
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import zipfile

from .binary import ParseError
from .character_conversion_package import (
    bind_pose_skeleton_bones,
    sample_csab_track_channel,
    selected_draw_world_transforms,
)
from .n64_animation_reference_audit import (
    N64_ANIMATION_REFERENCE_AUDIT_FORMAT,
    bounds_from_points,
    find_target,
    indexed_pose_batch_records,
    n64_frame_world_bounds,
    n64_player_animation_data_records,
    n64_player_animation_records,
    normalize_path,
    numeric_summary,
    oot3d_animation_records,
    pose_bounds_metric,
    pose_sample_pairs,
    read_player_animation_raw_payload,
    resolve_pose_sample_path,
)
from .romfs_inventory import sorted_counter


LINK_CHILD_ANIMATION_SEMANTIC_MAPPING_FORMAT = "oot3d_link_child_animation_semantic_mapping_v1"
LINK_CHILD_ANIMATION_SEMANTIC_MATERIALIZATION_PLAN_FORMAT = (
    "oot3d_link_child_animation_semantic_materialization_plan_v1"
)
LINK_CHILD_ANIMATION_SEMANTIC_MATERIALIZED_TRACK_MANIFEST_FORMAT = (
    "oot3d_link_child_animation_semantic_materialized_track_manifest_v1"
)
LINK_CHILD_ANIMATION_SEMANTIC_MATERIALIZED_POSE_METRIC_FORMAT = (
    "oot3d_link_child_animation_semantic_materialized_pose_metric_v1"
)
LINK_CHILD_ANIMATION_SEMANTIC_OWNERSHIP_DERIVATIVE_PLAN_FORMAT = (
    "oot3d_link_child_animation_semantic_ownership_derivative_plan_v1"
)
LINK_CHILD_ANIMATION_SEMANTIC_OWNERSHIP_DERIVATIVE_FRONTIER_FORMAT = (
    "oot3d_link_child_animation_semantic_ownership_derivative_frontier_v1"
)
LINK_CHILD_ANIMATION_SEMANTIC_OWNERSHIP_REUSE_ARBITRATION_FORMAT = (
    "oot3d_link_child_animation_semantic_ownership_reuse_arbitration_v1"
)
LINK_CHILD_ANIMATION_SEMANTIC_ROUTE_PROVEN_OWNERSHIP_CALLSITE_FORMAT = (
    "oot3d_link_child_animation_semantic_route_proven_ownership_callsite_v1"
)
LINK_CHILD_ANIMATION_ANONYMOUS_NUMERIC_DIAGNOSTIC_PLAN_FORMAT = (
    "oot3d_link_child_animation_anonymous_numeric_diagnostic_plan_v1"
)
LINK_CHILD_ANIMATION_ANONYMOUS_NUMERIC_DIAGNOSTIC_SUMMARY_FORMAT = (
    "oot3d_link_child_animation_anonymous_numeric_diagnostic_summary_v1"
)
LINK_CHILD_ANIMATION_POSE_SOURCE_RISK_DIAGNOSTIC_PLAN_FORMAT = (
    "oot3d_link_child_animation_pose_source_risk_diagnostic_plan_v1"
)
LINK_CHILD_ANIMATION_POSE_SOURCE_RISK_DIAGNOSTIC_SUMMARY_FORMAT = (
    "oot3d_link_child_animation_pose_source_risk_diagnostic_summary_v1"
)
LINK_CHILD_ANIMATION_ROUTE_RUNTIME_DIAGNOSTIC_PLAN_FORMAT = (
    "oot3d_link_child_animation_route_runtime_diagnostic_plan_v1"
)
LINK_CHILD_ANIMATION_ROUTE_RUNTIME_DIAGNOSTIC_SUMMARY_FORMAT = (
    "oot3d_link_child_animation_route_runtime_diagnostic_summary_v1"
)
LINK_CHILD_ANIMATION_POSE_SOURCE_CALLSITE_CONTEXT_FORMAT = (
    "oot3d_link_child_animation_pose_source_callsite_context_v1"
)


def audit_link_child_animation_semantic_mapping(
    n64_reference_audit_path: Path,
    n64_player_animation_xml_path: Path,
    output_path: Path | None = None,
    *,
    n64_player_animation_data_xml_path: Path | None = None,
    csv_output_path: Path | None = None,
    candidate_validation_csv_output_path: Path | None = None,
    candidate_pose_metric_csv_output_path: Path | None = None,
    semantic_alias_promotion_csv_output_path: Path | None = None,
    semantic_alias_route_review_csv_output_path: Path | None = None,
    semantic_alias_resample_review_csv_output_path: Path | None = None,
    semantic_alias_temporal_bake_review_csv_output_path: Path | None = None,
    semantic_alias_bake_contract_csv_output_path: Path | None = None,
    semantic_alias_ambiguity_arbitration_csv_output_path: Path | None = None,
    semantic_alias_accepted_overlay_csv_output_path: Path | None = None,
    unresolved_near_candidate_csv_output_path: Path | None = None,
    unresolved_near_candidate_pose_metric_csv_output_path: Path | None = None,
    unresolved_near_candidate_bake_contract_csv_output_path: Path | None = None,
    anonymous_numeric_candidate_csv_output_path: Path | None = None,
    anonymous_numeric_pose_metric_csv_output_path: Path | None = None,
    semantic_resolution_contract_csv_output_path: Path | None = None,
    semantic_blocked_resolution_frontier_csv_output_path: Path | None = None,
    character_profile_o2r_path: Path | None = None,
    semantic_materialization_plan_output_path: Path | None = None,
    semantic_materialization_plan_csv_output_path: Path | None = None,
    semantic_materialized_track_output_dir: Path | None = None,
    semantic_materialized_track_manifest_output_path: Path | None = None,
    semantic_materialized_track_csv_output_path: Path | None = None,
    semantic_materialized_pose_metric_csv_output_path: Path | None = None,
    semantic_ownership_derivative_plan_output_path: Path | None = None,
    semantic_ownership_derivative_plan_csv_output_path: Path | None = None,
    semantic_ownership_derivative_track_output_dir: Path | None = None,
    semantic_ownership_derivative_track_manifest_output_path: Path | None = None,
    semantic_ownership_derivative_track_csv_output_path: Path | None = None,
    semantic_ownership_derivative_pose_metric_csv_output_path: Path | None = None,
    semantic_ownership_derivative_frontier_output_path: Path | None = None,
    semantic_ownership_derivative_frontier_csv_output_path: Path | None = None,
    semantic_ownership_reuse_arbitration_output_path: Path | None = None,
    semantic_ownership_reuse_arbitration_csv_output_path: Path | None = None,
    semantic_route_proven_ownership_callsite_output_path: Path | None = None,
    semantic_route_proven_ownership_callsite_csv_output_path: Path | None = None,
    source_search_roots: list[Path] | None = None,
    sample_limit: int = 50,
) -> dict[str, object]:
    if not n64_reference_audit_path.is_file():
        raise ParseError(f"{n64_reference_audit_path}: N64 reference audit not found")
    if not n64_player_animation_xml_path.is_file():
        raise ParseError(f"{n64_player_animation_xml_path}: N64 player animation XML not found")
    if n64_player_animation_data_xml_path is not None and not n64_player_animation_data_xml_path.is_file():
        raise ParseError(f"{n64_player_animation_data_xml_path}: N64 player animation data XML not found")

    reference_audit = load_json(n64_reference_audit_path)
    if reference_audit.get("format") != N64_ANIMATION_REFERENCE_AUDIT_FORMAT:
        raise ParseError(f"{n64_reference_audit_path}: unexpected N64 reference audit format")

    strip_prefixes = list(
        reference_audit.get("normalization_policy", {}).get("n64_strip_prefixes", [])
        if isinstance(reference_audit.get("normalization_policy"), dict)
        else []
    )
    data_records = (
        n64_player_animation_data_records(n64_player_animation_data_xml_path)
        if n64_player_animation_data_xml_path is not None
        else None
    )
    n64_records = n64_player_animation_records(
        n64_player_animation_xml_path,
        strip_prefixes,
        data_records,
    )
    oot3d_records = oot3d_records_from_reference_audit(reference_audit)
    oot3d_by_stem = group_oot3d_records_by_stem(oot3d_records)
    runtime_entries = runtime_mapping_entries(reference_audit)
    entries_by_n64_name = group_entries_by_n64_name(runtime_entries)
    entries_by_csab_name = group_entries_by_csab_name(runtime_entries)
    source_references = source_reference_index(n64_records, source_search_roots or [])

    records: list[dict[str, object]] = []
    status_counts: Counter[str] = Counter()
    timing_status_counts: Counter[str] = Counter()
    match_status_counts: Counter[str] = Counter()
    candidate_promotion_analysis_counts: Counter[str] = Counter()
    candidate_temporal_severity_counts: Counter[str] = Counter()
    candidate_promotion_family_counts: Counter[str] = Counter()
    candidate_ambiguity_analysis_counts: Counter[str] = Counter()
    unresolved_family_counts: Counter[str] = Counter()
    unresolved_route_kind_counts: Counter[str] = Counter()
    unresolved_route_analysis_counts: Counter[str] = Counter()
    unresolved_resolution_class_counts: Counter[str] = Counter()
    anonymous_numeric_table_context_counts: Counter[str] = Counter()
    named_unmapped_table_context_counts: Counter[str] = Counter()
    unresolved_family_samples: dict[str, list[dict[str, object]]] = {}
    for n64_record_index, n64_record in enumerate(n64_records):
        n64_name = str(n64_record.get("name") or "")
        candidates = entries_by_n64_name.get(n64_name, [])
        alias_candidates: list[dict[str, object]] = []
        if len(candidates) == 1:
            status = "mapped_to_single_csab"
        elif len(candidates) > 1:
            status = "ambiguous_multiple_csab"
        else:
            alias_candidates = alias_csab_candidates_for_n64(n64_record, oot3d_by_stem)
            if len(alias_candidates) == 1:
                status = "candidate_single_csab_unpromoted"
            elif len(alias_candidates) > 1:
                ambiguity = candidate_ambiguity_analysis(
                    alias_candidates,
                    entries_by_csab_name,
                    str(n64_record.get("stem") or ""),
                )
                if ambiguity.get("kind") == "diagnostic_prefer_non_free_variant":
                    preferred_csab_name = str(ambiguity.get("preferred_csab_name") or "")
                    preferred_candidates = [
                        candidate
                        for candidate in alias_candidates
                        if str(candidate.get("csab_name") or "") == preferred_csab_name
                    ]
                    if len(preferred_candidates) == 1:
                        preferred_candidates[0]["disambiguation_policy"] = ambiguity.get("kind")
                        preferred_candidates[0]["disambiguation_reason"] = ambiguity.get("reason")
                        preferred_candidates[0]["disambiguation_required_evidence"] = ambiguity.get("required_evidence")
                        alias_candidates = preferred_candidates
                        status = "candidate_single_csab_unpromoted"
                    else:
                        status = "candidate_ambiguous_csab_unpromoted"
                else:
                    status = "candidate_ambiguous_csab_unpromoted"
            else:
                status = "unmapped_no_candidate_csab"

        timing_status = str(candidates[0].get("timing_status") or "unmapped") if len(candidates) == 1 else status
        match_status = str(candidates[0].get("match_status") or "unmapped") if len(candidates) == 1 else status
        status_counts[status] += 1
        timing_status_counts[timing_status] += 1
        match_status_counts[match_status] += 1
        record = mapping_record(
            n64_record,
            candidates,
            alias_candidates,
            status,
            timing_status,
            match_status,
        )
        if status == "unmapped_no_candidate_csab":
            family = unresolved_family_for_n64_stem(str(n64_record.get("stem") or ""))
            unresolved_kind = unresolved_route_kind_for_n64_record(n64_record)
            unresolved_analysis = unresolved_route_analysis_for_n64_stem(str(n64_record.get("stem") or ""))
            resolution = unresolved_resolution_for_analysis(unresolved_analysis)
            record["unresolved_route_kind"] = unresolved_kind
            record["unresolved_route_analysis"] = unresolved_analysis
            record["unresolved_resolution_class"] = resolution["class"]
            record["unresolved_resolution_required_evidence"] = resolution["required_evidence"]
            record["unresolved_resolution_notes"] = resolution["notes"]
            record["source_reference_evidence"] = source_references.get(n64_name, source_reference_empty(n64_name))
            table_context = anonymous_numeric_table_context(n64_records, n64_record_index)
            record["table_context_evidence"] = table_context
            if unresolved_analysis == "anonymous_numeric_symbol":
                anonymous_numeric_table_context_counts[str(table_context.get("inferred_neighbor_family") or "unknown")] += 1
            else:
                named_unmapped_table_context_counts[str(table_context.get("inferred_neighbor_family") or "unknown")] += 1
            unresolved_family_counts[family] += 1
            unresolved_route_kind_counts[unresolved_kind] += 1
            unresolved_route_analysis_counts[unresolved_analysis] += 1
            unresolved_resolution_class_counts[str(resolution["class"])] += 1
            unresolved_family_samples.setdefault(family, [])
            if len(unresolved_family_samples[family]) < 5:
                unresolved_family_samples[family].append(record)
        if status == "candidate_single_csab_unpromoted":
            promotion = candidate_promotion_analysis(n64_record, alias_candidates[0])
            record["candidate_promotion_analysis"] = promotion
            candidate_promotion_analysis_counts[str(promotion.get("kind") or "unknown")] += 1
            candidate_temporal_severity_counts[str(promotion.get("temporal_severity") or "unknown")] += 1
            candidate_promotion_family_counts[str(promotion.get("family") or "unknown")] += 1
        if status == "candidate_ambiguous_csab_unpromoted":
            ambiguity = candidate_ambiguity_analysis(
                alias_candidates,
                entries_by_csab_name,
                str(n64_record.get("stem") or ""),
            )
            record["candidate_ambiguity_analysis"] = ambiguity
            candidate_ambiguity_analysis_counts[str(ambiguity.get("kind") or "unknown")] += 1
        records.append(record)

    mapped_count = status_counts.get("mapped_to_single_csab", 0)
    candidate_count = status_counts.get("candidate_single_csab_unpromoted", 0)
    candidate_ambiguous_count = status_counts.get("candidate_ambiguous_csab_unpromoted", 0)
    unmapped_count = status_counts.get("unmapped_no_candidate_csab", 0)
    ambiguous_count = status_counts.get("ambiguous_multiple_csab", 0)
    total_count = len(records)
    coverage = ratio(mapped_count, total_count)
    candidate_coverage = ratio(mapped_count + candidate_count, total_count)
    reference_status = reference_semantic_status(reference_audit)
    n64_payload_duplicate_aliases = n64_payload_duplicate_alias_analysis(
        reference_audit,
        records,
        sample_limit,
    )
    unresolved_near_candidate_queue = build_unresolved_near_candidate_queue(records, oot3d_records)
    anonymous_numeric_candidate_queue = build_anonymous_numeric_pose_candidate_queue(records, oot3d_records)
    candidate_pose_metric = candidate_alias_pose_metric_analysis(reference_audit, records, sample_limit)
    semantic_alias_promotion_ledger = semantic_alias_promotion_analysis(
        records,
        runtime_entries,
        sample_limit,
    )
    semantic_alias_route_review = semantic_alias_route_review_analysis(
        records,
        semantic_alias_promotion_ledger,
        source_references,
        n64_records,
        oot3d_records,
        sample_limit,
    )
    semantic_alias_resample_review = semantic_alias_resample_review_analysis(
        semantic_alias_promotion_ledger,
        source_references,
        n64_records,
        oot3d_records,
        sample_limit,
    )
    semantic_alias_temporal_bake_review = semantic_alias_temporal_bake_review_analysis(
        semantic_alias_promotion_ledger,
        source_references,
        n64_records,
        oot3d_records,
        sample_limit,
    )
    semantic_alias_accepted_overlay = semantic_alias_accepted_overlay_analysis(
        semantic_alias_route_review,
        semantic_alias_resample_review,
        mapped_count,
        total_count,
        sample_limit,
    )
    semantic_alias_bake_contract_ledger = semantic_alias_bake_contract_analysis(
        semantic_alias_temporal_bake_review,
        semantic_alias_accepted_overlay,
        mapped_count,
        total_count,
        sample_limit,
    )
    semantic_alias_ambiguity_arbitration = semantic_alias_ambiguity_arbitration_analysis(
        reference_audit,
        records,
        runtime_entries,
        semantic_alias_bake_contract_ledger,
        total_count,
        sample_limit,
    )
    unresolved_near_candidate_pose_metric = unresolved_near_candidate_pose_metric_analysis(
        reference_audit,
        records,
        unresolved_near_candidate_queue,
        runtime_entries,
        sample_limit,
    )
    anonymous_numeric_pose_metric = unresolved_near_candidate_pose_metric_analysis(
        reference_audit,
        records,
        anonymous_numeric_candidate_queue,
        runtime_entries,
        sample_limit,
        include_anonymous_numeric=True,
    )
    unresolved_near_candidate_bake_contract_ledger = unresolved_near_candidate_bake_contract_analysis(
        unresolved_near_candidate_pose_metric,
        promoted_runtime_mapping_count=len(runtime_entries),
        total_n64_animation_count=total_count,
        sample_limit=sample_limit,
    )
    semantic_resolution_contract = semantic_resolution_contract_analysis(
        records,
        semantic_alias_promotion_ledger,
        semantic_alias_route_review,
        semantic_alias_resample_review,
        semantic_alias_accepted_overlay,
        semantic_alias_bake_contract_ledger,
        semantic_alias_ambiguity_arbitration,
        unresolved_near_candidate_bake_contract_ledger,
        anonymous_numeric_pose_metric,
        n64_payload_duplicate_aliases,
        sample_limit,
    )
    semantic_blocked_resolution_frontier = semantic_blocked_resolution_frontier_analysis(
        semantic_resolution_contract,
        sample_limit,
    )
    semantic_ownership_derivative_plan = semantic_ownership_derivative_plan_analysis(
        semantic_blocked_resolution_frontier,
        character_profile_o2r_path,
        sample_limit,
    )
    semantic_ownership_derivative_track_manifest = semantic_materialized_track_manifest_analysis(
        semantic_ownership_derivative_plan,
        character_profile_o2r_path,
        semantic_ownership_derivative_track_output_dir,
        sample_limit,
    )
    semantic_ownership_derivative_pose_metric = semantic_materialized_pose_metric_analysis(
        reference_audit,
        semantic_ownership_derivative_track_manifest,
        character_profile_o2r_path,
        sample_limit,
    )
    semantic_ownership_derivative_frontier = semantic_ownership_derivative_frontier_analysis(
        semantic_blocked_resolution_frontier,
        semantic_ownership_derivative_plan,
        semantic_ownership_derivative_pose_metric,
        sample_limit,
    )
    semantic_ownership_reuse_arbitration = semantic_ownership_reuse_arbitration_analysis(
        semantic_ownership_derivative_frontier,
        semantic_resolution_contract,
        source_references,
        sample_limit,
    )
    semantic_route_proven_ownership_callsite = semantic_route_proven_ownership_callsite_analysis(
        semantic_ownership_reuse_arbitration,
        source_references,
        sample_limit,
    )
    semantic_materialization_plan = semantic_materialization_plan_analysis(
        semantic_resolution_contract,
        character_profile_o2r_path,
        sample_limit,
    )
    semantic_materialized_track_manifest = semantic_materialized_track_manifest_analysis(
        semantic_materialization_plan,
        character_profile_o2r_path,
        semantic_materialized_track_output_dir,
        sample_limit,
    )
    semantic_materialized_pose_metric = semantic_materialized_pose_metric_analysis(
        reference_audit,
        semantic_materialized_track_manifest,
        character_profile_o2r_path,
        sample_limit,
    )
    semantic_materialization_closure = semantic_materialization_closure_analysis(
        semantic_resolution_contract,
        semantic_materialization_plan,
        semantic_materialized_track_manifest,
        semantic_materialized_pose_metric,
        sample_limit,
    )
    semantic_resolution_contract["materialization_closure"] = semantic_materialization_closure
    semantic_resolution_contract["offline_materialized_source_count"] = semantic_materialization_closure.get(
        "materialized_contract_count",
        0,
    )
    semantic_resolution_contract["offline_unmaterialized_source_count"] = semantic_materialization_closure.get(
        "unmaterialized_contract_count",
        0,
    )
    semantic_resolution_contract["offline_materialization_status"] = semantic_materialization_closure.get("status")

    audit = {
        "format": LINK_CHILD_ANIMATION_SEMANTIC_MAPPING_FORMAT,
        "profile_id": reference_audit.get("profile_id"),
        "reference_version": reference_audit.get("reference_version"),
        "source_files": {
            "n64_reference_audit": str(n64_reference_audit_path),
            "n64_player_animation_xml": str(n64_player_animation_xml_path),
            "n64_player_animation_data_xml": (
                str(n64_player_animation_data_xml_path)
                if n64_player_animation_data_xml_path is not None
                else None
            ),
            "source_search_roots": [str(path) for path in source_search_roots or []],
            "character_profile_o2r": (
                str(character_profile_o2r_path) if character_profile_o2r_path is not None else None
            ),
        },
        "policy": {
            "mapping_direction": "N64 PlayerAnimation -> OOT3D CSAB",
            "mapped_definition": "one promoted runtime mapping entry names exactly one CSAB for the N64 PlayerAnimation",
            "unmapped_definition": "the N64 PlayerAnimation is decoded but has no promoted CSAB route yet",
            "ambiguity_policy": "do not choose automatically when more than one CSAB route exists",
        },
        "semantic_status": {
            **reference_status,
            "n64_to_csab_mapping_status": (
                "complete"
                if mapped_count == total_count and ambiguous_count == 0
                else "partial"
            ),
            "promotion_status": (
                "promotion_ready"
                if mapped_count == total_count and ambiguous_count == 0 and reference_status["blocking_issue_count"] == 0
                else "blocked"
            ),
        },
        "coverage": {
            "n64_player_animation_count": total_count,
            "promoted_mapped_n64_animation_count": mapped_count,
            "candidate_unpromoted_n64_animation_count": candidate_count,
            "candidate_ambiguous_n64_animation_count": candidate_ambiguous_count,
            "unmapped_n64_animation_count": unmapped_count,
            "promoted_or_candidate_n64_animation_count": mapped_count + candidate_count,
            "ambiguous_n64_animation_count": ambiguous_count,
            "mapped_n64_animation_percent": round(coverage * 100.0, 3),
            "promoted_or_candidate_n64_animation_percent": round(candidate_coverage * 100.0, 3),
            "unmapped_n64_animation_percent": round(ratio(unmapped_count, total_count) * 100.0, 3),
            "runtime_mapping_entry_count": len(runtime_entries),
            "unique_runtime_n64_name_count": len(entries_by_n64_name),
            "unique_mapped_csab_count": len(
                {
                    str(entry.get("csab_name") or "")
                    for entry in runtime_entries
                    if entry.get("csab_name")
                }
            ),
        },
        "status_counts": sorted_counter(status_counts),
        "match_status_counts": sorted_counter(match_status_counts),
        "timing_status_counts": sorted_counter(timing_status_counts),
        "candidate_promotion_analysis_counts": sorted_counter(candidate_promotion_analysis_counts),
        "candidate_temporal_severity_counts": sorted_counter(candidate_temporal_severity_counts),
        "candidate_promotion_family_counts": sorted_counter(candidate_promotion_family_counts),
        "candidate_ambiguity_analysis_counts": sorted_counter(candidate_ambiguity_analysis_counts),
        "unresolved_family_counts": sorted_counter(unresolved_family_counts),
        "unresolved_route_kind_counts": sorted_counter(unresolved_route_kind_counts),
        "unresolved_route_analysis_counts": sorted_counter(unresolved_route_analysis_counts),
        "unresolved_resolution_class_counts": sorted_counter(unresolved_resolution_class_counts),
        "anonymous_numeric_source_reference_summary": anonymous_numeric_source_reference_summary(records),
        "named_unmapped_source_reference_summary": named_unmapped_source_reference_summary(records),
        "anonymous_numeric_table_context_counts": sorted_counter(anonymous_numeric_table_context_counts),
        "named_unmapped_table_context_counts": sorted_counter(named_unmapped_table_context_counts),
        "candidate_validation_queue": candidate_validation_queue(records),
        "candidate_pose_metric": candidate_pose_metric,
        "semantic_alias_promotion_ledger": semantic_alias_promotion_ledger,
        "semantic_alias_route_review": semantic_alias_route_review,
        "semantic_alias_resample_review": semantic_alias_resample_review,
        "semantic_alias_temporal_bake_review": semantic_alias_temporal_bake_review,
        "semantic_alias_accepted_overlay": semantic_alias_accepted_overlay,
        "semantic_alias_bake_contract_ledger": semantic_alias_bake_contract_ledger,
        "semantic_alias_ambiguity_arbitration": semantic_alias_ambiguity_arbitration,
        "unresolved_near_candidate_queue_count": len(unresolved_near_candidate_queue),
        "unresolved_near_candidate_queue": unresolved_near_candidate_queue,
        "unresolved_near_candidate_pose_metric": unresolved_near_candidate_pose_metric,
        "unresolved_near_candidate_bake_contract_ledger": unresolved_near_candidate_bake_contract_ledger,
        "anonymous_numeric_pose_candidate_queue_count": len(anonymous_numeric_candidate_queue),
        "anonymous_numeric_pose_candidate_queue": anonymous_numeric_candidate_queue,
        "anonymous_numeric_pose_metric": anonymous_numeric_pose_metric,
        "n64_payload_duplicate_aliases": n64_payload_duplicate_aliases,
        "semantic_resolution_contract": semantic_resolution_contract,
        "semantic_blocked_resolution_frontier": semantic_blocked_resolution_frontier,
        "semantic_ownership_derivative_plan": semantic_ownership_derivative_plan,
        "semantic_ownership_derivative_track_manifest": semantic_ownership_derivative_track_manifest,
        "semantic_ownership_derivative_pose_metric": semantic_ownership_derivative_pose_metric,
        "semantic_ownership_derivative_frontier": semantic_ownership_derivative_frontier,
        "semantic_ownership_reuse_arbitration": semantic_ownership_reuse_arbitration,
        "semantic_route_proven_ownership_callsite": semantic_route_proven_ownership_callsite,
        "semantic_materialization_plan": semantic_materialization_plan,
        "semantic_materialized_track_manifest": semantic_materialized_track_manifest,
        "semantic_materialized_pose_metric": semantic_materialized_pose_metric,
        "semantic_materialization_closure": semantic_materialization_closure,
        "resolution_gates": resolution_gates(
            candidate_count,
            candidate_ambiguous_count,
            candidate_ambiguity_analysis_counts,
            unresolved_route_kind_counts,
            unresolved_route_analysis_counts,
            reference_status,
        ),
        "unresolved_family_samples": {
            key: unresolved_family_samples[key]
            for key in sorted(unresolved_family_samples)
        },
        "reference_summary": {
            "oot3d_animation_count": reference_audit.get("oot3d_target", {}).get("animation_count")
            if isinstance(reference_audit.get("oot3d_target"), dict)
            else None,
            "reference_candidate_matched_oot3d_count": reference_audit.get("candidate_mapping", {}).get("matched_oot3d_count")
            if isinstance(reference_audit.get("candidate_mapping"), dict)
            else None,
            "reference_candidate_unmatched_oot3d_count": reference_audit.get("candidate_mapping", {}).get("unmatched_oot3d_count")
            if isinstance(reference_audit.get("candidate_mapping"), dict)
            else None,
            "runtime_entry_count": len(runtime_entries),
        },
        "records": records,
        "sample_mapped": [record for record in records if record["status"] == "mapped_to_single_csab"][:sample_limit],
        "sample_candidate_unpromoted": [
            record
            for record in records
            if record["status"] == "candidate_single_csab_unpromoted"
        ][:sample_limit],
        "sample_unmapped": [record for record in records if record["status"] == "unmapped_no_candidate_csab"][:sample_limit],
        "sample_ambiguous": [record for record in records if record["status"] == "ambiguous_multiple_csab"][:sample_limit],
        "sample_candidate_ambiguous": [
            record
            for record in records
            if record["status"] == "candidate_ambiguous_csab_unpromoted"
        ][:sample_limit],
    }

    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8", newline="\n")
    if csv_output_path is not None:
        write_csv(csv_output_path, records)
    if candidate_validation_csv_output_path is not None:
        write_candidate_validation_csv(candidate_validation_csv_output_path, audit["candidate_validation_queue"])
    if candidate_pose_metric_csv_output_path is not None:
        write_candidate_pose_metric_csv(
            candidate_pose_metric_csv_output_path,
            candidate_pose_metric.get("queue", []) if isinstance(candidate_pose_metric, dict) else [],
        )
    if semantic_alias_promotion_csv_output_path is not None:
        write_semantic_alias_promotion_csv(
            semantic_alias_promotion_csv_output_path,
            semantic_alias_promotion_ledger.get("ledger", [])
            if isinstance(semantic_alias_promotion_ledger, dict)
            else [],
        )
    if semantic_alias_route_review_csv_output_path is not None:
        write_semantic_alias_route_review_csv(
            semantic_alias_route_review_csv_output_path,
            semantic_alias_route_review.get("rows", [])
            if isinstance(semantic_alias_route_review, dict)
            else [],
        )
    if semantic_alias_resample_review_csv_output_path is not None:
        write_semantic_alias_resample_review_csv(
            semantic_alias_resample_review_csv_output_path,
            semantic_alias_resample_review.get("rows", [])
            if isinstance(semantic_alias_resample_review, dict)
            else [],
        )
    if semantic_alias_temporal_bake_review_csv_output_path is not None:
        write_semantic_alias_temporal_bake_review_csv(
            semantic_alias_temporal_bake_review_csv_output_path,
            semantic_alias_temporal_bake_review.get("rows", [])
            if isinstance(semantic_alias_temporal_bake_review, dict)
            else [],
        )
    if semantic_alias_bake_contract_csv_output_path is not None:
        write_semantic_alias_bake_contract_csv(
            semantic_alias_bake_contract_csv_output_path,
            semantic_alias_bake_contract_ledger.get("contracts", [])
            if isinstance(semantic_alias_bake_contract_ledger, dict)
            else [],
        )
    if semantic_alias_ambiguity_arbitration_csv_output_path is not None:
        write_semantic_alias_ambiguity_arbitration_csv(
            semantic_alias_ambiguity_arbitration_csv_output_path,
            semantic_alias_ambiguity_arbitration.get("candidate_rows", [])
            if isinstance(semantic_alias_ambiguity_arbitration, dict)
            else [],
        )
    if semantic_alias_accepted_overlay_csv_output_path is not None:
        write_semantic_alias_accepted_overlay_csv(
            semantic_alias_accepted_overlay_csv_output_path,
            semantic_alias_accepted_overlay.get("accepted_overlay_entries", [])
            if isinstance(semantic_alias_accepted_overlay, dict)
            else [],
        )
    if unresolved_near_candidate_csv_output_path is not None:
        write_unresolved_near_candidate_csv(
            unresolved_near_candidate_csv_output_path,
            unresolved_near_candidate_queue,
        )
    if unresolved_near_candidate_pose_metric_csv_output_path is not None:
        write_unresolved_near_candidate_pose_metric_csv(
            unresolved_near_candidate_pose_metric_csv_output_path,
            unresolved_near_candidate_pose_metric.get("candidate_rows", [])
            if isinstance(unresolved_near_candidate_pose_metric, dict)
            else [],
        )
    if unresolved_near_candidate_bake_contract_csv_output_path is not None:
        write_unresolved_near_candidate_bake_contract_csv(
            unresolved_near_candidate_bake_contract_csv_output_path,
            unresolved_near_candidate_bake_contract_ledger.get("contracts", [])
            if isinstance(unresolved_near_candidate_bake_contract_ledger, dict)
            else [],
        )
    if anonymous_numeric_candidate_csv_output_path is not None:
        write_unresolved_near_candidate_csv(
            anonymous_numeric_candidate_csv_output_path,
            anonymous_numeric_candidate_queue,
        )
    if anonymous_numeric_pose_metric_csv_output_path is not None:
        write_unresolved_near_candidate_pose_metric_csv(
            anonymous_numeric_pose_metric_csv_output_path,
            anonymous_numeric_pose_metric.get("candidate_rows", [])
            if isinstance(anonymous_numeric_pose_metric, dict)
            else [],
        )
    if semantic_resolution_contract_csv_output_path is not None:
        write_semantic_resolution_contract_csv(
            semantic_resolution_contract_csv_output_path,
            semantic_resolution_contract.get("rows", [])
            if isinstance(semantic_resolution_contract, dict)
            else [],
        )
    if semantic_blocked_resolution_frontier_csv_output_path is not None:
        write_semantic_blocked_resolution_frontier_csv(
            semantic_blocked_resolution_frontier_csv_output_path,
            semantic_blocked_resolution_frontier.get("rows", [])
            if isinstance(semantic_blocked_resolution_frontier, dict)
            else [],
        )
    if semantic_materialization_plan_output_path is not None:
        semantic_materialization_plan_output_path.parent.mkdir(parents=True, exist_ok=True)
        semantic_materialization_plan_output_path.write_text(
            json.dumps(semantic_materialization_plan, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    if semantic_materialization_plan_csv_output_path is not None:
        write_semantic_materialization_plan_csv(
            semantic_materialization_plan_csv_output_path,
            semantic_materialization_plan.get("rows", [])
            if isinstance(semantic_materialization_plan, dict)
            else [],
        )
    if semantic_materialized_track_manifest_output_path is not None:
        semantic_materialized_track_manifest_output_path.parent.mkdir(parents=True, exist_ok=True)
        semantic_materialized_track_manifest_output_path.write_text(
            json.dumps(semantic_materialized_track_manifest, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    if semantic_materialized_track_csv_output_path is not None:
        write_semantic_materialized_track_manifest_csv(
            semantic_materialized_track_csv_output_path,
            semantic_materialized_track_manifest.get("rows", [])
            if isinstance(semantic_materialized_track_manifest, dict)
            else [],
        )
    if semantic_materialized_pose_metric_csv_output_path is not None:
        write_semantic_materialized_pose_metric_csv(
            semantic_materialized_pose_metric_csv_output_path,
            semantic_materialized_pose_metric.get("rows", [])
            if isinstance(semantic_materialized_pose_metric, dict)
            else [],
        )
    if semantic_ownership_derivative_plan_output_path is not None:
        semantic_ownership_derivative_plan_output_path.parent.mkdir(parents=True, exist_ok=True)
        semantic_ownership_derivative_plan_output_path.write_text(
            json.dumps(semantic_ownership_derivative_plan, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    if semantic_ownership_derivative_plan_csv_output_path is not None:
        write_semantic_materialization_plan_csv(
            semantic_ownership_derivative_plan_csv_output_path,
            semantic_ownership_derivative_plan.get("rows", [])
            if isinstance(semantic_ownership_derivative_plan, dict)
            else [],
        )
    if semantic_ownership_derivative_track_manifest_output_path is not None:
        semantic_ownership_derivative_track_manifest_output_path.parent.mkdir(parents=True, exist_ok=True)
        semantic_ownership_derivative_track_manifest_output_path.write_text(
            json.dumps(semantic_ownership_derivative_track_manifest, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    if semantic_ownership_derivative_track_csv_output_path is not None:
        write_semantic_materialized_track_manifest_csv(
            semantic_ownership_derivative_track_csv_output_path,
            semantic_ownership_derivative_track_manifest.get("rows", [])
            if isinstance(semantic_ownership_derivative_track_manifest, dict)
            else [],
        )
    if semantic_ownership_derivative_pose_metric_csv_output_path is not None:
        write_semantic_materialized_pose_metric_csv(
            semantic_ownership_derivative_pose_metric_csv_output_path,
            semantic_ownership_derivative_pose_metric.get("rows", [])
            if isinstance(semantic_ownership_derivative_pose_metric, dict)
            else [],
        )
    if semantic_ownership_derivative_frontier_output_path is not None:
        semantic_ownership_derivative_frontier_output_path.parent.mkdir(parents=True, exist_ok=True)
        semantic_ownership_derivative_frontier_output_path.write_text(
            json.dumps(semantic_ownership_derivative_frontier, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    if semantic_ownership_derivative_frontier_csv_output_path is not None:
        write_semantic_ownership_derivative_frontier_csv(
            semantic_ownership_derivative_frontier_csv_output_path,
            semantic_ownership_derivative_frontier.get("rows", [])
            if isinstance(semantic_ownership_derivative_frontier, dict)
            else [],
        )
    if semantic_ownership_reuse_arbitration_output_path is not None:
        semantic_ownership_reuse_arbitration_output_path.parent.mkdir(parents=True, exist_ok=True)
        semantic_ownership_reuse_arbitration_output_path.write_text(
            json.dumps(semantic_ownership_reuse_arbitration, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    if semantic_ownership_reuse_arbitration_csv_output_path is not None:
        write_semantic_ownership_reuse_arbitration_csv(
            semantic_ownership_reuse_arbitration_csv_output_path,
            semantic_ownership_reuse_arbitration.get("rows", [])
            if isinstance(semantic_ownership_reuse_arbitration, dict)
            else [],
        )
    if semantic_route_proven_ownership_callsite_output_path is not None:
        semantic_route_proven_ownership_callsite_output_path.parent.mkdir(parents=True, exist_ok=True)
        semantic_route_proven_ownership_callsite_output_path.write_text(
            json.dumps(semantic_route_proven_ownership_callsite, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    if semantic_route_proven_ownership_callsite_csv_output_path is not None:
        write_semantic_route_proven_ownership_callsite_csv(
            semantic_route_proven_ownership_callsite_csv_output_path,
            semantic_route_proven_ownership_callsite.get("rows", [])
            if isinstance(semantic_route_proven_ownership_callsite, dict)
            else [],
        )
    return audit


def resolution_gates(
    candidate_count: int,
    candidate_ambiguous_count: int,
    candidate_ambiguity_analysis_counts: Counter[str],
    unresolved_route_kind_counts: Counter[str],
    unresolved_route_analysis_counts: Counter[str],
    reference_status: dict[str, object],
) -> list[dict[str, object]]:
    named_unresolved = unresolved_route_kind_counts.get(
        "named_n64_animation_without_oot3d_csab_alias_candidate",
        0,
    )
    numeric_unresolved = unresolved_route_kind_counts.get(
        "anonymous_numeric_n64_symbol_requires_pose_or_callsite_identity",
        0,
    )
    gates: list[dict[str, object]] = []
    if candidate_count:
        gates.append(
            {
                "gate": "promote_single_csab_alias_candidates",
                "record_count": candidate_count,
                "required_evidence": "pose-metric thresholds plus runtime route review before candidate aliases become promoted mappings",
            }
        )
    if candidate_ambiguous_count:
        diagnostic_preferred = candidate_ambiguity_analysis_counts.get("diagnostic_prefer_non_free_variant", 0)
        semantic_ambiguous = candidate_ambiguity_analysis_counts.get("semantic_ambiguity_requires_pose_or_callsite", 0)
        promoted_reuse_ambiguous = candidate_ambiguity_analysis_counts.get(
            "semantic_ambiguity_reuses_promoted_adjacent_routes",
            0,
        )
        gates.append(
            {
                "gate": "disambiguate_multi_csab_alias_candidates",
                "record_count": candidate_ambiguous_count,
                "diagnostic_prefer_non_free_variant_count": diagnostic_preferred,
                "semantic_ambiguity_requires_pose_or_callsite_count": semantic_ambiguous,
                "semantic_ambiguity_reuses_promoted_adjacent_routes_count": promoted_reuse_ambiguous,
                "required_evidence": (
                    "confirm diagnostic non-free preferences with pose/runtime route evidence, "
                    "and resolve remaining semantic ambiguities with pose metric, player callsite context, "
                    "or proof that OOT3D intentionally reuses an adjacent promoted route"
                ),
            }
        )
    if named_unresolved:
        gates.append(
            {
                "gate": "resolve_named_n64_rows_without_csab_alias",
                "record_count": named_unresolved,
                "analysis_counts": sorted_counter(
                    Counter(
                        {
                            key: value
                            for key, value in unresolved_route_analysis_counts.items()
                            if key != "anonymous_numeric_symbol"
                        }
                    )
                ),
                "required_evidence": "new semantic alias, proof of OOT3D omission, or fallback bake source",
            }
        )
    if numeric_unresolved:
        gates.append(
            {
                "gate": "identify_anonymous_numeric_n64_symbols",
                "record_count": numeric_unresolved,
                "required_evidence": "player callsite, action-state route, or pose identity for anonymous numeric animation symbols",
            }
        )
    if reference_status.get("pose_metric_acceptance_status") == "requires_thresholds":
        gates.append(
            {
                "gate": "promote_pose_metric_thresholds",
                "record_count": 1,
                "required_evidence": "accepted OOT3D/N64 pose metric thresholds for automated route promotion",
            }
        )
    return gates


def candidate_alias_pose_metric_analysis(
    reference_audit: dict[str, object],
    records: list[dict[str, object]],
    sample_limit: int,
) -> dict[str, object]:
    candidate_records = [
        record
        for record in records
        if record.get("status") == "candidate_single_csab_unpromoted"
    ]
    source_files = reference_audit.get("source_files") if isinstance(reference_audit.get("source_files"), dict) else {}
    target = reference_audit.get("oot3d_target") if isinstance(reference_audit.get("oot3d_target"), dict) else {}
    n64_reference = (
        reference_audit.get("n64_reference")
        if isinstance(reference_audit.get("n64_reference"), dict)
        else {}
    )
    skeleton_pose_reference = (
        n64_reference.get("skeleton_pose_reference")
        if isinstance(n64_reference.get("skeleton_pose_reference"), dict)
        else {}
    )
    pose_batch_manifest_path = optional_existing_path(source_files.get("oot3d_pose_batch_manifest"))
    n64_base_o2r_path = optional_existing_path(source_files.get("n64_base_o2r"))
    if pose_batch_manifest_path is None or n64_base_o2r_path is None:
        return {
            "status": "missing_inputs",
            "candidate_count": len(candidate_records),
            "queue_count": 0,
            "queue": [],
            "issue_count": 1,
            "sample_issues": [
                {
                    "type": "missing_pose_metric_inputs",
                    "oot3d_pose_batch_manifest": source_files.get("oot3d_pose_batch_manifest"),
                    "n64_base_o2r": source_files.get("n64_base_o2r"),
                }
            ],
        }
    if skeleton_pose_reference.get("status") != "decoded":
        return {
            "status": "missing_n64_skeleton_pose_reference",
            "candidate_count": len(candidate_records),
            "queue_count": 0,
            "queue": [],
            "issue_count": 1,
            "sample_issues": [
                {
                    "type": "missing_n64_skeleton_pose_reference",
                    "skeleton_pose_reference_status": skeleton_pose_reference.get("status"),
                }
            ],
        }

    pose_batch = load_json(pose_batch_manifest_path)
    pose_records = indexed_pose_batch_records(
        pose_batch,
        str(target.get("archive_path") or ""),
        str(target.get("target_cmb_name") or ""),
    )
    reference_envelope = reference_pose_metric_envelope(reference_audit.get("pose_error_metric"))
    queue: list[dict[str, object]] = []
    issues: list[dict[str, object]] = []
    status_counts: Counter[str] = Counter()
    envelope_counts: Counter[str] = Counter()
    temporal_severity_counts: Counter[str] = Counter()
    promotion_review_class_counts: Counter[str] = Counter()
    normalized_extent_mean_abs_deltas: list[float] = []
    normalized_extent_max_abs_deltas: list[float] = []
    center_delta_normalized_values: list[float] = []
    sample_pair_count = 0
    measured_pair_count = 0

    try:
        with zipfile.ZipFile(n64_base_o2r_path) as archive:
            for record in candidate_records:
                row, pair_metrics, row_issues = candidate_alias_pose_metric_record(
                    archive,
                    pose_batch_manifest_path,
                    pose_batch,
                    pose_records,
                    skeleton_pose_reference,
                    reference_envelope,
                    record,
                )
                record["candidate_pose_metric_summary"] = row
                queue.append(row)
                if row_issues:
                    issues.extend(row_issues)
                status_counts[str(row.get("pose_metric_status") or "unknown")] += 1
                envelope_counts[str(row.get("reference_envelope_status") or "unknown")] += 1
                temporal_severity_counts[str(row.get("temporal_severity") or "unknown")] += 1
                promotion_review_class_counts[str(row.get("promotion_review_class") or "unknown")] += 1
                sample_pair_count += int(row.get("sample_pair_count") or 0)
                measured_pair_count += int(row.get("measured_pair_count") or 0)
                for metric in pair_metrics:
                    normalized_extent_mean_abs_deltas.append(float(metric["normalized_extent_mean_abs_delta"]))
                    normalized_extent_max_abs_deltas.append(float(metric["normalized_extent_max_abs_delta"]))
                    center_delta_normalized_values.append(float(metric["center_delta_normalized"]))
    except zipfile.BadZipFile as exc:
        return {
            "status": "invalid_n64_base_o2r",
            "candidate_count": len(candidate_records),
            "queue_count": 0,
            "queue": [],
            "issue_count": 1,
            "sample_issues": [{"type": "invalid_n64_base_o2r", "message": str(exc)}],
        }

    sorted_queue = sorted(
        queue,
        key=lambda item: (
            candidate_pose_temporal_priority(str(item.get("temporal_severity") or "")),
            candidate_pose_envelope_priority(str(item.get("reference_envelope_status") or "")),
            float(item.get("normalized_extent_max_abs_delta_max") or 999.0),
            float(item.get("center_delta_normalized_max") or 999.0),
            str(item.get("family") or ""),
            int(item.get("index") or 0),
        ),
    )
    return {
        "status": "measured" if measured_pair_count > 0 else "not_measured",
        "policy": {
            "scope": "diagnostic metric for unpromoted single-CSAB semantic alias candidates",
            "metric_source": "same normalized skeleton-bounds pose metric used by the N64 reference audit",
            "promotion_policy": (
                "does not promote mappings by itself; same-frame candidates inside the "
                "reference envelope become priority promotion-review rows"
            ),
        },
        "candidate_count": len(candidate_records),
        "queue_count": len(sorted_queue),
        "sample_pair_count": sample_pair_count,
        "measured_pair_count": measured_pair_count,
        "issue_count": len(issues),
        "status_counts": sorted_counter(status_counts),
        "reference_envelope_status_counts": sorted_counter(envelope_counts),
        "temporal_severity_counts": sorted_counter(temporal_severity_counts),
        "promotion_review_class_counts": sorted_counter(promotion_review_class_counts),
        "reference_envelope": reference_envelope,
        "normalized_extent_mean_abs_delta": numeric_summary(normalized_extent_mean_abs_deltas),
        "normalized_extent_max_abs_delta": numeric_summary(normalized_extent_max_abs_deltas),
        "center_delta_normalized": numeric_summary(center_delta_normalized_values),
        "sample_issues": issues[:sample_limit],
        "queue": sorted_queue,
    }


def optional_existing_path(value: object) -> Path | None:
    if not value:
        return None
    path = Path(str(value))
    return path if path.is_file() else None


def reference_pose_metric_envelope(metric: object) -> dict[str, object]:
    if not isinstance(metric, dict):
        return {
            "status": "missing_reference_pose_metric",
            "normalized_extent_mean_abs_delta_max": None,
            "normalized_extent_max_abs_delta_max": None,
            "center_delta_normalized_max": None,
        }
    return {
        "status": metric.get("status"),
        "acceptance_status": metric.get("acceptance_status"),
        "compared_match_count": metric.get("compared_match_count"),
        "measured_pair_count": metric.get("measured_pair_count"),
        "normalized_extent_mean_abs_delta_max": metric_value(
            metric.get("normalized_extent_mean_abs_delta"), "max"
        ),
        "normalized_extent_max_abs_delta_max": metric_value(
            metric.get("normalized_extent_max_abs_delta"), "max"
        ),
        "center_delta_normalized_max": metric_value(metric.get("center_delta_normalized"), "max"),
    }


def candidate_alias_pose_metric_record(
    archive: zipfile.ZipFile,
    pose_batch_manifest_path: Path,
    pose_batch: dict[str, object],
    pose_records: dict[str, dict[str, object]],
    skeleton_pose_reference: dict[str, object],
    reference_envelope: dict[str, object],
    record: dict[str, object],
) -> tuple[dict[str, object], list[dict[str, object]], list[dict[str, object]]]:
    analysis = (
        record.get("candidate_promotion_analysis")
        if isinstance(record.get("candidate_promotion_analysis"), dict)
        else {}
    )
    base = {
        "index": record.get("index"),
        "n64_name": record.get("n64_name"),
        "n64_stem": record.get("n64_stem"),
        "csab_name": record.get("csab_name"),
        "oot3d_stem": record.get("oot3d_stem"),
        "family": analysis.get("family"),
        "temporal_severity": analysis.get("temporal_severity"),
        "n64_frame_count": record.get("n64_frame_count"),
        "oot3d_frame_slot_count": record.get("oot3d_frame_slot_count"),
        "frame_delta_oot3d_minus_n64": analysis.get("frame_delta_oot3d_minus_n64"),
        "duration_ratio_oot3d_per_n64": analysis.get("duration_ratio_oot3d_per_n64"),
    }
    issues: list[dict[str, object]] = []
    pose_record = pose_records.get(normalize_path(record.get("csab_name")))
    if pose_record is None:
        return candidate_pose_empty_row(base, "missing_oot3d_pose_record"), [], [
            {**base, "type": "missing_oot3d_pose_record"}
        ]
    pose_sample_path = resolve_pose_sample_path(pose_batch_manifest_path, pose_batch, pose_record)
    if pose_sample_path is None or not pose_sample_path.is_file():
        return candidate_pose_empty_row(base, "missing_oot3d_pose_sample_file"), [], [
            {
                **base,
                "type": "missing_oot3d_pose_sample_file",
                "pose_sample_export": pose_record.get("pose_sample_export"),
            }
        ]
    raw_payload = read_player_animation_raw_payload(
        archive,
        {
            "data_name": record.get("n64_data_name"),
            "name": record.get("n64_name"),
            "frame_count": record.get("n64_frame_count"),
        },
    )
    if raw_payload is None:
        return candidate_pose_empty_row(base, "missing_n64_payload"), [], [
            {**base, "type": "missing_n64_payload", "data_name": record.get("n64_data_name")}
        ]

    pose_sample = load_json(pose_sample_path)
    oot3d_record = {
        "csab_name": record.get("csab_name"),
        "stem": record.get("oot3d_stem"),
        "normalized_key": record.get("oot3d_normalized_key"),
        "frame_slot_count": record.get("oot3d_frame_slot_count"),
    }
    n64_candidate = {
        "name": record.get("n64_name"),
        "data_name": record.get("n64_data_name"),
        "frame_count": record.get("n64_frame_count"),
    }
    pair_metrics: list[dict[str, object]] = []
    sample_pair_count = 0
    for pair in pose_sample_pairs(oot3d_record, pose_record, pose_sample, n64_candidate, raw_payload):
        sample_pair_count += 1
        oot3d_bounds = pair.get("oot3d_position_bounds")
        n64_frame = int_or_none_local(pair.get("n64_frame"))
        if n64_frame is None or oot3d_bounds is None:
            issues.append(
                {
                    **base,
                    "type": "incomplete_candidate_pose_metric_pair",
                    "oot3d_frame": pair.get("oot3d_frame"),
                    "n64_frame": pair.get("n64_frame"),
                }
            )
            continue
        n64_bounds = n64_frame_world_bounds(raw_payload, n64_frame, skeleton_pose_reference)
        if n64_bounds is None:
            issues.append(
                {
                    **base,
                    "type": "missing_candidate_n64_skeleton_bounds",
                    "n64_frame": n64_frame,
                }
            )
            continue
        metric = pose_bounds_metric(oot3d_bounds, n64_bounds["bounds"])
        if metric is None:
            issues.append(
                {
                    **base,
                    "type": "invalid_candidate_pose_bounds_metric",
                    "oot3d_frame": pair.get("oot3d_frame"),
                    "n64_frame": n64_frame,
                }
            )
            continue
        pair_metrics.append(
            {
                "normalized_t": pair.get("normalized_t"),
                "oot3d_frame": pair.get("oot3d_frame"),
                "n64_frame": n64_frame,
                "normalized_extent_mean_abs_delta": metric["normalized_extent_mean_abs_delta"],
                "normalized_extent_max_abs_delta": metric["normalized_extent_max_abs_delta"],
                "center_delta_normalized": metric["center_delta_normalized"],
            }
        )

    mean_summary = numeric_summary([float(metric["normalized_extent_mean_abs_delta"]) for metric in pair_metrics])
    max_summary = numeric_summary([float(metric["normalized_extent_max_abs_delta"]) for metric in pair_metrics])
    center_summary = numeric_summary([float(metric["center_delta_normalized"]) for metric in pair_metrics])
    envelope_status = candidate_reference_envelope_status(mean_summary, max_summary, center_summary, reference_envelope)
    status = "measured" if pair_metrics and not issues else ("incomplete" if pair_metrics else "not_measured")
    return {
        **base,
        "pose_metric_status": status,
        "sample_pair_count": sample_pair_count,
        "measured_pair_count": len(pair_metrics),
        "issue_count": len(issues),
        "reference_envelope_status": envelope_status,
        "promotion_review_class": candidate_pose_promotion_review_class(
            str(analysis.get("temporal_severity") or ""),
            envelope_status,
            status,
        ),
        "normalized_extent_mean_abs_delta_avg": mean_summary.get("avg"),
        "normalized_extent_mean_abs_delta_max": mean_summary.get("max"),
        "normalized_extent_max_abs_delta_avg": max_summary.get("avg"),
        "normalized_extent_max_abs_delta_max": max_summary.get("max"),
        "center_delta_normalized_avg": center_summary.get("avg"),
        "center_delta_normalized_max": center_summary.get("max"),
    }, pair_metrics, issues


def candidate_pose_empty_row(base: dict[str, object], status: str) -> dict[str, object]:
    return {
        **base,
        "pose_metric_status": status,
        "sample_pair_count": 0,
        "measured_pair_count": 0,
        "issue_count": 1,
        "reference_envelope_status": "not_measured",
        "promotion_review_class": "not_ready_for_pose_review",
        "normalized_extent_mean_abs_delta_avg": None,
        "normalized_extent_mean_abs_delta_max": None,
        "normalized_extent_max_abs_delta_avg": None,
        "normalized_extent_max_abs_delta_max": None,
        "center_delta_normalized_avg": None,
        "center_delta_normalized_max": None,
    }


def metric_value(summary: object, key: str) -> float | None:
    if not isinstance(summary, dict):
        return None
    value = summary.get(key)
    return float(value) if isinstance(value, (int, float)) else None


def round_metric(value: float | None) -> float | None:
    if value is None:
        return None
    return round(float(value), 6)


def candidate_reference_envelope_status(
    mean_summary: dict[str, object],
    max_summary: dict[str, object],
    center_summary: dict[str, object],
    reference_envelope: dict[str, object],
) -> str:
    if int(mean_summary.get("count") or 0) <= 0:
        return "not_measured"
    thresholds = {
        "normalized_extent_mean_abs_delta_max": metric_value(
            reference_envelope, "normalized_extent_mean_abs_delta_max"
        ),
        "normalized_extent_max_abs_delta_max": metric_value(
            reference_envelope, "normalized_extent_max_abs_delta_max"
        ),
        "center_delta_normalized_max": metric_value(reference_envelope, "center_delta_normalized_max"),
    }
    if any(value is None for value in thresholds.values()):
        return "missing_reference_envelope"
    if (
        float(mean_summary.get("max") or 0.0) <= float(thresholds["normalized_extent_mean_abs_delta_max"])
        and float(max_summary.get("max") or 0.0) <= float(thresholds["normalized_extent_max_abs_delta_max"])
        and float(center_summary.get("max") or 0.0) <= float(thresholds["center_delta_normalized_max"])
    ):
        return "inside_reference_envelope"
    return "outside_reference_envelope"


def candidate_pose_promotion_review_class(temporal_severity: str, envelope_status: str, metric_status: str) -> str:
    if metric_status != "measured":
        return "not_ready_for_pose_review"
    if envelope_status != "inside_reference_envelope":
        return "pose_metric_outside_reference_envelope"
    if temporal_severity == "same_frame_count":
        return "same_frame_inside_reference_envelope"
    if temporal_severity == "small_delta":
        return "small_delta_inside_reference_envelope_requires_resample_policy"
    return "temporal_bake_required_inside_reference_envelope"


def candidate_pose_temporal_priority(severity: str) -> int:
    return {
        "same_frame_count": 0,
        "small_delta": 1,
        "moderate_delta": 2,
        "large_delta_or_ratio": 3,
    }.get(severity, 9)


def candidate_pose_envelope_priority(status: str) -> int:
    return {
        "inside_reference_envelope": 0,
        "outside_reference_envelope": 1,
        "missing_reference_envelope": 2,
        "not_measured": 3,
    }.get(status, 9)


def int_or_none_local(value: object) -> int | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError:
            return None
    return None


def semantic_alias_promotion_analysis(
    records: list[dict[str, object]],
    runtime_entries: list[dict[str, object]],
    sample_limit: int,
) -> dict[str, object]:
    promoted_by_csab = group_entries_by_csab_name(runtime_entries)
    candidate_by_csab = group_mapping_records_by_csab_name(records, "candidate_single_csab_unpromoted")
    ledger: list[dict[str, object]] = []
    class_counts: Counter[str] = Counter()
    clean_ready_entries: list[dict[str, object]] = []
    blocked_reuse_entries: list[dict[str, object]] = []
    blocked_candidate_reuse_entries: list[dict[str, object]] = []
    for record in records:
        if record.get("status") != "candidate_single_csab_unpromoted":
            continue
        pose_summary = (
            record.get("candidate_pose_metric_summary")
            if isinstance(record.get("candidate_pose_metric_summary"), dict)
            else {}
        )
        csab_name = str(record.get("csab_name") or "")
        runtime_collisions = [
            entry
            for entry in promoted_by_csab.get(csab_name, [])
            if str(entry.get("n64_name") or "") != str(record.get("n64_name") or "")
        ]
        candidate_collisions = [
            entry
            for entry in candidate_by_csab.get(csab_name, [])
            if str(entry.get("n64_name") or "") != str(record.get("n64_name") or "")
        ]
        promotion_class = semantic_alias_promotion_class(
            record,
            pose_summary,
            runtime_collisions,
            candidate_collisions,
        )
        row = {
            "index": record.get("index"),
            "n64_name": record.get("n64_name"),
            "n64_stem": record.get("n64_stem"),
            "n64_data_name": record.get("n64_data_name"),
            "csab_name": record.get("csab_name"),
            "oot3d_stem": record.get("oot3d_stem"),
            "alias_stem": record.get("alias_stem"),
            "alias_policy": record.get("alias_policy"),
            "n64_frame_count": record.get("n64_frame_count"),
            "oot3d_frame_slot_count": record.get("oot3d_frame_slot_count"),
            "frame_delta_oot3d_minus_n64": pose_summary.get("frame_delta_oot3d_minus_n64"),
            "duration_ratio_oot3d_per_n64": pose_summary.get("duration_ratio_oot3d_per_n64"),
            "pose_metric_status": pose_summary.get("pose_metric_status"),
            "reference_envelope_status": pose_summary.get("reference_envelope_status"),
            "promotion_review_class": pose_summary.get("promotion_review_class"),
            "semantic_alias_promotion_class": promotion_class,
            "runtime_csab_collision_count": len(runtime_collisions),
            "runtime_csab_collision_n64_names": ";".join(
                str(entry.get("n64_name") or "") for entry in runtime_collisions
            ),
            "candidate_csab_collision_count": len(candidate_collisions),
            "candidate_csab_collision_n64_names": ";".join(
                str(entry.get("n64_name") or "") for entry in candidate_collisions
            ),
            "normalized_extent_mean_abs_delta_max": pose_summary.get("normalized_extent_mean_abs_delta_max"),
            "normalized_extent_max_abs_delta_max": pose_summary.get("normalized_extent_max_abs_delta_max"),
            "center_delta_normalized_max": pose_summary.get("center_delta_normalized_max"),
            "required_evidence": semantic_alias_required_evidence(promotion_class),
        }
        ledger.append(row)
        class_counts[promotion_class] += 1
        if promotion_class == "promotion_ready_same_frame_alias_clean":
            clean_ready_entries.append(row)
        elif promotion_class == "blocked_same_frame_reuses_promoted_csab":
            blocked_reuse_entries.append(row)
        elif promotion_class == "blocked_same_frame_reuses_candidate_csab":
            blocked_candidate_reuse_entries.append(row)

    return {
        "status": "promotion_ready" if clean_ready_entries else "no_clean_promotions",
        "policy": {
            "scope": "promotion ledger for unpromoted single-CSAB semantic aliases",
            "promotion_ready_definition": (
                "single alias candidate, same frame count, pose metric measured inside the "
                "reference envelope, and no existing runtime mapping already uses that CSAB"
            ),
            "runtime_mapping_policy": (
                "ready rows are an overlay ledger, not inserted into the reference runtime "
                "mapping until route/callsite review accepts semantic alias promotion"
            ),
        },
        "candidate_count": len(ledger),
        "promotion_ready_count": len(clean_ready_entries),
        "blocked_same_frame_reuses_promoted_csab_count": len(blocked_reuse_entries),
        "blocked_same_frame_reuses_candidate_csab_count": len(blocked_candidate_reuse_entries),
        "class_counts": sorted_counter(class_counts),
        "ready_overlay_entries": clean_ready_entries,
        "sample_blocked_reuse_entries": blocked_reuse_entries[:sample_limit],
        "sample_blocked_candidate_reuse_entries": blocked_candidate_reuse_entries[:sample_limit],
        "ledger": sorted(
            ledger,
            key=lambda item: (
                semantic_alias_promotion_priority(str(item.get("semantic_alias_promotion_class") or "")),
                int(item.get("index") or 0),
            ),
        ),
    }


def semantic_alias_route_review_analysis(
    records: list[dict[str, object]],
    semantic_alias_promotion_ledger: dict[str, object],
    source_references: dict[str, dict[str, object]],
    n64_records: list[dict[str, object]],
    oot3d_records: list[dict[str, object]],
    sample_limit: int,
) -> dict[str, object]:
    mapping_records_by_name = {
        str(record.get("n64_name") or ""): record
        for record in records
        if record.get("n64_name")
    }
    n64_index_by_name = {
        str(record.get("name") or ""): int(record.get("index") or 0)
        for record in n64_records
        if record.get("name")
    }
    ledger_rows = (
        semantic_alias_promotion_ledger.get("ledger", [])
        if isinstance(semantic_alias_promotion_ledger, dict)
        else []
    )
    route_rows: list[dict[str, object]] = []
    route_class_counts: Counter[str] = Counter()
    source_status_counts: Counter[str] = Counter()
    direct_source_reference_count = 0
    direct_player_actor_reference_count = 0
    for ledger_row in ledger_rows:
        if not isinstance(ledger_row, dict):
            continue
        promotion_class = str(ledger_row.get("semantic_alias_promotion_class") or "")
        if promotion_class not in {
            "promotion_ready_same_frame_alias_clean",
            "blocked_same_frame_reuses_promoted_csab",
            "blocked_same_frame_reuses_candidate_csab",
        }:
            continue
        n64_name = str(ledger_row.get("n64_name") or "")
        source_reference = source_references.get(n64_name, source_reference_empty(n64_name))
        source_count = int(source_reference.get("direct_source_reference_count") or 0)
        player_actor_count = int(source_reference.get("direct_player_actor_source_reference_count") or 0)
        candidate_collision_source = candidate_collision_source_reference_summary(
            str(ledger_row.get("candidate_csab_collision_n64_names") or ""),
            source_references,
        )
        mapping_record_for_name = mapping_records_by_name.get(n64_name, {})
        n64_table_context = anonymous_numeric_table_context(
            n64_records,
            n64_index_by_name.get(n64_name, int(ledger_row.get("index") or 0)),
            neighbor_limit=2,
        )
        oot3d_table = oot3d_table_context(
            oot3d_records,
            str(ledger_row.get("csab_name") or ""),
            neighbor_limit=2,
        )
        route_review_class = semantic_alias_route_review_class(
            promotion_class,
            source_count,
            player_actor_count,
        )
        route_rows.append(
            {
                "index": ledger_row.get("index"),
                "n64_name": n64_name,
                "n64_stem": ledger_row.get("n64_stem"),
                "n64_data_name": ledger_row.get("n64_data_name"),
                "csab_name": ledger_row.get("csab_name"),
                "oot3d_stem": ledger_row.get("oot3d_stem"),
                "n64_frame_count": ledger_row.get("n64_frame_count"),
                "oot3d_frame_slot_count": ledger_row.get("oot3d_frame_slot_count"),
                "frame_delta_oot3d_minus_n64": ledger_row.get("frame_delta_oot3d_minus_n64"),
                "semantic_alias_promotion_class": promotion_class,
                "route_review_class": route_review_class,
                "n64_source_reference_status": source_reference.get("status"),
                "n64_direct_source_reference_count": source_count,
                "n64_direct_player_actor_source_reference_count": player_actor_count,
                "n64_sample_direct_source_references": source_reference_sample_string(source_reference),
                "n64_neighbor_family": n64_table_context.get("inferred_neighbor_family"),
                "n64_previous_neighbor_stems": neighbor_stem_string(n64_table_context.get("previous_named")),
                "n64_next_neighbor_stems": neighbor_stem_string(n64_table_context.get("next_named")),
                "oot3d_neighbor_family": oot3d_table.get("inferred_neighbor_family"),
                "oot3d_previous_neighbor_stems": neighbor_stem_string(oot3d_table.get("previous_records")),
                "oot3d_next_neighbor_stems": neighbor_stem_string(oot3d_table.get("next_records")),
                "normalized_extent_max_abs_delta_max": ledger_row.get("normalized_extent_max_abs_delta_max"),
                "center_delta_normalized_max": ledger_row.get("center_delta_normalized_max"),
                "runtime_csab_collision_count": ledger_row.get("runtime_csab_collision_count"),
                "runtime_csab_collision_n64_names": ledger_row.get("runtime_csab_collision_n64_names"),
                "candidate_csab_collision_count": ledger_row.get("candidate_csab_collision_count"),
                "candidate_csab_collision_n64_names": ledger_row.get("candidate_csab_collision_n64_names"),
                "candidate_collision_direct_player_actor_source_reference_count": (
                    candidate_collision_source["direct_player_actor_source_reference_count"]
                ),
                "candidate_collision_sample_direct_source_references": candidate_collision_source[
                    "sample_direct_source_references"
                ],
                "required_evidence": semantic_alias_route_required_evidence(route_review_class),
                "candidate_family": (
                    mapping_record_for_name.get("candidate_promotion_analysis", {}).get("family")
                    if isinstance(mapping_record_for_name.get("candidate_promotion_analysis"), dict)
                    else None
                ),
            }
        )
        route_class_counts[route_review_class] += 1
        source_status_counts[str(source_reference.get("status") or "unknown")] += 1
        direct_source_reference_count += source_count
        direct_player_actor_reference_count += player_actor_count

    sorted_rows = sorted(
        route_rows,
        key=lambda item: (
            semantic_alias_route_review_priority(str(item.get("route_review_class") or "")),
            int(item.get("index") or 0),
        ),
    )
    return {
        "status": "review_ready" if sorted_rows else "no_same_frame_alias_rows",
        "policy": {
            "scope": "route/callsite evidence for same-frame semantic alias promotion candidates",
            "source_reference_policy": (
                "exact C/C++ symbol scan excluding generated player_anim_headers and .inc.c "
                "asset definitions; player-actor references are counted separately"
            ),
            "promotion_policy": (
                "route review is an overlay gate only; it does not insert rows into the "
                "reference runtime mapping"
            ),
        },
        "review_count": len(sorted_rows),
        "route_review_class_counts": sorted_counter(route_class_counts),
        "source_reference_status_counts": sorted_counter(source_status_counts),
        "direct_source_reference_count": direct_source_reference_count,
        "direct_player_actor_source_reference_count": direct_player_actor_reference_count,
        "overlay_ready_with_player_actor_route_count": route_class_counts.get(
            "overlay_ready_with_direct_player_actor_route_reference",
            0,
        ),
        "overlay_ready_without_player_actor_route_count": route_class_counts.get(
            "overlay_ready_without_direct_player_actor_route_reference",
            0,
        ),
        "blocked_by_promoted_csab_reuse_count": route_class_counts.get(
            "blocked_same_frame_reuses_promoted_csab",
            0,
        ),
        "blocked_by_candidate_csab_reuse_count": route_class_counts.get(
            "blocked_same_frame_reuses_candidate_csab",
            0,
        ),
        "sample_rows": sorted_rows[:sample_limit],
        "rows": sorted_rows,
    }


def semantic_alias_accepted_overlay_analysis(
    semantic_alias_route_review: dict[str, object],
    semantic_alias_resample_review: dict[str, object],
    promoted_runtime_mapping_count: int,
    total_n64_animation_count: int,
    sample_limit: int,
) -> dict[str, object]:
    route_rows = (
        semantic_alias_route_review.get("rows", [])
        if isinstance(semantic_alias_route_review, dict)
        else []
    )
    accepted_entries: list[dict[str, object]] = []
    for row in route_rows:
        if not isinstance(row, dict):
            continue
        if row.get("route_review_class") != "overlay_ready_with_direct_player_actor_route_reference":
            continue
        accepted_entries.append(
            {
                "index": row.get("index"),
                "n64_name": row.get("n64_name"),
                "n64_stem": row.get("n64_stem"),
                "n64_data_name": row.get("n64_data_name"),
                "csab_name": row.get("csab_name"),
                "oot3d_stem": row.get("oot3d_stem"),
                "n64_frame_count": row.get("n64_frame_count"),
                "oot3d_frame_slot_count": row.get("oot3d_frame_slot_count"),
                "frame_delta_oot3d_minus_n64": row.get("frame_delta_oot3d_minus_n64"),
                "semantic_overlay_status": "accepted_semantic_alias_overlay",
                "acceptance_class": "accepted_same_frame_pose_metric_with_direct_player_actor_route",
                "temporal_policy": "same_frame_identity",
                "temporal_policy_status": "no_resample_required",
                "acceptance_basis": (
                    "same-frame candidate inside reference pose envelope, no CSAB collision, "
                    "and exact direct player-actor source reference"
                ),
                "n64_direct_player_actor_source_reference_count": row.get(
                    "n64_direct_player_actor_source_reference_count"
                ),
                "n64_sample_direct_source_references": row.get("n64_sample_direct_source_references"),
                "normalized_extent_max_abs_delta_max": row.get("normalized_extent_max_abs_delta_max"),
                "center_delta_normalized_max": row.get("center_delta_normalized_max"),
            }
        )
    resample_rows = (
        semantic_alias_resample_review.get("rows", [])
        if isinstance(semantic_alias_resample_review, dict)
        else []
    )
    accepted_names = {str(entry.get("n64_name") or "") for entry in accepted_entries}
    for row in resample_rows:
        if not isinstance(row, dict):
            continue
        if row.get("resample_review_class") != "resample_ready_with_direct_player_actor_route_reference":
            continue
        n64_name = str(row.get("n64_name") or "")
        if n64_name in accepted_names:
            continue
        accepted_names.add(n64_name)
        accepted_entries.append(
            {
                "index": row.get("index"),
                "n64_name": row.get("n64_name"),
                "n64_stem": row.get("n64_stem"),
                "n64_data_name": row.get("n64_data_name"),
                "csab_name": row.get("csab_name"),
                "oot3d_stem": row.get("oot3d_stem"),
                "n64_frame_count": row.get("n64_frame_count"),
                "oot3d_frame_slot_count": row.get("oot3d_frame_slot_count"),
                "frame_delta_oot3d_minus_n64": row.get("frame_delta_oot3d_minus_n64"),
                "duration_ratio_oot3d_per_n64": row.get("duration_ratio_oot3d_per_n64"),
                "semantic_overlay_status": "accepted_semantic_alias_overlay",
                "acceptance_class": "accepted_small_delta_resample_with_direct_player_actor_route",
                "temporal_policy": "proportional_csab_resample_to_n64_frame_count",
                "temporal_policy_status": "small_delta_resample_required",
                "acceptance_basis": (
                    "small-delta candidate inside reference pose envelope, no CSAB collision, "
                    "and exact direct player-actor source reference; CSAB frames are sampled "
                    "proportionally to the N64 frame count"
                ),
                "n64_direct_player_actor_source_reference_count": row.get(
                    "n64_direct_player_actor_source_reference_count"
                ),
                "n64_sample_direct_source_references": row.get("n64_sample_direct_source_references"),
                "normalized_extent_max_abs_delta_max": row.get("normalized_extent_max_abs_delta_max"),
                "center_delta_normalized_max": row.get("center_delta_normalized_max"),
            }
        )
    promoted_or_accepted = promoted_runtime_mapping_count + len(accepted_entries)
    acceptance_counts = Counter(str(entry.get("acceptance_class") or "unknown") for entry in accepted_entries)
    temporal_policy_counts = Counter(str(entry.get("temporal_policy") or "unknown") for entry in accepted_entries)
    return {
        "status": "accepted_overlay_ready" if accepted_entries else "no_accepted_overlay_entries",
        "policy": {
            "scope": "accepted semantic overlay rows for N64 PlayerAnimation -> OOT3D CSAB",
            "acceptance_definition": (
                "same-frame semantic alias candidate, measured inside the reference pose envelope, "
                "not colliding with promoted or candidate CSAB ownership, and directly referenced "
                "from the N64 player actor source"
            ),
            "runtime_mapping_policy": (
                "accepted overlay rows increase semantic mapping evidence but are not inserted into "
                "the promoted runtime mapping table"
            ),
        },
        "promoted_runtime_mapping_count": promoted_runtime_mapping_count,
        "accepted_overlay_count": len(accepted_entries),
        "accepted_overlay_class_counts": sorted_counter(acceptance_counts),
        "accepted_overlay_temporal_policy_counts": sorted_counter(temporal_policy_counts),
        "promoted_or_accepted_semantic_mapping_count": promoted_or_accepted,
        "n64_player_animation_count": total_n64_animation_count,
        "promoted_or_accepted_semantic_mapping_percent": round(
            ratio(promoted_or_accepted, total_n64_animation_count) * 100.0,
            3,
        ),
        "accepted_overlay_entries": sorted(
            accepted_entries,
            key=lambda item: int(item.get("index") or 0),
        ),
        "sample_accepted_overlay_entries": sorted(
            accepted_entries,
            key=lambda item: int(item.get("index") or 0),
        )[:sample_limit],
    }


def semantic_alias_resample_review_analysis(
    semantic_alias_promotion_ledger: dict[str, object],
    source_references: dict[str, dict[str, object]],
    n64_records: list[dict[str, object]],
    oot3d_records: list[dict[str, object]],
    sample_limit: int,
) -> dict[str, object]:
    n64_index_by_name = {
        str(record.get("name") or ""): int(record.get("index") or 0)
        for record in n64_records
        if record.get("name")
    }
    ledger_rows = (
        semantic_alias_promotion_ledger.get("ledger", [])
        if isinstance(semantic_alias_promotion_ledger, dict)
        else []
    )
    rows: list[dict[str, object]] = []
    class_counts: Counter[str] = Counter()
    source_status_counts: Counter[str] = Counter()
    direct_player_actor_reference_count = 0
    for ledger_row in ledger_rows:
        if not isinstance(ledger_row, dict):
            continue
        if ledger_row.get("semantic_alias_promotion_class") != "requires_resample_policy":
            continue
        n64_name = str(ledger_row.get("n64_name") or "")
        source_reference = source_references.get(n64_name, source_reference_empty(n64_name))
        source_count = int(source_reference.get("direct_source_reference_count") or 0)
        player_actor_count = int(source_reference.get("direct_player_actor_source_reference_count") or 0)
        runtime_collision_count = int(ledger_row.get("runtime_csab_collision_count") or 0)
        candidate_collision_count = int(ledger_row.get("candidate_csab_collision_count") or 0)
        review_class = semantic_alias_resample_review_class(
            runtime_collision_count,
            candidate_collision_count,
            source_count,
            player_actor_count,
        )
        n64_table_context = anonymous_numeric_table_context(
            n64_records,
            n64_index_by_name.get(n64_name, int(ledger_row.get("index") or 0)),
            neighbor_limit=2,
        )
        oot3d_table = oot3d_table_context(
            oot3d_records,
            str(ledger_row.get("csab_name") or ""),
            neighbor_limit=2,
        )
        rows.append(
            {
                "index": ledger_row.get("index"),
                "n64_name": n64_name,
                "n64_stem": ledger_row.get("n64_stem"),
                "n64_data_name": ledger_row.get("n64_data_name"),
                "csab_name": ledger_row.get("csab_name"),
                "oot3d_stem": ledger_row.get("oot3d_stem"),
                "n64_frame_count": ledger_row.get("n64_frame_count"),
                "oot3d_frame_slot_count": ledger_row.get("oot3d_frame_slot_count"),
                "frame_delta_oot3d_minus_n64": ledger_row.get("frame_delta_oot3d_minus_n64"),
                "duration_ratio_oot3d_per_n64": ledger_row.get("duration_ratio_oot3d_per_n64"),
                "semantic_alias_promotion_class": ledger_row.get("semantic_alias_promotion_class"),
                "resample_review_class": review_class,
                "temporal_policy": "proportional_csab_resample_to_n64_frame_count",
                "n64_source_reference_status": source_reference.get("status"),
                "n64_direct_source_reference_count": source_count,
                "n64_direct_player_actor_source_reference_count": player_actor_count,
                "n64_sample_direct_source_references": source_reference_sample_string(source_reference),
                "n64_neighbor_family": n64_table_context.get("inferred_neighbor_family"),
                "n64_previous_neighbor_stems": neighbor_stem_string(n64_table_context.get("previous_named")),
                "n64_next_neighbor_stems": neighbor_stem_string(n64_table_context.get("next_named")),
                "oot3d_neighbor_family": oot3d_table.get("inferred_neighbor_family"),
                "oot3d_previous_neighbor_stems": neighbor_stem_string(oot3d_table.get("previous_records")),
                "oot3d_next_neighbor_stems": neighbor_stem_string(oot3d_table.get("next_records")),
                "runtime_csab_collision_count": runtime_collision_count,
                "runtime_csab_collision_n64_names": ledger_row.get("runtime_csab_collision_n64_names"),
                "candidate_csab_collision_count": candidate_collision_count,
                "candidate_csab_collision_n64_names": ledger_row.get("candidate_csab_collision_n64_names"),
                "normalized_extent_max_abs_delta_max": ledger_row.get("normalized_extent_max_abs_delta_max"),
                "center_delta_normalized_max": ledger_row.get("center_delta_normalized_max"),
                "required_evidence": semantic_alias_resample_required_evidence(review_class),
            }
        )
        class_counts[review_class] += 1
        source_status_counts[str(source_reference.get("status") or "unknown")] += 1
        direct_player_actor_reference_count += player_actor_count

    sorted_rows = sorted(
        rows,
        key=lambda item: (
            semantic_alias_resample_review_priority(str(item.get("resample_review_class") or "")),
            int(item.get("index") or 0),
        ),
    )
    return {
        "status": "review_ready" if sorted_rows else "no_resample_policy_rows",
        "policy": {
            "scope": "route/callsite and ownership review for small-delta resample semantic aliases",
            "resample_policy": (
                "sample OOT3D CSAB proportionally onto the N64 PlayerAnimation frame count; "
                "small-delta is limited to abs(delta) <= 3 and max duration ratio <= 1.25"
            ),
            "runtime_mapping_policy": (
                "resample-ready rows increase semantic mapping evidence but are not inserted into "
                "the promoted runtime mapping table"
            ),
        },
        "review_count": len(sorted_rows),
        "resample_ready_with_player_actor_route_count": class_counts.get(
            "resample_ready_with_direct_player_actor_route_reference",
            0,
        ),
        "resample_ready_without_player_actor_route_count": class_counts.get(
            "resample_ready_without_direct_player_actor_route_reference",
            0,
        ),
        "blocked_by_promoted_csab_reuse_count": class_counts.get(
            "blocked_resample_reuses_promoted_csab",
            0,
        ),
        "blocked_by_candidate_csab_reuse_count": class_counts.get(
            "blocked_resample_reuses_candidate_csab",
            0,
        ),
        "resample_review_class_counts": sorted_counter(class_counts),
        "source_reference_status_counts": sorted_counter(source_status_counts),
        "direct_player_actor_source_reference_count": direct_player_actor_reference_count,
        "sample_rows": sorted_rows[:sample_limit],
        "rows": sorted_rows,
    }


def semantic_alias_resample_review_class(
    runtime_collision_count: int,
    candidate_collision_count: int,
    direct_source_reference_count: int,
    direct_player_actor_source_reference_count: int,
) -> str:
    if runtime_collision_count > 0:
        return "blocked_resample_reuses_promoted_csab"
    if candidate_collision_count > 0:
        return "blocked_resample_reuses_candidate_csab"
    if direct_player_actor_source_reference_count > 0:
        return "resample_ready_with_direct_player_actor_route_reference"
    if direct_source_reference_count > 0:
        return "resample_ready_with_non_player_direct_source_reference"
    return "resample_ready_without_direct_player_actor_route_reference"


def semantic_alias_resample_required_evidence(review_class: str) -> str:
    if review_class == "resample_ready_with_direct_player_actor_route_reference":
        return "accepted by proportional resample policy; N64 player actor route is directly referenced"
    if review_class == "resample_ready_with_non_player_direct_source_reference":
        return "manual route label acceptance; direct source reference is outside the player actor sample"
    if review_class == "resample_ready_without_direct_player_actor_route_reference":
        return "stronger callsite, route-table, or capture evidence before resample overlay acceptance"
    if review_class == "blocked_resample_reuses_promoted_csab":
        return "prove intentional CSAB reuse or keep as fallback/bake candidate"
    if review_class == "blocked_resample_reuses_candidate_csab":
        return "choose which N64 candidate owns the shared CSAB, prove intentional reuse, or keep one side as bake/fallback"
    return "manual resample route/callsite review"


def semantic_alias_resample_review_priority(review_class: str) -> int:
    return {
        "resample_ready_with_direct_player_actor_route_reference": 0,
        "resample_ready_with_non_player_direct_source_reference": 1,
        "resample_ready_without_direct_player_actor_route_reference": 2,
        "blocked_resample_reuses_promoted_csab": 3,
        "blocked_resample_reuses_candidate_csab": 4,
    }.get(review_class, 9)


def semantic_alias_temporal_bake_review_analysis(
    semantic_alias_promotion_ledger: dict[str, object],
    source_references: dict[str, dict[str, object]],
    n64_records: list[dict[str, object]],
    oot3d_records: list[dict[str, object]],
    sample_limit: int,
) -> dict[str, object]:
    n64_index_by_name = {
        str(record.get("name") or ""): int(record.get("index") or 0)
        for record in n64_records
        if record.get("name")
    }
    ledger_rows = (
        semantic_alias_promotion_ledger.get("ledger", [])
        if isinstance(semantic_alias_promotion_ledger, dict)
        else []
    )
    rows: list[dict[str, object]] = []
    class_counts: Counter[str] = Counter()
    temporal_severity_counts: Counter[str] = Counter()
    family_counts: Counter[str] = Counter()
    source_status_counts: Counter[str] = Counter()
    direct_player_actor_reference_count = 0
    for ledger_row in ledger_rows:
        if not isinstance(ledger_row, dict):
            continue
        if ledger_row.get("semantic_alias_promotion_class") != "requires_temporal_bake_policy":
            continue
        n64_name = str(ledger_row.get("n64_name") or "")
        n64_stem = str(ledger_row.get("n64_stem") or "")
        n64_frame_count = int(ledger_row.get("n64_frame_count") or 0)
        oot3d_frame_count = int(ledger_row.get("oot3d_frame_slot_count") or 0)
        source_reference = source_references.get(n64_name, source_reference_empty(n64_name))
        source_count = int(source_reference.get("direct_source_reference_count") or 0)
        player_actor_count = int(source_reference.get("direct_player_actor_source_reference_count") or 0)
        runtime_collision_count = int(ledger_row.get("runtime_csab_collision_count") or 0)
        candidate_collision_count = int(ledger_row.get("candidate_csab_collision_count") or 0)
        review_class = semantic_alias_temporal_bake_review_class(
            runtime_collision_count,
            candidate_collision_count,
            source_count,
            player_actor_count,
        )
        temporal_severity = candidate_temporal_severity(n64_frame_count, oot3d_frame_count)
        family = candidate_family_for_n64_stem(n64_stem)
        n64_table_context = anonymous_numeric_table_context(
            n64_records,
            n64_index_by_name.get(n64_name, int(ledger_row.get("index") or 0)),
            neighbor_limit=2,
        )
        oot3d_table = oot3d_table_context(
            oot3d_records,
            str(ledger_row.get("csab_name") or ""),
            neighbor_limit=2,
        )
        rows.append(
            {
                "index": ledger_row.get("index"),
                "n64_name": n64_name,
                "n64_stem": n64_stem,
                "n64_data_name": ledger_row.get("n64_data_name"),
                "csab_name": ledger_row.get("csab_name"),
                "oot3d_stem": ledger_row.get("oot3d_stem"),
                "candidate_family": family,
                "n64_frame_count": n64_frame_count,
                "oot3d_frame_slot_count": oot3d_frame_count,
                "frame_delta_oot3d_minus_n64": ledger_row.get("frame_delta_oot3d_minus_n64"),
                "duration_ratio_oot3d_per_n64": ledger_row.get("duration_ratio_oot3d_per_n64"),
                "temporal_severity": temporal_severity,
                "semantic_alias_promotion_class": ledger_row.get("semantic_alias_promotion_class"),
                "temporal_bake_review_class": review_class,
                "temporal_policy": "bake_or_fuse_oot3d_csab_to_n64_frame_count",
                "n64_source_reference_status": source_reference.get("status"),
                "n64_direct_source_reference_count": source_count,
                "n64_direct_player_actor_source_reference_count": player_actor_count,
                "n64_sample_direct_source_references": source_reference_sample_string(source_reference),
                "n64_neighbor_family": n64_table_context.get("inferred_neighbor_family"),
                "n64_previous_neighbor_stems": neighbor_stem_string(n64_table_context.get("previous_named")),
                "n64_next_neighbor_stems": neighbor_stem_string(n64_table_context.get("next_named")),
                "oot3d_neighbor_family": oot3d_table.get("inferred_neighbor_family"),
                "oot3d_previous_neighbor_stems": neighbor_stem_string(oot3d_table.get("previous_records")),
                "oot3d_next_neighbor_stems": neighbor_stem_string(oot3d_table.get("next_records")),
                "runtime_csab_collision_count": runtime_collision_count,
                "runtime_csab_collision_n64_names": ledger_row.get("runtime_csab_collision_n64_names"),
                "candidate_csab_collision_count": candidate_collision_count,
                "candidate_csab_collision_n64_names": ledger_row.get("candidate_csab_collision_n64_names"),
                "normalized_extent_max_abs_delta_max": ledger_row.get("normalized_extent_max_abs_delta_max"),
                "center_delta_normalized_max": ledger_row.get("center_delta_normalized_max"),
                "required_evidence": semantic_alias_temporal_bake_required_evidence(review_class),
            }
        )
        class_counts[review_class] += 1
        temporal_severity_counts[temporal_severity] += 1
        family_counts[family] += 1
        source_status_counts[str(source_reference.get("status") or "unknown")] += 1
        direct_player_actor_reference_count += player_actor_count

    sorted_rows = sorted(
        rows,
        key=lambda item: (
            semantic_alias_temporal_bake_review_priority(str(item.get("temporal_bake_review_class") or "")),
            candidate_pose_temporal_priority(str(item.get("temporal_severity") or "")),
            int(item.get("index") or 0),
        ),
    )
    return {
        "status": "review_ready" if sorted_rows else "no_temporal_bake_policy_rows",
        "policy": {
            "scope": "route/callsite and ownership review for inside-envelope aliases that require temporal bake or fusion",
            "temporal_policy": (
                "bake or fuse the candidate OOT3D CSAB onto the N64 PlayerAnimation frame count; "
                "this queue excludes same-frame and small-delta proportional-resample rows"
            ),
            "runtime_mapping_policy": (
                "temporal-bake rows are not accepted semantic overlays until a route/capture/bake-source "
                "contract is explicit"
            ),
        },
        "review_count": len(sorted_rows),
        "bake_ready_with_player_actor_route_count": class_counts.get(
            "bake_ready_with_direct_player_actor_route_reference",
            0,
        ),
        "bake_ready_without_player_actor_route_count": class_counts.get(
            "bake_candidate_requires_route_or_capture_evidence",
            0,
        ),
        "blocked_by_promoted_csab_reuse_count": class_counts.get(
            "blocked_bake_reuses_promoted_csab",
            0,
        ),
        "blocked_by_candidate_csab_reuse_count": class_counts.get(
            "blocked_bake_reuses_candidate_csab",
            0,
        ),
        "temporal_bake_review_class_counts": sorted_counter(class_counts),
        "temporal_severity_counts": sorted_counter(temporal_severity_counts),
        "candidate_family_counts": sorted_counter(family_counts),
        "source_reference_status_counts": sorted_counter(source_status_counts),
        "direct_player_actor_source_reference_count": direct_player_actor_reference_count,
        "sample_rows": sorted_rows[:sample_limit],
        "rows": sorted_rows,
    }


def semantic_alias_temporal_bake_review_class(
    runtime_collision_count: int,
    candidate_collision_count: int,
    direct_source_reference_count: int,
    direct_player_actor_source_reference_count: int,
) -> str:
    if runtime_collision_count > 0:
        return "blocked_bake_reuses_promoted_csab"
    if candidate_collision_count > 0:
        return "blocked_bake_reuses_candidate_csab"
    if direct_player_actor_source_reference_count > 0:
        return "bake_ready_with_direct_player_actor_route_reference"
    if direct_source_reference_count > 0:
        return "bake_ready_with_non_player_direct_source_reference"
    return "bake_candidate_requires_route_or_capture_evidence"


def semantic_alias_temporal_bake_required_evidence(review_class: str) -> str:
    if review_class == "bake_ready_with_direct_player_actor_route_reference":
        return "define final bake/fusion output contract; N64 player actor route is directly referenced"
    if review_class == "bake_ready_with_non_player_direct_source_reference":
        return "manual route label acceptance plus bake/fusion output contract; direct source reference is outside the player actor sample"
    if review_class == "bake_candidate_requires_route_or_capture_evidence":
        return "capture route, action-state table evidence, or explicit bake source before semantic overlay acceptance"
    if review_class == "blocked_bake_reuses_promoted_csab":
        return "prove intentional CSAB reuse, create a fused/baked derivative, or keep as fallback"
    if review_class == "blocked_bake_reuses_candidate_csab":
        return "choose the N64 owner for the shared CSAB, prove intentional reuse, or create distinct baked derivatives"
    return "manual temporal bake review"


def semantic_alias_temporal_bake_review_priority(review_class: str) -> int:
    return {
        "bake_ready_with_direct_player_actor_route_reference": 0,
        "bake_ready_with_non_player_direct_source_reference": 1,
        "blocked_bake_reuses_promoted_csab": 2,
        "blocked_bake_reuses_candidate_csab": 3,
        "bake_candidate_requires_route_or_capture_evidence": 4,
    }.get(review_class, 9)


def semantic_alias_bake_contract_analysis(
    semantic_alias_temporal_bake_review: dict[str, object],
    semantic_alias_accepted_overlay: dict[str, object],
    promoted_runtime_mapping_count: int,
    total_n64_animation_count: int,
    sample_limit: int,
) -> dict[str, object]:
    rows = (
        semantic_alias_temporal_bake_review.get("rows", [])
        if isinstance(semantic_alias_temporal_bake_review, dict)
        else []
    )
    accepted_overlay_count = (
        int(semantic_alias_accepted_overlay.get("accepted_overlay_count") or 0)
        if isinstance(semantic_alias_accepted_overlay, dict)
        else 0
    )
    contracts: list[dict[str, object]] = []
    status_counts: Counter[str] = Counter()
    materialization_counts: Counter[str] = Counter()
    ready_materialization_counts: Counter[str] = Counter()
    temporal_severity_counts: Counter[str] = Counter()
    ready_source_csabs: set[str] = set()
    ready_contract_count = 0
    for row in rows:
        if not isinstance(row, dict):
            continue
        review_class = str(row.get("temporal_bake_review_class") or "")
        contract_status = semantic_alias_bake_contract_status(review_class)
        materialization_class = semantic_alias_bake_materialization_class(row)
        contract_ready = contract_status == "bake_contract_ready"
        if contract_ready:
            ready_contract_count += 1
            if row.get("csab_name"):
                ready_source_csabs.add(str(row.get("csab_name") or ""))
            ready_materialization_counts[materialization_class] += 1
        status_counts[contract_status] += 1
        materialization_counts[materialization_class] += 1
        temporal_severity_counts[str(row.get("temporal_severity") or "unknown")] += 1
        contracts.append(
            {
                "index": row.get("index"),
                "n64_name": row.get("n64_name"),
                "n64_stem": row.get("n64_stem"),
                "n64_data_name": row.get("n64_data_name"),
                "source_csab_name": row.get("csab_name"),
                "source_oot3d_stem": row.get("oot3d_stem"),
                "candidate_family": row.get("candidate_family"),
                "contract_status": contract_status,
                "contract_kind": "semantic_alias_temporal_bake",
                "temporal_bake_review_class": review_class,
                "materialization_class": materialization_class,
                "source_frame_slot_count": row.get("oot3d_frame_slot_count"),
                "target_n64_frame_count": row.get("n64_frame_count"),
                "frame_delta_oot3d_minus_n64": row.get("frame_delta_oot3d_minus_n64"),
                "duration_ratio_oot3d_per_n64": row.get("duration_ratio_oot3d_per_n64"),
                "temporal_severity": row.get("temporal_severity"),
                "source_format": "oot3d_csab",
                "target_semantic_slot": "n64_playeranimation",
                "output_resource_policy": semantic_alias_bake_output_resource_policy(contract_status),
                "frame_mapping_contract": semantic_alias_bake_frame_mapping_contract(row),
                "ownership_policy": semantic_alias_bake_ownership_policy(contract_status),
                "route_evidence_status": semantic_alias_bake_route_evidence_status(contract_status),
                "n64_direct_player_actor_source_reference_count": row.get(
                    "n64_direct_player_actor_source_reference_count"
                ),
                "n64_sample_direct_source_references": row.get("n64_sample_direct_source_references"),
                "runtime_csab_collision_count": row.get("runtime_csab_collision_count"),
                "runtime_csab_collision_n64_names": row.get("runtime_csab_collision_n64_names"),
                "candidate_csab_collision_count": row.get("candidate_csab_collision_count"),
                "candidate_csab_collision_n64_names": row.get("candidate_csab_collision_n64_names"),
                "normalized_extent_max_abs_delta_max": row.get("normalized_extent_max_abs_delta_max"),
                "center_delta_normalized_max": row.get("center_delta_normalized_max"),
                "required_next_step": semantic_alias_bake_contract_next_step(contract_status),
            }
        )

    sorted_contracts = sorted(
        contracts,
        key=lambda item: (
            semantic_alias_bake_contract_priority(str(item.get("contract_status") or "")),
            candidate_pose_temporal_priority(str(item.get("temporal_severity") or "")),
            int(item.get("index") or 0),
        ),
    )
    promoted_or_accepted_or_bake_ready = (
        promoted_runtime_mapping_count
        + accepted_overlay_count
        + ready_contract_count
    )
    return {
        "status": "contract_ledger_ready" if sorted_contracts else "no_temporal_bake_contracts",
        "policy": {
            "scope": "materialization contracts for temporal-bake semantic aliases",
            "ready_definition": (
                "inside reference pose envelope, no promoted/candidate CSAB collision, and direct "
                "N64 player-actor route reference"
            ),
            "materialization_policy": (
                "ready rows define how to derive a unique baked CSAB/animation stream from the "
                "source OOT3D CSAB onto the N64 PlayerAnimation frame count"
            ),
            "acceptance_policy": (
                "bake contracts are not accepted overlay rows until the derived resource is "
                "materialized and runtime/pose parity is verified"
            ),
        },
        "contract_count": len(sorted_contracts),
        "bake_contract_ready_count": ready_contract_count,
        "bake_contract_blocked_or_pending_count": len(sorted_contracts) - ready_contract_count,
        "unique_ready_source_csab_count": len(ready_source_csabs),
        "promoted_runtime_mapping_count": promoted_runtime_mapping_count,
        "accepted_overlay_count": accepted_overlay_count,
        "promoted_or_accepted_semantic_mapping_count": promoted_runtime_mapping_count + accepted_overlay_count,
        "promoted_or_accepted_or_bake_contract_ready_count": promoted_or_accepted_or_bake_ready,
        "n64_player_animation_count": total_n64_animation_count,
        "promoted_or_accepted_or_bake_contract_ready_percent": round(
            ratio(promoted_or_accepted_or_bake_ready, total_n64_animation_count) * 100.0,
            3,
        ),
        "contract_status_counts": sorted_counter(status_counts),
        "materialization_class_counts": sorted_counter(materialization_counts),
        "ready_materialization_class_counts": sorted_counter(ready_materialization_counts),
        "temporal_severity_counts": sorted_counter(temporal_severity_counts),
        "sample_contracts": sorted_contracts[:sample_limit],
        "contracts": sorted_contracts,
    }


def semantic_alias_bake_contract_status(review_class: str) -> str:
    if review_class == "bake_ready_with_direct_player_actor_route_reference":
        return "bake_contract_ready"
    if review_class == "bake_ready_with_non_player_direct_source_reference":
        return "requires_manual_route_acceptance"
    if review_class == "bake_candidate_requires_route_or_capture_evidence":
        return "requires_route_or_capture_evidence"
    if review_class == "blocked_bake_reuses_promoted_csab":
        return "blocked_promoted_csab_reuse"
    if review_class == "blocked_bake_reuses_candidate_csab":
        return "blocked_candidate_csab_reuse"
    return "manual_bake_contract_review"


def semantic_alias_bake_materialization_class(row: dict[str, object]) -> str:
    delta = int(row.get("frame_delta_oot3d_minus_n64") or 0)
    if delta < 0:
        return "expand_source_csab_to_n64_frame_count"
    if delta > 0:
        return "compress_source_csab_to_n64_frame_count"
    return "same_frame_unexpected_temporal_bake"


def semantic_alias_bake_output_resource_policy(contract_status: str) -> str:
    if contract_status == "bake_contract_ready":
        return "derive_unique_baked_animation_resource_before_overlay_acceptance"
    if contract_status == "requires_manual_route_acceptance":
        return "derive_after_manual_route_label_acceptance"
    if contract_status == "requires_route_or_capture_evidence":
        return "do_not_materialize_until_route_or_capture_evidence_exists"
    if contract_status == "blocked_promoted_csab_reuse":
        return "do_not_materialize_until_promoted_csab_ownership_is_resolved"
    if contract_status == "blocked_candidate_csab_reuse":
        return "do_not_materialize_until_candidate_csab_ownership_is_resolved"
    return "manual_output_policy_required"


def semantic_alias_bake_frame_mapping_contract(row: dict[str, object]) -> str:
    source_frames = int(row.get("oot3d_frame_slot_count") or 0)
    target_frames = int(row.get("n64_frame_count") or 0)
    if source_frames <= 0 or target_frames <= 0:
        return "manual frame mapping required because source or target frame count is missing"
    if source_frames == 1 or target_frames == 1:
        return (
            f"sample source CSAB frame 0 across target N64 frame count {target_frames}; "
            "manual hold-frame review required"
        )
    return (
        f"for target frame i in [0,{target_frames - 1}], sample source CSAB at "
        f"i * ({source_frames - 1}) / ({target_frames - 1}) and bake onto the N64 frame count"
    )


def semantic_alias_bake_ownership_policy(contract_status: str) -> str:
    if contract_status == "bake_contract_ready":
        return "source_csab_is_unique_within_promoted_and_candidate_alias_sets"
    if contract_status == "blocked_promoted_csab_reuse":
        return "source_csab_is_already_owned_by_promoted_runtime_mapping"
    if contract_status == "blocked_candidate_csab_reuse":
        return "source_csab_is_claimed_by_multiple_unpromoted_candidates"
    return "ownership_or_route_evidence_required_before_materialization"


def semantic_alias_bake_route_evidence_status(contract_status: str) -> str:
    if contract_status == "bake_contract_ready":
        return "direct_player_actor_route_reference_found"
    if contract_status == "requires_manual_route_acceptance":
        return "direct_non_player_source_reference_found"
    if contract_status == "requires_route_or_capture_evidence":
        return "no_direct_route_reference_found"
    return "route_evidence_blocked_by_csab_ownership"


def semantic_alias_bake_contract_next_step(contract_status: str) -> str:
    if contract_status == "bake_contract_ready":
        return "materialize derived animation resource and verify runtime pose parity before overlay acceptance"
    if contract_status == "requires_manual_route_acceptance":
        return "accept source route label or capture runtime route, then materialize derived animation resource"
    if contract_status == "requires_route_or_capture_evidence":
        return "find action-state route, capture runtime route, or keep as unresolved bake candidate"
    if contract_status == "blocked_promoted_csab_reuse":
        return "prove intentional promoted CSAB reuse or create distinct baked derivative after ownership review"
    if contract_status == "blocked_candidate_csab_reuse":
        return "choose candidate owner or create distinct baked derivatives for each N64 route"
    return "manual bake contract review"


def semantic_alias_bake_contract_priority(contract_status: str) -> int:
    return {
        "bake_contract_ready": 0,
        "requires_manual_route_acceptance": 1,
        "blocked_promoted_csab_reuse": 2,
        "blocked_candidate_csab_reuse": 3,
        "requires_route_or_capture_evidence": 4,
        "manual_bake_contract_review": 5,
    }.get(contract_status, 9)


def semantic_alias_ambiguity_arbitration_analysis(
    reference_audit: dict[str, object],
    records: list[dict[str, object]],
    runtime_entries: list[dict[str, object]],
    semantic_alias_bake_contract_ledger: dict[str, object],
    total_n64_animation_count: int,
    sample_limit: int,
) -> dict[str, object]:
    ambiguous_records = [
        record
        for record in records
        if record.get("status") == "candidate_ambiguous_csab_unpromoted"
    ]
    source_files = reference_audit.get("source_files") if isinstance(reference_audit.get("source_files"), dict) else {}
    target = reference_audit.get("oot3d_target") if isinstance(reference_audit.get("oot3d_target"), dict) else {}
    n64_reference = (
        reference_audit.get("n64_reference")
        if isinstance(reference_audit.get("n64_reference"), dict)
        else {}
    )
    skeleton_pose_reference = (
        n64_reference.get("skeleton_pose_reference")
        if isinstance(n64_reference.get("skeleton_pose_reference"), dict)
        else {}
    )
    pose_batch_manifest_path = optional_existing_path(source_files.get("oot3d_pose_batch_manifest"))
    n64_base_o2r_path = optional_existing_path(source_files.get("n64_base_o2r"))
    if pose_batch_manifest_path is None or n64_base_o2r_path is None:
        return {
            "status": "missing_inputs",
            "ambiguous_record_count": len(ambiguous_records),
            "candidate_measurement_count": 0,
            "candidate_rows": [],
            "arbitration_rows": [],
            "issue_count": 1,
            "sample_issues": [
                {
                    "type": "missing_pose_metric_inputs",
                    "oot3d_pose_batch_manifest": source_files.get("oot3d_pose_batch_manifest"),
                    "n64_base_o2r": source_files.get("n64_base_o2r"),
                }
            ],
        }
    if skeleton_pose_reference.get("status") != "decoded":
        return {
            "status": "missing_n64_skeleton_pose_reference",
            "ambiguous_record_count": len(ambiguous_records),
            "candidate_measurement_count": 0,
            "candidate_rows": [],
            "arbitration_rows": [],
            "issue_count": 1,
            "sample_issues": [
                {
                    "type": "missing_n64_skeleton_pose_reference",
                    "skeleton_pose_reference_status": skeleton_pose_reference.get("status"),
                }
            ],
        }

    pose_batch = load_json(pose_batch_manifest_path)
    pose_records = indexed_pose_batch_records(
        pose_batch,
        str(target.get("archive_path") or ""),
        str(target.get("target_cmb_name") or ""),
    )
    reference_envelope = reference_pose_metric_envelope(reference_audit.get("pose_error_metric"))
    promoted_by_csab = group_entries_by_csab_name(runtime_entries)
    single_candidate_by_csab = group_mapping_records_by_csab_name(records, "candidate_single_csab_unpromoted")
    candidate_rows: list[dict[str, object]] = []
    arbitration_rows: list[dict[str, object]] = []
    issues: list[dict[str, object]] = []
    arbitration_class_counts: Counter[str] = Counter()
    candidate_status_counts: Counter[str] = Counter()
    envelope_counts: Counter[str] = Counter()
    best_candidate_temporal_counts: Counter[str] = Counter()
    measured_candidate_count = 0
    inside_candidate_count = 0
    pose_best_inside_count = 0

    try:
        with zipfile.ZipFile(n64_base_o2r_path) as archive:
            for record in ambiguous_records:
                measurements: list[dict[str, object]] = []
                candidates = record.get("candidate_csabs", [])
                if not isinstance(candidates, list):
                    candidates = []
                for candidate_index, candidate in enumerate(candidates):
                    if not isinstance(candidate, dict):
                        continue
                    candidate_record = ambiguous_candidate_pose_record(record, candidate, candidate_index)
                    row, _pair_metrics, row_issues = candidate_alias_pose_metric_record(
                        archive,
                        pose_batch_manifest_path,
                        pose_batch,
                        pose_records,
                        skeleton_pose_reference,
                        reference_envelope,
                        candidate_record,
                    )
                    csab_name = str(row.get("csab_name") or "")
                    runtime_collisions = [
                        entry
                        for entry in promoted_by_csab.get(csab_name, [])
                        if str(entry.get("n64_name") or "") != str(record.get("n64_name") or "")
                    ]
                    candidate_collisions = [
                        entry
                        for entry in single_candidate_by_csab.get(csab_name, [])
                        if str(entry.get("n64_name") or "") != str(record.get("n64_name") or "")
                    ]
                    measured_candidate_count += 1 if row.get("pose_metric_status") == "measured" else 0
                    inside_candidate_count += 1 if row.get("reference_envelope_status") == "inside_reference_envelope" else 0
                    candidate_status_counts[str(row.get("pose_metric_status") or "unknown")] += 1
                    envelope_counts[str(row.get("reference_envelope_status") or "unknown")] += 1
                    candidate_row = {
                        **row,
                        "ambiguous_candidate_index": candidate_index,
                        "candidate_rank": None,
                        "arbitration_class": None,
                        "is_pose_best_candidate": False,
                        "runtime_csab_collision_count": len(runtime_collisions),
                        "runtime_csab_collision_n64_names": ";".join(
                            str(entry.get("n64_name") or "") for entry in runtime_collisions
                        ),
                        "candidate_csab_collision_count": len(candidate_collisions),
                        "candidate_csab_collision_n64_names": ";".join(
                            str(entry.get("n64_name") or "") for entry in candidate_collisions
                        ),
                    }
                    measurements.append(candidate_row)
                    if row_issues:
                        issues.extend(row_issues)

                ranked = sorted(measurements, key=semantic_alias_ambiguity_candidate_sort_key)
                for rank, candidate_row in enumerate(ranked, start=1):
                    candidate_row["candidate_rank"] = rank
                best = ranked[0] if ranked else None
                second = ranked[1] if len(ranked) > 1 else None
                arbitration_class = semantic_alias_ambiguity_arbitration_class(best)
                arbitration_class_counts[arbitration_class] += 1
                if best is not None:
                    best["is_pose_best_candidate"] = True
                    best["arbitration_class"] = arbitration_class
                    best_candidate_temporal_counts[str(best.get("temporal_severity") or "unknown")] += 1
                    if best.get("reference_envelope_status") == "inside_reference_envelope":
                        pose_best_inside_count += 1
                for candidate_row in ranked:
                    if candidate_row.get("arbitration_class") is None:
                        candidate_row["arbitration_class"] = arbitration_class
                    candidate_rows.append(candidate_row)
                arbitration_rows.append(
                    semantic_alias_ambiguity_arbitration_row(
                        record,
                        ranked,
                        best,
                        second,
                        arbitration_class,
                    )
                )
    except zipfile.BadZipFile as exc:
        return {
            "status": "invalid_n64_base_o2r",
            "ambiguous_record_count": len(ambiguous_records),
            "candidate_measurement_count": 0,
            "candidate_rows": [],
            "arbitration_rows": [],
            "issue_count": 1,
            "sample_issues": [{"type": "invalid_n64_base_o2r", "message": str(exc)}],
        }

    base_ready_count = (
        int(semantic_alias_bake_contract_ledger.get("promoted_or_accepted_or_bake_contract_ready_count") or 0)
        if isinstance(semantic_alias_bake_contract_ledger, dict)
        else 0
    )
    return {
        "status": "arbitration_ready" if arbitration_rows else "no_ambiguous_candidates",
        "policy": {
            "scope": "pose-metric arbitration for N64 animations with multiple OOT3D CSAB alias candidates",
            "metric_source": "same normalized skeleton-bounds pose metric used by the N64 reference audit",
            "arbitration_policy": (
                "selects a pose-best candidate for ownership/bake review; it does not promote or accept "
                "the mapping when the selected CSAB is already owned by another route"
            ),
        },
        "ambiguous_record_count": len(ambiguous_records),
        "arbitrated_record_count": len(arbitration_rows),
        "candidate_measurement_count": len(candidate_rows),
        "measured_candidate_count": measured_candidate_count,
        "inside_reference_envelope_candidate_count": inside_candidate_count,
        "pose_best_inside_reference_envelope_count": pose_best_inside_count,
        "promoted_or_accepted_or_bake_contract_ready_count": base_ready_count,
        "promoted_or_accepted_or_bake_contract_ready_or_pose_arbitrated_count": base_ready_count + pose_best_inside_count,
        "n64_player_animation_count": total_n64_animation_count,
        "promoted_or_accepted_or_bake_contract_ready_or_pose_arbitrated_percent": round(
            ratio(base_ready_count + pose_best_inside_count, total_n64_animation_count) * 100.0,
            3,
        ),
        "arbitration_class_counts": sorted_counter(arbitration_class_counts),
        "candidate_pose_metric_status_counts": sorted_counter(candidate_status_counts),
        "candidate_reference_envelope_status_counts": sorted_counter(envelope_counts),
        "best_candidate_temporal_severity_counts": sorted_counter(best_candidate_temporal_counts),
        "reference_envelope": reference_envelope,
        "issue_count": len(issues),
        "sample_issues": issues[:sample_limit],
        "sample_arbitration_rows": arbitration_rows[:sample_limit],
        "arbitration_rows": arbitration_rows,
        "candidate_rows": sorted(
            candidate_rows,
            key=lambda item: (
                int(item.get("index") or 0),
                int(item.get("candidate_rank") or 999),
                int(item.get("ambiguous_candidate_index") or 0),
            ),
        ),
    }


def ambiguous_candidate_pose_record(
    record: dict[str, object],
    candidate: dict[str, object],
    candidate_index: int,
) -> dict[str, object]:
    n64_frame_count = int(record.get("n64_frame_count") or 0)
    oot3d_frame_count = int(candidate.get("oot3d_frame_slot_count") or 0)
    candidate_record = {
        **record,
        "status": "candidate_single_csab_unpromoted",
        "candidate_ambiguous_source_index": candidate_index,
        "csab_name": candidate.get("csab_name"),
        "oot3d_stem": candidate.get("oot3d_stem"),
        "oot3d_normalized_key": candidate.get("oot3d_normalized_key"),
        "oot3d_frame_slot_count": oot3d_frame_count,
        "alias_stem": candidate.get("alias_stem"),
        "alias_policy": candidate.get("alias_policy"),
        "candidate_promotion_analysis": {
            "family": candidate_family_for_n64_stem(str(record.get("n64_stem") or "")),
            "temporal_severity": candidate_temporal_severity(n64_frame_count, oot3d_frame_count),
            "frame_delta_oot3d_minus_n64": oot3d_frame_count - n64_frame_count,
            "duration_ratio_oot3d_per_n64": (
                round(float(oot3d_frame_count) / float(n64_frame_count), 6)
                if n64_frame_count > 0
                else None
            ),
        },
    }
    return candidate_record


def semantic_alias_ambiguity_candidate_sort_key(row: dict[str, object]) -> tuple[object, ...]:
    return (
        0 if row.get("pose_metric_status") == "measured" else 1,
        candidate_pose_envelope_priority(str(row.get("reference_envelope_status") or "")),
        float(row.get("normalized_extent_max_abs_delta_max") or 999.0),
        float(row.get("normalized_extent_mean_abs_delta_max") or 999.0),
        float(row.get("center_delta_normalized_max") or 999.0),
        candidate_pose_temporal_priority(str(row.get("temporal_severity") or "")),
        int(row.get("ambiguous_candidate_index") or 0),
    )


def semantic_alias_ambiguity_arbitration_class(best: dict[str, object] | None) -> str:
    if best is None:
        return "no_candidate_measurements"
    if best.get("pose_metric_status") != "measured":
        return "pose_best_not_measured"
    if best.get("reference_envelope_status") != "inside_reference_envelope":
        return "pose_best_outside_reference_envelope"
    if int(best.get("runtime_csab_collision_count") or 0) > 0:
        return "pose_best_reuses_promoted_csab_requires_bake_or_ownership"
    if int(best.get("candidate_csab_collision_count") or 0) > 0:
        return "pose_best_reuses_candidate_csab_requires_ownership"
    return "pose_best_no_collision_requires_route_or_capture"


def semantic_alias_ambiguity_arbitration_row(
    record: dict[str, object],
    ranked: list[dict[str, object]],
    best: dict[str, object] | None,
    second: dict[str, object] | None,
    arbitration_class: str,
) -> dict[str, object]:
    return {
        "index": record.get("index"),
        "n64_name": record.get("n64_name"),
        "n64_stem": record.get("n64_stem"),
        "n64_data_name": record.get("n64_data_name"),
        "n64_frame_count": record.get("n64_frame_count"),
        "candidate_count": len(ranked),
        "measured_candidate_count": sum(1 for row in ranked if row.get("pose_metric_status") == "measured"),
        "inside_reference_envelope_candidate_count": sum(
            1 for row in ranked if row.get("reference_envelope_status") == "inside_reference_envelope"
        ),
        "arbitration_class": arbitration_class,
        "best_csab_name": best.get("csab_name") if best is not None else None,
        "best_oot3d_stem": best.get("oot3d_stem") if best is not None else None,
        "best_temporal_severity": best.get("temporal_severity") if best is not None else None,
        "best_frame_delta_oot3d_minus_n64": best.get("frame_delta_oot3d_minus_n64") if best is not None else None,
        "best_duration_ratio_oot3d_per_n64": best.get("duration_ratio_oot3d_per_n64") if best is not None else None,
        "best_normalized_extent_max_abs_delta_max": (
            best.get("normalized_extent_max_abs_delta_max") if best is not None else None
        ),
        "best_normalized_extent_mean_abs_delta_max": (
            best.get("normalized_extent_mean_abs_delta_max") if best is not None else None
        ),
        "best_center_delta_normalized_max": best.get("center_delta_normalized_max") if best is not None else None,
        "second_csab_name": second.get("csab_name") if second is not None else None,
        "second_normalized_extent_max_abs_delta_max": (
            second.get("normalized_extent_max_abs_delta_max") if second is not None else None
        ),
        "pose_best_margin_vs_second_normalized_extent_max_abs_delta": (
            round(
                float(second.get("normalized_extent_max_abs_delta_max") or 0.0)
                - float(best.get("normalized_extent_max_abs_delta_max") or 0.0),
                6,
            )
            if best is not None and second is not None
            else None
        ),
        "best_runtime_csab_collision_n64_names": (
            best.get("runtime_csab_collision_n64_names") if best is not None else None
        ),
        "best_candidate_csab_collision_n64_names": (
            best.get("candidate_csab_collision_n64_names") if best is not None else None
        ),
        "required_next_step": semantic_alias_ambiguity_required_next_step(arbitration_class),
    }


def semantic_alias_ambiguity_required_next_step(arbitration_class: str) -> str:
    if arbitration_class == "pose_best_reuses_promoted_csab_requires_bake_or_ownership":
        return "treat pose-best CSAB as bake/fallback source only after promoted-route ownership review"
    if arbitration_class == "pose_best_reuses_candidate_csab_requires_ownership":
        return "choose candidate owner or create distinct baked derivatives before semantic overlay acceptance"
    if arbitration_class == "pose_best_no_collision_requires_route_or_capture":
        return "find route/capture evidence before semantic overlay acceptance"
    if arbitration_class == "pose_best_outside_reference_envelope":
        return "do not use pose-best candidate without stronger route proof or manual animation review"
    if arbitration_class == "pose_best_not_measured":
        return "repair pose metric inputs before ambiguity arbitration"
    return "manual ambiguity review"


def unresolved_near_candidate_pose_metric_analysis(
    reference_audit: dict[str, object],
    records: list[dict[str, object]],
    unresolved_near_candidate_queue: list[dict[str, object]],
    runtime_entries: list[dict[str, object]],
    sample_limit: int,
    *,
    include_anonymous_numeric: bool = False,
) -> dict[str, object]:
    named_unmapped_records = [
        record
        for record in records
        if record.get("status") == "unmapped_no_candidate_csab"
        and record.get("unresolved_route_analysis") != "anonymous_numeric_symbol"
    ]
    anonymous_numeric_records = [
        record
        for record in records
        if record.get("status") == "unmapped_no_candidate_csab"
        and record.get("unresolved_route_analysis") == "anonymous_numeric_symbol"
    ]
    review_records = anonymous_numeric_records if include_anonymous_numeric else named_unmapped_records
    records_by_n64_name = {str(record.get("n64_name") or ""): record for record in review_records}
    source_files = reference_audit.get("source_files") if isinstance(reference_audit.get("source_files"), dict) else {}
    target = reference_audit.get("oot3d_target") if isinstance(reference_audit.get("oot3d_target"), dict) else {}
    n64_reference = (
        reference_audit.get("n64_reference")
        if isinstance(reference_audit.get("n64_reference"), dict)
        else {}
    )
    skeleton_pose_reference = (
        n64_reference.get("skeleton_pose_reference")
        if isinstance(n64_reference.get("skeleton_pose_reference"), dict)
        else {}
    )
    pose_batch_manifest_path = optional_existing_path(source_files.get("oot3d_pose_batch_manifest"))
    n64_base_o2r_path = optional_existing_path(source_files.get("n64_base_o2r"))
    if pose_batch_manifest_path is None or n64_base_o2r_path is None:
        return {
            "status": "missing_inputs",
            "named_unmapped_record_count": len(named_unmapped_records),
            "anonymous_numeric_record_count": len(anonymous_numeric_records),
            "review_record_count": len(review_records),
            "near_candidate_count": len(unresolved_near_candidate_queue),
            "candidate_rows": [],
            "best_rows": [],
            "issue_count": 1,
            "sample_issues": [
                {
                    "type": "missing_pose_metric_inputs",
                    "oot3d_pose_batch_manifest": source_files.get("oot3d_pose_batch_manifest"),
                    "n64_base_o2r": source_files.get("n64_base_o2r"),
                }
            ],
        }
    if skeleton_pose_reference.get("status") != "decoded":
        return {
            "status": "missing_n64_skeleton_pose_reference",
            "named_unmapped_record_count": len(named_unmapped_records),
            "anonymous_numeric_record_count": len(anonymous_numeric_records),
            "review_record_count": len(review_records),
            "near_candidate_count": len(unresolved_near_candidate_queue),
            "candidate_rows": [],
            "best_rows": [],
            "issue_count": 1,
            "sample_issues": [
                {
                    "type": "missing_n64_skeleton_pose_reference",
                    "skeleton_pose_reference_status": skeleton_pose_reference.get("status"),
                }
            ],
        }

    pose_batch = load_json(pose_batch_manifest_path)
    pose_records = indexed_pose_batch_records(
        pose_batch,
        str(target.get("archive_path") or ""),
        str(target.get("target_cmb_name") or ""),
    )
    reference_envelope = reference_pose_metric_envelope(reference_audit.get("pose_error_metric"))
    promoted_by_csab = group_entries_by_csab_name(runtime_entries)
    single_candidate_by_csab = group_mapping_records_by_csab_name(records, "candidate_single_csab_unpromoted")
    queue_by_n64_name: dict[str, list[dict[str, object]]] = {}
    for near_candidate in unresolved_near_candidate_queue:
        if not isinstance(near_candidate, dict):
            continue
        queue_by_n64_name.setdefault(str(near_candidate.get("n64_name") or ""), []).append(near_candidate)

    candidate_rows: list[dict[str, object]] = []
    best_rows: list[dict[str, object]] = []
    issues: list[dict[str, object]] = []
    status_counts: Counter[str] = Counter()
    envelope_counts: Counter[str] = Counter()
    best_class_counts: Counter[str] = Counter()
    best_route_analysis_counts: Counter[str] = Counter()
    best_temporal_severity_counts: Counter[str] = Counter()
    measured_candidate_count = 0
    inside_candidate_count = 0
    pose_best_inside_count = 0

    try:
        with zipfile.ZipFile(n64_base_o2r_path) as archive:
            for n64_name, record in sorted(records_by_n64_name.items(), key=lambda item: int(item[1].get("index") or 0)):
                measurements: list[dict[str, object]] = []
                near_candidates = queue_by_n64_name.get(n64_name, [])
                for candidate_index, near_candidate in enumerate(near_candidates):
                    candidate_record = unresolved_near_candidate_pose_record(record, near_candidate, candidate_index)
                    row, _pair_metrics, row_issues = candidate_alias_pose_metric_record(
                        archive,
                        pose_batch_manifest_path,
                        pose_batch,
                        pose_records,
                        skeleton_pose_reference,
                        reference_envelope,
                        candidate_record,
                    )
                    csab_name = str(row.get("csab_name") or "")
                    runtime_collisions = [
                        entry
                        for entry in promoted_by_csab.get(csab_name, [])
                        if str(entry.get("n64_name") or "") != str(record.get("n64_name") or "")
                    ]
                    candidate_collisions = [
                        entry
                        for entry in single_candidate_by_csab.get(csab_name, [])
                        if str(entry.get("n64_name") or "") != str(record.get("n64_name") or "")
                    ]
                    measured_candidate_count += 1 if row.get("pose_metric_status") == "measured" else 0
                    inside_candidate_count += 1 if row.get("reference_envelope_status") == "inside_reference_envelope" else 0
                    status_counts[str(row.get("pose_metric_status") or "unknown")] += 1
                    envelope_counts[str(row.get("reference_envelope_status") or "unknown")] += 1
                    candidate_row = {
                        **row,
                        "near_candidate_index": candidate_index,
                        "near_candidate_score": near_candidate.get("score"),
                        "near_candidate_shared_tokens": near_candidate.get("shared_tokens"),
                        "near_candidate_source": near_candidate.get("candidate_source"),
                        "near_candidate_table_context_family": near_candidate.get("table_context_family"),
                        "near_candidate_previous_named_stems": near_candidate.get("previous_named_stems"),
                        "near_candidate_next_named_stems": near_candidate.get("next_named_stems"),
                        "near_candidate_oot3d_table_distance": near_candidate.get("oot3d_table_distance"),
                        "unresolved_route_analysis": record.get("unresolved_route_analysis"),
                        "unresolved_resolution_class": record.get("unresolved_resolution_class"),
                        "near_candidate_rank": None,
                        "is_pose_best_near_candidate": False,
                        "near_pose_review_class": None,
                        "runtime_csab_collision_count": len(runtime_collisions),
                        "runtime_csab_collision_n64_names": ";".join(
                            str(entry.get("n64_name") or "") for entry in runtime_collisions
                        ),
                        "candidate_csab_collision_count": len(candidate_collisions),
                        "candidate_csab_collision_n64_names": ";".join(
                            str(entry.get("n64_name") or "") for entry in candidate_collisions
                        ),
                    }
                    measurements.append(candidate_row)
                    if row_issues:
                        issues.extend(row_issues)

                ranked = sorted(measurements, key=unresolved_near_candidate_pose_sort_key)
                for rank, candidate_row in enumerate(ranked, start=1):
                    candidate_row["near_candidate_rank"] = rank
                best = ranked[0] if ranked else None
                review_class = unresolved_near_candidate_pose_review_class(best)
                best_class_counts[review_class] += 1
                best_route_analysis_counts[str(record.get("unresolved_route_analysis") or "unknown")] += 1
                if best is not None:
                    best["is_pose_best_near_candidate"] = True
                    best["near_pose_review_class"] = review_class
                    best_temporal_severity_counts[str(best.get("temporal_severity") or "unknown")] += 1
                    if best.get("reference_envelope_status") == "inside_reference_envelope":
                        pose_best_inside_count += 1
                for candidate_row in ranked:
                    if candidate_row.get("near_pose_review_class") is None:
                        candidate_row["near_pose_review_class"] = review_class
                    candidate_rows.append(candidate_row)
                best_rows.append(unresolved_near_candidate_best_row(record, ranked, best, review_class))
    except zipfile.BadZipFile as exc:
        return {
            "status": "invalid_n64_base_o2r",
            "named_unmapped_record_count": len(named_unmapped_records),
            "anonymous_numeric_record_count": len(anonymous_numeric_records),
            "review_record_count": len(review_records),
            "near_candidate_count": len(unresolved_near_candidate_queue),
            "candidate_rows": [],
            "best_rows": [],
            "issue_count": 1,
            "sample_issues": [{"type": "invalid_n64_base_o2r", "message": str(exc)}],
        }

    return {
        "status": "measured" if measured_candidate_count > 0 else "not_measured",
        "policy": {
            "scope": (
                "pose metric review for anonymous numeric records with table-context CSAB candidates"
                if include_anonymous_numeric
                else "pose metric review for named unmapped records with token/frame near-candidate CSABs"
            ),
            "metric_source": "same normalized skeleton-bounds pose metric used by the N64 reference audit",
            "promotion_policy": (
                "near-candidate pose best rows are triage only; semantic alias, route/callsite, "
                "ownership, or explicit bake-source evidence is still required before mapping acceptance"
            ),
        },
        "named_unmapped_record_count": len(named_unmapped_records),
        "anonymous_numeric_record_count": len(anonymous_numeric_records),
        "review_record_count": len(review_records),
        "near_candidate_count": len(candidate_rows),
        "measured_candidate_count": measured_candidate_count,
        "inside_reference_envelope_candidate_count": inside_candidate_count,
        "pose_best_inside_reference_envelope_count": pose_best_inside_count,
        "best_row_count": len(best_rows),
        "best_review_class_counts": sorted_counter(best_class_counts),
        "best_unresolved_route_analysis_counts": sorted_counter(best_route_analysis_counts),
        "best_temporal_severity_counts": sorted_counter(best_temporal_severity_counts),
        "candidate_pose_metric_status_counts": sorted_counter(status_counts),
        "candidate_reference_envelope_status_counts": sorted_counter(envelope_counts),
        "reference_envelope": reference_envelope,
        "issue_count": len(issues),
        "sample_issues": issues[:sample_limit],
        "sample_best_rows": best_rows[:sample_limit],
        "best_rows": best_rows,
        "candidate_rows": sorted(
            candidate_rows,
            key=lambda item: (
                int(item.get("index") or 0),
                int(item.get("near_candidate_rank") or 999),
                int(item.get("near_candidate_index") or 0),
            ),
        ),
    }


def unresolved_near_candidate_pose_record(
    record: dict[str, object],
    near_candidate: dict[str, object],
    candidate_index: int,
) -> dict[str, object]:
    n64_frame_count = int(record.get("n64_frame_count") or 0)
    oot3d_frame_count = int(near_candidate.get("oot3d_frame_slot_count") or 0)
    return {
        **record,
        "status": "candidate_single_csab_unpromoted",
        "near_candidate_index": candidate_index,
        "csab_name": near_candidate.get("csab_name"),
        "oot3d_stem": near_candidate.get("oot3d_stem"),
        "oot3d_normalized_key": ambiguity_normalized_stem(str(near_candidate.get("oot3d_stem") or "")),
        "oot3d_frame_slot_count": oot3d_frame_count,
        "candidate_promotion_analysis": {
            "family": (
                near_candidate.get("table_context_family")
                or candidate_family_for_n64_stem(str(record.get("n64_stem") or ""))
            ),
            "temporal_severity": candidate_temporal_severity(n64_frame_count, oot3d_frame_count),
            "frame_delta_oot3d_minus_n64": oot3d_frame_count - n64_frame_count,
            "duration_ratio_oot3d_per_n64": (
                round(float(oot3d_frame_count) / float(n64_frame_count), 6)
                if n64_frame_count > 0
                else None
            ),
        },
    }


def unresolved_near_candidate_pose_sort_key(row: dict[str, object]) -> tuple[object, ...]:
    return (
        0 if row.get("pose_metric_status") == "measured" else 1,
        candidate_pose_envelope_priority(str(row.get("reference_envelope_status") or "")),
        float(row.get("normalized_extent_max_abs_delta_max") or 999.0),
        float(row.get("normalized_extent_mean_abs_delta_max") or 999.0),
        float(row.get("center_delta_normalized_max") or 999.0),
        -int(row.get("near_candidate_score") or 0),
        int(abs(int(row.get("frame_delta_oot3d_minus_n64") or 0))),
        int(row.get("near_candidate_index") or 0),
    )


def unresolved_near_candidate_pose_review_class(best: dict[str, object] | None) -> str:
    if best is None:
        return "no_near_candidate_to_measure"
    if best.get("pose_metric_status") != "measured":
        return "near_pose_best_not_measured"
    if best.get("reference_envelope_status") != "inside_reference_envelope":
        return "near_pose_best_outside_reference_envelope"
    if int(best.get("runtime_csab_collision_count") or 0) > 0:
        return "near_pose_best_reuses_promoted_csab_requires_bake_or_ownership"
    if int(best.get("candidate_csab_collision_count") or 0) > 0:
        return "near_pose_best_reuses_candidate_csab_requires_ownership"
    return "near_pose_best_inside_envelope_requires_semantic_route_evidence"


def unresolved_near_candidate_best_row(
    record: dict[str, object],
    ranked: list[dict[str, object]],
    best: dict[str, object] | None,
    review_class: str,
) -> dict[str, object]:
    second = ranked[1] if len(ranked) > 1 else None
    return {
        "index": record.get("index"),
        "n64_name": record.get("n64_name"),
        "n64_stem": record.get("n64_stem"),
        "n64_data_name": record.get("n64_data_name"),
        "n64_frame_count": record.get("n64_frame_count"),
        "unresolved_route_analysis": record.get("unresolved_route_analysis"),
        "unresolved_resolution_class": record.get("unresolved_resolution_class"),
        "near_candidate_count": len(ranked),
        "measured_near_candidate_count": sum(1 for row in ranked if row.get("pose_metric_status") == "measured"),
        "inside_reference_envelope_near_candidate_count": sum(
            1 for row in ranked if row.get("reference_envelope_status") == "inside_reference_envelope"
        ),
        "near_pose_review_class": review_class,
        "best_csab_name": best.get("csab_name") if best is not None else None,
        "best_oot3d_stem": best.get("oot3d_stem") if best is not None else None,
        "best_near_candidate_score": best.get("near_candidate_score") if best is not None else None,
        "best_near_candidate_shared_tokens": best.get("near_candidate_shared_tokens") if best is not None else None,
        "best_temporal_severity": best.get("temporal_severity") if best is not None else None,
        "best_frame_delta_oot3d_minus_n64": best.get("frame_delta_oot3d_minus_n64") if best is not None else None,
        "best_duration_ratio_oot3d_per_n64": best.get("duration_ratio_oot3d_per_n64") if best is not None else None,
        "best_normalized_extent_max_abs_delta_max": (
            best.get("normalized_extent_max_abs_delta_max") if best is not None else None
        ),
        "best_center_delta_normalized_max": best.get("center_delta_normalized_max") if best is not None else None,
        "second_csab_name": second.get("csab_name") if second is not None else None,
        "second_normalized_extent_max_abs_delta_max": (
            second.get("normalized_extent_max_abs_delta_max") if second is not None else None
        ),
        "best_runtime_csab_collision_n64_names": (
            best.get("runtime_csab_collision_n64_names") if best is not None else None
        ),
        "best_candidate_csab_collision_n64_names": (
            best.get("candidate_csab_collision_n64_names") if best is not None else None
        ),
        "required_next_step": unresolved_near_candidate_pose_required_next_step(review_class),
    }


def unresolved_near_candidate_pose_required_next_step(review_class: str) -> str:
    if review_class == "near_pose_best_inside_envelope_requires_semantic_route_evidence":
        return "prove semantic alias route/callsite or define explicit bake source before mapping acceptance"
    if review_class == "near_pose_best_reuses_promoted_csab_requires_bake_or_ownership":
        return "use as bake/fallback source only after promoted-route ownership review"
    if review_class == "near_pose_best_reuses_candidate_csab_requires_ownership":
        return "choose candidate owner or create distinct baked derivatives before acceptance"
    if review_class == "near_pose_best_outside_reference_envelope":
        return "treat as weak near-candidate; seek omission proof, runtime capture, or different source"
    if review_class == "near_pose_best_not_measured":
        return "repair pose metric inputs before near-candidate review"
    return "manual near-candidate review"


def unresolved_near_candidate_bake_contract_analysis(
    unresolved_near_candidate_pose_metric: dict[str, object],
    *,
    promoted_runtime_mapping_count: int,
    total_n64_animation_count: int,
    sample_limit: int,
) -> dict[str, object]:
    best_rows = (
        unresolved_near_candidate_pose_metric.get("best_rows", [])
        if isinstance(unresolved_near_candidate_pose_metric, dict)
        else []
    )
    contracts: list[dict[str, object]] = []
    status_counts: Counter[str] = Counter()
    materialization_counts: Counter[str] = Counter()
    temporal_severity_counts: Counter[str] = Counter()
    review_class_counts: Counter[str] = Counter()
    for row in best_rows:
        if not isinstance(row, dict):
            continue
        review_class = str(row.get("near_pose_review_class") or "")
        contract_status = unresolved_near_candidate_bake_contract_status(review_class)
        materialization_class = unresolved_near_candidate_bake_materialization_class(row)
        status_counts[contract_status] += 1
        materialization_counts[materialization_class] += 1
        temporal_severity_counts[str(row.get("best_temporal_severity") or "unknown")] += 1
        review_class_counts[review_class] += 1
        contracts.append(
            {
                "index": row.get("index"),
                "n64_name": row.get("n64_name"),
                "n64_stem": row.get("n64_stem"),
                "n64_data_name": row.get("n64_data_name"),
                "source_csab_name": row.get("best_csab_name"),
                "source_oot3d_stem": row.get("best_oot3d_stem"),
                "contract_status": contract_status,
                "contract_kind": "named_unmapped_near_candidate_bake_or_ownership",
                "near_pose_review_class": review_class,
                "materialization_class": materialization_class,
                "source_frame_slot_count": (
                    int(row.get("n64_frame_count") or 0)
                    + int(row.get("best_frame_delta_oot3d_minus_n64") or 0)
                ),
                "target_n64_frame_count": row.get("n64_frame_count"),
                "frame_delta_oot3d_minus_n64": row.get("best_frame_delta_oot3d_minus_n64"),
                "duration_ratio_oot3d_per_n64": row.get("best_duration_ratio_oot3d_per_n64"),
                "temporal_severity": row.get("best_temporal_severity"),
                "source_format": "oot3d_csab",
                "target_semantic_slot": "n64_playeranimation_named_unmapped",
                "unresolved_route_analysis": row.get("unresolved_route_analysis"),
                "unresolved_resolution_class": row.get("unresolved_resolution_class"),
                "output_resource_policy": unresolved_near_candidate_bake_output_resource_policy(contract_status),
                "frame_mapping_contract": unresolved_near_candidate_frame_mapping_contract(row),
                "ownership_policy": unresolved_near_candidate_bake_ownership_policy(contract_status),
                "route_evidence_status": "route_or_capture_evidence_required_for_named_unmapped_record",
                "near_candidate_count": row.get("near_candidate_count"),
                "measured_near_candidate_count": row.get("measured_near_candidate_count"),
                "inside_reference_envelope_near_candidate_count": row.get(
                    "inside_reference_envelope_near_candidate_count"
                ),
                "best_near_candidate_score": row.get("best_near_candidate_score"),
                "best_near_candidate_shared_tokens": row.get("best_near_candidate_shared_tokens"),
                "best_normalized_extent_max_abs_delta_max": row.get("best_normalized_extent_max_abs_delta_max"),
                "best_center_delta_normalized_max": row.get("best_center_delta_normalized_max"),
                "second_csab_name": row.get("second_csab_name"),
                "second_normalized_extent_max_abs_delta_max": row.get("second_normalized_extent_max_abs_delta_max"),
                "runtime_csab_collision_n64_names": row.get("best_runtime_csab_collision_n64_names"),
                "candidate_csab_collision_n64_names": row.get("best_candidate_csab_collision_n64_names"),
                "required_next_step": unresolved_near_candidate_bake_contract_next_step(contract_status),
            }
        )

    sorted_contracts = sorted(
        contracts,
        key=lambda item: (
            unresolved_near_candidate_bake_contract_priority(str(item.get("contract_status") or "")),
            candidate_pose_temporal_priority(str(item.get("temporal_severity") or "")),
            int(item.get("index") or 0),
        ),
    )
    named_unmapped_contract_count = len(sorted_contracts)
    diagnostic_or_contract_count = promoted_runtime_mapping_count + named_unmapped_contract_count
    return {
        "status": "contract_ledger_ready" if sorted_contracts else "no_named_unmapped_near_candidate_contracts",
        "policy": {
            "scope": "best-pose near-candidate contracts for named N64 PlayerAnimation rows without an OOT3D CSAB alias candidate",
            "acceptance_policy": (
                "these contracts are diagnostic bake/ownership sources only; none are accepted as runtime "
                "mappings until route ownership, callsite evidence, or materialized baked derivatives are proven"
            ),
            "coverage_policy": (
                "counts are reported separately from promoted, accepted, and bake-ready semantic mappings "
                "because every current near-candidate contract is blocked by CSAB ownership"
            ),
        },
        "contract_count": named_unmapped_contract_count,
        "blocked_or_pending_contract_count": named_unmapped_contract_count,
        "promoted_runtime_mapping_count": promoted_runtime_mapping_count,
        "promoted_or_named_unmapped_contract_count": diagnostic_or_contract_count,
        "n64_player_animation_count": total_n64_animation_count,
        "promoted_or_named_unmapped_contract_percent": round(
            ratio(diagnostic_or_contract_count, total_n64_animation_count) * 100.0,
            3,
        ),
        "contract_status_counts": sorted_counter(status_counts),
        "near_pose_review_class_counts": sorted_counter(review_class_counts),
        "materialization_class_counts": sorted_counter(materialization_counts),
        "temporal_severity_counts": sorted_counter(temporal_severity_counts),
        "sample_contracts": sorted_contracts[:sample_limit],
        "contracts": sorted_contracts,
    }


def unresolved_near_candidate_bake_contract_status(review_class: str) -> str:
    if review_class == "near_pose_best_reuses_promoted_csab_requires_bake_or_ownership":
        return "blocked_promoted_csab_reuse"
    if review_class == "near_pose_best_reuses_candidate_csab_requires_ownership":
        return "blocked_candidate_csab_reuse"
    if review_class == "near_pose_best_inside_envelope_requires_semantic_route_evidence":
        return "requires_route_or_capture_evidence"
    if review_class == "near_pose_best_outside_reference_envelope":
        return "blocked_pose_metric_outside_reference_envelope"
    if review_class == "near_pose_best_not_measured":
        return "blocked_pose_metric_not_measured"
    return "manual_near_candidate_bake_review"


def unresolved_near_candidate_bake_materialization_class(row: dict[str, object]) -> str:
    severity = str(row.get("best_temporal_severity") or "")
    delta = int(row.get("best_frame_delta_oot3d_minus_n64") or 0)
    if severity == "same_frame_count" or delta == 0:
        return "same_frame_alias_or_distinct_derivative"
    if severity == "small_delta":
        return "proportional_resample_source_csab_to_n64_frame_count"
    if delta < 0:
        return "expand_source_csab_to_n64_frame_count"
    if delta > 0:
        return "compress_source_csab_to_n64_frame_count"
    return "manual_temporal_materialization"


def unresolved_near_candidate_frame_mapping_contract(row: dict[str, object]) -> str:
    target_frames = int(row.get("n64_frame_count") or 0)
    source_frames = target_frames + int(row.get("best_frame_delta_oot3d_minus_n64") or 0)
    if source_frames <= 0 or target_frames <= 0:
        return "manual frame mapping required because source or target frame count is missing"
    if source_frames == target_frames:
        return "source and target have the same frame count; bake may copy frames after ownership is resolved"
    if source_frames == 1 or target_frames == 1:
        return (
            f"sample source CSAB frame 0 across target N64 frame count {target_frames}; "
            "manual hold-frame review required"
        )
    return (
        f"for target frame i in [0,{target_frames - 1}], sample source CSAB at "
        f"i * ({source_frames - 1}) / ({target_frames - 1}) and bake onto the N64 frame count"
    )


def unresolved_near_candidate_bake_output_resource_policy(contract_status: str) -> str:
    if contract_status == "blocked_promoted_csab_reuse":
        return "do_not_materialize_until_promoted_csab_ownership_is_resolved"
    if contract_status == "blocked_candidate_csab_reuse":
        return "do_not_materialize_until_candidate_owner_is_selected_or_distinct_derivatives_are_defined"
    if contract_status == "requires_route_or_capture_evidence":
        return "do_not_materialize_until_route_or_capture_evidence_exists"
    return "manual_output_policy_required"


def unresolved_near_candidate_bake_ownership_policy(contract_status: str) -> str:
    if contract_status == "blocked_promoted_csab_reuse":
        return "source_csab_is_already_owned_by_promoted_runtime_mapping"
    if contract_status == "blocked_candidate_csab_reuse":
        return "source_csab_is_claimed_by_other_unpromoted_candidate_rows"
    if contract_status == "requires_route_or_capture_evidence":
        return "source_csab_ownership_unproven_until_route_evidence_exists"
    return "manual_ownership_review_required"


def unresolved_near_candidate_bake_contract_next_step(contract_status: str) -> str:
    if contract_status == "blocked_promoted_csab_reuse":
        return "prove intentional promoted CSAB reuse or define a distinct baked derivative after ownership review"
    if contract_status == "blocked_candidate_csab_reuse":
        return "choose the candidate owner or define distinct baked derivatives before mapping acceptance"
    if contract_status == "requires_route_or_capture_evidence":
        return "find route/callsite evidence or runtime capture before materializing a bake source"
    if contract_status == "blocked_pose_metric_outside_reference_envelope":
        return "seek a different source or prove intentional omission before bake planning"
    if contract_status == "blocked_pose_metric_not_measured":
        return "repair pose metric evidence before bake planning"
    return "manual near-candidate bake review"


def unresolved_near_candidate_bake_contract_priority(contract_status: str) -> int:
    return {
        "requires_route_or_capture_evidence": 0,
        "blocked_candidate_csab_reuse": 1,
        "blocked_promoted_csab_reuse": 2,
        "blocked_pose_metric_outside_reference_envelope": 3,
        "blocked_pose_metric_not_measured": 4,
        "manual_near_candidate_bake_review": 5,
    }.get(contract_status, 9)


SEMANTIC_RESOLUTION_SOURCE_IDENTIFIED_STATUSES = {
    "runtime_promoted_mapping",
    "accepted_semantic_overlay",
    "bake_contract_ready_pending_materialization",
    "pose_arbitrated_pending_ownership",
    "n64_payload_duplicate_alias_of_runtime_mapping",
}


def n64_payload_duplicate_alias_analysis(
    reference_audit: dict[str, object],
    records: list[dict[str, object]],
    sample_limit: int,
) -> dict[str, object]:
    source_files = reference_audit.get("source_files") if isinstance(reference_audit.get("source_files"), dict) else {}
    n64_base_o2r_path = optional_existing_path(source_files.get("n64_base_o2r"))
    anonymous_records = [
        record
        for record in records
        if record.get("unresolved_route_analysis") == "anonymous_numeric_symbol"
    ]
    if n64_base_o2r_path is None:
        return {
            "status": "missing_n64_base_o2r",
            "anonymous_numeric_record_count": len(anonymous_records),
            "duplicate_alias_count": 0,
            "aliases": [],
            "sample_aliases": [],
        }

    payload_hash_records: dict[str, list[dict[str, object]]] = {}
    payload_hash_by_name: dict[str, str] = {}
    try:
        with zipfile.ZipFile(n64_base_o2r_path) as archive:
            for record in records:
                payload = read_player_animation_raw_payload(
                    archive,
                    {
                        **record,
                        "data_name": record.get("data_name") or record.get("n64_data_name"),
                    },
                )
                if payload is None:
                    continue
                payload_hash = hashlib.sha1(payload).hexdigest()
                payload_hash_by_name[str(record.get("n64_name") or "")] = payload_hash
                payload_hash_records.setdefault(payload_hash, []).append(record)
    except zipfile.BadZipFile as exc:
        return {
            "status": "invalid_n64_base_o2r",
            "anonymous_numeric_record_count": len(anonymous_records),
            "duplicate_alias_count": 0,
            "aliases": [],
            "sample_aliases": [],
            "issue": str(exc),
        }

    aliases: list[dict[str, object]] = []
    for record in anonymous_records:
        payload_hash = payload_hash_by_name.get(str(record.get("n64_name") or ""))
        if not payload_hash:
            continue
        duplicate_candidates = [
            candidate
            for candidate in payload_hash_records.get(payload_hash, [])
            if str(candidate.get("n64_name") or "") != str(record.get("n64_name") or "")
            and candidate.get("unresolved_route_analysis") != "anonymous_numeric_symbol"
        ]
        if not duplicate_candidates:
            continue
        duplicate_candidates.sort(key=n64_payload_duplicate_alias_target_sort_key)
        target = duplicate_candidates[0]
        aliases.append(
            {
                "n64_name": record.get("n64_name"),
                "n64_stem": record.get("n64_stem"),
                "n64_data_name": record.get("n64_data_name"),
                "n64_frame_count": record.get("n64_frame_count"),
                "payload_sha1": payload_hash,
                "duplicate_of_n64_name": target.get("n64_name"),
                "duplicate_of_n64_stem": target.get("n64_stem"),
                "duplicate_of_n64_data_name": target.get("n64_data_name"),
                "duplicate_of_status": target.get("status"),
                "duplicate_of_csab_name": target.get("csab_name"),
                "duplicate_of_oot3d_stem": target.get("oot3d_stem"),
                "duplicate_of_oot3d_frame_slot_count": target.get("oot3d_frame_slot_count"),
                "duplicate_candidate_count": len(duplicate_candidates),
                "duplicate_candidate_n64_names": ";".join(
                    str(candidate.get("n64_name") or "") for candidate in duplicate_candidates
                ),
                "alias_resolution_class": n64_payload_duplicate_alias_resolution_class(target),
                "required_next_step": n64_payload_duplicate_alias_required_next_step(target),
            }
        )

    aliases_sorted = sorted(aliases, key=lambda item: int(item.get("n64_name", "0").split("_")[-1], 16))
    class_counts = Counter(str(alias.get("alias_resolution_class") or "unknown") for alias in aliases_sorted)
    return {
        "status": "duplicate_aliases_found" if aliases_sorted else "no_duplicate_aliases_found",
        "policy": {
            "scope": "anonymous numeric N64 PlayerAnimation records with exact payload duplicate of a named record",
            "acceptance_policy": (
                "exact payload duplicates can identify semantic source inheritance, but they do not add a "
                "new promoted runtime mapping unless replacement policy explicitly accepts alias routes"
            ),
        },
        "anonymous_numeric_record_count": len(anonymous_records),
        "duplicate_alias_count": len(aliases_sorted),
        "alias_resolution_class_counts": sorted_counter(class_counts),
        "sample_aliases": aliases_sorted[:sample_limit],
        "aliases": aliases_sorted,
    }


def n64_payload_duplicate_alias_target_sort_key(record: dict[str, object]) -> tuple[object, ...]:
    status_priority = {
        "mapped_to_single_csab": 0,
        "candidate_single_csab_unpromoted": 1,
        "candidate_ambiguous_csab_unpromoted": 2,
        "unmapped_no_candidate_csab": 3,
    }.get(str(record.get("status") or ""), 9)
    return (status_priority, int(record.get("index") or 0))


def n64_payload_duplicate_alias_resolution_class(target: dict[str, object]) -> str:
    if target.get("status") == "mapped_to_single_csab":
        return "duplicate_of_runtime_promoted_mapping"
    if target.get("status") == "candidate_single_csab_unpromoted":
        return "duplicate_of_candidate_semantic_alias"
    return "duplicate_of_unaccepted_or_unresolved_record"


def n64_payload_duplicate_alias_required_next_step(target: dict[str, object]) -> str:
    if target.get("status") == "mapped_to_single_csab":
        return "accept as payload-identical alias of promoted N64 mapping or keep as diagnostic alias"
    if target.get("status") == "candidate_single_csab_unpromoted":
        return "resolve the named candidate first, then decide whether the anonymous alias inherits it"
    return "resolve the duplicate target before using it as semantic source"


def semantic_resolution_contract_analysis(
    records: list[dict[str, object]],
    semantic_alias_promotion_ledger: dict[str, object],
    semantic_alias_route_review: dict[str, object],
    semantic_alias_resample_review: dict[str, object],
    semantic_alias_accepted_overlay: dict[str, object],
    semantic_alias_bake_contract_ledger: dict[str, object],
    semantic_alias_ambiguity_arbitration: dict[str, object],
    unresolved_near_candidate_bake_contract_ledger: dict[str, object],
    anonymous_numeric_pose_metric: dict[str, object],
    n64_payload_duplicate_aliases: dict[str, object],
    sample_limit: int,
) -> dict[str, object]:
    accepted_by_name = records_by_n64_name(semantic_alias_accepted_overlay.get("accepted_overlay_entries", []))
    bake_by_name = records_by_n64_name(semantic_alias_bake_contract_ledger.get("contracts", []))
    route_review_by_name = records_by_n64_name(semantic_alias_route_review.get("rows", []))
    resample_review_by_name = records_by_n64_name(semantic_alias_resample_review.get("rows", []))
    promotion_by_name = records_by_n64_name(semantic_alias_promotion_ledger.get("ledger", []))
    near_contract_by_name = records_by_n64_name(unresolved_near_candidate_bake_contract_ledger.get("contracts", []))
    anonymous_numeric_best_by_name = records_by_n64_name(anonymous_numeric_pose_metric.get("best_rows", []))
    payload_duplicate_alias_by_name = records_by_n64_name(n64_payload_duplicate_aliases.get("aliases", []))
    pose_best_by_name = records_by_n64_name(
        [
            row
            for row in list_dicts(semantic_alias_ambiguity_arbitration.get("candidate_rows", []))
            if truthy(row.get("is_pose_best_candidate"))
        ]
    )

    rows: list[dict[str, object]] = []
    resolution_status_counts: Counter[str] = Counter()
    source_contract_kind_counts: Counter[str] = Counter()
    runtime_acceptance_counts: Counter[str] = Counter()
    semantic_source_counts: Counter[str] = Counter()
    materialization_counts: Counter[str] = Counter()
    ownership_gate_counts: Counter[str] = Counter()
    required_next_step_counts: Counter[str] = Counter()

    for record in sorted(records, key=lambda item: int(item.get("index") or 0)):
        row = semantic_resolution_contract_row(
            record,
            accepted_by_name,
            bake_by_name,
            route_review_by_name,
            resample_review_by_name,
            promotion_by_name,
            pose_best_by_name,
            near_contract_by_name,
            anonymous_numeric_best_by_name,
            payload_duplicate_alias_by_name,
        )
        rows.append(row)
        resolution_status_counts[str(row.get("semantic_resolution_status") or "unknown")] += 1
        source_contract_kind_counts[str(row.get("source_contract_kind") or "none")] += 1
        runtime_acceptance_counts[str(row.get("runtime_acceptance_status") or "unknown")] += 1
        semantic_source_counts[str(row.get("semantic_source_status") or "unknown")] += 1
        materialization_counts[str(row.get("materialization_class") or "none")] += 1
        ownership_gate_counts[str(row.get("ownership_gate") or "none")] += 1
        required_next_step_counts[str(row.get("required_next_step") or "none")] += 1

    source_identified_count = sum(
        count
        for status, count in resolution_status_counts.items()
        if status in SEMANTIC_RESOLUTION_SOURCE_IDENTIFIED_STATUSES
    )
    accepted_count = (
        resolution_status_counts.get("runtime_promoted_mapping", 0)
        + resolution_status_counts.get("accepted_semantic_overlay", 0)
    )
    materialization_pending_count = (
        resolution_status_counts.get("bake_contract_ready_pending_materialization", 0)
        + resolution_status_counts.get("pose_arbitrated_pending_ownership", 0)
    )
    payload_duplicate_alias_count = resolution_status_counts.get("n64_payload_duplicate_alias_of_runtime_mapping", 0)
    blocked_or_unresolved_count = len(rows) - source_identified_count
    return {
        "status": "partial" if blocked_or_unresolved_count else "complete",
        "format": "oot3d_link_child_animation_semantic_resolution_contract_v1",
        "policy": {
            "scope": "one final semantic accounting row for every Link child N64 PlayerAnimation record",
            "runtime_policy": (
                "only runtime_promoted_mapping rows are current promoted runtime mappings; accepted overlays and "
                "bake-ready sources stay out of the promoted runtime table"
            ),
            "source_identified_policy": (
                "semantic source identified includes promoted runtime mappings, accepted overlays, ready bake "
                "contracts, and pose-best ambiguity arbitration rows"
            ),
        },
        "row_count": len(rows),
        "runtime_promoted_mapping_count": resolution_status_counts.get("runtime_promoted_mapping", 0),
        "accepted_semantic_overlay_count": resolution_status_counts.get("accepted_semantic_overlay", 0),
        "bake_contract_ready_pending_materialization_count": resolution_status_counts.get(
            "bake_contract_ready_pending_materialization",
            0,
        ),
        "pose_arbitrated_pending_ownership_count": resolution_status_counts.get(
            "pose_arbitrated_pending_ownership",
            0,
        ),
        "n64_payload_duplicate_alias_source_count": payload_duplicate_alias_count,
        "semantic_source_identified_count": source_identified_count,
        "accepted_or_runtime_mapping_count": accepted_count,
        "materialization_pending_source_count": materialization_pending_count,
        "blocked_or_unresolved_count": blocked_or_unresolved_count,
        "semantic_source_identified_percent": round(ratio(source_identified_count, len(rows)) * 100.0, 3),
        "accepted_or_runtime_mapping_percent": round(ratio(accepted_count, len(rows)) * 100.0, 3),
        "blocked_or_unresolved_percent": round(ratio(blocked_or_unresolved_count, len(rows)) * 100.0, 3),
        "semantic_resolution_status_counts": sorted_counter(resolution_status_counts),
        "source_contract_kind_counts": sorted_counter(source_contract_kind_counts),
        "runtime_acceptance_status_counts": sorted_counter(runtime_acceptance_counts),
        "semantic_source_status_counts": sorted_counter(semantic_source_counts),
        "materialization_class_counts": sorted_counter(materialization_counts),
        "ownership_gate_counts": sorted_counter(ownership_gate_counts),
        "required_next_step_counts": sorted_counter(required_next_step_counts),
        "sample_rows": rows[:sample_limit],
        "rows": rows,
    }


def semantic_resolution_contract_row(
    record: dict[str, object],
    accepted_by_name: dict[str, dict[str, object]],
    bake_by_name: dict[str, dict[str, object]],
    route_review_by_name: dict[str, dict[str, object]],
    resample_review_by_name: dict[str, dict[str, object]],
    promotion_by_name: dict[str, dict[str, object]],
    pose_best_by_name: dict[str, dict[str, object]],
    near_contract_by_name: dict[str, dict[str, object]],
    anonymous_numeric_best_by_name: dict[str, dict[str, object]],
    payload_duplicate_alias_by_name: dict[str, dict[str, object]],
) -> dict[str, object]:
    n64_name = str(record.get("n64_name") or "")
    source: dict[str, object] = {}
    resolution_status = "manual_semantic_resolution_review"
    source_contract_kind = "none"
    runtime_acceptance_status = "not_runtime_accepted"
    semantic_source_status = "blocked_or_unresolved"
    materialization_class = ""
    ownership_gate = ""
    required_next_step = "manual semantic resolution review"

    if record.get("status") == "mapped_to_single_csab":
        source = record
        resolution_status = "runtime_promoted_mapping"
        source_contract_kind = "promoted_runtime_mapping"
        runtime_acceptance_status = "promoted_runtime_mapping"
        semantic_source_status = "semantic_source_identified"
        required_next_step = "runtime mapping already promoted; retain parity checks"
    elif n64_name in accepted_by_name:
        source = accepted_by_name[n64_name]
        resolution_status = "accepted_semantic_overlay"
        source_contract_kind = str(source.get("acceptance_class") or "accepted_semantic_alias_overlay")
        runtime_acceptance_status = "accepted_overlay_not_promoted_runtime"
        semantic_source_status = "semantic_source_identified"
        materialization_class = str(source.get("temporal_policy") or "")
        required_next_step = "keep overlay out of runtime table until final replacement policy accepts it"
    elif n64_name in bake_by_name:
        source = bake_by_name[n64_name]
        contract_status = str(source.get("contract_status") or "")
        if contract_status == "bake_contract_ready":
            resolution_status = "bake_contract_ready_pending_materialization"
            semantic_source_status = "semantic_source_identified"
            runtime_acceptance_status = "bake_source_ready_not_runtime_accepted"
        else:
            resolution_status = "blocked_temporal_bake_contract"
            semantic_source_status = "blocked_or_unresolved"
            runtime_acceptance_status = "blocked_not_runtime_accepted"
        source_contract_kind = contract_status or "semantic_alias_temporal_bake"
        materialization_class = str(source.get("materialization_class") or "")
        ownership_gate = str(source.get("ownership_policy") or "")
        required_next_step = str(source.get("required_next_step") or "complete temporal bake contract review")
    elif n64_name in pose_best_by_name:
        source = pose_best_by_name[n64_name]
        resolution_status = "pose_arbitrated_pending_ownership"
        source_contract_kind = str(source.get("arbitration_class") or "pose_best_ambiguity_arbitration")
        runtime_acceptance_status = "pose_arbitrated_not_runtime_accepted"
        semantic_source_status = "semantic_source_identified"
        materialization_class = str(source.get("temporal_severity") or "")
        ownership_gate = "pose_best_candidate_reuses_existing_csab"
        required_next_step = "resolve CSAB ownership or materialize a distinct baked derivative before acceptance"
    elif n64_name in route_review_by_name:
        source = route_review_by_name[n64_name]
        resolution_status = "blocked_same_frame_route_or_ownership"
        source_contract_kind = str(source.get("route_review_class") or "")
        runtime_acceptance_status = "blocked_not_runtime_accepted"
        semantic_source_status = "blocked_or_unresolved"
        ownership_gate = source_contract_kind
        required_next_step = str(source.get("required_evidence") or "resolve same-frame route or CSAB ownership")
    elif n64_name in resample_review_by_name:
        source = resample_review_by_name[n64_name]
        resolution_status = "blocked_resample_route_or_ownership"
        source_contract_kind = str(source.get("resample_review_class") or "")
        runtime_acceptance_status = "blocked_not_runtime_accepted"
        semantic_source_status = "blocked_or_unresolved"
        materialization_class = str(source.get("temporal_policy") or "proportional_csab_resample_to_n64_frame_count")
        ownership_gate = source_contract_kind
        required_next_step = str(source.get("required_evidence") or "resolve resample route or CSAB ownership")
    elif n64_name in promotion_by_name:
        source = promotion_by_name[n64_name]
        promotion_class = str(source.get("semantic_alias_promotion_class") or "")
        if promotion_class == "blocked_pose_metric_outside_reference_envelope":
            resolution_status = "blocked_pose_metric_outside_reference_envelope"
            required_next_step = "find a different source or relax thresholds only with stronger route/runtime evidence"
        else:
            resolution_status = "blocked_unaccepted_candidate_alias"
            required_next_step = str(source.get("required_evidence") or "complete candidate alias review")
        source_contract_kind = promotion_class
        runtime_acceptance_status = "blocked_not_runtime_accepted"
        semantic_source_status = "blocked_or_unresolved"
        materialization_class = str(source.get("promotion_review_class") or "")
    elif n64_name in near_contract_by_name:
        source = near_contract_by_name[n64_name]
        resolution_status = "blocked_named_near_candidate_ownership"
        source_contract_kind = str(source.get("contract_status") or "")
        runtime_acceptance_status = "blocked_not_runtime_accepted"
        semantic_source_status = "blocked_or_unresolved"
        materialization_class = str(source.get("materialization_class") or "")
        ownership_gate = str(source.get("ownership_policy") or "")
        required_next_step = str(source.get("required_next_step") or "resolve near-candidate ownership")
    elif n64_name in payload_duplicate_alias_by_name:
        source = payload_duplicate_alias_by_name[n64_name]
        if source.get("alias_resolution_class") == "duplicate_of_runtime_promoted_mapping":
            resolution_status = "n64_payload_duplicate_alias_of_runtime_mapping"
            semantic_source_status = "semantic_source_identified"
            runtime_acceptance_status = "payload_duplicate_alias_not_promoted_runtime"
        else:
            resolution_status = "blocked_payload_duplicate_alias_target_unaccepted"
            semantic_source_status = "blocked_or_unresolved"
            runtime_acceptance_status = "blocked_not_runtime_accepted"
        source_contract_kind = str(source.get("alias_resolution_class") or "")
        ownership_gate = "anonymous_numeric_payload_duplicate_alias"
        required_next_step = str(source.get("required_next_step") or "review payload duplicate alias")
    elif n64_name in anonymous_numeric_best_by_name:
        source = semantic_resolution_anonymous_numeric_best_source(anonymous_numeric_best_by_name[n64_name])
        review_class = str(source.get("near_pose_review_class") or "")
        contract_status = unresolved_near_candidate_bake_contract_status(review_class)
        resolution_status = "blocked_anonymous_numeric_near_candidate_ownership"
        source_contract_kind = contract_status
        runtime_acceptance_status = "blocked_not_runtime_accepted"
        semantic_source_status = "blocked_or_unresolved"
        materialization_class = unresolved_near_candidate_bake_materialization_class(source)
        ownership_gate = unresolved_near_candidate_bake_ownership_policy(contract_status)
        required_next_step = str(source.get("required_next_step") or "resolve anonymous numeric near-candidate ownership")
    elif record.get("unresolved_route_analysis") == "anonymous_numeric_symbol":
        source = record
        resolution_status = "unresolved_anonymous_numeric_identity"
        source_contract_kind = str(record.get("unresolved_resolution_class") or "")
        runtime_acceptance_status = "blocked_not_runtime_accepted"
        semantic_source_status = "identity_unknown"
        required_next_step = str(
            record.get("unresolved_resolution_required_evidence")
            or "identify the anonymous N64 symbol by pose, table, or callsite evidence"
        )
    elif record.get("status") == "unmapped_no_candidate_csab":
        source = record
        resolution_status = "unresolved_no_candidate_csab"
        source_contract_kind = str(record.get("unresolved_resolution_class") or "")
        runtime_acceptance_status = "blocked_not_runtime_accepted"
        semantic_source_status = "blocked_or_unresolved"
        required_next_step = str(record.get("unresolved_resolution_required_evidence") or "find a semantic source")

    return {
        "index": record.get("index"),
        "n64_name": n64_name,
        "n64_stem": record.get("n64_stem"),
        "n64_data_name": record.get("n64_data_name"),
        "n64_frame_count": record.get("n64_frame_count"),
        "base_mapping_status": record.get("status"),
        "semantic_resolution_status": resolution_status,
        "semantic_source_status": semantic_source_status,
        "runtime_acceptance_status": runtime_acceptance_status,
        "source_contract_kind": source_contract_kind,
        "source_csab_name": semantic_resolution_source_csab_name(record, source),
        "source_oot3d_stem": (
            source.get("source_oot3d_stem")
            or source.get("oot3d_stem")
            or source.get("duplicate_of_oot3d_stem")
            or record.get("oot3d_stem")
        ),
        "source_frame_slot_count": (
            source.get("source_frame_slot_count")
            or source.get("oot3d_frame_slot_count")
            or source.get("duplicate_of_oot3d_frame_slot_count")
            or record.get("oot3d_frame_slot_count")
        ),
        "frame_delta_oot3d_minus_n64": source.get("frame_delta_oot3d_minus_n64"),
        "duration_ratio_oot3d_per_n64": source.get("duration_ratio_oot3d_per_n64"),
        "materialization_class": materialization_class,
        "ownership_gate": ownership_gate,
        "pose_metric_status": source.get("pose_metric_status"),
        "reference_envelope_status": source.get("reference_envelope_status"),
        "normalized_extent_max_abs_delta_max": source.get("normalized_extent_max_abs_delta_max"),
        "center_delta_normalized_max": source.get("center_delta_normalized_max"),
        "runtime_csab_collision_n64_names": source.get("runtime_csab_collision_n64_names"),
        "candidate_csab_collision_n64_names": source.get("candidate_csab_collision_n64_names"),
        "unresolved_route_analysis": record.get("unresolved_route_analysis"),
        "unresolved_resolution_class": record.get("unresolved_resolution_class"),
        "payload_duplicate_of_n64_name": source.get("duplicate_of_n64_name"),
        "payload_duplicate_sha1": source.get("payload_sha1"),
        "required_next_step": required_next_step,
    }


def semantic_blocked_resolution_frontier_analysis(
    semantic_resolution_contract: dict[str, object],
    sample_limit: int,
) -> dict[str, object]:
    contract_rows = [
        row
        for row in list_dicts(semantic_resolution_contract.get("rows", []))
        if row.get("semantic_source_status") == "blocked_or_unresolved"
    ]
    rows: list[dict[str, object]] = []
    frontier_counts: Counter[str] = Counter()
    gate_counts: Counter[str] = Counter()
    source_contract_counts: Counter[str] = Counter()
    materialization_counts: Counter[str] = Counter()
    candidate_source_present_count = 0
    for row in contract_rows:
        frontier_class = semantic_blocked_resolution_frontier_class(row)
        frontier_gate = semantic_blocked_resolution_frontier_gate(frontier_class)
        candidate_source_present = bool(row.get("source_csab_name"))
        if candidate_source_present:
            candidate_source_present_count += 1
        out = {
            "index": row.get("index"),
            "n64_name": row.get("n64_name"),
            "n64_stem": row.get("n64_stem"),
            "n64_data_name": row.get("n64_data_name"),
            "n64_frame_count": row.get("n64_frame_count"),
            "semantic_resolution_status": row.get("semantic_resolution_status"),
            "source_contract_kind": row.get("source_contract_kind"),
            "source_csab_name": row.get("source_csab_name"),
            "source_oot3d_stem": row.get("source_oot3d_stem"),
            "source_frame_slot_count": row.get("source_frame_slot_count"),
            "materialization_class": row.get("materialization_class"),
            "ownership_gate": row.get("ownership_gate"),
            "reference_envelope_status": row.get("reference_envelope_status"),
            "unresolved_route_analysis": row.get("unresolved_route_analysis"),
            "unresolved_resolution_class": row.get("unresolved_resolution_class"),
            "frontier_class": frontier_class,
            "frontier_gate": frontier_gate,
            "candidate_source_present": candidate_source_present,
            "diagnostic_derivative_policy": semantic_blocked_resolution_derivative_policy(frontier_class),
            "next_evidence_type": semantic_blocked_resolution_next_evidence_type(frontier_class),
            "required_next_step": semantic_blocked_resolution_required_next_step(frontier_class, row),
        }
        rows.append(out)
        frontier_counts[frontier_class] += 1
        gate_counts[frontier_gate] += 1
        source_contract_counts[str(row.get("source_contract_kind") or "none")] += 1
        materialization_counts[str(row.get("materialization_class") or "none")] += 1

    rows.sort(
        key=lambda item: (
            semantic_blocked_resolution_frontier_priority(str(item.get("frontier_class") or "")),
            int(item.get("index") or 0),
        )
    )
    return {
        "format": "oot3d_link_child_animation_semantic_blocked_resolution_frontier_v1",
        "status": "frontier_ready" if rows else "complete_no_blocked_resolution_rows",
        "policy": {
            "scope": "actionable accounting for semantic-resolution rows that remain blocked or unresolved",
            "promotion_policy": (
                "frontier rows are not promoted mappings; they identify the exact evidence gate needed before "
                "a blocked source can become a bake, overlay, or runtime route"
            ),
        },
        "row_count": len(rows),
        "candidate_source_present_count": candidate_source_present_count,
        "candidate_source_missing_count": len(rows) - candidate_source_present_count,
        "frontier_class_counts": sorted_counter(frontier_counts),
        "frontier_gate_counts": sorted_counter(gate_counts),
        "source_contract_kind_counts": sorted_counter(source_contract_counts),
        "materialization_class_counts": sorted_counter(materialization_counts),
        "sample_rows": rows[:sample_limit],
        "rows": rows,
    }


def semantic_blocked_resolution_frontier_class(row: dict[str, object]) -> str:
    status = str(row.get("semantic_resolution_status") or "")
    contract_kind = str(row.get("source_contract_kind") or "")
    ownership_gate = str(row.get("ownership_gate") or "")
    unresolved = str(row.get("unresolved_route_analysis") or "")
    if status == "blocked_pose_metric_outside_reference_envelope":
        return "alternate_source_or_stronger_route_required"
    if unresolved == "anonymous_numeric_symbol":
        return "anonymous_numeric_identity_plus_ownership"
    if contract_kind in {
        "requires_route_or_capture_evidence",
        "resample_ready_without_direct_player_actor_route_reference",
    }:
        return "route_or_capture_required"
    if contract_kind in {
        "blocked_promoted_csab_reuse",
        "blocked_resample_reuses_promoted_csab",
        "blocked_same_frame_reuses_promoted_csab",
    } or ownership_gate == "source_csab_is_already_owned_by_promoted_runtime_mapping":
        return "promoted_source_reuse_ownership_required"
    if contract_kind in {
        "blocked_candidate_csab_reuse",
        "blocked_same_frame_reuses_candidate_csab",
    } or ownership_gate in {
        "source_csab_is_claimed_by_other_unpromoted_candidate_rows",
        "source_csab_is_claimed_by_multiple_unpromoted_candidates",
    }:
        return "candidate_source_reuse_ownership_required"
    return "manual_frontier_review"


def semantic_blocked_resolution_frontier_gate(frontier_class: str) -> str:
    if frontier_class == "alternate_source_or_stronger_route_required":
        return "pose_metric_or_source_identity"
    if frontier_class == "anonymous_numeric_identity_plus_ownership":
        return "anonymous_symbol_identity"
    if frontier_class == "route_or_capture_required":
        return "route_or_runtime_capture"
    if frontier_class in {
        "promoted_source_reuse_ownership_required",
        "candidate_source_reuse_ownership_required",
    }:
        return "csab_source_ownership"
    return "manual_review"


def semantic_blocked_resolution_derivative_policy(frontier_class: str) -> str:
    if frontier_class == "alternate_source_or_stronger_route_required":
        return "do_not_materialize_current_source_until_pose_or_route_evidence_improves"
    if frontier_class == "route_or_capture_required":
        return "source_candidate_can_define_derivative_only_after_route_or_capture_evidence"
    if frontier_class == "anonymous_numeric_identity_plus_ownership":
        return "source_candidate_can_define_derivative_only_after_anonymous_identity_and_ownership_are_proven"
    if frontier_class in {
        "promoted_source_reuse_ownership_required",
        "candidate_source_reuse_ownership_required",
    }:
        return "distinct_derivative_possible_only_after_explicit_csab_ownership_or_intentional_reuse_proof"
    return "manual_derivative_policy_required"


def semantic_blocked_resolution_next_evidence_type(frontier_class: str) -> str:
    return {
        "alternate_source_or_stronger_route_required": "alternate CSAB source, runtime route capture, or threshold-quality proof",
        "anonymous_numeric_identity_plus_ownership": "anonymous N64 symbol identity plus CSAB ownership decision",
        "route_or_capture_required": "player action-state route, source callsite, or runtime capture",
        "promoted_source_reuse_ownership_required": "proof that promoted CSAB reuse should fork into a distinct derivative",
        "candidate_source_reuse_ownership_required": "owner selection among competing candidate rows or distinct derivative policy",
    }.get(frontier_class, "manual frontier evidence")


def semantic_blocked_resolution_required_next_step(frontier_class: str, row: dict[str, object]) -> str:
    existing = str(row.get("required_next_step") or "")
    if existing:
        return existing
    return semantic_blocked_resolution_next_evidence_type(frontier_class)


def semantic_blocked_resolution_frontier_priority(frontier_class: str) -> int:
    return {
        "route_or_capture_required": 0,
        "candidate_source_reuse_ownership_required": 1,
        "promoted_source_reuse_ownership_required": 2,
        "anonymous_numeric_identity_plus_ownership": 3,
        "alternate_source_or_stronger_route_required": 4,
        "manual_frontier_review": 5,
    }.get(frontier_class, 9)


def records_by_n64_name(value: object) -> dict[str, dict[str, object]]:
    return {
        str(record.get("n64_name") or ""): record
        for record in list_dicts(value)
        if record.get("n64_name")
    }


def list_dicts(value: object) -> list[dict[str, object]]:
    return [record for record in value if isinstance(record, dict)] if isinstance(value, list) else []


def require_counter_dict(value: object) -> Counter[str]:
    if not isinstance(value, dict):
        return Counter()
    return Counter({str(key): int(count) for key, count in value.items() if isinstance(count, (int, float))})


def truthy(value: object) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        return value.lower() in {"1", "true", "yes"}
    return bool(value)


def semantic_resolution_source_csab_name(record: dict[str, object], source: dict[str, object]) -> object:
    return (
        source.get("source_csab_name")
        or source.get("csab_name")
        or source.get("best_csab_name")
        or source.get("duplicate_of_csab_name")
        or record.get("csab_name")
    )


def semantic_ownership_derivative_plan_analysis(
    semantic_blocked_resolution_frontier: dict[str, object],
    character_profile_o2r_path: Path | None,
    sample_limit: int,
) -> dict[str, object]:
    frontier_rows = [
        row
        for row in list_dicts(semantic_blocked_resolution_frontier.get("rows", []))
        if row.get("frontier_gate") == "csab_source_ownership"
    ]
    synthetic_rows: list[dict[str, object]] = []
    frontier_by_name: dict[str, dict[str, object]] = {}
    for row in frontier_rows:
        n64_name = str(row.get("n64_name") or "")
        source_frame_count = int_or_none_local(row.get("source_frame_slot_count")) or 0
        target_frame_count = int_or_none_local(row.get("n64_frame_count")) or 0
        synthetic = {
            **row,
            "semantic_resolution_status": "bake_contract_ready_pending_materialization",
            "source_contract_kind": f"diagnostic_{row.get('frontier_class')}",
            "materialization_class": semantic_derivative_frame_mapping_class(
                source_frame_count,
                target_frame_count,
            ),
            "required_next_step": (
                "diagnostic derivative only: resolve CSAB ownership and run runtime/skinned parity "
                "before semantic acceptance"
            ),
        }
        synthetic_rows.append(synthetic)
        frontier_by_name[n64_name] = row

    plan = semantic_materialization_plan_analysis(
        {"rows": synthetic_rows},
        character_profile_o2r_path,
        sample_limit,
    )
    plan.update(
        {
            "format": LINK_CHILD_ANIMATION_SEMANTIC_OWNERSHIP_DERIVATIVE_PLAN_FORMAT,
            "plan_kind": "blocked_csab_ownership_derivative_feasibility",
            "policy": {
                "scope": (
                    "diagnostic materialization plan for blocked Link child animation rows whose next gate "
                    "is CSAB source ownership"
                ),
                "semantic_effect": (
                    "proves whether each blocked ownership row can be forked into a unique derived track; "
                    "does not resolve ownership or promote runtime mappings"
                ),
                "runtime_acceptance_policy": (
                    "diagnostic derivatives remain outside runtime acceptance until ownership, package, and "
                    "runtime/skinned pose parity are proven"
                ),
            },
            "ownership_frontier_row_count": len(frontier_rows),
            "diagnostic_derivative_candidate_count": len(synthetic_rows),
        }
    )
    rows = list_dicts(plan.get("rows", []))
    for plan_row in rows:
        source = frontier_by_name.get(str(plan_row.get("n64_name") or ""), {})
        plan_row.update(
            {
                "frontier_class": source.get("frontier_class"),
                "frontier_gate": source.get("frontier_gate"),
                "ownership_gate": source.get("ownership_gate"),
                "diagnostic_derivative_policy": source.get("diagnostic_derivative_policy"),
                "runtime_acceptance_status": "diagnostic_ownership_derivative_not_runtime_accepted",
                "required_next_step": (
                    "resolve CSAB ownership, then package and runtime/skinned-pose verify before acceptance"
                ),
            }
        )
    plan["rows"] = rows
    plan["sample_rows"] = [semantic_materialization_plan_sample_row(row) for row in rows[:sample_limit]]
    return plan


def build_link_child_anonymous_numeric_diagnostic_tracks(
    semantic_blocked_resolution_frontier_csv_path: Path,
    character_profile_o2r_path: Path,
    output_dir: Path,
    *,
    plan_output_path: Path | None = None,
    plan_csv_output_path: Path | None = None,
    track_manifest_output_path: Path | None = None,
    track_csv_output_path: Path | None = None,
    summary_output_path: Path | None = None,
    sample_limit: int = 50,
) -> dict[str, object]:
    if not semantic_blocked_resolution_frontier_csv_path.is_file():
        raise ParseError(
            f"{semantic_blocked_resolution_frontier_csv_path}: semantic blocked frontier CSV not found"
        )
    if not character_profile_o2r_path.is_file():
        raise ParseError(f"{character_profile_o2r_path}: Link child character profile O2R not found")

    with semantic_blocked_resolution_frontier_csv_path.open("r", encoding="utf-8", newline="") as handle:
        frontier_rows = [dict(row) for row in csv.DictReader(handle)]

    diagnostic_rows: list[dict[str, object]] = []
    for row in frontier_rows:
        if row.get("frontier_gate") != "anonymous_symbol_identity":
            continue
        source_frame_count = int_or_none_local(row.get("source_frame_slot_count")) or 0
        target_frame_count = int_or_none_local(row.get("n64_frame_count")) or 0
        diagnostic_rows.append(
            {
                **row,
                "semantic_resolution_status": "bake_contract_ready_pending_materialization",
                "diagnostic_resolution_status": "diagnostic_anonymous_numeric_near_candidate",
                "source_contract_kind": f"diagnostic_{row.get('source_contract_kind') or 'anonymous_numeric'}",
                "materialization_class": semantic_derivative_frame_mapping_class(
                    source_frame_count,
                    target_frame_count,
                ),
                "required_next_step": (
                    "diagnostic anonymous numeric derivative only: resolve symbol identity and CSAB "
                    "ownership before semantic acceptance"
                ),
            }
        )

    plan = semantic_materialization_plan_analysis(
        {"rows": diagnostic_rows},
        character_profile_o2r_path,
        sample_limit,
    )
    plan.update(
        {
            "format": LINK_CHILD_ANIMATION_ANONYMOUS_NUMERIC_DIAGNOSTIC_PLAN_FORMAT,
            "plan_kind": "anonymous_numeric_identity_derivative_feasibility",
            "policy": {
                "scope": (
                    "diagnostic materialization plan for Link child anonymous numeric PlayerAnimation "
                    "symbols with pose-best near-candidate CSAB sources"
                ),
                "semantic_effect": (
                    "proves whether each anonymous numeric row can be forked into a unique derived "
                    "track; does not name the symbol, resolve ownership, or promote runtime mappings"
                ),
                "runtime_acceptance_policy": (
                    "diagnostic derivatives remain outside runtime acceptance until identity, "
                    "ownership, package, and runtime/skinned pose parity are proven"
                ),
            },
            "anonymous_numeric_frontier_row_count": len(diagnostic_rows),
        }
    )
    for plan_row in list_dicts(plan.get("rows", [])):
        plan_row.update(
            {
                "runtime_acceptance_status": "diagnostic_anonymous_numeric_derivative_not_runtime_accepted",
                "required_next_step": (
                    "resolve anonymous numeric identity and CSAB ownership, then package and "
                    "runtime/skinned-pose verify before acceptance"
                ),
            }
        )
    plan["sample_rows"] = [
        semantic_materialization_plan_sample_row(row)
        for row in list_dicts(plan.get("rows", []))[:sample_limit]
    ]

    track_manifest = semantic_materialized_track_manifest_analysis(
        plan,
        character_profile_o2r_path,
        output_dir,
        sample_limit,
    )

    summary = {
        "format": LINK_CHILD_ANIMATION_ANONYMOUS_NUMERIC_DIAGNOSTIC_SUMMARY_FORMAT,
        "status": (
            "anonymous_numeric_diagnostic_tracks_ready"
            if plan.get("status") == "materialization_plan_ready"
            and track_manifest.get("status") == "materialized_tracks_ready"
            and int(track_manifest.get("materialized_track_count") or 0) == len(diagnostic_rows)
            and int(track_manifest.get("issue_count") or 0) == 0
            else "anonymous_numeric_diagnostic_tracks_incomplete"
        ),
        "policy": {
            "semantic_effect": (
                "diagnostic track generation only; anonymous numeric identity and CSAB ownership "
                "remain blocked until separately proven"
            )
        },
        "semantic_blocked_resolution_frontier_csv": str(semantic_blocked_resolution_frontier_csv_path),
        "character_profile_o2r": str(character_profile_o2r_path),
        "output_dir": str(output_dir),
        "plan_status": plan.get("status"),
        "track_manifest_status": track_manifest.get("status"),
        "anonymous_numeric_frontier_row_count": len(diagnostic_rows),
        "plan_row_count": plan.get("plan_row_count", 0),
        "materialized_track_count": track_manifest.get("materialized_track_count", 0),
        "written_file_count": track_manifest.get("written_file_count", 0),
        "issue_count": int(plan.get("issue_count") or 0) + int(track_manifest.get("issue_count") or 0),
        "counts": {
            "track_count": track_manifest.get("total_output_track_count", 0),
            "channel_count": track_manifest.get("total_output_channel_count", 0),
            "keyframe_count": track_manifest.get("total_output_keyframe_count", 0),
            "source_sample_parity_passed_count": track_manifest.get("source_sample_parity_passed_count", 0),
            "source_sample_parity_issue_count": track_manifest.get("source_sample_parity_issue_count", 0),
            "output_frame_dense_validation_passed_count": (
                track_manifest.get("output_frame_dense_validation_passed_count", 0)
            ),
            "output_frame_dense_validation_issue_count": (
                track_manifest.get("output_frame_dense_validation_issue_count", 0)
            ),
        },
        "materialization_class_counts": track_manifest.get("materialization_class_counts", {}),
        "source_sample_parity_status_counts": track_manifest.get("source_sample_parity_status_counts", {}),
        "output_frame_dense_validation_status_counts": (
            track_manifest.get("output_frame_dense_validation_status_counts", {})
        ),
        "runtime_acceptance_status": (
            "pending_package_and_runtime_skinned_pose_parity"
            if track_manifest.get("status") == "materialized_tracks_ready"
            else "not_ready_for_runtime_parity"
        ),
        "next_gate": (
            "Package the anonymous numeric diagnostic tracks and run offline runtime/skinned pose parity; "
            "identity and ownership remain separate semantic gates."
        ),
        "sample_rows": track_manifest.get("sample_rows", []),
    }

    if plan_output_path is not None:
        plan_output_path.parent.mkdir(parents=True, exist_ok=True)
        plan_output_path.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
    if plan_csv_output_path is not None:
        write_semantic_materialization_plan_csv(plan_csv_output_path, plan.get("rows", []))
    if track_manifest_output_path is not None:
        track_manifest_output_path.parent.mkdir(parents=True, exist_ok=True)
        track_manifest_output_path.write_text(json.dumps(track_manifest, indent=2) + "\n", encoding="utf-8")
    if track_csv_output_path is not None:
        write_semantic_materialized_track_manifest_csv(track_csv_output_path, track_manifest.get("rows", []))
    if summary_output_path is not None:
        summary_output_path.parent.mkdir(parents=True, exist_ok=True)
        summary_output_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

    return summary


def build_link_child_pose_source_risk_diagnostic_tracks(
    semantic_blocked_resolution_frontier_csv_path: Path,
    character_profile_o2r_path: Path,
    output_dir: Path,
    *,
    plan_output_path: Path | None = None,
    plan_csv_output_path: Path | None = None,
    track_manifest_output_path: Path | None = None,
    track_csv_output_path: Path | None = None,
    summary_output_path: Path | None = None,
    sample_limit: int = 50,
) -> dict[str, object]:
    if not semantic_blocked_resolution_frontier_csv_path.is_file():
        raise ParseError(
            f"{semantic_blocked_resolution_frontier_csv_path}: semantic blocked frontier CSV not found"
        )
    if not character_profile_o2r_path.is_file():
        raise ParseError(f"{character_profile_o2r_path}: Link child character profile O2R not found")

    with semantic_blocked_resolution_frontier_csv_path.open("r", encoding="utf-8", newline="") as handle:
        frontier_rows = [dict(row) for row in csv.DictReader(handle)]

    diagnostic_rows: list[dict[str, object]] = []
    for row in frontier_rows:
        if row.get("frontier_gate") != "pose_metric_or_source_identity":
            continue
        source_frame_count = int_or_none_local(row.get("source_frame_slot_count")) or 0
        target_frame_count = int_or_none_local(row.get("n64_frame_count")) or 0
        diagnostic_rows.append(
            {
                **row,
                "semantic_resolution_status": "bake_contract_ready_pending_materialization",
                "diagnostic_resolution_status": "diagnostic_pose_metric_or_source_identity",
                "source_contract_kind": f"diagnostic_{row.get('source_contract_kind') or 'pose_source_risk'}",
                "materialization_class": semantic_derivative_frame_mapping_class(
                    source_frame_count,
                    target_frame_count,
                ),
                "required_next_step": (
                    "diagnostic pose/source-risk derivative only: find alternate source, stronger route, "
                    "or runtime/skinned proof before semantic acceptance"
                ),
            }
        )

    plan = semantic_materialization_plan_analysis(
        {"rows": diagnostic_rows},
        character_profile_o2r_path,
        sample_limit,
    )
    plan.update(
        {
            "format": LINK_CHILD_ANIMATION_POSE_SOURCE_RISK_DIAGNOSTIC_PLAN_FORMAT,
            "plan_kind": "pose_source_risk_derivative_feasibility",
            "policy": {
                "scope": (
                    "diagnostic materialization plan for Link child PlayerAnimation rows whose best "
                    "candidate source is outside the current pose reference envelope"
                ),
                "semantic_effect": (
                    "proves whether each outside-envelope row can be forked into a unique derived "
                    "track; does not accept the source identity or promote runtime mappings"
                ),
                "runtime_acceptance_policy": (
                    "diagnostic derivatives remain outside runtime acceptance until alternate-source, "
                    "route, or installed runtime draw evidence is accepted"
                ),
            },
            "pose_source_risk_frontier_row_count": len(diagnostic_rows),
        }
    )
    for plan_row in list_dicts(plan.get("rows", [])):
        plan_row.update(
            {
                "runtime_acceptance_status": "diagnostic_pose_source_risk_derivative_not_runtime_accepted",
                "required_next_step": (
                    "find alternate source, stronger route evidence, or installed runtime draw proof "
                    "before acceptance"
                ),
            }
        )
    plan["sample_rows"] = [
        semantic_materialization_plan_sample_row(row)
        for row in list_dicts(plan.get("rows", []))[:sample_limit]
    ]

    track_manifest = semantic_materialized_track_manifest_analysis(
        plan,
        character_profile_o2r_path,
        output_dir,
        sample_limit,
    )

    summary = {
        "format": LINK_CHILD_ANIMATION_POSE_SOURCE_RISK_DIAGNOSTIC_SUMMARY_FORMAT,
        "status": (
            "pose_source_risk_diagnostic_tracks_ready"
            if plan.get("status") == "materialization_plan_ready"
            and track_manifest.get("status") == "materialized_tracks_ready"
            and int(track_manifest.get("materialized_track_count") or 0) == len(diagnostic_rows)
            and int(track_manifest.get("issue_count") or 0) == 0
            else "pose_source_risk_diagnostic_tracks_incomplete"
        ),
        "policy": {
            "semantic_effect": (
                "diagnostic track generation only; outside-envelope source identity remains blocked "
                "until separately proven"
            )
        },
        "semantic_blocked_resolution_frontier_csv": str(semantic_blocked_resolution_frontier_csv_path),
        "character_profile_o2r": str(character_profile_o2r_path),
        "output_dir": str(output_dir),
        "plan_status": plan.get("status"),
        "track_manifest_status": track_manifest.get("status"),
        "pose_source_risk_frontier_row_count": len(diagnostic_rows),
        "plan_row_count": plan.get("plan_row_count", 0),
        "materialized_track_count": track_manifest.get("materialized_track_count", 0),
        "written_file_count": track_manifest.get("written_file_count", 0),
        "issue_count": int(plan.get("issue_count") or 0) + int(track_manifest.get("issue_count") or 0),
        "counts": {
            "track_count": track_manifest.get("total_output_track_count", 0),
            "channel_count": track_manifest.get("total_output_channel_count", 0),
            "keyframe_count": track_manifest.get("total_output_keyframe_count", 0),
            "source_sample_parity_passed_count": track_manifest.get("source_sample_parity_passed_count", 0),
            "source_sample_parity_issue_count": track_manifest.get("source_sample_parity_issue_count", 0),
            "output_frame_dense_validation_passed_count": (
                track_manifest.get("output_frame_dense_validation_passed_count", 0)
            ),
            "output_frame_dense_validation_issue_count": (
                track_manifest.get("output_frame_dense_validation_issue_count", 0)
            ),
        },
        "materialization_class_counts": track_manifest.get("materialization_class_counts", {}),
        "source_sample_parity_status_counts": track_manifest.get("source_sample_parity_status_counts", {}),
        "output_frame_dense_validation_status_counts": (
            track_manifest.get("output_frame_dense_validation_status_counts", {})
        ),
        "runtime_acceptance_status": (
            "pending_package_and_runtime_skinned_pose_parity"
            if track_manifest.get("status") == "materialized_tracks_ready"
            else "not_ready_for_runtime_parity"
        ),
        "next_gate": (
            "Package the pose/source-risk diagnostic tracks and run offline runtime/skinned pose parity; "
            "alternate-source or installed-runtime evidence remains the semantic gate."
        ),
        "sample_rows": track_manifest.get("sample_rows", []),
    }

    if plan_output_path is not None:
        plan_output_path.parent.mkdir(parents=True, exist_ok=True)
        plan_output_path.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
    if plan_csv_output_path is not None:
        write_semantic_materialization_plan_csv(plan_csv_output_path, plan.get("rows", []))
    if track_manifest_output_path is not None:
        track_manifest_output_path.parent.mkdir(parents=True, exist_ok=True)
        track_manifest_output_path.write_text(json.dumps(track_manifest, indent=2) + "\n", encoding="utf-8")
    if track_csv_output_path is not None:
        write_semantic_materialized_track_manifest_csv(track_csv_output_path, track_manifest.get("rows", []))
    if summary_output_path is not None:
        summary_output_path.parent.mkdir(parents=True, exist_ok=True)
        summary_output_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

    return summary


def build_link_child_route_runtime_diagnostic_tracks(
    semantic_blocked_resolution_frontier_csv_path: Path,
    character_profile_o2r_path: Path,
    output_dir: Path,
    *,
    plan_output_path: Path | None = None,
    plan_csv_output_path: Path | None = None,
    track_manifest_output_path: Path | None = None,
    track_csv_output_path: Path | None = None,
    summary_output_path: Path | None = None,
    sample_limit: int = 50,
) -> dict[str, object]:
    if not semantic_blocked_resolution_frontier_csv_path.is_file():
        raise ParseError(
            f"{semantic_blocked_resolution_frontier_csv_path}: semantic blocked frontier CSV not found"
        )
    if not character_profile_o2r_path.is_file():
        raise ParseError(f"{character_profile_o2r_path}: Link child character profile O2R not found")

    with semantic_blocked_resolution_frontier_csv_path.open("r", encoding="utf-8", newline="") as handle:
        frontier_rows = [dict(row) for row in csv.DictReader(handle)]

    diagnostic_rows: list[dict[str, object]] = []
    for row in frontier_rows:
        if row.get("frontier_gate") != "route_or_runtime_capture":
            continue
        source_frame_count = int_or_none_local(row.get("source_frame_slot_count")) or 0
        target_frame_count = int_or_none_local(row.get("n64_frame_count")) or 0
        diagnostic_rows.append(
            {
                **row,
                "semantic_resolution_status": "bake_contract_ready_pending_materialization",
                "diagnostic_resolution_status": "diagnostic_route_or_runtime_capture_candidate",
                "source_contract_kind": f"diagnostic_{row.get('source_contract_kind') or 'route_runtime'}",
                "materialization_class": semantic_derivative_frame_mapping_class(
                    source_frame_count,
                    target_frame_count,
                ),
                "required_next_step": (
                    "diagnostic route/runtime derivative only: prove exact route, accepted sibling route, "
                    "or installed runtime draw evidence before semantic acceptance"
                ),
            }
        )

    plan = semantic_materialization_plan_analysis(
        {"rows": diagnostic_rows},
        character_profile_o2r_path,
        sample_limit,
    )
    plan.update(
        {
            "format": LINK_CHILD_ANIMATION_ROUTE_RUNTIME_DIAGNOSTIC_PLAN_FORMAT,
            "plan_kind": "route_runtime_derivative_feasibility",
            "policy": {
                "scope": (
                    "diagnostic materialization plan for Link child PlayerAnimation rows whose best "
                    "candidate source requires exact route or installed runtime context evidence"
                ),
                "semantic_effect": (
                    "proves whether each route/runtime row can be forked into a unique derived track; "
                    "does not accept route evidence or promote runtime mappings"
                ),
                "runtime_acceptance_policy": (
                    "diagnostic derivatives remain outside runtime acceptance until exact route, "
                    "accepted sibling route, or installed runtime draw evidence is accepted"
                ),
            },
            "route_runtime_frontier_row_count": len(diagnostic_rows),
        }
    )
    for plan_row in list_dicts(plan.get("rows", [])):
        plan_row.update(
            {
                "runtime_acceptance_status": "diagnostic_route_runtime_derivative_not_runtime_accepted",
                "required_next_step": (
                    "prove exact route, accepted sibling route, or installed runtime draw evidence "
                    "before acceptance"
                ),
            }
        )
    plan["sample_rows"] = [
        semantic_materialization_plan_sample_row(row)
        for row in list_dicts(plan.get("rows", []))[:sample_limit]
    ]

    track_manifest = semantic_materialized_track_manifest_analysis(
        plan,
        character_profile_o2r_path,
        output_dir,
        sample_limit,
    )

    summary = {
        "format": LINK_CHILD_ANIMATION_ROUTE_RUNTIME_DIAGNOSTIC_SUMMARY_FORMAT,
        "status": (
            "route_runtime_diagnostic_tracks_ready"
            if plan.get("status") == "materialization_plan_ready"
            and track_manifest.get("status") == "materialized_tracks_ready"
            and int(track_manifest.get("materialized_track_count") or 0) == len(diagnostic_rows)
            and int(track_manifest.get("issue_count") or 0) == 0
            else "route_runtime_diagnostic_tracks_incomplete"
        ),
        "policy": {
            "semantic_effect": (
                "diagnostic track generation only; route/runtime rows remain blocked until exact route "
                "or installed runtime evidence is separately proven"
            )
        },
        "semantic_blocked_resolution_frontier_csv": str(semantic_blocked_resolution_frontier_csv_path),
        "character_profile_o2r": str(character_profile_o2r_path),
        "output_dir": str(output_dir),
        "plan_status": plan.get("status"),
        "track_manifest_status": track_manifest.get("status"),
        "route_runtime_frontier_row_count": len(diagnostic_rows),
        "plan_row_count": plan.get("plan_row_count", 0),
        "materialized_track_count": track_manifest.get("materialized_track_count", 0),
        "written_file_count": track_manifest.get("written_file_count", 0),
        "issue_count": int(plan.get("issue_count") or 0) + int(track_manifest.get("issue_count") or 0),
        "counts": {
            "track_count": track_manifest.get("total_output_track_count", 0),
            "channel_count": track_manifest.get("total_output_channel_count", 0),
            "keyframe_count": track_manifest.get("total_output_keyframe_count", 0),
            "source_sample_parity_passed_count": track_manifest.get("source_sample_parity_passed_count", 0),
            "source_sample_parity_issue_count": track_manifest.get("source_sample_parity_issue_count", 0),
            "output_frame_dense_validation_passed_count": (
                track_manifest.get("output_frame_dense_validation_passed_count", 0)
            ),
            "output_frame_dense_validation_issue_count": (
                track_manifest.get("output_frame_dense_validation_issue_count", 0)
            ),
        },
        "materialization_class_counts": track_manifest.get("materialization_class_counts", {}),
        "source_sample_parity_status_counts": track_manifest.get("source_sample_parity_status_counts", {}),
        "output_frame_dense_validation_status_counts": (
            track_manifest.get("output_frame_dense_validation_status_counts", {})
        ),
        "runtime_acceptance_status": (
            "pending_package_and_runtime_skinned_pose_parity"
            if track_manifest.get("status") == "materialized_tracks_ready"
            else "not_ready_for_runtime_parity"
        ),
        "next_gate": (
            "Package the route/runtime diagnostic tracks and run offline runtime/skinned pose parity; "
            "exact route or installed-runtime evidence remains the semantic gate."
        ),
        "sample_rows": track_manifest.get("sample_rows", []),
    }

    if plan_output_path is not None:
        plan_output_path.parent.mkdir(parents=True, exist_ok=True)
        plan_output_path.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
    if plan_csv_output_path is not None:
        write_semantic_materialization_plan_csv(plan_csv_output_path, plan.get("rows", []))
    if track_manifest_output_path is not None:
        track_manifest_output_path.parent.mkdir(parents=True, exist_ok=True)
        track_manifest_output_path.write_text(json.dumps(track_manifest, indent=2) + "\n", encoding="utf-8")
    if track_csv_output_path is not None:
        write_semantic_materialized_track_manifest_csv(track_csv_output_path, track_manifest.get("rows", []))
    if summary_output_path is not None:
        summary_output_path.parent.mkdir(parents=True, exist_ok=True)
        summary_output_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

    return summary


def audit_link_child_pose_source_callsite_context(
    semantic_resolution_contract_csv_path: Path,
    output_path: Path | None = None,
    *,
    csv_output_path: Path | None = None,
    source_search_roots: list[Path] | None = None,
    sample_limit: int = 50,
) -> dict[str, object]:
    if not semantic_resolution_contract_csv_path.is_file():
        raise ParseError(
            f"{semantic_resolution_contract_csv_path}: semantic resolution contract CSV not found"
        )

    with semantic_resolution_contract_csv_path.open("r", encoding="utf-8", newline="") as handle:
        contract_rows = [dict(row) for row in csv.DictReader(handle)]

    frontier_rows = [
        row
        for row in contract_rows
        if row.get("semantic_resolution_status") == "blocked_pose_metric_outside_reference_envelope"
    ]
    roots = source_search_roots or []
    rows: list[dict[str, object]] = []
    callsite_counts: Counter[str] = Counter()
    next_evidence_counts: Counter[str] = Counter()
    direct_source_reference_count = 0
    direct_player_actor_source_reference_count = 0
    for row in frontier_rows:
        n64_name = str(row.get("n64_name") or "")
        callsite_evidence = pose_source_callsite_contexts_for_symbol(
            n64_name,
            roots,
            sample_limit=max(sample_limit, 16),
        )
        contexts = list_dicts(callsite_evidence.get("contexts", []))
        callsite_class = pose_source_callsite_review_class(n64_name, contexts)
        next_evidence = pose_source_callsite_next_evidence(callsite_class)
        direct_count = int(callsite_evidence.get("direct_source_reference_count") or 0)
        player_count = int(callsite_evidence.get("direct_player_actor_source_reference_count") or 0)
        direct_source_reference_count += direct_count
        direct_player_actor_source_reference_count += player_count
        callsite_counts[callsite_class] += 1
        next_evidence_counts[next_evidence] += 1
        row_record = {
            "index": row.get("index"),
            "n64_name": n64_name,
            "n64_stem": row.get("n64_stem"),
            "n64_data_name": row.get("n64_data_name"),
            "source_csab_name": row.get("source_csab_name"),
            "source_oot3d_stem": row.get("source_oot3d_stem"),
            "n64_frame_count": row.get("n64_frame_count"),
            "source_frame_slot_count": row.get("source_frame_slot_count"),
            "frame_delta_oot3d_minus_n64": row.get("frame_delta_oot3d_minus_n64"),
            "duration_ratio_oot3d_per_n64": row.get("duration_ratio_oot3d_per_n64"),
            "reference_envelope_status": row.get("reference_envelope_status"),
            "normalized_extent_max_abs_delta_max": row.get("normalized_extent_max_abs_delta_max"),
            "center_delta_normalized_max": row.get("center_delta_normalized_max"),
            "semantic_resolution_status": row.get("semantic_resolution_status"),
            "callsite_context_status": (
                "direct_player_actor_callsite_found"
                if player_count > 0
                else (
                    "direct_non_player_callsite_found"
                    if direct_count > 0
                    else "no_direct_callsite_found"
                )
            ),
            "callsite_review_class": callsite_class,
            "direct_source_reference_count": direct_count,
            "direct_player_actor_source_reference_count": player_count,
            "sample_callsite_contexts": pose_source_callsite_context_summary(contexts),
            "semantic_effect": (
                "callsite context only; does not promote or accept the outside-envelope source"
            ),
            "next_evidence": next_evidence,
            "required_next_step": pose_source_callsite_required_next_step(callsite_class),
        }
        rows.append(row_record)

    rows.sort(key=lambda item: int_or_none_local(item.get("index")) or 0)
    audit = {
        "format": LINK_CHILD_ANIMATION_POSE_SOURCE_CALLSITE_CONTEXT_FORMAT,
        "status": (
            "pose_source_callsite_context_ready"
            if rows
            else "complete_no_pose_source_frontier_rows"
        ),
        "policy": {
            "scope": (
                "N64 source-side callsite context for Link child rows blocked by outside-envelope pose metrics"
            ),
            "semantic_effect": (
                "classifies how the N64 PlayerAnimation symbol is used so the next proof can target "
                "the correct runtime state, actor cue, or table context; it does not relax thresholds "
                "or promote mappings"
            ),
            "acceptance_policy": (
                "a row remains blocked until alternate-source evidence, stronger route evidence with "
                "installed runtime draw parity, or equivalent source-side proof is accepted"
            ),
        },
        "semantic_resolution_contract_csv": str(semantic_resolution_contract_csv_path),
        "source_search_roots": [str(path) for path in roots],
        "row_count": len(rows),
        "direct_source_reference_count": direct_source_reference_count,
        "direct_player_actor_source_reference_count": direct_player_actor_source_reference_count,
        "callsite_review_class_counts": sorted_counter(callsite_counts),
        "next_evidence_counts": sorted_counter(next_evidence_counts),
        "semantic_resolution_change_count": 0,
        "sample_rows": rows[:sample_limit],
        "rows": rows,
    }

    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8")
    if csv_output_path is not None:
        write_pose_source_callsite_context_csv(csv_output_path, rows)
    return audit


def pose_source_callsite_contexts_for_symbol(
    symbol: str,
    source_search_roots: list[Path],
    *,
    sample_limit: int,
) -> dict[str, object]:
    evidence = source_reference_empty(symbol)
    if not symbol or not source_search_roots:
        return evidence
    pattern = re.compile(r"\b" + re.escape(symbol) + r"\b")
    contexts: list[dict[str, object]] = []
    for root in source_search_roots:
        if not root.exists():
            continue
        files = [root] if root.is_file() else root.rglob("*")
        for path in files:
            if not path.is_file() or path.suffix.lower() not in {".c", ".cpp", ".h", ".hpp"}:
                continue
            normalized = path.as_posix().lower()
            if normalized.endswith(".inc.c") or "player_anim_headers." in normalized:
                continue
            try:
                lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
            except OSError:
                continue
            for line_number, line in enumerate(lines, start=1):
                if not pattern.search(line):
                    continue
                evidence["direct_source_reference_count"] = (
                    int(evidence["direct_source_reference_count"]) + 1
                )
                if source_reference_is_player_actor_path(normalized):
                    evidence["direct_player_actor_source_reference_count"] = (
                        int(evidence["direct_player_actor_source_reference_count"]) + 1
                    )
                samples = evidence["sample_direct_source_references"]
                if isinstance(samples, list) and len(samples) < 10:
                    samples.append({"path": str(path), "line": line_number})
                if len(contexts) < sample_limit:
                    context = route_proven_callsite_context({"path": str(path), "line": line_number})
                    if context:
                        contexts.append(context)
    evidence["status"] = (
        "direct_source_references_found"
        if int(evidence["direct_source_reference_count"]) > 0
        else "no_direct_source_reference_in_search_roots"
    )
    evidence["contexts"] = contexts
    return evidence


def pose_source_callsite_review_class(n64_name: str, contexts: list[dict[str, object]]) -> str:
    context_names = {str(context.get("context_name") or "") for context in contexts}
    source_lines = " ".join(str(context.get("source_line") or "") for context in contexts)
    comment_parts: list[str] = []
    for context in contexts:
        comment_parts.append(str(context.get("line_comment") or ""))
        comment_parts.append(str(context.get("nearby_block_comment") or ""))
    comments = " ".join(comment_parts)
    if not contexts:
        return "no_direct_source_callsite"
    if context_names & {"D_80854B18", "D_80854E50"} or "PLAYER_CSACTION" in comments:
        if "kakeyori_wait" in n64_name:
            return "player_cutscene_wait_function_with_sfx"
        return "player_cutscene_action_table"
    if "D_80854A70" in context_names:
        return "player_magic_spell_stage_table"
    if "D_80853914" in context_names:
        if "jump_climb_hold" in n64_name:
            return "player_model_anim_group_climb_hold_variant"
        if "slope_slip_end" in n64_name:
            return "player_model_anim_group_slope_slip_variant"
        return "player_model_anim_group_variant"
    if "swimer_swim_15step_up" in n64_name:
        return "player_water_ledge_climb_runtime_action"
    if "swimer_swim_wait" in n64_name or "swim" in source_lines:
        return "player_swim_state_runtime_action"
    return "direct_player_actor_callsite_unclassified"


def pose_source_callsite_next_evidence(callsite_class: str) -> str:
    if callsite_class.startswith("player_cutscene"):
        return "cutscene_actor_cue_runtime_capture"
    if callsite_class.startswith("player_model_anim_group"):
        return "model_anim_type_and_equipment_variant_capture"
    if callsite_class == "player_magic_spell_stage_table":
        return "magic_spell_sequence_runtime_capture"
    if callsite_class in {
        "player_water_ledge_climb_runtime_action",
        "player_swim_state_runtime_action",
    }:
        return "stateful_player_runtime_transform_capture"
    if callsite_class == "no_direct_source_callsite":
        return "alternate_source_or_callsite_discovery"
    return "manual_callsite_context_review"


def pose_source_callsite_required_next_step(callsite_class: str) -> str:
    if callsite_class.startswith("player_cutscene"):
        return (
            "bind the PLAYER_CSACTION cue to cutscene data and capture the installed runtime draw for "
            "the action/wait pair before accepting the source"
        )
    if callsite_class.startswith("player_model_anim_group"):
        return (
            "capture the modelAnimType/equipment variant that selects this table entry and compare after "
            "the player actor state has applied the variant"
        )
    if callsite_class == "player_magic_spell_stage_table":
        return (
            "capture the magic spell action stage that indexes D_80854A70 and compare the timed sequence "
            "rather than the isolated clip"
        )
    if callsite_class == "player_water_ledge_climb_runtime_action":
        return (
            "capture the water ledge-climb setup including world position and shape.yOffset adjustments "
            "before pose comparison"
        )
    if callsite_class == "player_swim_state_runtime_action":
        return (
            "capture the swim action state after water, velocity, morph, and upper-body updates are applied"
        )
    if callsite_class == "no_direct_source_callsite":
        return "search additional source roots or identify a different OOT3D source"
    return "inspect the player actor callsite manually and define the runtime capture condition"


def pose_source_callsite_context_summary(contexts: list[dict[str, object]]) -> str:
    parts: list[str] = []
    for context in contexts[:6]:
        parts.append(
            ":".join(
                [
                    str(context.get("source_path_class") or "source"),
                    str(context.get("line") or ""),
                    str(context.get("context_name") or ""),
                    str(context.get("line_comment") or context.get("nearby_block_comment") or ""),
                ]
            )
        )
    return ";".join(parts)


def semantic_ownership_derivative_frontier_analysis(
    semantic_blocked_resolution_frontier: dict[str, object],
    semantic_ownership_derivative_plan: dict[str, object],
    semantic_ownership_derivative_pose_metric: dict[str, object],
    sample_limit: int,
) -> dict[str, object]:
    frontier_rows = [
        row
        for row in list_dicts(semantic_blocked_resolution_frontier.get("rows", []))
        if row.get("frontier_gate") == "csab_source_ownership"
    ]
    plan_by_name = records_by_n64_name(semantic_ownership_derivative_plan.get("rows", []))
    pose_by_name = records_by_n64_name(semantic_ownership_derivative_pose_metric.get("rows", []))
    source_claim_counts: Counter[str] = Counter(
        str(row.get("source_csab_name") or "missing_source_csab") for row in frontier_rows
    )
    rows: list[dict[str, object]] = []
    derivative_counts: Counter[str] = Counter()
    post_gate_counts: Counter[str] = Counter()
    frontier_counts: Counter[str] = Counter()
    reference_counts: Counter[str] = Counter()
    materialization_counts: Counter[str] = Counter()
    source_claim_size_counts: Counter[str] = Counter()
    issue_count = 0
    missing_plan_count = 0
    missing_pose_metric_count = 0
    for row in frontier_rows:
        n64_name = str(row.get("n64_name") or "")
        plan_row = plan_by_name.get(n64_name, {})
        pose_row = pose_by_name.get(n64_name, {})
        if not plan_row:
            missing_plan_count += 1
        if not pose_row:
            missing_pose_metric_count += 1
        source_csab_name = str(row.get("source_csab_name") or "missing_source_csab")
        source_claim_count = source_claim_counts[source_csab_name]
        frontier_class = str(row.get("frontier_class") or "")
        pose_metric_status = str(pose_row.get("pose_metric_status") or "missing_pose_metric")
        row_issue_count = int_or_none_local(pose_row.get("issue_count")) or 0
        reference_envelope_status = str(
            pose_row.get("reference_envelope_status")
            or row.get("reference_envelope_status")
            or "unknown_reference_envelope"
        )
        derivative_class = semantic_ownership_derivative_frontier_class(
            frontier_class,
            pose_metric_status,
            row_issue_count,
            reference_envelope_status,
        )
        post_gate = semantic_ownership_derivative_post_gate(derivative_class)
        required_next_step = semantic_ownership_derivative_required_next_step(derivative_class)
        materialization_class = plan_row.get("materialization_class") or row.get("materialization_class")
        out = {
            "index": row.get("index"),
            "n64_name": row.get("n64_name"),
            "n64_stem": row.get("n64_stem"),
            "n64_data_name": row.get("n64_data_name"),
            "n64_frame_count": row.get("n64_frame_count"),
            "source_contract_kind": row.get("source_contract_kind"),
            "source_csab_name": row.get("source_csab_name"),
            "source_oot3d_stem": row.get("source_oot3d_stem"),
            "source_frame_slot_count": row.get("source_frame_slot_count"),
            "source_csab_claim_count": source_claim_count,
            "frontier_class": frontier_class,
            "frontier_gate": row.get("frontier_gate"),
            "ownership_gate": row.get("ownership_gate"),
            "materialization_class": materialization_class,
            "frame_mapping_status": plan_row.get("frame_mapping_status"),
            "output_csab_name": plan_row.get("output_csab_name"),
            "output_track_resource_path": plan_row.get("output_track_resource_path"),
            "pose_metric_status": pose_metric_status,
            "pose_metric_issue_count": row_issue_count,
            "reference_envelope_status": reference_envelope_status,
            "normalized_extent_max_abs_delta_max": pose_row.get("normalized_extent_max_abs_delta_max"),
            "center_delta_normalized_max": pose_row.get("center_delta_normalized_max"),
            "scale_factor_oot3d_per_n64_avg": pose_row.get("scale_factor_oot3d_per_n64_avg"),
            "scale_factor_oot3d_per_n64_min": pose_row.get("scale_factor_oot3d_per_n64_min"),
            "scale_factor_oot3d_per_n64_max": pose_row.get("scale_factor_oot3d_per_n64_max"),
            "derivative_frontier_class": derivative_class,
            "post_derivative_gate": post_gate,
            "runtime_acceptance_status": "diagnostic_ownership_derivative_not_runtime_accepted",
            "required_next_step": required_next_step,
        }
        rows.append(out)
        derivative_counts[derivative_class] += 1
        post_gate_counts[post_gate] += 1
        frontier_counts[frontier_class] += 1
        reference_counts[reference_envelope_status] += 1
        materialization_counts[str(materialization_class or "unknown_materialization_class")] += 1
        source_claim_size_counts[str(source_claim_count)] += 1
        issue_count += row_issue_count

    rows.sort(
        key=lambda item: (
            semantic_ownership_derivative_frontier_priority(str(item.get("derivative_frontier_class") or "")),
            int(item.get("index") or 0),
        )
    )
    ownership_only_pose_ready_count = sum(
        count for key, count in derivative_counts.items() if key.endswith("_ownership_only_pose_ready")
    )
    ownership_plus_pose_or_source_risk_count = sum(
        count for key, count in derivative_counts.items() if key.endswith("_ownership_plus_pose_or_source_risk")
    )
    return {
        "format": LINK_CHILD_ANIMATION_SEMANTIC_OWNERSHIP_DERIVATIVE_FRONTIER_FORMAT,
        "status": semantic_ownership_derivative_frontier_status(
            rows,
            issue_count,
            missing_plan_count,
            missing_pose_metric_count,
        ),
        "policy": {
            "scope": (
                "post-metric accounting for diagnostic derivatives of blocked Link child ownership rows"
            ),
            "semantic_effect": (
                "splits ownership-only pose-ready rows from rows that still need alternate source or "
                "runtime/skinned parity proof"
            ),
            "runtime_acceptance_policy": (
                "this frontier classifies offline diagnostics only; it does not promote runtime mappings"
            ),
        },
        "row_count": len(rows),
        "ownership_only_pose_ready_count": ownership_only_pose_ready_count,
        "ownership_plus_pose_or_source_risk_count": ownership_plus_pose_or_source_risk_count,
        "missing_plan_count": missing_plan_count,
        "missing_pose_metric_count": missing_pose_metric_count,
        "issue_count": issue_count,
        "derivative_frontier_class_counts": sorted_counter(derivative_counts),
        "post_derivative_gate_counts": sorted_counter(post_gate_counts),
        "source_frontier_class_counts": sorted_counter(frontier_counts),
        "reference_envelope_status_counts": sorted_counter(reference_counts),
        "materialization_class_counts": sorted_counter(materialization_counts),
        "source_csab_claim_count_counts": sorted_counter(source_claim_size_counts),
        "sample_rows": rows[:sample_limit],
        "rows": rows,
    }


def semantic_ownership_derivative_frontier_status(
    rows: list[dict[str, object]],
    issue_count: int,
    missing_plan_count: int,
    missing_pose_metric_count: int,
) -> str:
    if not rows:
        return "complete_no_ownership_derivative_rows"
    if issue_count or missing_plan_count or missing_pose_metric_count:
        return "post_derivative_frontier_incomplete"
    return "post_derivative_frontier_ready"


def semantic_ownership_derivative_frontier_class(
    frontier_class: str,
    pose_metric_status: str,
    pose_metric_issue_count: int,
    reference_envelope_status: str,
) -> str:
    if pose_metric_status != "measured" or pose_metric_issue_count:
        return "ownership_derivative_pose_metric_unmeasured_or_issued"
    if frontier_class == "promoted_source_reuse_ownership_required":
        prefix = "promoted_source"
    elif frontier_class == "candidate_source_reuse_ownership_required":
        prefix = "candidate_source"
    else:
        prefix = "manual_source"
    if reference_envelope_status == "inside_reference_envelope":
        return f"{prefix}_ownership_only_pose_ready"
    if reference_envelope_status == "outside_reference_envelope":
        return f"{prefix}_ownership_plus_pose_or_source_risk"
    return f"{prefix}_ownership_reference_envelope_unknown"


def semantic_ownership_derivative_post_gate(derivative_class: str) -> str:
    if derivative_class.endswith("_ownership_only_pose_ready"):
        return "ownership_or_intentional_reuse_policy"
    if derivative_class.endswith("_ownership_plus_pose_or_source_risk"):
        return "alternate_source_or_runtime_skinned_parity"
    if derivative_class == "ownership_derivative_pose_metric_unmeasured_or_issued":
        return "rerun_pose_metric_or_fix_derivative_materialization"
    return "manual_post_derivative_review"


def semantic_ownership_derivative_required_next_step(derivative_class: str) -> str:
    if derivative_class.endswith("_ownership_only_pose_ready"):
        return "prove CSAB ownership/reuse, then package a controlled runtime/skinned-parity subset"
    if derivative_class.endswith("_ownership_plus_pose_or_source_risk"):
        return "find alternate source or prove current derivative with runtime/skinned pose parity"
    if derivative_class == "ownership_derivative_pose_metric_unmeasured_or_issued":
        return "fix derivative metric coverage before ownership arbitration"
    return "manual post-derivative review"


def semantic_ownership_derivative_frontier_priority(derivative_class: str) -> int:
    return {
        "candidate_source_ownership_only_pose_ready": 0,
        "promoted_source_ownership_only_pose_ready": 1,
        "candidate_source_ownership_plus_pose_or_source_risk": 2,
        "promoted_source_ownership_plus_pose_or_source_risk": 3,
        "ownership_derivative_pose_metric_unmeasured_or_issued": 4,
    }.get(derivative_class, 9)


def semantic_ownership_reuse_arbitration_analysis(
    semantic_ownership_derivative_frontier: dict[str, object],
    semantic_resolution_contract: dict[str, object],
    source_references: dict[str, dict[str, object]],
    sample_limit: int,
) -> dict[str, object]:
    frontier_rows = [
        row
        for row in list_dicts(semantic_ownership_derivative_frontier.get("rows", []))
        if str(row.get("derivative_frontier_class") or "").endswith("_ownership_only_pose_ready")
    ]
    contract_by_name = records_by_n64_name(semantic_resolution_contract.get("rows", []))
    rows_by_source: dict[str, list[dict[str, object]]] = {}
    for row in frontier_rows:
        rows_by_source.setdefault(str(row.get("source_csab_name") or "missing_source_csab"), []).append(row)

    rows: list[dict[str, object]] = []
    class_counts: Counter[str] = Counter()
    route_counts: Counter[str] = Counter()
    frontier_counts: Counter[str] = Counter()
    source_claim_counts: Counter[str] = Counter()
    decision_scope_counts: Counter[str] = Counter()
    source_group_class_counts: Counter[str] = Counter()
    direct_source_reference_count = 0
    direct_player_actor_reference_count = 0
    route_proven_count = 0
    route_absent_count = 0

    source_group_rows: list[dict[str, object]] = []
    for source_csab_name, group_rows in sorted(rows_by_source.items()):
        group_class = semantic_ownership_reuse_source_group_class(group_rows)
        source_group_class_counts[group_class] += 1
        source_group_rows.append(
            {
                "source_csab_name": source_csab_name,
                "source_group_row_count": len(group_rows),
                "source_group_class": group_class,
                "source_group_n64_names": ";".join(
                    sorted(str(row.get("n64_name") or "") for row in group_rows if row.get("n64_name"))
                ),
            }
        )

    source_group_class_by_source = {
        str(row.get("source_csab_name") or ""): str(row.get("source_group_class") or "")
        for row in source_group_rows
    }

    for row in frontier_rows:
        n64_name = str(row.get("n64_name") or "")
        source_csab_name = str(row.get("source_csab_name") or "missing_source_csab")
        contract_row = contract_by_name.get(n64_name, {})
        source_reference = source_references.get(n64_name, source_reference_empty(n64_name))
        source_count = int(source_reference.get("direct_source_reference_count") or 0)
        player_actor_count = int(source_reference.get("direct_player_actor_source_reference_count") or 0)
        route_status = semantic_ownership_reuse_route_status(source_count, player_actor_count)
        source_claim_count = len(rows_by_source.get(source_csab_name, []))
        arbitration_class = semantic_ownership_reuse_arbitration_class(
            str(row.get("frontier_class") or ""),
            source_claim_count,
            route_status,
        )
        decision_scope = (
            "direct_route_row"
            if route_status == "direct_player_actor_route_reference_found"
            else "source_group"
            if source_claim_count > 1
            else "single_owner_alias"
        )
        route_acceptance_status = semantic_ownership_reuse_route_acceptance_status(route_status)
        out = {
            "index": row.get("index"),
            "n64_name": row.get("n64_name"),
            "n64_stem": row.get("n64_stem"),
            "n64_data_name": row.get("n64_data_name"),
            "n64_frame_count": row.get("n64_frame_count"),
            "source_csab_name": row.get("source_csab_name"),
            "source_oot3d_stem": row.get("source_oot3d_stem"),
            "source_frame_slot_count": row.get("source_frame_slot_count"),
            "source_group_n64_names": ";".join(
                sorted(
                    str(item.get("n64_name") or "")
                    for item in rows_by_source.get(source_csab_name, [])
                    if item.get("n64_name")
                )
            ),
            "source_csab_claim_count": source_claim_count,
            "source_group_class": source_group_class_by_source.get(source_csab_name),
            "frontier_class": row.get("frontier_class"),
            "derivative_frontier_class": row.get("derivative_frontier_class"),
            "ownership_gate": row.get("ownership_gate"),
            "materialization_class": row.get("materialization_class"),
            "output_csab_name": row.get("output_csab_name"),
            "output_track_resource_path": row.get("output_track_resource_path"),
            "runtime_csab_collision_n64_names": contract_row.get("runtime_csab_collision_n64_names"),
            "candidate_csab_collision_n64_names": contract_row.get("candidate_csab_collision_n64_names"),
            "direct_source_reference_count": source_count,
            "direct_player_actor_source_reference_count": player_actor_count,
            "route_evidence_status": route_status,
            "sample_direct_source_references": source_reference_sample_string(source_reference),
            "ownership_reuse_arbitration_class": arbitration_class,
            "policy_decision_scope": decision_scope,
            "route_acceptance_status": route_acceptance_status,
            "runtime_acceptance_status": "diagnostic_ownership_reuse_not_runtime_accepted",
            "required_next_step": semantic_ownership_reuse_required_next_step(arbitration_class),
        }
        rows.append(out)
        class_counts[arbitration_class] += 1
        route_counts[route_status] += 1
        frontier_counts[str(row.get("frontier_class") or "unknown_frontier_class")] += 1
        source_claim_counts[str(source_claim_count)] += 1
        decision_scope_counts[decision_scope] += 1
        direct_source_reference_count += source_count
        direct_player_actor_reference_count += player_actor_count
        if route_status == "direct_player_actor_route_reference_found":
            route_proven_count += 1
        if route_status == "no_direct_source_reference_found":
            route_absent_count += 1

    rows.sort(
        key=lambda item: (
            semantic_ownership_reuse_arbitration_priority(
                str(item.get("ownership_reuse_arbitration_class") or "")
            ),
            int(item.get("index") or 0),
        )
    )
    return {
        "format": LINK_CHILD_ANIMATION_SEMANTIC_OWNERSHIP_REUSE_ARBITRATION_FORMAT,
        "status": "reuse_arbitration_ready" if rows else "complete_no_ownership_only_pose_ready_rows",
        "policy": {
            "scope": "route/callsite and intentional-reuse accounting for ownership-only pose-ready derivatives",
            "semantic_effect": (
                "does not accept ownership; separates direct route evidence from policy/capture decisions"
            ),
            "runtime_acceptance_policy": (
                "rows remain diagnostic until ownership/reuse policy plus runtime/skinned parity are accepted"
            ),
        },
        "row_count": len(rows),
        "source_group_count": len(source_group_rows),
        "policy_decision_count": len(source_group_rows),
        "direct_source_reference_count": direct_source_reference_count,
        "direct_player_actor_route_reference_count": direct_player_actor_reference_count,
        "route_proven_row_count": route_proven_count,
        "route_absent_row_count": route_absent_count,
        "ownership_reuse_arbitration_class_counts": sorted_counter(class_counts),
        "route_evidence_status_counts": sorted_counter(route_counts),
        "source_frontier_class_counts": sorted_counter(frontier_counts),
        "source_csab_claim_count_counts": sorted_counter(source_claim_counts),
        "policy_decision_scope_counts": sorted_counter(decision_scope_counts),
        "source_group_class_counts": sorted_counter(source_group_class_counts),
        "sample_source_groups": source_group_rows[:sample_limit],
        "sample_rows": rows[:sample_limit],
        "source_groups": source_group_rows,
        "rows": rows,
    }


def semantic_ownership_reuse_source_group_class(group_rows: list[dict[str, object]]) -> str:
    if len(group_rows) > 1:
        return "multi_claim_source_group_intentional_reuse_policy_candidate"
    frontier_class = str(group_rows[0].get("frontier_class") or "") if group_rows else ""
    if frontier_class == "promoted_source_reuse_ownership_required":
        return "single_promoted_owner_alias_policy_candidate"
    if frontier_class == "candidate_source_reuse_ownership_required":
        return "single_candidate_owner_alias_policy_candidate"
    return "single_manual_owner_policy_candidate"


def semantic_ownership_reuse_route_status(source_count: int, player_actor_count: int) -> str:
    if player_actor_count > 0:
        return "direct_player_actor_route_reference_found"
    if source_count > 0:
        return "direct_non_player_source_reference_found"
    return "no_direct_source_reference_found"


def semantic_ownership_reuse_arbitration_class(
    frontier_class: str,
    source_claim_count: int,
    route_status: str,
) -> str:
    if route_status == "direct_player_actor_route_reference_found":
        return "route_proven_ownership_candidate"
    if route_status == "direct_non_player_source_reference_found":
        return "non_player_callsite_manual_acceptance_candidate"
    if source_claim_count > 1:
        return "route_absent_multi_claim_intentional_reuse_policy_candidate"
    if frontier_class == "promoted_source_reuse_ownership_required":
        return "route_absent_promoted_owner_single_alias_policy_candidate"
    if frontier_class == "candidate_source_reuse_ownership_required":
        return "route_absent_candidate_owner_single_alias_policy_candidate"
    return "route_absent_manual_owner_policy_candidate"


def semantic_ownership_reuse_route_acceptance_status(route_status: str) -> str:
    if route_status == "direct_player_actor_route_reference_found":
        return "route_evidence_can_support_ownership_review"
    if route_status == "direct_non_player_source_reference_found":
        return "manual_non_player_callsite_review_required"
    return "route_not_proven_by_current_source_scan"


def semantic_ownership_reuse_required_next_step(arbitration_class: str) -> str:
    if arbitration_class == "route_proven_ownership_candidate":
        return "review direct player-actor route and then package a runtime/skinned-parity subset"
    if arbitration_class == "non_player_callsite_manual_acceptance_candidate":
        return "review non-player callsite label or capture runtime route before ownership acceptance"
    if arbitration_class == "route_absent_multi_claim_intentional_reuse_policy_candidate":
        return "make one source-group intentional-reuse decision, then runtime/skinned-parity test a subset"
    if arbitration_class == "route_absent_promoted_owner_single_alias_policy_candidate":
        return "decide whether the promoted owner can intentionally fork to this derivative, then parity-test"
    if arbitration_class == "route_absent_candidate_owner_single_alias_policy_candidate":
        return "choose candidate owner or intentional derivative policy, then parity-test"
    return "manual ownership/reuse policy review"


def semantic_ownership_reuse_arbitration_priority(arbitration_class: str) -> int:
    return {
        "route_proven_ownership_candidate": 0,
        "non_player_callsite_manual_acceptance_candidate": 1,
        "route_absent_multi_claim_intentional_reuse_policy_candidate": 2,
        "route_absent_promoted_owner_single_alias_policy_candidate": 3,
        "route_absent_candidate_owner_single_alias_policy_candidate": 4,
        "route_absent_manual_owner_policy_candidate": 5,
    }.get(arbitration_class, 9)


def semantic_route_proven_ownership_callsite_analysis(
    semantic_ownership_reuse_arbitration: dict[str, object],
    source_references: dict[str, dict[str, object]],
    sample_limit: int,
) -> dict[str, object]:
    arbitration_rows = [
        row
        for row in list_dicts(semantic_ownership_reuse_arbitration.get("rows", []))
        if row.get("ownership_reuse_arbitration_class") == "route_proven_ownership_candidate"
    ]
    rows: list[dict[str, object]] = []
    class_counts: Counter[str] = Counter()
    package_status_counts: Counter[str] = Counter()
    context_counts: Counter[str] = Counter()
    context_review_counts: Counter[str] = Counter()
    usage_status_counts: Counter[str] = Counter()
    total_direct_player_actor_reference_count = 0
    total_local_source_reference_count = 0
    total_external_duplicate_reference_count = 0
    unique_callsite_signatures: set[str] = set()
    unique_context_signatures: set[str] = set()

    for row in arbitration_rows:
        n64_name = str(row.get("n64_name") or "")
        source_reference = source_references.get(n64_name, source_reference_empty(n64_name))
        contexts = route_proven_callsite_contexts(source_reference)
        player_contexts = [
            context
            for context in contexts
            if context.get("source_path_class") in {"local_player_actor", "external_player_actor_mirror"}
        ]
        local_contexts = [
            context for context in player_contexts if context.get("source_path_class") == "local_player_actor"
        ]
        external_contexts = [
            context for context in player_contexts if context.get("source_path_class") == "external_player_actor_mirror"
        ]
        canonical_contexts = local_contexts if local_contexts else player_contexts
        row_unique_callsite_signatures = {
            str(context.get("callsite_signature") or "")
            for context in canonical_contexts
            if context.get("callsite_signature")
        }
        row_unique_context_signatures = {
            str(context.get("context_signature") or "")
            for context in canonical_contexts
            if context.get("context_signature")
        }
        unique_callsite_signatures.update(row_unique_callsite_signatures)
        unique_context_signatures.update(row_unique_context_signatures)
        context_names = sorted(
            {
                str(context.get("context_name") or "")
                for context in canonical_contexts
                if context.get("context_name")
            }
        )
        context_review_classes = sorted(
            {
                str(context.get("callsite_review_class") or "")
                for context in canonical_contexts
                if context.get("callsite_review_class")
            }
        )
        usage_statuses = sorted(
            {
                str(context.get("context_usage_status") or "")
                for context in canonical_contexts
                if context.get("context_usage_status")
            }
        )
        package_status = (
            "route_callsite_package_candidate_ready"
            if row.get("output_track_resource_path")
            else "missing_derived_track_output_path"
        )
        out = {
            "index": row.get("index"),
            "n64_name": row.get("n64_name"),
            "n64_stem": row.get("n64_stem"),
            "n64_data_name": row.get("n64_data_name"),
            "n64_frame_count": row.get("n64_frame_count"),
            "source_csab_name": row.get("source_csab_name"),
            "source_oot3d_stem": row.get("source_oot3d_stem"),
            "source_frame_slot_count": row.get("source_frame_slot_count"),
            "materialization_class": row.get("materialization_class"),
            "output_csab_name": row.get("output_csab_name"),
            "output_track_resource_path": row.get("output_track_resource_path"),
            "source_group_n64_names": row.get("source_group_n64_names"),
            "source_csab_claim_count": row.get("source_csab_claim_count"),
            "ownership_reuse_arbitration_class": row.get("ownership_reuse_arbitration_class"),
            "direct_player_actor_source_reference_count": row.get("direct_player_actor_source_reference_count"),
            "local_player_actor_source_reference_count": len(local_contexts),
            "external_player_actor_mirror_reference_count": len(external_contexts),
            "unique_callsite_signature_count": len(row_unique_callsite_signatures),
            "unique_context_signature_count": len(row_unique_context_signatures),
            "context_names": ";".join(context_names),
            "callsite_review_classes": ";".join(context_review_classes),
            "context_usage_statuses": ";".join(usage_statuses),
            "callsite_context_summary": route_proven_callsite_context_summary(canonical_contexts),
            "package_candidate_status": package_status,
            "runtime_acceptance_status": "route_callsite_not_runtime_accepted",
            "required_next_step": (
                "package this derived track subset and run runtime/skinned pose parity before promotion"
            ),
        }
        rows.append(out)
        class_counts[str(row.get("ownership_reuse_arbitration_class") or "unknown")] += 1
        package_status_counts[package_status] += 1
        for context_name in context_names:
            context_counts[context_name] += 1
        for review_class in context_review_classes:
            context_review_counts[review_class] += 1
        for usage_status in usage_statuses:
            usage_status_counts[usage_status] += 1
        total_direct_player_actor_reference_count += int(row.get("direct_player_actor_source_reference_count") or 0)
        total_local_source_reference_count += len(local_contexts)
        total_external_duplicate_reference_count += len(external_contexts)

    rows.sort(key=lambda item: int(item.get("index") or 0))
    return {
        "format": LINK_CHILD_ANIMATION_SEMANTIC_ROUTE_PROVEN_OWNERSHIP_CALLSITE_FORMAT,
        "status": "route_callsite_review_ready" if rows else "complete_no_route_proven_ownership_rows",
        "policy": {
            "scope": "callsite context for ownership derivatives with direct player-actor route evidence",
            "semantic_effect": (
                "contextualizes route evidence and identifies a controlled package/parity candidate subset; "
                "does not promote runtime mappings"
            ),
            "runtime_acceptance_policy": (
                "route-proven rows still require packaging plus runtime/skinned pose parity before promotion"
            ),
        },
        "row_count": len(rows),
        "package_candidate_count": package_status_counts.get("route_callsite_package_candidate_ready", 0),
        "direct_player_actor_source_reference_count": total_direct_player_actor_reference_count,
        "local_player_actor_source_reference_count": total_local_source_reference_count,
        "external_player_actor_mirror_reference_count": total_external_duplicate_reference_count,
        "unique_callsite_signature_count": len(unique_callsite_signatures),
        "unique_context_signature_count": len(unique_context_signatures),
        "ownership_reuse_arbitration_class_counts": sorted_counter(class_counts),
        "package_candidate_status_counts": sorted_counter(package_status_counts),
        "context_name_counts": sorted_counter(context_counts),
        "callsite_review_class_counts": sorted_counter(context_review_counts),
        "context_usage_status_counts": sorted_counter(usage_status_counts),
        "sample_rows": rows[:sample_limit],
        "rows": rows,
    }


def route_proven_callsite_contexts(source_reference: dict[str, object]) -> list[dict[str, object]]:
    samples = source_reference.get("sample_direct_source_references")
    if not isinstance(samples, list):
        return []
    contexts: list[dict[str, object]] = []
    for sample in samples:
        if not isinstance(sample, dict):
            continue
        context = route_proven_callsite_context(sample)
        if context:
            contexts.append(context)
    return contexts


def route_proven_callsite_context(sample: dict[str, object]) -> dict[str, object]:
    path = Path(str(sample.get("path") or ""))
    line = int_or_none_local(sample.get("line"))
    if line is None or line <= 0 or not path.is_file():
        return {}
    try:
        lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
    except OSError:
        return {}
    if line > len(lines):
        return {}
    line_index = line - 1
    source_line = lines[line_index].rstrip()
    declaration = route_proven_context_declaration(lines, line_index)
    context_name = declaration.get("context_name") or ""
    relative_line = line - int(declaration.get("context_start_line") or line)
    line_comment = source_line_comment(source_line)
    nearby_comment = nearby_block_comment(lines, line_index, int(declaration.get("context_start_line") or 1) - 1)
    usage = source_context_usage(path, lines, str(context_name), int(declaration.get("context_start_line") or 0))
    callsite_signature = "|".join(
        [
            str(context_name),
            str(relative_line),
            re.sub(r"\s+", " ", source_line.strip()),
        ]
    )
    context_signature = "|".join([str(context_name), str(declaration.get("context_kind") or "")])
    review_class = route_proven_callsite_review_class(str(context_name), nearby_comment, line_comment)
    return {
        "path": str(path),
        "line": line,
        "source_path_class": source_path_class(path),
        "source_line": source_line.strip(),
        "context_kind": declaration.get("context_kind"),
        "context_name": context_name,
        "context_start_line": declaration.get("context_start_line"),
        "relative_context_line": relative_line,
        "line_comment": line_comment,
        "nearby_block_comment": nearby_comment,
        "callsite_review_class": review_class,
        "context_usage_count": usage["usage_count"],
        "context_usage_samples": usage["usage_samples"],
        "context_usage_status": (
            "context_used_by_player_actor_runtime"
            if usage["usage_count"] > 0
            else "context_declaration_only"
        ),
        "callsite_signature": callsite_signature,
        "context_signature": context_signature,
    }


def route_proven_context_declaration(lines: list[str], line_index: int) -> dict[str, object]:
    for index in range(line_index, -1, -1):
        line = lines[index].strip()
        table_match = re.match(
            r"(?:static\s+)?[A-Za-z_][\w\s\*]*\s+(?P<name>[A-Za-z_]\w*)\s*(?:\[[^\]]*\])+\s*=\s*\{",
            line,
        )
        if table_match:
            return {
                "context_kind": "static_table",
                "context_name": table_match.group("name"),
                "context_start_line": index + 1,
            }
        function_match = re.match(
            r"(?:static\s+)?[A-Za-z_][\w\s\*]*\s+(?P<name>[A-Za-z_]\w*)\s*\([^;]*\)\s*\{",
            line,
        )
        if function_match:
            return {
                "context_kind": "function",
                "context_name": function_match.group("name"),
                "context_start_line": index + 1,
            }
    return {
        "context_kind": "unknown",
        "context_name": "",
        "context_start_line": line_index + 1,
    }


def source_line_comment(line: str) -> str:
    if "//" not in line:
        return ""
    return line.split("//", 1)[1].strip()


def nearby_block_comment(lines: list[str], line_index: int, context_start_index: int) -> str:
    for index in range(line_index - 1, max(context_start_index - 1, line_index - 8), -1):
        text = lines[index].strip()
        match = re.search(r"/\*\s*(.*?)\s*\*/", text)
        if match:
            return match.group(1).strip()
    return ""


def source_context_usage(path: Path, lines: list[str], context_name: str, declaration_line: int) -> dict[str, object]:
    if not context_name:
        return {"usage_count": 0, "usage_samples": ""}
    pattern = re.compile(r"\b" + re.escape(context_name) + r"\b")
    samples: list[str] = []
    count = 0
    for index, line in enumerate(lines, start=1):
        if index == declaration_line:
            continue
        if not pattern.search(line):
            continue
        count += 1
        if len(samples) < 5:
            samples.append(f"{path}:{index}")
    return {
        "usage_count": count,
        "usage_samples": ";".join(samples),
    }


def source_path_class(path: Path) -> str:
    normalized = path.as_posix().lower()
    if "/tools/oot3d/external/oot/src/overlays/actors/ovl_player_actor/z_player.c" in normalized:
        return "external_player_actor_mirror"
    if "/soh/src/overlays/actors/ovl_player_actor/z_player.c" in normalized:
        return "local_player_actor"
    if source_reference_is_player_actor_path(normalized):
        return "other_player_actor"
    return "non_player_source"


def route_proven_callsite_review_class(context_name: str, nearby_comment: str, line_comment: str) -> str:
    if context_name == "sAgeProperties" and line_comment == "unk_98":
        return "age_properties_treasure_box_open_route"
    if context_name == "D_80853914" and nearby_comment == "PLAYER_ANIMGROUP_jump_climb_up":
        return "player_animation_group_jump_climb_up_route"
    if context_name == "D_808543C4":
        return "defense_hit_player_actor_table_route"
    if context_name == "D_808545CC":
        return "fighter_rebound_action_table_route"
    if context_name:
        return "player_actor_static_table_route"
    return "manual_callsite_context_review"


def route_proven_callsite_context_summary(contexts: list[dict[str, object]]) -> str:
    parts: list[str] = []
    seen: set[str] = set()
    for context in contexts:
        signature = str(context.get("callsite_signature") or "")
        if signature in seen:
            continue
        seen.add(signature)
        path = str(context.get("path") or "")
        line = context.get("line")
        context_name = str(context.get("context_name") or "")
        review_class = str(context.get("callsite_review_class") or "")
        label = str(context.get("nearby_block_comment") or context.get("line_comment") or "")
        parts.append(f"{path}:{line}:{context_name}:{review_class}:{label}")
    return "|".join(parts[:8])


def semantic_derivative_frame_mapping_class(source_frame_count: int, target_frame_count: int) -> str:
    if source_frame_count > target_frame_count:
        return "compress_source_csab_to_n64_frame_count"
    if source_frame_count < target_frame_count:
        return "expand_source_csab_to_n64_frame_count"
    if source_frame_count > 0 and target_frame_count > 0:
        return "same_frame_identity"
    return "manual_frame_mapping_required"


def semantic_materialization_plan_analysis(
    semantic_resolution_contract: dict[str, object],
    character_profile_o2r_path: Path | None,
    sample_limit: int,
) -> dict[str, object]:
    contract_rows = list_dicts(semantic_resolution_contract.get("rows", []))
    ready_rows = [
        row
        for row in contract_rows
        if row.get("semantic_resolution_status") == "bake_contract_ready_pending_materialization"
    ]
    base: dict[str, object] = {
        "format": LINK_CHILD_ANIMATION_SEMANTIC_MATERIALIZATION_PLAN_FORMAT,
        "status": "not_built_missing_character_profile_o2r",
        "policy": {
            "scope": "deterministic source-track and frame-map plan for bake-ready Link child N64 animation aliases",
            "runtime_acceptance_policy": (
                "this plan does not create accepted runtime mappings; each row must still be sampled, rekeyed, "
                "packaged as a derived animation resource, and pose-parity verified"
            ),
            "frame_mapping_policy": (
                "for target N64 frame i, sample source CSAB frame i * (source_frame_slot_count - 1) / "
                "(target_n64_frame_count - 1); one-frame targets sample source frame 0"
            ),
        },
        "character_profile_o2r": str(character_profile_o2r_path) if character_profile_o2r_path is not None else None,
        "ready_contract_count": len(ready_rows),
        "plan_row_count": 0,
        "resolved_source_resource_count": 0,
        "missing_source_resource_count": 0,
        "source_frame_count_mismatch_count": 0,
        "output_resource_path_duplicate_count": 0,
        "issue_count": 0,
        "sample_issues": [],
        "sample_rows": [],
        "rows": [],
    }
    if character_profile_o2r_path is None:
        return base
    if not character_profile_o2r_path.is_file():
        return {
            **base,
            "status": "missing_character_profile_o2r",
            "issue_count": 1,
            "sample_issues": [
                {
                    "type": "missing_character_profile_o2r",
                    "path": str(character_profile_o2r_path),
                }
            ],
        }

    runtime_profile_path = "characters/oot3d/link_child/character_runtime_profile.json"
    plan_rows: list[dict[str, object]] = []
    issues: list[dict[str, object]] = []
    materialization_counts: Counter[str] = Counter()
    source_track_format_counts: Counter[str] = Counter()
    source_track_validation_counts: Counter[str] = Counter()
    source_resolution_counts: Counter[str] = Counter()
    frame_mapping_counts: Counter[str] = Counter()
    output_resource_path_counts: Counter[str] = Counter()
    source_resource_path_counts: Counter[str] = Counter()
    source_track_sha1_counts: Counter[str] = Counter()
    total_target_frame_count = 0
    total_source_frame_slot_count = 0
    total_frame_sample_count = 0

    try:
        with zipfile.ZipFile(character_profile_o2r_path) as archive:
            archive_names = set(archive.namelist())
            if runtime_profile_path not in archive_names:
                return {
                    **base,
                    "status": "missing_runtime_profile",
                    "issue_count": 1,
                    "sample_issues": [
                        {
                            "type": "missing_runtime_profile",
                            "resource_path": runtime_profile_path,
                        }
                    ],
                }
            profile = json.loads(archive.read(runtime_profile_path))
            animation_lookup = profile.get("animation_lookup")
            native_resources = profile.get("native_resources")
            by_csab_name = (
                animation_lookup.get("by_csab_name")
                if isinstance(animation_lookup, dict)
                and isinstance(animation_lookup.get("by_csab_name"), dict)
                else {}
            )
            animations = (
                native_resources.get("animations")
                if isinstance(native_resources, dict)
                and isinstance(native_resources.get("animations"), list)
                else []
            )
            for contract_row in sorted(ready_rows, key=lambda item: int(item.get("index") or 0)):
                plan_row, row_issues = semantic_materialization_plan_row(
                    archive,
                    archive_names,
                    animations,
                    by_csab_name,
                    contract_row,
                )
                plan_rows.append(plan_row)
                issues.extend(row_issues)
                materialization_counts[str(plan_row.get("materialization_class") or "none")] += 1
                source_track_format_counts[str(plan_row.get("source_track_format") or "unknown")] += 1
                source_track_validation_counts[str(plan_row.get("source_track_validation_status") or "unknown")] += 1
                source_resolution_counts[str(plan_row.get("source_resolution_status") or "unknown")] += 1
                frame_mapping_counts[str(plan_row.get("frame_mapping_status") or "unknown")] += 1
                output_resource_path_counts[str(plan_row.get("output_track_resource_path") or "")] += 1
                if plan_row.get("source_resource_path"):
                    source_resource_path_counts[str(plan_row["source_resource_path"])] += 1
                if plan_row.get("source_track_sha1"):
                    source_track_sha1_counts[str(plan_row["source_track_sha1"])] += 1
                total_target_frame_count += int(plan_row.get("target_n64_frame_count") or 0)
                total_source_frame_slot_count += int(plan_row.get("source_frame_slot_count") or 0)
                total_frame_sample_count += int(plan_row.get("frame_sample_count") or 0)
    except zipfile.BadZipFile as exc:
        return {
            **base,
            "status": "invalid_character_profile_o2r",
            "issue_count": 1,
            "sample_issues": [
                {
                    "type": "invalid_character_profile_o2r",
                    "message": str(exc),
                }
            ],
        }

    duplicate_output_paths = {
        path: count for path, count in output_resource_path_counts.items() if path and count > 1
    }
    if duplicate_output_paths:
        for plan_row in plan_rows:
            path = str(plan_row.get("output_track_resource_path") or "")
            if duplicate_output_paths.get(path, 0) > 1:
                plan_row["output_resource_path_status"] = "duplicate_output_resource_path"
        issues.extend(
            {
                "type": "duplicate_output_resource_path",
                "output_track_resource_path": path,
                "count": count,
            }
            for path, count in sorted(duplicate_output_paths.items())
        )
    else:
        for plan_row in plan_rows:
            plan_row["output_resource_path_status"] = "unique_output_resource_path"

    missing_source_count = source_resolution_counts.get("missing_source_csab_lookup", 0) + source_resolution_counts.get(
        "missing_source_resource",
        0,
    )
    frame_mismatch_count = frame_mapping_counts.get("source_frame_count_mismatch", 0)
    status = "materialization_plan_ready" if not issues and len(plan_rows) == len(ready_rows) else "partial"
    return {
        **base,
        "status": status,
        "plan_row_count": len(plan_rows),
        "resolved_source_resource_count": source_resolution_counts.get("resolved_source_track", 0),
        "missing_source_resource_count": missing_source_count,
        "source_frame_count_mismatch_count": frame_mismatch_count,
        "output_resource_path_duplicate_count": sum(count - 1 for count in duplicate_output_paths.values()),
        "unique_source_resource_count": len(source_resource_path_counts),
        "unique_source_track_sha1_count": len(source_track_sha1_counts),
        "unique_output_resource_path_count": len(
            [path for path in output_resource_path_counts if path]
        ),
        "total_target_frame_count": total_target_frame_count,
        "total_source_frame_slot_count": total_source_frame_slot_count,
        "total_frame_sample_count": total_frame_sample_count,
        "materialization_class_counts": sorted_counter(materialization_counts),
        "source_track_format_counts": sorted_counter(source_track_format_counts),
        "source_track_validation_status_counts": sorted_counter(source_track_validation_counts),
        "source_resolution_status_counts": sorted_counter(source_resolution_counts),
        "frame_mapping_status_counts": sorted_counter(frame_mapping_counts),
        "issue_count": len(issues),
        "sample_issues": issues[:sample_limit],
        "sample_rows": [
            semantic_materialization_plan_sample_row(row)
            for row in plan_rows[:sample_limit]
        ],
        "rows": plan_rows,
    }


def semantic_materialization_plan_row(
    archive: zipfile.ZipFile,
    archive_names: set[str],
    animations: list[object],
    by_csab_name: dict[object, object],
    contract_row: dict[str, object],
) -> tuple[dict[str, object], list[dict[str, object]]]:
    n64_name = str(contract_row.get("n64_name") or "")
    source_csab_name = str(contract_row.get("source_csab_name") or "")
    target_frame_count = int_or_none_local(contract_row.get("n64_frame_count")) or 0
    expected_source_frame_slot_count = int_or_none_local(contract_row.get("source_frame_slot_count")) or 0
    materialization_class = str(contract_row.get("materialization_class") or "")
    output_csab_name = semantic_materialization_output_csab_name(contract_row)
    output_track_resource_path = semantic_materialization_output_track_resource_path(contract_row)
    row: dict[str, object] = {
        "index": contract_row.get("index"),
        "n64_name": n64_name,
        "n64_stem": contract_row.get("n64_stem"),
        "n64_data_name": contract_row.get("n64_data_name"),
        "source_csab_name": source_csab_name,
        "source_oot3d_stem": contract_row.get("source_oot3d_stem"),
        "source_frame_slot_count": expected_source_frame_slot_count,
        "target_n64_frame_count": target_frame_count,
        "materialization_class": materialization_class,
        "source_contract_kind": contract_row.get("source_contract_kind"),
        "output_csab_name": output_csab_name,
        "output_track_resource_path": output_track_resource_path,
        "source_resolution_status": "unresolved_source_track",
        "frame_mapping_status": "not_built",
        "output_resource_path_status": "pending_duplicate_check",
        "sampling_policy": (
            "sample source CSAB skeleton-track channels at fractional source frames, then rekey onto "
            "target N64 frame slots"
        ),
        "runtime_acceptance_status": "not_runtime_accepted_plan_only",
        "required_next_step": (
            "sample and rekey source track channels, package a derived CSAB track JSON, then pose-parity "
            "verify before overlay/runtime acceptance"
        ),
        "frame_sample_count": 0,
        "frame_sample_map_sha1": "",
        "frame_sample_map": [],
    }
    issues: list[dict[str, object]] = []
    source_index = by_csab_name.get(source_csab_name)
    if source_index is None:
        row["source_resolution_status"] = "missing_source_csab_lookup"
        issues.append(
            {
                "type": "missing_source_csab_lookup",
                "n64_name": n64_name,
                "source_csab_name": source_csab_name,
            }
        )
        return row, issues
    source_index_int = int_or_none_local(source_index)
    if source_index_int is None or source_index_int < 0 or source_index_int >= len(animations):
        row["source_resolution_status"] = "invalid_source_animation_index"
        row["source_animation_index"] = source_index
        issues.append(
            {
                "type": "invalid_source_animation_index",
                "n64_name": n64_name,
                "source_csab_name": source_csab_name,
                "source_animation_index": source_index,
            }
        )
        return row, issues
    source_record = animations[source_index_int]
    if not isinstance(source_record, dict):
        row["source_resolution_status"] = "invalid_source_animation_record"
        row["source_animation_index"] = source_index_int
        issues.append(
            {
                "type": "invalid_source_animation_record",
                "n64_name": n64_name,
                "source_csab_name": source_csab_name,
                "source_animation_index": source_index_int,
            }
        )
        return row, issues
    source_resource_path = normalize_path(source_record.get("resource_path"))
    row.update(
        {
            "source_animation_index": source_index_int,
            "source_resource_path": source_resource_path,
            "source_track_validation_status": source_record.get("validation_status"),
            "source_track_count": source_record.get("track_count"),
            "source_channel_count": source_record.get("channel_count"),
            "source_keyframe_count": source_record.get("keyframe_count"),
        }
    )
    if not source_resource_path or source_resource_path not in archive_names:
        row["source_resolution_status"] = "missing_source_resource"
        issues.append(
            {
                "type": "missing_source_resource",
                "n64_name": n64_name,
                "source_csab_name": source_csab_name,
                "source_resource_path": source_resource_path,
            }
        )
        return row, issues
    source_payload = archive.read(source_resource_path)
    row["source_track_sha1"] = hashlib.sha1(source_payload).hexdigest()
    source_track = json.loads(source_payload)
    actual_source_frame_slot_count = int_or_none_local(source_track.get("frame_slot_count")) or 0
    source_frame_count_candidate = int_or_none_local(source_track.get("frame_count_candidate")) or 0
    row.update(
        {
            "source_resolution_status": "resolved_source_track",
            "source_track_format": source_track.get("format"),
            "source_track_frame_count_candidate": source_frame_count_candidate,
            "actual_source_frame_slot_count": actual_source_frame_slot_count,
            "source_track_track_count": len(source_track.get("tracks", []))
            if isinstance(source_track.get("tracks"), list)
            else None,
            "source_track_encoding_counts": source_record.get("encoding_counts"),
        }
    )
    if actual_source_frame_slot_count != expected_source_frame_slot_count:
        row["frame_mapping_status"] = "source_frame_count_mismatch"
        issues.append(
            {
                "type": "source_frame_count_mismatch",
                "n64_name": n64_name,
                "source_csab_name": source_csab_name,
                "expected_source_frame_slot_count": expected_source_frame_slot_count,
                "actual_source_frame_slot_count": actual_source_frame_slot_count,
            }
        )
        return row, issues
    frame_sample_map = semantic_materialization_frame_sample_map(
        actual_source_frame_slot_count,
        target_frame_count,
    )
    row["frame_sample_count"] = len(frame_sample_map)
    row["frame_sample_map"] = frame_sample_map
    row["frame_sample_map_sha1"] = hashlib.sha1(
        json.dumps(frame_sample_map, sort_keys=True, separators=(",", ":")).encode("utf-8")
    ).hexdigest()
    row["frame_mapping_status"] = semantic_materialization_frame_mapping_status(
        actual_source_frame_slot_count,
        target_frame_count,
        materialization_class,
    )
    return row, issues


def semantic_materialization_frame_sample_map(
    source_frame_slot_count: int,
    target_frame_count: int,
) -> list[dict[str, object]]:
    if source_frame_slot_count <= 0 or target_frame_count <= 0:
        return []
    source_last_frame = max(0, source_frame_slot_count - 1)
    target_last_frame = max(0, target_frame_count - 1)
    denominator = target_last_frame if target_last_frame else 1
    samples: list[dict[str, object]] = []
    for target_frame in range(target_frame_count):
        numerator = target_frame * source_last_frame if target_last_frame else 0
        source_floor = numerator // denominator
        remainder = numerator % denominator
        source_ceil = source_floor if remainder == 0 else min(source_last_frame, source_floor + 1)
        samples.append(
            {
                "target_frame": target_frame,
                "source_frame_numerator": numerator,
                "source_frame_denominator": denominator,
                "source_frame_float": round(numerator / denominator, 9),
                "source_frame_floor": source_floor,
                "source_frame_ceil": source_ceil,
                "source_frame_lerp": round(remainder / denominator, 9),
            }
        )
    return samples


def semantic_materialization_frame_mapping_status(
    source_frame_slot_count: int,
    target_frame_count: int,
    materialization_class: str,
) -> str:
    if source_frame_slot_count <= 0 or target_frame_count <= 0:
        return "invalid_frame_count"
    if source_frame_slot_count > target_frame_count:
        expected = "compress_source_csab_to_n64_frame_count"
    elif source_frame_slot_count < target_frame_count:
        expected = "expand_source_csab_to_n64_frame_count"
    else:
        expected = "same_frame_identity"
    if materialization_class and materialization_class != expected:
        return "frame_mapping_class_mismatch"
    return f"{expected}_frame_map_ready"


def semantic_materialization_output_csab_name(row: dict[str, object]) -> str:
    stem = semantic_materialization_stem(row.get("n64_stem") or row.get("n64_name"))
    return f"child/anim/derived_n64/{stem}.csab"


def semantic_materialization_output_track_resource_path(row: dict[str, object]) -> str:
    target_stem = semantic_materialization_stem(row.get("n64_name"))
    source_stem = semantic_materialization_stem(row.get("source_oot3d_stem") or row.get("source_csab_name"))
    return (
        "animations/oot3d/csab/skinned/derived/link_child/n64_playeranimation/"
        f"{target_stem}__from__{source_stem}.json"
    )


def semantic_materialization_stem(value: object) -> str:
    text = re.sub(r"[^A-Za-z0-9_]+", "_", str(value or "")).strip("_")
    return text or "unknown"


def semantic_materialization_plan_sample_row(row: dict[str, object]) -> dict[str, object]:
    return {key: value for key, value in row.items() if key != "frame_sample_map"}


def semantic_materialized_track_manifest_analysis(
    semantic_materialization_plan: dict[str, object],
    character_profile_o2r_path: Path | None,
    output_dir: Path | None,
    sample_limit: int,
) -> dict[str, object]:
    plan_rows = list_dicts(semantic_materialization_plan.get("rows", []))
    base: dict[str, object] = {
        "format": LINK_CHILD_ANIMATION_SEMANTIC_MATERIALIZED_TRACK_MANIFEST_FORMAT,
        "status": "not_built_missing_output_dir",
        "policy": {
            "scope": "frame-dense derived CSAB skeleton-track JSON materialized from bake-ready Link child plans",
            "sampling_policy": (
                "keyed channels are sampled with the same CSAB Hermite sampler used by the runtime diagnostics; "
                "derived keyed channels receive one key per target N64 frame"
            ),
            "tangent_policy": (
                "derived frame-dense keys use zero Hermite tangents to avoid overshoot; frame-exact pose parity "
                "is the acceptance gate before runtime overlay promotion"
            ),
            "runtime_acceptance_policy": "materialized tracks remain offline artifacts until package integration and pose parity pass",
        },
        "character_profile_o2r": str(character_profile_o2r_path) if character_profile_o2r_path is not None else None,
        "output_dir": str(output_dir) if output_dir is not None else None,
        "plan_row_count": len(plan_rows),
        "materialized_track_count": 0,
        "written_file_count": 0,
        "issue_count": 0,
        "sample_issues": [],
        "sample_rows": [],
        "rows": [],
    }
    if output_dir is None:
        return base
    if semantic_materialization_plan.get("status") != "materialization_plan_ready":
        return {
            **base,
            "status": "not_built_materialization_plan_not_ready",
            "issue_count": 1,
            "sample_issues": [
                {
                    "type": "materialization_plan_not_ready",
                    "plan_status": semantic_materialization_plan.get("status"),
                }
            ],
        }
    if character_profile_o2r_path is None or not character_profile_o2r_path.is_file():
        return {
            **base,
            "status": "missing_character_profile_o2r",
            "issue_count": 1,
            "sample_issues": [
                {
                    "type": "missing_character_profile_o2r",
                    "path": str(character_profile_o2r_path) if character_profile_o2r_path is not None else None,
                }
            ],
        }

    rows: list[dict[str, object]] = []
    issues: list[dict[str, object]] = []
    status_counts: Counter[str] = Counter()
    materialization_counts: Counter[str] = Counter()
    source_format_counts: Counter[str] = Counter()
    output_format_counts: Counter[str] = Counter()
    output_path_counts: Counter[str] = Counter()
    source_sample_parity_status_counts: Counter[str] = Counter()
    total_output_keyframe_count = 0
    total_output_channel_count = 0
    total_output_track_count = 0
    total_source_sample_parity_checked_channel_count = 0
    total_source_sample_parity_checked_key_count = 0
    total_source_sample_parity_zero_tangent_mismatch_count = 0
    total_source_sample_parity_issue_count = 0
    max_source_sample_parity_abs_value_delta = 0.0
    output_frame_dense_validation_status_counts: Counter[str] = Counter()
    output_frame_dense_validation_issue_count = 0
    output_frame_dense_validation_sampled_frame_count = 0
    output_frame_dense_validation_checked_key_count = 0
    output_frame_dense_validation_zero_tangent_mismatch_count = 0
    materialized_pose_parity_status_counts: Counter[str] = Counter()

    output_dir.mkdir(parents=True, exist_ok=True)
    try:
        with zipfile.ZipFile(character_profile_o2r_path) as archive:
            archive_names = set(archive.namelist())
            for plan_row in sorted(plan_rows, key=lambda item: int(item.get("index") or 0)):
                manifest_row, row_issues = semantic_materialized_track_manifest_row(
                    archive,
                    archive_names,
                    output_dir,
                    plan_row,
                )
                rows.append(manifest_row)
                issues.extend(row_issues)
                status_counts[str(manifest_row.get("materialization_status") or "unknown")] += 1
                materialization_counts[str(manifest_row.get("materialization_class") or "none")] += 1
                source_format_counts[str(manifest_row.get("source_track_format") or "unknown")] += 1
                output_format_counts[str(manifest_row.get("output_track_format") or "unknown")] += 1
                output_path_counts[str(manifest_row.get("output_track_resource_path") or "")] += 1
                total_output_keyframe_count += int(manifest_row.get("output_keyframe_count") or 0)
                total_output_channel_count += int(manifest_row.get("output_channel_count") or 0)
                total_output_track_count += int(manifest_row.get("output_track_count") or 0)
                source_sample_parity_status_counts[
                    str(manifest_row.get("source_sample_parity_status") or "unknown")
                ] += 1
                total_source_sample_parity_checked_channel_count += int(
                    manifest_row.get("source_sample_parity_checked_channel_count") or 0
                )
                total_source_sample_parity_checked_key_count += int(
                    manifest_row.get("source_sample_parity_checked_key_count") or 0
                )
                total_source_sample_parity_zero_tangent_mismatch_count += int(
                    manifest_row.get("source_sample_parity_zero_tangent_mismatch_count") or 0
                )
                total_source_sample_parity_issue_count += int(
                    manifest_row.get("source_sample_parity_issue_count") or 0
                )
                max_source_sample_parity_abs_value_delta = max(
                    max_source_sample_parity_abs_value_delta,
                    float(manifest_row.get("source_sample_parity_max_abs_value_delta") or 0.0),
                )
                output_frame_dense_validation_status_counts[
                    str(manifest_row.get("output_frame_dense_validation_status") or "unknown")
                ] += 1
                output_frame_dense_validation_issue_count += int(
                    manifest_row.get("output_frame_dense_validation_issue_count") or 0
                )
                output_frame_dense_validation_sampled_frame_count += int(
                    manifest_row.get("output_frame_dense_validation_sampled_frames") or 0
                )
                output_frame_dense_validation_checked_key_count += int(
                    manifest_row.get("output_frame_dense_validation_checked_key_count") or 0
                )
                output_frame_dense_validation_zero_tangent_mismatch_count += int(
                    manifest_row.get("output_frame_dense_validation_zero_tangent_mismatch_count") or 0
                )
                materialized_pose_parity_status_counts[
                    str(manifest_row.get("materialized_pose_parity_status") or "unknown")
                ] += 1
    except zipfile.BadZipFile as exc:
        return {
            **base,
            "status": "invalid_character_profile_o2r",
            "issue_count": 1,
            "sample_issues": [{"type": "invalid_character_profile_o2r", "message": str(exc)}],
        }

    duplicate_outputs = {path: count for path, count in output_path_counts.items() if path and count > 1}
    if duplicate_outputs:
        issues.extend(
            {
                "type": "duplicate_materialized_output_path",
                "output_track_resource_path": path,
                "count": count,
            }
            for path, count in sorted(duplicate_outputs.items())
        )
    status = "materialized_tracks_ready" if not issues and len(rows) == len(plan_rows) else "partial"
    return {
        **base,
        "status": status,
        "materialized_track_count": len(rows),
        "written_file_count": status_counts.get("materialized_track_written", 0),
        "output_path_duplicate_count": sum(count - 1 for count in duplicate_outputs.values()),
        "unique_output_resource_path_count": len([path for path in output_path_counts if path]),
        "total_output_track_count": total_output_track_count,
        "total_output_channel_count": total_output_channel_count,
        "total_output_keyframe_count": total_output_keyframe_count,
        "source_sample_parity_passed_count": source_sample_parity_status_counts.get("passed", 0),
        "source_sample_parity_checked_channel_count": total_source_sample_parity_checked_channel_count,
        "source_sample_parity_checked_key_count": total_source_sample_parity_checked_key_count,
        "source_sample_parity_zero_tangent_mismatch_count": (
            total_source_sample_parity_zero_tangent_mismatch_count
        ),
        "source_sample_parity_issue_count": total_source_sample_parity_issue_count,
        "source_sample_parity_max_abs_value_delta": round_metric(max_source_sample_parity_abs_value_delta),
        "output_frame_dense_validation_passed_count": output_frame_dense_validation_status_counts.get("valid", 0),
        "output_frame_dense_validation_issue_count": output_frame_dense_validation_issue_count,
        "output_frame_dense_validation_sampled_frame_count": output_frame_dense_validation_sampled_frame_count,
        "output_frame_dense_validation_checked_key_count": output_frame_dense_validation_checked_key_count,
        "output_frame_dense_validation_zero_tangent_mismatch_count": (
            output_frame_dense_validation_zero_tangent_mismatch_count
        ),
        "materialized_pose_parity_status_counts": sorted_counter(materialized_pose_parity_status_counts),
        "materialization_status_counts": sorted_counter(status_counts),
        "materialization_class_counts": sorted_counter(materialization_counts),
        "source_sample_parity_status_counts": sorted_counter(source_sample_parity_status_counts),
        "output_frame_dense_validation_status_counts": sorted_counter(
            output_frame_dense_validation_status_counts
        ),
        "source_track_format_counts": sorted_counter(source_format_counts),
        "output_track_format_counts": sorted_counter(output_format_counts),
        "issue_count": len(issues),
        "sample_issues": issues[:sample_limit],
        "sample_rows": rows[:sample_limit],
        "rows": rows,
    }


def semantic_materialization_closure_analysis(
    semantic_resolution_contract: dict[str, object],
    semantic_materialization_plan: dict[str, object],
    semantic_materialized_track_manifest: dict[str, object],
    semantic_materialized_pose_metric: dict[str, object],
    sample_limit: int,
) -> dict[str, object]:
    contract_rows = list_dicts(semantic_resolution_contract.get("rows", []))
    ready_contract_rows = [
        row
        for row in contract_rows
        if row.get("semantic_resolution_status") == "bake_contract_ready_pending_materialization"
    ]
    manifest_rows = list_dicts(semantic_materialized_track_manifest.get("rows", []))
    ready_by_name = {str(row.get("n64_name") or ""): row for row in ready_contract_rows}
    manifest_by_name = {str(row.get("n64_name") or ""): row for row in manifest_rows}
    materialized_names = {
        name
        for name, row in manifest_by_name.items()
        if row.get("materialization_status") == "materialized_track_written"
    }
    ready_names = set(ready_by_name)
    missing_names = sorted(ready_names - materialized_names)
    extra_names = sorted(set(manifest_by_name) - ready_names)
    materialized_contract_names = sorted(ready_names & materialized_names)
    status = "offline_materialization_complete"
    if semantic_materialized_track_manifest.get("status") != "materialized_tracks_ready":
        status = "manifest_not_ready"
    elif missing_names or extra_names:
        status = "offline_materialization_partial"

    materialized_rows = [manifest_by_name[name] for name in materialized_contract_names]
    materialization_counts: Counter[str] = Counter(
        str(row.get("materialization_class") or "none") for row in materialized_rows
    )
    output_status_counts: Counter[str] = Counter(
        str(row.get("materialization_status") or "unknown") for row in manifest_rows
    )
    runtime_acceptance_counts: Counter[str] = Counter(
        str(row.get("runtime_acceptance_status") or "unknown") for row in manifest_rows
    )
    source_sample_parity_counts: Counter[str] = Counter(
        str(row.get("source_sample_parity_status") or "unknown") for row in manifest_rows
    )
    pose_metric_status_counts = require_counter_dict(semantic_materialized_pose_metric.get("status_counts"))
    pose_metric_envelope_counts = require_counter_dict(
        semantic_materialized_pose_metric.get("reference_envelope_status_counts")
    )
    return {
        "format": "oot3d_link_child_animation_semantic_materialization_closure_v1",
        "status": status,
        "policy": {
            "scope": "cross-check semantic-resolution bake-ready rows against materialized derived track outputs",
            "semantic_effect": (
                "offline materialization proves the derived track files exist; it does not promote rows to "
                "runtime mappings until package integration and pose parity pass"
            ),
        },
        "ready_contract_count": len(ready_contract_rows),
        "plan_status": semantic_materialization_plan.get("status"),
        "plan_row_count": semantic_materialization_plan.get("plan_row_count", 0),
        "manifest_status": semantic_materialized_track_manifest.get("status"),
        "manifest_issue_count": semantic_materialized_track_manifest.get("issue_count", 0),
        "manifest_materialized_track_count": semantic_materialized_track_manifest.get("materialized_track_count", 0),
        "manifest_written_file_count": semantic_materialized_track_manifest.get("written_file_count", 0),
        "materialized_contract_count": len(materialized_contract_names),
        "unmaterialized_contract_count": len(missing_names),
        "extra_materialized_track_count": len(extra_names),
        "materialization_class_counts": sorted_counter(materialization_counts),
        "output_materialization_status_counts": sorted_counter(output_status_counts),
        "runtime_acceptance_status_counts": sorted_counter(runtime_acceptance_counts),
        "source_sample_parity_status_counts": sorted_counter(source_sample_parity_counts),
        "source_sample_parity_passed_count": source_sample_parity_counts.get("passed", 0),
        "source_sample_parity_checked_channel_count": semantic_materialized_track_manifest.get(
            "source_sample_parity_checked_channel_count",
            0,
        ),
        "source_sample_parity_checked_key_count": semantic_materialized_track_manifest.get(
            "source_sample_parity_checked_key_count",
            0,
        ),
        "source_sample_parity_zero_tangent_mismatch_count": semantic_materialized_track_manifest.get(
            "source_sample_parity_zero_tangent_mismatch_count",
            0,
        ),
        "source_sample_parity_issue_count": semantic_materialized_track_manifest.get(
            "source_sample_parity_issue_count",
            0,
        ),
        "source_sample_parity_max_abs_value_delta": semantic_materialized_track_manifest.get(
            "source_sample_parity_max_abs_value_delta",
            0,
        ),
        "output_frame_dense_validation_status_counts": semantic_materialized_track_manifest.get(
            "output_frame_dense_validation_status_counts",
            {},
        ),
        "output_frame_dense_validation_passed_count": semantic_materialized_track_manifest.get(
            "output_frame_dense_validation_passed_count",
            0,
        ),
        "output_frame_dense_validation_issue_count": semantic_materialized_track_manifest.get(
            "output_frame_dense_validation_issue_count",
            0,
        ),
        "output_frame_dense_validation_sampled_frame_count": semantic_materialized_track_manifest.get(
            "output_frame_dense_validation_sampled_frame_count",
            0,
        ),
        "output_frame_dense_validation_checked_key_count": semantic_materialized_track_manifest.get(
            "output_frame_dense_validation_checked_key_count",
            0,
        ),
        "output_frame_dense_validation_zero_tangent_mismatch_count": semantic_materialized_track_manifest.get(
            "output_frame_dense_validation_zero_tangent_mismatch_count",
            0,
        ),
        "materialized_pose_parity_status_counts": semantic_materialized_track_manifest.get(
            "materialized_pose_parity_status_counts",
            {},
        ),
        "materialized_pose_metric_status": semantic_materialized_pose_metric.get("status"),
        "materialized_pose_metric_row_count": semantic_materialized_pose_metric.get("row_count", 0),
        "materialized_pose_metric_measured_row_count": semantic_materialized_pose_metric.get(
            "measured_row_count",
            0,
        ),
        "materialized_pose_metric_frame_pair_count": semantic_materialized_pose_metric.get(
            "frame_pair_count",
            0,
        ),
        "materialized_pose_metric_measured_frame_pair_count": semantic_materialized_pose_metric.get(
            "measured_frame_pair_count",
            0,
        ),
        "materialized_pose_metric_issue_count": semantic_materialized_pose_metric.get("issue_count", 0),
        "materialized_pose_metric_status_counts": sorted_counter(pose_metric_status_counts),
        "materialized_pose_metric_reference_envelope_status_counts": sorted_counter(
            pose_metric_envelope_counts
        ),
        "materialized_pose_metric_normalized_extent_mean_abs_delta": semantic_materialized_pose_metric.get(
            "normalized_extent_mean_abs_delta",
            {},
        ),
        "materialized_pose_metric_normalized_extent_max_abs_delta": semantic_materialized_pose_metric.get(
            "normalized_extent_max_abs_delta",
            {},
        ),
        "materialized_pose_metric_center_delta_normalized": semantic_materialized_pose_metric.get(
            "center_delta_normalized",
            {},
        ),
        "materialized_pose_metric_scale_factor_oot3d_per_n64": semantic_materialized_pose_metric.get(
            "scale_factor_oot3d_per_n64",
            {},
        ),
        "sample_unmaterialized_contracts": [ready_by_name[name] for name in missing_names[:sample_limit]],
        "sample_extra_materialized_tracks": [manifest_by_name[name] for name in extra_names[:sample_limit]],
        "sample_materialized_contracts": [manifest_by_name[name] for name in materialized_contract_names[:sample_limit]],
        "required_next_step": "package derived tracks into the runtime profile and run pose-parity verification before runtime promotion",
    }


def semantic_materialized_pose_metric_analysis(
    reference_audit: dict[str, object],
    semantic_materialized_track_manifest: dict[str, object],
    character_profile_o2r_path: Path | None,
    sample_limit: int,
) -> dict[str, object]:
    manifest_rows = [
        row
        for row in list_dicts(semantic_materialized_track_manifest.get("rows", []))
        if row.get("materialization_status") == "materialized_track_written"
    ]
    source_files = reference_audit.get("source_files") if isinstance(reference_audit.get("source_files"), dict) else {}
    n64_base_o2r_path = optional_existing_path(source_files.get("n64_base_o2r"))
    reference_n64 = (
        reference_audit.get("n64_reference")
        if isinstance(reference_audit.get("n64_reference"), dict)
        else {}
    )
    skeleton_pose_reference = (
        reference_n64.get("skeleton_pose_reference")
        if isinstance(reference_n64.get("skeleton_pose_reference"), dict)
        else None
    )
    base: dict[str, object] = {
        "format": LINK_CHILD_ANIMATION_SEMANTIC_MATERIALIZED_POSE_METRIC_FORMAT,
        "status": "not_measured_missing_inputs",
        "policy": {
            "scope": (
                "frame-dense offline pose metric for materialized Link child N64-derived CSAB track JSONs"
            ),
            "oot3d_reference": (
                "OOT3D bind-pose skeleton world transforms composed with the materialized derived CSAB track"
            ),
            "n64_reference": "decoded N64 PlayerAnimation skeleton joint bounds for the same target frame index",
            "metric_kind": "skeleton_bounds_not_skinned_vertex_bounds",
            "reference_envelope_policy": (
                "candidate reference envelope is reported as a diagnostic only because this metric is not "
                "runtime skinned-mesh parity"
            ),
            "runtime_acceptance_policy": (
                "does not promote materialized tracks; runtime/package draw parity remains a separate gate"
            ),
        },
        "character_profile_o2r": str(character_profile_o2r_path) if character_profile_o2r_path is not None else None,
        "n64_base_o2r": str(n64_base_o2r_path) if n64_base_o2r_path is not None else source_files.get("n64_base_o2r"),
        "manifest_status": semantic_materialized_track_manifest.get("status"),
        "manifest_materialized_track_count": len(manifest_rows),
        "row_count": 0,
        "measured_row_count": 0,
        "frame_pair_count": 0,
        "measured_frame_pair_count": 0,
        "issue_count": 0,
        "sample_issues": [],
        "sample_rows": [],
        "rows": [],
    }
    if semantic_materialized_track_manifest.get("status") != "materialized_tracks_ready":
        return {
            **base,
            "status": "not_measured_materialized_manifest_not_ready",
            "issue_count": 1,
            "sample_issues": [
                {
                    "type": "materialized_manifest_not_ready",
                    "manifest_status": semantic_materialized_track_manifest.get("status"),
                }
            ],
        }
    if character_profile_o2r_path is None or not character_profile_o2r_path.is_file():
        return {
            **base,
            "status": "not_measured_missing_character_profile_o2r",
            "issue_count": 1,
            "sample_issues": [
                {
                    "type": "missing_character_profile_o2r",
                    "path": str(character_profile_o2r_path) if character_profile_o2r_path is not None else None,
                }
            ],
        }
    if n64_base_o2r_path is None:
        return {
            **base,
            "status": "not_measured_missing_n64_base_o2r",
            "issue_count": 1,
            "sample_issues": [{"type": "missing_n64_base_o2r", "path": source_files.get("n64_base_o2r")}],
        }
    if skeleton_pose_reference is None:
        return {
            **base,
            "status": "not_measured_missing_n64_skeleton_pose_reference",
            "issue_count": 1,
            "sample_issues": [{"type": "missing_n64_skeleton_pose_reference"}],
        }

    reference_envelope = reference_pose_metric_envelope(reference_audit.get("pose_error_metric"))
    rows: list[dict[str, object]] = []
    issues: list[dict[str, object]] = []
    status_counts: Counter[str] = Counter()
    envelope_counts: Counter[str] = Counter()
    materialization_counts: Counter[str] = Counter()
    mean_deltas: list[float] = []
    max_deltas: list[float] = []
    center_deltas: list[float] = []
    scale_factors: list[float] = []
    measured_frame_pair_count = 0
    frame_pair_count = 0
    try:
        with zipfile.ZipFile(character_profile_o2r_path) as character_archive:
            bind_pose_path = materialized_pose_metric_bind_pose_path(character_archive)
            if bind_pose_path is None:
                return {
                    **base,
                    "status": "not_measured_missing_bind_pose_resource",
                    "issue_count": 1,
                    "sample_issues": [{"type": "missing_bind_pose_resource"}],
                }
            bind_pose_json = json.loads(character_archive.read(bind_pose_path))
            skeleton_bones = bind_pose_skeleton_bones(bind_pose_json)
            with zipfile.ZipFile(n64_base_o2r_path) as n64_archive:
                for manifest_row in sorted(manifest_rows, key=lambda item: int(item.get("index") or 0)):
                    row, row_metrics, row_issues = semantic_materialized_pose_metric_row(
                        n64_archive,
                        skeleton_bones,
                        skeleton_pose_reference,
                        reference_envelope,
                        manifest_row,
                    )
                    rows.append(row)
                    issues.extend(row_issues)
                    status_counts[str(row.get("pose_metric_status") or "unknown")] += 1
                    envelope_counts[str(row.get("reference_envelope_status") or "unknown")] += 1
                    materialization_counts[str(row.get("materialization_class") or "none")] += 1
                    frame_pair_count += int(row.get("frame_pair_count") or 0)
                    measured_frame_pair_count += int(row.get("measured_frame_pair_count") or 0)
                    for metric in row_metrics:
                        if not isinstance(metric, dict):
                            continue
                        for key, target in (
                            ("normalized_extent_mean_abs_delta", mean_deltas),
                            ("normalized_extent_max_abs_delta", max_deltas),
                            ("center_delta_normalized", center_deltas),
                            ("scale_factor_oot3d_per_n64", scale_factors),
                        ):
                            value = metric.get(key)
                            if isinstance(value, (int, float)):
                                target.append(float(value))
    except zipfile.BadZipFile as exc:
        return {
            **base,
            "status": "not_measured_invalid_o2r",
            "issue_count": 1,
            "sample_issues": [{"type": "invalid_o2r", "message": str(exc)}],
        }

    status = "measured" if rows and not issues else ("partial" if rows else "not_measured")
    return {
        **base,
        "status": status,
        "bind_pose_resource_path": bind_pose_path,
        "skeleton_bone_count": len(skeleton_bones),
        "n64_skeleton_limb_count": len(
            skeleton_pose_reference.get("limbs", [])
            if isinstance(skeleton_pose_reference.get("limbs"), list)
            else []
        ),
        "row_count": len(rows),
        "measured_row_count": status_counts.get("measured", 0),
        "frame_pair_count": frame_pair_count,
        "measured_frame_pair_count": measured_frame_pair_count,
        "status_counts": sorted_counter(status_counts),
        "reference_envelope_status_counts": sorted_counter(envelope_counts),
        "materialization_class_counts": sorted_counter(materialization_counts),
        "normalized_extent_mean_abs_delta": numeric_summary(mean_deltas),
        "normalized_extent_max_abs_delta": numeric_summary(max_deltas),
        "center_delta_normalized": numeric_summary(center_deltas),
        "scale_factor_oot3d_per_n64": numeric_summary(scale_factors),
        "issue_count": len(issues),
        "sample_issues": issues[:sample_limit],
        "sample_rows": rows[:sample_limit],
        "rows": rows,
    }


def materialized_pose_metric_bind_pose_path(archive: zipfile.ZipFile) -> str | None:
    names = set(archive.namelist())
    profile_name = "characters/oot3d/link_child/character_runtime_profile.json"
    if profile_name in names:
        profile = json.loads(archive.read(profile_name))
        for group_key in ("native_resources", "resources"):
            group = profile.get(group_key) if isinstance(profile, dict) else None
            if not isinstance(group, dict):
                continue
            bind_pose = group.get("bind_pose")
            if not isinstance(bind_pose, dict):
                continue
            resource_path = normalize_path(bind_pose.get("resource_path"))
            if resource_path in names:
                return resource_path
    candidates = sorted(
        name
        for name in names
        if name.startswith("objects/oot3d/skinned_bind_pose/exports/")
        and name.endswith(".json")
    )
    return candidates[0] if len(candidates) == 1 else None


def semantic_materialized_pose_metric_row(
    n64_archive: zipfile.ZipFile,
    skeleton_bones: object,
    skeleton_pose_reference: dict[str, object],
    reference_envelope: dict[str, object],
    manifest_row: dict[str, object],
) -> tuple[dict[str, object], list[dict[str, object]], list[dict[str, object]]]:
    target_frame_count = int_or_none_local(manifest_row.get("target_n64_frame_count")) or 0
    output_track_file = Path(str(manifest_row.get("output_track_file") or ""))
    base: dict[str, object] = {
        "index": manifest_row.get("index"),
        "n64_name": manifest_row.get("n64_name"),
        "n64_stem": manifest_row.get("n64_stem"),
        "n64_data_name": manifest_row.get("n64_data_name"),
        "source_csab_name": manifest_row.get("source_csab_name"),
        "output_csab_name": manifest_row.get("output_csab_name"),
        "output_track_resource_path": manifest_row.get("output_track_resource_path"),
        "output_track_file": str(output_track_file),
        "materialization_class": manifest_row.get("materialization_class"),
        "target_n64_frame_count": target_frame_count,
    }
    if target_frame_count <= 0:
        return semantic_materialized_pose_empty_row(base, "invalid_target_frame_count"), [], [
            {**base, "type": "invalid_target_frame_count"}
        ]
    if not output_track_file.is_file():
        return semantic_materialized_pose_empty_row(base, "missing_materialized_track_file"), [], [
            {**base, "type": "missing_materialized_track_file"}
        ]

    raw_payload = read_player_animation_raw_payload(
        n64_archive,
        {
            "data_name": manifest_row.get("n64_data_name"),
            "name": manifest_row.get("n64_name"),
            "frame_count": target_frame_count,
        },
    )
    if raw_payload is None:
        return semantic_materialized_pose_empty_row(base, "missing_n64_payload"), [], [
            {**base, "type": "missing_n64_payload"}
        ]

    materialized_track = load_json(output_track_file)
    tracks = materialized_track.get("tracks")
    if not isinstance(tracks, list):
        return semantic_materialized_pose_empty_row(base, "materialized_track_missing_tracks"), [], [
            {**base, "type": "materialized_track_missing_tracks"}
        ]

    pair_metrics: list[dict[str, object]] = []
    issues: list[dict[str, object]] = []
    for frame in range(target_frame_count):
        try:
            oot3d_world = selected_draw_world_transforms(skeleton_bones, tracks, frame)  # type: ignore[arg-type]
        except ParseError as exc:
            issues.append({**base, "type": "oot3d_world_transform_error", "frame": frame, "message": str(exc)})
            continue
        oot3d_points = [point for matrix in oot3d_world if (point := matrix4_translation_point(matrix)) is not None]
        oot3d_bounds = bounds_from_points(oot3d_points)
        n64_frame = n64_frame_world_bounds(raw_payload, frame, skeleton_pose_reference)
        if oot3d_bounds is None or n64_frame is None:
            issues.append({**base, "type": "missing_pose_bounds", "frame": frame})
            continue
        metric = pose_bounds_metric(oot3d_bounds, n64_frame["bounds"])
        if metric is None:
            issues.append({**base, "type": "invalid_pose_bounds_metric", "frame": frame})
            continue
        pair_metrics.append(
            {
                "frame": frame,
                "normalized_extent_mean_abs_delta": metric["normalized_extent_mean_abs_delta"],
                "normalized_extent_max_abs_delta": metric["normalized_extent_max_abs_delta"],
                "center_delta_normalized": metric["center_delta_normalized"],
                "scale_factor_oot3d_per_n64": metric["scale_factor_oot3d_per_n64"],
                "oot3d_diagonal": metric["oot3d_diagonal"],
                "n64_diagonal": metric["n64_diagonal"],
                "oot3d_point_count": len(oot3d_points),
                "n64_point_count": n64_frame.get("point_count"),
            }
        )

    mean_summary = numeric_summary([float(metric["normalized_extent_mean_abs_delta"]) for metric in pair_metrics])
    max_summary = numeric_summary([float(metric["normalized_extent_max_abs_delta"]) for metric in pair_metrics])
    center_summary = numeric_summary([float(metric["center_delta_normalized"]) for metric in pair_metrics])
    scale_summary = numeric_summary([float(metric["scale_factor_oot3d_per_n64"]) for metric in pair_metrics])
    envelope_status = candidate_reference_envelope_status(mean_summary, max_summary, center_summary, reference_envelope)
    status = "measured" if pair_metrics and not issues else ("incomplete" if pair_metrics else "not_measured")
    return {
        **base,
        "pose_metric_status": status,
        "frame_pair_count": target_frame_count,
        "measured_frame_pair_count": len(pair_metrics),
        "issue_count": len(issues),
        "reference_envelope_status": envelope_status,
        "normalized_extent_mean_abs_delta_avg": mean_summary.get("avg"),
        "normalized_extent_mean_abs_delta_max": mean_summary.get("max"),
        "normalized_extent_max_abs_delta_avg": max_summary.get("avg"),
        "normalized_extent_max_abs_delta_max": max_summary.get("max"),
        "center_delta_normalized_avg": center_summary.get("avg"),
        "center_delta_normalized_max": center_summary.get("max"),
        "scale_factor_oot3d_per_n64_avg": scale_summary.get("avg"),
        "scale_factor_oot3d_per_n64_min": scale_summary.get("min"),
        "scale_factor_oot3d_per_n64_max": scale_summary.get("max"),
        "runtime_acceptance_status": "diagnostic_not_runtime_accepted",
        "required_next_step": "package derived track and run runtime/skinned pose parity before promotion",
    }, pair_metrics, issues


def semantic_materialized_pose_empty_row(base: dict[str, object], status: str) -> dict[str, object]:
    return {
        **base,
        "pose_metric_status": status,
        "frame_pair_count": 0,
        "measured_frame_pair_count": 0,
        "issue_count": 1,
        "reference_envelope_status": "not_measured",
        "normalized_extent_mean_abs_delta_avg": None,
        "normalized_extent_mean_abs_delta_max": None,
        "normalized_extent_max_abs_delta_avg": None,
        "normalized_extent_max_abs_delta_max": None,
        "center_delta_normalized_avg": None,
        "center_delta_normalized_max": None,
        "scale_factor_oot3d_per_n64_avg": None,
        "scale_factor_oot3d_per_n64_min": None,
        "scale_factor_oot3d_per_n64_max": None,
        "runtime_acceptance_status": "diagnostic_not_runtime_accepted",
        "required_next_step": "fix offline pose metric inputs before package/runtime parity",
    }


def matrix4_translation_point(matrix: object) -> dict[str, float] | None:
    if not isinstance(matrix, (list, tuple)) or len(matrix) != 4:
        return None
    rows = list(matrix)
    if any(not isinstance(row, (list, tuple)) or len(row) != 4 for row in rows):
        return None
    try:
        point = {
            "x": float(rows[0][3]),
            "y": float(rows[1][3]),
            "z": float(rows[2][3]),
        }
    except (TypeError, ValueError):
        return None
    if not all(math.isfinite(value) for value in point.values()):
        return None
    return {axis: round_metric(value) for axis, value in point.items()}  # type: ignore[return-value]


def semantic_materialized_track_manifest_row(
    archive: zipfile.ZipFile,
    archive_names: set[str],
    output_dir: Path,
    plan_row: dict[str, object],
) -> tuple[dict[str, object], list[dict[str, object]]]:
    n64_name = str(plan_row.get("n64_name") or "")
    source_resource_path = normalize_path(plan_row.get("source_resource_path"))
    output_resource_path = normalize_path(plan_row.get("output_track_resource_path"))
    row: dict[str, object] = {
        "index": plan_row.get("index"),
        "n64_name": n64_name,
        "n64_stem": plan_row.get("n64_stem"),
        "n64_data_name": plan_row.get("n64_data_name"),
        "source_csab_name": plan_row.get("source_csab_name"),
        "source_resource_path": source_resource_path,
        "source_track_sha1": plan_row.get("source_track_sha1"),
        "output_csab_name": plan_row.get("output_csab_name"),
        "output_track_resource_path": output_resource_path,
        "output_track_file": str(output_dir / output_resource_path),
        "materialization_class": plan_row.get("materialization_class"),
        "source_frame_slot_count": plan_row.get("source_frame_slot_count"),
        "target_n64_frame_count": plan_row.get("target_n64_frame_count"),
        "frame_sample_count": plan_row.get("frame_sample_count"),
        "frame_sample_map_sha1": plan_row.get("frame_sample_map_sha1"),
        "materialization_status": "not_written",
        "runtime_acceptance_status": "not_runtime_accepted_materialized_offline",
    }
    issues: list[dict[str, object]] = []
    if not source_resource_path or source_resource_path not in archive_names:
        row["materialization_status"] = "missing_source_resource"
        issues.append(
            {
                "type": "missing_source_resource_for_materialized_track",
                "n64_name": n64_name,
                "source_resource_path": source_resource_path,
            }
        )
        return row, issues
    if not output_resource_path:
        row["materialization_status"] = "missing_output_resource_path"
        issues.append({"type": "missing_output_resource_path", "n64_name": n64_name})
        return row, issues

    source_payload = archive.read(source_resource_path)
    source_sha1 = hashlib.sha1(source_payload).hexdigest()
    if plan_row.get("source_track_sha1") and source_sha1 != plan_row.get("source_track_sha1"):
        row["materialization_status"] = "source_track_sha1_mismatch"
        issues.append(
            {
                "type": "source_track_sha1_mismatch",
                "n64_name": n64_name,
                "source_resource_path": source_resource_path,
                "expected_sha1": plan_row.get("source_track_sha1"),
                "actual_sha1": source_sha1,
            }
        )
        return row, issues

    source_track = json.loads(source_payload)
    materialized_track, track_issues = materialized_csab_track_from_plan(source_track, plan_row)
    if track_issues:
        row["materialization_status"] = "materialized_track_issue"
        issues.extend(track_issues)
        return row, issues

    source_sample_parity, parity_issues = materialized_csab_source_sample_parity(
        source_track,
        materialized_track,
        plan_row,
    )
    materialized_track["derivation"]["source_sample_parity"] = source_sample_parity
    row.update(
        {
            "source_sample_parity_status": source_sample_parity.get("status"),
            "source_sample_parity_checked_channel_count": source_sample_parity.get("checked_channel_count", 0),
            "source_sample_parity_checked_key_count": source_sample_parity.get("checked_key_count", 0),
            "source_sample_parity_zero_tangent_mismatch_count": source_sample_parity.get(
                "zero_tangent_mismatch_count",
                0,
            ),
            "source_sample_parity_issue_count": source_sample_parity.get("issue_count", 0),
            "source_sample_parity_max_abs_value_delta": source_sample_parity.get("max_abs_value_delta", 0.0),
        }
    )
    if parity_issues:
        row["materialization_status"] = "source_sample_parity_issue"
        issues.extend(parity_issues)
        return row, issues

    output_file = output_dir / output_resource_path
    output_file.parent.mkdir(parents=True, exist_ok=True)
    output_text = json.dumps(materialized_track, indent=2) + "\n"
    output_file.write_text(output_text, encoding="utf-8", newline="\n")
    output_sha1 = hashlib.sha1(output_text.encode("utf-8")).hexdigest()
    counts = materialized_track.get("counts") if isinstance(materialized_track.get("counts"), dict) else {}
    validation = materialized_track.get("validation") if isinstance(materialized_track.get("validation"), dict) else {}
    dense_sample = (
        validation.get("materialized_frame_dense_channel_sample")
        if isinstance(validation.get("materialized_frame_dense_channel_sample"), dict)
        else {}
    )
    row.update(
        {
            "materialization_status": "materialized_track_written",
            "source_track_format": source_track.get("format"),
            "output_track_format": materialized_track.get("format"),
            "output_track_sha1": output_sha1,
            "output_frame_count_candidate": materialized_track.get("frame_count_candidate"),
            "output_frame_slot_count": materialized_track.get("frame_slot_count"),
            "output_track_count": counts.get("track_count"),
            "output_channel_count": counts.get("channel_count"),
            "output_const_channel_count": counts.get("const_channel_count"),
            "output_keyed_channel_count": counts.get("keyed_channel_count"),
            "output_keyframe_count": counts.get("keyframe_count"),
            "output_encoding_counts": counts.get("encoding_counts"),
            "output_frame_dense_validation_status": dense_sample.get("status"),
            "output_frame_dense_validation_sampled_frames": dense_sample.get("sampled_pose_frames"),
            "output_frame_dense_validation_checked_key_count": dense_sample.get("checked_key_count"),
            "output_frame_dense_validation_issue_count": dense_sample.get("issue_count"),
            "output_frame_dense_validation_zero_tangent_mismatch_count": dense_sample.get(
                "zero_tangent_mismatch_count",
            ),
            "source_full_pose_sample_status": validation.get("source_full_pose_sample_status"),
            "source_full_pose_sample_frames": validation.get("source_full_pose_sample_frames"),
            "materialized_pose_parity_status": validation.get("materialized_pose_parity_status"),
            "required_next_step": "package derived track into O2R overlay and run pose-parity verification",
        }
    )
    return row, issues


def materialized_csab_track_from_plan(
    source_track: dict[str, object],
    plan_row: dict[str, object],
) -> tuple[dict[str, object], list[dict[str, object]]]:
    issues: list[dict[str, object]] = []
    if source_track.get("format") != "oot3d_csab_skeleton_track_export_v1":
        return {}, [
            {
                "type": "unexpected_source_track_format",
                "n64_name": plan_row.get("n64_name"),
                "source_track_format": source_track.get("format"),
            }
        ]
    frame_sample_map = list_dicts(plan_row.get("frame_sample_map", []))
    target_frame_count = int_or_none_local(plan_row.get("target_n64_frame_count")) or 0
    if len(frame_sample_map) != target_frame_count or target_frame_count <= 0:
        return {}, [
            {
                "type": "invalid_frame_sample_map",
                "n64_name": plan_row.get("n64_name"),
                "target_frame_count": target_frame_count,
                "frame_sample_count": len(frame_sample_map),
            }
        ]

    materialized_track = copy.deepcopy(source_track)
    materialized_track.update(
        {
            "csab_name": plan_row.get("output_csab_name"),
            "frame_count_candidate": target_frame_count - 1,
            "frame_slot_count": target_frame_count,
            "source_csab_name": plan_row.get("source_csab_name"),
            "source_track_resource_path": plan_row.get("source_resource_path"),
            "source_track_sha1": plan_row.get("source_track_sha1"),
            "derivation": {
                "kind": "n64_playeranimation_semantic_bake_materialization",
                "n64_name": plan_row.get("n64_name"),
                "n64_data_name": plan_row.get("n64_data_name"),
                "materialization_class": plan_row.get("materialization_class"),
                "source_frame_slot_count": plan_row.get("source_frame_slot_count"),
                "target_n64_frame_count": target_frame_count,
                "frame_sample_map_sha1": plan_row.get("frame_sample_map_sha1"),
                "tangent_policy": "zero_tangents_on_frame_dense_resampled_keys",
            },
        }
    )
    tracks = materialized_track.get("tracks")
    if not isinstance(tracks, list):
        return {}, [{"type": "source_track_missing_tracks", "n64_name": plan_row.get("n64_name")}]
    materialized_tracks = []
    for track in tracks:
        if not isinstance(track, dict):
            issues.append({"type": "invalid_source_track_record", "n64_name": plan_row.get("n64_name")})
            continue
        channels = track.get("channels")
        if not isinstance(channels, list):
            issues.append(
                {
                    "type": "source_track_missing_channels",
                    "n64_name": plan_row.get("n64_name"),
                    "bone_index": track.get("bone_index"),
                }
            )
            continue
        materialized_channels: list[dict[str, object]] = []
        for channel in channels:
            if not isinstance(channel, dict):
                issues.append({"type": "invalid_source_channel", "n64_name": plan_row.get("n64_name")})
                continue
            materialized_channel, channel_issues = materialized_csab_channel_from_plan(channel, frame_sample_map, plan_row)
            issues.extend(channel_issues)
            if not channel_issues:
                materialized_channels.append(materialized_channel)
        materialized_track_record = copy.deepcopy(track)
        materialized_track_record["channels"] = materialized_channels
        materialized_tracks.append(materialized_track_record)
    if issues:
        return {}, issues
    materialized_track["tracks"] = materialized_tracks
    materialized_track["counts"] = materialized_csab_track_counts(materialized_tracks)
    materialized_track["validation"] = materialized_csab_track_validation(
        source_track,
        materialized_tracks,
        target_frame_count,
    )
    return materialized_track, []


def materialized_csab_track_validation(
    source_track: dict[str, object],
    materialized_tracks: list[dict[str, object]],
    target_frame_count: int,
) -> dict[str, object]:
    source_validation = copy.deepcopy(source_track.get("validation") if isinstance(source_track.get("validation"), dict) else {})
    source_full_pose_sample = (
        source_validation.get("full_pose_sample") if isinstance(source_validation.get("full_pose_sample"), dict) else {}
    )
    dense_sample = materialized_frame_dense_channel_sample(materialized_tracks, target_frame_count)
    validation = {
        **source_validation,
        "source_full_pose_sample_scope": (
            "source CSAB validation before N64-frame materialization; not proof of derived-track pose parity"
        ),
        "source_full_pose_sample_status": source_full_pose_sample.get("status"),
        "source_full_pose_sample_frames": source_full_pose_sample.get("sampled_pose_frames", 0),
        "materialized_frame_dense_channel_sample": dense_sample,
        "materialized_pose_parity_status": "pending_runtime_package_pose_parity",
    }
    return validation


def materialized_frame_dense_channel_sample(
    materialized_tracks: list[dict[str, object]],
    target_frame_count: int,
) -> dict[str, object]:
    issues: list[dict[str, object]] = []
    frame_indices = list(range(target_frame_count)) if target_frame_count > 0 else []
    checked_channel_count = 0
    checked_key_count = 0
    constant_channel_count = 0
    finite_value_count = 0
    non_finite_value_count = 0
    zero_tangent_mismatch_count = 0
    slot_sample_counts: Counter[str] = Counter()

    for track in materialized_tracks:
        bone_index = track.get("bone_index")
        for channel in list_dicts(track.get("channels", [])):
            encoding = normalize_path(channel.get("encoding"))
            slot = str(channel.get("slot"))
            if encoding in {"constant_f32", "constant_s16_rotation"}:
                constant_channel_count += 1
                value = float(channel.get("value", math.nan))
                if math.isfinite(value):
                    finite_value_count += target_frame_count
                    slot_sample_counts[slot] += target_frame_count
                else:
                    non_finite_value_count += target_frame_count
                    issues.append(
                        {
                            "type": "materialized_frame_dense_non_finite_constant_value",
                            "bone_index": bone_index,
                            "slot": channel.get("slot"),
                            "encoding": encoding,
                        }
                    )
                continue

            if encoding not in {"keyed_f32_hermite", "keyed_s16_rotation_hermite"}:
                issues.append(
                    {
                        "type": "materialized_frame_dense_unsupported_encoding",
                        "bone_index": bone_index,
                        "slot": channel.get("slot"),
                        "encoding": encoding,
                    }
                )
                continue

            checked_channel_count += 1
            keys = list_dicts(channel.get("keys", []))
            if len(keys) != target_frame_count:
                issues.append(
                    {
                        "type": "materialized_frame_dense_key_count_mismatch",
                        "bone_index": bone_index,
                        "slot": channel.get("slot"),
                        "expected_key_count": target_frame_count,
                        "actual_key_count": len(keys),
                    }
                )

            seen_frames: set[int] = set()
            for key in keys:
                frame = int_or_none_local(key.get("frame"))
                if frame is None:
                    issues.append(
                        {
                            "type": "materialized_frame_dense_key_missing_frame",
                            "bone_index": bone_index,
                            "slot": channel.get("slot"),
                        }
                    )
                    continue
                seen_frames.add(frame)
                if frame < 0 or frame >= target_frame_count:
                    issues.append(
                        {
                            "type": "materialized_frame_dense_key_frame_out_of_range",
                            "bone_index": bone_index,
                            "slot": channel.get("slot"),
                            "frame": frame,
                            "target_frame_count": target_frame_count,
                        }
                    )
                if float(key.get("left_tangent", 0.0) or 0.0) != 0.0 or float(
                    key.get("right_tangent", 0.0) or 0.0
                ) != 0.0:
                    zero_tangent_mismatch_count += 1
                    issues.append(
                        {
                            "type": "materialized_frame_dense_nonzero_tangent",
                            "bone_index": bone_index,
                            "slot": channel.get("slot"),
                            "frame": frame,
                        }
                    )
                value = float(key.get("value", math.nan))
                checked_key_count += 1
                if math.isfinite(value):
                    finite_value_count += 1
                    slot_sample_counts[slot] += 1
                else:
                    non_finite_value_count += 1
                    issues.append(
                        {
                            "type": "materialized_frame_dense_non_finite_key_value",
                            "bone_index": bone_index,
                            "slot": channel.get("slot"),
                            "frame": frame,
                        }
                    )
            missing_frames = sorted(set(frame_indices) - seen_frames)
            if missing_frames:
                issues.append(
                    {
                        "type": "materialized_frame_dense_missing_key_frames",
                        "bone_index": bone_index,
                        "slot": channel.get("slot"),
                        "missing_frame_count": len(missing_frames),
                        "sample_missing_frames": missing_frames[:10],
                    }
                )

    status = "valid" if target_frame_count > 0 and not issues else "invalid"
    return {
        "format": "oot3d_link_child_materialized_frame_dense_channel_sample_v1",
        "status": status,
        "sampled_pose_frame_indices": frame_indices,
        "sampled_pose_frames": target_frame_count,
        "checked_channel_count": checked_channel_count,
        "checked_key_count": checked_key_count,
        "constant_channel_count": constant_channel_count,
        "finite_value_count": finite_value_count,
        "non_finite_value_count": non_finite_value_count,
        "zero_tangent_mismatch_count": zero_tangent_mismatch_count,
        "slot_sample_counts": sorted_counter(slot_sample_counts),
        "issue_count": len(issues),
        "sample_issues": issues[:10],
    }


def materialized_csab_source_sample_parity(
    source_track: dict[str, object],
    materialized_track: dict[str, object],
    plan_row: dict[str, object],
) -> tuple[dict[str, object], list[dict[str, object]]]:
    frame_sample_map = list_dicts(plan_row.get("frame_sample_map", []))
    source_tracks = list_dicts(source_track.get("tracks", []))
    materialized_tracks = list_dicts(materialized_track.get("tracks", []))
    value_tolerance = 0.000001
    issues: list[dict[str, object]] = []
    checked_channel_count = 0
    checked_key_count = 0
    constant_channel_count = 0
    zero_tangent_mismatch_count = 0
    max_abs_value_delta = 0.0

    if len(source_tracks) != len(materialized_tracks):
        issues.append(
            {
                "type": "source_sample_parity_track_count_mismatch",
                "n64_name": plan_row.get("n64_name"),
                "source_track_count": len(source_tracks),
                "materialized_track_count": len(materialized_tracks),
            }
        )

    for track_index, source_record in enumerate(source_tracks):
        materialized_record = materialized_tracks[track_index] if track_index < len(materialized_tracks) else {}
        if source_record.get("bone_index") != materialized_record.get("bone_index"):
            issues.append(
                {
                    "type": "source_sample_parity_bone_index_mismatch",
                    "n64_name": plan_row.get("n64_name"),
                    "track_index": track_index,
                    "source_bone_index": source_record.get("bone_index"),
                    "materialized_bone_index": materialized_record.get("bone_index"),
                }
            )
            continue
        source_channels = list_dicts(source_record.get("channels", []))
        materialized_channels = list_dicts(materialized_record.get("channels", []))
        if len(source_channels) != len(materialized_channels):
            issues.append(
                {
                    "type": "source_sample_parity_channel_count_mismatch",
                    "n64_name": plan_row.get("n64_name"),
                    "bone_index": source_record.get("bone_index"),
                    "source_channel_count": len(source_channels),
                    "materialized_channel_count": len(materialized_channels),
                }
            )

        for channel_index, source_channel in enumerate(source_channels):
            materialized_channel = (
                materialized_channels[channel_index] if channel_index < len(materialized_channels) else {}
            )
            encoding = normalize_path(source_channel.get("encoding"))
            if encoding in {"constant_f32", "constant_s16_rotation"}:
                constant_channel_count += 1
                continue
            if encoding not in {"keyed_f32_hermite", "keyed_s16_rotation_hermite"}:
                issues.append(
                    {
                        "type": "source_sample_parity_unsupported_encoding",
                        "n64_name": plan_row.get("n64_name"),
                        "bone_index": source_record.get("bone_index"),
                        "slot": source_channel.get("slot"),
                        "encoding": encoding,
                    }
                )
                continue
            if (
                source_channel.get("slot") != materialized_channel.get("slot")
                or encoding != normalize_path(materialized_channel.get("encoding"))
            ):
                issues.append(
                    {
                        "type": "source_sample_parity_channel_identity_mismatch",
                        "n64_name": plan_row.get("n64_name"),
                        "bone_index": source_record.get("bone_index"),
                        "channel_index": channel_index,
                        "source_slot": source_channel.get("slot"),
                        "materialized_slot": materialized_channel.get("slot"),
                        "source_encoding": encoding,
                        "materialized_encoding": materialized_channel.get("encoding"),
                    }
                )
                continue

            checked_channel_count += 1
            keys = list_dicts(materialized_channel.get("keys", []))
            if len(keys) != len(frame_sample_map):
                issues.append(
                    {
                        "type": "source_sample_parity_key_count_mismatch",
                        "n64_name": plan_row.get("n64_name"),
                        "bone_index": source_record.get("bone_index"),
                        "slot": source_channel.get("slot"),
                        "expected_key_count": len(frame_sample_map),
                        "actual_key_count": len(keys),
                    }
                )
                continue

            expected_values: list[float] = []
            for frame_map in frame_sample_map:
                source_frame = float(frame_map.get("source_frame_float") or 0.0)
                expected_value = sample_csab_track_channel(source_channel, source_frame)
                if not math.isfinite(expected_value):
                    issues.append(
                        {
                            "type": "source_sample_parity_non_finite_source_sample",
                            "n64_name": plan_row.get("n64_name"),
                            "bone_index": source_record.get("bone_index"),
                            "slot": source_channel.get("slot"),
                            "source_frame": source_frame,
                        }
                    )
                    expected_value = math.nan
                expected_values.append(expected_value)
            if encoding == "keyed_s16_rotation_hermite":
                expected_values = unwrap_sampled_angle_series(expected_values)

            for key_index, key in enumerate(keys):
                expected_frame = int(frame_sample_map[key_index].get("target_frame") or 0)
                actual_frame = int_or_none_local(key.get("frame"))
                if actual_frame != expected_frame:
                    issues.append(
                        {
                            "type": "source_sample_parity_target_frame_mismatch",
                            "n64_name": plan_row.get("n64_name"),
                            "bone_index": source_record.get("bone_index"),
                            "slot": source_channel.get("slot"),
                            "key_index": key_index,
                            "expected_frame": expected_frame,
                            "actual_frame": actual_frame,
                        }
                    )
                    continue
                if float(key.get("left_tangent", 0.0) or 0.0) != 0.0 or float(
                    key.get("right_tangent", 0.0) or 0.0
                ) != 0.0:
                    zero_tangent_mismatch_count += 1
                    issues.append(
                        {
                            "type": "source_sample_parity_nonzero_tangent",
                            "n64_name": plan_row.get("n64_name"),
                            "bone_index": source_record.get("bone_index"),
                            "slot": source_channel.get("slot"),
                            "key_index": key_index,
                        }
                    )
                expected_value = expected_values[key_index]
                actual_value = float(key.get("value", math.nan))
                if not math.isfinite(expected_value) or not math.isfinite(actual_value):
                    issues.append(
                        {
                            "type": "source_sample_parity_non_finite_value",
                            "n64_name": plan_row.get("n64_name"),
                            "bone_index": source_record.get("bone_index"),
                            "slot": source_channel.get("slot"),
                            "key_index": key_index,
                        }
                    )
                    continue
                delta = abs(actual_value - expected_value)
                max_abs_value_delta = max(max_abs_value_delta, delta)
                checked_key_count += 1
                if delta > value_tolerance:
                    issues.append(
                        {
                            "type": "source_sample_parity_value_mismatch",
                            "n64_name": plan_row.get("n64_name"),
                            "bone_index": source_record.get("bone_index"),
                            "slot": source_channel.get("slot"),
                            "key_index": key_index,
                            "source_frame": frame_sample_map[key_index].get("source_frame_float"),
                            "expected_value": expected_value,
                            "actual_value": actual_value,
                            "abs_delta": delta,
                        }
                    )

    status = "passed" if checked_key_count > 0 and not issues else "failed"
    return {
        "format": "oot3d_link_child_materialized_csab_source_sample_parity_v1",
        "status": status,
        "value_tolerance": value_tolerance,
        "checked_channel_count": checked_channel_count,
        "checked_key_count": checked_key_count,
        "constant_channel_count": constant_channel_count,
        "zero_tangent_mismatch_count": zero_tangent_mismatch_count,
        "issue_count": len(issues),
        "max_abs_value_delta": round_metric(max_abs_value_delta),
        "sample_issues": issues[:10],
    }, issues


def materialized_csab_channel_from_plan(
    channel: dict[str, object],
    frame_sample_map: list[dict[str, object]],
    plan_row: dict[str, object],
) -> tuple[dict[str, object], list[dict[str, object]]]:
    materialized_channel = copy.deepcopy(channel)
    encoding = normalize_path(channel.get("encoding"))
    if encoding in {"constant_f32", "constant_s16_rotation"}:
        return materialized_channel, []
    if encoding not in {"keyed_f32_hermite", "keyed_s16_rotation_hermite"}:
        return {}, [
            {
                "type": "unsupported_channel_encoding_for_materialization",
                "n64_name": plan_row.get("n64_name"),
                "encoding": encoding,
                "slot": channel.get("slot"),
            }
        ]

    sampled_values: list[float] = []
    for frame_map in frame_sample_map:
        source_frame = float(frame_map.get("source_frame_float") or 0.0)
        value = sample_csab_track_channel(channel, source_frame)
        if not math.isfinite(value):
            return {}, [
                {
                    "type": "non_finite_materialized_channel_sample",
                    "n64_name": plan_row.get("n64_name"),
                    "encoding": encoding,
                    "slot": channel.get("slot"),
                    "source_frame": source_frame,
                }
            ]
        sampled_values.append(value)
    if encoding == "keyed_s16_rotation_hermite":
        sampled_values = unwrap_sampled_angle_series(sampled_values)

    keys = [
        {
            "frame": int(frame_map.get("target_frame") or 0),
            "value": sampled_values[index],
            "left_tangent": 0.0,
            "right_tangent": 0.0,
        }
        for index, frame_map in enumerate(frame_sample_map)
    ]
    materialized_channel["keys"] = keys
    materialized_channel["key_count"] = len(keys)
    materialized_channel["materialization"] = {
        "source_key_count": channel.get("key_count"),
        "sampled_from_source_frames": "frame_sample_map",
        "tangent_policy": "zero_tangents_on_frame_dense_resampled_keys",
    }
    return materialized_channel, []


def unwrap_sampled_angle_series(values: list[float]) -> list[float]:
    if not values:
        return []
    unwrapped = [values[0]]
    for value in values[1:]:
        adjusted = value
        reference = unwrapped[-1]
        while adjusted - reference > math.pi:
            adjusted -= math.tau
        while adjusted - reference < -math.pi:
            adjusted += math.tau
        unwrapped.append(adjusted)
    return unwrapped


def materialized_csab_track_counts(tracks: list[dict[str, object]]) -> dict[str, object]:
    counts: Counter[str] = Counter()
    encoding_counts: Counter[str] = Counter()
    target_status_counts: Counter[str] = Counter()
    counts["track_count"] = len(tracks)
    for track in tracks:
        channels = track.get("channels")
        if not isinstance(channels, list):
            continue
        counts["channel_count"] += len(channels)
        for channel in channels:
            encoding = str(channel.get("encoding") or "unknown")
            encoding_counts[encoding] += 1
            if encoding.startswith("constant_"):
                counts["const_channel_count"] += 1
            elif encoding.startswith("keyed_"):
                counts["keyed_channel_count"] += 1
                counts["keyframe_count"] += int(channel.get("key_count") or 0)
            target_status_counts[str(channel.get("target_channel_status") or "unknown")] += 1
    return {
        "track_count": counts["track_count"],
        "channel_count": counts["channel_count"],
        "const_channel_count": counts["const_channel_count"],
        "keyed_channel_count": counts["keyed_channel_count"],
        "keyframe_count": counts["keyframe_count"],
        "encoding_counts": sorted_counter(encoding_counts),
        "target_channel_compatibility_counts": sorted_counter(target_status_counts),
    }


def semantic_resolution_anonymous_numeric_best_source(row: dict[str, object]) -> dict[str, object]:
    n64_frame_count = int(row.get("n64_frame_count") or 0)
    frame_delta = int(row.get("best_frame_delta_oot3d_minus_n64") or 0)
    source_frame_count = n64_frame_count + frame_delta if n64_frame_count else None
    review_class = str(row.get("near_pose_review_class") or "")
    return {
        **row,
        "source_csab_name": row.get("best_csab_name"),
        "source_oot3d_stem": row.get("best_oot3d_stem"),
        "source_frame_slot_count": source_frame_count,
        "frame_delta_oot3d_minus_n64": row.get("best_frame_delta_oot3d_minus_n64"),
        "duration_ratio_oot3d_per_n64": row.get("best_duration_ratio_oot3d_per_n64"),
        "pose_metric_status": (
            "measured"
            if int(row.get("measured_near_candidate_count") or 0) > 0
            else "not_measured"
        ),
        "reference_envelope_status": (
            "inside_reference_envelope"
            if int(row.get("inside_reference_envelope_near_candidate_count") or 0) > 0
            and review_class != "near_pose_best_outside_reference_envelope"
            else "outside_reference_envelope"
            if review_class == "near_pose_best_outside_reference_envelope"
            else "not_measured"
        ),
        "normalized_extent_max_abs_delta_max": row.get("best_normalized_extent_max_abs_delta_max"),
        "center_delta_normalized_max": row.get("best_center_delta_normalized_max"),
        "runtime_csab_collision_n64_names": row.get("best_runtime_csab_collision_n64_names"),
        "candidate_csab_collision_n64_names": row.get("best_candidate_csab_collision_n64_names"),
    }


def semantic_alias_route_review_class(
    promotion_class: str,
    direct_source_reference_count: int,
    direct_player_actor_source_reference_count: int,
) -> str:
    if promotion_class == "blocked_same_frame_reuses_promoted_csab":
        return "blocked_same_frame_reuses_promoted_csab"
    if promotion_class == "blocked_same_frame_reuses_candidate_csab":
        return "blocked_same_frame_reuses_candidate_csab"
    if direct_player_actor_source_reference_count > 0:
        return "overlay_ready_with_direct_player_actor_route_reference"
    if direct_source_reference_count > 0:
        return "overlay_ready_with_non_player_direct_source_reference"
    return "overlay_ready_without_direct_player_actor_route_reference"


def semantic_alias_route_required_evidence(route_review_class: str) -> str:
    if route_review_class == "overlay_ready_with_direct_player_actor_route_reference":
        return "manual route label acceptance; N64 player actor route is directly referenced"
    if route_review_class == "overlay_ready_with_non_player_direct_source_reference":
        return "manual route label acceptance; direct source reference is outside the player actor sample"
    if route_review_class == "overlay_ready_without_direct_player_actor_route_reference":
        return "stronger callsite, route-table, or capture evidence before promotion"
    if route_review_class == "blocked_same_frame_reuses_promoted_csab":
        return "prove intentional CSAB reuse or keep as fallback/bake candidate"
    if route_review_class == "blocked_same_frame_reuses_candidate_csab":
        return "choose which N64 candidate owns the shared CSAB, prove intentional reuse, or keep one side as bake/fallback"
    return "manual route/callsite review"


def semantic_alias_route_review_priority(route_review_class: str) -> int:
    return {
        "overlay_ready_with_direct_player_actor_route_reference": 0,
        "overlay_ready_with_non_player_direct_source_reference": 1,
        "overlay_ready_without_direct_player_actor_route_reference": 2,
        "blocked_same_frame_reuses_promoted_csab": 3,
        "blocked_same_frame_reuses_candidate_csab": 4,
    }.get(route_review_class, 9)


def candidate_collision_source_reference_summary(
    collision_names: str,
    source_references: dict[str, dict[str, object]],
) -> dict[str, object]:
    direct_source_reference_count = 0
    direct_player_actor_source_reference_count = 0
    sample_parts: list[str] = []
    for name in semicolon_names(collision_names):
        source_reference = source_references.get(name, source_reference_empty(name))
        direct_source_reference_count += int(source_reference.get("direct_source_reference_count") or 0)
        direct_player_actor_source_reference_count += int(
            source_reference.get("direct_player_actor_source_reference_count") or 0
        )
        sample = source_reference_sample_string(source_reference)
        if sample:
            sample_parts.append(f"{name}={sample}")
    return {
        "direct_source_reference_count": direct_source_reference_count,
        "direct_player_actor_source_reference_count": direct_player_actor_source_reference_count,
        "sample_direct_source_references": "|".join(sample_parts[:5]),
    }


def semicolon_names(value: str) -> list[str]:
    return [part for part in (item.strip() for item in value.split(";")) if part]


def source_reference_sample_string(source_reference: dict[str, object]) -> str:
    samples = source_reference.get("sample_direct_source_references")
    if not isinstance(samples, list):
        return ""
    parts: list[str] = []
    for sample in samples[:5]:
        if not isinstance(sample, dict):
            continue
        path = str(sample.get("path") or "")
        line = sample.get("line")
        parts.append(f"{path}:{line}" if line is not None else path)
    return ";".join(parts)


def neighbor_stem_string(neighbors: object) -> str:
    if not isinstance(neighbors, list):
        return ""
    stems: list[str] = []
    for neighbor in neighbors:
        if not isinstance(neighbor, dict):
            continue
        stem = neighbor.get("n64_stem") or neighbor.get("oot3d_stem")
        if stem:
            stems.append(str(stem))
    return ";".join(stems)


def oot3d_table_context(
    oot3d_records: list[dict[str, object]],
    csab_name: str,
    neighbor_limit: int = 2,
) -> dict[str, object]:
    target = normalize_path(csab_name)
    index = next(
        (
            candidate_index
            for candidate_index, record in enumerate(oot3d_records)
            if normalize_path(record.get("csab_name")) == target
        ),
        None,
    )
    if index is None:
        return {
            "status": "oot3d_csab_not_found",
            "previous_records": [],
            "next_records": [],
            "inferred_neighbor_family": "unknown",
        }
    previous_records = [
        oot3d_context_neighbor(record, candidate_index)
        for candidate_index, record in list(enumerate(oot3d_records[:index]))[-neighbor_limit:]
    ]
    next_records = [
        oot3d_context_neighbor(record, candidate_index)
        for candidate_index, record in list(enumerate(oot3d_records[index + 1:], start=index + 1))[:neighbor_limit]
    ]
    return {
        "status": "oot3d_neighbor_context_available",
        "previous_records": previous_records,
        "next_records": next_records,
        "inferred_neighbor_family": inferred_neighbor_family(previous_records, next_records),
    }


def oot3d_context_neighbor(record: dict[str, object], index: int) -> dict[str, object]:
    stem = str(record.get("stem") or "")
    return {
        "index": index,
        "oot3d_stem": stem,
        "frame_count": record.get("frame_slot_count"),
        "family": candidate_family_for_oot3d_stem(stem),
    }


def candidate_family_for_oot3d_stem(stem: str) -> str:
    lower = stem.lower()
    if lower.startswith("cl_dm_"):
        return "demo_" + lower[len("cl_dm_") :].split("_")[0]
    for prefix, family in (
        ("dm_", "demo"),
        ("ac_", "anchor"),
        ("ft_", "fighter"),
        ("bt_", "bottle"),
        ("hm_", "hammer"),
        ("mg_", "magic"),
        ("nml_", "normal"),
        ("sw_", "swimer"),
        ("d_lk_", "d_link"),
    ):
        if lower.startswith(prefix):
            if family == "demo":
                rest = lower[len(prefix) :]
                return "demo_" + (rest.split("_")[0] if rest else "unknown")
            return family
    return lower.split("_")[0] if lower else "unknown"


def semantic_alias_promotion_class(
    record: dict[str, object],
    pose_summary: dict[str, object],
    runtime_collisions: list[dict[str, object]],
    candidate_collisions: list[dict[str, object]],
) -> str:
    review_class = str(pose_summary.get("promotion_review_class") or "")
    if not pose_summary or pose_summary.get("pose_metric_status") != "measured":
        return "not_measured"
    if review_class == "same_frame_inside_reference_envelope":
        if runtime_collisions:
            return "blocked_same_frame_reuses_promoted_csab"
        if candidate_collisions:
            return "blocked_same_frame_reuses_candidate_csab"
        if int(record.get("alias_candidate_csab_count") or 0) == 1:
            return "promotion_ready_same_frame_alias_clean"
        return "blocked_same_frame_alias_not_single_candidate"
    if review_class == "small_delta_inside_reference_envelope_requires_resample_policy":
        return "requires_resample_policy"
    if review_class == "temporal_bake_required_inside_reference_envelope":
        return "requires_temporal_bake_policy"
    if review_class == "pose_metric_outside_reference_envelope":
        return "blocked_pose_metric_outside_reference_envelope"
    return "not_ready_for_semantic_alias_promotion"


def semantic_alias_required_evidence(promotion_class: str) -> str:
    if promotion_class == "promotion_ready_same_frame_alias_clean":
        return "route/callsite review can accept this as a semantic alias promotion overlay"
    if promotion_class == "blocked_same_frame_reuses_promoted_csab":
        return "prove intentional CSAB reuse or keep as fallback/bake candidate"
    if promotion_class == "blocked_same_frame_reuses_candidate_csab":
        return "choose a single N64 owner for the shared CSAB, prove intentional reuse, or keep as fallback/bake candidate"
    if promotion_class == "requires_resample_policy":
        return "define temporal resample policy and then repeat route/callsite review"
    if promotion_class == "requires_temporal_bake_policy":
        return "define bake source or larger temporal policy before promotion"
    if promotion_class == "blocked_pose_metric_outside_reference_envelope":
        return "manual pose review or stronger route proof; metric is outside current reference envelope"
    return "additional pose, route, or alias evidence"


def semantic_alias_promotion_priority(promotion_class: str) -> int:
    return {
        "promotion_ready_same_frame_alias_clean": 0,
        "blocked_same_frame_reuses_promoted_csab": 1,
        "blocked_same_frame_reuses_candidate_csab": 2,
        "requires_resample_policy": 3,
        "requires_temporal_bake_policy": 4,
        "blocked_pose_metric_outside_reference_envelope": 5,
    }.get(promotion_class, 9)


def unresolved_route_analysis_for_n64_stem(stem: str) -> str:
    lower = stem.lower()
    if lower and all(char in "0123456789abcdef" for char in lower):
        return "anonymous_numeric_symbol"
    stripped = lower
    for prefix in ("link_", "clink_", "child_", "kolink_"):
        if stripped.startswith(prefix):
            stripped = stripped[len(prefix) :]
            break
    if "ddbox_open" in stripped:
        return "demo_ddbox_open_without_csab_candidate"
    if "tbox_open" in stripped:
        return "demo_tbox_open_age_or_route_mismatch"
    if stripped.startswith("anchor_bom_"):
        return "anchor_boomerang_state_without_ac_candidate"
    if stripped.startswith("anchor_wait") and ("defense" in stripped or "pierce" in stripped):
        return "anchor_wait_defense_pierce_state_without_ac_candidate"
    if stripped.startswith("child_tunnel_"):
        return "child_tunnel_door_without_tunnel_csab_candidate"
    if stripped.startswith("fighter_defense_long"):
        return "fighter_long_defense_state_without_csab_candidate"
    if stripped.startswith("fighter_upper_"):
        return "fighter_upper_attack_state_without_csab_candidate"
    if stripped.startswith("normal_jump_climb_"):
        return "normal_jump_climb_hold_wait_suffix_without_csab_candidate"
    if stripped == "normal_wakeup":
        return "normal_wakeup_requires_down_state_or_callsite"
    return "named_without_specific_analysis"


def unresolved_resolution_for_analysis(analysis: str) -> dict[str, str]:
    if analysis == "anonymous_numeric_symbol":
        return {
            "class": "requires_identity_evidence",
            "required_evidence": "player callsite, neighboring action-state table identity, or pose match",
            "notes": "numeric PlayerAnimation symbol has no semantic name and no direct source reference",
        }
    if analysis in {
        "demo_ddbox_open_without_csab_candidate",
        "child_tunnel_door_without_tunnel_csab_candidate",
    }:
        return {
            "class": "probable_oot3d_omission_or_bake_required",
            "required_evidence": "proof that OOT3D omits the route, or an accepted baked/substituted CSAB source",
            "notes": "named N64 animation has a clear action meaning but no matching OOT3D CSAB stem candidate",
        }
    if analysis in {
        "anchor_wait_defense_pierce_state_without_ac_candidate",
        "fighter_long_defense_state_without_csab_candidate",
        "fighter_upper_attack_state_without_csab_candidate",
    }:
        return {
            "class": "requires_action_state_route_or_bake",
            "required_evidence": "player action-state callsite, combat-state route table, pose match, or baked replacement",
            "notes": "combat/anchor state name is meaningful but no promoted or alias CSAB route exists",
        }
    if analysis == "normal_wakeup_requires_down_state_or_callsite":
        return {
            "class": "requires_down_state_callsite_or_bake",
            "required_evidence": "down/wakeup action-state callsite, pose match, or baked replacement",
            "notes": "normal wakeup is likely state-driven and has no direct CSAB alias candidate",
        }
    if analysis == "demo_tbox_open_age_or_route_mismatch":
        return {
            "class": "requires_age_route_disambiguation",
            "required_evidence": "child/adult route evidence or callsite proving which treasure-box CSAB applies",
            "notes": "treasure-box opening has age-prefixed OOT3D routes that can differ between child and adult",
        }
    return {
        "class": "requires_specific_semantic_analysis",
        "required_evidence": "new semantic alias, callsite evidence, pose match, or proof of omission",
        "notes": "named N64 animation lacks a specific resolver rule",
    }


def candidate_ambiguity_analysis(
    candidates: list[dict[str, object]],
    promoted_entries_by_csab_name: dict[str, list[dict[str, object]]] | None = None,
    n64_stem: str = "",
) -> dict[str, object]:
    if len(candidates) < 2:
        return {"kind": "not_ambiguous", "candidate_count": len(candidates)}
    stems = [str(candidate.get("oot3d_stem") or "").lower() for candidate in candidates]
    non_free = [
        candidate
        for candidate in candidates
        if not str(candidate.get("oot3d_stem") or "").lower().endswith("_free")
    ]
    normalized_without_free = {
        ambiguity_normalized_stem(str(candidate.get("oot3d_stem") or ""))
        for candidate in candidates
    }
    if len(non_free) == 1 and len(normalized_without_free) == 1:
        preferred = non_free[0]
        return {
            "kind": "diagnostic_prefer_non_free_variant",
            "candidate_count": len(candidates),
            "preferred_csab_name": preferred.get("csab_name"),
            "preferred_oot3d_stem": preferred.get("oot3d_stem"),
            "required_evidence": "runtime route or pose metric confirmation before promotion",
            "reason": "all candidates collapse after _free removal and text normalization; N64 stem does not name a _free route",
        }
    promoted_entries_by_csab_name = promoted_entries_by_csab_name or {}
    promoted_collisions = []
    for candidate in candidates:
        csab_name = str(candidate.get("csab_name") or "")
        for entry in promoted_entries_by_csab_name.get(csab_name, []):
            promoted_collisions.append(
                {
                    "candidate_csab_name": csab_name,
                    "candidate_oot3d_stem": candidate.get("oot3d_stem"),
                    "promoted_n64_name": entry.get("n64_name"),
                    "promoted_n64_stem": entry.get("n64_stem"),
                    "promoted_n64_frame_count": entry.get("n64_frame_count"),
                    "promoted_oot3d_frame_slot_count": entry.get("oot3d_frame_slot_count"),
                }
            )
    if len(promoted_collisions) >= len(candidates) and len({row["candidate_csab_name"] for row in promoted_collisions}) == len(candidates):
        return {
            "kind": "semantic_ambiguity_reuses_promoted_adjacent_routes",
            "candidate_count": len(candidates),
            "candidate_oot3d_stems": stems,
            "n64_stem": n64_stem,
            "promoted_route_collisions": promoted_collisions,
            "required_evidence": "pose metric, player callsite context, or proof that OOT3D intentionally reuses one adjacent route",
            "reason": "all candidate CSAB routes are already promoted for distinct N64 animations, so this N64 row may need a bake/fallback instead of alias promotion",
        }
    return {
        "kind": "semantic_ambiguity_requires_pose_or_callsite",
        "candidate_count": len(candidates),
        "candidate_oot3d_stems": stems,
        "required_evidence": "pose metric or player callsite context",
    }


def ambiguity_normalized_stem(stem: str) -> str:
    normalized = stem.lower()
    if normalized.endswith("_free"):
        normalized = normalized[:-len("_free")]
    return normalized.replace("fighter", "ft")


def candidate_promotion_analysis(
    n64_record: dict[str, object],
    alias_candidate: dict[str, object],
) -> dict[str, object]:
    n64_frame_count = int(n64_record.get("frame_count") or 0)
    oot3d_frame_count = int(alias_candidate.get("oot3d_frame_slot_count") or 0)
    frame_delta = oot3d_frame_count - n64_frame_count
    same_frame_count = n64_frame_count == oot3d_frame_count
    duration_ratio = (
        round(float(oot3d_frame_count) / float(n64_frame_count), 6)
        if n64_frame_count > 0
        else None
    )
    temporal_severity = candidate_temporal_severity(n64_frame_count, oot3d_frame_count)
    kind = (
        "candidate_same_frame_count_requires_pose_metric"
        if same_frame_count
        else "candidate_requires_temporal_bake_or_resample"
    )
    return {
        "kind": kind,
        "family": candidate_family_for_n64_stem(str(n64_record.get("stem") or "")),
        "n64_frame_count": n64_frame_count,
        "oot3d_frame_slot_count": oot3d_frame_count,
        "frame_delta_oot3d_minus_n64": frame_delta,
        "duration_ratio_oot3d_per_n64": duration_ratio,
        "temporal_severity": temporal_severity,
        "required_evidence": (
            "pose metric and runtime route review"
            if same_frame_count
            else "temporal bake/resample policy plus pose metric and runtime route review"
        ),
    }


def candidate_temporal_severity(n64_frame_count: int, oot3d_frame_count: int) -> str:
    if n64_frame_count <= 0 or oot3d_frame_count <= 0:
        return "invalid_frame_count"
    delta = abs(oot3d_frame_count - n64_frame_count)
    ratio = max(
        float(oot3d_frame_count) / float(n64_frame_count),
        float(n64_frame_count) / float(oot3d_frame_count),
    )
    if delta == 0:
        return "same_frame_count"
    if delta <= 3 and ratio <= 1.25:
        return "small_delta"
    if delta <= 15 and ratio <= 2.0:
        return "moderate_delta"
    return "large_delta_or_ratio"


def candidate_family_for_n64_stem(stem: str) -> str:
    lower = stem.lower()
    for prefix in ("link_", "clink_", "child_", "kolink_"):
        if lower.startswith(prefix):
            lower = lower[len(prefix) :]
            break
    if lower.startswith("demo_kakeyori"):
        return "demo_kakeyori_runup"
    if lower.startswith("demo_"):
        parts = lower.split("_")
        return "demo_" + (parts[1] if len(parts) > 1 else "unknown")
    if lower.startswith("anchor_"):
        parts = lower.split("_")
        return "anchor_" + (parts[1] if len(parts) > 1 else "unknown")
    if lower.startswith("normal_jump_climb_"):
        return "normal_jump_climb"
    if lower.startswith("normal_"):
        return "normal"
    if lower.startswith("fighter_"):
        return "fighter"
    if lower.startswith("bottle_"):
        return "bottle"
    if lower.startswith("hammer_"):
        return "hammer"
    if lower.startswith("magic_"):
        return "magic"
    if lower.startswith("swimer_"):
        return "swimer"
    if lower.startswith("d_link_"):
        return "d_link"
    return lower.split("_")[0] if lower else "unknown"


def load_json(path: Path) -> dict[str, object]:
    return json.loads(path.read_text(encoding="utf-8"))


def runtime_mapping_entries(reference_audit: dict[str, object]) -> list[dict[str, object]]:
    candidate_mapping = reference_audit.get("candidate_mapping")
    if not isinstance(candidate_mapping, dict):
        return []
    runtime_mapping = candidate_mapping.get("runtime_mapping")
    if not isinstance(runtime_mapping, dict):
        return []
    return [entry for entry in runtime_mapping.get("entries", []) if isinstance(entry, dict)]


def group_entries_by_n64_name(entries: list[dict[str, object]]) -> dict[str, list[dict[str, object]]]:
    grouped: dict[str, list[dict[str, object]]] = {}
    for entry in entries:
        n64_name = str(entry.get("n64_name") or "")
        if not n64_name:
            continue
        grouped.setdefault(n64_name, []).append(entry)
    return grouped


def group_entries_by_csab_name(entries: list[dict[str, object]]) -> dict[str, list[dict[str, object]]]:
    grouped: dict[str, list[dict[str, object]]] = {}
    for entry in entries:
        csab_name = str(entry.get("csab_name") or "")
        if not csab_name:
            continue
        grouped.setdefault(csab_name, []).append(entry)
    return grouped


def group_mapping_records_by_csab_name(
    records: list[dict[str, object]],
    status: str | None = None,
) -> dict[str, list[dict[str, object]]]:
    grouped: dict[str, list[dict[str, object]]] = {}
    for record in records:
        if status is not None and record.get("status") != status:
            continue
        csab_name = str(record.get("csab_name") or "")
        if not csab_name:
            continue
        grouped.setdefault(csab_name, []).append(record)
    return grouped


def source_reference_index(
    n64_records: list[dict[str, object]],
    source_search_roots: list[Path],
) -> dict[str, dict[str, object]]:
    names = {
        str(record.get("name") or "")
        for record in n64_records
        if record.get("name")
    }
    names.discard("")
    references = {name: source_reference_empty(name) for name in names}
    if not names or not source_search_roots:
        return references

    pattern = re.compile(r"\b(" + "|".join(re.escape(name) for name in sorted(names)) + r")\b")
    for root in source_search_roots:
        if not root.exists():
            continue
        files = [root] if root.is_file() else root.rglob("*")
        for path in files:
            if not path.is_file() or path.suffix.lower() not in {".c", ".cpp", ".h", ".hpp"}:
                continue
            normalized = path.as_posix().lower()
            if normalized.endswith(".inc.c") or "player_anim_headers." in normalized:
                continue
            try:
                text = path.read_text(encoding="utf-8", errors="ignore")
            except OSError:
                continue
            for match in pattern.finditer(text):
                name = match.group(1)
                entry = references[name]
                entry["direct_source_reference_count"] = int(entry["direct_source_reference_count"]) + 1
                if source_reference_is_player_actor_path(normalized):
                    entry["direct_player_actor_source_reference_count"] = (
                        int(entry["direct_player_actor_source_reference_count"]) + 1
                    )
                samples = entry["sample_direct_source_references"]
                if isinstance(samples, list) and len(samples) < 10:
                    line = text.count("\n", 0, match.start()) + 1
                    samples.append({"path": str(path), "line": line})
    for entry in references.values():
        entry["status"] = (
            "direct_source_references_found"
            if int(entry["direct_source_reference_count"]) > 0
            else "no_direct_source_reference_in_search_roots"
        )
    return references


def source_reference_empty(name: str) -> dict[str, object]:
    return {
        "symbol": name,
        "status": "source_roots_not_searched",
        "direct_source_reference_count": 0,
        "direct_player_actor_source_reference_count": 0,
        "sample_direct_source_references": [],
        "search_policy": "C/C++ source scan excluding generated player_anim_headers and .inc.c asset definitions",
    }


def source_reference_is_player_actor_path(normalized_path: str) -> bool:
    return (
        "ovl_player_actor" in normalized_path
        and normalized_path.endswith("z_player.c")
    )


def anonymous_numeric_source_reference_summary(records: list[dict[str, object]]) -> dict[str, object]:
    return source_reference_summary_for_unmapped(records, anonymous_numeric=True)


def named_unmapped_source_reference_summary(records: list[dict[str, object]]) -> dict[str, object]:
    return source_reference_summary_for_unmapped(records, anonymous_numeric=False)


def source_reference_summary_for_unmapped(
    records: list[dict[str, object]],
    *,
    anonymous_numeric: bool,
) -> dict[str, object]:
    counter: Counter[str] = Counter()
    searched_count = 0
    direct_reference_count = 0
    for record in records:
        is_numeric = record.get("unresolved_route_analysis") == "anonymous_numeric_symbol"
        if is_numeric != anonymous_numeric:
            continue
        evidence = record.get("source_reference_evidence")
        if not isinstance(evidence, dict):
            continue
        status = str(evidence.get("status") or "unknown")
        counter[status] += 1
        if status != "source_roots_not_searched":
            searched_count += 1
        direct_reference_count += int(evidence.get("direct_source_reference_count") or 0)
    return {
        "status_counts": sorted_counter(counter),
        "searched_symbol_count": searched_count,
        "direct_source_reference_count": direct_reference_count,
    }


def anonymous_numeric_table_context(
    n64_records: list[dict[str, object]],
    record_index: int,
    neighbor_limit: int = 3,
) -> dict[str, object]:
    previous_named: list[dict[str, object]] = []
    next_named: list[dict[str, object]] = []
    for candidate in reversed(n64_records[:record_index]):
        stem = str(candidate.get("stem") or "")
        if is_numeric_stem(stem):
            continue
        previous_named.append(table_context_neighbor(candidate))
        if len(previous_named) >= neighbor_limit:
            break
    previous_named.reverse()
    for candidate in n64_records[record_index + 1:]:
        stem = str(candidate.get("stem") or "")
        if is_numeric_stem(stem):
            continue
        next_named.append(table_context_neighbor(candidate))
        if len(next_named) >= neighbor_limit:
            break
    return {
        "status": "table_neighbor_context_available",
        "previous_named": previous_named,
        "next_named": next_named,
        "inferred_neighbor_family": inferred_neighbor_family(previous_named, next_named),
        "required_evidence": "table proximity is contextual evidence only; promotion still requires pose, route, or indirect callsite evidence",
    }


def table_context_neighbor(record: dict[str, object]) -> dict[str, object]:
    return {
        "index": record.get("index"),
        "n64_stem": record.get("stem"),
        "frame_count": record.get("frame_count"),
        "family": unresolved_family_for_n64_stem(str(record.get("stem") or "")),
    }


def inferred_neighbor_family(previous_named: list[dict[str, object]], next_named: list[dict[str, object]]) -> str:
    adjacent = []
    if previous_named:
        adjacent.append(str(previous_named[-1].get("family") or ""))
    if next_named:
        adjacent.append(str(next_named[0].get("family") or ""))
    adjacent = [value for value in adjacent if value]
    if len(adjacent) == 2 and adjacent[0] == adjacent[1]:
        return adjacent[0]
    if adjacent:
        return "between_" + "_and_".join(adjacent)
    return "unknown"


def build_anonymous_numeric_pose_candidate_queue(
    records: list[dict[str, object]],
    oot3d_records: list[dict[str, object]],
    *,
    per_record_limit: int = 12,
    table_window: int = 4,
) -> list[dict[str, object]]:
    records_by_index = {int(record.get("index") or -1): record for record in records}
    oot3d_by_csab = {str(record.get("csab_name") or ""): record for record in oot3d_records}
    oot3d_index_by_csab = {
        str(record.get("csab_name") or ""): index
        for index, record in enumerate(oot3d_records)
        if record.get("csab_name")
    }
    queue: list[dict[str, object]] = []
    for record in records:
        if (
            record.get("status") != "unmapped_no_candidate_csab"
            or record.get("unresolved_route_analysis") != "anonymous_numeric_symbol"
        ):
            continue
        table_context = record.get("table_context_evidence")
        if not isinstance(table_context, dict):
            continue
        family = str(table_context.get("inferred_neighbor_family") or "")
        previous_named = list_dicts(table_context.get("previous_named"))
        next_named = list_dicts(table_context.get("next_named"))
        previous_stems = neighbor_stem_string(previous_named)
        next_stems = neighbor_stem_string(next_named)
        candidates: dict[str, dict[str, object]] = {}

        for oot3d_record in oot3d_records:
            stem = str(oot3d_record.get("stem") or "")
            if not anonymous_numeric_family_matches(family, stem):
                continue
            add_anonymous_numeric_candidate(
                candidates,
                record,
                oot3d_record,
                score=anonymous_numeric_frame_score(record, oot3d_record) + 40,
                source="neighbor_family_prefix",
                table_context_family=family,
                previous_named_stems=previous_stems,
                next_named_stems=next_stems,
                oot3d_table_distance=None,
            )

        for neighbor in [*previous_named, *next_named]:
            neighbor_record = records_by_index.get(int(neighbor.get("index") or -1))
            if not isinstance(neighbor_record, dict):
                continue
            neighbor_csab = str(neighbor_record.get("csab_name") or "")
            if not neighbor_csab:
                continue
            neighbor_index = oot3d_index_by_csab.get(neighbor_csab)
            if neighbor_index is None:
                continue
            for offset in range(-table_window, table_window + 1):
                candidate_index = neighbor_index + offset
                if candidate_index < 0 or candidate_index >= len(oot3d_records):
                    continue
                oot3d_record = oot3d_records[candidate_index]
                distance = abs(offset)
                neighbor_score, shared_tokens = near_candidate_token_score(
                    str(neighbor.get("n64_stem") or ""),
                    str(oot3d_record.get("stem") or ""),
                )
                add_anonymous_numeric_candidate(
                    candidates,
                    record,
                    oot3d_record,
                    score=100 - (distance * 5) + anonymous_numeric_frame_score(record, oot3d_record) + neighbor_score,
                    source="oot3d_table_neighbor_window",
                    table_context_family=family,
                    previous_named_stems=previous_stems,
                    next_named_stems=next_stems,
                    oot3d_table_distance=distance,
                    shared_tokens=",".join(shared_tokens),
                    nearest_neighbor_csab=neighbor_csab,
                )

        ranked = sorted(
            candidates.values(),
            key=lambda item: (
                -int(item.get("score") or 0),
                int(item.get("abs_frame_delta") or 999999),
                str(item.get("csab_name") or ""),
            ),
        )
        queue.extend(ranked[:per_record_limit])
    return queue


def add_anonymous_numeric_candidate(
    candidates: dict[str, dict[str, object]],
    record: dict[str, object],
    oot3d_record: dict[str, object],
    *,
    score: int,
    source: str,
    table_context_family: str,
    previous_named_stems: str,
    next_named_stems: str,
    oot3d_table_distance: int | None,
    shared_tokens: str = "",
    nearest_neighbor_csab: str = "",
) -> None:
    csab_name = str(oot3d_record.get("csab_name") or "")
    if not csab_name:
        return
    n64_frame_count = int(record.get("n64_frame_count") or 0)
    oot3d_frame_count = int(oot3d_record.get("frame_slot_count") or 0)
    frame_delta = oot3d_frame_count - n64_frame_count
    existing = candidates.get(csab_name)
    if existing is not None and int(existing.get("score") or 0) >= score:
        return
    candidates[csab_name] = {
        "score": score,
        "abs_frame_delta": abs(frame_delta),
        "n64_index": record.get("index"),
        "n64_name": record.get("n64_name"),
        "n64_stem": record.get("n64_stem"),
        "n64_frame_count": n64_frame_count,
        "unresolved_route_analysis": record.get("unresolved_route_analysis"),
        "csab_name": csab_name,
        "oot3d_stem": oot3d_record.get("stem"),
        "oot3d_frame_slot_count": oot3d_frame_count,
        "frame_delta_oot3d_minus_n64": frame_delta,
        "shared_tokens": shared_tokens,
        "candidate_source": source,
        "table_context_family": table_context_family,
        "previous_named_stems": previous_named_stems,
        "next_named_stems": next_named_stems,
        "nearest_neighbor_csab": nearest_neighbor_csab,
        "oot3d_table_distance": oot3d_table_distance,
        "diagnostic_only": True,
        "required_evidence": (
            "anonymous numeric table-context candidates are triage only; promotion requires "
            "pose metric, route/callsite, capture, or payload identity evidence"
        ),
    }


def anonymous_numeric_frame_score(record: dict[str, object], oot3d_record: dict[str, object]) -> int:
    n64_frame_count = int(record.get("n64_frame_count") or 0)
    oot3d_frame_count = int(oot3d_record.get("frame_slot_count") or 0)
    delta = abs(oot3d_frame_count - n64_frame_count)
    return max(0, 30 - delta)


def anonymous_numeric_family_matches(family: str, oot3d_stem: str) -> bool:
    lower_family = family.lower()
    lower_stem = oot3d_stem.lower()
    if "fighter" in lower_family:
        return lower_stem.startswith("ft_")
    if "boom" in lower_family:
        return lower_stem.startswith("boom_")
    if "normal" in lower_family:
        return lower_stem.startswith("nml_") or lower_stem.startswith("cl_nml_")
    if "anchor" in lower_family:
        return lower_stem.startswith("ac_")
    if "demo" in lower_family:
        return lower_stem.startswith("dm_") or lower_stem.startswith("cl_dm_")
    return False


def build_unresolved_near_candidate_queue(
    records: list[dict[str, object]],
    oot3d_records: list[dict[str, object]],
    *,
    per_record_limit: int = 5,
    min_score: int = 12,
) -> list[dict[str, object]]:
    queue: list[dict[str, object]] = []
    for record in records:
        if record.get("status") != "unmapped_no_candidate_csab":
            continue
        if record.get("unresolved_route_analysis") == "anonymous_numeric_symbol":
            continue
        n64_stem = str(record.get("n64_stem") or "")
        n64_frame_count = int(record.get("n64_frame_count") or 0)
        candidates: list[dict[str, object]] = []
        for oot3d_record in oot3d_records:
            score, shared_tokens = near_candidate_token_score(
                n64_stem,
                str(oot3d_record.get("stem") or ""),
            )
            if score < min_score:
                continue
            oot3d_frame_count = int(oot3d_record.get("frame_slot_count") or 0)
            frame_delta = oot3d_frame_count - n64_frame_count
            candidates.append(
                {
                    "score": score,
                    "abs_frame_delta": abs(frame_delta),
                    "n64_index": record.get("index"),
                    "n64_name": record.get("n64_name"),
                    "n64_stem": n64_stem,
                    "n64_frame_count": n64_frame_count,
                    "unresolved_route_analysis": record.get("unresolved_route_analysis"),
                    "csab_name": oot3d_record.get("csab_name"),
                    "oot3d_stem": oot3d_record.get("stem"),
                    "oot3d_frame_slot_count": oot3d_frame_count,
                    "frame_delta_oot3d_minus_n64": frame_delta,
                    "shared_tokens": ",".join(shared_tokens),
                    "diagnostic_only": True,
                    "required_evidence": (
                        "near-candidate token overlap is only triage; promotion still requires "
                        "a semantic alias rule, pose metric, runtime route, or callsite evidence"
                    ),
                }
            )
        candidates.sort(
            key=lambda item: (
                -int(item["score"]),
                int(item["abs_frame_delta"]),
                str(item.get("csab_name") or ""),
            )
        )
        queue.extend(candidates[:per_record_limit])
    return queue


def near_candidate_token_score(n64_stem: str, oot3d_stem: str) -> tuple[int, list[str]]:
    n64_tokens = near_candidate_tokens(n64_stem)
    oot3d_tokens = near_candidate_tokens(oot3d_stem)
    shared_tokens = sorted(set(n64_tokens) & set(oot3d_tokens))
    score = len(shared_tokens) * 10
    for n64_token in n64_tokens:
        if len(n64_token) < 3:
            continue
        for oot3d_token in oot3d_tokens:
            if len(oot3d_token) < 3:
                continue
            if n64_token in oot3d_token or oot3d_token in n64_token:
                score += 2
                break
    if n64_tokens and oot3d_tokens and n64_tokens[-1] == oot3d_tokens[-1]:
        score += 4
    return score, shared_tokens


def near_candidate_tokens(stem: str) -> list[str]:
    lower = stem.lower()
    replacements = (
        ("clink", "cl"),
        ("child", "cl"),
        ("link", ""),
        ("anchor", "ac"),
        ("fighter", "ft"),
        ("normal", "nml"),
        ("power", "pow"),
        ("finsh", "fin"),
    )
    for before, after in replacements:
        lower = lower.replace(before, after)
    return [
        token
        for token in re.split(r"[^a-z0-9]+", lower)
        if token and token not in {"boy", "anim", "csab"}
    ]


def candidate_validation_queue(records: list[dict[str, object]]) -> list[dict[str, object]]:
    priority = {"same_frame_count": 0, "small_delta": 1}
    queue: list[dict[str, object]] = []
    for record in records:
        analysis = record.get("candidate_promotion_analysis")
        if not isinstance(analysis, dict):
            continue
        severity = str(analysis.get("temporal_severity") or "")
        if severity not in priority:
            continue
        queue.append(
            {
                "index": record.get("index"),
                "n64_name": record.get("n64_name"),
                "n64_stem": record.get("n64_stem"),
                "csab_name": record.get("csab_name"),
                "family": analysis.get("family"),
                "temporal_severity": severity,
                "n64_frame_count": analysis.get("n64_frame_count"),
                "oot3d_frame_slot_count": analysis.get("oot3d_frame_slot_count"),
                "frame_delta_oot3d_minus_n64": analysis.get("frame_delta_oot3d_minus_n64"),
                "duration_ratio_oot3d_per_n64": analysis.get("duration_ratio_oot3d_per_n64"),
                "next_gate": analysis.get("required_evidence"),
            }
        )
    return sorted(
        queue,
        key=lambda item: (
            priority[str(item["temporal_severity"])],
            abs(int(item.get("frame_delta_oot3d_minus_n64") or 0)),
            str(item.get("family") or ""),
            int(item.get("index") or 0),
        ),
    )


def oot3d_records_from_reference_audit(reference_audit: dict[str, object]) -> list[dict[str, object]]:
    source_files = reference_audit.get("source_files")
    target_summary = reference_audit.get("oot3d_target")
    normalization = reference_audit.get("normalization_policy")
    if not isinstance(source_files, dict) or not isinstance(target_summary, dict):
        return []
    if not isinstance(normalization, dict):
        normalization = {}
    skinned_animation_binding = source_files.get("skinned_animation_binding")
    if not skinned_animation_binding:
        return []
    binding_path = Path(str(skinned_animation_binding))
    if not binding_path.is_file():
        return []
    binding = load_json(binding_path)
    target = find_target(
        binding,
        str(target_summary.get("archive_path") or ""),
        str(target_summary.get("target_cmb_name") or ""),
    )
    strip_prefixes = [
        str(prefix)
        for prefix in normalization.get("oot3d_strip_prefixes", [])
        if isinstance(prefix, str)
    ]
    return oot3d_animation_records(target, strip_prefixes)


def group_oot3d_records_by_stem(records: list[dict[str, object]]) -> dict[str, list[dict[str, object]]]:
    grouped: dict[str, list[dict[str, object]]] = {}
    for record in records:
        stem = str(record.get("stem") or "").lower()
        if stem:
            grouped.setdefault(stem, []).append(record)
    return grouped


def alias_csab_candidates_for_n64(
    n64_record: dict[str, object],
    oot3d_by_stem: dict[str, list[dict[str, object]]],
) -> list[dict[str, object]]:
    matches: list[dict[str, object]] = []
    seen_csabs: set[str] = set()
    for stem in alias_stem_candidates_for_n64(str(n64_record.get("stem") or "")):
        for record in oot3d_by_stem.get(stem, []):
            csab_name = str(record.get("csab_name") or "")
            if not csab_name or csab_name in seen_csabs:
                continue
            seen_csabs.add(csab_name)
            matches.append(
                {
                    "csab_name": csab_name,
                    "oot3d_stem": record.get("stem"),
                    "oot3d_normalized_key": record.get("normalized_key"),
                    "oot3d_frame_slot_count": record.get("frame_slot_count"),
                    "match_status": "single_semantic_alias_candidate",
                    "alias_stem": stem,
                    "alias_policy": "n64-anchor-fighter-demo-normal-finsh-to-oot3d-stem",
                }
            )
    return matches


def alias_stem_candidates_for_n64(stem: str) -> list[str]:
    out: list[str] = []
    lower = stem.lower()
    for prefix in ("", "link_", "clink_", "child_", "kolink_"):
        if prefix and not lower.startswith(prefix):
            continue
        base = lower[len(prefix) :] if prefix else lower
        add_alias_stem_variants(out, base)
        if base.startswith("anchor_"):
            anchor_base = base[len("anchor_") :]
            anchor_variants = alias_text_variants(anchor_base)
            for variant in list(anchor_variants):
                add_alias_stem(anchor_variants, variant.replace("_kiru", ""))
            for variant in anchor_variants:
                add_alias_stem(out, "ac_" + variant)
        if base.startswith("fighter_"):
            fighter_base = base[len("fighter_") :]
            fighter_variants = alias_text_variants(fighter_base)
            for variant in list(fighter_variants):
                add_alias_stem(fighter_variants, variant.replace("_kiru", ""))
            for variant in fighter_variants:
                add_alias_stem(out, "ft_" + variant)
                add_alias_stem(out, "ft_" + variant + "_free")
        for source_prefix, oot3d_prefix in (
            ("bottle_", "bt_"),
            ("hammer_", "hm_"),
            ("magic_", "mg_"),
            ("normal_", "nml_"),
            ("swimer_", "sw_"),
        ):
            if base.startswith(source_prefix):
                source_base = base[len(source_prefix) :]
                for variant in alias_text_variants(source_base):
                    add_alias_stem(out, oot3d_prefix + variant)
                    add_alias_stem(out, oot3d_prefix + variant + "_free")
                if source_prefix == "normal_":
                    add_normal_family_aliases(out, source_base)
        if base.startswith("d_link_"):
            d_link_base = base[len("d_link_") :]
            for variant in alias_text_variants(d_link_base):
                add_alias_stem(out, "d_lk_" + variant)
        if base.startswith("child_tunnel_"):
            tunnel_base = base[len("child_tunnel_") :]
            for variant in alias_text_variants(tunnel_base):
                add_alias_stem(out, "cl_nml_tunnel_" + variant)
        if base.startswith("demo_"):
            demo_base = base[len("demo_") :]
            for variant in alias_text_variants(demo_base):
                if prefix in ("clink_", "child_"):
                    add_alias_stem(out, "cl_dm_" + variant)
                else:
                    add_alias_stem(out, "dm_" + variant)
            if demo_base.startswith("link_"):
                demo_link_base = demo_base[len("link_") :]
                for variant in alias_text_variants(demo_link_base):
                    if prefix in ("clink_", "child_"):
                        add_alias_stem(out, "cl_dm_" + variant)
                    else:
                        add_alias_stem(out, "dm_lk_" + variant)
        add_one_off_aliases(out, base)
    return out


def add_normal_family_aliases(out: list[str], source_base: str) -> None:
    if source_base == "jump2landing":
        add_alias_stem(out, "nml_landing")
    if source_base.startswith("jump_climb_"):
        climb_base = source_base[len("jump_climb_") :]
        for variant in alias_text_variants(climb_base):
            add_alias_stem(out, "nml_climb_" + variant)
            add_alias_stem(out, "nml_climb_" + variant + "_free")
            add_alias_stem(out, "cl_nml_climb_" + variant)
    if source_base.startswith("talk_navi"):
        suffix = source_base[len("talk_navi") :].strip("_")
        add_alias_stem(out, "nml_talk_free" + (f"_{suffix}" if suffix else ""))
    if source_base == "rebound":
        add_alias_stem(out, "ft_rebound")
    if source_base == "wait2waitl":
        add_alias_stem(out, "nml_waitl2wait")
    if source_base == "run_jump_water_fall":
        add_alias_stem(out, "nml_run_dive")
    if source_base == "run_jump_water_fall_wait":
        add_alias_stem(out, "nml_run_dive_wait")
    if source_base == "push_fall":
        add_alias_stem(out, "nml_push_wait_end")
        add_alias_stem(out, "nml_push_end")


def add_one_off_aliases(out: list[str], base: str) -> None:
    one_off = {
        "hatto_demo": "hatto_dm",
        "odoroki_demo": "koodoroki_dm",
        "okiru_demo": "okiru_dm",
        "shagamu_demo": "shagamu_dm",
        "swimer_swim_dead": "derth_rebirth",
        "demo_tbox_open": "cl_dm_tbox_open",
        "anchor_bom_side_walkl": "boom_throw_side_walkl",
        "anchor_bom_side_walkr": "boom_throw_side_walkr",
        "fighter_defense_long": "ft_defense_long_hit",
        "normal_jump_climb_hold": "nml_hang_hold",
        "normal_jump_climb_hold_free": "nml_hang_hold_free",
        "normal_jump_climb_up_free": "nml_hang_up_free",
        "normal_jump_climb_wait": "nml_hang_wait",
        "normal_jump_climb_wait_free": "nml_hang_wait_free",
    }
    alias = one_off.get(base)
    if alias is not None:
        add_alias_stem(out, alias)


def add_alias_stem_variants(out: list[str], stem: str) -> None:
    for variant in alias_text_variants(stem):
        add_alias_stem(out, variant)


def alias_text_variants(stem: str) -> list[str]:
    variants: list[str] = []
    add_alias_stem(variants, stem)
    add_alias_stem(variants, stem.replace("finsh", "fin"))
    add_alias_stem(variants, stem.replace("normal", "nml"))
    add_alias_stem(variants, stem.replace("finsh", "fin").replace("normal", "nml"))
    add_alias_stem(variants, stem.replace("power", "pow"))
    add_alias_stem(variants, stem.replace("finsh", "fin").replace("power", "pow"))
    add_alias_stem(variants, stem.replace("fighter", "ft"))
    add_alias_stem(variants, stem.replace("normal", "nml").replace("fighter", "ft"))
    add_alias_stem(variants, stem.replace("link", "lk"))
    add_alias_stem(variants, stem.replace("down_slope_slip", "down_slip"))
    add_alias_stem(variants, stem.replace("up_slope_slip", "up_slip"))
    add_alias_stem(variants, stem.replace("drink_demo", "drink_dm"))
    add_alias_stem(variants, stem.replace("kakeyori", "runup"))
    return variants


def add_alias_stem(out: list[str], stem: str) -> None:
    key = stem.strip("_").lower()
    if key and key not in out:
        out.append(key)


def unresolved_family_for_n64_stem(stem: str) -> str:
    lower = stem.lower()
    for prefix in ("link_", "clink_", "child_", "kolink_"):
        if lower.startswith(prefix):
            lower = lower[len(prefix) :]
            break
    parts = [part for part in lower.split("_") if part]
    if not parts:
        return "unknown"
    if parts[0].isdigit() or all(char in "0123456789abcdef" for char in parts[0]):
        return "numeric_symbol"
    if parts[0] == "demo" and len(parts) > 1:
        return "demo_" + parts[1]
    if parts[0] == "anchor" and len(parts) > 1:
        return "anchor_" + parts[1]
    if parts[0] in {"d", "l", "r"} and len(parts) > 1:
        return parts[0] + "_" + parts[1]
    return parts[0]


def unresolved_route_kind_for_n64_record(record: dict[str, object]) -> str:
    stem = str(record.get("stem") or "")
    lower = stem.lower()
    if is_numeric_stem(lower):
        return "anonymous_numeric_n64_symbol_requires_pose_or_callsite_identity"
    return "named_n64_animation_without_oot3d_csab_alias_candidate"


def is_numeric_stem(stem: str) -> bool:
    lower = stem.lower()
    return bool(lower) and all(char in "0123456789abcdef" for char in lower)


def mapping_record(
    n64_record: dict[str, object],
    candidates: list[dict[str, object]],
    alias_candidates: list[dict[str, object]],
    status: str,
    timing_status: str,
    match_status: str,
) -> dict[str, object]:
    first = candidates[0] if len(candidates) == 1 else {}
    alias_first = alias_candidates[0] if len(alias_candidates) == 1 else {}
    return {
        "index": n64_record.get("index"),
        "n64_name": n64_record.get("name"),
        "n64_stem": n64_record.get("stem"),
        "n64_normalized_key": n64_record.get("normalized_key"),
        "n64_data_name": n64_record.get("data_name"),
        "n64_resource_path": (
            f"misc/link_animetion/{n64_record.get('data_name')}"
            if n64_record.get("data_name")
            else None
        ),
        "n64_frame_count": n64_record.get("frame_count"),
        "status": status,
        "match_status": match_status,
        "timing_status": timing_status,
        "csab_name": first.get("csab_name") or alias_first.get("csab_name"),
        "oot3d_stem": first.get("oot3d_stem") or alias_first.get("oot3d_stem"),
        "oot3d_normalized_key": first.get("oot3d_normalized_key") or alias_first.get("oot3d_normalized_key"),
        "oot3d_frame_slot_count": first.get("oot3d_frame_slot_count") or alias_first.get("oot3d_frame_slot_count"),
        "alias_stem": alias_first.get("alias_stem"),
        "alias_policy": alias_first.get("alias_policy"),
        "candidate_csab_count": len(candidates),
        "alias_candidate_csab_count": len(alias_candidates),
        "candidate_csabs": [
            {
                "csab_name": candidate.get("csab_name"),
                "oot3d_stem": candidate.get("oot3d_stem"),
                "oot3d_normalized_key": candidate.get("oot3d_normalized_key"),
                "match_status": candidate.get("match_status"),
                "timing_status": candidate.get("timing_status"),
                "oot3d_frame_slot_count": candidate.get("oot3d_frame_slot_count"),
            }
            for candidate in candidates
        ]
        or alias_candidates,
    }


def reference_semantic_status(reference_audit: dict[str, object]) -> dict[str, object]:
    blockers: list[str] = []
    n64_reference = reference_audit.get("n64_reference") if isinstance(reference_audit.get("n64_reference"), dict) else {}
    payload_decode = (
        n64_reference.get("player_animation_payload_decode")
        if isinstance(n64_reference.get("player_animation_payload_decode"), dict)
        else {}
    )
    limb_mapping = reference_audit.get("limb_mapping") if isinstance(reference_audit.get("limb_mapping"), dict) else {}
    pose_metric = reference_audit.get("pose_error_metric") if isinstance(reference_audit.get("pose_error_metric"), dict) else {}
    if payload_decode.get("status") != "decoded":
        blockers.append("n64-player-animation-payloads-not-decoded")
    if int(payload_decode.get("issue_count") or 0) != 0:
        blockers.append("n64-player-animation-payload-decode-issues")
    if limb_mapping.get("status") != "candidate_ready_for_metric":
        blockers.append("oot3d-n64-limb-map-not-ready")
    if int(limb_mapping.get("issue_count") or 0) != 0:
        blockers.append("oot3d-n64-limb-map-issues")
    if pose_metric.get("status") != "measured":
        blockers.append("oot3d-n64-pose-metric-not-measured")
    if pose_metric.get("acceptance_status") == "requires_thresholds":
        blockers.append("oot3d-n64-pose-metric-thresholds-not-promoted")
    return {
        "n64_payload_decode_status": payload_decode.get("status"),
        "n64_payload_decode_issue_count": int(payload_decode.get("issue_count") or 0),
        "limb_mapping_status": limb_mapping.get("status"),
        "limb_mapping_issue_count": int(limb_mapping.get("issue_count") or 0),
        "pose_metric_status": pose_metric.get("status"),
        "pose_metric_acceptance_status": pose_metric.get("acceptance_status"),
        "blocking_issues": blockers,
        "blocking_issue_count": len(blockers),
    }


def ratio(value: int, total: int) -> float:
    return float(value) / float(total) if total else 0.0


def write_csv(path: Path, records: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "index",
        "n64_name",
        "n64_data_name",
        "n64_frame_count",
        "status",
        "match_status",
        "timing_status",
        "csab_name",
        "oot3d_frame_slot_count",
        "candidate_csab_count",
        "alias_candidate_csab_count",
        "unresolved_resolution_class",
        "unresolved_resolution_required_evidence",
        "unresolved_resolution_notes",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for record in records:
            writer.writerow({field: record.get(field) for field in fieldnames})


def write_candidate_validation_csv(path: Path, queue: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    records = [record for record in queue if isinstance(record, dict)] if isinstance(queue, list) else []
    fieldnames = [
        "index",
        "n64_name",
        "n64_stem",
        "csab_name",
        "family",
        "temporal_severity",
        "n64_frame_count",
        "oot3d_frame_slot_count",
        "frame_delta_oot3d_minus_n64",
        "duration_ratio_oot3d_per_n64",
        "next_gate",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for record in records:
            writer.writerow({field: record.get(field) for field in fieldnames})


def write_candidate_pose_metric_csv(path: Path, queue: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    records = [record for record in queue if isinstance(record, dict)] if isinstance(queue, list) else []
    fieldnames = [
        "index",
        "n64_name",
        "n64_stem",
        "csab_name",
        "oot3d_stem",
        "family",
        "temporal_severity",
        "n64_frame_count",
        "oot3d_frame_slot_count",
        "frame_delta_oot3d_minus_n64",
        "duration_ratio_oot3d_per_n64",
        "pose_metric_status",
        "sample_pair_count",
        "measured_pair_count",
        "issue_count",
        "reference_envelope_status",
        "promotion_review_class",
        "normalized_extent_mean_abs_delta_avg",
        "normalized_extent_mean_abs_delta_max",
        "normalized_extent_max_abs_delta_avg",
        "normalized_extent_max_abs_delta_max",
        "center_delta_normalized_avg",
        "center_delta_normalized_max",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for record in records:
            writer.writerow({field: record.get(field) for field in fieldnames})


def write_semantic_alias_promotion_csv(path: Path, ledger: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    records = [record for record in ledger if isinstance(record, dict)] if isinstance(ledger, list) else []
    fieldnames = [
        "index",
        "n64_name",
        "n64_stem",
        "n64_data_name",
        "csab_name",
        "oot3d_stem",
        "alias_stem",
        "alias_policy",
        "n64_frame_count",
        "oot3d_frame_slot_count",
        "frame_delta_oot3d_minus_n64",
        "duration_ratio_oot3d_per_n64",
        "pose_metric_status",
        "reference_envelope_status",
        "promotion_review_class",
        "semantic_alias_promotion_class",
        "runtime_csab_collision_count",
        "runtime_csab_collision_n64_names",
        "candidate_csab_collision_count",
        "candidate_csab_collision_n64_names",
        "normalized_extent_mean_abs_delta_max",
        "normalized_extent_max_abs_delta_max",
        "center_delta_normalized_max",
        "required_evidence",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for record in records:
            writer.writerow({field: record.get(field) for field in fieldnames})


def write_semantic_alias_route_review_csv(path: Path, rows: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    records = [record for record in rows if isinstance(record, dict)] if isinstance(rows, list) else []
    fieldnames = [
        "index",
        "n64_name",
        "n64_stem",
        "n64_data_name",
        "csab_name",
        "oot3d_stem",
        "semantic_alias_promotion_class",
        "route_review_class",
        "n64_source_reference_status",
        "n64_direct_source_reference_count",
        "n64_direct_player_actor_source_reference_count",
        "n64_sample_direct_source_references",
        "n64_neighbor_family",
        "n64_previous_neighbor_stems",
        "n64_next_neighbor_stems",
        "oot3d_neighbor_family",
        "oot3d_previous_neighbor_stems",
        "oot3d_next_neighbor_stems",
        "candidate_family",
        "normalized_extent_max_abs_delta_max",
        "center_delta_normalized_max",
        "runtime_csab_collision_count",
        "runtime_csab_collision_n64_names",
        "candidate_csab_collision_count",
        "candidate_csab_collision_n64_names",
        "candidate_collision_direct_player_actor_source_reference_count",
        "candidate_collision_sample_direct_source_references",
        "required_evidence",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for record in records:
            writer.writerow({field: record.get(field) for field in fieldnames})


def write_pose_source_callsite_context_csv(path: Path, rows: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    records = [record for record in rows if isinstance(record, dict)] if isinstance(rows, list) else []
    fieldnames = [
        "index",
        "n64_name",
        "n64_stem",
        "n64_data_name",
        "source_csab_name",
        "source_oot3d_stem",
        "n64_frame_count",
        "source_frame_slot_count",
        "frame_delta_oot3d_minus_n64",
        "duration_ratio_oot3d_per_n64",
        "reference_envelope_status",
        "normalized_extent_max_abs_delta_max",
        "center_delta_normalized_max",
        "semantic_resolution_status",
        "callsite_context_status",
        "callsite_review_class",
        "direct_source_reference_count",
        "direct_player_actor_source_reference_count",
        "sample_callsite_contexts",
        "semantic_effect",
        "next_evidence",
        "required_next_step",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for record in records:
            writer.writerow({field: record.get(field) for field in fieldnames})


def write_semantic_alias_accepted_overlay_csv(path: Path, rows: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    records = [record for record in rows if isinstance(record, dict)] if isinstance(rows, list) else []
    fieldnames = [
        "index",
        "n64_name",
        "n64_stem",
        "n64_data_name",
        "csab_name",
        "oot3d_stem",
        "n64_frame_count",
        "oot3d_frame_slot_count",
        "frame_delta_oot3d_minus_n64",
        "duration_ratio_oot3d_per_n64",
        "semantic_overlay_status",
        "acceptance_class",
        "temporal_policy",
        "temporal_policy_status",
        "acceptance_basis",
        "n64_direct_player_actor_source_reference_count",
        "n64_sample_direct_source_references",
        "normalized_extent_max_abs_delta_max",
        "center_delta_normalized_max",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for record in records:
            writer.writerow({field: record.get(field) for field in fieldnames})


def write_semantic_alias_resample_review_csv(path: Path, rows: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    records = [record for record in rows if isinstance(record, dict)] if isinstance(rows, list) else []
    fieldnames = [
        "index",
        "n64_name",
        "n64_stem",
        "n64_data_name",
        "csab_name",
        "oot3d_stem",
        "n64_frame_count",
        "oot3d_frame_slot_count",
        "frame_delta_oot3d_minus_n64",
        "duration_ratio_oot3d_per_n64",
        "semantic_alias_promotion_class",
        "resample_review_class",
        "temporal_policy",
        "n64_source_reference_status",
        "n64_direct_source_reference_count",
        "n64_direct_player_actor_source_reference_count",
        "n64_sample_direct_source_references",
        "n64_neighbor_family",
        "n64_previous_neighbor_stems",
        "n64_next_neighbor_stems",
        "oot3d_neighbor_family",
        "oot3d_previous_neighbor_stems",
        "oot3d_next_neighbor_stems",
        "runtime_csab_collision_count",
        "runtime_csab_collision_n64_names",
        "candidate_csab_collision_count",
        "candidate_csab_collision_n64_names",
        "normalized_extent_max_abs_delta_max",
        "center_delta_normalized_max",
        "required_evidence",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for record in records:
            writer.writerow({field: record.get(field) for field in fieldnames})


def write_semantic_alias_temporal_bake_review_csv(path: Path, rows: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    records = [record for record in rows if isinstance(record, dict)] if isinstance(rows, list) else []
    fieldnames = [
        "index",
        "n64_name",
        "n64_stem",
        "n64_data_name",
        "csab_name",
        "oot3d_stem",
        "candidate_family",
        "n64_frame_count",
        "oot3d_frame_slot_count",
        "frame_delta_oot3d_minus_n64",
        "duration_ratio_oot3d_per_n64",
        "temporal_severity",
        "semantic_alias_promotion_class",
        "temporal_bake_review_class",
        "temporal_policy",
        "n64_source_reference_status",
        "n64_direct_source_reference_count",
        "n64_direct_player_actor_source_reference_count",
        "n64_sample_direct_source_references",
        "n64_neighbor_family",
        "n64_previous_neighbor_stems",
        "n64_next_neighbor_stems",
        "oot3d_neighbor_family",
        "oot3d_previous_neighbor_stems",
        "oot3d_next_neighbor_stems",
        "runtime_csab_collision_count",
        "runtime_csab_collision_n64_names",
        "candidate_csab_collision_count",
        "candidate_csab_collision_n64_names",
        "normalized_extent_max_abs_delta_max",
        "center_delta_normalized_max",
        "required_evidence",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for record in records:
            writer.writerow({field: record.get(field) for field in fieldnames})


def write_semantic_alias_bake_contract_csv(path: Path, contracts: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    records = [record for record in contracts if isinstance(record, dict)] if isinstance(contracts, list) else []
    fieldnames = [
        "index",
        "n64_name",
        "n64_stem",
        "n64_data_name",
        "source_csab_name",
        "source_oot3d_stem",
        "candidate_family",
        "contract_status",
        "contract_kind",
        "temporal_bake_review_class",
        "materialization_class",
        "source_frame_slot_count",
        "target_n64_frame_count",
        "frame_delta_oot3d_minus_n64",
        "duration_ratio_oot3d_per_n64",
        "temporal_severity",
        "source_format",
        "target_semantic_slot",
        "output_resource_policy",
        "frame_mapping_contract",
        "ownership_policy",
        "route_evidence_status",
        "n64_direct_player_actor_source_reference_count",
        "n64_sample_direct_source_references",
        "runtime_csab_collision_count",
        "runtime_csab_collision_n64_names",
        "candidate_csab_collision_count",
        "candidate_csab_collision_n64_names",
        "normalized_extent_max_abs_delta_max",
        "center_delta_normalized_max",
        "required_next_step",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for record in records:
            writer.writerow({field: record.get(field) for field in fieldnames})


def write_semantic_alias_ambiguity_arbitration_csv(path: Path, rows: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    records = [record for record in rows if isinstance(record, dict)] if isinstance(rows, list) else []
    fieldnames = [
        "index",
        "n64_name",
        "n64_stem",
        "csab_name",
        "oot3d_stem",
        "family",
        "ambiguous_candidate_index",
        "candidate_rank",
        "is_pose_best_candidate",
        "arbitration_class",
        "temporal_severity",
        "n64_frame_count",
        "oot3d_frame_slot_count",
        "frame_delta_oot3d_minus_n64",
        "duration_ratio_oot3d_per_n64",
        "pose_metric_status",
        "sample_pair_count",
        "measured_pair_count",
        "issue_count",
        "reference_envelope_status",
        "promotion_review_class",
        "normalized_extent_mean_abs_delta_avg",
        "normalized_extent_mean_abs_delta_max",
        "normalized_extent_max_abs_delta_avg",
        "normalized_extent_max_abs_delta_max",
        "center_delta_normalized_avg",
        "center_delta_normalized_max",
        "runtime_csab_collision_count",
        "runtime_csab_collision_n64_names",
        "candidate_csab_collision_count",
        "candidate_csab_collision_n64_names",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for record in records:
            writer.writerow({field: record.get(field) for field in fieldnames})


def write_unresolved_near_candidate_csv(path: Path, queue: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    records = [record for record in queue if isinstance(record, dict)] if isinstance(queue, list) else []
    fieldnames = [
        "score",
        "abs_frame_delta",
        "n64_index",
        "n64_name",
        "n64_stem",
        "n64_frame_count",
        "unresolved_route_analysis",
        "csab_name",
        "oot3d_stem",
        "oot3d_frame_slot_count",
        "frame_delta_oot3d_minus_n64",
        "shared_tokens",
        "candidate_source",
        "table_context_family",
        "previous_named_stems",
        "next_named_stems",
        "nearest_neighbor_csab",
        "oot3d_table_distance",
        "diagnostic_only",
        "required_evidence",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for record in records:
            writer.writerow({field: record.get(field) for field in fieldnames})


def write_unresolved_near_candidate_pose_metric_csv(path: Path, rows: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    records = [record for record in rows if isinstance(record, dict)] if isinstance(rows, list) else []
    fieldnames = [
        "index",
        "n64_name",
        "n64_stem",
        "n64_data_name",
        "n64_frame_count",
        "unresolved_route_analysis",
        "unresolved_resolution_class",
        "csab_name",
        "oot3d_stem",
        "oot3d_frame_slot_count",
        "near_candidate_index",
        "near_candidate_rank",
        "is_pose_best_near_candidate",
        "near_pose_review_class",
        "near_candidate_score",
        "near_candidate_shared_tokens",
        "near_candidate_source",
        "near_candidate_table_context_family",
        "near_candidate_previous_named_stems",
        "near_candidate_next_named_stems",
        "near_candidate_oot3d_table_distance",
        "family",
        "temporal_severity",
        "frame_delta_oot3d_minus_n64",
        "duration_ratio_oot3d_per_n64",
        "pose_metric_status",
        "sample_pair_count",
        "measured_pair_count",
        "issue_count",
        "reference_envelope_status",
        "promotion_review_class",
        "normalized_extent_mean_abs_delta_avg",
        "normalized_extent_mean_abs_delta_max",
        "normalized_extent_max_abs_delta_avg",
        "normalized_extent_max_abs_delta_max",
        "center_delta_normalized_avg",
        "center_delta_normalized_max",
        "runtime_csab_collision_count",
        "runtime_csab_collision_n64_names",
        "candidate_csab_collision_count",
        "candidate_csab_collision_n64_names",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for record in records:
            writer.writerow({field: record.get(field) for field in fieldnames})


def write_unresolved_near_candidate_bake_contract_csv(path: Path, contracts: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    records = [record for record in contracts if isinstance(record, dict)] if isinstance(contracts, list) else []
    fieldnames = [
        "index",
        "n64_name",
        "n64_stem",
        "n64_data_name",
        "source_csab_name",
        "source_oot3d_stem",
        "contract_status",
        "contract_kind",
        "near_pose_review_class",
        "materialization_class",
        "source_frame_slot_count",
        "target_n64_frame_count",
        "frame_delta_oot3d_minus_n64",
        "duration_ratio_oot3d_per_n64",
        "temporal_severity",
        "source_format",
        "target_semantic_slot",
        "unresolved_route_analysis",
        "unresolved_resolution_class",
        "output_resource_policy",
        "frame_mapping_contract",
        "ownership_policy",
        "route_evidence_status",
        "near_candidate_count",
        "measured_near_candidate_count",
        "inside_reference_envelope_near_candidate_count",
        "best_near_candidate_score",
        "best_near_candidate_shared_tokens",
        "best_normalized_extent_max_abs_delta_max",
        "best_center_delta_normalized_max",
        "second_csab_name",
        "second_normalized_extent_max_abs_delta_max",
        "runtime_csab_collision_n64_names",
        "candidate_csab_collision_n64_names",
        "required_next_step",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for record in records:
            writer.writerow({field: record.get(field) for field in fieldnames})


def write_semantic_resolution_contract_csv(path: Path, rows: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    records = [record for record in rows if isinstance(record, dict)] if isinstance(rows, list) else []
    fieldnames = [
        "index",
        "n64_name",
        "n64_stem",
        "n64_data_name",
        "n64_frame_count",
        "base_mapping_status",
        "semantic_resolution_status",
        "semantic_source_status",
        "runtime_acceptance_status",
        "source_contract_kind",
        "source_csab_name",
        "source_oot3d_stem",
        "source_frame_slot_count",
        "frame_delta_oot3d_minus_n64",
        "duration_ratio_oot3d_per_n64",
        "materialization_class",
        "ownership_gate",
        "pose_metric_status",
        "reference_envelope_status",
        "normalized_extent_max_abs_delta_max",
        "center_delta_normalized_max",
        "runtime_csab_collision_n64_names",
        "candidate_csab_collision_n64_names",
        "unresolved_route_analysis",
        "unresolved_resolution_class",
        "payload_duplicate_of_n64_name",
        "payload_duplicate_sha1",
        "required_next_step",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for record in records:
            writer.writerow({field: record.get(field) for field in fieldnames})


def write_semantic_blocked_resolution_frontier_csv(path: Path, rows: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    records = [record for record in rows if isinstance(record, dict)] if isinstance(rows, list) else []
    fieldnames = [
        "index",
        "n64_name",
        "n64_stem",
        "n64_data_name",
        "n64_frame_count",
        "semantic_resolution_status",
        "source_contract_kind",
        "source_csab_name",
        "source_oot3d_stem",
        "source_frame_slot_count",
        "materialization_class",
        "ownership_gate",
        "reference_envelope_status",
        "unresolved_route_analysis",
        "unresolved_resolution_class",
        "frontier_class",
        "frontier_gate",
        "candidate_source_present",
        "diagnostic_derivative_policy",
        "next_evidence_type",
        "required_next_step",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for record in records:
            writer.writerow({field: record.get(field) for field in fieldnames})


def write_semantic_materialization_plan_csv(path: Path, rows: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    records = [record for record in rows if isinstance(record, dict)] if isinstance(rows, list) else []
    fieldnames = [
        "index",
        "n64_name",
        "n64_stem",
        "n64_data_name",
        "source_csab_name",
        "source_oot3d_stem",
        "source_resource_path",
        "source_animation_index",
        "source_frame_slot_count",
        "actual_source_frame_slot_count",
        "source_track_frame_count_candidate",
        "target_n64_frame_count",
        "materialization_class",
        "source_contract_kind",
        "source_resolution_status",
        "frame_mapping_status",
        "output_resource_path_status",
        "source_track_format",
        "source_track_validation_status",
        "source_track_count",
        "source_channel_count",
        "source_keyframe_count",
        "source_track_sha1",
        "output_csab_name",
        "output_track_resource_path",
        "frame_sample_count",
        "frame_sample_map_sha1",
        "frontier_class",
        "frontier_gate",
        "ownership_gate",
        "diagnostic_derivative_policy",
        "sampling_policy",
        "runtime_acceptance_status",
        "required_next_step",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for record in records:
            writer.writerow({field: record.get(field) for field in fieldnames})


def write_semantic_materialized_track_manifest_csv(path: Path, rows: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    records = [record for record in rows if isinstance(record, dict)] if isinstance(rows, list) else []
    fieldnames = [
        "index",
        "n64_name",
        "n64_stem",
        "n64_data_name",
        "source_csab_name",
        "source_resource_path",
        "source_track_sha1",
        "output_csab_name",
        "output_track_resource_path",
        "output_track_file",
        "output_track_sha1",
        "materialization_class",
        "materialization_status",
        "source_track_format",
        "output_track_format",
        "source_frame_slot_count",
        "target_n64_frame_count",
        "output_frame_count_candidate",
        "output_frame_slot_count",
        "output_track_count",
        "output_channel_count",
        "output_const_channel_count",
        "output_keyed_channel_count",
        "output_keyframe_count",
        "source_sample_parity_status",
        "source_sample_parity_checked_channel_count",
        "source_sample_parity_checked_key_count",
        "source_sample_parity_zero_tangent_mismatch_count",
        "source_sample_parity_issue_count",
        "source_sample_parity_max_abs_value_delta",
        "output_frame_dense_validation_status",
        "output_frame_dense_validation_sampled_frames",
        "output_frame_dense_validation_checked_key_count",
        "output_frame_dense_validation_issue_count",
        "output_frame_dense_validation_zero_tangent_mismatch_count",
        "source_full_pose_sample_status",
        "source_full_pose_sample_frames",
        "materialized_pose_parity_status",
        "frame_sample_count",
        "frame_sample_map_sha1",
        "runtime_acceptance_status",
        "required_next_step",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for record in records:
            writer.writerow({field: record.get(field) for field in fieldnames})


def write_semantic_materialized_pose_metric_csv(path: Path, rows: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    records = [record for record in rows if isinstance(record, dict)] if isinstance(rows, list) else []
    fieldnames = [
        "index",
        "n64_name",
        "n64_stem",
        "n64_data_name",
        "source_csab_name",
        "output_csab_name",
        "output_track_resource_path",
        "output_track_file",
        "materialization_class",
        "target_n64_frame_count",
        "pose_metric_status",
        "frame_pair_count",
        "measured_frame_pair_count",
        "issue_count",
        "reference_envelope_status",
        "normalized_extent_mean_abs_delta_avg",
        "normalized_extent_mean_abs_delta_max",
        "normalized_extent_max_abs_delta_avg",
        "normalized_extent_max_abs_delta_max",
        "center_delta_normalized_avg",
        "center_delta_normalized_max",
        "scale_factor_oot3d_per_n64_avg",
        "scale_factor_oot3d_per_n64_min",
        "scale_factor_oot3d_per_n64_max",
        "runtime_acceptance_status",
        "required_next_step",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for record in records:
            writer.writerow({field: record.get(field) for field in fieldnames})


def write_semantic_ownership_derivative_frontier_csv(path: Path, rows: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    records = [record for record in rows if isinstance(record, dict)] if isinstance(rows, list) else []
    fieldnames = [
        "index",
        "n64_name",
        "n64_stem",
        "n64_data_name",
        "n64_frame_count",
        "source_contract_kind",
        "source_csab_name",
        "source_oot3d_stem",
        "source_frame_slot_count",
        "source_csab_claim_count",
        "frontier_class",
        "frontier_gate",
        "ownership_gate",
        "materialization_class",
        "frame_mapping_status",
        "output_csab_name",
        "output_track_resource_path",
        "pose_metric_status",
        "pose_metric_issue_count",
        "reference_envelope_status",
        "normalized_extent_max_abs_delta_max",
        "center_delta_normalized_max",
        "scale_factor_oot3d_per_n64_avg",
        "scale_factor_oot3d_per_n64_min",
        "scale_factor_oot3d_per_n64_max",
        "derivative_frontier_class",
        "post_derivative_gate",
        "runtime_acceptance_status",
        "required_next_step",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for record in records:
            writer.writerow({field: record.get(field) for field in fieldnames})


def write_semantic_ownership_reuse_arbitration_csv(path: Path, rows: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    records = [record for record in rows if isinstance(record, dict)] if isinstance(rows, list) else []
    fieldnames = [
        "index",
        "n64_name",
        "n64_stem",
        "n64_data_name",
        "n64_frame_count",
        "source_csab_name",
        "source_oot3d_stem",
        "source_frame_slot_count",
        "source_group_n64_names",
        "source_csab_claim_count",
        "source_group_class",
        "frontier_class",
        "derivative_frontier_class",
        "ownership_gate",
        "materialization_class",
        "output_csab_name",
        "output_track_resource_path",
        "runtime_csab_collision_n64_names",
        "candidate_csab_collision_n64_names",
        "direct_source_reference_count",
        "direct_player_actor_source_reference_count",
        "route_evidence_status",
        "sample_direct_source_references",
        "ownership_reuse_arbitration_class",
        "policy_decision_scope",
        "route_acceptance_status",
        "runtime_acceptance_status",
        "required_next_step",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for record in records:
            writer.writerow({field: record.get(field) for field in fieldnames})


def write_semantic_route_proven_ownership_callsite_csv(path: Path, rows: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    records = [record for record in rows if isinstance(record, dict)] if isinstance(rows, list) else []
    fieldnames = [
        "index",
        "n64_name",
        "n64_stem",
        "n64_data_name",
        "n64_frame_count",
        "source_csab_name",
        "source_oot3d_stem",
        "source_frame_slot_count",
        "materialization_class",
        "output_csab_name",
        "output_track_resource_path",
        "source_group_n64_names",
        "source_csab_claim_count",
        "ownership_reuse_arbitration_class",
        "direct_player_actor_source_reference_count",
        "local_player_actor_source_reference_count",
        "external_player_actor_mirror_reference_count",
        "unique_callsite_signature_count",
        "unique_context_signature_count",
        "context_names",
        "callsite_review_classes",
        "context_usage_statuses",
        "callsite_context_summary",
        "package_candidate_status",
        "runtime_acceptance_status",
        "required_next_step",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for record in records:
            writer.writerow({field: record.get(field) for field in fieldnames})
