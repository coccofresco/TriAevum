from __future__ import annotations

from collections import Counter
import json
import math
from pathlib import Path
import zipfile

from .binary import ParseError
from .cmb import CmbModel, Texture
from .romfs_inventory import sorted_counter
from .legacy_fast_resource import (
    TEXTURE_ORIENTATION_NORMAL,
    Matrix4,
    Vec3,
    matrix_rotate_x,
    matrix_rotate_y,
    matrix_rotate_z,
    matrix_scale,
    matrix_translate,
    multiply_matrix,
    transform_direction,
    transform_position,
    write_texture_resource_image,
)
from .skinned_animation import (
    add_vec3_bounds,
    invert_affine_matrix,
    list_to_vec3,
    rounded_vec3_bounds,
    vec3_distance,
    weighted_pose_preview,
)
from .skinned_export import load_cmb_payload, vec3_is_finite
from .zar import ZarArchive


CMAB_STRT_MAGIC = b"strt"
CMAB_TXPT_MAGIC = b"txpt"
CMAB_STRING_TABLE_OFFSET_CANDIDATE = 0x18
CMAB_TEXTURE_DATA_OFFSET_CANDIDATE = 0x1C
CMAB_TXPT_RECORD_SIZE = 24


ROOT_ARCHIVE_MANIFEST = "manifest.json"
CHARACTER_CONVERSION_ARCHIVE_MANIFEST = "oot3d_character_conversion_manifest.json"
SKINNED_BINDING_ARCHIVE_MANIFEST = "oot3d_skinned_animation_binding_manifest.json"
N64_REFERENCE_ARCHIVE_AUDIT = "oot3d_n64_animation_reference_audit.json"
SKINNED_BIND_POSE_NATIVE_MANIFEST_FORMAT = "oot3d_skinned_bind_pose_native_export_v1"
CHARACTER_RUNTIME_SEMANTICS_FORMAT = "oot3d_character_runtime_semantics_v1"
ANIMATION_TIME_SOURCE_CONTRACT_FORMAT = "oot3d_character_animation_time_source_contract_v1"
ANIMATION_SEMANTIC_BINDING_CONTRACT_FORMAT = "oot3d_character_animation_semantic_binding_contract_v1"
ANIMATION_CONTROLLER_CONTRACT_FORMAT = "oot3d_character_animation_controller_contract_v1"
ROOT_MOTION_OWNERSHIP_CONTRACT_FORMAT = "oot3d_character_root_motion_ownership_contract_v1"
SKEL_ANIME_SAMPLING_CONTRACT_FORMAT = "oot3d_skel_anime_sampling_contract_v1"
CONSTANT_TRANSLATION_BIND_ABSOLUTE_TOLERANCE = 0.5
CONSTANT_TRANSLATION_BIND_RELATIVE_TOLERANCE = 0.01


def pack_character_conversion_manifest(
    character_manifest_path: Path,
    output_path: Path,
    *,
    name: str = "OOT3D Character Conversion Profile",
    author: str = "local",
    version: str = "0.1.0",
    runtime_profile_path: str | None = None,
    segment_continuity_audit_path: Path | None = None,
    anb_export_path: Path | None = None,
    anb_semantic_audit_path: Path | None = None,
    runtime_semantics_path: Path | None = None,
) -> Path:
    if not character_manifest_path.is_file():
        raise ParseError(f"{character_manifest_path}: character conversion manifest not found")
    if output_path.suffix.lower() != ".o2r":
        raise ValueError(f"{output_path}: output path must end in .o2r")

    character_manifest = read_json(character_manifest_path)
    skinned_binding_path = source_manifest_path(character_manifest, "skinned_animation_binding")
    skinned_binding = read_json(skinned_binding_path)
    target = target_from_character_manifest(character_manifest, skinned_binding)
    resource_records = character_native_resource_records(
        character_manifest,
        target,
        generated_resource_dir=generated_character_resource_dir(character_manifest_path, character_manifest),
        anb_export_path=anb_export_path,
    )
    runtime_profile_archive_path = normalize_resource_path(
        runtime_profile_path or default_runtime_profile_path(character_manifest)
    )
    segment_continuity_audit = read_optional_json(segment_continuity_audit_path)
    anb_export = read_optional_json(anb_export_path)
    anb_semantic_audit = read_optional_json(anb_semantic_audit_path)
    runtime_semantics = read_optional_json(runtime_semantics_path)
    runtime_profile = character_runtime_profile(
        character_manifest,
        target,
        resource_records,
        segment_continuity_audit=segment_continuity_audit,
        anb_export=anb_export,
        anb_semantic_audit=anb_semantic_audit,
        runtime_semantics=runtime_semantics,
    )

    root_manifest = {
        "name": name,
        "author": author,
        "version": version,
        "description": "OOT3D character profile resources for gated Shipwright runtime testing.",
        "license": "Generated from user-provided local assets; do not redistribute without rights.",
        "code_version": 1,
    }

    output_path.parent.mkdir(parents=True, exist_ok=True)
    written_paths: set[str] = set()
    with zipfile.ZipFile(output_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        write_archive_json(archive, ROOT_ARCHIVE_MANIFEST, root_manifest)
        write_archive_json(archive, CHARACTER_CONVERSION_ARCHIVE_MANIFEST, character_manifest)
        write_archive_json(archive, SKINNED_BINDING_ARCHIVE_MANIFEST, skinned_binding)
        write_archive_json(archive, runtime_profile_archive_path, runtime_profile)
        written_paths.update(
            {
                ROOT_ARCHIVE_MANIFEST,
                CHARACTER_CONVERSION_ARCHIVE_MANIFEST,
                SKINNED_BINDING_ARCHIVE_MANIFEST,
                runtime_profile_archive_path,
            }
        )

        n64_reference_path = optional_source_manifest_path(character_manifest, "n64_animation_reference_audit")
        if n64_reference_path is not None and n64_reference_path.is_file():
            write_archive_json(archive, N64_REFERENCE_ARCHIVE_AUDIT, read_json(n64_reference_path))
            written_paths.add(N64_REFERENCE_ARCHIVE_AUDIT)

        for record in resource_records:
            archive_path = str(record["resource_path"])
            if archive_path in written_paths:
                continue
            source_path = Path(str(record["source_path"]))
            if not source_path.is_file():
                raise ValueError(f"missing character resource source for {archive_path}: {source_path}")
            archive.write(source_path, archive_path)
            written_paths.add(archive_path)

    return output_path


def audit_character_conversion_package(
    character_manifest_path: Path,
    archive_path: Path,
    output_path: Path | None = None,
    *,
    runtime_profile_path: str | None = None,
    segment_continuity_audit_path: Path | None = None,
    anb_export_path: Path | None = None,
    anb_semantic_audit_path: Path | None = None,
    runtime_semantics_path: Path | None = None,
    sample_limit: int = 100,
) -> dict[str, object]:
    if not character_manifest_path.is_file():
        raise ParseError(f"{character_manifest_path}: character conversion manifest not found")
    if not archive_path.is_file():
        raise ParseError(f"{archive_path}: O2R archive not found")

    character_manifest = read_json(character_manifest_path)
    skinned_binding_path = source_manifest_path(character_manifest, "skinned_animation_binding")
    skinned_binding = read_json(skinned_binding_path)
    target = target_from_character_manifest(character_manifest, skinned_binding)
    resource_records = character_native_resource_records(
        character_manifest,
        target,
        generated_resource_dir=generated_character_resource_dir(character_manifest_path, character_manifest),
        anb_export_path=anb_export_path,
    )
    segment_continuity_audit = read_optional_json(segment_continuity_audit_path)
    anb_export = read_optional_json(anb_export_path)
    anb_semantic_audit = read_optional_json(anb_semantic_audit_path)
    runtime_semantics = read_optional_json(runtime_semantics_path)
    runtime_profile_archive_path = normalize_resource_path(
        runtime_profile_path or default_runtime_profile_path(character_manifest)
    )
    expected_resource_paths = [str(record["resource_path"]) for record in resource_records]
    expected_resource_path_counts = Counter(expected_resource_paths)
    expected_resource_path_set = set(expected_resource_path_counts)
    expected_json_resource_path_set = {
        str(record["resource_path"])
        for record in resource_records
        if resource_record_expects_json(record)
    }
    expected_manifest_paths = {
        ROOT_ARCHIVE_MANIFEST,
        CHARACTER_CONVERSION_ARCHIVE_MANIFEST,
        SKINNED_BINDING_ARCHIVE_MANIFEST,
        runtime_profile_archive_path,
    }
    n64_reference_path = optional_source_manifest_path(character_manifest, "n64_animation_reference_audit")
    expects_n64_reference = n64_reference_path is not None and n64_reference_path.is_file()
    if expects_n64_reference:
        expected_manifest_paths.add(N64_REFERENCE_ARCHIVE_AUDIT)

    missing_source_resources: list[dict[str, str]] = []
    invalid_source_resource_json: list[dict[str, str]] = []
    expected_resource_format_counts: Counter[str] = Counter()
    expected_resource_kind_counts: Counter[str] = Counter()
    for record in resource_records:
        expected_resource_kind_counts[str(record["kind"])] += 1
        source_path = Path(str(record["source_path"]))
        if not source_path.is_file():
            missing_source_resources.append(
                {
                    "source_path": str(source_path),
                    "resource_path": str(record["resource_path"]),
                    "kind": str(record["kind"]),
                }
            )
            continue
        if resource_record_expects_json(record):
            error, loaded = read_path_json_error(source_path)
            if error is not None:
                invalid_source_resource_json.append(
                    {
                        "source_path": str(source_path),
                        "resource_path": str(record["resource_path"]),
                        "kind": str(record["kind"]),
                        "error": error,
                    }
                )
                continue
            if isinstance(loaded, dict):
                expected_resource_format_counts[str(loaded.get("format", "<missing>"))] += 1

    try:
        with zipfile.ZipFile(archive_path, "r") as archive:
            archive_names = [
                normalize_resource_path(info.filename)
                for info in archive.infolist()
                if not info.is_dir()
            ]
            archive_name_counts = Counter(archive_names)
            archive_name_set = set(archive_names)
            manifest_errors = {
                path: read_archive_json_error(archive, path)
                for path in sorted(expected_manifest_paths)
                if path in archive_name_set
            }
            missing_manifest_entries = sorted(path for path in expected_manifest_paths if path not in archive_name_set)

            archived_character_manifest_matches = archived_json_matches(
                archive,
                CHARACTER_CONVERSION_ARCHIVE_MANIFEST,
                character_manifest,
            )
            archived_skinned_binding_manifest_matches = archived_json_matches(
                archive,
                SKINNED_BINDING_ARCHIVE_MANIFEST,
                skinned_binding,
            )
            archived_n64_reference_matches = True
            if expects_n64_reference:
                archived_n64_reference_matches = archived_json_matches(
                    archive,
                    N64_REFERENCE_ARCHIVE_AUDIT,
                    read_json(n64_reference_path),
                )

            runtime_profile_error, runtime_profile = read_archive_json_with_error(
                archive,
                runtime_profile_archive_path,
            )
            runtime_profile_valid = (
                runtime_profile_error is None
                and isinstance(runtime_profile, dict)
                and runtime_profile.get("format") == "oot3d_character_runtime_profile_v1"
                and runtime_profile.get("profile_id") == character_manifest.get("profile_id")
            )
            runtime_profile_resource_status = runtime_profile_resource_match_status(
                runtime_profile,
                expected_resource_path_set,
            )
            runtime_animation_contract = runtime_animation_contract_summary(
                runtime_profile,
                expected_resource_kind_counts["csab_track"],
            )
            runtime_animation_time_source_contract = runtime_animation_time_source_contract_summary(
                runtime_profile,
                runtime_semantics,
                str(character_manifest.get("profile_id", "")),
            )
            runtime_root_motion_ownership_contract = runtime_root_motion_ownership_contract_summary(
                runtime_profile,
                runtime_semantics,
                str(character_manifest.get("profile_id", "")),
            )
            runtime_skinning_contract = runtime_skinning_contract_summary(runtime_profile)
            runtime_selected_draw_skinning_contract = runtime_selected_draw_skinning_contract_summary(runtime_profile)
            runtime_transform_contract = runtime_transform_contract_summary(runtime_profile)
            runtime_native_bind_pose_contract = runtime_native_bind_pose_contract_summary(
                runtime_profile,
                expected_native_selection_primitive_count(target),
            )
            runtime_material_animation_contract = runtime_material_animation_contract_summary(runtime_profile)
            runtime_face_state_contract = runtime_face_state_contract_summary(runtime_profile)
            runtime_segment_continuity_contract = runtime_segment_continuity_contract_summary(
                runtime_profile,
                segment_continuity_audit,
            )
            runtime_anb_raw_ir_contract = runtime_anb_raw_ir_contract_summary(
                runtime_profile,
                anb_export,
            )
            runtime_anb_semantic_candidate_contract = runtime_anb_semantic_candidate_contract_summary(
                runtime_profile,
                anb_semantic_audit,
            )

            missing_resource_entries = sorted(
                path for path in expected_resource_path_set if path not in archive_name_set
            )
            extra_entries = sorted(
                path
                for path in archive_name_set
                if path not in expected_resource_path_set and path not in expected_manifest_paths
            )
            duplicate_archive_entries = sorted(
                (
                    {"path": path, "count": count}
                    for path, count in archive_name_counts.items()
                    if count > 1
                ),
                key=lambda item: str(item["path"]),
            )
            duplicate_expected_resources = sorted(
                (
                    {"path": path, "count": count}
                    for path, count in expected_resource_path_counts.items()
                    if count > 1
                ),
                key=lambda item: str(item["path"]),
            )
            (
                invalid_archived_resource_json,
                archived_resource_format_counts,
                bind_pose_render_contract,
            ) = audit_archived_resources(
                archive,
                expected_json_resource_path_set,
                archive_name_set,
            )
    except zipfile.BadZipFile as exc:
        raise ParseError(f"{archive_path}: invalid O2R/zip archive: {exc}") from exc

    issue_counts = {
        "character_manifest_issue": character_manifest_issue_count(character_manifest),
        "missing_manifest_entry": len(missing_manifest_entries),
        "invalid_manifest_json": sum(1 for error in manifest_errors.values() if error is not None),
        "character_conversion_manifest_mismatch": 0 if archived_character_manifest_matches else 1,
        "skinned_binding_manifest_mismatch": 0 if archived_skinned_binding_manifest_matches else 1,
        "n64_reference_audit_mismatch": 0 if archived_n64_reference_matches else 1,
        "invalid_runtime_profile": 0 if runtime_profile_valid else 1,
        "runtime_profile_resource_mismatch": 0 if runtime_profile_resource_status == "matched" else 1,
        "invalid_runtime_animation_contract": 0 if runtime_animation_contract["status"] == "valid" else 1,
        "invalid_runtime_animation_time_source_contract": (
            0 if runtime_animation_time_source_contract["status"] in {"valid", "not_required"} else 1
        ),
        "invalid_runtime_root_motion_ownership_contract": (
            0 if runtime_root_motion_ownership_contract["status"] in {"valid", "not_required"} else 1
        ),
        "invalid_runtime_skinning_contract": 0 if runtime_skinning_contract["status"] == "valid" else 1,
        "invalid_runtime_selected_draw_skinning_contract": (
            0 if runtime_selected_draw_skinning_contract["status"] == "valid" else 1
        ),
        "invalid_runtime_transform_contract": 0 if runtime_transform_contract["status"] == "valid" else 1,
        "invalid_runtime_native_bind_pose_contract": (
            0 if runtime_native_bind_pose_contract["status"] == "valid" else 1
        ),
        "invalid_runtime_material_animation_contract": (
            0 if runtime_material_animation_contract["status"] == "valid" else 1
        ),
        "invalid_runtime_face_state_contract": 0 if runtime_face_state_contract["status"] == "valid" else 1,
        "invalid_runtime_segment_continuity_contract": (
            0 if runtime_segment_continuity_contract["status"] in {"valid", "not_required"} else 1
        ),
        "invalid_runtime_anb_raw_ir_contract": (
            0 if runtime_anb_raw_ir_contract["status"] in {"valid", "not_required"} else 1
        ),
        "invalid_runtime_anb_semantic_candidate_contract": (
            0 if runtime_anb_semantic_candidate_contract["status"] in {"valid", "not_required"} else 1
        ),
        "missing_source_resource_file": len(missing_source_resources),
        "invalid_source_resource_json": len(invalid_source_resource_json),
        "missing_resource_entry": len(missing_resource_entries),
        "extra_archive_entry": len(extra_entries),
        "duplicate_archive_entry": sum(int(entry["count"]) - 1 for entry in duplicate_archive_entries),
        "duplicate_expected_resource_path": sum(
            int(entry["count"]) - 1 for entry in duplicate_expected_resources
        ),
        "invalid_archived_resource_json": len(invalid_archived_resource_json),
    }
    issue_counts["total"] = sum(issue_counts.values())

    resource_entry_count = len(expected_resource_path_set & set(archive_names))
    audit: dict[str, object] = {
        "format": "oot3d_character_conversion_package_audit_v1",
        "character_manifest": str(character_manifest_path),
        "archive": str(archive_path),
        "profile_id": character_manifest.get("profile_id"),
        "model_archive": character_manifest.get("source_archives", {}).get("model_archive")
        if isinstance(character_manifest.get("source_archives"), dict)
        else None,
        "model_cmb": character_manifest.get("source_archives", {}).get("model_cmb")
        if isinstance(character_manifest.get("source_archives"), dict)
        else None,
        "archive_entry_count": len(archive_names),
        "expected_manifest_paths": sorted(expected_manifest_paths),
        "runtime_profile_path": runtime_profile_archive_path,
        "runtime_profile_valid": runtime_profile_valid,
        "runtime_profile_resource_status": runtime_profile_resource_status,
        "runtime_animation_contract": runtime_animation_contract,
        "runtime_animation_time_source_contract": runtime_animation_time_source_contract,
        "runtime_root_motion_ownership_contract": runtime_root_motion_ownership_contract,
        "runtime_skinning_contract": runtime_skinning_contract,
        "runtime_selected_draw_skinning_contract": runtime_selected_draw_skinning_contract,
        "runtime_transform_contract": runtime_transform_contract,
        "runtime_native_bind_pose_contract": runtime_native_bind_pose_contract,
        "runtime_material_animation_contract": runtime_material_animation_contract,
        "runtime_face_state_contract": runtime_face_state_contract,
        "runtime_segment_continuity_contract": runtime_segment_continuity_contract,
        "runtime_anb_raw_ir_contract": runtime_anb_raw_ir_contract,
        "runtime_anb_semantic_candidate_contract": runtime_anb_semantic_candidate_contract,
        "archived_character_conversion_manifest_matches": archived_character_manifest_matches,
        "archived_skinned_binding_manifest_matches": archived_skinned_binding_manifest_matches,
        "archived_n64_reference_audit_matches": archived_n64_reference_matches,
        "expected_resource_count": len(expected_resource_paths),
        "expected_unique_resource_count": len(expected_resource_path_set),
        "resource_entry_count": resource_entry_count,
        "resource_kind_counts_expected": sorted_counter(expected_resource_kind_counts),
        "expected_resource_format_counts": sorted_counter(expected_resource_format_counts),
        "archived_resource_format_counts": sorted_counter(archived_resource_format_counts),
        "bind_pose_render_contract": bind_pose_render_contract,
        "missing_manifest_entry_count": len(missing_manifest_entries),
        "missing_source_resource_file_count": len(missing_source_resources),
        "invalid_source_resource_json_count": len(invalid_source_resource_json),
        "missing_resource_entry_count": len(missing_resource_entries),
        "extra_archive_entry_count": len(extra_entries),
        "duplicate_archive_entry_count": len(duplicate_archive_entries),
        "duplicate_expected_resource_path_count": len(duplicate_expected_resources),
        "invalid_archived_resource_json_count": len(invalid_archived_resource_json),
        "issue_counts": issue_counts,
        "manifest_errors": manifest_errors,
        "runtime_profile_error": runtime_profile_error,
        "sample_missing_manifest_entries": missing_manifest_entries[:sample_limit],
        "sample_missing_source_resources": missing_source_resources[:sample_limit],
        "sample_invalid_source_resource_json": invalid_source_resource_json[:sample_limit],
        "sample_missing_resource_entries": missing_resource_entries[:sample_limit],
        "sample_extra_archive_entries": extra_entries[:sample_limit],
        "sample_duplicate_archive_entries": duplicate_archive_entries[:sample_limit],
        "sample_duplicate_expected_resources": duplicate_expected_resources[:sample_limit],
        "sample_invalid_archived_resource_json": invalid_archived_resource_json[:sample_limit],
        "fallback_policy": [
            "This package is a runtime resource contract for one OOT3D character profile.",
            "It does not enable actor replacement without a fork runtime hook and player implementation.",
            "N64 character assets remain fallback until the runtime profile is explicitly consumed.",
        ],
    }
    if output_path is not None:
        write_json(output_path, audit)
    return audit


def character_runtime_profile(
    character_manifest: dict[str, object],
    target: dict[str, object],
    resource_records: list[dict[str, object]],
    *,
    segment_continuity_audit: dict[str, object] | None = None,
    anb_export: dict[str, object] | None = None,
    anb_semantic_audit: dict[str, object] | None = None,
    runtime_semantics: dict[str, object] | None = None,
) -> dict[str, object]:
    bind_pose = target.get("bind_pose", {}) if isinstance(target.get("bind_pose"), dict) else {}
    native_bind_pose = target.get("native_bind_pose", {}) if isinstance(target.get("native_bind_pose"), dict) else {}
    native_bind_pose_manifest_record = next(
        (record for record in resource_records if record.get("kind") == "native_bind_pose_manifest"),
        None,
    )
    native_bind_pose_resource_records = [
        record
        for record in resource_records
        if str(record.get("kind", "")).startswith("native_bind_pose_")
        and record.get("kind") != "native_bind_pose_manifest"
    ]
    material_animation_texture_records = [
        record for record in resource_records if record.get("kind") == "material_animation_texture"
    ]
    anb_resource_record = next((record for record in resource_records if record.get("kind") == "anb_payload_batch"), None)
    native_bind_pose_display_profiles = native_bind_pose.get("display_profiles", [])
    if not isinstance(native_bind_pose_display_profiles, list):
        native_bind_pose_display_profiles = []
    native_bind_pose_visibility_groups = native_bind_pose.get("visibility_groups", [])
    if not isinstance(native_bind_pose_visibility_groups, list):
        native_bind_pose_visibility_groups = []
    native_bind_pose_mesh_records = native_bind_pose.get("mesh_records", [])
    if not isinstance(native_bind_pose_mesh_records, list):
        native_bind_pose_mesh_records = []
    animation_resources = [
        runtime_animation_resource_record(record)
        for record in resource_records
        if record.get("kind") == "csab_track"
    ]
    runtime_readiness = character_manifest.get("runtime_readiness", {})
    n64_reference_validation = character_manifest.get("n64_reference_validation", {})
    skinning_contract = skinning_diagnostic_contract(character_manifest, target)
    selected_draw_contract = selected_draw_skinning_contract(
        target,
        skinning_contract,
    )
    semantic_binding_contract = animation_semantic_binding_contract(
        runtime_semantics,
        str(character_manifest.get("profile_id", "")),
        animation_resources,
    )
    sampling_contract = skel_anime_sampling_contract(
        runtime_semantics,
        str(character_manifest.get("profile_id", "")),
    ) if runtime_semantics is not None else None
    return {
        "format": "oot3d_character_runtime_profile_v1",
        "profile_id": character_manifest.get("profile_id"),
        "source_strategy": character_manifest.get("conversion_strategy", {}),
        "target": {
            "archive_path": target.get("archive_path"),
            "target_cmb_name": target.get("target_cmb_name"),
            "model_name": target.get("model_name"),
            "bone_count": target.get("bone_count"),
        },
        "animation_playback_contract": animation_playback_contract(
            target,
            native_bind_pose,
            animation_resources,
            n64_reference_validation if isinstance(n64_reference_validation, dict) else {},
        ),
        "animation_time_source_contract": animation_time_source_contract(
            runtime_semantics,
            str(character_manifest.get("profile_id", "")),
            animation_resources,
        ),
        **(
            {
                "skel_anime_sampling_contract": sampling_contract
            }
            if sampling_contract is not None
            else {}
        ),
        "animation_semantic_binding_contract": semantic_binding_contract,
        "animation_controller_contract": animation_controller_contract(
            runtime_semantics,
            str(character_manifest.get("profile_id", "")),
            animation_resources,
        ),
        "root_motion_ownership_contract": root_motion_ownership_contract(
            runtime_semantics,
            str(character_manifest.get("profile_id", "")),
        ),
        "runtime_transform": runtime_transform_policy(
            n64_reference_validation if isinstance(n64_reference_validation, dict) else {},
            selected_draw_contract,
            sampling_contract,
        ),
        "skinning_diagnostic_contract": skinning_contract,
        "selected_draw_skinning_contract": selected_draw_contract,
        "material_animation_contract": material_animation_contract(
            character_manifest,
            material_animation_texture_records,
        ),
        "face_state_contract": face_state_contract(character_manifest),
        "segment_continuity_contract": segment_continuity_contract(segment_continuity_audit),
        "anb_raw_ir_contract": anb_raw_ir_contract(anb_export, anb_resource_record),
        "anb_semantic_candidate_contract": anb_semantic_candidate_contract(anb_semantic_audit),
        "animation_lookup": animation_lookup(
            animation_resources,
            character_manifest,
            semantic_binding_contract,
        ),
        "native_resources": {
            "bind_pose": {
                "resource_path": normalize_resource_path(bind_pose.get("package_entry")),
                "source_path": str(bind_pose.get("export")),
            },
            "bind_pose_native": {
                "manifest_resource_path": (
                    native_bind_pose_manifest_record.get("resource_path")
                    if isinstance(native_bind_pose_manifest_record, dict)
                    else None
                ),
                "source_manifest": native_bind_pose.get("manifest"),
                "selection_source": native_bind_pose.get("selection_source"),
                "selection_profile_id": native_bind_pose.get("selection_profile_id"),
                "selection_primitive_key_count": native_bind_pose.get("selection_primitive_key_count"),
                "top_display_list": native_bind_pose.get("top_display_list"),
                "diagnostic_display_profile": native_bind_pose.get("diagnostic_display_profile"),
                "diagnostic_display_list": native_bind_pose.get("diagnostic_display_list"),
                "export_profile": native_bind_pose.get("export_profile"),
                "display_profile_count": len(native_bind_pose_display_profiles),
                "display_profiles": native_bind_pose_display_profiles,
                "visibility_group_count": len(native_bind_pose_visibility_groups),
                "visibility_groups": native_bind_pose_visibility_groups,
                "mesh_record_count": len(native_bind_pose_mesh_records),
                "selected_material_indices": native_bind_pose.get("selected_material_indices", []),
                "selected_material_display_lists": native_bind_pose.get("selected_material_display_lists", []),
                "resource_count": len(native_bind_pose_resource_records),
                "resources": [
                    {
                        "kind": record.get("native_kind", record.get("kind")),
                        "resource_path": record.get("resource_path"),
                    }
                    for record in native_bind_pose_resource_records
                ],
            },
            "material_animation_textures": [
                {
                    "role": record.get("role"),
                    "cmab_name": record.get("cmab_name"),
                    "texture_name": record.get("texture_name"),
                    "resource_path": record.get("resource_path"),
                    "width": record.get("width"),
                    "height": record.get("height"),
                    "texture_format": record.get("texture_format"),
                    "data_type": record.get("data_type"),
                }
                for record in material_animation_texture_records
            ],
            "anb_payload_batch": (
                {
                    "resource_path": anb_resource_record.get("resource_path"),
                    "source_path": anb_resource_record.get("source_path"),
                }
                if isinstance(anb_resource_record, dict)
                else None
            ),
            "animations": animation_resources,
        },
        "validation": {
            "character_issue_counts": character_manifest.get("issue_counts", {}),
            "n64_reference_status": (
                n64_reference_validation.get("status") if isinstance(n64_reference_validation, dict) else None
            ),
            "n64_pose_error_metric_status": (
                n64_reference_validation.get("n64_pose_error_metric_status")
                if isinstance(n64_reference_validation, dict)
                else None
            ),
            "n64_pose_error_metric_acceptance_status": (
                n64_reference_validation.get("n64_pose_error_metric_acceptance_status")
                if isinstance(n64_reference_validation, dict)
                else None
            ),
            "n64_pose_error_metric_extent_ratio_oot3d_per_n64": (
                n64_reference_validation.get("n64_pose_error_metric_extent_ratio_oot3d_per_n64")
                if isinstance(n64_reference_validation, dict)
                else None
            ),
        },
        "required_players": {
            "skeleton": bool(runtime_readiness.get("skeleton_player_required"))
            if isinstance(runtime_readiness, dict)
            else False,
            "skinning": bool(runtime_readiness.get("skinning_player_required"))
            if isinstance(runtime_readiness, dict)
            else False,
            "csab": bool(runtime_readiness.get("csab_player_required")) if isinstance(runtime_readiness, dict) else False,
            "faceb": bool(runtime_readiness.get("faceb_player_required"))
            if isinstance(runtime_readiness, dict)
            else False,
            "anb": bool(runtime_readiness.get("anb_decoder_required")) if isinstance(runtime_readiness, dict) else False,
            "cmab": bool(runtime_readiness.get("cmab_material_player_required"))
            if isinstance(runtime_readiness, dict)
            else False,
        },
        "runtime_enablement": (
            runtime_readiness.get("runtime_enablement") if isinstance(runtime_readiness, dict) else None
        ),
        "fallback_policy": "N64 player remains active until this runtime profile is explicitly routed and all required players are implemented.",
    }


def runtime_transform_policy(
    n64_reference_validation: dict[str, object],
    selected_draw_contract: dict[str, object] | None = None,
    sampling_contract: dict[str, object] | None = None,
) -> dict[str, object]:
    extent_ratio = n64_reference_validation.get("n64_pose_error_metric_extent_ratio_oot3d_per_n64", {})
    scale_factor = n64_reference_validation.get("n64_pose_error_metric_scale_factor_oot3d_per_n64", {})
    model_scale, scale_source = runtime_model_scale_from_reference_metrics(extent_ratio, scale_factor)
    visual_origin_offset, visual_origin_source = native_visual_origin_offset(
        selected_draw_contract, sampling_contract
    )
    return {
        "format": "oot3d_character_runtime_transform_v1",
        "model_scale": model_scale,
        "model_scale_source": scale_source,
        "visual_origin_offset": visual_origin_offset,
        "visual_origin_offset_source": visual_origin_source,
        "axis_extent_ratio_oot3d_per_n64": extent_ratio if isinstance(extent_ratio, dict) else {},
        "diagonal_scale_factor_oot3d_per_n64": scale_factor if isinstance(scale_factor, dict) else {},
        "origin_policy": (
            "N64 actor world position is authoritative; preserve authored OOT3D CSAB root translation "
            "inside the visual pose, then apply a local visual-origin offset derived from the selected "
            "runtime draw bounds so the OOT3D visual subset is grounded on the actor origin."
        ),
        "scale_policy": (
            "Apply actor scale from Shipwright first, then multiply by this OOT3D-to-N64 visual model scale; "
            "debug CVars are runtime multipliers only."
        ),
    }


def native_visual_origin_offset(
    selected_draw_contract: dict[str, object] | None,
    sampling_contract: dict[str, object] | None,
) -> tuple[list[float], str]:
    if isinstance(selected_draw_contract, dict) and isinstance(sampling_contract, dict):
        translations = selected_draw_contract.get("skeleton_bind_translations")
        special_bone = sampling_contract.get("special_bone")
        base_translation = sampling_contract.get("base_translation")
        if (
            isinstance(translations, list)
            and isinstance(special_bone, int)
            and not isinstance(special_bone, bool)
            and 0 <= special_bone < len(translations)
            and isinstance(translations[special_bone], list)
            and len(translations[special_bone]) == 3
            and isinstance(base_translation, list)
            and len(base_translation) == 3
        ):
            bind_translation = translations[special_bone]
            if all(
                isinstance(value, (int, float))
                and not isinstance(value, bool)
                and math.isfinite(float(value))
                for value in [*bind_translation, *base_translation]
            ):
                return (
                    [
                        float(bind_translation[index]) - float(base_translation[index])
                        for index in range(3)
                    ],
                    "cmb_special_bone_bind_translation_minus_player_init_base_translation",
                )
    return (
        selected_draw_visual_origin_offset(selected_draw_contract),
        "selected_draw_dynamic_position_bounds_min_y_fallback",
    )


def selected_draw_visual_origin_offset(selected_draw_contract: dict[str, object] | None) -> list[float]:
    if not isinstance(selected_draw_contract, dict):
        return [0.0, 0.0, 0.0]
    bounds = selected_draw_contract.get("dynamic_position_bounds")
    if not isinstance(bounds, dict):
        bounds = selected_draw_contract.get("position_bounds")
    if not isinstance(bounds, dict):
        return [0.0, 0.0, 0.0]
    y_bounds = bounds.get("y")
    if not isinstance(y_bounds, dict):
        return [0.0, 0.0, 0.0]
    min_y = y_bounds.get("min")
    if not isinstance(min_y, (int, float)) or not math.isfinite(float(min_y)):
        return [0.0, 0.0, 0.0]
    return [0.0, -float(min_y), 0.0]


def segment_continuity_contract(segment_continuity_audit: dict[str, object] | None) -> dict[str, object]:
    if not isinstance(segment_continuity_audit, dict):
        return {
            "format": "oot3d_character_segment_continuity_contract_v1",
            "status": "not_provided",
            "segment_count": 0,
            "transition_count": 0,
            "segments": [],
        }
    normalization_plan = segment_continuity_audit.get("normalization_plan", {})
    normalized_segments = (
        normalization_plan.get("segments", [])
        if isinstance(normalization_plan, dict)
        else []
    )
    segments: list[dict[str, object]] = []
    if isinstance(normalized_segments, list):
        for segment in normalized_segments:
            if not isinstance(segment, dict):
                continue
            csab_name = normalize_resource_path(segment.get("csab_name"))
            offset = finite_vec3_record(segment.get("normalization_offset"))
            if csab_name and offset is not None:
                segments.append(
                    {
                        "csab_name": csab_name,
                        "normalization_offset": offset,
                    }
                )
    return {
        "format": "oot3d_character_segment_continuity_contract_v1",
        "status": "ready" if segment_continuity_audit.get("status") == "valid" and segments else "not_ready",
        "source_audit_format": segment_continuity_audit.get("format"),
        "root_motion_bone": segment_continuity_audit.get("root_motion_bone"),
        "segment_count": segment_continuity_audit.get("segment_count", len(segments)),
        "transition_count": segment_continuity_audit.get("transition_count", 0),
        "raw_discontinuity_count": segment_continuity_audit.get("raw_discontinuity_count", 0),
        "normalized_discontinuity_count": segment_continuity_audit.get("normalized_discontinuity_count", 0),
        "max_raw_transition_delta": segment_continuity_audit.get("max_raw_transition_delta"),
        "max_normalized_transition_delta": segment_continuity_audit.get("max_normalized_transition_delta"),
        "normalization_policy": (
            normalization_plan.get("policy")
            if isinstance(normalization_plan, dict)
            else None
        ),
        "segments": segments,
    }


def anb_raw_ir_contract(
    anb_export: dict[str, object] | None,
    anb_resource_record: dict[str, object] | None,
) -> dict[str, object]:
    if not isinstance(anb_export, dict):
        return {
            "format": "oot3d_character_anb_raw_ir_contract_v1",
            "status": "not_provided",
            "source_export_format": None,
            "resource_path": None,
            "payload_count": 0,
        }

    return {
        "format": "oot3d_character_anb_raw_ir_contract_v1",
        "status": (
            "ready"
            if anb_export.get("format") == "oot3d_anb_payload_batch_export_v1"
            and int(anb_export.get("payload_count", 0) or 0) > 0
            and int(anb_export.get("issue_count", 1) or 0) == 0
            and isinstance(anb_resource_record, dict)
            else "not_ready"
        ),
        "source_export_format": anb_export.get("format"),
        "resource_path": (
            anb_resource_record.get("resource_path") if isinstance(anb_resource_record, dict) else None
        ),
        "source_path": anb_resource_record.get("source_path") if isinstance(anb_resource_record, dict) else None,
        "primary_archive": anb_export.get("primary_archive"),
        "duplicate_archive": anb_export.get("duplicate_archive"),
        "payload_count": anb_export.get("payload_count", 0),
        "duplicate_payload_match_count": anb_export.get("duplicate_payload_match_count", 0),
        "duplicate_payload_missing_count": anb_export.get("duplicate_payload_missing_count", 0),
        "duplicate_payload_mismatch_count": anb_export.get("duplicate_payload_mismatch_count", 0),
        "invalid_header_count": anb_export.get("invalid_header_count", 0),
        "decoded_frame_table_match_count": anb_export.get("decoded_frame_table_match_count", 0),
        "decoded_frame_table_mismatch_count": anb_export.get("decoded_frame_table_mismatch_count", 0),
        "total_decoded_frame_count": anb_export.get("total_decoded_frame_count", 0),
        "total_s16_sample_count": anb_export.get("total_s16_sample_count", 0),
        "channel_summary": anb_export.get("channel_summary", {}),
        "csab_lookup": anb_export.get("csab_lookup", {}),
        "issue_count": anb_export.get("issue_count", 1),
        "frame_count_candidate_counts": anb_export.get("frame_count_candidate_counts", {}),
        "channel_count_candidate_counts": anb_export.get("channel_count_candidate_counts", {}),
        "payload_word_count_counts": anb_export.get("payload_word_count_counts", {}),
        "trailing_byte_count_counts": anb_export.get("trailing_byte_count_counts", {}),
        "raw_ir_policy": (
            "ANB payloads are package-mounted as byte-stable raw IR until the semantic channel layout "
            "is decoded and consumed by the runtime player."
        ),
    }


def anb_semantic_candidate_contract(anb_semantic_audit: dict[str, object] | None) -> dict[str, object]:
    if not isinstance(anb_semantic_audit, dict):
        return {
            "format": "oot3d_character_anb_semantic_candidate_contract_v1",
            "status": "not_provided",
            "source_audit_format": None,
            "compared_record_count": 0,
            "candidate_channel_count": 0,
        }

    return {
        "format": "oot3d_character_anb_semantic_candidate_contract_v1",
        "status": (
            "ready"
            if anb_semantic_audit.get("format") == "oot3d_anb_semantic_candidate_audit_v1"
            and int(anb_semantic_audit.get("compared_record_count", 0) or 0) > 0
            and int(anb_semantic_audit.get("channel_count", 0) or 0) == 67
            and int(anb_semantic_audit.get("candidate_channel_count", 0) or 0) > 0
            else "not_ready"
        ),
        "source_audit_format": anb_semantic_audit.get("format"),
        "anb_export": anb_semantic_audit.get("anb_export"),
        "character_manifest": anb_semantic_audit.get("character_manifest"),
        "track_root": anb_semantic_audit.get("track_root"),
        "matched_anb_record_count": anb_semantic_audit.get("matched_anb_record_count", 0),
        "compared_record_count": anb_semantic_audit.get("compared_record_count", 0),
        "status_counts": anb_semantic_audit.get("status_counts", {}),
        "total_compared_frame_component_pairs": anb_semantic_audit.get("total_compared_frame_component_pairs", 0),
        "min_abs_correlation": anb_semantic_audit.get("min_abs_correlation"),
        "min_consensus_count": anb_semantic_audit.get("min_consensus_count", 0),
        "include_frame_mismatch_resample": anb_semantic_audit.get("include_frame_mismatch_resample", False),
        "channel_count": anb_semantic_audit.get("channel_count", 0),
        "candidate_channel_count": anb_semantic_audit.get("candidate_channel_count", 0),
        "strong_candidate_count": anb_semantic_audit.get("strong_candidate_count", 0),
        "stable_candidate_channel_count": anb_semantic_audit.get("stable_candidate_channel_count", 0),
        "resolved_zero_candidate_channel_count": anb_semantic_audit.get("resolved_zero_candidate_channel_count", 0),
        "resolved_static_zero_candidate_channel_count": anb_semantic_audit.get(
            "resolved_static_zero_candidate_channel_count",
            0,
        ),
        "resolved_constant_control_tuple_channel_count": anb_semantic_audit.get(
            "resolved_constant_control_tuple_channel_count",
            0,
        ),
        "unstable_candidate_channel_count": anb_semantic_audit.get("unstable_candidate_channel_count", 0),
        "zero_candidate_channel_count": anb_semantic_audit.get("zero_candidate_channel_count", 0),
        "unresolved_zero_candidate_channel_count": anb_semantic_audit.get("unresolved_zero_candidate_channel_count", 0),
        "unresolved_channel_count": anb_semantic_audit.get("unresolved_channel_count", 0),
        "unstable_candidate_channel_class_counts": anb_semantic_audit.get(
            "unstable_candidate_channel_class_counts",
            {},
        ),
        "unstable_candidate_semantic_class_counts": anb_semantic_audit.get(
            "unstable_candidate_semantic_class_counts",
            {},
        ),
        "zero_candidate_channel_class_counts": anb_semantic_audit.get("zero_candidate_channel_class_counts", {}),
        "unresolved_zero_candidate_channel_class_counts": anb_semantic_audit.get(
            "unresolved_zero_candidate_channel_class_counts",
            {},
        ),
        "zero_candidate_semantic_class_counts": anb_semantic_audit.get("zero_candidate_semantic_class_counts", {}),
        "zero_candidate_resolution_status_counts": anb_semantic_audit.get(
            "zero_candidate_resolution_status_counts",
            {},
        ),
        "unstable_candidate_channel_classification": anb_semantic_audit.get(
            "unstable_candidate_channel_classification",
            [],
        ),
        "zero_candidate_channel_classification": anb_semantic_audit.get(
            "zero_candidate_channel_classification",
            [],
        ),
        "stable_candidate_mappings": anb_semantic_audit.get("stable_candidate_mappings", []),
        "component_candidate_counts": anb_semantic_audit.get("component_candidate_counts", {}),
        "channel_candidates": anb_semantic_audit.get("channel_candidates", []),
        "semantic_policy": (
            "Candidate-only contract: correlations identify likely ANB channel names/components, but final "
            "runtime ANB playback must consume a separately validated semantic mapping."
        ),
    }


def finite_vec3_record(value: object) -> list[float] | None:
    if not isinstance(value, list) or len(value) != 3:
        return None
    out: list[float] = []
    for item in value:
        if not isinstance(item, (int, float)) or not math.isfinite(float(item)):
            return None
        out.append(round_float(float(item)))
    return out


def material_animation_contract(
    character_manifest: dict[str, object],
    material_animation_texture_records: list[dict[str, object]] | None = None,
) -> dict[str, object]:
    material_payloads = character_manifest.get("material_animation_payloads", {})
    material_payloads = material_payloads if isinstance(material_payloads, dict) else {}
    bindings = material_payloads.get("bindings", [])
    bindings = bindings if isinstance(bindings, list) else []
    texture_resource_by_key: dict[tuple[str, str, str], dict[str, object]] = {}
    for record in material_animation_texture_records or []:
        if not isinstance(record, dict):
            continue
        key = (
            normalize_resource_path(record.get("cmab_name")),
            normalize_resource_path(record.get("role")),
            normalize_resource_path(record.get("texture_name")),
        )
        if all(key) and key not in texture_resource_by_key:
            texture_resource_by_key[key] = record

    normalized_bindings: list[dict[str, object]] = []
    missing_texture_resource_count = 0
    for binding in bindings:
        if not isinstance(binding, dict):
            continue
        cmab_name = normalize_resource_path(binding.get("cmab_name"))
        role = normalize_resource_path(binding.get("role"))
        texture_names = (
            [
                normalize_resource_path(texture_name)
                for texture_name in binding.get("texture_names", [])
                if normalize_resource_path(texture_name)
            ]
            if isinstance(binding.get("texture_names"), list)
            else []
        )
        texture_resources: list[dict[str, object]] = []
        for texture_name in texture_names:
            resource = texture_resource_by_key.get((cmab_name, role, texture_name))
            if resource is None:
                missing_texture_resource_count += 1
                texture_resources.append(
                    {
                        "texture_name": texture_name,
                        "resource_path": None,
                        "status": "missing_resource",
                    }
                )
                continue
            texture_resources.append(
                {
                    "texture_name": texture_name,
                    "resource_path": normalize_resource_path(resource.get("resource_path")),
                    "width": resource.get("width"),
                    "height": resource.get("height"),
                    "texture_format": resource.get("texture_format"),
                    "data_type": resource.get("data_type"),
                    "status": "ready",
                }
            )
        normalized_bindings.append(
            {
            "cmab_name": normalize_resource_path(binding.get("cmab_name")),
            "role": normalize_resource_path(binding.get("role")),
            "target_cmb": normalize_resource_path(binding.get("target_cmb")),
            "frame_count_candidate": binding.get("frame_count_candidate"),
            "loop_mode_candidate": binding.get("loop_mode_candidate"),
                "texture_names": texture_names,
                "texture_resources": texture_resources,
            }
        )
    return {
        "format": "oot3d_character_material_animation_contract_v1",
        "status": (
            "ready"
            if normalized_bindings and missing_texture_resource_count == 0
            else "missing_texture_resources"
            if normalized_bindings
            else "missing_bindings"
        ),
        "cmab_count": material_payloads.get("count", 0),
        "binding_count": len(normalized_bindings),
        "unresolved_or_ambiguous_count": material_payloads.get("unresolved_or_ambiguous_count", 0),
        "texture_name_counts_by_role": material_payloads.get("texture_name_counts_by_role", {}),
        "missing_texture_resource_count": missing_texture_resource_count,
        "bindings": normalized_bindings,
    }


def face_state_contract(character_manifest: dict[str, object]) -> dict[str, object]:
    auxiliary = character_manifest.get("auxiliary_animation_payloads", {})
    auxiliary = auxiliary if isinstance(auxiliary, dict) else {}
    material_contract = material_animation_contract(character_manifest)
    role_texture_counts = {
        str(binding.get("role")): len(binding.get("texture_names", []))
        for binding in material_contract.get("bindings", [])
        if isinstance(binding, dict)
    }
    eye_count = role_texture_counts.get("eye", 0)
    mouth_count = role_texture_counts.get("mouth", 0)
    tracks = auxiliary.get("faceb_tracks", [])
    tracks = tracks if isinstance(tracks, list) else []
    invalid_eye_event_count = 0
    invalid_mouth_event_count = 0
    event_count = 0
    for track in tracks:
        if not isinstance(track, dict):
            continue
        events = track.get("events", [])
        if not isinstance(events, list):
            continue
        for event in events:
            if not isinstance(event, dict):
                continue
            event_count += 1
            eye_index = event.get("eye_index")
            mouth_index = event.get("mouth_index")
            if isinstance(eye_index, int) and not isinstance(eye_index, bool):
                if eye_index != 255 and (eye_index < 0 or eye_index >= eye_count):
                    invalid_eye_event_count += 1
            else:
                invalid_eye_event_count += 1
            if isinstance(mouth_index, int) and not isinstance(mouth_index, bool):
                if mouth_index != 255 and (mouth_index < 0 or mouth_index >= mouth_count):
                    invalid_mouth_event_count += 1
            else:
                invalid_mouth_event_count += 1
    stem_overlap = auxiliary.get("stem_overlap", {})
    stem_overlap = stem_overlap if isinstance(stem_overlap, dict) else {}
    status = "ready"
    if not tracks:
        status = "missing_faceb_tracks"
    elif eye_count <= 0 or mouth_count <= 0:
        status = "missing_face_texture_domain"
    elif int(stem_overlap.get("faceb_without_csab_count", 1) or 0) != 0:
        status = "faceb_without_csab"
    elif invalid_eye_event_count != 0 or invalid_mouth_event_count != 0:
        status = "face_event_index_out_of_range"
    return {
        "format": "oot3d_character_face_state_contract_v1",
        "status": status,
        "faceb_track_count": len(tracks),
        "faceb_entry_total": event_count,
        "faceb_csab_overlap_count": stem_overlap.get("csab_faceb_overlap_count"),
        "faceb_without_csab_count": stem_overlap.get("faceb_without_csab_count"),
        "eye_texture_count": eye_count,
        "mouth_texture_count": mouth_count,
        "hold_value": 255,
        "invalid_eye_event_count": invalid_eye_event_count,
        "invalid_mouth_event_count": invalid_mouth_event_count,
        "tracks": tracks,
    }


def runtime_model_scale_from_reference_metrics(
    extent_ratio: object,
    scale_factor: object,
) -> tuple[float, str]:
    if (
        metric_summary_value(extent_ratio, "y", "avg") is not None
        or metric_summary_value(scale_factor, None, "avg") is not None
    ):
        return 1.0, "cmb_source_units_preserved_actor_scale_applied_by_shipwright"

    return 1.0, "cmb_source_units_fallback"


def metric_summary_value(summary: object, axis: str | None, key: str) -> float | None:
    if not isinstance(summary, dict):
        return None
    source = summary.get(axis) if axis is not None else summary
    if not isinstance(source, dict):
        return None
    value = source.get(key)
    if not isinstance(value, (int, float)):
        return None
    value = float(value)
    return value if math.isfinite(value) else None


def round_float(value: float) -> float:
    return round(float(value), 6)


def skinning_diagnostic_contract(
    character_manifest: dict[str, object],
    target: dict[str, object],
) -> dict[str, object]:
    pose_batch_path = optional_source_manifest_path(character_manifest, "skinned_animation_pose_batch")
    if pose_batch_path is None or not pose_batch_path.is_file():
        return {
            "format": "oot3d_character_skinning_diagnostic_contract_v1",
            "status": "not_provided",
            "samples": [],
        }

    pose_batch = read_json(pose_batch_path)
    records = [
        record
        for record in pose_batch.get("records", [])
        if isinstance(record, dict)
        and record.get("status") == "exported"
        and normalize_resource_path(record.get("archive_path")) == normalize_resource_path(target.get("archive_path"))
        and normalize_resource_path(record.get("target_cmb_name"))
        == normalize_resource_path(target.get("target_cmb_name"))
    ]
    selected = select_skinning_diagnostic_records(character_manifest, records)
    samples = [
        skinning_diagnostic_sample(pose_batch_path, pose_batch, record)
        for record in selected
    ]
    ready_samples = [
        sample
        for sample in samples
        if sample.get("status") == "ready"
        and sample.get("validation_status") == "valid"
        and int(sample.get("validation_error_count", 1) or 0) == 0
    ]
    return {
        "format": "oot3d_character_skinning_diagnostic_contract_v1",
        "status": "ready" if ready_samples else "incomplete",
        "source_pose_batch_manifest": str(pose_batch_path),
        "selection_policy": (
            "prefer the playable locomotion sample for the active pilot profile, "
            "then fall back to the first valid pose sample for the same CMB target"
        ),
        "sample_count": len(samples),
        "ready_sample_count": len(ready_samples),
        "samples": samples,
    }


def selected_draw_skinning_contract(
    target: dict[str, object],
    skinning_contract: dict[str, object],
) -> dict[str, object]:
    sample = selected_ready_skinning_sample(skinning_contract)
    if sample is None:
        return {
            "format": "oot3d_character_selected_draw_skinning_contract_v1",
            "status": "missing_ready_skinning_sample",
        }

    bind_pose = target.get("bind_pose", {}) if isinstance(target.get("bind_pose"), dict) else {}
    native_bind_pose = target.get("native_bind_pose", {}) if isinstance(target.get("native_bind_pose"), dict) else {}
    bind_pose_path = Path(str(bind_pose.get("export") or ""))
    native_manifest_path = Path(str(native_bind_pose.get("manifest") or ""))
    csab_name = normalize_resource_path(sample.get("csab_name"))
    track_path = selected_draw_track_export_path(target, csab_name)

    try:
        if not bind_pose_path.is_file():
            raise ParseError(f"{bind_pose_path}: bind-pose export not found")
        if not native_manifest_path.is_file():
            raise ParseError(f"{native_manifest_path}: native bind-pose manifest not found")
        if track_path is None or not track_path.is_file():
            raise ParseError(f"{track_path}: CSAB track export not found for {csab_name}")

        bind_pose_json = read_json(bind_pose_path)
        native_manifest = read_json(native_manifest_path)
        track_json = read_json(track_path)
        return build_selected_draw_skinning_contract(
            bind_pose_json,
            native_manifest,
            track_json,
            csab_name=csab_name,
            target_cmb_name=normalize_resource_path(target.get("target_cmb_name")),
            sample_frames=[
                int(frame)
                for frame in sample.get("sample_frames", [])
                if isinstance(frame, int) and not isinstance(frame, bool)
            ],
        )
    except Exception as exc:
        return {
            "format": "oot3d_character_selected_draw_skinning_contract_v1",
            "status": "invalid",
            "csab_name": csab_name,
            "bind_pose_export": str(bind_pose_path),
            "native_bind_pose_manifest": str(native_manifest_path),
            "track_export": str(track_path) if track_path is not None else None,
            "error": str(exc),
        }


def selected_ready_skinning_sample(contract: dict[str, object]) -> dict[str, object] | None:
    samples = contract.get("samples", [])
    if not isinstance(samples, list):
        return None
    for sample in samples:
        if (
            isinstance(sample, dict)
            and sample.get("status") == "ready"
            and sample.get("validation_status") == "valid"
            and int(sample.get("validation_error_count", 1) or 0) == 0
        ):
            return sample
    return None


def selected_draw_track_export_path(target: dict[str, object], csab_name: str) -> Path | None:
    animations = target.get("animations", [])
    if not isinstance(animations, list):
        return None
    for animation in animations:
        if not isinstance(animation, dict):
            continue
        if normalize_resource_path(animation.get("csab_name")) != csab_name:
            continue
        path = animation.get("track_export_file")
        return Path(str(path)) if path else None
    return None


def build_selected_draw_skinning_contract(
    bind_pose_json: dict[str, object],
    native_manifest: dict[str, object],
    track_json: dict[str, object],
    *,
    csab_name: str,
    target_cmb_name: str,
    sample_frames: list[int],
) -> dict[str, object]:
    if bind_pose_json.get("format") != "oot3d_skinned_bind_pose_export_v1":
        raise ParseError("unexpected bind-pose export format")
    if native_manifest.get("format") != SKINNED_BIND_POSE_NATIVE_MANIFEST_FORMAT:
        raise ParseError("unexpected native bind-pose manifest format")
    if track_json.get("format") != "oot3d_csab_skeleton_track_export_v1":
        raise ParseError("unexpected CSAB track export format")
    if normalize_resource_path(track_json.get("csab_name")) != csab_name:
        raise ParseError("CSAB track export does not match selected diagnostic sample")

    selected_keys = selected_primitive_keys(native_manifest)
    selected_primitives, ignored_rigid_primitive_count = selected_skinned_draw_primitives(
        bind_pose_json,
        selected_keys,
    )
    dynamic_primitives, dynamic_skinned_primitive_count, dynamic_rigid_primitive_count = (
        selected_dynamic_draw_primitives(bind_pose_json, selected_keys)
    )
    skeleton_bones = bind_pose_skeleton_bones(bind_pose_json)
    inverse_bind_world_transforms = tuple(
        invert_affine_matrix(matrix)
        for _parent, _translation, _rotation, _scale, matrix in skeleton_bones
    )
    tracks = track_json.get("tracks", [])
    if not isinstance(tracks, list):
        raise ParseError("CSAB track export has no track array")
    if int(track_json.get("skeleton_bone_count_candidate", -1) or -1) != len(skeleton_bones):
        raise ParseError("CSAB track skeleton count does not match bind-pose skeleton")

    frames = sorted(set(sample_frames))
    frame_count_candidate = int(track_json.get("frame_count_candidate", 0) or 0)
    if not frames:
        frames = [0, frame_count_candidate // 2, frame_count_candidate]

    aggregate = Counter()
    aggregate_bounds: dict[str, list[float]] = {}
    max_position_delta = 0.0
    frame_records: list[dict[str, object]] = []
    dynamic_aggregate = Counter()
    dynamic_aggregate_bounds: dict[str, list[float]] = {}
    dynamic_max_position_delta = 0.0
    dynamic_frame_records: list[dict[str, object]] = []

    for frame in frames:
        if frame < 0 or frame > frame_count_candidate:
            raise ParseError(f"selected draw sample frame outside CSAB domain: {frame}")
        world_transforms = selected_draw_world_transforms(skeleton_bones, tracks, frame)
        skin_transforms = tuple(
            multiply_matrix(world_transforms[bone_index], inverse_bind_world_transforms[bone_index])
            for bone_index in range(len(world_transforms))
        )
        frame_counts = Counter()
        frame_bounds: dict[str, list[float]] = {}
        dynamic_frame_counts = Counter()
        dynamic_frame_bounds: dict[str, list[float]] = {}

        for primitive in selected_primitives:
            vertices = primitive["vertices"]
            indices = primitive["indices"]
            if not isinstance(vertices, list) or not isinstance(indices, list):
                raise ParseError("selected primitive is missing vertices or indices")
            for index in indices:
                if not isinstance(index, int) or index < 0 or index >= len(vertices):
                    raise ParseError("selected primitive index is outside its vertex array")
                vertex = vertices[index]
                if not isinstance(vertex, dict):
                    raise ParseError("selected primitive vertex is not an object")
                position, normal = weighted_pose_preview(
                    list_to_vec3(vertex.get("source_position")),
                    list_to_vec3(vertex.get("source_normal")),
                    vertex.get("influences"),
                    skin_transforms,
                )
                delta = vec3_distance(position, list_to_vec3(vertex.get("source_position")))
                max_position_delta = max(max_position_delta, delta)
                frame_counts["sampled_vertex_rows"] += 1
                aggregate["sampled_vertex_rows"] += 1
                if delta > 0.0001:
                    frame_counts["changed_position_rows"] += 1
                    aggregate["changed_position_rows"] += 1
                position_finite = vec3_is_finite(position)
                normal_finite = vec3_is_finite(normal)
                if position_finite:
                    frame_counts["finite_position_rows"] += 1
                    aggregate["finite_position_rows"] += 1
                    add_vec3_bounds(frame_bounds, position)
                    add_vec3_bounds(aggregate_bounds, position)
                if normal_finite:
                    frame_counts["finite_normal_rows"] += 1
                    aggregate["finite_normal_rows"] += 1
                if position_finite and normal_finite:
                    frame_counts["finite_pose_rows"] += 1
                    aggregate["finite_pose_rows"] += 1

        for primitive in dynamic_primitives:
            vertices = primitive["vertices"]
            indices = primitive["indices"]
            if not isinstance(vertices, list) or not isinstance(indices, list):
                raise ParseError("selected dynamic primitive is missing vertices or indices")
            skinning_mode = json_int(primitive.get("skinning_mode"), 0)
            rigid_bone_index = rigid_primitive_bone_index(primitive) if skinning_mode == 0 else -1
            if skinning_mode == 0 and (rigid_bone_index < 0 or rigid_bone_index >= len(world_transforms)):
                raise ParseError("selected dynamic rigid primitive bone is outside sampled skeleton")
            for index in indices:
                if not isinstance(index, int) or index < 0 or index >= len(vertices):
                    raise ParseError("selected dynamic primitive index is outside its vertex array")
                vertex = vertices[index]
                if not isinstance(vertex, dict):
                    raise ParseError("selected dynamic primitive vertex is not an object")
                source_position = list_to_vec3(vertex.get("source_position"))
                source_normal = list_to_vec3(vertex.get("source_normal"))
                if skinning_mode == 0:
                    transform = world_transforms[rigid_bone_index]
                    position = transform_position(transform, source_position)
                    normal = transform_direction(transform, source_normal)
                else:
                    position, normal = weighted_pose_preview(
                        source_position,
                        source_normal,
                        vertex.get("influences"),
                        skin_transforms,
                    )
                delta = vec3_distance(position, source_position)
                dynamic_max_position_delta = max(dynamic_max_position_delta, delta)
                dynamic_frame_counts["sampled_vertex_rows"] += 1
                dynamic_aggregate["sampled_vertex_rows"] += 1
                if delta > 0.0001:
                    dynamic_frame_counts["changed_position_rows"] += 1
                    dynamic_aggregate["changed_position_rows"] += 1
                position_finite = vec3_is_finite(position)
                normal_finite = vec3_is_finite(normal)
                if position_finite:
                    dynamic_frame_counts["finite_position_rows"] += 1
                    dynamic_aggregate["finite_position_rows"] += 1
                    add_vec3_bounds(dynamic_frame_bounds, position)
                    add_vec3_bounds(dynamic_aggregate_bounds, position)
                if normal_finite:
                    dynamic_frame_counts["finite_normal_rows"] += 1
                    dynamic_aggregate["finite_normal_rows"] += 1
                if position_finite and normal_finite:
                    dynamic_frame_counts["finite_pose_rows"] += 1
                    dynamic_aggregate["finite_pose_rows"] += 1

        frame_records.append(
            {
                "frame": frame,
                "counts": {
                    "sampled_vertex_rows": frame_counts["sampled_vertex_rows"],
                    "finite_position_rows": frame_counts["finite_position_rows"],
                    "finite_normal_rows": frame_counts["finite_normal_rows"],
                    "finite_pose_rows": frame_counts["finite_pose_rows"],
                    "changed_position_rows": frame_counts["changed_position_rows"],
                    "validation_error_count": (
                        frame_counts["sampled_vertex_rows"] - frame_counts["finite_pose_rows"]
                    ),
                },
                "position_bounds": rounded_vec3_bounds(frame_bounds),
            }
        )
        dynamic_frame_records.append(
            {
                "frame": frame,
                "counts": {
                    "sampled_vertex_rows": dynamic_frame_counts["sampled_vertex_rows"],
                    "finite_position_rows": dynamic_frame_counts["finite_position_rows"],
                    "finite_normal_rows": dynamic_frame_counts["finite_normal_rows"],
                    "finite_pose_rows": dynamic_frame_counts["finite_pose_rows"],
                    "changed_position_rows": dynamic_frame_counts["changed_position_rows"],
                    "validation_error_count": (
                        dynamic_frame_counts["sampled_vertex_rows"] -
                        dynamic_frame_counts["finite_pose_rows"]
                    ),
                },
                "position_bounds": rounded_vec3_bounds(dynamic_frame_bounds),
            }
        )

    triangle_count = sum(int(primitive.get("triangle_count", 0) or 0) for primitive in selected_primitives)
    selected_unique_vertex_rows = sum(
        len(primitive.get("vertices", []))
        for primitive in selected_primitives
        if isinstance(primitive.get("vertices"), list)
    )
    validation_error_count = aggregate["sampled_vertex_rows"] - aggregate["finite_pose_rows"]
    dynamic_triangle_count = sum(
        primitive_triangle_count(primitive)
        for primitive in dynamic_primitives
    )
    dynamic_unique_vertex_rows = sum(
        len(primitive.get("vertices", []))
        for primitive in dynamic_primitives
        if isinstance(primitive.get("vertices"), list)
    )
    dynamic_validation_error_count = (
        dynamic_aggregate["sampled_vertex_rows"] - dynamic_aggregate["finite_pose_rows"]
    )
    return {
        "format": "oot3d_character_selected_draw_skinning_contract_v1",
        "status": "ready" if validation_error_count == 0 and dynamic_validation_error_count == 0 else "invalid",
        "csab_name": csab_name,
        "target_cmb_name": target_cmb_name,
        "frame_count_candidate": frame_count_candidate,
        "frame_slot_count": int(track_json.get("frame_slot_count", frame_count_candidate + 1) or 0),
        "sample_frames": frames,
        "selection_policy": (
            "selected native bind-pose primitive keys; mode-1/mode-2 skinned primitives only, "
            "kept for compatibility with the older passive C++ scanner"
        ),
        "dynamic_draw_policy": (
            "selected native bind-pose primitive keys; mode-1/mode-2 primitives use "
            "pose_world * inverse(bind_world), while mode-0 rigid primitives use the animated bone world matrix, "
            "matching the playable dynamic runtime draw path"
        ),
        "skeleton_bind_translations": [
            [translation.x, translation.y, translation.z]
            for _parent, translation, _rotation, _scale, _matrix in skeleton_bones
        ],
        "selected_primitive_key_count": len(selected_keys),
        "selected_skinned_primitive_count": len(selected_primitives),
        "ignored_rigid_primitive_count": ignored_rigid_primitive_count,
        "dynamic_primitive_count": len(dynamic_primitives),
        "dynamic_skinned_primitive_count": dynamic_skinned_primitive_count,
        "dynamic_rigid_primitive_count": dynamic_rigid_primitive_count,
        "triangle_count": triangle_count,
        "dynamic_triangle_count": dynamic_triangle_count,
        "selected_unique_vertex_rows": selected_unique_vertex_rows,
        "dynamic_unique_vertex_rows": dynamic_unique_vertex_rows,
        "counts": {
            "sampled_frames": len(frames),
            "sampled_vertex_rows": aggregate["sampled_vertex_rows"],
            "finite_position_rows": aggregate["finite_position_rows"],
            "finite_normal_rows": aggregate["finite_normal_rows"],
            "finite_pose_rows": aggregate["finite_pose_rows"],
            "changed_position_rows": aggregate["changed_position_rows"],
            "max_position_delta_from_bind": round_float(max_position_delta),
            "validation_error_count": validation_error_count,
        },
        "dynamic_counts": {
            "sampled_frames": len(frames),
            "sampled_vertex_rows": dynamic_aggregate["sampled_vertex_rows"],
            "finite_position_rows": dynamic_aggregate["finite_position_rows"],
            "finite_normal_rows": dynamic_aggregate["finite_normal_rows"],
            "finite_pose_rows": dynamic_aggregate["finite_pose_rows"],
            "changed_position_rows": dynamic_aggregate["changed_position_rows"],
            "max_position_delta_from_bind": round_float(dynamic_max_position_delta),
            "validation_error_count": dynamic_validation_error_count,
        },
        "position_bounds": rounded_vec3_bounds(aggregate_bounds),
        "dynamic_position_bounds": rounded_vec3_bounds(dynamic_aggregate_bounds),
        "frames": frame_records,
        "dynamic_frames": dynamic_frame_records,
    }


def selected_primitive_keys(native_manifest: dict[str, object]) -> frozenset[tuple[int, int]]:
    keys = native_manifest.get("selection_primitive_keys", [])
    if not isinstance(keys, list):
        return frozenset()
    out: set[tuple[int, int]] = set()
    for key in keys:
        if not isinstance(key, dict):
            continue
        mesh_index = key.get("mesh_index")
        primitive_index = key.get("primitive_index")
        if isinstance(mesh_index, int) and isinstance(primitive_index, int):
            out.add((mesh_index, primitive_index))
    if not out:
        raise ParseError("native bind-pose manifest has no selected primitive keys")
    return frozenset(out)


def selected_skinned_draw_primitives(
    bind_pose_json: dict[str, object],
    selected_keys: frozenset[tuple[int, int]],
) -> tuple[list[dict[str, object]], int]:
    meshes = bind_pose_json.get("meshes", [])
    if not isinstance(meshes, list):
        raise ParseError("bind-pose export has no mesh array")
    selected: list[dict[str, object]] = []
    ignored_rigid = 0
    for mesh in meshes:
        if not isinstance(mesh, dict):
            continue
        mesh_index = json_int(mesh.get("mesh_index"))
        primitives = mesh.get("primitives", [])
        if not isinstance(primitives, list):
            continue
        for primitive in primitives:
            if not isinstance(primitive, dict):
                continue
            primitive_index = json_int(primitive.get("primitive_index"))
            if (mesh_index, primitive_index) not in selected_keys:
                continue
            if json_int(primitive.get("skinning_mode"), 0) == 0:
                ignored_rigid += 1
                continue
            selected.append(primitive)
    if not selected:
        raise ParseError("selected primitive keys yielded no skinned draw primitives")
    return selected, ignored_rigid


def selected_dynamic_draw_primitives(
    bind_pose_json: dict[str, object],
    selected_keys: frozenset[tuple[int, int]],
) -> tuple[list[dict[str, object]], int, int]:
    meshes = bind_pose_json.get("meshes", [])
    if not isinstance(meshes, list):
        raise ParseError("bind-pose export has no mesh array")
    selected: list[dict[str, object]] = []
    skinned_count = 0
    rigid_count = 0
    for mesh in meshes:
        if not isinstance(mesh, dict):
            continue
        mesh_index = json_int(mesh.get("mesh_index"))
        primitives = mesh.get("primitives", [])
        if not isinstance(primitives, list):
            continue
        for primitive in primitives:
            if not isinstance(primitive, dict):
                continue
            primitive_index = json_int(primitive.get("primitive_index"))
            if (mesh_index, primitive_index) not in selected_keys:
                continue
            skinning_mode = json_int(primitive.get("skinning_mode"), 0)
            if skinning_mode == 0:
                rigid_primitive_bone_index(primitive)
                rigid_count += 1
            elif skinning_mode in (1, 2):
                skinned_count += 1
            else:
                raise ParseError(f"selected primitive has unsupported skinning mode {skinning_mode}")
            selected.append(primitive)
    if len(selected) != len(selected_keys):
        raise ParseError("selected primitive keys did not resolve to all dynamic draw primitives")
    if not selected:
        raise ParseError("selected primitive keys yielded no dynamic draw primitives")
    return selected, skinned_count, rigid_count


def primitive_triangle_count(primitive: dict[str, object]) -> int:
    triangle_count = primitive.get("triangle_count")
    if isinstance(triangle_count, int) and not isinstance(triangle_count, bool):
        return triangle_count
    indices = primitive.get("indices", [])
    if isinstance(indices, list):
        return len(indices) // 3
    return 0


def rigid_primitive_bone_index(primitive: dict[str, object]) -> int:
    palette = primitive.get("bone_palette")
    if isinstance(palette, list):
        bones = {
            int(item)
            for item in palette
            if isinstance(item, int) and not isinstance(item, bool) and item >= 0
        }
        if len(bones) == 1:
            return next(iter(bones))

    influence_bones: set[int] = set()
    vertices = primitive.get("vertices", [])
    if isinstance(vertices, list):
        for vertex in vertices:
            if not isinstance(vertex, dict):
                continue
            influences = vertex.get("influences", [])
            if not isinstance(influences, list):
                continue
            for influence in influences:
                if not isinstance(influence, dict):
                    continue
                bone_index = influence.get("bone_index")
                weight = float(influence.get("weight", 0.0) or 0.0)
                if isinstance(bone_index, int) and not isinstance(bone_index, bool) and weight > 0.0:
                    influence_bones.add(bone_index)
    if len(influence_bones) == 1:
        return next(iter(influence_bones))
    raise ParseError("selected rigid primitive does not resolve to exactly one animated bone")


def bind_pose_skeleton_bones(
    bind_pose_json: dict[str, object],
) -> tuple[tuple[int, Vec3, Vec3, Vec3, Matrix4], ...]:
    bones = bind_pose_json.get("skeleton_bones", [])
    if not isinstance(bones, list) or not bones:
        raise ParseError("bind-pose export has no skeleton_bones array")
    out: list[tuple[int, Vec3, Vec3, Vec3, Matrix4]] = []
    for expected_index, bone in enumerate(bones):
        if not isinstance(bone, dict):
            raise ParseError("bind-pose skeleton bone is not an object")
        if json_int(bone.get("index")) != expected_index:
            raise ParseError("bind-pose skeleton bone indices are not dense")
        matrix = matrix4_from_json(bone.get("bind_world_matrix"))
        out.append(
            (
                json_int(bone.get("parent_index")),
                list_to_vec3(bone.get("translation")),
                list_to_vec3(bone.get("rotation")),
                list_to_vec3(bone.get("scale")),
                matrix,
            )
        )
    return tuple(out)


def matrix4_from_json(value: object) -> Matrix4:
    if not isinstance(value, list) or len(value) != 4:
        raise ParseError("expected a 4x4 matrix")
    rows: list[tuple[float, float, float, float]] = []
    for row in value:
        if not isinstance(row, list) or len(row) != 4:
            raise ParseError("expected a 4x4 matrix")
        rows.append(tuple(float(item) for item in row))  # type: ignore[arg-type]
    return tuple(rows)  # type: ignore[return-value]


def selected_draw_world_transforms(
    skeleton_bones: tuple[tuple[int, Vec3, Vec3, Vec3, Matrix4], ...],
    tracks: list[object],
    frame: int,
) -> tuple[Matrix4, ...]:
    translations = [bone[1] for bone in skeleton_bones]
    rotations = [bone[2] for bone in skeleton_bones]
    scales = [bone[3] for bone in skeleton_bones]

    for track in tracks:
        if not isinstance(track, dict):
            raise ParseError("CSAB track record is not an object")
        bone_index = json_int(track.get("bone_index"))
        if bone_index < 0 or bone_index >= len(skeleton_bones):
            raise ParseError("CSAB track references a bone outside the skeleton")
        translation = skeleton_bones[bone_index][1]
        rotation = skeleton_bones[bone_index][2]
        scale = skeleton_bones[bone_index][3]
        channels = track.get("channels", [])
        if not isinstance(channels, list):
            raise ParseError("CSAB track has no channel array")
        for channel in channels:
            if not isinstance(channel, dict):
                continue
            value = sample_csab_track_channel(channel, float(frame))
            if math.isfinite(value):
                if not csab_channel_applies_to_target(channel, skeleton_bones[bone_index][1], value):
                    continue
                slot = json_int(channel.get("slot"))
                if slot == 0:
                    translation = Vec3(value, translation.y, translation.z)
                elif slot == 1:
                    translation = Vec3(translation.x, value, translation.z)
                elif slot == 2:
                    translation = Vec3(translation.x, translation.y, value)
                elif slot == 3:
                    rotation = Vec3(value, rotation.y, rotation.z)
                elif slot == 4:
                    rotation = Vec3(rotation.x, value, rotation.z)
                elif slot == 5:
                    rotation = Vec3(rotation.x, rotation.y, value)
                elif slot == 6:
                    scale = Vec3(value, scale.y, scale.z)
                elif slot == 7:
                    scale = Vec3(scale.x, value, scale.z)
                elif slot == 8:
                    scale = Vec3(scale.x, scale.y, value)
        translations[bone_index] = translation
        rotations[bone_index] = rotation
        scales[bone_index] = scale

    world: list[Matrix4] = []
    for bone_index, (parent_index, _translation, _rotation, _scale, _matrix) in enumerate(skeleton_bones):
        local = local_trs_matrix(translations[bone_index], rotations[bone_index], scales[bone_index])
        if parent_index < 0:
            world.append(local)
        else:
            if parent_index >= bone_index:
                raise ParseError("bind-pose skeleton parent order is not topological")
            world.append(multiply_matrix(world[parent_index], local))
    return tuple(world)


def csab_channel_applies_to_target(
    channel: dict[str, object],
    base_translation: Vec3,
    value: float,
) -> bool:
    if channel.get("apply_to_target") is False:
        return False

    slot = json_int(channel.get("slot"))
    if slot not in (0, 1, 2):
        return True
    if normalize_resource_path(channel.get("encoding")) != "constant_f32":
        return True

    base_value = (base_translation.x, base_translation.y, base_translation.z)[slot]
    return abs(value - base_value) <= constant_translation_bind_tolerance(base_value)


def constant_translation_bind_tolerance(base_value: float) -> float:
    return max(
        CONSTANT_TRANSLATION_BIND_ABSOLUTE_TOLERANCE,
        abs(base_value) * CONSTANT_TRANSLATION_BIND_RELATIVE_TOLERANCE,
    )


def local_trs_matrix(translation: Vec3, rotation: Vec3, scale: Vec3) -> Matrix4:
    return multiply_matrix(
        matrix_translate(translation),
        multiply_matrix(
            matrix_rotate_z(rotation.z),
            multiply_matrix(matrix_rotate_y(rotation.y), multiply_matrix(matrix_rotate_x(rotation.x), matrix_scale(scale))),
        ),
    )


def sample_csab_track_channel(channel: dict[str, object], frame: float) -> float:
    encoding = normalize_resource_path(channel.get("encoding"))
    if encoding in {"constant_f32", "constant_s16_rotation"}:
        return float(channel.get("value", 0.0) or 0.0)
    keys = channel.get("keys", [])
    if not isinstance(keys, list) or not keys:
        return math.nan
    first_key = keys[0]
    last_key = keys[-1]
    if not isinstance(first_key, dict) or not isinstance(last_key, dict):
        return math.nan
    if frame <= float(first_key.get("frame", 0.0) or 0.0):
        return float(first_key.get("value", math.nan))
    if frame >= float(last_key.get("frame", 0.0) or 0.0):
        return float(last_key.get("value", math.nan))

    for index in range(len(keys) - 1):
        left = keys[index]
        right = keys[index + 1]
        if not isinstance(left, dict) or not isinstance(right, dict):
            continue
        right_frame = float(right.get("frame", 0.0) or 0.0)
        if frame >= right_frame:
            continue
        left_frame = float(left.get("frame", 0.0) or 0.0)
        interval = right_frame - left_frame
        if interval <= 0.0:
            return float(left.get("value", 0.0) or 0.0)
        left_value = float(left.get("value", 0.0) or 0.0)
        right_value = float(right.get("value", 0.0) or 0.0)
        if encoding == "keyed_s16_rotation_hermite":
            right_value = unwrap_angle_near(left_value, right_value)
        t = (frame - left_frame) / interval
        return cubic_hermite(
            t,
            interval / 30.0,
            left_value,
            right_value,
            float(left.get("right_tangent", 0.0) or 0.0),
            float(right.get("left_tangent", 0.0) or 0.0),
        )
    return float(last_key.get("value", math.nan))


def cubic_hermite(t: float, interval: float, y0: float, y1: float, m0: float, m1: float) -> float:
    t2 = t * t
    t3 = t2 * t
    return (
        ((2.0 * t3) - (3.0 * t2) + 1.0) * y0
        + ((3.0 * t2) - (2.0 * t3)) * y1
        + (t3 - (2.0 * t2) + t) * m0 * interval
        + (t3 - t2) * m1 * interval
    )


def unwrap_angle_near(reference: float, value: float) -> float:
    while value - reference > math.pi:
        value -= math.tau
    while value - reference < -math.pi:
        value += math.tau
    return value


def json_int(value: object, fallback: int = -1) -> int:
    if isinstance(value, int) and not isinstance(value, bool):
        return value
    return fallback


def select_skinning_diagnostic_records(
    character_manifest: dict[str, object],
    records: list[dict[str, object]],
) -> list[dict[str, object]]:
    diagnostics = character_manifest.get("runtime_diagnostics", {})
    diagnostics = diagnostics if isinstance(diagnostics, dict) else {}
    raw_preferred_csab_names = diagnostics.get("skinning_diagnostic_csab_names", [])
    preferred_csab_names = (
        [
            normalize_resource_path(csab_name)
            for csab_name in raw_preferred_csab_names
            if normalize_resource_path(csab_name)
        ]
        if isinstance(raw_preferred_csab_names, list)
        else []
    )
    by_name = {
        normalize_resource_path(record.get("csab_name")): record
        for record in records
    }
    for csab_name in preferred_csab_names:
        record = by_name.get(csab_name)
        if record is not None:
            return [record]
    return records[:1]


def skinning_diagnostic_sample(
    pose_batch_path: Path,
    pose_batch: dict[str, object],
    record: dict[str, object],
) -> dict[str, object]:
    sample: dict[str, object] = {
        "status": "missing_pose_sample",
        "csab_name": normalize_resource_path(record.get("csab_name")),
        "target_cmb_name": normalize_resource_path(record.get("target_cmb_name")),
        "pose_sample_export": normalize_resource_path(record.get("pose_sample_export")),
        "frame_count_candidate": record.get("frame_count_candidate"),
        "sample_frames": record.get("sample_frames", []),
        "counts": record.get("counts", {}),
        "pose_counts": record.get("pose_counts", {}),
        "bind_pose_counts": record.get("bind_pose_counts", {}),
    }
    pose_sample_path = resolve_pose_sample_path(pose_batch_path, pose_batch, record)
    if not pose_sample_path.is_file():
        return sample

    pose_sample = read_json(pose_sample_path)
    validation = pose_sample.get("validation", {})
    counts = pose_sample.get("counts", {})
    sample.update(
        {
            "status": "ready",
            "pose_sample_path": str(pose_sample_path),
            "validation_status": validation.get("status") if isinstance(validation, dict) else None,
            "validation_error_count": counts.get("validation_error_count", 1) if isinstance(counts, dict) else 1,
            "position_bounds": pose_sample.get("position_bounds", {}),
            "frames": skinning_diagnostic_frames(pose_sample),
        }
    )
    return sample


def resolve_pose_sample_path(
    manifest_path: Path,
    manifest: dict[str, object],
    record: dict[str, object],
) -> Path:
    pose_export = Path(str(record.get("pose_sample_export", "")))
    if pose_export.is_absolute() or pose_export.exists():
        return pose_export
    output = Path(str(manifest.get("output", manifest_path.parent)))
    return output / pose_export


def skinning_diagnostic_frames(pose_sample: dict[str, object]) -> list[dict[str, object]]:
    frames = pose_sample.get("frames", [])
    if not isinstance(frames, list):
        return []
    out: list[dict[str, object]] = []
    for frame in frames:
        if not isinstance(frame, dict):
            continue
        out.append(
            {
                "frame": frame.get("frame"),
                "counts": frame.get("counts", {}),
                "position_bounds": frame.get("position_bounds", {}),
                "normal_length_bounds": frame.get("normal_length_bounds", {}),
            }
        )
    return out


def runtime_animation_resource_record(record: dict[str, object]) -> dict[str, object]:
    csab_name = normalize_resource_path(record.get("csab_name"))
    frame_slot_count = optional_int(record.get("frame_slot_count"))
    encoding_counts = record.get("encoding_counts", {})
    return {
        "csab_name": csab_name,
        "stem": animation_stem(csab_name),
        "resource_path": normalize_resource_path(record.get("resource_path")),
        "frame_slot_count": frame_slot_count,
        "frame_count_candidate": frame_slot_count - 1 if frame_slot_count is not None and frame_slot_count > 0 else None,
        "frame_domain": "inclusive_0_to_frame_count_candidate",
        "track_count": optional_int(record.get("track_count")),
        "channel_count": optional_int(record.get("channel_count")),
        "keyframe_count": optional_int(record.get("keyframe_count")),
        "encoding_counts": encoding_counts if isinstance(encoding_counts, dict) else {},
        "target_support_status": record.get("target_support_status"),
        "target_resolution_status": record.get("target_resolution_status"),
        "validation_status": record.get("validation_status"),
    }


def animation_playback_contract(
    target: dict[str, object],
    native_bind_pose: dict[str, object],
    animation_resources: list[dict[str, object]],
    n64_reference_validation: dict[str, object],
) -> dict[str, object]:
    frame_slot_counts = [
        int(record["frame_slot_count"])
        for record in animation_resources
        if isinstance(record.get("frame_slot_count"), int)
    ]
    return {
        "format": "oot3d_character_animation_playback_contract_v1",
        "track_resource_format": "oot3d_csab_skeleton_track_export_v1",
        "track_count": len(animation_resources),
        "frame_slot_min": min(frame_slot_counts, default=0),
        "frame_slot_max": max(frame_slot_counts, default=0),
        "default_fps": 60.0,
        "time_domain": {
            "frame_zero": 0,
            "frame_count_candidate_is_inclusive_max_frame": True,
            "frame_slot_count": "frame_count_candidate + 1",
            "loop_policy": "wrap_or_clamp_is_runtime_state; decoded CSAB tracks preserve authored frame slots",
        },
        "sampling_policy": {
            "base_transform_source": "CMB skeleton bone base_transform",
            "missing_channel_policy": "inherit CMB base transform component",
            "translation_and_scale": "constant_f32 or keyed_f32_hermite",
            "channel_application": (
                "runtime SkelAnime channel masks select translation, rotation, and scale components; "
                "unselected components inherit the target CMB bind transform"
            ),
            "rotation": "constant_s16_rotation or keyed_s16_rotation_hermite",
            "rotation_unwrap": "sample keyed rotation channels on the nearest continuous angular branch before Hermite interpolation",
            "world_transform_order": "compose local TRS down the CMB skeleton parent hierarchy",
        },
        "skinning_policy": {
            "bind_pose_source": "decoded CMB source_position/source_normal/influences",
            "bind_world_source": "CMB skeleton bind pose world transforms",
            "skin_matrix": "pose_world * inverse(bind_world)",
            "selected_mesh_source": native_bind_pose.get("selection_source"),
            "selected_primitive_key_count": native_bind_pose.get("selection_primitive_key_count"),
            "position_scale_source": "runtime_transform.model_scale for Shipwright draws; selection profile scale remains GLB diagnostics only",
        },
        "validation_policy": {
            "glb_reference": "export-skinned-animation-glb uses the same CSAB sampling and skinning policy",
            "n64_reference_status": n64_reference_validation.get("status"),
            "n64_pose_error_metric_status": n64_reference_validation.get("n64_pose_error_metric_status"),
            "n64_pose_error_metric_acceptance_status": n64_reference_validation.get(
                "n64_pose_error_metric_acceptance_status"
            ),
            "runtime_gate": "do not replace N64 player until skeleton, CSAB sampling, skinning, material, and face-state players pass their checks",
        },
        "target": {
            "archive_path": target.get("archive_path"),
            "target_cmb_name": target.get("target_cmb_name"),
            "model_name": target.get("model_name"),
            "bone_count": target.get("bone_count"),
        },
    }


def animation_time_source_contract(
    runtime_semantics: dict[str, object] | None,
    profile_id: str,
    animation_resources: list[dict[str, object]],
) -> dict[str, object]:
    if runtime_semantics is None:
        return {
            "format": ANIMATION_TIME_SOURCE_CONTRACT_FORMAT,
            "status": "not_provided",
            "default_source": "skel_animation_clock",
            "bindings": [],
        }
    if runtime_semantics.get("format") != CHARACTER_RUNTIME_SEMANTICS_FORMAT:
        raise ParseError("runtime semantics input has an unsupported format")
    if runtime_semantics.get("profile_id") != profile_id:
        raise ParseError("runtime semantics profile_id does not match the character conversion profile")
    source_contract = runtime_semantics.get("animation_time_source_contract")
    if not isinstance(source_contract, dict) or source_contract.get("format") != ANIMATION_TIME_SOURCE_CONTRACT_FORMAT:
        raise ParseError("runtime semantics input has no supported animation time-source contract")
    if source_contract.get("default_source") != "skel_animation_clock":
        raise ParseError("animation time-source default must be skel_animation_clock")
    bindings = source_contract.get("bindings")
    if not isinstance(bindings, list):
        raise ParseError("animation time-source bindings must be an array")

    animation_by_csab_name = {
        str(record.get("csab_name", "")): record
        for record in animation_resources
        if isinstance(record, dict) and str(record.get("csab_name", ""))
    }
    available_csab_names = set(animation_by_csab_name)
    canonical_bindings: list[dict[str, object]] = []
    seen_csab_names: set[str] = set()
    for index, binding in enumerate(bindings):
        if not isinstance(binding, dict):
            raise ParseError(f"animation time-source binding {index} is not an object")
        csab_name = str(binding.get("csab_name", ""))
        if not csab_name or csab_name in seen_csab_names:
            raise ParseError(f"animation time-source binding {index} has an empty or duplicate CSAB name")
        if csab_name not in available_csab_names:
            raise ParseError(f"animation time-source binding references an unavailable CSAB: {csab_name}")
        source = str(binding.get("source", ""))
        sample_mode = str(binding.get("sample_mode", ""))
        locomotion_source = source == "player_locomotion_cycle"
        player_timeline_source = source == "player_skel_animation_timeline"
        if (
            (not locomotion_source and not player_timeline_source)
            or sample_mode not in {"direct_scaled_frame", "native_frame_span_over_source_span"}
            or (player_timeline_source and sample_mode != "native_frame_span_over_source_span")
        ):
            raise ParseError(
                f"animation time-source binding {csab_name} uses an unsupported source or sample mode"
            )
        source_frame_span = binding.get("source_frame_span")
        sample_offset = binding.get("sample_offset", 0.0)
        if (
            not isinstance(source_frame_span, (int, float))
            or not math.isfinite(float(source_frame_span))
            or float(source_frame_span) <= 0.0
        ):
            raise ParseError(f"animation time-source binding {csab_name} has an invalid source frame span")
        if not isinstance(sample_offset, (int, float)) or not math.isfinite(float(sample_offset)):
            raise ParseError(f"animation time-source binding {csab_name} has an invalid sample offset")
        canonical_binding: dict[str, object] = {
            "csab_name": csab_name,
            "source": source,
            "sample_mode": (
                "direct_clamped_normalized_frame"
                if player_timeline_source
                else "direct_scaled_frame"
            ),
            "source_frame_span": float(source_frame_span),
            "sample_offset": float(sample_offset),
        }
        if sample_mode == "native_frame_span_over_source_span":
            animation = animation_by_csab_name[csab_name]
            native_frame_span = animation.get("frame_slot_count")
            if (
                not isinstance(native_frame_span, int)
                or isinstance(native_frame_span, bool)
                or native_frame_span <= 0
            ):
                raise ParseError(
                    f"animation time-source binding {csab_name} has no valid native frame span"
                )
            if player_timeline_source:
                if float(source_frame_span) <= 1.0 or native_frame_span <= 1:
                    raise ParseError(
                        f"animation time-source binding {csab_name} cannot normalize a single-frame timeline"
                    )
                canonical_binding["sample_scale"] = (
                    (native_frame_span - 1) / (float(source_frame_span) - 1.0)
                )
                canonical_binding["scaffold_playback_scale"] = (
                    float(source_frame_span) / native_frame_span
                )
                canonical_binding["playback_mode"] = "once_full_span"
            else:
                canonical_binding["sample_scale"] = native_frame_span / float(source_frame_span)
            canonical_binding["sample_scale_source"] = (
                "native_csab_frame_slot_count_over_source_frame_span"
            )
            canonical_binding["native_frame_span"] = native_frame_span
        else:
            sample_scale = binding.get("sample_scale")
            if (
                not isinstance(sample_scale, (int, float))
                or not math.isfinite(float(sample_scale))
                or float(sample_scale) <= 0.0
            ):
                raise ParseError(f"animation time-source binding {csab_name} has an invalid sample scale")
            canonical_binding["sample_scale"] = float(sample_scale)
            canonical_binding["sample_scale_source"] = "explicit_runtime_semantics"
        canonical_bindings.append(canonical_binding)
        seen_csab_names.add(csab_name)

    evidence = source_contract.get("evidence", {})
    if not isinstance(evidence, dict):
        raise ParseError("animation time-source evidence must be an object")
    return {
        "format": ANIMATION_TIME_SOURCE_CONTRACT_FORMAT,
        "status": "ready",
        "default_source": "skel_animation_clock",
        "bindings": canonical_bindings,
        "evidence": evidence,
    }


def skel_anime_sampling_contract(
    runtime_semantics: dict[str, object],
    profile_id: str,
) -> dict[str, object] | None:
    if runtime_semantics.get("format") != CHARACTER_RUNTIME_SEMANTICS_FORMAT:
        raise ParseError("runtime semantics input has an unsupported format")
    if runtime_semantics.get("profile_id") != profile_id:
        raise ParseError("runtime semantics profile_id does not match the character conversion profile")
    contract = runtime_semantics.get("skel_anime_sampling_contract")
    if contract is None:
        return None
    if not isinstance(contract, dict) or contract.get("format") != SKEL_ANIME_SAMPLING_CONTRACT_FORMAT:
        raise ParseError("runtime semantics input has no supported SkelAnime sampling contract")
    values: dict[str, int] = {}
    for key in (
        "frame_data_path",
        "special_bone",
        "default_channel_mask",
        "special_bone_channel_mask",
    ):
        value = contract.get(key)
        if not isinstance(value, int) or isinstance(value, bool):
            raise ParseError(f"SkelAnime sampling {key} is invalid")
        values[key] = value
    if values["frame_data_path"] != 1:
        raise ParseError("only the decoded native SkelAnime frame-data path 1 is supported")
    if values["special_bone"] < 0:
        raise ParseError("SkelAnime sampling special_bone is invalid")
    if not 0 <= values["default_channel_mask"] <= 7 or not 0 <= values["special_bone_channel_mask"] <= 7:
        raise ParseError("SkelAnime sampling channel mask is outside the native three-bit domain")
    base_translation = contract.get("base_translation")
    if (
        not isinstance(base_translation, list)
        or len(base_translation) != 3
        or any(
            not isinstance(component, (int, float))
            or isinstance(component, bool)
            or not math.isfinite(component)
            for component in base_translation
        )
    ):
        raise ParseError("SkelAnime sampling base_translation must be a finite vec3")
    evidence = contract.get("evidence", {})
    if not isinstance(evidence, dict):
        raise ParseError("SkelAnime sampling evidence must be an object")
    return {
        "format": SKEL_ANIME_SAMPLING_CONTRACT_FORMAT,
        **values,
        "base_translation": [float(component) for component in base_translation],
        "evidence": evidence,
    }


def root_motion_ownership_contract(
    runtime_semantics: dict[str, object] | None,
    profile_id: str,
) -> dict[str, object]:
    if runtime_semantics is None:
        return {
            "format": ROOT_MOTION_OWNERSHIP_CONTRACT_FORMAT,
            "status": "not_provided",
        }
    if runtime_semantics.get("format") != CHARACTER_RUNTIME_SEMANTICS_FORMAT:
        raise ParseError("runtime semantics input has an unsupported format")
    if runtime_semantics.get("profile_id") != profile_id:
        raise ParseError("runtime semantics profile_id does not match the character conversion profile")
    contract = runtime_semantics.get("root_motion_ownership_contract")
    if not isinstance(contract, dict) or contract.get("format") != ROOT_MOTION_OWNERSHIP_CONTRACT_FORMAT:
        raise ParseError("runtime semantics input has no supported root-motion ownership contract")
    movement_enabled_flag = contract.get("movement_enabled_flag")
    update_y_flag = contract.get("update_y_flag")
    if (
        not isinstance(movement_enabled_flag, int)
        or isinstance(movement_enabled_flag, bool)
        or movement_enabled_flag <= 0
        or movement_enabled_flag > 0xFF
    ):
        raise ParseError("root-motion ownership movement-enabled flag is invalid")
    if (
        not isinstance(update_y_flag, int)
        or isinstance(update_y_flag, bool)
        or update_y_flag <= 0
        or update_y_flag > 0xFF
    ):
        raise ParseError("root-motion ownership update-y flag is invalid")
    expected_values = {
        "translation_policy": "align_only_controller_consumed_axes",
        "xz_ownership": "controller_when_movement_enabled",
        "y_ownership": "controller_when_movement_enabled_and_update_y",
        "root_rotation_policy": "preserve_authored_oot3d",
    }
    for key, expected in expected_values.items():
        if contract.get(key) != expected:
            raise ParseError(f"root-motion ownership {key} is unsupported")
    evidence = contract.get("evidence", {})
    if not isinstance(evidence, dict):
        raise ParseError("root-motion ownership evidence must be an object")
    return {
        "format": ROOT_MOTION_OWNERSHIP_CONTRACT_FORMAT,
        "status": "ready",
        "movement_enabled_flag": movement_enabled_flag,
        "update_y_flag": update_y_flag,
        **expected_values,
        "evidence": evidence,
    }


def animation_semantic_binding_contract(
    runtime_semantics: dict[str, object] | None,
    profile_id: str,
    animation_resources: list[dict[str, object]],
) -> dict[str, object]:
    if runtime_semantics is None:
        return {
            "format": ANIMATION_SEMANTIC_BINDING_CONTRACT_FORMAT,
            "status": "not_declared",
            "bindings": [],
        }
    if runtime_semantics.get("format") != CHARACTER_RUNTIME_SEMANTICS_FORMAT:
        raise ParseError("character runtime semantics format is unsupported")
    if str(runtime_semantics.get("profile_id", "")) != profile_id:
        raise ParseError("character runtime semantics profile does not match the character manifest")
    contract = runtime_semantics.get("animation_semantic_binding_contract")
    if contract is None:
        return {
            "format": ANIMATION_SEMANTIC_BINDING_CONTRACT_FORMAT,
            "status": "not_declared",
            "bindings": [],
        }
    if not isinstance(contract, dict) or contract.get("format") != ANIMATION_SEMANTIC_BINDING_CONTRACT_FORMAT:
        raise ParseError("animation semantic binding contract format is unsupported")
    bindings = contract.get("bindings", [])
    if not isinstance(bindings, list):
        raise ParseError("animation semantic binding contract bindings must be an array")

    available_csab_names = {
        normalize_resource_path(animation.get("csab_name"))
        for animation in animation_resources
        if normalize_resource_path(animation.get("csab_name"))
    }
    canonical_bindings: list[dict[str, object]] = []
    seen_n64_names: set[str] = set()
    for index, binding in enumerate(bindings):
        if not isinstance(binding, dict):
            raise ParseError(f"animation semantic binding {index} must be an object")
        n64_name = normalize_animation_lookup_key(binding.get("n64_name"))
        csab_name = normalize_resource_path(binding.get("csab_name"))
        if not n64_name or n64_name in seen_n64_names:
            raise ParseError(f"animation semantic binding {index} has an empty or duplicate N64 name")
        if csab_name not in available_csab_names:
            raise ParseError(
                f"animation semantic binding {n64_name} references an unavailable CSAB: {csab_name}"
            )
        canonical = {
            "n64_name": n64_name,
            "csab_name": csab_name,
        }
        for field in (
            "oot3d_group_index",
            "csab_type_local_index",
            "animation_type_indices",
        ):
            if field in binding:
                canonical[field] = binding[field]
        canonical_bindings.append(canonical)
        seen_n64_names.add(n64_name)

    evidence = contract.get("evidence", {})
    if not isinstance(evidence, dict):
        raise ParseError("animation semantic binding evidence must be an object")
    return {
        "format": ANIMATION_SEMANTIC_BINDING_CONTRACT_FORMAT,
        "status": "ready",
        "bindings": canonical_bindings,
        "evidence": evidence,
    }


def animation_controller_contract(
    runtime_semantics: dict[str, object] | None,
    profile_id: str,
    animation_resources: list[dict[str, object]],
) -> dict[str, object]:
    if runtime_semantics is None:
        return {
            "format": ANIMATION_CONTROLLER_CONTRACT_FORMAT,
            "status": "not_declared",
            "controllers": [],
        }
    if runtime_semantics.get("format") != CHARACTER_RUNTIME_SEMANTICS_FORMAT:
        raise ParseError("character runtime semantics format is unsupported")
    if str(runtime_semantics.get("profile_id", "")) != profile_id:
        raise ParseError("character runtime semantics profile does not match the character manifest")
    contract = runtime_semantics.get("animation_controller_contract")
    if contract is None:
        return {
            "format": ANIMATION_CONTROLLER_CONTRACT_FORMAT,
            "status": "not_declared",
            "controllers": [],
        }
    if not isinstance(contract, dict) or contract.get("format") != ANIMATION_CONTROLLER_CONTRACT_FORMAT:
        raise ParseError("animation controller contract format is unsupported")
    controllers = contract.get("controllers", [])
    if not isinstance(controllers, list):
        raise ParseError("animation controller contract controllers must be an array")

    available_csab_names = {
        normalize_resource_path(animation.get("csab_name"))
        for animation in animation_resources
        if normalize_resource_path(animation.get("csab_name"))
    }
    canonical_controllers: list[dict[str, object]] = []
    seen_ids: set[str] = set()
    for controller_index, controller in enumerate(controllers):
        if not isinstance(controller, dict):
            raise ParseError(f"animation controller {controller_index} must be an object")
        controller_id = normalize_animation_lookup_key(controller.get("id"))
        semantic = normalize_animation_lookup_key(controller.get("notification_semantic"))
        if not controller_id or controller_id in seen_ids or not semantic:
            raise ParseError(f"animation controller {controller_index} has an invalid or duplicate identity")
        variants = controller.get("variants", [])
        if not isinstance(variants, list) or not variants:
            raise ParseError(f"animation controller {controller_id} has no variants")
        canonical_variants: list[dict[str, object]] = []
        seen_types: set[int] = set()
        for variant_index, variant in enumerate(variants):
            if not isinstance(variant, dict):
                raise ParseError(f"animation controller {controller_id} variant {variant_index} must be an object")
            animation_type_index = variant.get("animation_type_index")
            if not isinstance(animation_type_index, int) or animation_type_index < 0 or animation_type_index in seen_types:
                raise ParseError(f"animation controller {controller_id} has an invalid or duplicate animation type")
            walk_csab_name = normalize_resource_path(variant.get("walk_csab_name"))
            run_csab_name = normalize_resource_path(variant.get("run_csab_name"))
            if walk_csab_name not in available_csab_names or run_csab_name not in available_csab_names:
                raise ParseError(f"animation controller {controller_id} references an unavailable CSAB")
            canonical_variants.append({
                "animation_type_index": animation_type_index,
                "walk_csab_name": walk_csab_name,
                "run_csab_name": run_csab_name,
            })
            seen_types.add(animation_type_index)
        canonical = {
            "id": controller_id,
            "notification_semantic": semantic,
            "phase_source": str(controller.get("phase_source", "")),
            "source_frame_span": float(controller.get("source_frame_span", 0.0)),
            "blend_source": str(controller.get("blend_source", "")),
            "blend_threshold": float(controller.get("blend_threshold", 0.0)),
            "blend_scale": float(controller.get("blend_scale", 0.0)),
            "warmup_source": str(controller.get("warmup_source", "")),
            "variants": canonical_variants,
        }
        numeric_values = (
            canonical["source_frame_span"],
            canonical["blend_threshold"],
            canonical["blend_scale"],
        )
        if not all(math.isfinite(value) for value in numeric_values) or canonical["source_frame_span"] <= 0.0:
            raise ParseError(f"animation controller {controller_id} has invalid numeric values")
        canonical_controllers.append(canonical)
        seen_ids.add(controller_id)

    evidence = contract.get("evidence", {})
    if not isinstance(evidence, dict):
        raise ParseError("animation controller evidence must be an object")
    return {
        "format": ANIMATION_CONTROLLER_CONTRACT_FORMAT,
        "status": "ready",
        "controllers": canonical_controllers,
        "evidence": evidence,
    }


def animation_lookup(
    animation_resources: list[dict[str, object]],
    character_manifest: dict[str, object] | None = None,
    semantic_binding_contract: dict[str, object] | None = None,
) -> dict[str, object]:
    by_csab_name: dict[str, int] = {}
    by_stem: dict[str, list[int]] = {}
    for index, record in enumerate(animation_resources):
        csab_name = str(record.get("csab_name") or "")
        stem = str(record.get("stem") or "")
        if csab_name:
            by_csab_name[csab_name] = index
        if stem:
            by_stem.setdefault(stem, []).append(index)
    lookup: dict[str, object] = {
        "resource_array": "native_resources.animations",
        "by_csab_name": by_csab_name,
        "by_stem": dict(sorted(by_stem.items())),
    }
    lookup.update(
        n64_animation_runtime_lookup(
            character_manifest,
            by_csab_name,
            semantic_binding_contract,
        )
    )
    return lookup


def n64_animation_runtime_lookup(
    character_manifest: dict[str, object] | None,
    by_csab_name: dict[str, int],
    semantic_binding_contract: dict[str, object] | None = None,
) -> dict[str, object]:
    runtime_mapping: dict[str, object] = {}
    entries: list[object] = []
    if character_manifest is not None:
        n64_reference_path = optional_source_manifest_path(character_manifest, "n64_animation_reference_audit")
        if n64_reference_path is not None and n64_reference_path.is_file():
            n64_reference = read_json(n64_reference_path)
            candidate_mapping = n64_reference.get("candidate_mapping", {})
            runtime_mapping = (
                candidate_mapping.get("runtime_mapping", {})
                if isinstance(candidate_mapping, dict)
                else {}
            )
            candidate_entries = runtime_mapping.get("entries", []) if isinstance(runtime_mapping, dict) else []
            if isinstance(candidate_entries, list):
                entries = candidate_entries

    by_n64_name: dict[str, int] = {}
    by_n64_data_name: dict[str, int] = {}
    by_n64_resource_path: dict[str, int] = {}
    for entry in entries:
        if not isinstance(entry, dict):
            continue
        csab_name = normalize_resource_path(entry.get("csab_name"))
        animation_index = by_csab_name.get(csab_name)
        if animation_index is None:
            continue
        add_animation_lookup_key(by_n64_name, entry.get("n64_name"), animation_index)
        add_animation_lookup_key(by_n64_name, entry.get("n64_stem"), animation_index)
        add_animation_lookup_key(by_n64_data_name, entry.get("n64_data_name"), animation_index)
        add_animation_lookup_key(by_n64_resource_path, entry.get("n64_resource_path"), animation_index)

    semantic_binding_count = 0
    if isinstance(semantic_binding_contract, dict):
        semantic_bindings = semantic_binding_contract.get("bindings", [])
        if isinstance(semantic_bindings, list):
            for entry in semantic_bindings:
                if not isinstance(entry, dict):
                    continue
                csab_name = normalize_resource_path(entry.get("csab_name"))
                animation_index = by_csab_name.get(csab_name)
                if animation_index is None:
                    continue
                key = normalize_animation_lookup_key(entry.get("n64_name"))
                if not key:
                    continue
                by_n64_name[key] = animation_index
                semantic_binding_count += 1

    return {
        "n64_runtime_mapping_policy": (
            runtime_mapping.get("policy")
            if isinstance(runtime_mapping, dict)
            else "non-ambiguous N64/OOT3D animation matches"
        ),
        "native_semantic_binding_policy": "OOT3D code.bin animation groups override heuristic aliases",
        "native_semantic_binding_count": semantic_binding_count,
        "by_n64_name": dict(sorted(by_n64_name.items())),
        "by_n64_data_name": dict(sorted(by_n64_data_name.items())),
        "by_n64_resource_path": dict(sorted(by_n64_resource_path.items())),
    }


def add_animation_lookup_key(out: dict[str, int], value: object, animation_index: int) -> None:
    key = normalize_animation_lookup_key(value)
    if key and key not in out:
        out[key] = animation_index


def normalize_animation_lookup_key(value: object) -> str:
    key = normalize_resource_path(value)
    if key.startswith("__OTR__"):
        key = key[len("__OTR__") :]
    if "/" in key:
        key = key.rsplit("/", 1)[1]
    if "." in key:
        key = key.rsplit(".", 1)[0]
    return key


def animation_stem(csab_name: str) -> str:
    name = normalize_resource_path(csab_name)
    if "/" in name:
        name = name.rsplit("/", 1)[1]
    if "." in name:
        name = name.rsplit(".", 1)[0]
    return name


def optional_int(value: object) -> int | None:
    if isinstance(value, int) and not isinstance(value, bool):
        return value
    return None


def character_native_resource_records(
    character_manifest: dict[str, object],
    target: dict[str, object],
    *,
    generated_resource_dir: Path | None = None,
    anb_export_path: Path | None = None,
) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    bind_pose = target.get("bind_pose")
    if not isinstance(bind_pose, dict):
        raise ParseError("skinned binding target is missing bind_pose")
    bind_source = bind_pose.get("export")
    bind_resource = normalize_resource_path(bind_pose.get("package_entry"))
    if not bind_source or not bind_resource:
        raise ParseError("skinned binding target bind_pose is missing export/package_entry")
    records.append(
        {
            "kind": "bind_pose",
            "source_path": str(bind_source),
            "resource_path": bind_resource,
        }
    )
    append_native_bind_pose_resource_records(records, target)
    append_material_animation_texture_resource_records(
        records,
        character_manifest,
        target,
        generated_resource_dir=generated_resource_dir,
    )
    append_anb_payload_batch_resource_record(records, character_manifest, anb_export_path)
    for animation in target.get("animations", []):
        if not isinstance(animation, dict):
            continue
        source_path = animation.get("track_export_file")
        resource_path = normalize_resource_path(animation.get("track_package_entry"))
        if not source_path or not resource_path:
            continue
        validation = animation.get("validation", {})
        counts = animation.get("counts", {})
        counts = counts if isinstance(counts, dict) else {}
        records.append(
            {
                "kind": "csab_track",
                "source_path": str(source_path),
                "resource_path": resource_path,
                "csab_name": normalize_resource_path(animation.get("csab_name")),
                "frame_slot_count": animation.get("frame_slot_count"),
                "track_count": counts.get("track_count"),
                "channel_count": counts.get("channel_count"),
                "keyframe_count": counts.get("keyframe_count"),
                "encoding_counts": counts.get("encoding_counts", {}),
                "target_support_status": animation.get("target_support_status"),
                "target_resolution_status": animation.get("target_resolution_status"),
                "validation_status": validation.get("status") if isinstance(validation, dict) else None,
            }
        )
    return records


def append_anb_payload_batch_resource_record(
    records: list[dict[str, object]],
    character_manifest: dict[str, object],
    anb_export_path: Path | None,
) -> None:
    if anb_export_path is None:
        return
    profile_id = sanitize_path_part(character_manifest.get("profile_id") or "character")
    source_path = Path(anb_export_path)
    records.append(
        {
            "kind": "anb_payload_batch",
            "source_path": str(source_path),
            "resource_path": f"characters/oot3d/{profile_id}/anb/{sanitize_path_part(source_path.name)}",
        }
    )


def append_material_animation_texture_resource_records(
    records: list[dict[str, object]],
    character_manifest: dict[str, object],
    target: dict[str, object],
    *,
    generated_resource_dir: Path | None,
) -> None:
    material_payloads = character_manifest.get("material_animation_payloads")
    if not isinstance(material_payloads, dict):
        return
    bindings = material_payloads.get("bindings", [])
    if not isinstance(bindings, list) or not bindings:
        return
    if generated_resource_dir is None:
        raise ParseError("generated character resource directory is required for material animation textures")

    model = load_target_cmb_model_for_generated_resources(target)
    texture_by_name = {normalize_resource_path(texture.name): texture for texture in model.textures}
    native_bind_pose = target.get("native_bind_pose")
    native_bind_pose = native_bind_pose if isinstance(native_bind_pose, dict) else {}
    texture_orientation = normalize_resource_path(native_bind_pose.get("texture_orientation")) or TEXTURE_ORIENTATION_NORMAL
    profile_id = sanitize_path_part(character_manifest.get("profile_id") or "character")
    resource_root = f"characters/oot3d/{profile_id}/material_animation_textures"
    generated_root = generated_resource_dir / "material_animation_textures"
    emitted_keys: set[tuple[str, str, str]] = set()

    for binding in bindings:
        if not isinstance(binding, dict):
            continue
        cmab_name = normalize_resource_path(binding.get("cmab_name"))
        role = normalize_resource_path(binding.get("role")) or "material"
        texture_names = binding.get("texture_names", [])
        if not cmab_name or not isinstance(texture_names, list):
            continue
        cmab_texture_by_name = load_cmab_texture_map(character_manifest, target, cmab_name)
        for texture_name_value in texture_names:
            texture_name = normalize_resource_path(texture_name_value)
            if not texture_name:
                continue
            key = (cmab_name, role, texture_name)
            if key in emitted_keys:
                continue
            emitted_keys.add(key)
            texture = cmab_texture_by_name.get(texture_name) or texture_by_name.get(texture_name)
            if texture is None:
                raise ParseError(
                    f"{target.get('archive_path')}!{target.get('target_cmb_name')}: "
                    f"material animation texture {texture_name!r} for {cmab_name!r} not found in CMB or CMAB"
                )
            role_part = sanitize_path_part(role)
            texture_part = sanitize_path_part(texture_name)
            output_file = generated_root / role_part / f"{texture_part}.rgba16"
            write_texture_resource_image(
                output_file,
                texture.width,
                texture.height,
                texture.as_rgba16(),
                texture_orientation=texture_orientation,
            )
            records.append(
                {
                    "kind": "material_animation_texture",
                    "native_kind": "Texture",
                    "source_path": str(output_file),
                    "resource_path": f"{resource_root}/{role_part}/{texture_part}.rgba16",
                    "cmab_name": cmab_name,
                    "role": role,
                    "texture_name": texture_name,
                    "texture_index": texture.index,
                    "width": texture.width,
                    "height": texture.height,
                    "texture_format": texture.texture_format,
                    "data_type": texture.data_type,
                }
            )


def load_target_cmb_model_for_generated_resources(target: dict[str, object]) -> CmbModel:
    native_bind_pose = target.get("native_bind_pose")
    if isinstance(native_bind_pose, dict):
        source = str(native_bind_pose.get("source") or "")
        archive_path, cmb_name = split_cmb_source(source)
        if archive_path is not None:
            data, parsed_source, _ = load_cmb_payload(archive_path, 0, cmb_name)
            return CmbModel.parse(data, parsed_source)

    archive_path = Path(str(target.get("archive_path") or ""))
    cmb_name = normalize_resource_path(target.get("target_cmb_name")) or None
    if archive_path.is_file():
        data, parsed_source, _ = load_cmb_payload(archive_path, 0, cmb_name)
        return CmbModel.parse(data, parsed_source)

    raise ParseError(
        "cannot load CMB model for generated character resources; "
        "target has no usable native_bind_pose.source or file archive_path"
    )


def load_cmab_texture_map(
    character_manifest: dict[str, object],
    target: dict[str, object],
    cmab_name: str,
) -> dict[str, Texture]:
    normalized_cmab_name = normalize_resource_path(cmab_name)
    for archive_path in source_archive_candidates(character_manifest, target):
        archive = ZarArchive.from_path(archive_path)
        for file in archive.files:
            if normalize_resource_path(file.name) != normalized_cmab_name:
                continue
            data = archive.read_file(file)
            return {
                normalize_resource_path(texture.name): texture
                for texture in parse_cmab_textures(data, f"{archive_path}!{file.name}")
            }
    return {}


def source_archive_candidates(
    character_manifest: dict[str, object],
    target: dict[str, object],
) -> list[Path]:
    candidates: list[Path] = []
    seen: set[str] = set()

    native_bind_pose = target.get("native_bind_pose")
    native_source_archive: Path | None = None
    if isinstance(native_bind_pose, dict):
        native_source_archive, _ = split_cmb_source(str(native_bind_pose.get("source") or ""))
    append_unique_path(candidates, seen, native_source_archive)

    source_archives = character_manifest.get("source_archives")
    archive_names: list[object] = []
    if isinstance(source_archives, dict):
        archive_names.append(source_archives.get("model_archive"))
        auxiliary = source_archives.get("auxiliary_archives", [])
        if isinstance(auxiliary, list):
            archive_names.extend(auxiliary)

    base_dir = native_source_archive.parent if native_source_archive is not None else None
    for raw_archive_name in archive_names:
        normalized = normalize_resource_path(raw_archive_name)
        if not normalized:
            continue
        archive_path = Path(normalized)
        append_unique_path(candidates, seen, archive_path if archive_path.is_file() else None)
        if base_dir is not None:
            append_unique_path(candidates, seen, base_dir / archive_path.name)
            if archive_path.parent != Path("."):
                append_unique_path(candidates, seen, base_dir / archive_path)
    return candidates


def append_unique_path(paths: list[Path], seen: set[str], path: Path | None) -> None:
    if path is None or not path.is_file():
        return
    key = str(path.resolve()).lower()
    if key in seen:
        return
    seen.add(key)
    paths.append(path)


def split_cmb_source(source: str) -> tuple[Path | None, str | None]:
    if not source:
        return None, None
    if "!" in source:
        archive, embedded = source.split("!", 1)
        archive_path = Path(archive)
        if archive_path.is_file():
            return archive_path, normalize_resource_path(embedded) or None
    path = Path(source)
    if path.is_file():
        return path, None
    return None, None


def parse_cmab_textures(data: bytes, source: str) -> list[Texture]:
    txpt_offset = data.find(CMAB_TXPT_MAGIC)
    if txpt_offset < 0:
        return []
    texture_count = read_u32_at(data, txpt_offset + 4)
    if texture_count is None:
        raise ParseError(f"{source}: CMAB txpt section has no texture count")
    records_start = txpt_offset + 8
    records_end = records_start + texture_count * CMAB_TXPT_RECORD_SIZE
    if texture_count < 0 or records_end > len(data):
        raise ParseError(f"{source}: CMAB txpt section is out of bounds")

    string_table_offset = read_u32_at(data, CMAB_STRING_TABLE_OFFSET_CANDIDATE)
    if (
        string_table_offset is None
        or string_table_offset + 4 > len(data)
        or data[string_table_offset : string_table_offset + 4] != CMAB_STRT_MAGIC
    ):
        string_table_offset = data.find(CMAB_STRT_MAGIC, records_end)
    names = parse_cmab_string_table(data, string_table_offset)
    if len(names) < texture_count:
        raise ParseError(
            f"{source}: CMAB txpt texture count {texture_count} exceeds string table names {len(names)}"
        )

    texture_data_base = read_u32_at(data, CMAB_TEXTURE_DATA_OFFSET_CANDIDATE)
    if texture_data_base is None or texture_data_base > len(data):
        raise ParseError(f"{source}: CMAB texture data base offset is invalid")

    textures: list[Texture] = []
    for index in range(texture_count):
        offset = records_start + index * CMAB_TXPT_RECORD_SIZE
        data_size = read_u32_at(data, offset)
        width = read_u16_at(data, offset + 8)
        height = read_u16_at(data, offset + 10)
        texture_format = read_u16_at(data, offset + 12)
        data_type = read_u16_at(data, offset + 14)
        image_offset = read_u32_at(data, offset + 16)
        if None in (data_size, width, height, texture_format, data_type, image_offset):
            raise ParseError(f"{source}: CMAB txpt record {index} is truncated")
        image_start = texture_data_base + int(image_offset)
        image_end = image_start + int(data_size)
        if image_start < 0 or image_end > len(data):
            raise ParseError(f"{source}: CMAB txpt image {index} is out of bounds")
        textures.append(
            Texture(
                index=index,
                name=names[index],
                width=int(width),
                height=int(height),
                texture_format=int(texture_format),
                data_type=int(data_type),
                data=data[image_start:image_end],
            )
        )
    return textures


def parse_cmab_string_table(data: bytes, offset: int | None) -> list[str]:
    if offset is None or offset + 8 > len(data):
        return []
    if data[offset : offset + 4] != CMAB_STRT_MAGIC:
        return []
    string_count = read_u32_at(data, offset + 4)
    if string_count is None:
        return []
    offsets_start = offset + 8
    offsets_end = offsets_start + string_count * 4
    if offsets_end > len(data):
        return []
    string_data_start = offsets_end
    names: list[str] = []
    for index in range(string_count):
        relative_offset = read_u32_at(data, offsets_start + index * 4)
        if relative_offset is None:
            return []
        string_offset = string_data_start + relative_offset
        if string_offset >= len(data):
            return []
        end = data.find(b"\x00", string_offset)
        if end < 0:
            return []
        try:
            names.append(data[string_offset:end].decode("ascii"))
        except UnicodeDecodeError:
            return []
    return names


def read_u32_at(data: bytes, offset: int) -> int | None:
    if offset < 0 or offset + 4 > len(data):
        return None
    return int.from_bytes(data[offset : offset + 4], "little")


def read_u16_at(data: bytes, offset: int) -> int | None:
    if offset < 0 or offset + 2 > len(data):
        return None
    return int.from_bytes(data[offset : offset + 2], "little")


def append_native_bind_pose_resource_records(records: list[dict[str, object]], target: dict[str, object]) -> None:
    native_bind_pose = target.get("native_bind_pose")
    if not isinstance(native_bind_pose, dict) or native_bind_pose.get("status") == "not_provided":
        return
    if native_bind_pose.get("status") != "ready":
        raise ParseError(f"native bind-pose manifest is not ready: {native_bind_pose.get('status')}")

    manifest_path = Path(str(native_bind_pose.get("manifest") or ""))
    if not manifest_path.is_file():
        raise ParseError(f"{manifest_path}: native bind-pose manifest not found")
    native_manifest = read_json(manifest_path)
    if native_manifest.get("format") != SKINNED_BIND_POSE_NATIVE_MANIFEST_FORMAT:
        raise ParseError(f"{manifest_path}: unexpected native bind-pose manifest format")

    resource_root = normalize_resource_path(native_manifest.get("resource_root"))
    if not resource_root:
        raise ParseError(f"{manifest_path}: native bind-pose manifest has no resource_root")
    records.append(
        {
            "kind": "native_bind_pose_manifest",
            "native_kind": "Json",
            "source_path": str(manifest_path),
            "resource_path": f"{resource_root}/native_bind_pose_manifest.json",
        }
    )

    resources = native_manifest.get("resources", [])
    if not isinstance(resources, list):
        raise ParseError(f"{manifest_path}: native bind-pose manifest resources must be an array")
    for resource in resources:
        if not isinstance(resource, dict):
            continue
        native_kind = str(resource.get("kind") or "")
        source_path = resource.get("file")
        resource_path = normalize_resource_path(resource.get("path"))
        if not native_kind or not source_path or not resource_path:
            continue
        records.append(
            {
                "kind": f"native_bind_pose_{native_kind.lower()}",
                "native_kind": native_kind,
                "source_path": str(source_path),
                "resource_path": resource_path,
            }
        )


def target_from_character_manifest(
    character_manifest: dict[str, object],
    skinned_binding: dict[str, object],
) -> dict[str, object]:
    source_archives = character_manifest.get("source_archives", {})
    if not isinstance(source_archives, dict):
        raise ParseError("character manifest is missing source_archives")
    archive = normalize_resource_path(source_archives.get("model_archive"))
    cmb = normalize_resource_path(source_archives.get("model_cmb"))
    for target in skinned_binding.get("targets", []):
        if not isinstance(target, dict):
            continue
        if normalize_resource_path(target.get("archive_path")) != archive:
            continue
        if normalize_resource_path(target.get("target_cmb_name")) != cmb:
            continue
        merged = dict(target)
        character_target = character_manifest.get("target", {})
        if isinstance(character_target, dict):
            merged["native_bind_pose"] = character_target.get("native_bind_pose")
        return merged
    raise ParseError(f"skinned binding target not found for {archive}!{cmb}")


def default_runtime_profile_path(character_manifest: dict[str, object]) -> str:
    profile_id = str(character_manifest.get("profile_id") or "character")
    return f"characters/oot3d/{sanitize_path_part(profile_id)}/character_runtime_profile.json"


def generated_character_resource_dir(
    character_manifest_path: Path,
    character_manifest: dict[str, object],
) -> Path:
    profile_id = sanitize_path_part(str(character_manifest.get("profile_id") or "character"))
    return character_manifest_path.parent / "generated_resources" / profile_id


def source_manifest_path(character_manifest: dict[str, object], key: str) -> Path:
    path = optional_source_manifest_path(character_manifest, key)
    if path is None or not path.is_file():
        raise ParseError(f"character manifest source_manifests.{key} not found: {path}")
    return path


def optional_source_manifest_path(character_manifest: dict[str, object], key: str) -> Path | None:
    source_manifests = character_manifest.get("source_manifests", {})
    if not isinstance(source_manifests, dict):
        return None
    raw_path = source_manifests.get(key)
    if not raw_path:
        return None
    return Path(str(raw_path))


def runtime_profile_resource_match_status(runtime_profile: object, expected_resource_paths: set[str]) -> str:
    if not isinstance(runtime_profile, dict):
        return "missing_runtime_profile"
    resources = runtime_profile.get("native_resources", {})
    if not isinstance(resources, dict):
        return "missing_native_resources"
    declared_paths: set[str] = set()
    bind_pose = resources.get("bind_pose", {})
    if isinstance(bind_pose, dict):
        resource_path = normalize_resource_path(bind_pose.get("resource_path"))
        if resource_path:
            declared_paths.add(resource_path)
    native_bind_pose = resources.get("bind_pose_native", {})
    if isinstance(native_bind_pose, dict):
        manifest_resource_path = normalize_resource_path(native_bind_pose.get("manifest_resource_path"))
        if manifest_resource_path:
            declared_paths.add(manifest_resource_path)
        native_resources = native_bind_pose.get("resources", [])
        if isinstance(native_resources, list):
            for resource in native_resources:
                if not isinstance(resource, dict):
                    continue
                resource_path = normalize_resource_path(resource.get("resource_path"))
                if resource_path:
                    declared_paths.add(resource_path)
    animations = resources.get("animations", [])
    if isinstance(animations, list):
        for animation in animations:
            if not isinstance(animation, dict):
                continue
            resource_path = normalize_resource_path(animation.get("resource_path"))
            if resource_path:
                declared_paths.add(resource_path)
    material_animation_textures = resources.get("material_animation_textures", [])
    if isinstance(material_animation_textures, list):
        for texture in material_animation_textures:
            if not isinstance(texture, dict):
                continue
            resource_path = normalize_resource_path(texture.get("resource_path"))
            if resource_path:
                declared_paths.add(resource_path)
    anb_payload_batch = resources.get("anb_payload_batch")
    if isinstance(anb_payload_batch, dict):
        resource_path = normalize_resource_path(anb_payload_batch.get("resource_path"))
        if resource_path:
            declared_paths.add(resource_path)
    return "matched" if declared_paths == expected_resource_paths else "mismatch"


def runtime_animation_contract_summary(runtime_profile: object, expected_animation_count: int) -> dict[str, object]:
    if not isinstance(runtime_profile, dict):
        return {
            "status": "missing_runtime_profile",
            "format": None,
            "animation_count": 0,
            "expected_animation_count": expected_animation_count,
        }

    contract = runtime_profile.get("animation_playback_contract")
    native_resources = runtime_profile.get("native_resources", {})
    animations = native_resources.get("animations", []) if isinstance(native_resources, dict) else []
    lookup = runtime_profile.get("animation_lookup", {})
    by_csab_name = lookup.get("by_csab_name", {}) if isinstance(lookup, dict) else {}
    by_stem = lookup.get("by_stem", {}) if isinstance(lookup, dict) else {}
    time_domain = contract.get("time_domain", {}) if isinstance(contract, dict) else {}
    sampling_policy = contract.get("sampling_policy", {}) if isinstance(contract, dict) else {}
    skinning_policy = contract.get("skinning_policy", {}) if isinstance(contract, dict) else {}

    animation_count = len(animations) if isinstance(animations, list) else 0
    csab_lookup_count = len(by_csab_name) if isinstance(by_csab_name, dict) else 0
    stem_lookup_count = len(by_stem) if isinstance(by_stem, dict) else 0
    status = "valid"
    if not isinstance(contract, dict):
        status = "missing_contract"
    elif contract.get("format") != "oot3d_character_animation_playback_contract_v1":
        status = "invalid_contract_format"
    elif contract.get("track_resource_format") != "oot3d_csab_skeleton_track_export_v1":
        status = "invalid_track_resource_format"
    elif int(contract.get("track_count", -1) or -1) != expected_animation_count:
        status = "contract_track_count_mismatch"
    elif animation_count != expected_animation_count:
        status = "animation_resource_count_mismatch"
    elif csab_lookup_count != expected_animation_count:
        status = "csab_lookup_count_mismatch"
    elif stem_lookup_count <= 0:
        status = "missing_stem_lookup"
    elif contract.get("default_fps") != 60.0:
        status = "invalid_default_fps"
    elif not isinstance(time_domain, dict) or time_domain.get("frame_zero") != 0:
        status = "invalid_frame_zero"
    elif time_domain.get("frame_count_candidate_is_inclusive_max_frame") is not True:
        status = "invalid_frame_count_domain"
    elif time_domain.get("frame_slot_count") != "frame_count_candidate + 1":
        status = "invalid_frame_slot_policy"
    elif not isinstance(sampling_policy, dict) or sampling_policy.get("base_transform_source") != "CMB skeleton bone base_transform":
        status = "invalid_base_transform_source"
    elif sampling_policy.get("missing_channel_policy") != "inherit CMB base transform component":
        status = "invalid_missing_channel_policy"
    elif sampling_policy.get("rotation_unwrap") != (
        "sample keyed rotation channels on the nearest continuous angular branch before Hermite interpolation"
    ):
        status = "invalid_rotation_unwrap_policy"
    elif sampling_policy.get("world_transform_order") != "compose local TRS down the CMB skeleton parent hierarchy":
        status = "invalid_world_transform_order"
    elif not isinstance(skinning_policy, dict) or skinning_policy.get("bind_pose_source") != "decoded CMB source_position/source_normal/influences":
        status = "invalid_bind_pose_source"
    elif skinning_policy.get("bind_world_source") != "CMB skeleton bind pose world transforms":
        status = "invalid_bind_world_source"
    elif skinning_policy.get("skin_matrix") != "pose_world * inverse(bind_world)":
        status = "invalid_skin_matrix_policy"

    return {
        "status": status,
        "format": contract.get("format") if isinstance(contract, dict) else None,
        "track_resource_format": contract.get("track_resource_format") if isinstance(contract, dict) else None,
        "contract_track_count": contract.get("track_count") if isinstance(contract, dict) else None,
        "animation_count": animation_count,
        "expected_animation_count": expected_animation_count,
        "csab_lookup_count": csab_lookup_count,
        "stem_lookup_count": stem_lookup_count,
        "frame_slot_min": contract.get("frame_slot_min") if isinstance(contract, dict) else None,
        "frame_slot_max": contract.get("frame_slot_max") if isinstance(contract, dict) else None,
        "default_fps": contract.get("default_fps") if isinstance(contract, dict) else None,
        "frame_zero": time_domain.get("frame_zero") if isinstance(time_domain, dict) else None,
        "frame_count_candidate_is_inclusive_max_frame": (
            time_domain.get("frame_count_candidate_is_inclusive_max_frame")
            if isinstance(time_domain, dict)
            else None
        ),
        "frame_slot_count_policy": time_domain.get("frame_slot_count") if isinstance(time_domain, dict) else None,
        "base_transform_source": sampling_policy.get("base_transform_source") if isinstance(sampling_policy, dict) else None,
        "missing_channel_policy": sampling_policy.get("missing_channel_policy") if isinstance(sampling_policy, dict) else None,
        "rotation_unwrap_policy": sampling_policy.get("rotation_unwrap") if isinstance(sampling_policy, dict) else None,
        "world_transform_order": sampling_policy.get("world_transform_order") if isinstance(sampling_policy, dict) else None,
        "bind_pose_source": skinning_policy.get("bind_pose_source") if isinstance(skinning_policy, dict) else None,
        "bind_world_source": skinning_policy.get("bind_world_source") if isinstance(skinning_policy, dict) else None,
        "skin_matrix": skinning_policy.get("skin_matrix") if isinstance(skinning_policy, dict) else None,
    }


def runtime_animation_time_source_contract_summary(
    runtime_profile: object,
    runtime_semantics: dict[str, object] | None,
    profile_id: str,
) -> dict[str, object]:
    if not isinstance(runtime_profile, dict):
        return {"status": "missing_runtime_profile", "binding_count": 0}
    actual = runtime_profile.get("animation_time_source_contract")
    if not isinstance(actual, dict):
        return {"status": "missing_contract", "binding_count": 0}
    native_resources = runtime_profile.get("native_resources", {})
    animations = native_resources.get("animations", []) if isinstance(native_resources, dict) else []
    if not isinstance(animations, list):
        animations = []
    expected = animation_time_source_contract(runtime_semantics, profile_id, animations)
    bindings = actual.get("bindings", [])
    binding_count = len(bindings) if isinstance(bindings, list) else 0
    status = "valid"
    if actual.get("format") != ANIMATION_TIME_SOURCE_CONTRACT_FORMAT:
        status = "invalid_contract_format"
    elif runtime_semantics is None:
        status = "not_required" if actual == expected else "unexpected_default_contract"
    elif actual != expected:
        status = "contract_mismatch"
    return {
        "status": status,
        "format": actual.get("format"),
        "contract_status": actual.get("status"),
        "default_source": actual.get("default_source"),
        "binding_count": binding_count,
        "bindings": bindings if isinstance(bindings, list) else [],
        "matches_runtime_semantics": actual == expected,
    }


def runtime_root_motion_ownership_contract_summary(
    runtime_profile: object,
    runtime_semantics: dict[str, object] | None,
    profile_id: str,
) -> dict[str, object]:
    if not isinstance(runtime_profile, dict):
        return {"status": "missing_runtime_profile"}
    actual = runtime_profile.get("root_motion_ownership_contract")
    if not isinstance(actual, dict):
        return {"status": "missing_contract"}
    expected = root_motion_ownership_contract(runtime_semantics, profile_id)
    status = "valid"
    if actual.get("format") != ROOT_MOTION_OWNERSHIP_CONTRACT_FORMAT:
        status = "invalid_contract_format"
    elif runtime_semantics is None:
        status = "not_required" if actual == expected else "unexpected_default_contract"
    elif actual != expected:
        status = "contract_mismatch"
    return {
        "status": status,
        "format": actual.get("format"),
        "contract_status": actual.get("status"),
        "movement_enabled_flag": actual.get("movement_enabled_flag"),
        "update_y_flag": actual.get("update_y_flag"),
        "translation_policy": actual.get("translation_policy"),
        "xz_ownership": actual.get("xz_ownership"),
        "y_ownership": actual.get("y_ownership"),
        "root_rotation_policy": actual.get("root_rotation_policy"),
        "matches_runtime_semantics": actual == expected,
    }


def runtime_skinning_contract_summary(runtime_profile: object) -> dict[str, object]:
    if not isinstance(runtime_profile, dict):
        return {
            "status": "missing_runtime_profile",
            "format": None,
            "sample_count": 0,
            "ready_sample_count": 0,
        }

    contract = runtime_profile.get("skinning_diagnostic_contract")
    if not isinstance(contract, dict):
        return {
            "status": "missing_contract",
            "format": None,
            "sample_count": 0,
            "ready_sample_count": 0,
        }

    samples = contract.get("samples", [])
    samples = samples if isinstance(samples, list) else []
    ready_samples = [
        sample
        for sample in samples
        if isinstance(sample, dict)
        and sample.get("status") == "ready"
        and sample.get("validation_status") == "valid"
        and int(sample.get("validation_error_count", 1) or 0) == 0
    ]
    selected_sample = ready_samples[0] if ready_samples else samples[0] if samples and isinstance(samples[0], dict) else {}
    counts = selected_sample.get("counts", {}) if isinstance(selected_sample, dict) else {}
    pose_counts = selected_sample.get("pose_counts", {}) if isinstance(selected_sample, dict) else {}
    bind_counts = selected_sample.get("bind_pose_counts", {}) if isinstance(selected_sample, dict) else {}
    frames = selected_sample.get("frames", []) if isinstance(selected_sample, dict) else []
    status = "valid"
    if contract.get("format") != "oot3d_character_skinning_diagnostic_contract_v1":
        status = "invalid_contract_format"
    elif contract.get("status") != "ready":
        status = "contract_not_ready"
    elif not ready_samples:
        status = "missing_ready_sample"
    elif not isinstance(counts, dict) or int(counts.get("validation_error_count", 1) or 0) != 0:
        status = "sample_validation_errors"
    elif not isinstance(counts, dict) or int(counts.get("sampled_vertex_rows", 0) or 0) <= 0:
        status = "missing_sampled_vertices"
    elif int(counts.get("finite_pose_rows", -1) or -1) != int(counts.get("sampled_vertex_rows", 0) or 0):
        status = "nonfinite_sampled_pose_rows"
    elif not isinstance(pose_counts, dict) or int(pose_counts.get("finite_world_matrix_entries", 0) or 0) <= 0:
        status = "missing_finite_world_matrices"
    elif not isinstance(bind_counts, dict) or int(bind_counts.get("skinned_vertex_rows", 0) or 0) <= 0:
        status = "missing_bind_pose_vertices"
    elif not isinstance(frames, list) or len(frames) <= 0:
        status = "missing_frame_diagnostics"

    return {
        "status": status,
        "format": contract.get("format"),
        "contract_status": contract.get("status"),
        "sample_count": len(samples),
        "ready_sample_count": len(ready_samples),
        "csab_name": selected_sample.get("csab_name") if isinstance(selected_sample, dict) else None,
        "sample_frames": selected_sample.get("sample_frames", []) if isinstance(selected_sample, dict) else [],
        "frame_count_candidate": selected_sample.get("frame_count_candidate") if isinstance(selected_sample, dict) else None,
        "source_vertex_rows": counts.get("source_vertex_rows") if isinstance(counts, dict) else None,
        "sampled_vertex_rows": counts.get("sampled_vertex_rows") if isinstance(counts, dict) else None,
        "finite_pose_rows": counts.get("finite_pose_rows") if isinstance(counts, dict) else None,
        "changed_position_rows": counts.get("changed_position_rows") if isinstance(counts, dict) else None,
        "validation_error_count": counts.get("validation_error_count") if isinstance(counts, dict) else None,
        "sampled_pose_frames": pose_counts.get("sampled_pose_frames") if isinstance(pose_counts, dict) else None,
        "finite_world_matrix_entries": pose_counts.get("finite_world_matrix_entries") if isinstance(pose_counts, dict) else None,
        "skinned_vertex_rows": bind_counts.get("skinned_vertex_rows") if isinstance(bind_counts, dict) else None,
        "frame_diagnostic_count": len(frames) if isinstance(frames, list) else 0,
    }


def runtime_selected_draw_skinning_contract_summary(runtime_profile: object) -> dict[str, object]:
    if not isinstance(runtime_profile, dict):
        return {
            "status": "missing_runtime_profile",
            "format": None,
        }

    contract = runtime_profile.get("selected_draw_skinning_contract")
    if not isinstance(contract, dict):
        return {
            "status": "missing_contract",
            "format": None,
        }

    counts = contract.get("counts", {})
    counts = counts if isinstance(counts, dict) else {}
    dynamic_counts = contract.get("dynamic_counts", {})
    dynamic_counts = dynamic_counts if isinstance(dynamic_counts, dict) else {}
    frames = contract.get("frames", [])
    frames = frames if isinstance(frames, list) else []
    dynamic_frames = contract.get("dynamic_frames", [])
    dynamic_frames = dynamic_frames if isinstance(dynamic_frames, list) else []
    sampled_vertex_rows = int(counts.get("sampled_vertex_rows", 0) or 0)
    finite_pose_rows = int(counts.get("finite_pose_rows", -1) or -1)
    validation_error_count = json_int(counts.get("validation_error_count"), 1)
    selected_skinned_primitive_count = int(contract.get("selected_skinned_primitive_count", 0) or 0)
    triangle_count = int(contract.get("triangle_count", 0) or 0)
    selected_unique_vertex_rows = int(contract.get("selected_unique_vertex_rows", 0) or 0)
    selected_primitive_key_count = int(contract.get("selected_primitive_key_count", 0) or 0)
    dynamic_primitive_count = int(contract.get("dynamic_primitive_count", 0) or 0)
    dynamic_skinned_primitive_count = int(contract.get("dynamic_skinned_primitive_count", 0) or 0)
    dynamic_rigid_primitive_count = int(contract.get("dynamic_rigid_primitive_count", 0) or 0)
    dynamic_triangle_count = int(contract.get("dynamic_triangle_count", 0) or 0)
    dynamic_unique_vertex_rows = int(contract.get("dynamic_unique_vertex_rows", 0) or 0)
    dynamic_sampled_vertex_rows = int(dynamic_counts.get("sampled_vertex_rows", 0) or 0)
    dynamic_finite_pose_rows = int(dynamic_counts.get("finite_pose_rows", -1) or -1)
    dynamic_validation_error_count = json_int(dynamic_counts.get("validation_error_count"), 1)
    status = "valid"
    if contract.get("format") != "oot3d_character_selected_draw_skinning_contract_v1":
        status = "invalid_contract_format"
    elif contract.get("status") != "ready":
        status = "contract_not_ready"
    elif dynamic_primitive_count != selected_primitive_key_count:
        status = "dynamic_primitive_key_count_mismatch"
    elif dynamic_skinned_primitive_count <= 0:
        status = "missing_dynamic_skinned_primitives"
    elif dynamic_rigid_primitive_count <= 0:
        status = "missing_dynamic_rigid_primitives"
    elif dynamic_triangle_count <= 0:
        status = "missing_dynamic_triangles"
    elif dynamic_unique_vertex_rows <= 0:
        status = "missing_dynamic_vertices"
    elif dynamic_sampled_vertex_rows != dynamic_triangle_count * 3 * int(dynamic_counts.get("sampled_frames", 0) or 0):
        status = "dynamic_sampled_vertex_row_count_mismatch"
    elif dynamic_finite_pose_rows != dynamic_sampled_vertex_rows:
        status = "nonfinite_dynamic_draw_pose_rows"
    elif dynamic_validation_error_count != 0:
        status = "dynamic_draw_validation_errors"
    elif not dynamic_frames:
        status = "missing_dynamic_frame_diagnostics"
    elif selected_skinned_primitive_count <= 0:
        status = "missing_selected_skinned_primitives"
    elif triangle_count <= 0:
        status = "missing_selected_triangles"
    elif selected_unique_vertex_rows <= 0:
        status = "missing_selected_vertices"
    elif sampled_vertex_rows != triangle_count * 3 * int(counts.get("sampled_frames", 0) or 0):
        status = "sampled_vertex_row_count_mismatch"
    elif finite_pose_rows != sampled_vertex_rows:
        status = "nonfinite_selected_draw_pose_rows"
    elif validation_error_count != 0:
        status = "selected_draw_validation_errors"
    elif not frames:
        status = "missing_frame_diagnostics"
    else:
        for frame in frames:
            if not isinstance(frame, dict):
                status = "invalid_frame_diagnostic"
                break
            frame_counts = frame.get("counts", {})
            if not isinstance(frame_counts, dict):
                status = "invalid_frame_counts"
                break
            frame_sampled = int(frame_counts.get("sampled_vertex_rows", 0) or 0)
            frame_finite = int(frame_counts.get("finite_pose_rows", -1) or -1)
            if frame_sampled != triangle_count * 3:
                status = "frame_sampled_vertex_row_count_mismatch"
                break
            if frame_finite != frame_sampled:
                status = "frame_nonfinite_selected_draw_pose_rows"
                break
        if status == "valid":
            for frame in dynamic_frames:
                if not isinstance(frame, dict):
                    status = "invalid_dynamic_frame_diagnostic"
                    break
                frame_counts = frame.get("counts", {})
                if not isinstance(frame_counts, dict):
                    status = "invalid_dynamic_frame_counts"
                    break
                frame_sampled = int(frame_counts.get("sampled_vertex_rows", 0) or 0)
                frame_finite = int(frame_counts.get("finite_pose_rows", -1) or -1)
                if frame_sampled != dynamic_triangle_count * 3:
                    status = "dynamic_frame_sampled_vertex_row_count_mismatch"
                    break
                if frame_finite != frame_sampled:
                    status = "frame_nonfinite_dynamic_draw_pose_rows"
                    break

    return {
        "status": status,
        "format": contract.get("format"),
        "contract_status": contract.get("status"),
        "csab_name": contract.get("csab_name"),
        "sample_frames": contract.get("sample_frames", []),
        "frame_count_candidate": contract.get("frame_count_candidate"),
        "frame_slot_count": contract.get("frame_slot_count"),
        "selected_primitive_key_count": selected_primitive_key_count,
        "selected_skinned_primitive_count": selected_skinned_primitive_count,
        "ignored_rigid_primitive_count": contract.get("ignored_rigid_primitive_count"),
        "dynamic_primitive_count": dynamic_primitive_count,
        "dynamic_skinned_primitive_count": dynamic_skinned_primitive_count,
        "dynamic_rigid_primitive_count": dynamic_rigid_primitive_count,
        "triangle_count": triangle_count,
        "dynamic_triangle_count": dynamic_triangle_count,
        "selected_unique_vertex_rows": selected_unique_vertex_rows,
        "dynamic_unique_vertex_rows": dynamic_unique_vertex_rows,
        "sampled_frames": counts.get("sampled_frames"),
        "sampled_vertex_rows": sampled_vertex_rows,
        "finite_pose_rows": finite_pose_rows,
        "changed_position_rows": counts.get("changed_position_rows"),
        "validation_error_count": validation_error_count,
        "max_position_delta_from_bind": counts.get("max_position_delta_from_bind"),
        "dynamic_sampled_frames": dynamic_counts.get("sampled_frames"),
        "dynamic_sampled_vertex_rows": dynamic_sampled_vertex_rows,
        "dynamic_finite_pose_rows": dynamic_finite_pose_rows,
        "dynamic_changed_position_rows": dynamic_counts.get("changed_position_rows"),
        "dynamic_validation_error_count": dynamic_validation_error_count,
        "dynamic_max_position_delta_from_bind": dynamic_counts.get("max_position_delta_from_bind"),
        "position_bounds": contract.get("position_bounds", {}),
        "dynamic_position_bounds": contract.get("dynamic_position_bounds", {}),
        "frame_diagnostic_count": len(frames),
        "dynamic_frame_diagnostic_count": len(dynamic_frames),
    }


def runtime_transform_contract_summary(runtime_profile: object) -> dict[str, object]:
    if not isinstance(runtime_profile, dict):
        return {
            "status": "missing_runtime_profile",
            "format": None,
            "model_scale": None,
            "model_scale_source": None,
        }

    contract = runtime_profile.get("runtime_transform")
    if not isinstance(contract, dict):
        return {
            "status": "missing_contract",
            "format": None,
            "model_scale": None,
            "model_scale_source": None,
        }

    model_scale = contract.get("model_scale")
    model_scale_source = normalize_resource_path(contract.get("model_scale_source"))
    status = "valid"
    if contract.get("format") != "oot3d_character_runtime_transform_v1":
        status = "invalid_contract_format"
    elif not isinstance(model_scale, (int, float)) or not math.isfinite(float(model_scale)) or float(model_scale) <= 0.0:
        status = "invalid_model_scale"
    elif abs(float(model_scale) - 1.0) > 0.000001:
        status = "non_source_unit_model_scale"
    elif not model_scale_source.startswith("cmb_source_units_"):
        status = "invalid_model_scale_source"

    return {
        "status": status,
        "format": contract.get("format"),
        "model_scale": model_scale,
        "model_scale_source": model_scale_source,
        "visual_origin_offset": contract.get("visual_origin_offset"),
        "visual_origin_offset_source": contract.get("visual_origin_offset_source"),
        "origin_policy": contract.get("origin_policy"),
        "scale_policy": contract.get("scale_policy"),
    }


def runtime_native_bind_pose_contract_summary(
    runtime_profile: object,
    expected_selection_primitive_key_count: int,
) -> dict[str, object]:
    if not isinstance(runtime_profile, dict):
        return {
            "status": "missing_runtime_profile",
            "expected_selection_primitive_key_count": expected_selection_primitive_key_count,
            "selection_primitive_key_count": 0,
        }

    native_resources = runtime_profile.get("native_resources", {})
    native_bind_pose = (
        native_resources.get("bind_pose_native")
        if isinstance(native_resources, dict)
        else None
    )
    if not isinstance(native_bind_pose, dict):
        return {
            "status": "missing_contract",
            "expected_selection_primitive_key_count": expected_selection_primitive_key_count,
            "selection_primitive_key_count": 0,
        }

    selection_primitive_key_count = int(native_bind_pose.get("selection_primitive_key_count", 0) or 0)
    display_profile_count = int(native_bind_pose.get("display_profile_count", 0) or 0)
    mesh_record_count = int(native_bind_pose.get("mesh_record_count", 0) or 0)
    resource_count = int(native_bind_pose.get("resource_count", 0) or 0)
    selected_material_indices = native_bind_pose.get("selected_material_indices", [])
    selected_material_display_lists = native_bind_pose.get("selected_material_display_lists", [])
    selected_material_indices = selected_material_indices if isinstance(selected_material_indices, list) else []
    selected_material_display_lists = (
        selected_material_display_lists if isinstance(selected_material_display_lists, list) else []
    )
    selected_material_display_list_paths = [
        normalize_resource_path(record.get("path"))
        for record in selected_material_display_lists
        if isinstance(record, dict)
    ]
    status = "valid"
    if not normalize_resource_path(native_bind_pose.get("manifest_resource_path")):
        status = "missing_manifest_resource"
    elif not normalize_resource_path(native_bind_pose.get("top_display_list")):
        status = "missing_top_display_list"
    elif expected_selection_primitive_key_count <= 0:
        status = "missing_expected_selection_primitive_keys"
    elif selection_primitive_key_count != expected_selection_primitive_key_count:
        status = "selection_primitive_key_count_mismatch"
    elif display_profile_count <= 0:
        status = "missing_display_profiles"
    elif mesh_record_count <= 0:
        status = "missing_mesh_records"
    elif len(selected_material_indices) <= 0:
        status = "missing_selected_material_indices"
    elif len(selected_material_display_lists) != len(selected_material_indices):
        status = "selected_material_display_list_count_mismatch"
    elif len(selected_material_display_list_paths) != len(selected_material_display_lists):
        status = "selected_material_display_list_path_missing"
    elif resource_count <= 0:
        status = "missing_native_resources"

    return {
        "status": status,
        "manifest_resource_path": normalize_resource_path(native_bind_pose.get("manifest_resource_path")),
        "selection_source": native_bind_pose.get("selection_source"),
        "selection_profile_id": native_bind_pose.get("selection_profile_id"),
        "expected_selection_primitive_key_count": expected_selection_primitive_key_count,
        "selection_primitive_key_count": selection_primitive_key_count,
        "top_display_list": normalize_resource_path(native_bind_pose.get("top_display_list")),
        "diagnostic_display_profile": native_bind_pose.get("diagnostic_display_profile"),
        "display_profile_count": display_profile_count,
        "visibility_group_count": int(native_bind_pose.get("visibility_group_count", 0) or 0),
        "mesh_record_count": mesh_record_count,
        "selected_material_index_count": len(selected_material_indices),
        "selected_material_display_list_count": len(selected_material_display_lists),
        "selected_material_display_list_paths": selected_material_display_list_paths,
        "resource_count": resource_count,
    }


def runtime_material_animation_contract_summary(runtime_profile: object) -> dict[str, object]:
    if not isinstance(runtime_profile, dict):
        return {
            "status": "missing_runtime_profile",
            "format": None,
            "cmab_count": 0,
            "binding_count": 0,
        }
    contract = runtime_profile.get("material_animation_contract")
    if not isinstance(contract, dict):
        return {
            "status": "missing_contract",
            "format": None,
            "cmab_count": 0,
            "binding_count": 0,
        }
    bindings = contract.get("bindings", [])
    bindings = bindings if isinstance(bindings, list) else []
    role_counts = Counter(
        normalize_resource_path(binding.get("role"))
        for binding in bindings
        if isinstance(binding, dict) and normalize_resource_path(binding.get("role"))
    )
    texture_counts_by_role = {
        normalize_resource_path(binding.get("role")): len(binding.get("texture_names", []))
        for binding in bindings
        if isinstance(binding, dict) and isinstance(binding.get("texture_names"), list)
    }
    status = "valid"
    if contract.get("format") != "oot3d_character_material_animation_contract_v1":
        status = "invalid_contract_format"
    elif contract.get("status") != "ready":
        status = "contract_not_ready"
    elif int(contract.get("cmab_count", 0) or 0) <= 0:
        status = "missing_cmab_payloads"
    elif int(contract.get("unresolved_or_ambiguous_count", 1) or 0) != 0:
        status = "unresolved_cmab_targets"
    elif role_counts.get("eye", 0) != 1:
        status = "missing_eye_binding"
    elif role_counts.get("mouth", 0) != 1:
        status = "missing_mouth_binding"
    elif texture_counts_by_role.get("eye", 0) <= 0:
        status = "missing_eye_textures"
    elif texture_counts_by_role.get("mouth", 0) <= 0:
        status = "missing_mouth_textures"
    return {
        "status": status,
        "format": contract.get("format"),
        "contract_status": contract.get("status"),
        "cmab_count": contract.get("cmab_count"),
        "binding_count": len(bindings),
        "unresolved_or_ambiguous_count": contract.get("unresolved_or_ambiguous_count"),
        "role_counts": sorted_counter(role_counts),
        "texture_counts_by_role": texture_counts_by_role,
    }


def runtime_face_state_contract_summary(runtime_profile: object) -> dict[str, object]:
    if not isinstance(runtime_profile, dict):
        return {
            "status": "missing_runtime_profile",
            "format": None,
            "faceb_track_count": 0,
        }
    contract = runtime_profile.get("face_state_contract")
    if not isinstance(contract, dict):
        return {
            "status": "missing_contract",
            "format": None,
            "faceb_track_count": 0,
        }
    status = "valid"
    if contract.get("format") != "oot3d_character_face_state_contract_v1":
        status = "invalid_contract_format"
    elif contract.get("status") != "ready":
        status = "contract_not_ready"
    elif int(contract.get("faceb_track_count", 0) or 0) <= 0:
        status = "missing_faceb_tracks"
    elif int(contract.get("faceb_without_csab_count", 1) or 0) != 0:
        status = "faceb_without_csab"
    elif int(contract.get("eye_texture_count", 0) or 0) <= 0:
        status = "missing_eye_texture_domain"
    elif int(contract.get("mouth_texture_count", 0) or 0) <= 0:
        status = "missing_mouth_texture_domain"
    elif int(contract.get("invalid_eye_event_count", 1) or 0) != 0:
        status = "invalid_eye_events"
    elif int(contract.get("invalid_mouth_event_count", 1) or 0) != 0:
        status = "invalid_mouth_events"
    return {
        "status": status,
        "format": contract.get("format"),
        "contract_status": contract.get("status"),
        "faceb_track_count": contract.get("faceb_track_count"),
        "faceb_entry_total": contract.get("faceb_entry_total"),
        "faceb_csab_overlap_count": contract.get("faceb_csab_overlap_count"),
        "faceb_without_csab_count": contract.get("faceb_without_csab_count"),
        "eye_texture_count": contract.get("eye_texture_count"),
        "mouth_texture_count": contract.get("mouth_texture_count"),
        "hold_value": contract.get("hold_value"),
        "invalid_eye_event_count": contract.get("invalid_eye_event_count"),
        "invalid_mouth_event_count": contract.get("invalid_mouth_event_count"),
    }


def runtime_segment_continuity_contract_summary(
    runtime_profile: object,
    expected_audit: dict[str, object] | None,
) -> dict[str, object]:
    if not isinstance(runtime_profile, dict):
        return {
            "status": "missing_runtime_profile",
            "format": None,
            "segment_count": 0,
            "transition_count": 0,
        }

    contract = runtime_profile.get("segment_continuity_contract")
    if not isinstance(contract, dict):
        return {
            "status": "missing_contract" if expected_audit is not None else "not_required",
            "format": None,
            "segment_count": 0,
            "transition_count": 0,
        }

    segments = contract.get("segments", [])
    segments = segments if isinstance(segments, list) else []
    expected_segment_count = (
        int(expected_audit.get("segment_count", 0) or 0)
        if isinstance(expected_audit, dict)
        else 0
    )
    expected_transition_count = (
        int(expected_audit.get("transition_count", 0) or 0)
        if isinstance(expected_audit, dict)
        else 0
    )
    normalized_discontinuity_count = json_int(contract.get("normalized_discontinuity_count"), -1)
    max_normalized_delta = contract.get("max_normalized_transition_delta")
    status = "valid"
    if expected_audit is None and contract.get("status") == "not_provided":
        status = "not_required"
    elif contract.get("format") != "oot3d_character_segment_continuity_contract_v1":
        status = "invalid_contract_format"
    elif contract.get("status") != "ready":
        status = "contract_not_ready"
    elif expected_segment_count > 0 and int(contract.get("segment_count", 0) or 0) != expected_segment_count:
        status = "segment_count_mismatch"
    elif expected_transition_count > 0 and int(contract.get("transition_count", 0) or 0) != expected_transition_count:
        status = "transition_count_mismatch"
    elif expected_segment_count > 0 and len(segments) != expected_segment_count:
        status = "segment_record_count_mismatch"
    elif normalized_discontinuity_count != 0:
        status = "normalized_discontinuities_present"
    elif not isinstance(max_normalized_delta, (int, float)) or not math.isfinite(float(max_normalized_delta)):
        status = "invalid_max_normalized_delta"
    elif float(max_normalized_delta) > 0.001:
        status = "max_normalized_delta_exceeds_tolerance"
    else:
        for segment in segments:
            if not isinstance(segment, dict):
                status = "invalid_segment_record"
                break
            if not normalize_resource_path(segment.get("csab_name")):
                status = "missing_segment_csab_name"
                break
            if finite_vec3_record(segment.get("normalization_offset")) is None:
                status = "invalid_segment_offset"
                break

    return {
        "status": status,
        "format": contract.get("format"),
        "contract_status": contract.get("status"),
        "source_audit_format": contract.get("source_audit_format"),
        "root_motion_bone": contract.get("root_motion_bone"),
        "segment_count": contract.get("segment_count"),
        "expected_segment_count": expected_segment_count,
        "segment_record_count": len(segments),
        "transition_count": contract.get("transition_count"),
        "expected_transition_count": expected_transition_count,
        "raw_discontinuity_count": contract.get("raw_discontinuity_count"),
        "normalized_discontinuity_count": contract.get("normalized_discontinuity_count"),
        "max_raw_transition_delta": contract.get("max_raw_transition_delta"),
        "max_normalized_transition_delta": contract.get("max_normalized_transition_delta"),
        "normalization_policy": contract.get("normalization_policy"),
    }


def runtime_anb_raw_ir_contract_summary(
    runtime_profile: object,
    expected_export: dict[str, object] | None,
) -> dict[str, object]:
    if not isinstance(runtime_profile, dict):
        return {
            "status": "missing_runtime_profile",
            "format": None,
            "payload_count": 0,
        }

    contract = runtime_profile.get("anb_raw_ir_contract")
    if not isinstance(contract, dict):
        return {
            "status": "missing_contract" if expected_export is not None else "not_required",
            "format": None,
            "payload_count": 0,
        }

    expected_payload_count = (
        int(expected_export.get("payload_count", 0) or 0)
        if isinstance(expected_export, dict)
        else 0
    )
    expected_issue_count = (
        int(expected_export.get("issue_count", 0) or 0)
        if isinstance(expected_export, dict)
        else 0
    )
    expected_duplicate_match_count = (
        int(expected_export.get("duplicate_payload_match_count", 0) or 0)
        if isinstance(expected_export, dict)
        else 0
    )
    expected_decoded_frame_match_count = (
        int(expected_export.get("decoded_frame_table_match_count", 0) or 0)
        if isinstance(expected_export, dict)
        else 0
    )
    status = "valid"
    if expected_export is None and contract.get("status") == "not_provided":
        status = "not_required"
    elif contract.get("format") != "oot3d_character_anb_raw_ir_contract_v1":
        status = "invalid_contract_format"
    elif contract.get("status") != "ready":
        status = "contract_not_ready"
    elif contract.get("source_export_format") != "oot3d_anb_payload_batch_export_v1":
        status = "invalid_source_export_format"
    elif expected_payload_count > 0 and int(contract.get("payload_count", 0) or 0) != expected_payload_count:
        status = "payload_count_mismatch"
    elif int(contract.get("issue_count", 1) or 0) != expected_issue_count:
        status = "issue_count_mismatch"
    elif int(contract.get("issue_count", 1) or 0) != 0:
        status = "source_export_has_issues"
    elif int(contract.get("decoded_frame_table_mismatch_count", 1) or 0) != 0:
        status = "frame_table_size_mismatches_present"
    elif expected_duplicate_match_count > 0 and (
        int(contract.get("duplicate_payload_match_count", 0) or 0) != expected_duplicate_match_count
    ):
        status = "duplicate_match_count_mismatch"
    elif expected_decoded_frame_match_count > 0 and (
        int(contract.get("decoded_frame_table_match_count", 0) or 0) != expected_decoded_frame_match_count
    ):
        status = "decoded_frame_table_match_count_mismatch"
    elif contract.get("channel_count_candidate_counts") != {"67": expected_payload_count}:
        status = "channel_count_candidate_mismatch"
    elif not valid_anb_channel_summary(contract.get("channel_summary")):
        status = "invalid_channel_summary"
    elif not valid_anb_csab_lookup(contract.get("csab_lookup")):
        status = "invalid_csab_lookup"
    elif not normalize_resource_path(contract.get("resource_path")):
        status = "missing_resource_path"

    return {
        "status": status,
        "format": contract.get("format"),
        "contract_status": contract.get("status"),
        "source_export_format": contract.get("source_export_format"),
        "resource_path": contract.get("resource_path"),
        "payload_count": contract.get("payload_count"),
        "expected_payload_count": expected_payload_count,
        "duplicate_payload_match_count": contract.get("duplicate_payload_match_count"),
        "expected_duplicate_payload_match_count": expected_duplicate_match_count,
        "duplicate_payload_missing_count": contract.get("duplicate_payload_missing_count"),
        "duplicate_payload_mismatch_count": contract.get("duplicate_payload_mismatch_count"),
        "invalid_header_count": contract.get("invalid_header_count"),
        "decoded_frame_table_match_count": contract.get("decoded_frame_table_match_count"),
        "expected_decoded_frame_table_match_count": expected_decoded_frame_match_count,
        "decoded_frame_table_mismatch_count": contract.get("decoded_frame_table_mismatch_count"),
        "total_decoded_frame_count": contract.get("total_decoded_frame_count"),
        "total_s16_sample_count": contract.get("total_s16_sample_count"),
        "channel_count_candidate_counts": contract.get("channel_count_candidate_counts"),
        "channel_summary": contract.get("channel_summary"),
        "csab_lookup": contract.get("csab_lookup"),
        "issue_count": contract.get("issue_count"),
        "expected_issue_count": expected_issue_count,
    }


def runtime_anb_semantic_candidate_contract_summary(
    runtime_profile: object,
    expected_audit: dict[str, object] | None,
) -> dict[str, object]:
    if not isinstance(runtime_profile, dict):
        return {
            "status": "missing_runtime_profile",
            "format": None,
            "candidate_channel_count": 0,
        }

    contract = runtime_profile.get("anb_semantic_candidate_contract")
    if not isinstance(contract, dict):
        return {
            "status": "missing_contract" if expected_audit is not None else "not_required",
            "format": None,
            "candidate_channel_count": 0,
        }

    expected_compared_count = (
        int(expected_audit.get("compared_record_count", 0) or 0)
        if isinstance(expected_audit, dict)
        else 0
    )
    expected_candidate_channel_count = (
        int(expected_audit.get("candidate_channel_count", 0) or 0)
        if isinstance(expected_audit, dict)
        else 0
    )
    expected_strong_candidate_count = (
        int(expected_audit.get("strong_candidate_count", 0) or 0)
        if isinstance(expected_audit, dict)
        else 0
    )
    expected_stable_candidate_channel_count = (
        int(expected_audit.get("stable_candidate_channel_count", 0) or 0)
        if isinstance(expected_audit, dict)
        else 0
    )

    status = "valid"
    if expected_audit is None and contract.get("status") == "not_provided":
        status = "not_required"
    elif contract.get("format") != "oot3d_character_anb_semantic_candidate_contract_v1":
        status = "invalid_contract_format"
    elif contract.get("status") != "ready":
        status = "contract_not_ready"
    elif contract.get("source_audit_format") != "oot3d_anb_semantic_candidate_audit_v1":
        status = "invalid_source_audit_format"
    elif int(contract.get("channel_count", 0) or 0) != 67:
        status = "channel_count_mismatch"
    elif expected_compared_count > 0 and int(contract.get("compared_record_count", 0) or 0) != expected_compared_count:
        status = "compared_record_count_mismatch"
    elif expected_candidate_channel_count > 0 and (
        int(contract.get("candidate_channel_count", 0) or 0) != expected_candidate_channel_count
    ):
        status = "candidate_channel_count_mismatch"
    elif expected_strong_candidate_count > 0 and (
        int(contract.get("strong_candidate_count", 0) or 0) != expected_strong_candidate_count
    ):
        status = "strong_candidate_count_mismatch"
    elif expected_stable_candidate_channel_count > 0 and (
        int(contract.get("stable_candidate_channel_count", 0) or 0) != expected_stable_candidate_channel_count
    ):
        status = "stable_candidate_channel_count_mismatch"
    elif expected_stable_candidate_channel_count > 0 and not valid_anb_semantic_stable_mappings(
        contract.get("stable_candidate_mappings")
    ):
        status = "invalid_stable_candidate_mappings"
    elif not valid_anb_semantic_channel_candidates(contract.get("channel_candidates")):
        status = "invalid_channel_candidates"

    return {
        "status": status,
        "format": contract.get("format"),
        "contract_status": contract.get("status"),
        "source_audit_format": contract.get("source_audit_format"),
        "matched_anb_record_count": contract.get("matched_anb_record_count"),
        "compared_record_count": contract.get("compared_record_count"),
        "expected_compared_record_count": expected_compared_count,
        "status_counts": contract.get("status_counts"),
        "total_compared_frame_component_pairs": contract.get("total_compared_frame_component_pairs"),
        "min_abs_correlation": contract.get("min_abs_correlation"),
        "include_frame_mismatch_resample": contract.get("include_frame_mismatch_resample"),
        "channel_count": contract.get("channel_count"),
        "candidate_channel_count": contract.get("candidate_channel_count"),
        "expected_candidate_channel_count": expected_candidate_channel_count,
        "strong_candidate_count": contract.get("strong_candidate_count"),
        "expected_strong_candidate_count": expected_strong_candidate_count,
        "stable_candidate_channel_count": contract.get("stable_candidate_channel_count"),
        "expected_stable_candidate_channel_count": expected_stable_candidate_channel_count,
        "resolved_zero_candidate_channel_count": contract.get("resolved_zero_candidate_channel_count"),
        "resolved_static_zero_candidate_channel_count": contract.get("resolved_static_zero_candidate_channel_count"),
        "resolved_constant_control_tuple_channel_count": contract.get(
            "resolved_constant_control_tuple_channel_count"
        ),
        "unstable_candidate_channel_count": contract.get("unstable_candidate_channel_count"),
        "zero_candidate_channel_count": contract.get("zero_candidate_channel_count"),
        "unresolved_zero_candidate_channel_count": contract.get("unresolved_zero_candidate_channel_count"),
        "unresolved_channel_count": contract.get("unresolved_channel_count"),
        "unstable_candidate_channel_class_counts": contract.get("unstable_candidate_channel_class_counts"),
        "unstable_candidate_semantic_class_counts": contract.get("unstable_candidate_semantic_class_counts"),
        "zero_candidate_channel_class_counts": contract.get("zero_candidate_channel_class_counts"),
        "unresolved_zero_candidate_channel_class_counts": contract.get("unresolved_zero_candidate_channel_class_counts"),
        "zero_candidate_semantic_class_counts": contract.get("zero_candidate_semantic_class_counts"),
        "zero_candidate_resolution_status_counts": contract.get("zero_candidate_resolution_status_counts"),
        "unstable_candidate_channel_classification": contract.get("unstable_candidate_channel_classification"),
        "zero_candidate_channel_classification": contract.get("zero_candidate_channel_classification"),
        "stable_candidate_mappings": contract.get("stable_candidate_mappings"),
        "component_candidate_counts": contract.get("component_candidate_counts"),
    }


def valid_anb_semantic_channel_candidates(value: object) -> bool:
    if not isinstance(value, list) or len(value) != 67:
        return False
    for channel_index, record in enumerate(value):
        if not isinstance(record, dict):
            return False
        if record.get("anb_channel") != channel_index:
            return False
        if not isinstance(record.get("candidate_count"), int) or int(record.get("candidate_count")) < 0:
            return False
        best_candidate = record.get("best_candidate")
        if best_candidate is not None and not isinstance(best_candidate, dict):
            return False
    return True


def valid_anb_semantic_stable_mappings(value: object) -> bool:
    if not isinstance(value, list):
        return False
    seen_channels: set[int] = set()
    for record in value:
        if not isinstance(record, dict):
            return False
        channel = record.get("anb_channel")
        if not isinstance(channel, int) or channel < 0 or channel >= 67:
            return False
        if channel in seen_channels:
            return False
        seen_channels.add(channel)
        if not isinstance(record.get("component"), str) or not record.get("component"):
            return False
        consensus_count = record.get("consensus_count")
        if not isinstance(consensus_count, int) or consensus_count < 1:
            return False
        consensus_share = record.get("consensus_share")
        if not isinstance(consensus_share, (int, float)) or float(consensus_share) <= 0.0:
            return False
    return True


def valid_anb_channel_summary(value: object) -> bool:
    if not isinstance(value, dict):
        return False
    if int(value.get("channel_count", 0) or 0) != 67:
        return False
    channel_bounds = value.get("channel_bounds")
    if not isinstance(channel_bounds, list) or len(channel_bounds) != 67:
        return False
    for channel_index, record in enumerate(channel_bounds):
        if not isinstance(record, dict):
            return False
        raw_channel_index = record.get("channel_index")
        if not isinstance(raw_channel_index, int) or raw_channel_index != channel_index:
            return False
        if not isinstance(record.get("min"), int) or not isinstance(record.get("max"), int):
            return False
        if int(record.get("distinct_sample_count", 0) or 0) <= 0:
            return False
        raw_nonzero_count = record.get("nonzero_sample_count")
        if not isinstance(raw_nonzero_count, int) or raw_nonzero_count < 0:
            return False
    return True


def valid_anb_csab_lookup(value: object) -> bool:
    if not isinstance(value, dict):
        return False
    status = value.get("status")
    if status == "not_provided":
        return True
    if status != "provided":
        return False
    for key in (
        "csab_stem_count",
        "anb_stem_count",
        "matched_anb_count",
        "matched_stem_count",
        "unmatched_anb_count",
        "csab_without_anb_count",
    ):
        if not isinstance(value.get(key), int) or int(value.get(key)) < 0:
            return False
    matched_records = value.get("matched_records")
    if not isinstance(matched_records, list):
        return False
    if len(matched_records) != int(value.get("matched_anb_count", -1)):
        return False
    for record in matched_records:
        if not isinstance(record, dict):
            return False
        if not normalize_resource_path(record.get("embedded_name")):
            return False
        if not normalize_resource_path(record.get("path_stem")):
            return False
        if not normalize_resource_path(record.get("csab_name")):
            return False
    return True


def expected_native_selection_primitive_count(target: object) -> int:
    if not isinstance(target, dict):
        return 0
    native_bind_pose = target.get("native_bind_pose")
    if not isinstance(native_bind_pose, dict):
        return 0
    value = native_bind_pose.get("selection_primitive_key_count")
    return int(value) if isinstance(value, int) else 0


def resource_record_expects_json(record: dict[str, object]) -> bool:
    return record.get("kind") in {"bind_pose", "csab_track", "native_bind_pose_manifest", "anb_payload_batch"}


def character_manifest_issue_count(character_manifest: dict[str, object]) -> int:
    issue_counts = character_manifest.get("issue_counts", {})
    if not isinstance(issue_counts, dict):
        return 1
    return int(issue_counts.get("total", 1) or 0)


def audit_archived_resources(
    archive: zipfile.ZipFile,
    expected_paths: set[str],
    archive_name_set: set[str],
) -> tuple[list[dict[str, str]], Counter[str], dict[str, object]]:
    invalid_entries: list[dict[str, str]] = []
    format_counts: Counter[str] = Counter()
    bind_pose_contract = {
        "checked_bind_pose_count": 0,
        "renderable_bind_pose_count": 0,
        "issue_count": 0,
        "sample_issues": [],
    }
    for archive_path in sorted(expected_paths & archive_name_set):
        try:
            loaded = json.loads(archive.read(archive_path).decode("utf-8"))
        except (KeyError, UnicodeDecodeError, json.JSONDecodeError) as exc:
            invalid_entries.append({"path": archive_path, "error": str(exc)})
            continue
        if isinstance(loaded, dict):
            resource_format = str(loaded.get("format", "<missing>"))
            format_counts[resource_format] += 1
            if resource_format == "oot3d_skinned_bind_pose_export_v1":
                bind_pose_issues = audit_bind_pose_render_contract(loaded)
                bind_pose_contract["checked_bind_pose_count"] = (
                    int(bind_pose_contract["checked_bind_pose_count"]) + 1
                )
                if bind_pose_issues:
                    bind_pose_contract["issue_count"] = (
                        int(bind_pose_contract["issue_count"]) + len(bind_pose_issues)
                    )
                    samples = bind_pose_contract["sample_issues"]
                    if isinstance(samples, list):
                        samples.extend(
                            {"path": archive_path, **issue}
                            for issue in bind_pose_issues[: max(0, 20 - len(samples))]
                        )
                else:
                    bind_pose_contract["renderable_bind_pose_count"] = (
                        int(bind_pose_contract["renderable_bind_pose_count"]) + 1
                    )
    return invalid_entries, format_counts, bind_pose_contract


def audit_bind_pose_render_contract(resource: dict[str, object]) -> list[dict[str, object]]:
    issues: list[dict[str, object]] = []
    textures = resource.get("textures")
    materials = resource.get("materials")
    meshes = resource.get("meshes")
    if not isinstance(textures, list) or not textures:
        issues.append({"type": "missing_textures"})
    if not isinstance(materials, list) or not materials:
        issues.append({"type": "missing_materials"})
    if not isinstance(meshes, list) or not meshes:
        issues.append({"type": "missing_meshes"})
        return issues

    primitive_count = 0
    for mesh_index, mesh in enumerate(meshes):
        if not isinstance(mesh, dict):
            issues.append({"type": "invalid_mesh_record", "mesh_index": mesh_index})
            continue
        primitives = mesh.get("primitives")
        if not isinstance(primitives, list):
            issues.append({"type": "missing_primitives", "mesh_index": mesh_index})
            continue
        for primitive_index, primitive in enumerate(primitives):
            primitive_count += 1
            if not isinstance(primitive, dict):
                issues.append(
                    {
                        "type": "invalid_primitive_record",
                        "mesh_index": mesh_index,
                        "primitive_index": primitive_index,
                    }
                )
                continue
            issues.extend(audit_bind_pose_primitive_contract(mesh_index, primitive_index, primitive))
    if primitive_count == 0:
        issues.append({"type": "missing_renderable_primitives"})
    return issues


def audit_bind_pose_primitive_contract(
    mesh_index: int,
    primitive_index: int,
    primitive: dict[str, object],
) -> list[dict[str, object]]:
    issues: list[dict[str, object]] = []
    indices = primitive.get("indices")
    source_indices = primitive.get("source_indices")
    vertices = primitive.get("vertices")
    if not isinstance(indices, list) or not indices:
        return [
            {
                "type": "missing_indices",
                "mesh_index": mesh_index,
                "primitive_index": primitive_index,
            }
        ]
    if not isinstance(source_indices, list) or len(source_indices) != len(indices):
        issues.append(
            {
                "type": "source_index_count_mismatch",
                "mesh_index": mesh_index,
                "primitive_index": primitive_index,
            }
        )
    if not isinstance(vertices, list) or not vertices:
        return [
            *issues,
            {
                "type": "missing_vertices",
                "mesh_index": mesh_index,
                "primitive_index": primitive_index,
            },
        ]
    if primitive.get("index_count") != len(indices):
        issues.append(
            {
                "type": "index_count_mismatch",
                "mesh_index": mesh_index,
                "primitive_index": primitive_index,
            }
        )
    if len(indices) % 3 != 0:
        issues.append(
            {
                "type": "non_triangular_index_count",
                "mesh_index": mesh_index,
                "primitive_index": primitive_index,
                "index_count": len(indices),
            }
        )
    bad_index_count = sum(
        1
        for index in indices
        if not isinstance(index, int) or index < 0 or index >= len(vertices)
    )
    if bad_index_count:
        issues.append(
            {
                "type": "index_out_of_range",
                "mesh_index": mesh_index,
                "primitive_index": primitive_index,
                "count": bad_index_count,
            }
        )
    missing_uv_count = 0
    missing_color_count = 0
    for vertex in vertices:
        if not isinstance(vertex, dict):
            continue
        if not valid_float_pair(vertex.get("source_uv0")):
            missing_uv_count += 1
        if not valid_color_rgba(vertex.get("source_color_rgba")):
            missing_color_count += 1
    if missing_uv_count:
        issues.append(
            {
                "type": "missing_or_invalid_uv0",
                "mesh_index": mesh_index,
                "primitive_index": primitive_index,
                "count": missing_uv_count,
            }
        )
    if missing_color_count:
        issues.append(
            {
                "type": "missing_or_invalid_color_rgba",
                "mesh_index": mesh_index,
                "primitive_index": primitive_index,
                "count": missing_color_count,
            }
        )
    return issues


def valid_float_pair(value: object) -> bool:
    return (
        isinstance(value, list)
        and len(value) == 2
        and all(isinstance(item, (int, float)) for item in value)
    )


def valid_color_rgba(value: object) -> bool:
    return (
        isinstance(value, list)
        and len(value) == 4
        and all(isinstance(item, int) and 0 <= item <= 255 for item in value)
    )


def archived_json_matches(archive: zipfile.ZipFile, archive_path: str, expected: object) -> bool:
    error, loaded = read_archive_json_with_error(archive, archive_path)
    return error is None and loaded == expected


def read_archive_json_with_error(archive: zipfile.ZipFile, archive_path: str) -> tuple[str | None, object | None]:
    try:
        return None, json.loads(archive.read(archive_path).decode("utf-8"))
    except (KeyError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        return str(exc), None


def read_archive_json_error(archive: zipfile.ZipFile, archive_path: str) -> str | None:
    return read_archive_json_with_error(archive, archive_path)[0]


def read_path_json_error(path: Path) -> tuple[str | None, object | None]:
    try:
        return None, read_json(path)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        return str(exc), None


def read_json(path: Path) -> dict[str, object]:
    return json.loads(path.read_text(encoding="utf-8"))


def read_optional_json(path: Path | None) -> dict[str, object] | None:
    if path is None:
        return None
    if not path.is_file():
        raise ParseError(f"{path}: optional JSON input not found")
    value = read_json(path)
    if not isinstance(value, dict):
        raise ParseError(f"{path}: JSON root must be an object")
    return value


def write_archive_json(archive: zipfile.ZipFile, archive_path: str, value: object) -> None:
    archive.writestr(archive_path, json.dumps(value, indent=2) + "\n")


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8", newline="\n")


def normalize_resource_path(value: object) -> str:
    return str(value or "").replace("\\", "/").strip("/")


def sanitize_path_part(value: str) -> str:
    sanitized = "".join(ch if ch.isalnum() or ch in ("_", "-") else "_" for ch in value.strip())
    return sanitized.strip("_") or "character"
