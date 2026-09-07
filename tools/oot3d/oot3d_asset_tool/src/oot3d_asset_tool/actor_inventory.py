from __future__ import annotations

import json
import math
import struct
from collections import Counter
from pathlib import Path

from .binary import ParseError
from .cmb import CmbModel, Skeleton, SkeletonBone, Vec3
from .romfs_inventory import normalized_suffix, sorted_counter
from .legacy_fast_resource import Matrix4, bone_local_transform, multiply_matrix
from .zar import ZarArchive, ZarFile

KNOWN_ANIMATION_TYPES = {"csab", "cmab"}
ANIMATION_LIKE_TYPES = {"csab", "cmab", "anb", "faceb"}
ANIMATION_HEADER_FIELD_OFFSETS = {
    "csab": tuple(range(0x0C, 0x40, 4)),
    "cmab": tuple(range(0x0C, 0x40, 4)),
}
CSAB_FRAME_COUNT_CANDIDATE_OFFSET = 0x28
CSAB_ANIMATED_BONE_COUNT_CANDIDATE_OFFSET = 0x30
CSAB_SKELETON_BONE_COUNT_CANDIDATE_OFFSET = 0x34
CSAB_BONE_TABLE_OFFSET = 0x38
CSAB_NODE_OFFSET_BASE = 0x18
CSAB_NODE_MAGIC = b"anod"
CSAB_ANOD_HEADER_SIZE = 0x1C
CSAB_ANOD_CHANNEL_OFFSET_TABLE_OFFSET = 0x08
CSAB_ANOD_CHANNEL_OFFSET_COUNT = 10
CSAB_ANOD_CHANNEL_BLOCK_HEADER_SIZE = 0x08
CSAB_ANOD_CHANNEL_BLOCK_KEY_HEADER_SIZE = 0x10
CSAB_ANOD_F32_KEY_SIZE = 0x10
CSAB_ANOD_S16_KEY_SIZE = 0x08
CSAB_ANOD_HERMITE_INTERVAL_SCALE = 1.0 / 30.0
CSAB_ANOD_S16_ROTATION_SCALE = math.pi / 32768.0
CSAB_ROTATION_CHANNEL_SLOTS = {3, 4, 5}
MODEL_STEM_SUFFIXES = ("_modelt", "_models", "_model", "_parts", "_part")
CURRENT_RIGID_EXPORT_TARGET_SUPPORT = {
    "rigid_multibone_export_supported",
    "static_cmb_export_supported",
}
SKELETON_METADATA_FIELDS = (
    "chunk_size_delta",
    "header_word_0c",
    "root_count",
    "max_depth",
    "bone_index_mismatch_count",
    "parent_out_of_range_count",
    "non_identity_scale_count",
    "nonzero_rotation_count",
    "nonzero_translation_count",
)


def inventory_actors(
    actor_root: Path,
    output_path: Path | None = None,
    *,
    include_records: bool = True,
    sample_limit: int = 100,
) -> dict[str, object]:
    if not actor_root.is_dir():
        raise ParseError(f"{actor_root}: expected an extracted OOT3D actor directory")

    paths = sorted(path for path in actor_root.rglob("*") if path.is_file())
    zar_paths = [path for path in paths if normalized_suffix(path) == ".zar"]
    loose_cmb_paths = [path for path in paths if normalized_suffix(path) == ".cmb"]

    archive_records: list[dict[str, object]] = []
    model_records: list[dict[str, object]] = []
    parse_errors: list[dict[str, object]] = []
    nonstatic_model_samples: list[dict[str, object]] = []

    embedded_type_counts: Counter[str] = Counter()
    embedded_extension_counts: Counter[str] = Counter()
    archive_counts: Counter[str] = Counter()
    archive_support_counts: Counter[str] = Counter()
    cmb_counts: Counter[str] = Counter()
    model_support_counts: Counter[str] = Counter()
    skinning_mode_primitive_counts: Counter[str] = Counter()
    primitive_bone_count_counts: Counter[str] = Counter()
    skinning_mode_bone_count_counts: Counter[str] = Counter()
    bone_count_counts: Counter[str] = Counter()
    skeleton_metadata_counts: dict[str, Counter[str]] = {
        f"{field}_counts": Counter() for field in SKELETON_METADATA_FIELDS
    }
    animation_file_counts: Counter[str] = Counter()
    animation_like_file_counts: Counter[str] = Counter()
    animation_payload_magic_counts: dict[str, Counter[str]] = {}
    animation_payload_declared_size_match_counts: dict[str, Counter[str]] = {}
    animation_payload_header_version_counts: dict[str, Counter[str]] = {}
    animation_payload_header_field_counts: dict[str, dict[str, Counter[str]]] = {}
    animation_payload_archive_support_counts: dict[str, Counter[str]] = {}
    animation_payload_sizes: dict[str, list[int]] = {}
    animation_payload_samples: list[dict[str, object]] = []
    csab_node_table_counts: Counter[str] = Counter()
    csab_anod_record_counts: Counter[str] = Counter()
    csab_anod_active_channel_count_counts: Counter[str] = Counter()
    csab_anod_channel_slot_active_counts: Counter[str] = Counter()
    csab_anod_record_field_04_high16_counts: Counter[str] = Counter()
    csab_anod_channel_block_counts: Counter[str] = Counter()
    csab_anod_channel_block_class_counts: Counter[str] = Counter()
    csab_anod_channel_block_class_key_entry_counts: Counter[str] = Counter()
    csab_anod_channel_block_key_count_counts: Counter[str] = Counter()
    csab_anod_channel_block_slot_class_counts: Counter[str] = Counter()
    csab_skeleton_match_counts: Counter[str] = Counter()
    csab_target_resolution_counts: Counter[str] = Counter()
    csab_resolved_target_support_counts: Counter[str] = Counter()
    csab_playback_blocker_counts: Counter[str] = Counter()
    csab_playback_candidate_node_table_counts: Counter[str] = Counter()
    csab_playback_candidate_anod_record_counts: Counter[str] = Counter()
    csab_playback_candidate_anod_active_channel_count_counts: Counter[str] = Counter()
    csab_playback_candidate_anod_channel_slot_active_counts: Counter[str] = Counter()
    csab_playback_candidate_anod_record_field_04_high16_counts: Counter[str] = Counter()
    csab_playback_candidate_anod_channel_block_counts: Counter[str] = Counter()
    csab_playback_candidate_anod_channel_block_class_counts: Counter[str] = Counter()
    csab_playback_candidate_anod_channel_block_class_key_entry_counts: Counter[str] = Counter()
    csab_playback_candidate_anod_channel_block_key_count_counts: Counter[str] = Counter()
    csab_playback_candidate_anod_channel_block_slot_class_counts: Counter[str] = Counter()
    csab_playback_candidate_f32_sampler_counts: Counter[str] = Counter()
    csab_playback_candidate_f32_sampler_frame_count_counts: Counter[str] = Counter()
    csab_playback_candidate_f32_sampler_slot_sample_counts: Counter[str] = Counter()
    csab_playback_candidate_pose_sample_counts: Counter[str] = Counter()
    csab_playback_candidate_pose_sample_slot_counts: Counter[str] = Counter()
    csab_playback_candidate_full_pose_sample_counts: Counter[str] = Counter()
    csab_playback_candidate_full_pose_sample_slot_counts: Counter[str] = Counter()
    csab_skeleton_match_samples: list[dict[str, object]] = []
    csab_playback_candidate_samples: list[dict[str, object]] = []

    for path in loose_cmb_paths:
        scan_loose_cmb(
            path,
            actor_root,
            cmb_counts,
            model_support_counts,
            skinning_mode_primitive_counts,
            primitive_bone_count_counts,
            skinning_mode_bone_count_counts,
            bone_count_counts,
            skeleton_metadata_counts,
            model_records,
            parse_errors,
            nonstatic_model_samples,
            sample_limit,
        )

    for path in zar_paths:
        scan_actor_zar(
            path,
            actor_root,
            archive_counts,
            archive_support_counts,
            embedded_type_counts,
            embedded_extension_counts,
            cmb_counts,
            model_support_counts,
            skinning_mode_primitive_counts,
            primitive_bone_count_counts,
            skinning_mode_bone_count_counts,
            bone_count_counts,
            skeleton_metadata_counts,
            animation_file_counts,
            animation_like_file_counts,
            animation_payload_magic_counts,
            animation_payload_declared_size_match_counts,
            animation_payload_header_version_counts,
            animation_payload_header_field_counts,
            animation_payload_archive_support_counts,
            animation_payload_sizes,
            animation_payload_samples,
            csab_node_table_counts,
            csab_anod_record_counts,
            csab_anod_active_channel_count_counts,
            csab_anod_channel_slot_active_counts,
            csab_anod_record_field_04_high16_counts,
            csab_anod_channel_block_counts,
            csab_anod_channel_block_class_counts,
            csab_anod_channel_block_class_key_entry_counts,
            csab_anod_channel_block_key_count_counts,
            csab_anod_channel_block_slot_class_counts,
            csab_skeleton_match_counts,
            csab_target_resolution_counts,
            csab_resolved_target_support_counts,
            csab_playback_blocker_counts,
            csab_playback_candidate_node_table_counts,
            csab_playback_candidate_anod_record_counts,
            csab_playback_candidate_anod_active_channel_count_counts,
            csab_playback_candidate_anod_channel_slot_active_counts,
            csab_playback_candidate_anod_record_field_04_high16_counts,
            csab_playback_candidate_anod_channel_block_counts,
            csab_playback_candidate_anod_channel_block_class_counts,
            csab_playback_candidate_anod_channel_block_class_key_entry_counts,
            csab_playback_candidate_anod_channel_block_key_count_counts,
            csab_playback_candidate_anod_channel_block_slot_class_counts,
            csab_playback_candidate_f32_sampler_counts,
            csab_playback_candidate_f32_sampler_frame_count_counts,
            csab_playback_candidate_f32_sampler_slot_sample_counts,
            csab_playback_candidate_pose_sample_counts,
            csab_playback_candidate_pose_sample_slot_counts,
            csab_playback_candidate_full_pose_sample_counts,
            csab_playback_candidate_full_pose_sample_slot_counts,
            csab_skeleton_match_samples,
            csab_playback_candidate_samples,
            archive_records,
            model_records,
            parse_errors,
            nonstatic_model_samples,
            sample_limit,
        )

    cmb_counts["parsed"] = cmb_counts["static_candidate"] + cmb_counts["nonstatic_candidate"]
    cmb_counts["parse_errors"] = len(
        [error for error in parse_errors if error.get("kind") == "cmb_model"]
    )

    inventory = {
        "schema_version": 1,
        "actor_root": str(actor_root),
        "file_count": len(paths),
        "archive_count": len(zar_paths),
        "loose_cmb_count": len(loose_cmb_paths),
        "embedded_file_count": sum(embedded_type_counts.values()),
        "embedded_type_counts": sorted_counter(embedded_type_counts),
        "embedded_extension_counts": sorted_counter(embedded_extension_counts),
        "animation_file_counts": sorted_counter(animation_file_counts),
        "animation_like_file_counts": sorted_counter(animation_like_file_counts),
        "archive_counts": {
            "with_cmb": archive_counts["with_cmb"],
            "without_cmb": archive_counts["without_cmb"],
            "with_zsi": archive_counts["with_zsi"],
            "with_ctxb": archive_counts["with_ctxb"],
            "with_qdb": archive_counts["with_qdb"],
            "with_known_animation": archive_counts["with_known_animation"],
            "with_animation_like_data": archive_counts["with_animation_like_data"],
            "static_payload_only": archive_counts["static_payload_only"],
        },
        "archive_support_counts": sorted_counter(archive_support_counts),
        "cmb_counts": {
            "discovered": cmb_counts["discovered"],
            "parsed": cmb_counts["parsed"],
            "static_candidate": cmb_counts["static_candidate"],
            "nonstatic_candidate": cmb_counts["nonstatic_candidate"],
            "parse_errors": cmb_counts["parse_errors"],
        },
        "model_support_counts": sorted_counter(model_support_counts),
        "skinning_mode_primitive_counts": sorted_counter(skinning_mode_primitive_counts),
        "primitive_bone_count_counts": sorted_counter(primitive_bone_count_counts),
        "skinning_mode_bone_count_counts": sorted_counter(skinning_mode_bone_count_counts),
        "bone_count_counts": sorted_counter(bone_count_counts),
        "skeleton_metadata_counts": sorted_nested_counter(skeleton_metadata_counts),
        "animation_payload_magic_counts": sorted_nested_counter(animation_payload_magic_counts),
        "animation_payload_declared_size_match_counts": sorted_nested_counter(
            animation_payload_declared_size_match_counts
        ),
        "animation_payload_header_version_counts": sorted_nested_counter(
            animation_payload_header_version_counts
        ),
        "animation_payload_header_field_counts": sorted_deep_nested_counter(
            animation_payload_header_field_counts
        ),
        "animation_payload_archive_support_counts": sorted_nested_counter(
            animation_payload_archive_support_counts
        ),
        "animation_payload_size_summary": animation_payload_size_summary(animation_payload_sizes),
        "animation_payload_samples": animation_payload_samples,
        "csab_node_table_counts": sorted_counter(csab_node_table_counts),
        "csab_anod_record_counts": sorted_counter(csab_anod_record_counts),
        "csab_anod_active_channel_count_counts": sorted_counter(
            csab_anod_active_channel_count_counts
        ),
        "csab_anod_channel_slot_active_counts": sorted_counter(
            csab_anod_channel_slot_active_counts
        ),
        "csab_anod_record_field_04_high16_counts": sorted_counter(
            csab_anod_record_field_04_high16_counts
        ),
        "csab_anod_channel_block_counts": sorted_counter(
            csab_anod_channel_block_counts
        ),
        "csab_anod_channel_block_class_counts": sorted_counter(
            csab_anod_channel_block_class_counts
        ),
        "csab_anod_channel_block_class_key_entry_counts": sorted_counter(
            csab_anod_channel_block_class_key_entry_counts
        ),
        "csab_anod_channel_block_key_count_counts": sorted_counter(
            csab_anod_channel_block_key_count_counts
        ),
        "csab_anod_channel_block_slot_class_counts": sorted_counter(
            csab_anod_channel_block_slot_class_counts
        ),
        "csab_skeleton_match_counts": sorted_counter(csab_skeleton_match_counts),
        "csab_target_resolution_counts": sorted_counter(csab_target_resolution_counts),
        "csab_resolved_target_support_counts": sorted_counter(
            csab_resolved_target_support_counts
        ),
        "csab_playback_blocker_counts": sorted_counter(csab_playback_blocker_counts),
        "csab_playback_candidate_node_table_counts": sorted_counter(
            csab_playback_candidate_node_table_counts
        ),
        "csab_playback_candidate_anod_record_counts": sorted_counter(
            csab_playback_candidate_anod_record_counts
        ),
        "csab_playback_candidate_anod_active_channel_count_counts": sorted_counter(
            csab_playback_candidate_anod_active_channel_count_counts
        ),
        "csab_playback_candidate_anod_channel_slot_active_counts": sorted_counter(
            csab_playback_candidate_anod_channel_slot_active_counts
        ),
        "csab_playback_candidate_anod_record_field_04_high16_counts": sorted_counter(
            csab_playback_candidate_anod_record_field_04_high16_counts
        ),
        "csab_playback_candidate_anod_channel_block_counts": sorted_counter(
            csab_playback_candidate_anod_channel_block_counts
        ),
        "csab_playback_candidate_anod_channel_block_class_counts": sorted_counter(
            csab_playback_candidate_anod_channel_block_class_counts
        ),
        "csab_playback_candidate_anod_channel_block_class_key_entry_counts": sorted_counter(
            csab_playback_candidate_anod_channel_block_class_key_entry_counts
        ),
        "csab_playback_candidate_anod_channel_block_key_count_counts": sorted_counter(
            csab_playback_candidate_anod_channel_block_key_count_counts
        ),
        "csab_playback_candidate_anod_channel_block_slot_class_counts": sorted_counter(
            csab_playback_candidate_anod_channel_block_slot_class_counts
        ),
        "csab_playback_candidate_f32_sampler_counts": sorted_counter(
            csab_playback_candidate_f32_sampler_counts
        ),
        "csab_playback_candidate_f32_sampler_frame_count_counts": sorted_counter(
            csab_playback_candidate_f32_sampler_frame_count_counts
        ),
        "csab_playback_candidate_f32_sampler_slot_sample_counts": sorted_counter(
            csab_playback_candidate_f32_sampler_slot_sample_counts
        ),
        "csab_playback_candidate_pose_sample_counts": sorted_counter(
            csab_playback_candidate_pose_sample_counts
        ),
        "csab_playback_candidate_pose_sample_slot_counts": sorted_counter(
            csab_playback_candidate_pose_sample_slot_counts
        ),
        "csab_playback_candidate_full_pose_sample_counts": sorted_counter(
            csab_playback_candidate_full_pose_sample_counts
        ),
        "csab_playback_candidate_full_pose_sample_slot_counts": sorted_counter(
            csab_playback_candidate_full_pose_sample_slot_counts
        ),
        "csab_skeleton_match_samples": csab_skeleton_match_samples,
        "csab_playback_candidate_samples": csab_playback_candidate_samples,
        "nonstatic_model_samples": nonstatic_model_samples,
        "parse_error_count": len(parse_errors),
        "parse_errors": parse_errors,
        "archive_records": archive_records if include_records else [],
        "model_records": model_records if include_records else [],
    }

    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8", newline="\n")
    return inventory


def scan_actor_zar(
    path: Path,
    actor_root: Path,
    archive_counts: Counter[str],
    archive_support_counts: Counter[str],
    embedded_type_counts: Counter[str],
    embedded_extension_counts: Counter[str],
    cmb_counts: Counter[str],
    model_support_counts: Counter[str],
    skinning_mode_primitive_counts: Counter[str],
    primitive_bone_count_counts: Counter[str],
    skinning_mode_bone_count_counts: Counter[str],
    bone_count_counts: Counter[str],
    skeleton_metadata_counts: dict[str, Counter[str]],
    animation_file_counts: Counter[str],
    animation_like_file_counts: Counter[str],
    animation_payload_magic_counts: dict[str, Counter[str]],
    animation_payload_declared_size_match_counts: dict[str, Counter[str]],
    animation_payload_header_version_counts: dict[str, Counter[str]],
    animation_payload_header_field_counts: dict[str, dict[str, Counter[str]]],
    animation_payload_archive_support_counts: dict[str, Counter[str]],
    animation_payload_sizes: dict[str, list[int]],
    animation_payload_samples: list[dict[str, object]],
    csab_node_table_counts: Counter[str],
    csab_anod_record_counts: Counter[str],
    csab_anod_active_channel_count_counts: Counter[str],
    csab_anod_channel_slot_active_counts: Counter[str],
    csab_anod_record_field_04_high16_counts: Counter[str],
    csab_anod_channel_block_counts: Counter[str],
    csab_anod_channel_block_class_counts: Counter[str],
    csab_anod_channel_block_class_key_entry_counts: Counter[str],
    csab_anod_channel_block_key_count_counts: Counter[str],
    csab_anod_channel_block_slot_class_counts: Counter[str],
    csab_skeleton_match_counts: Counter[str],
    csab_target_resolution_counts: Counter[str],
    csab_resolved_target_support_counts: Counter[str],
    csab_playback_blocker_counts: Counter[str],
    csab_playback_candidate_node_table_counts: Counter[str],
    csab_playback_candidate_anod_record_counts: Counter[str],
    csab_playback_candidate_anod_active_channel_count_counts: Counter[str],
    csab_playback_candidate_anod_channel_slot_active_counts: Counter[str],
    csab_playback_candidate_anod_record_field_04_high16_counts: Counter[str],
    csab_playback_candidate_anod_channel_block_counts: Counter[str],
    csab_playback_candidate_anod_channel_block_class_counts: Counter[str],
    csab_playback_candidate_anod_channel_block_class_key_entry_counts: Counter[str],
    csab_playback_candidate_anod_channel_block_key_count_counts: Counter[str],
    csab_playback_candidate_anod_channel_block_slot_class_counts: Counter[str],
    csab_playback_candidate_f32_sampler_counts: Counter[str],
    csab_playback_candidate_f32_sampler_frame_count_counts: Counter[str],
    csab_playback_candidate_f32_sampler_slot_sample_counts: Counter[str],
    csab_playback_candidate_pose_sample_counts: Counter[str],
    csab_playback_candidate_pose_sample_slot_counts: Counter[str],
    csab_playback_candidate_full_pose_sample_counts: Counter[str],
    csab_playback_candidate_full_pose_sample_slot_counts: Counter[str],
    csab_skeleton_match_samples: list[dict[str, object]],
    csab_playback_candidate_samples: list[dict[str, object]],
    archive_records: list[dict[str, object]],
    model_records: list[dict[str, object]],
    parse_errors: list[dict[str, object]],
    nonstatic_model_samples: list[dict[str, object]],
    sample_limit: int,
) -> None:
    try:
        archive = ZarArchive.from_path(path)
    except Exception as exc:
        add_parse_error(parse_errors, path, actor_root, "zar_archive", str(exc))
        archive_records.append(
            {
                "path": path.relative_to(actor_root).as_posix(),
                "status": "parse_error",
                "error": str(exc),
            }
        )
        return

    type_counts: Counter[str] = Counter(file.type_name for file in archive.files)
    extension_counts: Counter[str] = Counter(normalized_suffix(Path(file.name)) for file in archive.files)
    cmb_files = archive.cmb_files()
    animation_count = sum(type_counts[file_type] for file_type in KNOWN_ANIMATION_TYPES)
    animation_like_count = sum(type_counts[file_type] for file_type in ANIMATION_LIKE_TYPES)

    embedded_type_counts.update(type_counts)
    embedded_extension_counts.update(extension_counts)
    animation_file_counts.update(
        {file_type: type_counts[file_type] for file_type in KNOWN_ANIMATION_TYPES if type_counts[file_type]}
    )
    animation_like_file_counts.update(
        {file_type: type_counts[file_type] for file_type in ANIMATION_LIKE_TYPES if type_counts[file_type]}
    )
    for file in archive.files:
        if file.type_name in ANIMATION_LIKE_TYPES:
            add_animation_payload_metadata(
                archive,
                file,
                path,
                actor_root,
                animation_payload_magic_counts,
                animation_payload_declared_size_match_counts,
                animation_payload_header_version_counts,
                animation_payload_header_field_counts,
                animation_payload_sizes,
                animation_payload_samples,
                csab_node_table_counts,
                csab_anod_record_counts,
                csab_anod_active_channel_count_counts,
                csab_anod_channel_slot_active_counts,
                csab_anod_record_field_04_high16_counts,
                csab_anod_channel_block_counts,
                csab_anod_channel_block_class_counts,
                csab_anod_channel_block_class_key_entry_counts,
                csab_anod_channel_block_key_count_counts,
                csab_anod_channel_block_slot_class_counts,
                sample_limit,
            )

    archive_counts["with_cmb" if cmb_files else "without_cmb"] += 1
    if type_counts["zsi"]:
        archive_counts["with_zsi"] += 1
    if type_counts["ctxb"]:
        archive_counts["with_ctxb"] += 1
    if type_counts["qdb"]:
        archive_counts["with_qdb"] += 1
    if animation_count:
        archive_counts["with_known_animation"] += 1
    if animation_like_count:
        archive_counts["with_animation_like_data"] += 1
    if set(type_counts).issubset({"cmb", "zsi", "ctxb"}):
        archive_counts["static_payload_only"] += 1

    parsed_cmb_count = 0
    static_cmb_count = 0
    nonstatic_cmb_count = 0
    cmb_bone_records: list[dict[str, object]] = []
    cmb_models_by_embedded_name: dict[str, CmbModel] = {}
    for file in cmb_files:
        cmb_counts["discovered"] += 1
        try:
            model = CmbModel.parse(archive.read_file(file), f"{path}!{file.name}")
        except Exception as exc:
            add_parse_error(
                parse_errors,
                path,
                actor_root,
                "cmb_model",
                str(exc),
                embedded_name=file.name,
            )
            continue
        parsed_cmb_count += 1
        if model.is_static_candidate():
            static_cmb_count += 1
        else:
            nonstatic_cmb_count += 1
        cmb_models_by_embedded_name[file.name] = model
        cmb_bone_records.append(
            {
                "embedded_name": file.name,
                "model_name": model.name,
                "bone_count": model.bone_count,
                "support_status": model_support_status(model),
            }
        )
        add_model_record(
            model,
            path,
            actor_root,
            "zar",
            cmb_counts,
            model_support_counts,
            skinning_mode_primitive_counts,
            primitive_bone_count_counts,
            skinning_mode_bone_count_counts,
            bone_count_counts,
            skeleton_metadata_counts,
            model_records,
            nonstatic_model_samples,
            sample_limit,
            embedded_file=file,
        )

    support_status = archive_support_status(
        parsed_cmb_count,
        nonstatic_cmb_count,
        animation_count,
        animation_like_count,
    )
    for file in archive.files:
        if file.type_name in ANIMATION_LIKE_TYPES:
            animation_payload_archive_support_counts.setdefault(file.type_name, Counter())[
                support_status
            ] += 1
        if file.type_name == "csab":
            add_csab_skeleton_match_metadata(
                archive,
                file,
                path,
                actor_root,
                cmb_bone_records,
                csab_skeleton_match_counts,
                csab_target_resolution_counts,
                csab_resolved_target_support_counts,
                csab_playback_blocker_counts,
                csab_playback_candidate_node_table_counts,
                csab_playback_candidate_anod_record_counts,
                csab_playback_candidate_anod_active_channel_count_counts,
                csab_playback_candidate_anod_channel_slot_active_counts,
                csab_playback_candidate_anod_record_field_04_high16_counts,
                csab_playback_candidate_anod_channel_block_counts,
                csab_playback_candidate_anod_channel_block_class_counts,
                csab_playback_candidate_anod_channel_block_class_key_entry_counts,
                csab_playback_candidate_anod_channel_block_key_count_counts,
                csab_playback_candidate_anod_channel_block_slot_class_counts,
                csab_playback_candidate_f32_sampler_counts,
                csab_playback_candidate_f32_sampler_frame_count_counts,
                csab_playback_candidate_f32_sampler_slot_sample_counts,
                csab_playback_candidate_pose_sample_counts,
                csab_playback_candidate_pose_sample_slot_counts,
                csab_playback_candidate_full_pose_sample_counts,
                csab_playback_candidate_full_pose_sample_slot_counts,
                csab_skeleton_match_samples,
                csab_playback_candidate_samples,
                sample_limit,
                cmb_models_by_embedded_name,
            )
    archive_support_counts[support_status] += 1
    archive_records.append(
        {
            "path": path.relative_to(actor_root).as_posix(),
            "status": "parsed",
            "file_count": len(archive.files),
            "type_counts": sorted_counter(type_counts),
            "extension_counts": sorted_counter(extension_counts),
            "cmb_count": len(cmb_files),
            "parsed_cmb_count": parsed_cmb_count,
            "static_cmb_count": static_cmb_count,
            "nonstatic_cmb_count": nonstatic_cmb_count,
            "known_animation_file_count": animation_count,
            "animation_like_file_count": animation_like_count,
            "support_status": support_status,
        }
    )


def scan_loose_cmb(
    path: Path,
    actor_root: Path,
    cmb_counts: Counter[str],
    model_support_counts: Counter[str],
    skinning_mode_primitive_counts: Counter[str],
    primitive_bone_count_counts: Counter[str],
    skinning_mode_bone_count_counts: Counter[str],
    bone_count_counts: Counter[str],
    skeleton_metadata_counts: dict[str, Counter[str]],
    model_records: list[dict[str, object]],
    parse_errors: list[dict[str, object]],
    nonstatic_model_samples: list[dict[str, object]],
    sample_limit: int,
) -> None:
    cmb_counts["discovered"] += 1
    try:
        model = CmbModel.from_path(path)
    except Exception as exc:
        add_parse_error(parse_errors, path, actor_root, "cmb_model", str(exc))
        return
    add_model_record(
        model,
        path,
        actor_root,
        "cmb",
        cmb_counts,
        model_support_counts,
        skinning_mode_primitive_counts,
        primitive_bone_count_counts,
        skinning_mode_bone_count_counts,
        bone_count_counts,
        skeleton_metadata_counts,
        model_records,
        nonstatic_model_samples,
        sample_limit,
    )


def add_model_record(
    model: CmbModel,
    path: Path,
    actor_root: Path,
    container_type: str,
    cmb_counts: Counter[str],
    model_support_counts: Counter[str],
    skinning_mode_primitive_counts: Counter[str],
    primitive_bone_count_counts: Counter[str],
    skinning_mode_bone_count_counts: Counter[str],
    bone_count_counts: Counter[str],
    skeleton_metadata_counts: dict[str, Counter[str]],
    model_records: list[dict[str, object]],
    nonstatic_model_samples: list[dict[str, object]],
    sample_limit: int,
    *,
    embedded_file: ZarFile | None = None,
) -> None:
    static_candidate = model.is_static_candidate()
    cmb_counts["static_candidate" if static_candidate else "nonstatic_candidate"] += 1
    support_status = model_support_status(model)
    model_support_counts[support_status] += 1
    bone_count_counts[str(model.bone_count)] += 1
    skinning_modes = model_skinning_mode_counts(model)
    skinning_mode_primitive_counts.update(skinning_modes)
    primitive_bone_counts, skinning_mode_bone_counts = model_primitive_bone_counts(model)
    primitive_bone_count_counts.update(primitive_bone_counts)
    skinning_mode_bone_count_counts.update(skinning_mode_bone_counts)
    update_skeleton_metadata_counts(model, skeleton_metadata_counts)
    skeleton_summary = model.skeleton.summary()

    record = {
        "container_path": path.relative_to(actor_root).as_posix(),
        "container_type": container_type,
        "embedded_name": embedded_file.name if embedded_file is not None else None,
        "embedded_index": embedded_file.index if embedded_file is not None else None,
        "embedded_type": embedded_file.type_name if embedded_file is not None else None,
        "model_name": model.name,
        "static_candidate": static_candidate,
        "rigid_export_candidate": model.is_rigid_export_candidate(),
        "support_status": support_status,
        "bone_count": model.bone_count,
        "skeleton": skeleton_summary,
        "skinning_mode_primitive_counts": sorted_counter(skinning_modes),
        "primitive_bone_count_counts": sorted_counter(primitive_bone_counts),
        "mesh_count": len(model.meshes),
        "shape_count": len(model.shapes),
        "material_count": len(model.materials),
        "texture_count": len(model.textures),
    }
    model_records.append(record)
    if not static_candidate and len(nonstatic_model_samples) < sample_limit:
        nonstatic_model_samples.append(record)


def update_skeleton_metadata_counts(
    model: CmbModel,
    skeleton_metadata_counts: dict[str, Counter[str]],
) -> None:
    skeleton_summary = model.skeleton.summary()
    for field in SKELETON_METADATA_FIELDS:
        count_name = f"{field}_counts"
        skeleton_metadata_counts[count_name][str(skeleton_summary[field])] += 1


def model_skinning_mode_counts(model: CmbModel) -> Counter[str]:
    counts: Counter[str] = Counter()
    for shape in model.shapes:
        for primitive in shape.primitives:
            counts[str(primitive.skinning_mode)] += 1
    return counts


def model_primitive_bone_counts(model: CmbModel) -> tuple[Counter[str], Counter[str]]:
    primitive_bone_counts: Counter[str] = Counter()
    skinning_mode_bone_counts: Counter[str] = Counter()
    for shape in model.shapes:
        for primitive in shape.primitives:
            bone_count = len(primitive.bone_indices)
            primitive_bone_counts[str(bone_count)] += 1
            skinning_mode_bone_counts[
                f"mode={primitive.skinning_mode};bones={bone_count}"
            ] += 1
    return primitive_bone_counts, skinning_mode_bone_counts


def model_support_status(model: CmbModel) -> str:
    if model.is_static_candidate():
        return "static_cmb_export_supported"
    modes = {int(mode) for mode in model_skinning_mode_counts(model)}
    if modes == {0}:
        return "rigid_multibone_export_supported"
    if modes.issubset({0, 1}):
        return "needs_skinning_mode_1_support"
    if modes.issubset({0, 2}):
        return "needs_skinning_mode_2_support"
    if modes.issubset({0, 1, 2}):
        return "needs_skinning_mode_1_and_2_support"
    return "needs_unknown_skinning_mode_support"


def archive_support_status(
    parsed_cmb_count: int,
    nonstatic_cmb_count: int,
    known_animation_count: int,
    animation_like_count: int,
) -> str:
    if parsed_cmb_count == 0:
        return "no_parsed_cmb_model"
    if nonstatic_cmb_count > 0 and known_animation_count > 0:
        return "needs_skeleton_skinning_and_animation_support"
    if nonstatic_cmb_count > 0:
        return "needs_skeleton_or_skinning_support"
    if known_animation_count > 0:
        return "static_models_with_animation_data"
    if animation_like_count > 0:
        return "static_models_with_animation_like_data"
    return "static_payload_supported_by_current_exporter"


def add_animation_payload_metadata(
    archive: ZarArchive,
    file: ZarFile,
    path: Path,
    actor_root: Path,
    magic_counts: dict[str, Counter[str]],
    declared_size_match_counts: dict[str, Counter[str]],
    header_version_counts: dict[str, Counter[str]],
    header_field_counts: dict[str, dict[str, Counter[str]]],
    payload_sizes: dict[str, list[int]],
    payload_samples: list[dict[str, object]],
    csab_node_table_counts: Counter[str],
    csab_anod_record_counts: Counter[str],
    csab_anod_active_channel_count_counts: Counter[str],
    csab_anod_channel_slot_active_counts: Counter[str],
    csab_anod_record_field_04_high16_counts: Counter[str],
    csab_anod_channel_block_counts: Counter[str],
    csab_anod_channel_block_class_counts: Counter[str],
    csab_anod_channel_block_class_key_entry_counts: Counter[str],
    csab_anod_channel_block_key_count_counts: Counter[str],
    csab_anod_channel_block_slot_class_counts: Counter[str],
    sample_limit: int,
) -> None:
    data = archive.read_file(file)
    file_type = file.type_name
    magic = format_magic(data[:4])
    magic_counts.setdefault(file_type, Counter())[magic] += 1
    payload_sizes.setdefault(file_type, []).append(len(data))

    declared_size = animation_declared_size(file_type, data)
    if declared_size is None:
        declared_size_match_counts.setdefault(file_type, Counter())["not_applicable"] += 1
    elif declared_size == len(data):
        declared_size_match_counts.setdefault(file_type, Counter())["matches"] += 1
    else:
        declared_size_match_counts.setdefault(file_type, Counter())["mismatch"] += 1

    header_version = animation_header_version(file_type, data)
    if header_version is not None:
        header_version_counts.setdefault(file_type, Counter())[str(header_version)] += 1

    header_fields = animation_header_fields(file_type, data)
    for field_name, value in header_fields.items():
        header_field_counts.setdefault(file_type, {}).setdefault(field_name, Counter())[
            str(value)
        ] += 1
    header_candidates = csab_header_candidates(data) if file_type == "csab" else {}
    node_table = csab_node_table_metadata(data) if file_type == "csab" else {}
    if node_table:
        add_csab_node_table_counts(node_table, csab_node_table_counts)
    anod_records = csab_anod_record_metadata(data, node_table) if file_type == "csab" else {}
    if anod_records:
        add_csab_anod_record_counts(
            anod_records,
            csab_anod_record_counts,
            csab_anod_active_channel_count_counts,
            csab_anod_channel_slot_active_counts,
            csab_anod_record_field_04_high16_counts,
        )
    channel_blocks = csab_anod_channel_block_metadata(data, node_table) if file_type == "csab" else {}
    if channel_blocks:
        add_csab_anod_channel_block_counts(
            channel_blocks,
            csab_anod_channel_block_counts,
            csab_anod_channel_block_class_counts,
            csab_anod_channel_block_class_key_entry_counts,
            csab_anod_channel_block_key_count_counts,
            csab_anod_channel_block_slot_class_counts,
        )

    if len(payload_samples) < sample_limit:
        payload_samples.append(
            {
                "archive_path": path.relative_to(actor_root).as_posix(),
                "embedded_name": file.name,
                "type": file_type,
                "size": len(data),
                "magic": magic,
                "declared_size": declared_size,
                "header_version": header_version,
                "header_fields": header_fields,
                "header_candidates": header_candidates,
                "node_table": node_table,
                "anod_records": anod_records,
                "anod_channel_blocks": channel_blocks,
                "first_16_bytes": data[:16].hex(" "),
            }
        )


def animation_declared_size(file_type: str, data: bytes) -> int | None:
    if file_type == "csab" and len(data) >= 8:
        return int.from_bytes(data[4:8], "little")
    if file_type == "cmab" and len(data) >= 12:
        return int.from_bytes(data[8:12], "little")
    return None


def animation_header_version(file_type: str, data: bytes) -> int | None:
    if file_type == "csab" and len(data) >= 12:
        return int.from_bytes(data[8:12], "little")
    if file_type == "cmab" and len(data) >= 8:
        return int.from_bytes(data[4:8], "little")
    return None


def animation_header_fields(file_type: str, data: bytes) -> dict[str, int]:
    offsets = ANIMATION_HEADER_FIELD_OFFSETS.get(file_type)
    if offsets is None:
        return {}
    fields: dict[str, int] = {}
    for offset in offsets:
        if len(data) < offset + 4:
            continue
        fields[f"word_{offset:02x}"] = int.from_bytes(data[offset : offset + 4], "little")
    return fields


def csab_header_candidates(data: bytes) -> dict[str, int]:
    candidates: dict[str, int] = {}
    fields = {
        "frame_count_candidate": CSAB_FRAME_COUNT_CANDIDATE_OFFSET,
        "animated_bone_count_candidate": CSAB_ANIMATED_BONE_COUNT_CANDIDATE_OFFSET,
        "skeleton_bone_count_candidate": CSAB_SKELETON_BONE_COUNT_CANDIDATE_OFFSET,
    }
    for name, offset in fields.items():
        if len(data) >= offset + 4:
            candidates[name] = int.from_bytes(data[offset : offset + 4], "little")
    return candidates


def align4(value: int) -> int:
    return (value + 3) & ~3


def csab_node_table_metadata(data: bytes) -> dict[str, object]:
    candidates = csab_header_candidates(data)
    animated_bone_count = candidates.get("animated_bone_count_candidate")
    skeleton_bone_count = candidates.get("skeleton_bone_count_candidate")
    if animated_bone_count is None or skeleton_bone_count is None:
        return {
            "valid": False,
            "status": "missing_header_candidates",
        }

    bone_table_start = CSAB_BONE_TABLE_OFFSET
    bone_table_end = bone_table_start + skeleton_bone_count * 2
    node_offset_table_start = align4(bone_table_end)
    node_offset_table_end = node_offset_table_start + animated_bone_count * 4
    if node_offset_table_end > len(data):
        return {
            "valid": False,
            "status": "table_out_of_bounds",
            "bone_table_start": bone_table_start,
            "node_offset_table_start": node_offset_table_start,
            "node_offset_table_end": node_offset_table_end,
        }

    bone_to_node_indices = [
        int.from_bytes(
            data[bone_table_start + index * 2 : bone_table_start + index * 2 + 2],
            "little",
        )
        for index in range(skeleton_bone_count)
    ]
    node_offsets = [
        int.from_bytes(
            data[node_offset_table_start + index * 4 : node_offset_table_start + index * 4 + 4],
            "little",
        )
        for index in range(animated_bone_count)
    ]
    active_indices = [index for index in bone_to_node_indices if index != 0xFFFF]
    anod_offsets = [
        CSAB_NODE_OFFSET_BASE + offset
        for offset in node_offsets
        if 0 <= CSAB_NODE_OFFSET_BASE + offset <= len(data) - len(CSAB_NODE_MAGIC)
        and data[CSAB_NODE_OFFSET_BASE + offset : CSAB_NODE_OFFSET_BASE + offset + len(CSAB_NODE_MAGIC)]
        == CSAB_NODE_MAGIC
    ]
    valid = (
        sorted(active_indices) == list(range(animated_bone_count))
        and len(anod_offsets) == animated_bone_count
        and all(later > earlier for earlier, later in zip(node_offsets, node_offsets[1:]))
    )
    status = "valid" if valid else "invalid"
    return {
        "valid": valid,
        "status": status,
        "bone_table_start": bone_table_start,
        "node_offset_table_start": node_offset_table_start,
        "node_offset_table_end": node_offset_table_end,
        "skeleton_bone_table_entries": skeleton_bone_count,
        "animated_node_offsets": animated_bone_count,
        "unused_skeleton_bone_entries": bone_to_node_indices.count(0xFFFF),
        "anod_node_offsets": len(anod_offsets),
        "bone_to_node_indices_are_contiguous": sorted(active_indices)
        == list(range(animated_bone_count)),
        "node_offsets_point_to_anod": len(anod_offsets) == animated_bone_count,
        "node_offsets_strictly_increasing": all(
            later > earlier for earlier, later in zip(node_offsets, node_offsets[1:])
        ),
    }


def add_csab_node_table_counts(
    node_table: dict[str, object],
    counts: Counter[str],
) -> None:
    counts["valid_node_tables" if node_table.get("valid") else "invalid_node_tables"] += 1
    for key in (
        "skeleton_bone_table_entries",
        "animated_node_offsets",
        "unused_skeleton_bone_entries",
    ):
        value = node_table.get(key)
        if isinstance(value, int):
            counts[key] += value


def csab_node_table_arrays(data: bytes) -> tuple[list[int], list[int]] | None:
    candidates = csab_header_candidates(data)
    animated_bone_count = candidates.get("animated_bone_count_candidate")
    skeleton_bone_count = candidates.get("skeleton_bone_count_candidate")
    if animated_bone_count is None or skeleton_bone_count is None:
        return None

    bone_table_start = CSAB_BONE_TABLE_OFFSET
    bone_table_end = bone_table_start + skeleton_bone_count * 2
    node_offset_table_start = align4(bone_table_end)
    node_offset_table_end = node_offset_table_start + animated_bone_count * 4
    if node_offset_table_end > len(data):
        return None

    bone_to_node_indices = [
        int.from_bytes(data[bone_table_start + index * 2 : bone_table_start + index * 2 + 2], "little")
        for index in range(skeleton_bone_count)
    ]
    node_offsets = [
        int.from_bytes(
            data[node_offset_table_start + index * 4 : node_offset_table_start + index * 4 + 4],
            "little",
        )
        for index in range(animated_bone_count)
    ]
    return bone_to_node_indices, node_offsets


def csab_anod_record_metadata(
    data: bytes,
    node_table: dict[str, object] | None = None,
) -> dict[str, object]:
    if node_table is None:
        node_table = csab_node_table_metadata(data)
    if not node_table.get("valid"):
        return {
            "valid": False,
            "status": "invalid_node_table",
        }

    table_arrays = csab_node_table_arrays(data)
    if table_arrays is None:
        return {
            "valid": False,
            "status": "missing_node_table_arrays",
        }

    bone_to_node_indices, node_offsets = table_arrays
    node_to_bone_indices = {
        node_index: bone_index
        for bone_index, node_index in enumerate(bone_to_node_indices)
        if node_index != 0xFFFF
    }
    record_count = len(node_offsets)
    record_spans_valid = True
    record_spans_have_min_header = True
    record_magic_matches = True
    record_low16_bone_indices_match_bone_table = True
    record_low16_bone_indices_in_skeleton_range = True
    record_low16_bone_indices_unique = True
    record_low16_bone_indices_sorted = True
    record_field_04_high16_values_are_0_or_1 = True
    channel_offsets_after_header = True
    channel_offsets_in_record_span = True
    channel_offsets_aligned = True
    channel_offsets_strictly_increasing = True
    channel_offsets_unique_per_record = True
    first_active_channel_at_0x1c = True
    channel_offset_entries = 0
    active_channel_offsets = 0
    records_without_active_channel_offsets = 0
    previous_bone_index = -1
    seen_bone_indices: set[int] = set()
    active_channel_count_counts: Counter[str] = Counter()
    channel_slot_active_counts: Counter[str] = Counter()
    field_04_high16_counts: Counter[str] = Counter()

    for record_index, relative_offset in enumerate(node_offsets):
        record_start = CSAB_NODE_OFFSET_BASE + relative_offset
        next_record_start = (
            CSAB_NODE_OFFSET_BASE + node_offsets[record_index + 1]
            if record_index + 1 < record_count
            else len(data)
        )
        record_span = next_record_start - record_start
        has_header = (
            0 <= record_start
            and record_start < next_record_start <= len(data)
            and record_span >= CSAB_ANOD_HEADER_SIZE
        )
        if not has_header:
            record_spans_valid = False
            record_spans_have_min_header = False
            continue

        if data[record_start : record_start + len(CSAB_NODE_MAGIC)] != CSAB_NODE_MAGIC:
            record_magic_matches = False

        field_04 = int.from_bytes(data[record_start + 4 : record_start + 8], "little")
        bone_index = field_04 & 0xFFFF
        high16 = field_04 >> 16
        field_04_high16_counts[str(high16)] += 1
        if high16 not in (0, 1):
            record_field_04_high16_values_are_0_or_1 = False
        if bone_index != node_to_bone_indices.get(record_index):
            record_low16_bone_indices_match_bone_table = False
        if bone_index >= len(bone_to_node_indices):
            record_low16_bone_indices_in_skeleton_range = False
        if bone_index in seen_bone_indices:
            record_low16_bone_indices_unique = False
        seen_bone_indices.add(bone_index)
        if bone_index < previous_bone_index:
            record_low16_bone_indices_sorted = False
        previous_bone_index = bone_index

        raw_channel_offsets = data[
            record_start
            + CSAB_ANOD_CHANNEL_OFFSET_TABLE_OFFSET : record_start
            + CSAB_ANOD_CHANNEL_OFFSET_TABLE_OFFSET
            + CSAB_ANOD_CHANNEL_OFFSET_COUNT * 2
        ]
        channel_offsets = [
            int.from_bytes(raw_channel_offsets[index : index + 2], "little")
            for index in range(0, len(raw_channel_offsets), 2)
        ]
        active_offsets = [offset for offset in channel_offsets if offset != 0]
        channel_offset_entries += len(channel_offsets)
        active_channel_offsets += len(active_offsets)
        active_channel_count_counts[str(len(active_offsets))] += 1
        if not active_offsets:
            records_without_active_channel_offsets += 1

        for slot, offset in enumerate(channel_offsets):
            if offset != 0:
                channel_slot_active_counts[str(slot)] += 1

        if active_offsets:
            if active_offsets[0] != CSAB_ANOD_HEADER_SIZE:
                first_active_channel_at_0x1c = False
            if any(
                later <= earlier
                for earlier, later in zip(active_offsets, active_offsets[1:])
            ):
                channel_offsets_strictly_increasing = False
            if len(set(active_offsets)) != len(active_offsets):
                channel_offsets_unique_per_record = False
            if any(offset < CSAB_ANOD_HEADER_SIZE for offset in active_offsets):
                channel_offsets_after_header = False
            if any(offset >= record_span for offset in active_offsets):
                channel_offsets_in_record_span = False
            if any(offset % 4 != 0 for offset in active_offsets):
                channel_offsets_aligned = False

    channel_offsets_valid = (
        channel_offsets_after_header
        and channel_offsets_in_record_span
        and channel_offsets_aligned
        and channel_offsets_strictly_increasing
        and channel_offsets_unique_per_record
        and first_active_channel_at_0x1c
    )
    valid = (
        record_spans_valid
        and record_spans_have_min_header
        and record_magic_matches
        and record_low16_bone_indices_match_bone_table
        and record_low16_bone_indices_in_skeleton_range
        and record_low16_bone_indices_unique
        and record_low16_bone_indices_sorted
        and record_field_04_high16_values_are_0_or_1
        and channel_offsets_valid
    )
    return {
        "valid": valid,
        "status": "valid" if valid else "invalid",
        "anod_records": record_count,
        "channel_offset_entries": channel_offset_entries,
        "active_channel_offsets": active_channel_offsets,
        "zero_channel_offsets": channel_offset_entries - active_channel_offsets,
        "records_without_active_channel_offsets": records_without_active_channel_offsets,
        "record_spans_valid": record_spans_valid,
        "record_spans_have_min_header": record_spans_have_min_header,
        "record_magic_matches": record_magic_matches,
        "record_low16_bone_indices_match_bone_table": record_low16_bone_indices_match_bone_table,
        "record_low16_bone_indices_in_skeleton_range": record_low16_bone_indices_in_skeleton_range,
        "record_low16_bone_indices_unique": record_low16_bone_indices_unique,
        "record_low16_bone_indices_sorted": record_low16_bone_indices_sorted,
        "record_field_04_high16_values_are_0_or_1": record_field_04_high16_values_are_0_or_1,
        "channel_offsets_valid": channel_offsets_valid,
        "channel_offsets_after_header": channel_offsets_after_header,
        "channel_offsets_in_record_span": channel_offsets_in_record_span,
        "channel_offsets_aligned": channel_offsets_aligned,
        "channel_offsets_strictly_increasing": channel_offsets_strictly_increasing,
        "channel_offsets_unique_per_record": channel_offsets_unique_per_record,
        "first_active_channel_at_0x1c": first_active_channel_at_0x1c,
        "active_channel_count_counts": sorted_counter(active_channel_count_counts),
        "channel_slot_active_counts": sorted_counter(channel_slot_active_counts),
        "field_04_high16_counts": sorted_counter(field_04_high16_counts),
    }


def add_csab_anod_record_counts(
    anod_records: dict[str, object],
    counts: Counter[str],
    active_channel_count_counts: Counter[str],
    channel_slot_active_counts: Counter[str],
    field_04_high16_counts: Counter[str],
) -> None:
    counts["valid_record_sets" if anod_records.get("valid") else "invalid_record_sets"] += 1
    for key in (
        "anod_records",
        "channel_offset_entries",
        "active_channel_offsets",
        "zero_channel_offsets",
        "records_without_active_channel_offsets",
    ):
        value = anod_records.get(key)
        if isinstance(value, int):
            counts[key] += value
    for key in (
        "record_spans_valid",
        "record_spans_have_min_header",
        "record_magic_matches",
        "record_low16_bone_indices_match_bone_table",
        "record_low16_bone_indices_in_skeleton_range",
        "record_low16_bone_indices_unique",
        "record_low16_bone_indices_sorted",
        "record_field_04_high16_values_are_0_or_1",
        "channel_offsets_valid",
        "channel_offsets_after_header",
        "channel_offsets_in_record_span",
        "channel_offsets_aligned",
        "channel_offsets_strictly_increasing",
        "channel_offsets_unique_per_record",
        "first_active_channel_at_0x1c",
    ):
        if anod_records.get(key):
            counts[f"payloads_{key}"] += 1

    active_channel_count_counts.update(
        counter_from_mapping(anod_records.get("active_channel_count_counts"))
    )
    channel_slot_active_counts.update(
        counter_from_mapping(anod_records.get("channel_slot_active_counts"))
    )
    field_04_high16_counts.update(
        counter_from_mapping(anod_records.get("field_04_high16_counts"))
    )


def csab_anod_channel_block_metadata(
    data: bytes,
    node_table: dict[str, object] | None = None,
) -> dict[str, object]:
    if node_table is None:
        node_table = csab_node_table_metadata(data)
    if not node_table.get("valid"):
        return {
            "valid": False,
            "status": "invalid_node_table",
        }

    table_arrays = csab_node_table_arrays(data)
    if table_arrays is None:
        return {
            "valid": False,
            "status": "missing_node_table_arrays",
        }

    header_candidates = csab_header_candidates(data)
    frame_count = header_candidates.get("frame_count_candidate")
    _bone_to_node_indices, node_offsets = table_arrays
    channel_blocks = 0
    channel_block_key_entries = 0
    block_classes_valid = True
    block_types_known = True
    block_key_counts_positive = True
    type2_frame_headers_match_csab = True
    type2_key_frames_valid = True
    f32_values_finite = True
    class_counts: Counter[str] = Counter()
    class_key_entry_counts: Counter[str] = Counter()
    key_count_counts: Counter[str] = Counter()
    slot_class_counts: Counter[str] = Counter()

    for record_index, relative_offset in enumerate(node_offsets):
        record_start = CSAB_NODE_OFFSET_BASE + relative_offset
        next_record_start = (
            CSAB_NODE_OFFSET_BASE + node_offsets[record_index + 1]
            if record_index + 1 < len(node_offsets)
            else len(data)
        )
        if not (0 <= record_start < next_record_start <= len(data)):
            block_classes_valid = False
            continue

        active_offsets = csab_anod_active_channel_offsets(data, record_start)
        for active_index, (slot, channel_offset) in enumerate(active_offsets):
            block_start = record_start + channel_offset
            block_end = (
                record_start + active_offsets[active_index + 1][1]
                if active_index + 1 < len(active_offsets)
                else next_record_start
            )
            block_class, key_count = csab_anod_channel_block_class(
                data, block_start, block_end
            )
            channel_blocks += 1
            channel_block_key_entries += key_count
            class_counts[block_class] += 1
            class_key_entry_counts[block_class] += key_count
            key_count_counts[str(key_count)] += 1
            slot_class_counts[f"slot={slot};class={block_class}"] += 1

            if block_class.startswith("unknown") or block_class == "short":
                block_classes_valid = False
            block_type = (
                int.from_bytes(data[block_start : block_start + 4], "little")
                if block_end - block_start >= 4
                else None
            )
            if block_type not in (1, 2):
                block_types_known = False
            if key_count <= 0:
                block_key_counts_positive = False

            validation = csab_anod_validate_channel_block(
                data,
                block_start,
                block_end,
                block_class,
                frame_count,
            )
            if not validation["type2_frame_headers_match_csab"]:
                type2_frame_headers_match_csab = False
            if not validation["type2_key_frames_valid"]:
                type2_key_frames_valid = False
            if not validation["f32_values_finite"]:
                f32_values_finite = False

    valid = (
        block_classes_valid
        and block_types_known
        and block_key_counts_positive
        and type2_frame_headers_match_csab
        and type2_key_frames_valid
        and f32_values_finite
    )
    return {
        "valid": valid,
        "status": "valid" if valid else "invalid",
        "channel_blocks": channel_blocks,
        "channel_block_key_entries": channel_block_key_entries,
        "block_classes_valid": block_classes_valid,
        "block_types_known": block_types_known,
        "block_key_counts_positive": block_key_counts_positive,
        "type2_frame_headers_match_csab": type2_frame_headers_match_csab,
        "type2_key_frames_valid": type2_key_frames_valid,
        "f32_values_finite": f32_values_finite,
        "class_counts": sorted_counter(class_counts),
        "class_key_entry_counts": sorted_counter(class_key_entry_counts),
        "key_count_counts": sorted_counter(key_count_counts),
        "slot_class_counts": sorted_counter(slot_class_counts),
    }


def csab_anod_active_channel_offsets(data: bytes, record_start: int) -> list[tuple[int, int]]:
    raw_channel_offsets = data[
        record_start
        + CSAB_ANOD_CHANNEL_OFFSET_TABLE_OFFSET : record_start
        + CSAB_ANOD_CHANNEL_OFFSET_TABLE_OFFSET
        + CSAB_ANOD_CHANNEL_OFFSET_COUNT * 2
    ]
    offsets = [
        int.from_bytes(raw_channel_offsets[index : index + 2], "little")
        for index in range(0, len(raw_channel_offsets), 2)
    ]
    return [(slot, offset) for slot, offset in enumerate(offsets) if offset != 0]


def csab_anod_channel_block_class(
    data: bytes,
    block_start: int,
    block_end: int,
) -> tuple[str, int]:
    block_size = block_end - block_start
    if block_size < CSAB_ANOD_CHANNEL_BLOCK_HEADER_SIZE:
        return "short", 0

    block_type = int.from_bytes(data[block_start : block_start + 4], "little")
    key_count = int.from_bytes(data[block_start + 4 : block_start + 8], "little")
    if block_type == 1 and key_count == 1 and block_size == 0x18:
        return "type1_f32_const_len24", key_count
    if block_type == 1 and key_count == 1 and block_size == 0x14:
        return "type1_s16_const_len20", key_count
    if block_type == 2 and block_size == CSAB_ANOD_CHANNEL_BLOCK_KEY_HEADER_SIZE + key_count * CSAB_ANOD_F32_KEY_SIZE:
        return "type2_f32_keys_len16_plus_count16", key_count
    if block_type == 2 and block_size == CSAB_ANOD_CHANNEL_BLOCK_KEY_HEADER_SIZE + key_count * CSAB_ANOD_S16_KEY_SIZE:
        return "type2_s16_keys_len16_plus_count8", key_count
    return f"unknown_type={block_type};len={block_size};count={key_count}", key_count


def csab_anod_validate_channel_block(
    data: bytes,
    block_start: int,
    block_end: int,
    block_class: str,
    frame_count: int | None,
) -> dict[str, bool]:
    block_size = block_end - block_start
    if block_size < CSAB_ANOD_CHANNEL_BLOCK_HEADER_SIZE:
        return {
            "type2_frame_headers_match_csab": False,
            "type2_key_frames_valid": False,
            "f32_values_finite": False,
        }

    block_type = int.from_bytes(data[block_start : block_start + 4], "little")
    key_count = int.from_bytes(data[block_start + 4 : block_start + 8], "little")
    type2_frame_headers_match_csab = True
    type2_key_frames_valid = True
    f32_values_finite = True

    if "f32" in block_class:
        if block_type == 1:
            f32_offsets = (0x0C, 0x10, 0x14)
        elif block_type == 2:
            f32_offsets = tuple(
                key_offset + value_offset
                for key_offset in range(
                    CSAB_ANOD_CHANNEL_BLOCK_KEY_HEADER_SIZE,
                    block_size,
                    CSAB_ANOD_F32_KEY_SIZE,
                )
                for value_offset in (0x04, 0x08, 0x0C)
            )
        else:
            f32_offsets = ()
        f32_values_finite = all(
            offset + 4 <= block_size
            and math.isfinite(struct.unpack_from("<f", data, block_start + offset)[0])
            for offset in f32_offsets
        )

    if block_type == 2:
        if block_size < CSAB_ANOD_CHANNEL_BLOCK_KEY_HEADER_SIZE:
            return {
                "type2_frame_headers_match_csab": False,
                "type2_key_frames_valid": False,
                "f32_values_finite": f32_values_finite,
            }
        start_frame = int.from_bytes(data[block_start + 8 : block_start + 12], "little")
        end_frame = int.from_bytes(data[block_start + 12 : block_start + 16], "little")
        if start_frame != 0 or (frame_count is not None and end_frame != frame_count):
            type2_frame_headers_match_csab = False

        if "f32" in block_class:
            key_size = CSAB_ANOD_F32_KEY_SIZE
            frame_size = 4
        elif "s16" in block_class:
            key_size = CSAB_ANOD_S16_KEY_SIZE
            frame_size = 2
        else:
            key_size = 0
            frame_size = 0

        if key_size == 0:
            type2_key_frames_valid = False
        else:
            frames = [
                int.from_bytes(
                    data[
                        block_start
                        + CSAB_ANOD_CHANNEL_BLOCK_KEY_HEADER_SIZE
                        + index * key_size : block_start
                        + CSAB_ANOD_CHANNEL_BLOCK_KEY_HEADER_SIZE
                        + index * key_size
                        + frame_size
                    ],
                    "little",
                )
                for index in range(key_count)
            ]
            if not frames or frames[0] != 0:
                type2_key_frames_valid = False
            if any(later <= earlier for earlier, later in zip(frames, frames[1:])):
                type2_key_frames_valid = False
            if frame_count is not None and frames and frames[-1] > frame_count:
                type2_key_frames_valid = False

    return {
        "type2_frame_headers_match_csab": type2_frame_headers_match_csab,
        "type2_key_frames_valid": type2_key_frames_valid,
        "f32_values_finite": f32_values_finite,
    }


def add_csab_anod_channel_block_counts(
    channel_blocks: dict[str, object],
    counts: Counter[str],
    class_counts: Counter[str],
    class_key_entry_counts: Counter[str],
    key_count_counts: Counter[str],
    slot_class_counts: Counter[str],
) -> None:
    counts["valid_block_sets" if channel_blocks.get("valid") else "invalid_block_sets"] += 1
    for key in (
        "channel_blocks",
        "channel_block_key_entries",
    ):
        value = channel_blocks.get(key)
        if isinstance(value, int):
            counts[key] += value
    for key in (
        "block_classes_valid",
        "block_types_known",
        "block_key_counts_positive",
        "type2_frame_headers_match_csab",
        "type2_key_frames_valid",
        "f32_values_finite",
    ):
        if channel_blocks.get(key):
            counts[f"payloads_{key}"] += 1

    class_counts.update(counter_from_mapping(channel_blocks.get("class_counts")))
    class_key_entry_counts.update(
        counter_from_mapping(channel_blocks.get("class_key_entry_counts"))
    )
    key_count_counts.update(counter_from_mapping(channel_blocks.get("key_count_counts")))
    slot_class_counts.update(counter_from_mapping(channel_blocks.get("slot_class_counts")))


def csab_playback_f32_sampler_metadata(
    data: bytes,
    node_table: dict[str, object] | None = None,
) -> dict[str, object]:
    if node_table is None:
        node_table = csab_node_table_metadata(data)
    if not node_table.get("valid"):
        return {
            "valid": False,
            "status": "invalid_node_table",
        }

    table_arrays = csab_node_table_arrays(data)
    header_candidates = csab_header_candidates(data)
    frame_count = header_candidates.get("frame_count_candidate")
    if table_arrays is None or frame_count is None:
        return {
            "valid": False,
            "status": "missing_sampling_metadata",
        }

    _bone_to_node_indices, node_offsets = table_arrays
    frame_slots = frame_count + 1
    sampled_channel_blocks = 0
    sampled_channel_values = 0
    finite_sampled_channel_values = 0
    const_f32_channel_blocks = 0
    keyed_f32_channel_blocks = 0
    non_f32_channel_blocks = 0
    slot_sample_counts: Counter[str] = Counter()
    value_range_by_slot: dict[str, list[float]] = {}

    for record_index, relative_offset in enumerate(node_offsets):
        record_start = CSAB_NODE_OFFSET_BASE + relative_offset
        next_record_start = (
            CSAB_NODE_OFFSET_BASE + node_offsets[record_index + 1]
            if record_index + 1 < len(node_offsets)
            else len(data)
        )
        if not (0 <= record_start < next_record_start <= len(data)):
            return {
                "valid": False,
                "status": "invalid_record_span",
            }

        active_offsets = csab_anod_active_channel_offsets(data, record_start)
        for active_index, (slot, channel_offset) in enumerate(active_offsets):
            block_start = record_start + channel_offset
            block_end = (
                record_start + active_offsets[active_index + 1][1]
                if active_index + 1 < len(active_offsets)
                else next_record_start
            )
            block_class, key_count = csab_anod_channel_block_class(
                data, block_start, block_end
            )
            if block_class == "type1_f32_const_len24":
                const_f32_channel_blocks += 1
                sampled_channel_blocks += 1
                values = [
                    csab_f32_constant_channel_value(data, block_start)
                    for _frame in range(frame_slots)
                ]
            elif block_class == "type2_f32_keys_len16_plus_count16":
                keyed_f32_channel_blocks += 1
                sampled_channel_blocks += 1
                keys = csab_f32_channel_keys(data, block_start, key_count)
                values = [
                    csab_sample_f32_keyed_channel(keys, float(frame))
                    for frame in range(frame_slots)
                ]
            else:
                non_f32_channel_blocks += 1
                continue

            slot_key = str(slot)
            sampled_channel_values += len(values)
            slot_sample_counts[slot_key] += len(values)
            for value in values:
                if math.isfinite(value):
                    finite_sampled_channel_values += 1
                current_range = value_range_by_slot.get(slot_key)
                if current_range is None:
                    value_range_by_slot[slot_key] = [value, value]
                else:
                    current_range[0] = min(current_range[0], value)
                    current_range[1] = max(current_range[1], value)

    valid = (
        non_f32_channel_blocks == 0
        and sampled_channel_values == finite_sampled_channel_values
    )
    return {
        "valid": valid,
        "status": "valid" if valid else "invalid",
        "sampler": "f32_hermite_candidate_integer_frames",
        "frame_count_candidate": frame_count,
        "sampled_payload_frame_slots": frame_slots,
        "sampled_channel_blocks": sampled_channel_blocks,
        "sampled_channel_values": sampled_channel_values,
        "finite_sampled_channel_values": finite_sampled_channel_values,
        "const_f32_channel_blocks": const_f32_channel_blocks,
        "keyed_f32_channel_blocks": keyed_f32_channel_blocks,
        "non_f32_channel_blocks": non_f32_channel_blocks,
        "slot_sample_counts": sorted_counter(slot_sample_counts),
        "value_range_by_slot": {
            slot: {
                "min": round(bounds[0], 6),
                "max": round(bounds[1], 6),
            }
            for slot, bounds in sorted(
                value_range_by_slot.items(),
                key=lambda item: int(item[0]),
            )
        },
    }


def csab_f32_constant_channel_value(data: bytes, block_start: int) -> float:
    return struct.unpack_from("<f", data, block_start + 0x14)[0]


def csab_f32_channel_keys(
    data: bytes,
    block_start: int,
    key_count: int,
) -> list[tuple[int, float, float, float]]:
    keys: list[tuple[int, float, float, float]] = []
    for index in range(key_count):
        offset = (
            block_start
            + CSAB_ANOD_CHANNEL_BLOCK_KEY_HEADER_SIZE
            + index * CSAB_ANOD_F32_KEY_SIZE
        )
        keys.append(
            (
                int.from_bytes(data[offset : offset + 4], "little"),
                struct.unpack_from("<f", data, offset + 4)[0],
                struct.unpack_from("<f", data, offset + 8)[0],
                struct.unpack_from("<f", data, offset + 12)[0],
            )
        )
    return keys


def csab_s16_rotation_value(raw_value: int) -> float:
    return raw_value * CSAB_ANOD_S16_ROTATION_SCALE


def csab_s16_constant_rotation_value(data: bytes, block_start: int) -> float:
    return csab_s16_rotation_value(struct.unpack_from("<h", data, block_start + 0x12)[0])


def csab_s16_rotation_channel_keys(
    data: bytes,
    block_start: int,
    key_count: int,
) -> list[tuple[int, float, float, float]]:
    keys: list[tuple[int, float, float, float]] = []
    for index in range(key_count):
        offset = (
            block_start
            + CSAB_ANOD_CHANNEL_BLOCK_KEY_HEADER_SIZE
            + index * CSAB_ANOD_S16_KEY_SIZE
        )
        keys.append(
            (
                int.from_bytes(data[offset : offset + 2], "little"),
                csab_s16_rotation_value(struct.unpack_from("<h", data, offset + 2)[0]),
                csab_s16_rotation_value(struct.unpack_from("<h", data, offset + 4)[0]),
                csab_s16_rotation_value(struct.unpack_from("<h", data, offset + 6)[0]),
            )
        )
    return keys


def csab_sample_f32_keyed_channel(
    keys: list[tuple[int, float, float, float]],
    frame: float,
) -> float:
    if not keys:
        return math.nan
    if frame <= keys[0][0]:
        return keys[0][1]
    if frame >= keys[-1][0]:
        return keys[-1][1]

    for left, right in zip(keys, keys[1:]):
        if frame < right[0]:
            interval = float(right[0] - left[0])
            if interval <= 0.0:
                return left[1]
            t = (frame - float(left[0])) / interval
            return csab_cubic_hermite(
                t,
                interval * CSAB_ANOD_HERMITE_INTERVAL_SCALE,
                left[1],
                right[1],
                left[3],
                right[2],
            )
    return keys[-1][1]


def csab_cubic_hermite(
    t: float,
    interval: float,
    y0: float,
    y1: float,
    m0: float,
    m1: float,
) -> float:
    t2 = t * t
    t3 = t2 * t
    return (
        (2.0 * t3 - 3.0 * t2 + 1.0) * y0
        + (3.0 * t2 - 2.0 * t3) * y1
        + (t3 - 2.0 * t2 + t) * m0 * interval
        + (t3 - t2) * m1 * interval
    )


def add_csab_playback_f32_sampler_counts(
    sampler: dict[str, object],
    counts: Counter[str],
    frame_count_counts: Counter[str],
    slot_sample_counts: Counter[str],
) -> None:
    counts[
        "valid_sample_sets" if sampler.get("valid") else "invalid_sample_sets"
    ] += 1
    for key in (
        "sampled_payload_frame_slots",
        "sampled_channel_blocks",
        "sampled_channel_values",
        "finite_sampled_channel_values",
        "const_f32_channel_blocks",
        "keyed_f32_channel_blocks",
        "non_f32_channel_blocks",
    ):
        value = sampler.get(key)
        if isinstance(value, int):
            counts[key] += value
    frame_count = sampler.get("frame_count_candidate")
    if isinstance(frame_count, int):
        frame_count_counts[str(frame_count)] += 1
    slot_sample_counts.update(counter_from_mapping(sampler.get("slot_sample_counts")))


def csab_playback_pose_sample_metadata(
    data: bytes,
    target_model: CmbModel | None,
    node_table: dict[str, object] | None = None,
    *,
    sample_frames: list[int] | None = None,
    sampler_name: str = "trs_candidate_endpoint_world_matrices",
) -> dict[str, object]:
    if target_model is None:
        return {
            "valid": False,
            "status": "missing_target_model",
        }
    if node_table is None:
        node_table = csab_node_table_metadata(data)
    if not node_table.get("valid"):
        return {
            "valid": False,
            "status": "invalid_node_table",
        }

    table_arrays = csab_node_table_arrays(data)
    header_candidates = csab_header_candidates(data)
    frame_count = header_candidates.get("frame_count_candidate")
    skeleton_bone_count = header_candidates.get("skeleton_bone_count_candidate")
    if table_arrays is None or frame_count is None or skeleton_bone_count is None:
        return {
            "valid": False,
            "status": "missing_pose_metadata",
        }
    if target_model.bone_count != skeleton_bone_count:
        return {
            "valid": False,
            "status": "target_bone_count_mismatch",
            "target_bone_count": target_model.bone_count,
            "skeleton_bone_count_candidate": skeleton_bone_count,
        }

    bone_to_node_indices, node_offsets = table_arrays
    if sample_frames is None:
        sample_frames = sorted({0, frame_count})
    else:
        sample_frames = sorted(set(sample_frames))
    sampled_skeleton_bone_transforms = 0
    sampled_animated_bone_transforms = 0
    sampled_channel_values = 0
    finite_channel_values = 0
    world_matrix_entries = 0
    finite_world_matrix_entries = 0
    non_f32_channel_blocks = 0
    slot_sample_counts: Counter[str] = Counter()
    world_translation_range_by_axis: dict[str, list[float]] = {}

    for frame in sample_frames:
        channel_values_by_node, frame_metadata = csab_channel_values_by_node(
            data,
            node_offsets,
            float(frame),
        )
        sampled_channel_values += frame_metadata["sampled_channel_values"]
        finite_channel_values += frame_metadata["finite_channel_values"]
        non_f32_channel_blocks += frame_metadata["non_f32_channel_blocks"]
        slot_sample_counts.update(frame_metadata["slot_sample_counts"])

        local_transforms: list[Matrix4] = []
        for bone_index, bone in enumerate(target_model.skeleton.bones):
            sampled_skeleton_bone_transforms += 1
            node_index = bone_to_node_indices[bone_index]
            values = (
                channel_values_by_node.get(node_index, {})
                if node_index != 0xFFFF
                else {}
            )
            if node_index != 0xFFFF:
                sampled_animated_bone_transforms += 1
            pose_bone = csab_pose_bone_from_channel_values(bone, values)
            local_transforms.append(bone_local_transform(pose_bone))

        world_transforms = compose_world_transforms(
            target_model.skeleton,
            tuple(local_transforms),
        )
        for transform in world_transforms:
            for row in transform:
                for value in row:
                    world_matrix_entries += 1
                    if math.isfinite(value):
                        finite_world_matrix_entries += 1
            add_axis_range(world_translation_range_by_axis, "x", transform[0][3])
            add_axis_range(world_translation_range_by_axis, "y", transform[1][3])
            add_axis_range(world_translation_range_by_axis, "z", transform[2][3])

    valid = (
        non_f32_channel_blocks == 0
        and sampled_channel_values == finite_channel_values
        and world_matrix_entries == finite_world_matrix_entries
    )
    return {
        "valid": valid,
        "status": "valid" if valid else "invalid",
        "sampler": sampler_name,
        "sampled_pose_frame_indices": sample_frames,
        "sampled_pose_frames": len(sample_frames),
        "target_bone_count": target_model.bone_count,
        "sampled_skeleton_bone_transforms": sampled_skeleton_bone_transforms,
        "sampled_animated_bone_transforms": sampled_animated_bone_transforms,
        "sampled_channel_values": sampled_channel_values,
        "finite_channel_values": finite_channel_values,
        "non_f32_channel_blocks": non_f32_channel_blocks,
        "world_matrix_entries": world_matrix_entries,
        "finite_world_matrix_entries": finite_world_matrix_entries,
        "slot_sample_counts": sorted_counter(slot_sample_counts),
        "world_translation_range_by_axis": rounded_range_mapping(
            world_translation_range_by_axis
        ),
    }


def csab_channel_values_by_node(
    data: bytes,
    node_offsets: list[int],
    frame: float,
) -> tuple[dict[int, dict[int, float]], dict[str, object]]:
    channel_values_by_node: dict[int, dict[int, float]] = {}
    sampled_channel_values = 0
    finite_channel_values = 0
    non_f32_channel_blocks = 0
    slot_sample_counts: Counter[str] = Counter()

    for record_index, relative_offset in enumerate(node_offsets):
        record_start = CSAB_NODE_OFFSET_BASE + relative_offset
        next_record_start = (
            CSAB_NODE_OFFSET_BASE + node_offsets[record_index + 1]
            if record_index + 1 < len(node_offsets)
            else len(data)
        )
        active_offsets = csab_anod_active_channel_offsets(data, record_start)
        for active_index, (slot, channel_offset) in enumerate(active_offsets):
            block_start = record_start + channel_offset
            block_end = (
                record_start + active_offsets[active_index + 1][1]
                if active_index + 1 < len(active_offsets)
                else next_record_start
            )
            value = csab_sample_channel_block(
                data,
                block_start,
                block_end,
                frame,
                slot,
            )
            if value is None:
                non_f32_channel_blocks += 1
                continue
            channel_values_by_node.setdefault(record_index, {})[slot] = value
            sampled_channel_values += 1
            slot_sample_counts[str(slot)] += 1
            if math.isfinite(value):
                finite_channel_values += 1

    return channel_values_by_node, {
        "sampled_channel_values": sampled_channel_values,
        "finite_channel_values": finite_channel_values,
        "non_f32_channel_blocks": non_f32_channel_blocks,
        "slot_sample_counts": slot_sample_counts,
    }


def csab_f32_channel_values_by_node(
    data: bytes,
    node_offsets: list[int],
    frame: float,
) -> tuple[dict[int, dict[int, float]], dict[str, object]]:
    return csab_channel_values_by_node(data, node_offsets, frame)


def csab_sample_channel_block(
    data: bytes,
    block_start: int,
    block_end: int,
    frame: float,
    slot: int,
) -> float | None:
    block_class, key_count = csab_anod_channel_block_class(data, block_start, block_end)
    if block_class == "type1_f32_const_len24":
        return csab_f32_constant_channel_value(data, block_start)
    if block_class == "type2_f32_keys_len16_plus_count16":
        keys = csab_f32_channel_keys(data, block_start, key_count)
        return (
            csab_sample_rotation_keyed_channel(keys, frame)
            if slot in CSAB_ROTATION_CHANNEL_SLOTS
            else csab_sample_f32_keyed_channel(keys, frame)
        )
    if block_class == "type1_s16_const_len20" and slot in CSAB_ROTATION_CHANNEL_SLOTS:
        return csab_s16_constant_rotation_value(data, block_start)
    if block_class == "type2_s16_keys_len16_plus_count8" and slot in CSAB_ROTATION_CHANNEL_SLOTS:
        return csab_sample_rotation_keyed_channel(
            csab_s16_rotation_channel_keys(data, block_start, key_count),
            frame,
        )
    return None


def csab_sample_f32_channel_block(
    data: bytes,
    block_start: int,
    block_end: int,
    frame: float,
) -> float | None:
    return csab_sample_channel_block(data, block_start, block_end, frame, -1)


def csab_sample_rotation_keyed_channel(
    keys: list[tuple[int, float, float, float]],
    frame: float,
) -> float:
    if not keys:
        return math.nan
    if frame <= keys[0][0]:
        return keys[0][1]
    if frame >= keys[-1][0]:
        return keys[-1][1]

    for left, right in zip(keys, keys[1:]):
        if frame < right[0]:
            interval = float(right[0] - left[0])
            if interval <= 0.0:
                return left[1]
            t = (frame - float(left[0])) / interval
            return csab_cubic_hermite(
                t,
                interval * CSAB_ANOD_HERMITE_INTERVAL_SCALE,
                left[1],
                unwrap_angle_near(left[1], right[1]),
                left[3],
                right[2],
            )
    return keys[-1][1]


def unwrap_angle_near(reference: float, value: float) -> float:
    full_turn = math.tau
    while value - reference > math.pi:
        value -= full_turn
    while value - reference < -math.pi:
        value += full_turn
    return value


def csab_pose_bone_from_channel_values(
    bone: SkeletonBone,
    values: dict[int, float],
) -> SkeletonBone:
    return SkeletonBone(
        index=bone.index,
        parent_index=bone.parent_index,
        scale=Vec3(
            values.get(6, bone.scale.x),
            values.get(7, bone.scale.y),
            values.get(8, bone.scale.z),
        ),
        rotation=Vec3(
            values.get(3, bone.rotation.x),
            values.get(4, bone.rotation.y),
            values.get(5, bone.rotation.z),
        ),
        translation=Vec3(
            values.get(0, bone.translation.x),
            values.get(1, bone.translation.y),
            values.get(2, bone.translation.z),
        ),
    )


def compose_world_transforms(
    skeleton: Skeleton,
    local_transforms: tuple[Matrix4, ...],
) -> tuple[Matrix4, ...]:
    resolved: list[Matrix4 | None] = [None] * len(skeleton.bones)

    def resolve(index: int) -> Matrix4:
        cached = resolved[index]
        if cached is not None:
            return cached
        bone = skeleton.bones[index]
        local = local_transforms[index]
        if bone.parent_index < 0:
            world = local
        else:
            world = multiply_matrix(resolve(bone.parent_index), local)
        resolved[index] = world
        return world

    return tuple(resolve(index) for index in range(len(skeleton.bones)))


def add_axis_range(
    ranges: dict[str, list[float]],
    axis: str,
    value: float,
) -> None:
    current = ranges.get(axis)
    if current is None:
        ranges[axis] = [value, value]
    else:
        current[0] = min(current[0], value)
        current[1] = max(current[1], value)


def rounded_range_mapping(ranges: dict[str, list[float]]) -> dict[str, dict[str, float]]:
    return {
        axis: {
            "min": round(bounds[0], 6),
            "max": round(bounds[1], 6),
        }
        for axis, bounds in sorted(ranges.items())
    }


def add_csab_playback_pose_sample_counts(
    pose_sample: dict[str, object],
    counts: Counter[str],
    slot_sample_counts: Counter[str],
) -> None:
    counts[
        "valid_pose_sample_sets"
        if pose_sample.get("valid")
        else "invalid_pose_sample_sets"
    ] += 1
    for key in (
        "sampled_pose_frames",
        "sampled_skeleton_bone_transforms",
        "sampled_animated_bone_transforms",
        "sampled_channel_values",
        "finite_channel_values",
        "non_f32_channel_blocks",
        "world_matrix_entries",
        "finite_world_matrix_entries",
    ):
        value = pose_sample.get(key)
        if isinstance(value, int):
            counts[key] += value
    slot_sample_counts.update(counter_from_mapping(pose_sample.get("slot_sample_counts")))


def counter_from_mapping(value: object) -> Counter[str]:
    if not isinstance(value, dict):
        return Counter()
    return Counter({str(key): int(count) for key, count in value.items()})


def add_csab_skeleton_match_metadata(
    archive: ZarArchive,
    file: ZarFile,
    path: Path,
    actor_root: Path,
    cmb_bone_records: list[dict[str, object]],
    match_counts: Counter[str],
    target_resolution_counts: Counter[str],
    resolved_target_support_counts: Counter[str],
    playback_blocker_counts: Counter[str],
    playback_candidate_node_table_counts: Counter[str],
    playback_candidate_anod_record_counts: Counter[str],
    playback_candidate_anod_active_channel_count_counts: Counter[str],
    playback_candidate_anod_channel_slot_active_counts: Counter[str],
    playback_candidate_anod_record_field_04_high16_counts: Counter[str],
    playback_candidate_anod_channel_block_counts: Counter[str],
    playback_candidate_anod_channel_block_class_counts: Counter[str],
    playback_candidate_anod_channel_block_class_key_entry_counts: Counter[str],
    playback_candidate_anod_channel_block_key_count_counts: Counter[str],
    playback_candidate_anod_channel_block_slot_class_counts: Counter[str],
    playback_candidate_f32_sampler_counts: Counter[str],
    playback_candidate_f32_sampler_frame_count_counts: Counter[str],
    playback_candidate_f32_sampler_slot_sample_counts: Counter[str],
    playback_candidate_pose_sample_counts: Counter[str],
    playback_candidate_pose_sample_slot_counts: Counter[str],
    playback_candidate_full_pose_sample_counts: Counter[str],
    playback_candidate_full_pose_sample_slot_counts: Counter[str],
    samples: list[dict[str, object]],
    playback_candidate_samples: list[dict[str, object]],
    sample_limit: int,
    cmb_models_by_embedded_name: dict[str, CmbModel],
) -> None:
    data = archive.read_file(file)
    candidates = csab_header_candidates(data)
    node_table = csab_node_table_metadata(data)
    anod_records = csab_anod_record_metadata(data, node_table)
    channel_blocks = csab_anod_channel_block_metadata(data, node_table)
    skeleton_bone_count = candidates.get("skeleton_bone_count_candidate")
    animated_bone_count = candidates.get("animated_bone_count_candidate")
    if skeleton_bone_count is None:
        match_status = "missing_skeleton_bone_count_candidate"
        matches: list[dict[str, object]] = []
    else:
        matches = [
            record
            for record in cmb_bone_records
            if record["bone_count"] == skeleton_bone_count
        ]
        if len(matches) == 1:
            match_status = "matches_single_cmb_bone_count"
        elif len(matches) > 1:
            match_status = "matches_multiple_cmb_bone_counts"
        elif cmb_bone_records:
            match_status = "no_matching_cmb_bone_count"
        else:
            match_status = "no_parsed_cmb_model"
    match_counts[match_status] += 1
    if animated_bone_count is not None and skeleton_bone_count is not None:
        if animated_bone_count <= skeleton_bone_count:
            match_counts["animated_bone_count_lte_skeleton_candidate"] += 1
        else:
            match_counts["animated_bone_count_gt_skeleton_candidate"] += 1
    target_status, target_candidates = csab_target_resolution(file.name, match_status, matches)
    target_resolution_counts[target_status] += 1
    add_csab_target_support_metadata(
        path,
        actor_root,
        file,
        data,
        candidates,
        node_table,
        anod_records,
        channel_blocks,
        target_status,
        target_candidates,
        resolved_target_support_counts,
        playback_blocker_counts,
        playback_candidate_node_table_counts,
        playback_candidate_anod_record_counts,
        playback_candidate_anod_active_channel_count_counts,
        playback_candidate_anod_channel_slot_active_counts,
        playback_candidate_anod_record_field_04_high16_counts,
        playback_candidate_anod_channel_block_counts,
        playback_candidate_anod_channel_block_class_counts,
        playback_candidate_anod_channel_block_class_key_entry_counts,
        playback_candidate_anod_channel_block_key_count_counts,
        playback_candidate_anod_channel_block_slot_class_counts,
        playback_candidate_f32_sampler_counts,
        playback_candidate_f32_sampler_frame_count_counts,
        playback_candidate_f32_sampler_slot_sample_counts,
        playback_candidate_pose_sample_counts,
        playback_candidate_pose_sample_slot_counts,
        playback_candidate_full_pose_sample_counts,
        playback_candidate_full_pose_sample_slot_counts,
        playback_candidate_samples,
        sample_limit,
        cmb_models_by_embedded_name,
    )

    if match_status == "matches_single_cmb_bone_count" or len(samples) >= sample_limit:
        return
    samples.append(
        {
            "archive_path": path.relative_to(actor_root).as_posix(),
            "embedded_name": file.name,
            "size": len(data),
            "match_status": match_status,
            "target_resolution_status": target_status,
            **candidates,
            "node_table": node_table,
            "anod_records": anod_records,
            "anod_channel_blocks": channel_blocks,
            "target_cmb_candidates": target_candidates,
            "matching_cmbs": matches[:8],
            "archive_cmb_bone_counts": cmb_bone_records[:12],
        }
    )


def add_csab_target_support_metadata(
    path: Path,
    actor_root: Path,
    file: ZarFile,
    data: bytes,
    header_candidates: dict[str, int],
    node_table: dict[str, object],
    anod_records: dict[str, object],
    channel_blocks: dict[str, object],
    target_status: str,
    target_candidates: list[dict[str, object]],
    resolved_target_support_counts: Counter[str],
    playback_blocker_counts: Counter[str],
    playback_candidate_node_table_counts: Counter[str],
    playback_candidate_anod_record_counts: Counter[str],
    playback_candidate_anod_active_channel_count_counts: Counter[str],
    playback_candidate_anod_channel_slot_active_counts: Counter[str],
    playback_candidate_anod_record_field_04_high16_counts: Counter[str],
    playback_candidate_anod_channel_block_counts: Counter[str],
    playback_candidate_anod_channel_block_class_counts: Counter[str],
    playback_candidate_anod_channel_block_class_key_entry_counts: Counter[str],
    playback_candidate_anod_channel_block_key_count_counts: Counter[str],
    playback_candidate_anod_channel_block_slot_class_counts: Counter[str],
    playback_candidate_f32_sampler_counts: Counter[str],
    playback_candidate_f32_sampler_frame_count_counts: Counter[str],
    playback_candidate_f32_sampler_slot_sample_counts: Counter[str],
    playback_candidate_pose_sample_counts: Counter[str],
    playback_candidate_pose_sample_slot_counts: Counter[str],
    playback_candidate_full_pose_sample_counts: Counter[str],
    playback_candidate_full_pose_sample_slot_counts: Counter[str],
    playback_candidate_samples: list[dict[str, object]],
    sample_limit: int,
    cmb_models_by_embedded_name: dict[str, CmbModel],
) -> None:
    if len(target_candidates) != 1:
        playback_blocker_counts["target_unresolved_or_missing"] += 1
        return

    target = target_candidates[0]
    support_status = str(target["support_status"])
    resolved_target_support_counts[support_status] += 1
    if support_status in CURRENT_RIGID_EXPORT_TARGET_SUPPORT:
        playback_blocker_counts["target_export_supported_by_current_rigid_path"] += 1
        add_csab_node_table_counts(node_table, playback_candidate_node_table_counts)
        add_csab_anod_record_counts(
            anod_records,
            playback_candidate_anod_record_counts,
            playback_candidate_anod_active_channel_count_counts,
            playback_candidate_anod_channel_slot_active_counts,
            playback_candidate_anod_record_field_04_high16_counts,
        )
        add_csab_anod_channel_block_counts(
            channel_blocks,
            playback_candidate_anod_channel_block_counts,
            playback_candidate_anod_channel_block_class_counts,
            playback_candidate_anod_channel_block_class_key_entry_counts,
            playback_candidate_anod_channel_block_key_count_counts,
            playback_candidate_anod_channel_block_slot_class_counts,
        )
        sampler = csab_playback_f32_sampler_metadata(data, node_table)
        add_csab_playback_f32_sampler_counts(
            sampler,
            playback_candidate_f32_sampler_counts,
            playback_candidate_f32_sampler_frame_count_counts,
            playback_candidate_f32_sampler_slot_sample_counts,
        )
        target_model = cmb_models_by_embedded_name.get(str(target["embedded_name"]))
        pose_sample = csab_playback_pose_sample_metadata(
            data,
            target_model,
            node_table,
        )
        frame_count = header_candidates.get("frame_count_candidate")
        full_pose_sample = csab_playback_pose_sample_metadata(
            data,
            target_model,
            node_table,
            sample_frames=list(range(frame_count + 1))
            if isinstance(frame_count, int)
            else [],
            sampler_name="trs_candidate_all_integer_frame_world_matrices",
        )
        add_csab_playback_pose_sample_counts(
            pose_sample,
            playback_candidate_pose_sample_counts,
            playback_candidate_pose_sample_slot_counts,
        )
        add_csab_playback_pose_sample_counts(
            full_pose_sample,
            playback_candidate_full_pose_sample_counts,
            playback_candidate_full_pose_sample_slot_counts,
        )
        if len(playback_candidate_samples) < sample_limit:
            playback_candidate_samples.append(
                {
                    "archive_path": path.relative_to(actor_root).as_posix(),
                    "embedded_name": file.name,
                    "target_resolution_status": target_status,
                    "target_support_status": support_status,
                    **header_candidates,
                    "node_table": node_table,
                    "anod_records": anod_records,
                    "anod_channel_blocks": channel_blocks,
                    "f32_sampler": sampler,
                    "pose_sample": pose_sample,
                    "full_pose_sample": full_pose_sample,
                    "target_cmb": target,
                }
            )
    elif support_status.startswith("needs_skinning"):
        playback_blocker_counts["target_needs_skinning_support"] += 1
    else:
        playback_blocker_counts["target_needs_unknown_support"] += 1


def csab_target_resolution(
    animation_name: str,
    match_status: str,
    matches: list[dict[str, object]],
) -> tuple[str, list[dict[str, object]]]:
    if match_status == "matches_single_cmb_bone_count":
        return "single_bone_count_match", matches
    if match_status == "missing_skeleton_bone_count_candidate":
        return "missing_skeleton_bone_count_candidate", []
    if match_status == "no_matching_cmb_bone_count":
        return "no_matching_cmb_bone_count", []
    if match_status == "no_parsed_cmb_model":
        return "no_parsed_cmb_model", []

    animation_stem = normalized_asset_stem(animation_name)
    exact_matches = [
        record
        for record in matches
        if animation_stem in cmb_candidate_stems(record)
    ]
    if len(exact_matches) == 1:
        return "multiple_bone_count_exact_name_match", exact_matches

    contained_matches = [
        record
        for record in matches
        if stem_contained(animation_stem, cmb_candidate_stems(record))
    ]
    if len(contained_matches) == 1:
        return "multiple_bone_count_contained_name_match", contained_matches

    return "multiple_bone_count_unresolved", []


def cmb_candidate_stems(record: dict[str, object]) -> set[str]:
    stems = set()
    for key in ("embedded_name", "model_name"):
        value = record.get(key)
        if isinstance(value, str):
            stems.add(normalized_asset_stem(value))
    return stems


def stem_contained(animation_stem: str, cmb_stems: set[str]) -> bool:
    return any(
        animation_stem in cmb_stem or cmb_stem in animation_stem
        for cmb_stem in cmb_stems
        if len(animation_stem) >= 3 and len(cmb_stem) >= 3
    )


def normalized_asset_stem(name: str) -> str:
    filename = name.replace("\\", "/").rsplit("/", 1)[-1]
    stem = filename.rsplit(".", 1)[0].lower()
    for suffix in MODEL_STEM_SUFFIXES:
        if stem.endswith(suffix):
            stem = stem[: -len(suffix)]
            break
    return stem.rstrip("_")


def format_magic(data: bytes) -> str:
    if all(32 <= byte < 127 for byte in data):
        return "ascii:" + data.decode("ascii", errors="strict")
    return "hex:" + data.hex()


def animation_payload_size_summary(payload_sizes: dict[str, list[int]]) -> dict[str, dict[str, int]]:
    summary: dict[str, dict[str, int]] = {}
    for file_type in sorted(payload_sizes):
        sizes = payload_sizes[file_type]
        if not sizes:
            continue
        summary[file_type] = {
            "count": len(sizes),
            "min": min(sizes),
            "max": max(sizes),
            "unique_size_count": len(set(sizes)),
        }
    return summary


def sorted_nested_counter(counters: dict[str, Counter[str]]) -> dict[str, dict[str, int]]:
    return {key: sorted_counter(counters[key]) for key in sorted(counters)}


def sorted_deep_nested_counter(
    counters: dict[str, dict[str, Counter[str]]],
) -> dict[str, dict[str, dict[str, int]]]:
    return {
        outer_key: {
            inner_key: sorted_counter(counters[outer_key][inner_key])
            for inner_key in sorted(counters[outer_key])
        }
        for outer_key in sorted(counters)
    }


def add_parse_error(
    parse_errors: list[dict[str, object]],
    path: Path,
    actor_root: Path,
    kind: str,
    error: str,
    *,
    embedded_name: str | None = None,
) -> None:
    parse_errors.append(
        {
            "path": path.relative_to(actor_root).as_posix(),
            "kind": kind,
            "embedded_name": embedded_name,
            "error": error,
        }
    )
