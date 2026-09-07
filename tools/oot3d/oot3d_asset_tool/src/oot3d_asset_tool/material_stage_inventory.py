from __future__ import annotations

import json
from collections import Counter
from pathlib import Path

from .binary import ParseError
from .cmb import CmbModel
from .material_audit import (
    KNOWN_MATERIAL_ISSUES,
    KNOWN_TEXTURE_STAGE_RISKS,
    material_audit_record,
    material_issues,
)
from .romfs_inventory import normalized_suffix, sorted_counter, top_level_key
from .zar import ZarArchive, ZarFile
from .zsi import ZsiFile

FLOAT_EPSILON = 0.000001
TRANSFORM_SIGNATURE_LIMIT = 50
RAW_STAGE_GAP_PATTERN_SAMPLE_LIMIT = 5


def inventory_material_stages(
    romfs_root: Path,
    output_path: Path | None = None,
    *,
    sample_limit: int = 200,
    include_model_records: bool = True,
    model_limit: int | None = None,
) -> dict[str, object]:
    if not romfs_root.is_dir():
        raise ParseError(f"{romfs_root}: expected an extracted OOT3D RomFS directory")
    if sample_limit < 0:
        raise ValueError("sample_limit must be non-negative")
    if model_limit is not None and model_limit < 0:
        raise ValueError("model_limit must be non-negative")

    files = sorted(path for path in romfs_root.rglob("*") if path.is_file())
    top_level_file_counts: Counter[str] = Counter()
    extension_counts: Counter[str] = Counter()
    container_counts: Counter[str] = Counter()
    model_counts: Counter[str] = Counter()
    material_totals = new_material_totals()
    risk_counts: Counter[str] = Counter({risk: 0 for risk in KNOWN_TEXTURE_STAGE_RISKS})
    issue_counts: Counter[str] = Counter({issue: 0 for issue in KNOWN_MATERIAL_ISSUES})
    raw_stage_selector_counts: Counter[str] = Counter()
    raw_stage_candidate_summary_counts: Counter[str] = Counter()
    raw_stage_gap_candidate_summary_counts: Counter[str] = Counter()
    raw_stage_export_classification_counts: Counter[str] = Counter()
    raw_stage_export_gap_classification_counts: Counter[str] = Counter()
    raw_stage_export_blocker_counts: Counter[str] = Counter()
    raw_stage_alignment_case_counts: Counter[str] = Counter()
    raw_stage_gap_alignment_case_counts: Counter[str] = Counter()
    raw_stage_resolution_case_counts: Counter[str] = Counter()
    raw_stage_gap_resolution_case_counts: Counter[str] = Counter()
    raw_stage_export_order_case_counts: Counter[str] = Counter()
    raw_stage_gap_export_order_case_counts: Counter[str] = Counter()
    raw_stage_slot_bounds_case_counts: Counter[str] = Counter()
    raw_stage_gap_slot_bounds_case_counts: Counter[str] = Counter()
    raw_stage_slot_sequence_case_counts: Counter[str] = Counter()
    raw_stage_gap_slot_sequence_case_counts: Counter[str] = Counter()
    raw_stage_oob_delta_counts: Counter[str] = Counter()
    raw_stage_gap_oob_delta_counts: Counter[str] = Counter()
    raw_stage_oob_delta_signature_counts: Counter[str] = Counter()
    raw_stage_gap_oob_delta_signature_counts: Counter[str] = Counter()
    raw_stage_unexported_stage_position_counts: Counter[str] = Counter()
    raw_stage_gap_unexported_stage_position_counts: Counter[str] = Counter()
    raw_stage_unexported_material_ref_stage_position_counts: Counter[str] = Counter()
    raw_stage_gap_unexported_material_ref_stage_position_counts: Counter[str] = Counter()
    exported_stage_counts: Counter[str] = Counter()
    secondary_transform_kind_counts: Counter[str] = Counter()
    secondary_transform_signature_counts: Counter[str] = Counter()
    parse_errors: list[dict[str, object]] = []
    model_records: list[dict[str, object]] = []
    sample_records: list[dict[str, object]] = []
    secondary_transform_samples: list[dict[str, object]] = []
    raw_stage_gap_candidate_samples: dict[str, list[dict[str, object]]] = {}
    raw_stage_non_gap_candidate_samples: dict[str, list[dict[str, object]]] = {}
    raw_stage_non_gap_non_material_index_candidate_samples: list[dict[str, object]] = []

    for path in files:
        rel = path.relative_to(romfs_root)
        suffix = normalized_suffix(path)
        top_level_file_counts[top_level_key(rel)] += 1
        extension_counts[suffix] += 1

        if suffix == ".cmb":
            container_counts["cmb_files"] += 1
            scan_cmb_model(
                path,
                romfs_root,
                model_counts,
                material_totals,
                risk_counts,
                issue_counts,
                raw_stage_selector_counts,
                raw_stage_candidate_summary_counts,
                raw_stage_gap_candidate_summary_counts,
                raw_stage_export_classification_counts,
                raw_stage_export_gap_classification_counts,
                raw_stage_export_blocker_counts,
                raw_stage_alignment_case_counts,
                raw_stage_gap_alignment_case_counts,
                raw_stage_resolution_case_counts,
                raw_stage_gap_resolution_case_counts,
                raw_stage_export_order_case_counts,
                raw_stage_gap_export_order_case_counts,
                raw_stage_slot_bounds_case_counts,
                raw_stage_gap_slot_bounds_case_counts,
                raw_stage_slot_sequence_case_counts,
                raw_stage_gap_slot_sequence_case_counts,
                raw_stage_oob_delta_counts,
                raw_stage_gap_oob_delta_counts,
                raw_stage_oob_delta_signature_counts,
                raw_stage_gap_oob_delta_signature_counts,
                raw_stage_unexported_stage_position_counts,
                raw_stage_gap_unexported_stage_position_counts,
                raw_stage_unexported_material_ref_stage_position_counts,
                raw_stage_gap_unexported_material_ref_stage_position_counts,
                exported_stage_counts,
                secondary_transform_kind_counts,
                secondary_transform_signature_counts,
                parse_errors,
                model_records,
                sample_records,
                secondary_transform_samples,
                raw_stage_gap_candidate_samples,
                raw_stage_non_gap_candidate_samples,
                raw_stage_non_gap_non_material_index_candidate_samples,
                sample_limit=sample_limit,
                include_model_records=include_model_records,
                model_limit=model_limit,
            )
        elif suffix == ".zar":
            container_counts["zar_archives"] += 1
            scan_zar_models(
                path,
                romfs_root,
                model_counts,
                material_totals,
                risk_counts,
                issue_counts,
                raw_stage_selector_counts,
                raw_stage_candidate_summary_counts,
                raw_stage_gap_candidate_summary_counts,
                raw_stage_export_classification_counts,
                raw_stage_export_gap_classification_counts,
                raw_stage_export_blocker_counts,
                raw_stage_alignment_case_counts,
                raw_stage_gap_alignment_case_counts,
                raw_stage_resolution_case_counts,
                raw_stage_gap_resolution_case_counts,
                raw_stage_export_order_case_counts,
                raw_stage_gap_export_order_case_counts,
                raw_stage_slot_bounds_case_counts,
                raw_stage_gap_slot_bounds_case_counts,
                raw_stage_slot_sequence_case_counts,
                raw_stage_gap_slot_sequence_case_counts,
                raw_stage_oob_delta_counts,
                raw_stage_gap_oob_delta_counts,
                raw_stage_oob_delta_signature_counts,
                raw_stage_gap_oob_delta_signature_counts,
                raw_stage_unexported_stage_position_counts,
                raw_stage_gap_unexported_stage_position_counts,
                raw_stage_unexported_material_ref_stage_position_counts,
                raw_stage_gap_unexported_material_ref_stage_position_counts,
                exported_stage_counts,
                secondary_transform_kind_counts,
                secondary_transform_signature_counts,
                parse_errors,
                model_records,
                sample_records,
                secondary_transform_samples,
                raw_stage_gap_candidate_samples,
                raw_stage_non_gap_candidate_samples,
                raw_stage_non_gap_non_material_index_candidate_samples,
                sample_limit=sample_limit,
                include_model_records=include_model_records,
                model_limit=model_limit,
            )
        elif suffix == ".zsi":
            container_counts["zsi_files"] += 1
            scan_zsi_models(
                path,
                romfs_root,
                model_counts,
                material_totals,
                risk_counts,
                issue_counts,
                raw_stage_selector_counts,
                raw_stage_candidate_summary_counts,
                raw_stage_gap_candidate_summary_counts,
                raw_stage_export_classification_counts,
                raw_stage_export_gap_classification_counts,
                raw_stage_export_blocker_counts,
                raw_stage_alignment_case_counts,
                raw_stage_gap_alignment_case_counts,
                raw_stage_resolution_case_counts,
                raw_stage_gap_resolution_case_counts,
                raw_stage_export_order_case_counts,
                raw_stage_gap_export_order_case_counts,
                raw_stage_slot_bounds_case_counts,
                raw_stage_gap_slot_bounds_case_counts,
                raw_stage_slot_sequence_case_counts,
                raw_stage_gap_slot_sequence_case_counts,
                raw_stage_oob_delta_counts,
                raw_stage_gap_oob_delta_counts,
                raw_stage_oob_delta_signature_counts,
                raw_stage_gap_oob_delta_signature_counts,
                raw_stage_unexported_stage_position_counts,
                raw_stage_gap_unexported_stage_position_counts,
                raw_stage_unexported_material_ref_stage_position_counts,
                raw_stage_gap_unexported_material_ref_stage_position_counts,
                exported_stage_counts,
                secondary_transform_kind_counts,
                secondary_transform_signature_counts,
                parse_errors,
                model_records,
                sample_records,
                secondary_transform_samples,
                raw_stage_gap_candidate_samples,
                raw_stage_non_gap_candidate_samples,
                raw_stage_non_gap_non_material_index_candidate_samples,
                sample_limit=sample_limit,
                include_model_records=include_model_records,
                model_limit=model_limit,
            )

    risk_counts["total"] = sum(
        count for risk, count in risk_counts.items() if risk != "total"
    )
    issue_counts["total"] = sum(
        count for issue, count in issue_counts.items() if issue != "total"
    )
    inventory = {
        "schema_version": 1,
        "romfs_root": str(romfs_root),
        "model_limit": model_limit,
        "sample_limit": sample_limit,
        "file_count": len(files),
        "top_level_file_counts": sorted_counter(top_level_file_counts),
        "extension_counts": sorted_counter(extension_counts),
        "container_counts": {
            "cmb_files": container_counts["cmb_files"],
            "zar_archives": container_counts["zar_archives"],
            "zsi_files": container_counts["zsi_files"],
            "embedded_cmbs": model_counts["discovered"] - container_counts["cmb_files"],
        },
        "model_counts": {
            "discovered": model_counts["discovered"],
            "parsed": model_counts["parsed"],
            "static_candidate": model_counts["static_candidate"],
            "nonstatic_candidate": model_counts["nonstatic_candidate"],
            "parse_errors": model_counts["parse_errors"],
            "skipped_by_model_limit": model_counts["skipped_by_model_limit"],
        },
        "material_count": material_totals["material_count"],
        "textured_material_count": material_totals["textured_material_count"],
        "multi_texture_material_count": material_totals["multi_texture_material_count"],
        "alpha_test_material_count": material_totals["alpha_test_material_count"],
        "blended_material_count": material_totals["blended_material_count"],
        "primary_texture_coord_transform_count": material_totals[
            "primary_texture_coord_transform_count"
        ],
        "secondary_texture_coord_transform_count": material_totals[
            "secondary_texture_coord_transform_count"
        ],
        "raw_texture_stage_selector_counts": sorted_counter(raw_stage_selector_counts),
        "raw_texture_stage_candidate_summary_counts": sorted_counter(
            raw_stage_candidate_summary_counts
        ),
        "raw_texture_stage_export_gap_candidate_summary_counts": sorted_counter(
            raw_stage_gap_candidate_summary_counts
        ),
        "raw_texture_stage_non_gap_candidate_summary_counts": sorted_counter(
            positive_counter_delta(
                raw_stage_candidate_summary_counts,
                raw_stage_gap_candidate_summary_counts,
            )
        ),
        "raw_texture_stage_export_classification_counts": sorted_counter(
            raw_stage_export_classification_counts
        ),
        "raw_texture_stage_export_gap_classification_counts": sorted_counter(
            raw_stage_export_gap_classification_counts
        ),
        "raw_texture_stage_export_blocker_counts": sorted_counter(
            raw_stage_export_blocker_counts
        ),
        "raw_texture_stage_alignment_case_counts": sorted_counter(
            raw_stage_alignment_case_counts
        ),
        "raw_texture_stage_export_gap_alignment_case_counts": sorted_counter(
            raw_stage_gap_alignment_case_counts
        ),
        "raw_texture_stage_resolution_case_counts": sorted_counter(
            raw_stage_resolution_case_counts
        ),
        "raw_texture_stage_export_gap_resolution_case_counts": sorted_counter(
            raw_stage_gap_resolution_case_counts
        ),
        "raw_texture_stage_export_order_case_counts": sorted_counter(
            raw_stage_export_order_case_counts
        ),
        "raw_texture_stage_export_gap_order_case_counts": sorted_counter(
            raw_stage_gap_export_order_case_counts
        ),
        "raw_texture_stage_f3d_limit_case_counts": sorted_counter(
            totals_prefixed_counter(
                material_totals,
                "raw_stage_f3d_limit_case_count:",
            )
        ),
        "raw_texture_stage_slot_bounds_case_counts": sorted_counter(
            raw_stage_slot_bounds_case_counts
        ),
        "raw_texture_stage_export_gap_slot_bounds_case_counts": sorted_counter(
            raw_stage_gap_slot_bounds_case_counts
        ),
        "raw_texture_stage_slot_sequence_case_counts": sorted_counter(
            raw_stage_slot_sequence_case_counts
        ),
        "raw_texture_stage_export_gap_slot_sequence_case_counts": sorted_counter(
            raw_stage_gap_slot_sequence_case_counts
        ),
        "raw_texture_stage_oob_delta_counts": sorted_counter(raw_stage_oob_delta_counts),
        "raw_texture_stage_export_gap_oob_delta_counts": sorted_counter(
            raw_stage_gap_oob_delta_counts
        ),
        "raw_texture_stage_oob_delta_signature_counts": sorted_counter(
            raw_stage_oob_delta_signature_counts
        ),
        "raw_texture_stage_export_gap_oob_delta_signature_counts": sorted_counter(
            raw_stage_gap_oob_delta_signature_counts
        ),
        "raw_texture_stage_unexported_stage_position_counts": sorted_counter(
            raw_stage_unexported_stage_position_counts
        ),
        "raw_texture_stage_export_gap_unexported_stage_position_counts": sorted_counter(
            raw_stage_gap_unexported_stage_position_counts
        ),
        "raw_texture_stage_non_gap_unexported_stage_position_counts": sorted_counter(
            positive_counter_delta(
                raw_stage_unexported_stage_position_counts,
                raw_stage_gap_unexported_stage_position_counts,
            )
        ),
        "raw_texture_stage_unexported_material_ref_stage_position_counts": sorted_counter(
            raw_stage_unexported_material_ref_stage_position_counts
        ),
        "raw_texture_stage_export_gap_unexported_material_ref_stage_position_counts": (
            sorted_counter(raw_stage_gap_unexported_material_ref_stage_position_counts)
        ),
        "raw_texture_stage_non_gap_unexported_material_ref_stage_position_counts": (
            sorted_counter(
                positive_counter_delta(
                    raw_stage_unexported_material_ref_stage_position_counts,
                    raw_stage_gap_unexported_material_ref_stage_position_counts,
                )
            )
        ),
        "raw_texture_stage_unexported_material_index_ref_stage_position_counts": (
            sorted_counter(
                totals_prefixed_counter(
                    material_totals,
                    "raw_stage_unexported_material_index_ref_stage_position_count:",
                )
            )
        ),
        "raw_texture_stage_export_gap_unexported_material_index_ref_stage_position_counts": (
            sorted_counter(
                totals_prefixed_counter(
                    material_totals,
                    "raw_stage_export_gap_unexported_material_index_ref_stage_position_count:",
                )
            )
        ),
        "raw_texture_stage_non_gap_unexported_material_index_ref_stage_position_counts": (
            sorted_counter(
                positive_counter_delta(
                    totals_prefixed_counter(
                        material_totals,
                        "raw_stage_unexported_material_index_ref_stage_position_count:",
                    ),
                    totals_prefixed_counter(
                        material_totals,
                        "raw_stage_export_gap_unexported_material_index_ref_stage_position_count:",
                    ),
                )
            )
        ),
        "raw_texture_stage_non_material_index_unexported_stage_position_counts": (
            sorted_counter(
                positive_counter_delta(
                    raw_stage_unexported_stage_position_counts,
                    totals_prefixed_counter(
                        material_totals,
                        "raw_stage_unexported_material_index_ref_stage_position_count:",
                    ),
                )
            )
        ),
        "raw_texture_stage_export_gap_non_material_index_unexported_stage_position_counts": (
            sorted_counter(
                positive_counter_delta(
                    raw_stage_gap_unexported_stage_position_counts,
                    totals_prefixed_counter(
                        material_totals,
                        "raw_stage_export_gap_unexported_material_index_ref_stage_position_count:",
                    ),
                )
            )
        ),
        "raw_texture_stage_non_gap_non_material_index_unexported_stage_position_counts": (
            sorted_counter(
                positive_counter_delta(
                    positive_counter_delta(
                        raw_stage_unexported_stage_position_counts,
                        raw_stage_gap_unexported_stage_position_counts,
                    ),
                    positive_counter_delta(
                        totals_prefixed_counter(
                            material_totals,
                            "raw_stage_unexported_material_index_ref_stage_position_count:",
                        ),
                        totals_prefixed_counter(
                            material_totals,
                            "raw_stage_export_gap_unexported_material_index_ref_stage_position_count:",
                        ),
                    ),
                )
            )
        ),
        "raw_texture_stage_direct_unexported_stage_position_counts": (
            sorted_counter(
                positive_counter_delta(
                    raw_stage_unexported_stage_position_counts,
                    raw_stage_unexported_material_ref_stage_position_counts,
                )
            )
        ),
        "raw_texture_stage_export_gap_direct_unexported_stage_position_counts": (
            sorted_counter(
                positive_counter_delta(
                    raw_stage_gap_unexported_stage_position_counts,
                    raw_stage_gap_unexported_material_ref_stage_position_counts,
                )
            )
        ),
        "raw_texture_stage_non_gap_direct_unexported_stage_position_counts": (
            sorted_counter(
                positive_counter_delta(
                    positive_counter_delta(
                        raw_stage_unexported_stage_position_counts,
                        raw_stage_gap_unexported_stage_position_counts,
                    ),
                    positive_counter_delta(
                        raw_stage_unexported_material_ref_stage_position_counts,
                        raw_stage_gap_unexported_material_ref_stage_position_counts,
                    ),
                )
            )
        ),
        "exported_texture_stage_counts": sorted_counter(exported_stage_counts),
        "secondary_texture_coord_transform_kind_counts": sorted_counter(
            secondary_transform_kind_counts
        ),
        "secondary_texture_coord_transform_signature_top_counts": top_counter(
            secondary_transform_signature_counts,
            TRANSFORM_SIGNATURE_LIMIT,
        ),
        "raw_texture_stage_mapper_mismatch_count": material_totals[
            "raw_texture_stage_mapper_mismatch_count"
        ],
        "raw_texture_stage_baked_extra_material_count": material_totals[
            "raw_texture_stage_baked_extra_material_count"
        ],
        "raw_texture_stage_baked_extra_total": material_totals[
            "raw_texture_stage_baked_extra_total"
        ],
        "raw_texture_stage_export_gap_material_count": material_totals[
            "raw_texture_stage_export_gap_material_count"
        ],
        "raw_texture_stage_export_gap_total": material_totals[
            "raw_texture_stage_export_gap_total"
        ],
        "texture_stage_risk_material_count": material_totals[
            "texture_stage_risk_material_count"
        ],
        "texture_stage_risk_counts": sorted_counter(risk_counts),
        "issue_counts": sorted_counter(issue_counts),
        "parse_error_count": len(parse_errors),
        "parse_errors": parse_errors,
        "sample_records": sample_records,
        "secondary_texture_coord_transform_samples": secondary_transform_samples,
        "raw_texture_stage_export_gap_candidate_samples": raw_stage_gap_candidate_samples,
        "raw_texture_stage_non_gap_candidate_samples": raw_stage_non_gap_candidate_samples,
        "raw_texture_stage_non_gap_non_material_index_candidate_samples": (
            raw_stage_non_gap_non_material_index_candidate_samples
        ),
        "model_records": model_records if include_model_records else [],
    }

    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8", newline="\n")
    return inventory


def scan_cmb_model(
    path: Path,
    romfs_root: Path,
    model_counts: Counter[str],
    material_totals: Counter[str],
    risk_counts: Counter[str],
    issue_counts: Counter[str],
    raw_stage_selector_counts: Counter[str],
    raw_stage_candidate_summary_counts: Counter[str],
    raw_stage_gap_candidate_summary_counts: Counter[str],
    raw_stage_export_classification_counts: Counter[str],
    raw_stage_export_gap_classification_counts: Counter[str],
    raw_stage_export_blocker_counts: Counter[str],
    raw_stage_alignment_case_counts: Counter[str],
    raw_stage_gap_alignment_case_counts: Counter[str],
    raw_stage_resolution_case_counts: Counter[str],
    raw_stage_gap_resolution_case_counts: Counter[str],
    raw_stage_export_order_case_counts: Counter[str],
    raw_stage_gap_export_order_case_counts: Counter[str],
    raw_stage_slot_bounds_case_counts: Counter[str],
    raw_stage_gap_slot_bounds_case_counts: Counter[str],
    raw_stage_slot_sequence_case_counts: Counter[str],
    raw_stage_gap_slot_sequence_case_counts: Counter[str],
    raw_stage_oob_delta_counts: Counter[str],
    raw_stage_gap_oob_delta_counts: Counter[str],
    raw_stage_oob_delta_signature_counts: Counter[str],
    raw_stage_gap_oob_delta_signature_counts: Counter[str],
    raw_stage_unexported_stage_position_counts: Counter[str],
    raw_stage_gap_unexported_stage_position_counts: Counter[str],
    raw_stage_unexported_material_ref_stage_position_counts: Counter[str],
    raw_stage_gap_unexported_material_ref_stage_position_counts: Counter[str],
    exported_stage_counts: Counter[str],
    secondary_transform_kind_counts: Counter[str],
    secondary_transform_signature_counts: Counter[str],
    parse_errors: list[dict[str, object]],
    model_records: list[dict[str, object]],
    sample_records: list[dict[str, object]],
    secondary_transform_samples: list[dict[str, object]],
    raw_stage_gap_candidate_samples: dict[str, list[dict[str, object]]],
    raw_stage_non_gap_candidate_samples: dict[str, list[dict[str, object]]],
    raw_stage_non_gap_non_material_index_candidate_samples: list[dict[str, object]],
    *,
    sample_limit: int,
    include_model_records: bool,
    model_limit: int | None,
) -> None:
    model_counts["discovered"] += 1
    try:
        model = CmbModel.from_path(path)
    except Exception as exc:
        add_parse_error(parse_errors, path, romfs_root, "cmb_model", str(exc))
        model_counts["parse_errors"] += 1
        return

    record_model_materials(
        model,
        path,
        romfs_root,
        "cmb",
        model_counts,
        material_totals,
        risk_counts,
        issue_counts,
        raw_stage_selector_counts,
        raw_stage_candidate_summary_counts,
        raw_stage_gap_candidate_summary_counts,
        raw_stage_export_classification_counts,
        raw_stage_export_gap_classification_counts,
        raw_stage_export_blocker_counts,
        raw_stage_alignment_case_counts,
        raw_stage_gap_alignment_case_counts,
        raw_stage_resolution_case_counts,
        raw_stage_gap_resolution_case_counts,
        raw_stage_export_order_case_counts,
        raw_stage_gap_export_order_case_counts,
        raw_stage_slot_bounds_case_counts,
        raw_stage_gap_slot_bounds_case_counts,
        raw_stage_slot_sequence_case_counts,
        raw_stage_gap_slot_sequence_case_counts,
        raw_stage_oob_delta_counts,
        raw_stage_gap_oob_delta_counts,
        raw_stage_oob_delta_signature_counts,
        raw_stage_gap_oob_delta_signature_counts,
        raw_stage_unexported_stage_position_counts,
        raw_stage_gap_unexported_stage_position_counts,
        raw_stage_unexported_material_ref_stage_position_counts,
        raw_stage_gap_unexported_material_ref_stage_position_counts,
        exported_stage_counts,
        secondary_transform_kind_counts,
        secondary_transform_signature_counts,
        model_records,
        sample_records,
        secondary_transform_samples,
        raw_stage_gap_candidate_samples,
        raw_stage_non_gap_candidate_samples,
        raw_stage_non_gap_non_material_index_candidate_samples,
        sample_limit=sample_limit,
        include_model_records=include_model_records,
        model_limit=model_limit,
    )


def scan_zar_models(
    path: Path,
    romfs_root: Path,
    model_counts: Counter[str],
    material_totals: Counter[str],
    risk_counts: Counter[str],
    issue_counts: Counter[str],
    raw_stage_selector_counts: Counter[str],
    raw_stage_candidate_summary_counts: Counter[str],
    raw_stage_gap_candidate_summary_counts: Counter[str],
    raw_stage_export_classification_counts: Counter[str],
    raw_stage_export_gap_classification_counts: Counter[str],
    raw_stage_export_blocker_counts: Counter[str],
    raw_stage_alignment_case_counts: Counter[str],
    raw_stage_gap_alignment_case_counts: Counter[str],
    raw_stage_resolution_case_counts: Counter[str],
    raw_stage_gap_resolution_case_counts: Counter[str],
    raw_stage_export_order_case_counts: Counter[str],
    raw_stage_gap_export_order_case_counts: Counter[str],
    raw_stage_slot_bounds_case_counts: Counter[str],
    raw_stage_gap_slot_bounds_case_counts: Counter[str],
    raw_stage_slot_sequence_case_counts: Counter[str],
    raw_stage_gap_slot_sequence_case_counts: Counter[str],
    raw_stage_oob_delta_counts: Counter[str],
    raw_stage_gap_oob_delta_counts: Counter[str],
    raw_stage_oob_delta_signature_counts: Counter[str],
    raw_stage_gap_oob_delta_signature_counts: Counter[str],
    raw_stage_unexported_stage_position_counts: Counter[str],
    raw_stage_gap_unexported_stage_position_counts: Counter[str],
    raw_stage_unexported_material_ref_stage_position_counts: Counter[str],
    raw_stage_gap_unexported_material_ref_stage_position_counts: Counter[str],
    exported_stage_counts: Counter[str],
    secondary_transform_kind_counts: Counter[str],
    secondary_transform_signature_counts: Counter[str],
    parse_errors: list[dict[str, object]],
    model_records: list[dict[str, object]],
    sample_records: list[dict[str, object]],
    secondary_transform_samples: list[dict[str, object]],
    raw_stage_gap_candidate_samples: dict[str, list[dict[str, object]]],
    raw_stage_non_gap_candidate_samples: dict[str, list[dict[str, object]]],
    raw_stage_non_gap_non_material_index_candidate_samples: list[dict[str, object]],
    *,
    sample_limit: int,
    include_model_records: bool,
    model_limit: int | None,
) -> None:
    try:
        archive = ZarArchive.from_path(path)
    except Exception as exc:
        add_parse_error(parse_errors, path, romfs_root, "zar_archive", str(exc))
        return

    for file in archive.cmb_files():
        model_counts["discovered"] += 1
        try:
            model = CmbModel.parse(archive.read_file(file), f"{path}!{file.name}")
        except Exception as exc:
            add_parse_error(
                parse_errors,
                path,
                romfs_root,
                "cmb_model",
                str(exc),
                embedded_name=file.name,
            )
            model_counts["parse_errors"] += 1
            continue
        record_model_materials(
            model,
            path,
            romfs_root,
            "zar",
            model_counts,
            material_totals,
            risk_counts,
            issue_counts,
            raw_stage_selector_counts,
            raw_stage_candidate_summary_counts,
            raw_stage_gap_candidate_summary_counts,
            raw_stage_export_classification_counts,
            raw_stage_export_gap_classification_counts,
            raw_stage_export_blocker_counts,
            raw_stage_alignment_case_counts,
            raw_stage_gap_alignment_case_counts,
            raw_stage_resolution_case_counts,
            raw_stage_gap_resolution_case_counts,
            raw_stage_export_order_case_counts,
            raw_stage_gap_export_order_case_counts,
            raw_stage_slot_bounds_case_counts,
            raw_stage_gap_slot_bounds_case_counts,
            raw_stage_slot_sequence_case_counts,
            raw_stage_gap_slot_sequence_case_counts,
            raw_stage_oob_delta_counts,
            raw_stage_gap_oob_delta_counts,
            raw_stage_oob_delta_signature_counts,
            raw_stage_gap_oob_delta_signature_counts,
            raw_stage_unexported_stage_position_counts,
            raw_stage_gap_unexported_stage_position_counts,
            raw_stage_unexported_material_ref_stage_position_counts,
            raw_stage_gap_unexported_material_ref_stage_position_counts,
            exported_stage_counts,
            secondary_transform_kind_counts,
            secondary_transform_signature_counts,
            model_records,
            sample_records,
            secondary_transform_samples,
            raw_stage_gap_candidate_samples,
            raw_stage_non_gap_candidate_samples,
            raw_stage_non_gap_non_material_index_candidate_samples,
            embedded_zar_file=file,
            sample_limit=sample_limit,
            include_model_records=include_model_records,
            model_limit=model_limit,
        )


def scan_zsi_models(
    path: Path,
    romfs_root: Path,
    model_counts: Counter[str],
    material_totals: Counter[str],
    risk_counts: Counter[str],
    issue_counts: Counter[str],
    raw_stage_selector_counts: Counter[str],
    raw_stage_candidate_summary_counts: Counter[str],
    raw_stage_gap_candidate_summary_counts: Counter[str],
    raw_stage_export_classification_counts: Counter[str],
    raw_stage_export_gap_classification_counts: Counter[str],
    raw_stage_export_blocker_counts: Counter[str],
    raw_stage_alignment_case_counts: Counter[str],
    raw_stage_gap_alignment_case_counts: Counter[str],
    raw_stage_resolution_case_counts: Counter[str],
    raw_stage_gap_resolution_case_counts: Counter[str],
    raw_stage_export_order_case_counts: Counter[str],
    raw_stage_gap_export_order_case_counts: Counter[str],
    raw_stage_slot_bounds_case_counts: Counter[str],
    raw_stage_gap_slot_bounds_case_counts: Counter[str],
    raw_stage_slot_sequence_case_counts: Counter[str],
    raw_stage_gap_slot_sequence_case_counts: Counter[str],
    raw_stage_oob_delta_counts: Counter[str],
    raw_stage_gap_oob_delta_counts: Counter[str],
    raw_stage_oob_delta_signature_counts: Counter[str],
    raw_stage_gap_oob_delta_signature_counts: Counter[str],
    raw_stage_unexported_stage_position_counts: Counter[str],
    raw_stage_gap_unexported_stage_position_counts: Counter[str],
    raw_stage_unexported_material_ref_stage_position_counts: Counter[str],
    raw_stage_gap_unexported_material_ref_stage_position_counts: Counter[str],
    exported_stage_counts: Counter[str],
    secondary_transform_kind_counts: Counter[str],
    secondary_transform_signature_counts: Counter[str],
    parse_errors: list[dict[str, object]],
    model_records: list[dict[str, object]],
    sample_records: list[dict[str, object]],
    secondary_transform_samples: list[dict[str, object]],
    raw_stage_gap_candidate_samples: dict[str, list[dict[str, object]]],
    raw_stage_non_gap_candidate_samples: dict[str, list[dict[str, object]]],
    raw_stage_non_gap_non_material_index_candidate_samples: list[dict[str, object]],
    *,
    sample_limit: int,
    include_model_records: bool,
    model_limit: int | None,
) -> None:
    try:
        zsi = ZsiFile.from_path(path)
        cmbs = zsi.embedded_cmbs()
    except Exception as exc:
        add_parse_error(parse_errors, path, romfs_root, "zsi_file", str(exc))
        return

    for cmb in cmbs:
        model_counts["discovered"] += 1
        record_model_materials(
            cmb.model,
            path,
            romfs_root,
            "zsi",
            model_counts,
            material_totals,
            risk_counts,
            issue_counts,
            raw_stage_selector_counts,
            raw_stage_candidate_summary_counts,
            raw_stage_gap_candidate_summary_counts,
            raw_stage_export_classification_counts,
            raw_stage_export_gap_classification_counts,
            raw_stage_export_blocker_counts,
            raw_stage_alignment_case_counts,
            raw_stage_gap_alignment_case_counts,
            raw_stage_resolution_case_counts,
            raw_stage_gap_resolution_case_counts,
            raw_stage_export_order_case_counts,
            raw_stage_gap_export_order_case_counts,
            raw_stage_slot_bounds_case_counts,
            raw_stage_gap_slot_bounds_case_counts,
            raw_stage_slot_sequence_case_counts,
            raw_stage_gap_slot_sequence_case_counts,
            raw_stage_oob_delta_counts,
            raw_stage_gap_oob_delta_counts,
            raw_stage_oob_delta_signature_counts,
            raw_stage_gap_oob_delta_signature_counts,
            raw_stage_unexported_stage_position_counts,
            raw_stage_gap_unexported_stage_position_counts,
            raw_stage_unexported_material_ref_stage_position_counts,
            raw_stage_gap_unexported_material_ref_stage_position_counts,
            exported_stage_counts,
            secondary_transform_kind_counts,
            secondary_transform_signature_counts,
            model_records,
            sample_records,
            secondary_transform_samples,
            raw_stage_gap_candidate_samples,
            raw_stage_non_gap_candidate_samples,
            raw_stage_non_gap_non_material_index_candidate_samples,
            embedded_index=cmb.index,
            embedded_offset=cmb.offset,
            embedded_size=cmb.size,
            sample_limit=sample_limit,
            include_model_records=include_model_records,
            model_limit=model_limit,
        )


def record_model_materials(
    model: CmbModel,
    path: Path,
    romfs_root: Path,
    container_type: str,
    model_counts: Counter[str],
    material_totals: Counter[str],
    risk_counts: Counter[str],
    issue_counts: Counter[str],
    raw_stage_selector_counts: Counter[str],
    raw_stage_candidate_summary_counts: Counter[str],
    raw_stage_gap_candidate_summary_counts: Counter[str],
    raw_stage_export_classification_counts: Counter[str],
    raw_stage_export_gap_classification_counts: Counter[str],
    raw_stage_export_blocker_counts: Counter[str],
    raw_stage_alignment_case_counts: Counter[str],
    raw_stage_gap_alignment_case_counts: Counter[str],
    raw_stage_resolution_case_counts: Counter[str],
    raw_stage_gap_resolution_case_counts: Counter[str],
    raw_stage_export_order_case_counts: Counter[str],
    raw_stage_gap_export_order_case_counts: Counter[str],
    raw_stage_slot_bounds_case_counts: Counter[str],
    raw_stage_gap_slot_bounds_case_counts: Counter[str],
    raw_stage_slot_sequence_case_counts: Counter[str],
    raw_stage_gap_slot_sequence_case_counts: Counter[str],
    raw_stage_oob_delta_counts: Counter[str],
    raw_stage_gap_oob_delta_counts: Counter[str],
    raw_stage_oob_delta_signature_counts: Counter[str],
    raw_stage_gap_oob_delta_signature_counts: Counter[str],
    raw_stage_unexported_stage_position_counts: Counter[str],
    raw_stage_gap_unexported_stage_position_counts: Counter[str],
    raw_stage_unexported_material_ref_stage_position_counts: Counter[str],
    raw_stage_gap_unexported_material_ref_stage_position_counts: Counter[str],
    exported_stage_counts: Counter[str],
    secondary_transform_kind_counts: Counter[str],
    secondary_transform_signature_counts: Counter[str],
    model_records: list[dict[str, object]],
    sample_records: list[dict[str, object]],
    secondary_transform_samples: list[dict[str, object]],
    raw_stage_gap_candidate_samples: dict[str, list[dict[str, object]]],
    raw_stage_non_gap_candidate_samples: dict[str, list[dict[str, object]]],
    raw_stage_non_gap_non_material_index_candidate_samples: list[dict[str, object]],
    *,
    embedded_zar_file: ZarFile | None = None,
    embedded_index: int | None = None,
    embedded_offset: int | None = None,
    embedded_size: int | None = None,
    sample_limit: int,
    include_model_records: bool,
    model_limit: int | None,
) -> None:
    if model_limit is not None and model_counts["parsed"] >= model_limit:
        model_counts["skipped_by_model_limit"] += 1
        return

    model_counts["parsed"] += 1
    static_candidate = model.is_static_candidate()
    model_counts["static_candidate" if static_candidate else "nonstatic_candidate"] += 1
    source = source_record(
        path,
        romfs_root,
        container_type,
        embedded_zar_file=embedded_zar_file,
        embedded_index=embedded_index,
        embedded_offset=embedded_offset,
        embedded_size=embedded_size,
    )
    model_totals = new_material_totals()
    resource_root = f"inventory/{source['container_path'].rsplit('.', 1)[0]}"
    symbol = f"gOot3dMaterialInventory{model_counts['parsed']}"

    for material in model.materials:
        record = material_audit_record(model, material, resource_root, symbol)
        issues = material_issues(record)
        add_material_record_counts(
            record,
            issues,
            material_totals,
            risk_counts,
            issue_counts,
            raw_stage_selector_counts,
            raw_stage_candidate_summary_counts,
            raw_stage_gap_candidate_summary_counts,
            raw_stage_export_classification_counts,
            raw_stage_export_gap_classification_counts,
            raw_stage_export_blocker_counts,
            raw_stage_alignment_case_counts,
            raw_stage_gap_alignment_case_counts,
            raw_stage_resolution_case_counts,
            raw_stage_gap_resolution_case_counts,
            raw_stage_export_order_case_counts,
            raw_stage_gap_export_order_case_counts,
            raw_stage_slot_bounds_case_counts,
            raw_stage_gap_slot_bounds_case_counts,
            raw_stage_slot_sequence_case_counts,
            raw_stage_gap_slot_sequence_case_counts,
            raw_stage_oob_delta_counts,
            raw_stage_gap_oob_delta_counts,
            raw_stage_oob_delta_signature_counts,
            raw_stage_gap_oob_delta_signature_counts,
            raw_stage_unexported_stage_position_counts,
            raw_stage_gap_unexported_stage_position_counts,
            raw_stage_unexported_material_ref_stage_position_counts,
            raw_stage_gap_unexported_material_ref_stage_position_counts,
            exported_stage_counts,
        )
        add_material_record_counts(
            record,
            issues,
            model_totals,
            Counter(),
            Counter(),
            Counter(),
            Counter(),
            Counter(),
            Counter(),
            Counter(),
            Counter(),
            Counter(),
            Counter(),
            Counter(),
            Counter(),
            Counter(),
            Counter(),
            Counter(),
            Counter(),
            Counter(),
            Counter(),
            Counter(),
            Counter(),
            Counter(),
            Counter(),
            Counter(),
            Counter(),
            Counter(),
            Counter(),
            Counter(),
        )
        if should_sample_material(record, issues) and len(sample_records) < sample_limit:
            sample_records.append(
                compact_material_sample(
                    source,
                    model,
                    static_candidate,
                    record,
                    issues,
                )
            )
        if record["raw_texture_stage_export_gap"] > 0:
            add_raw_stage_gap_candidate_sample(
                raw_stage_gap_candidate_samples,
                source,
                model,
                static_candidate,
                record,
                issues,
                sample_limit=sample_limit,
            )
        elif record["raw_texture_stage_unexported_valid_textures"]:
            add_raw_stage_non_gap_candidate_sample(
                raw_stage_non_gap_candidate_samples,
                source,
                model,
                static_candidate,
                record,
                issues,
                sample_limit=sample_limit,
            )
        add_raw_stage_non_gap_non_material_index_candidate_sample(
            raw_stage_non_gap_non_material_index_candidate_samples,
            source,
            model,
            static_candidate,
            record,
            issues,
            sample_limit=sample_limit,
        )
        if record["selected_secondary_texture_coord_transformed"]:
            add_secondary_transform_counts(
                record,
                secondary_transform_kind_counts,
                secondary_transform_signature_counts,
            )
            if len(secondary_transform_samples) < sample_limit:
                secondary_transform_samples.append(
                    compact_secondary_transform_sample(
                        source,
                        model,
                        static_candidate,
                        record,
                        issues,
                    )
                )

    if include_model_records:
        model_records.append(
            {
                **source,
                "model_name": model.name,
                "static_candidate": static_candidate,
                "texture_count": len(model.textures),
                "material_count": model_totals["material_count"],
                "mesh_count": len(model.meshes),
                "multi_texture_material_count": model_totals["multi_texture_material_count"],
                "raw_texture_stage_mapper_mismatch_count": model_totals[
                    "raw_texture_stage_mapper_mismatch_count"
                ],
                "raw_texture_stage_export_gap_material_count": model_totals[
                    "raw_texture_stage_export_gap_material_count"
                ],
                "raw_texture_stage_export_gap_total": model_totals[
                    "raw_texture_stage_export_gap_total"
                ],
                "raw_texture_stage_baked_extra_material_count": model_totals[
                    "raw_texture_stage_baked_extra_material_count"
                ],
                "raw_texture_stage_baked_extra_total": model_totals[
                    "raw_texture_stage_baked_extra_total"
                ],
                "texture_stage_risk_material_count": model_totals[
                    "texture_stage_risk_material_count"
                ],
            }
        )


def add_material_record_counts(
    record: dict[str, object],
    issues: list[str],
    totals: Counter[str],
    risk_counts: Counter[str],
    issue_counts: Counter[str],
    raw_stage_selector_counts: Counter[str],
    raw_stage_candidate_summary_counts: Counter[str],
    raw_stage_gap_candidate_summary_counts: Counter[str],
    raw_stage_export_classification_counts: Counter[str],
    raw_stage_export_gap_classification_counts: Counter[str],
    raw_stage_export_blocker_counts: Counter[str],
    raw_stage_alignment_case_counts: Counter[str],
    raw_stage_gap_alignment_case_counts: Counter[str],
    raw_stage_resolution_case_counts: Counter[str],
    raw_stage_gap_resolution_case_counts: Counter[str],
    raw_stage_export_order_case_counts: Counter[str],
    raw_stage_gap_export_order_case_counts: Counter[str],
    raw_stage_slot_bounds_case_counts: Counter[str],
    raw_stage_gap_slot_bounds_case_counts: Counter[str],
    raw_stage_slot_sequence_case_counts: Counter[str],
    raw_stage_gap_slot_sequence_case_counts: Counter[str],
    raw_stage_oob_delta_counts: Counter[str],
    raw_stage_gap_oob_delta_counts: Counter[str],
    raw_stage_oob_delta_signature_counts: Counter[str],
    raw_stage_gap_oob_delta_signature_counts: Counter[str],
    raw_stage_unexported_stage_position_counts: Counter[str],
    raw_stage_gap_unexported_stage_position_counts: Counter[str],
    raw_stage_unexported_material_ref_stage_position_counts: Counter[str],
    raw_stage_gap_unexported_material_ref_stage_position_counts: Counter[str],
    exported_stage_counts: Counter[str],
) -> None:
    totals["material_count"] += 1
    if record["selected_primary_texture"] is not None:
        totals["textured_material_count"] += 1
    if record["active_texture_slot_count"] > 1:
        totals["multi_texture_material_count"] += 1
    if record["alpha_test"]:
        totals["alpha_test_material_count"] += 1
    if record["blended"]:
        totals["blended_material_count"] += 1
    if record["selected_primary_texture_coord_transformed"]:
        totals["primary_texture_coord_transform_count"] += 1
    if record["selected_secondary_texture_coord_transformed"]:
        totals["secondary_texture_coord_transform_count"] += 1
    baked_extra_count = int(record.get("raw_texture_stage_baked_extra_count", 0) or 0)
    if baked_extra_count > 0:
        totals["raw_texture_stage_baked_extra_material_count"] += 1
        totals["raw_texture_stage_baked_extra_total"] += baked_extra_count

    selector = record["raw_texture_stage_selector"]
    raw_stage_selector_counts[f"stage_count_{selector['stage_count']}"] += 1
    candidate_key = raw_stage_candidate_summary_key(record["raw_texture_stage_candidate_summary"])
    raw_stage_candidate_summary_counts[candidate_key] += 1
    classification = record["raw_texture_stage_export_classification"]
    classification_status = str(classification["status"])
    raw_stage_export_classification_counts[classification_status] += 1
    alignment_case = str(record["raw_texture_stage_alignment_case"])
    raw_stage_alignment_case_counts[alignment_case] += 1
    resolution_case = str(record["raw_texture_stage_resolution_case"])
    raw_stage_resolution_case_counts[resolution_case] += 1
    export_order_case = str(record["raw_texture_stage_export_order_case"])
    raw_stage_export_order_case_counts[export_order_case] += 1
    f3d_limit_case = str(record["raw_texture_stage_f3d_limit_case"])
    if f3d_limit_case != "not_f3d_limit":
        totals[f"raw_stage_f3d_limit_case_count:{f3d_limit_case}"] += 1
    bounds = record["raw_texture_stage_slot_bounds"]
    bounds_case = str(bounds["case"])
    sequence_case = str(bounds["sequence_case"])
    oob_delta_signature = str(bounds["oob_delta_signature"])
    raw_stage_slot_bounds_case_counts[bounds_case] += 1
    raw_stage_slot_sequence_case_counts[sequence_case] += 1
    raw_stage_oob_delta_signature_counts[oob_delta_signature] += 1
    for delta in bounds["oob_deltas_from_texture_count"]:
        raw_stage_oob_delta_counts[f"delta_{delta}"] += 1
    for texture in record["raw_texture_stage_unexported_valid_textures"]:
        key = f"stage_position_{texture['stage_position']}"
        raw_stage_unexported_stage_position_counts[key] += 1
    for material_ref in record["raw_texture_stage_unexported_material_refs"]:
        key = f"stage_position_{material_ref['stage_position']}"
        raw_stage_unexported_material_ref_stage_position_counts[key] += 1
    for material_ref in record["raw_texture_stage_unexported_material_index_refs"]:
        key = f"stage_position_{material_ref['stage_position']}"
        totals[f"raw_stage_unexported_material_index_ref_stage_position_count:{key}"] += 1
    for blocker in classification["blockers"]:
        raw_stage_export_blocker_counts[str(blocker)] += 1
    exported_stage_counts[f"stage_count_{record['exported_texture_stage_count']}"] += 1
    if not selector["matches_texture_mappers_used"]:
        totals["raw_texture_stage_mapper_mismatch_count"] += 1
    if record["raw_texture_stage_export_gap"] > 0:
        totals["raw_texture_stage_export_gap_material_count"] += 1
        totals["raw_texture_stage_export_gap_total"] += record["raw_texture_stage_export_gap"]
        raw_stage_gap_candidate_summary_counts[candidate_key] += 1
        raw_stage_export_gap_classification_counts[classification_status] += 1
        raw_stage_gap_alignment_case_counts[alignment_case] += 1
        raw_stage_gap_resolution_case_counts[resolution_case] += 1
        raw_stage_gap_export_order_case_counts[export_order_case] += 1
        raw_stage_gap_slot_bounds_case_counts[bounds_case] += 1
        raw_stage_gap_slot_sequence_case_counts[sequence_case] += 1
        raw_stage_gap_oob_delta_signature_counts[oob_delta_signature] += 1
        for delta in bounds["oob_deltas_from_texture_count"]:
            raw_stage_gap_oob_delta_counts[f"delta_{delta}"] += 1
        for texture in record["raw_texture_stage_unexported_valid_textures"]:
            key = f"stage_position_{texture['stage_position']}"
            raw_stage_gap_unexported_stage_position_counts[key] += 1
        for material_ref in record["raw_texture_stage_unexported_material_refs"]:
            key = f"stage_position_{material_ref['stage_position']}"
            raw_stage_gap_unexported_material_ref_stage_position_counts[key] += 1
        for material_ref in record["raw_texture_stage_unexported_material_index_refs"]:
            key = f"stage_position_{material_ref['stage_position']}"
            totals[
                f"raw_stage_export_gap_unexported_material_index_ref_stage_position_count:{key}"
            ] += 1
    if record["texture_stage_risk_tags"]:
        totals["texture_stage_risk_material_count"] += 1
    for risk in record["texture_stage_risk_tags"]:
        risk_counts[risk] += 1
    for issue in issues:
        issue_counts[issue] += 1


def raw_stage_candidate_summary_key(summary: object) -> str:
    if not isinstance(summary, dict):
        return "unknown"
    primary = bool(summary.get("selected_primary_in_raw"))
    secondary = bool(summary.get("selected_secondary_in_raw"))
    return (
        f"stage={int(summary.get('stage_count', 0) or 0)};"
        f"valid={int(summary.get('valid_texture_candidate_count', 0) or 0)};"
        f"exported={int(summary.get('exported_candidate_count', 0) or 0)};"
        f"unexported={int(summary.get('unexported_valid_texture_candidate_count', 0) or 0)};"
        f"active={int(summary.get('active_mapper_candidate_count', 0) or 0)};"
        f"primary={'yes' if primary else 'no'};"
        f"secondary={'yes' if secondary else 'no'}"
    )


def add_raw_stage_gap_candidate_sample(
    samples_by_pattern: dict[str, list[dict[str, object]]],
    source: dict[str, object],
    model: CmbModel,
    static_candidate: bool,
    record: dict[str, object],
    issues: list[str],
    *,
    sample_limit: int,
) -> None:
    if sample_limit <= 0:
        return
    key = raw_stage_candidate_summary_key(record["raw_texture_stage_candidate_summary"])
    samples = samples_by_pattern.setdefault(key, [])
    if len(samples) >= min(sample_limit, RAW_STAGE_GAP_PATTERN_SAMPLE_LIMIT):
        return
    samples.append(
        compact_raw_stage_gap_candidate_sample(
            source,
            model,
            static_candidate,
            record,
            issues,
            pattern=key,
        )
    )


def add_raw_stage_non_gap_candidate_sample(
    samples_by_pattern: dict[str, list[dict[str, object]]],
    source: dict[str, object],
    model: CmbModel,
    static_candidate: bool,
    record: dict[str, object],
    issues: list[str],
    *,
    sample_limit: int,
) -> None:
    if sample_limit <= 0:
        return
    key = raw_stage_candidate_summary_key(record["raw_texture_stage_candidate_summary"])
    samples = samples_by_pattern.setdefault(key, [])
    if len(samples) >= min(sample_limit, RAW_STAGE_GAP_PATTERN_SAMPLE_LIMIT):
        return
    samples.append(
        compact_raw_stage_gap_candidate_sample(
            source,
            model,
            static_candidate,
            record,
            issues,
            pattern=key,
        )
    )


def add_raw_stage_non_gap_non_material_index_candidate_sample(
    samples: list[dict[str, object]],
    source: dict[str, object],
    model: CmbModel,
    static_candidate: bool,
    record: dict[str, object],
    issues: list[str],
    *,
    sample_limit: int,
) -> None:
    if sample_limit <= 0 or len(samples) >= sample_limit:
        return
    candidates = raw_stage_non_gap_non_material_index_candidates(record)
    if not candidates:
        return
    sample = compact_raw_stage_gap_candidate_sample(
        source,
        model,
        static_candidate,
        record,
        issues,
        pattern=raw_stage_candidate_summary_key(
            record["raw_texture_stage_candidate_summary"]
        ),
    )
    sample["raw_texture_stage_non_gap_non_material_index_candidates"] = candidates
    samples.append(sample)


def raw_stage_non_gap_non_material_index_candidates(
    record: dict[str, object],
) -> list[dict[str, object]]:
    if int(record["raw_texture_stage_export_gap"]) > 0:
        return []
    return [
        candidate
        for candidate in record["raw_texture_stage_texture_candidates"]
        if (
            candidate["valid_texture_index"]
            and not candidate["currently_exported"]
            and not candidate["valid_material_index"]
        )
    ]


def compact_raw_stage_gap_candidate_sample(
    source: dict[str, object],
    model: CmbModel,
    static_candidate: bool,
    record: dict[str, object],
    issues: list[str],
    *,
    pattern: str,
) -> dict[str, object]:
    return {
        **source,
        "model_name": model.name,
        "static_candidate": static_candidate,
        "material_index": record["material_index"],
        "pattern": pattern,
        "texture_indices": record["texture_indices"],
        "texture_mappers_used": record["texture_mappers_used"],
        "active_texture_slot_count": record["active_texture_slot_count"],
        "selected_primary_texture": record["selected_primary_texture"],
        "selected_secondary_texture": record["selected_secondary_texture"],
        "raw_texture_stage_selector": record["raw_texture_stage_selector"],
        "raw_texture_stage_slot_bounds": record["raw_texture_stage_slot_bounds"],
        "raw_texture_stage_candidate_summary": record["raw_texture_stage_candidate_summary"],
        "raw_texture_stage_texture_candidates": record["raw_texture_stage_texture_candidates"],
        "raw_texture_stage_unexported_valid_textures": record[
            "raw_texture_stage_unexported_valid_textures"
        ],
        "raw_texture_stage_unexported_material_refs": record[
            "raw_texture_stage_unexported_material_refs"
        ],
        "raw_texture_stage_unexported_material_index_refs": record[
            "raw_texture_stage_unexported_material_index_refs"
        ],
        "exported_texture_stage_count": record["exported_texture_stage_count"],
        "raw_texture_stage_export_gap": record["raw_texture_stage_export_gap"],
        "raw_texture_stage_export_classification": record[
            "raw_texture_stage_export_classification"
        ],
        "raw_texture_stage_alignment_case": record["raw_texture_stage_alignment_case"],
        "raw_texture_stage_resolution_case": record["raw_texture_stage_resolution_case"],
        "raw_texture_stage_export_order_case": record["raw_texture_stage_export_order_case"],
        "raw_texture_stage_f3d_limit_case": record["raw_texture_stage_f3d_limit_case"],
        "texture_stage_risk_tags": record["texture_stage_risk_tags"],
        "issues": issues,
    }


def compact_material_sample(
    source: dict[str, object],
    model: CmbModel,
    static_candidate: bool,
    record: dict[str, object],
    issues: list[str],
) -> dict[str, object]:
    return {
        **source,
        "model_name": model.name,
        "static_candidate": static_candidate,
        "material_index": record["material_index"],
        "texture_indices": record["texture_indices"],
        "texture_mappers_used": record["texture_mappers_used"],
        "texture_coords_used": record["texture_coords_used"],
        "active_texture_slot_count": record["active_texture_slot_count"],
        "selected_primary_slot": record["selected_primary_slot"],
        "selected_primary_texture": record["selected_primary_texture"],
        "selected_primary_texture_coord": record["selected_primary_texture_coord"],
        "selected_primary_texture_coord_transformed": record[
            "selected_primary_texture_coord_transformed"
        ],
        "selected_secondary_slot": record["selected_secondary_slot"],
        "selected_secondary_texture": record["selected_secondary_texture"],
        "selected_secondary_texture_export_path": record["selected_secondary_texture_export_path"],
        "selected_secondary_texture_coord": record["selected_secondary_texture_coord"],
        "selected_secondary_texture_coord_transformed": record[
            "selected_secondary_texture_coord_transformed"
        ],
        "selected_secondary_texture_coord_export_status": record[
            "selected_secondary_texture_coord_export_status"
        ],
        "raw_texture_stage_selector": record["raw_texture_stage_selector"],
        "raw_texture_stage_slot_bounds": record["raw_texture_stage_slot_bounds"],
        "raw_texture_stage_candidate_summary": record["raw_texture_stage_candidate_summary"],
        "raw_texture_stage_unexported_valid_textures": record[
            "raw_texture_stage_unexported_valid_textures"
        ],
        "raw_texture_stage_unexported_material_refs": record[
            "raw_texture_stage_unexported_material_refs"
        ],
        "raw_texture_stage_unexported_material_index_refs": record[
            "raw_texture_stage_unexported_material_index_refs"
        ],
        "exported_texture_stage_count": record["exported_texture_stage_count"],
        "raw_texture_stage_export_gap": record["raw_texture_stage_export_gap"],
        "raw_texture_stage_export_classification": record[
            "raw_texture_stage_export_classification"
        ],
        "raw_texture_stage_alignment_case": record["raw_texture_stage_alignment_case"],
        "raw_texture_stage_resolution_case": record["raw_texture_stage_resolution_case"],
        "raw_texture_stage_export_order_case": record["raw_texture_stage_export_order_case"],
        "raw_texture_stage_f3d_limit_case": record["raw_texture_stage_f3d_limit_case"],
        "texture_stage_risk_tags": record["texture_stage_risk_tags"],
        "issues": issues,
    }


def should_sample_material(record: dict[str, object], issues: list[str]) -> bool:
    return bool(record["texture_stage_risk_tags"] or issues)


def add_secondary_transform_counts(
    record: dict[str, object],
    kind_counts: Counter[str],
    signature_counts: Counter[str],
) -> None:
    coord = record["selected_secondary_texture_coord"]
    if not isinstance(coord, dict):
        return
    kind_counts[secondary_transform_kind(coord)] += 1
    signature_counts[secondary_transform_signature(coord)] += 1


def compact_secondary_transform_sample(
    source: dict[str, object],
    model: CmbModel,
    static_candidate: bool,
    record: dict[str, object],
    issues: list[str],
) -> dict[str, object]:
    coord = record["selected_secondary_texture_coord"]
    return {
        **source,
        "model_name": model.name,
        "static_candidate": static_candidate,
        "material_index": record["material_index"],
        "selected_primary_texture": record["selected_primary_texture"],
        "selected_secondary_texture": record["selected_secondary_texture"],
        "selected_secondary_texture_export_path": record["selected_secondary_texture_export_path"],
        "selected_secondary_slot": record["selected_secondary_slot"],
        "selected_secondary_texture_coord": coord,
        "selected_secondary_texture_coord_export_status": record[
            "selected_secondary_texture_coord_export_status"
        ],
        "secondary_texture_coord_transform_kind": (
            secondary_transform_kind(coord) if isinstance(coord, dict) else None
        ),
        "secondary_texture_coord_transform_signature": (
            secondary_transform_signature(coord) if isinstance(coord, dict) else None
        ),
        "raw_texture_stage_selector": record["raw_texture_stage_selector"],
        "raw_texture_stage_candidate_summary": record["raw_texture_stage_candidate_summary"],
        "raw_texture_stage_export_gap": record["raw_texture_stage_export_gap"],
        "raw_texture_stage_export_classification": record[
            "raw_texture_stage_export_classification"
        ],
        "texture_stage_risk_tags": record["texture_stage_risk_tags"],
        "issues": issues,
    }


def secondary_transform_kind(coord: dict[str, object]) -> str:
    parts: list[str] = []
    if int(coord.get("coordinate_index", 0)) != 0:
        parts.append("non_uv0")
    scale = coord.get("scale", [1.0, 1.0])
    translation = coord.get("translation", [0.0, 0.0])
    rotation = float(coord.get("rotation", 0.0))
    if (
        isinstance(scale, list)
        and len(scale) >= 2
        and (not nearly_equal(scale[0], 1.0) or not nearly_equal(scale[1], 1.0))
    ):
        parts.append("scale")
    if abs(rotation) > FLOAT_EPSILON:
        parts.append("rotation")
    if (
        isinstance(translation, list)
        and len(translation) >= 2
        and (not nearly_equal(translation[0], 0.0) or not nearly_equal(translation[1], 0.0))
    ):
        parts.append("translation")
    return "_".join(parts) if parts else "identity"


def secondary_transform_signature(coord: dict[str, object]) -> str:
    scale = coord.get("scale", [1.0, 1.0])
    translation = coord.get("translation", [0.0, 0.0])
    return (
        f"coord={coord.get('coordinate_index', 0)};"
        f"matrix={coord.get('matrix_mode', 0)};"
        f"mapping={coord.get('mapping_method', 0)};"
        f"scale={format_float(scale[0])},{format_float(scale[1])};"
        f"rotation={format_float(coord.get('rotation', 0.0))};"
        f"translation={format_float(translation[0])},{format_float(translation[1])}"
    )


def nearly_equal(actual: object, expected: float) -> bool:
    return abs(float(actual) - expected) <= FLOAT_EPSILON


def format_float(value: object) -> str:
    return f"{float(value):.6g}"


def top_counter(counter: Counter[str], limit: int) -> dict[str, int]:
    items = sorted(counter.items(), key=lambda item: (-item[1], item[0]))
    return {key: value for key, value in items[:limit]}


def totals_prefixed_counter(totals: Counter[str], prefix: str) -> Counter[str]:
    return Counter(
        {
            key[len(prefix) :]: count
            for key, count in totals.items()
            if key.startswith(prefix) and count > 0
        }
    )


def positive_counter_delta(
    total_counts: Counter[str],
    subset_counts: Counter[str],
) -> Counter[str]:
    counts = Counter(total_counts)
    counts.subtract(subset_counts)
    return Counter({key: count for key, count in counts.items() if count > 0})


def new_material_totals() -> Counter[str]:
    return Counter(
        {
            "material_count": 0,
            "textured_material_count": 0,
            "multi_texture_material_count": 0,
            "alpha_test_material_count": 0,
            "blended_material_count": 0,
            "primary_texture_coord_transform_count": 0,
            "secondary_texture_coord_transform_count": 0,
            "raw_texture_stage_mapper_mismatch_count": 0,
            "raw_texture_stage_export_gap_material_count": 0,
            "raw_texture_stage_export_gap_total": 0,
            "raw_texture_stage_baked_extra_material_count": 0,
            "raw_texture_stage_baked_extra_total": 0,
            "texture_stage_risk_material_count": 0,
        }
    )


def source_record(
    path: Path,
    romfs_root: Path,
    container_type: str,
    *,
    embedded_zar_file: ZarFile | None = None,
    embedded_index: int | None = None,
    embedded_offset: int | None = None,
    embedded_size: int | None = None,
) -> dict[str, object]:
    return {
        "container_path": path.relative_to(romfs_root).as_posix(),
        "container_type": container_type,
        "embedded_name": embedded_zar_file.name if embedded_zar_file is not None else None,
        "embedded_index": embedded_zar_file.index if embedded_zar_file is not None else embedded_index,
        "embedded_offset": embedded_zar_file.offset if embedded_zar_file is not None else embedded_offset,
        "embedded_size": embedded_zar_file.size if embedded_zar_file is not None else embedded_size,
    }


def add_parse_error(
    parse_errors: list[dict[str, object]],
    path: Path,
    romfs_root: Path,
    kind: str,
    error: str,
    *,
    embedded_name: str | None = None,
) -> None:
    parse_errors.append(
        {
            "path": path.relative_to(romfs_root).as_posix(),
            "kind": kind,
            "embedded_name": embedded_name,
            "error": error,
        }
    )
