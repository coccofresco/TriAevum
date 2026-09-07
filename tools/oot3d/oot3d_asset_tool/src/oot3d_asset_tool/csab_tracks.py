from __future__ import annotations

from collections import Counter
import json
import math
from pathlib import Path

from .actor_inventory import (
    CSAB_NODE_OFFSET_BASE,
    CURRENT_RIGID_EXPORT_TARGET_SUPPORT,
    csab_anod_active_channel_offsets,
    csab_anod_channel_block_class,
    csab_f32_channel_keys,
    csab_f32_constant_channel_value,
    csab_header_candidates,
    csab_node_table_arrays,
    csab_node_table_metadata,
    csab_playback_pose_sample_metadata,
    csab_s16_constant_rotation_value,
    csab_s16_rotation_channel_keys,
    csab_target_resolution,
    model_support_status,
)
from .binary import ParseError
from .cmb import CmbModel, SkeletonBone, Vec3
from .zar import ZarArchive, ZarFile

SKINNED_TARGET_SUPPORT_PREFIX = "needs_skinning"
CONSTANT_TRANSLATION_BIND_ABSOLUTE_TOLERANCE = 0.5
CONSTANT_TRANSLATION_BIND_RELATIVE_TOLERANCE = 0.01

CHANNEL_SLOT_SEMANTICS = {
    0: "translation_x",
    1: "translation_y",
    2: "translation_z",
    3: "rotation_x",
    4: "rotation_y",
    5: "rotation_z",
    6: "scale_x",
    7: "scale_y",
    8: "scale_z",
}


def export_csab_rigid_tracks(
    zar_path: Path,
    csab_name: str,
    cmb_name: str,
    output_path: Path | None = None,
) -> dict[str, object]:
    archive = ZarArchive.from_path(zar_path)
    csab_file = find_archive_file(archive, csab_name)
    cmb_file = find_archive_file(archive, cmb_name)
    csab_data = archive.read_file(csab_file)
    target_model = CmbModel.parse(archive.read_file(cmb_file), f"{zar_path}!{cmb_file.name}")

    export = csab_rigid_track_export(
        csab_data,
        target_model,
        source_zar=str(zar_path),
        csab_name=csab_file.name,
        target_cmb_name=cmb_file.name,
    )
    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(
            json.dumps(export, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    return export


def export_csab_skeleton_tracks(
    zar_path: Path,
    csab_name: str,
    cmb_name: str,
    output_path: Path | None = None,
) -> dict[str, object]:
    archive = ZarArchive.from_path(zar_path)
    csab_file = find_archive_file(archive, csab_name)
    cmb_file = find_archive_file(archive, cmb_name)
    csab_data = archive.read_file(csab_file)
    target_model = CmbModel.parse(archive.read_file(cmb_file), f"{zar_path}!{cmb_file.name}")

    export = csab_rigid_track_export(
        csab_data,
        target_model,
        source_zar=str(zar_path),
        csab_name=csab_file.name,
        target_cmb_name=cmb_file.name,
    )
    export["format"] = "oot3d_csab_skeleton_track_export_v1"
    export["target_support_status"] = model_support_status(target_model)
    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(
            json.dumps(export, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    return export


def batch_export_csab_rigid_tracks(
    actor_root: Path,
    output_dir: Path,
) -> Path:
    if not actor_root.is_dir():
        raise ParseError(f"{actor_root}: expected an extracted OOT3D actor directory")
    output_dir.mkdir(parents=True, exist_ok=True)
    track_dir = output_dir / "tracks"
    track_dir.mkdir(parents=True, exist_ok=True)

    records: list[dict[str, object]] = []
    considered_csab = 0
    exported = 0
    failed = 0
    target_unresolved_or_missing = 0
    target_needs_skinning_support = 0
    target_needs_unknown_support = 0
    aggregate_counts = {
        "track_count": 0,
        "channel_count": 0,
        "const_channel_count": 0,
        "keyed_channel_count": 0,
        "keyframe_count": 0,
        "frame_slot_count": 0,
        "sampled_pose_frames": 0,
        "sampled_channel_values": 0,
        "finite_world_matrix_entries": 0,
        "non_f32_channel_blocks": 0,
    }

    for zar_path in sorted(actor_root.rglob("*.zar")):
        try:
            archive = ZarArchive.from_path(zar_path)
        except Exception as exc:
            records.append(
                {
                    "status": "parse_failed",
                    "archive_path": zar_path.relative_to(actor_root).as_posix(),
                    "reason": str(exc),
                }
            )
            failed += 1
            continue

        cmb_records, cmb_models_by_name = parsed_archive_cmb_records(
            archive,
            zar_path,
        )
        for csab_file in archive.files:
            if not is_csab_file(csab_file):
                continue
            considered_csab += 1
            try:
                target_status, target_candidates = resolve_csab_target(
                    archive,
                    csab_file,
                    cmb_records,
                )
                if len(target_candidates) != 1:
                    target_unresolved_or_missing += 1
                    records.append(
                        skipped_track_record(
                            actor_root,
                            zar_path,
                            csab_file,
                            target_status,
                            "target_unresolved_or_missing",
                        )
                    )
                    continue
                target = target_candidates[0]
                support_status = str(target["support_status"])
                if support_status not in CURRENT_RIGID_EXPORT_TARGET_SUPPORT:
                    if support_status.startswith("needs_skinning"):
                        target_needs_skinning_support += 1
                        skip_reason = "target_needs_skinning_support"
                    else:
                        target_needs_unknown_support += 1
                        skip_reason = "target_needs_unknown_support"
                    records.append(
                        skipped_track_record(
                            actor_root,
                            zar_path,
                            csab_file,
                            target_status,
                            skip_reason,
                            target,
                        )
                    )
                    continue

                target_name = str(target["embedded_name"])
                target_model = cmb_models_by_name[target_name]
                csab_data = archive.read_file(csab_file)
                track_export = csab_rigid_track_export(
                    csab_data,
                    target_model,
                    source_zar=str(zar_path),
                    csab_name=csab_file.name,
                    target_cmb_name=target_name,
                )
                track_path = track_dir / track_export_filename(
                    zar_path.relative_to(actor_root).as_posix(),
                    csab_file.name,
                    target_name,
                )
                track_path.write_text(
                    json.dumps(track_export, indent=2) + "\n",
                    encoding="utf-8",
                    newline="\n",
                )
                exported += 1
                update_batch_track_counts(aggregate_counts, track_export)
                records.append(
                    {
                        "status": "exported",
                        "archive_path": zar_path.relative_to(actor_root).as_posix(),
                        "csab_name": csab_file.name,
                        "target_resolution_status": target_status,
                        "target_cmb_name": target_name,
                        "target_support_status": support_status,
                        "track_export": track_path.relative_to(output_dir).as_posix(),
                        "counts": track_export["counts"],
                        "frame_slot_count": track_export["frame_slot_count"],
                        "validation": track_export["validation"]["full_pose_sample"],
                    }
                )
            except Exception as exc:
                failed += 1
                records.append(
                    {
                        "status": "failed",
                        "archive_path": zar_path.relative_to(actor_root).as_posix(),
                        "csab_name": csab_file.name,
                        "reason": str(exc),
                    }
                )

    manifest = {
        "format": "oot3d_csab_rigid_track_batch_v1",
        "actor_root": str(actor_root),
        "output": str(output_dir),
        "track_dir": str(track_dir),
        "considered_csab": considered_csab,
        "exported": exported,
        "failed": failed,
        "target_unresolved_or_missing": target_unresolved_or_missing,
        "target_needs_skinning_support": target_needs_skinning_support,
        "target_needs_unknown_support": target_needs_unknown_support,
        "counts": aggregate_counts,
        "records": records,
    }
    manifest_path = output_dir / "csab_rigid_track_batch_manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    return manifest_path


def batch_export_csab_skeleton_tracks(
    actor_root: Path,
    output_dir: Path,
) -> Path:
    if not actor_root.is_dir():
        raise ParseError(f"{actor_root}: expected an extracted OOT3D actor directory")
    output_dir.mkdir(parents=True, exist_ok=True)
    track_dir = output_dir / "tracks"
    track_dir.mkdir(parents=True, exist_ok=True)

    records: list[dict[str, object]] = []
    considered_csab = 0
    archive_parse_failed = 0
    target_unresolved_or_missing = 0
    target_not_skinned = 0
    resolved_skinned_targets = 0
    target_unsupported_non_f32_channels = 0
    exported = 0
    failed = 0
    support_status_counts: Counter[str] = Counter()
    exported_support_status_counts: Counter[str] = Counter()
    unsupported_support_status_counts: Counter[str] = Counter()
    encoding_counts: Counter[str] = Counter()
    aggregate_counts = {
        "track_count": 0,
        "channel_count": 0,
        "const_channel_count": 0,
        "keyed_channel_count": 0,
        "keyframe_count": 0,
        "frame_slot_count": 0,
        "sampled_pose_frames": 0,
        "sampled_channel_values": 0,
        "finite_world_matrix_entries": 0,
        "non_f32_channel_blocks": 0,
    }
    unsupported_counts = {
        "sampled_pose_frames": 0,
        "sampled_channel_values": 0,
        "finite_channel_values": 0,
        "non_f32_channel_blocks": 0,
        "world_matrix_entries": 0,
        "finite_world_matrix_entries": 0,
    }

    for zar_path in sorted(actor_root.rglob("*.zar")):
        try:
            archive = ZarArchive.from_path(zar_path)
            cmb_records, cmb_models_by_name = parsed_archive_cmb_records(
                archive,
                zar_path,
            )
        except Exception as exc:
            archive_parse_failed += 1
            failed += 1
            records.append(
                {
                    "status": "parse_failed",
                    "archive_path": zar_path.relative_to(actor_root).as_posix(),
                    "reason": str(exc),
                }
            )
            continue

        for csab_file in archive.files:
            if not is_csab_file(csab_file):
                continue
            considered_csab += 1
            try:
                target_status, target_candidates = resolve_csab_target(
                    archive,
                    csab_file,
                    cmb_records,
                )
                if len(target_candidates) != 1:
                    target_unresolved_or_missing += 1
                    records.append(
                        skipped_track_record(
                            actor_root,
                            zar_path,
                            csab_file,
                            target_status,
                            "target_unresolved_or_missing",
                        )
                    )
                    continue

                target = target_candidates[0]
                support_status = str(target["support_status"])
                support_status_counts[support_status] += 1
                if not support_status.startswith(SKINNED_TARGET_SUPPORT_PREFIX):
                    target_not_skinned += 1
                    records.append(
                        skipped_track_record(
                            actor_root,
                            zar_path,
                            csab_file,
                            target_status,
                            "target_not_skinned",
                            target,
                        )
                    )
                    continue

                resolved_skinned_targets += 1
                target_name = str(target["embedded_name"])
                target_model = cmb_models_by_name[target_name]
                csab_data = archive.read_file(csab_file)
                context = csab_track_context(
                    csab_data,
                    target_model,
                    csab_file.name,
                )
                full_pose_sample = context["full_pose_sample"]
                if not isinstance(full_pose_sample, dict):
                    raise ParseError(f"{csab_file.name}: full-frame pose validation is missing")
                if not full_pose_sample.get("valid"):
                    non_f32_blocks = int(full_pose_sample.get("non_f32_channel_blocks", 0))
                    if non_f32_blocks > 0:
                        target_unsupported_non_f32_channels += 1
                        unsupported_support_status_counts[support_status] += 1
                        update_unsupported_track_counts(unsupported_counts, full_pose_sample)
                        records.append(
                            {
                                **skipped_track_record(
                                    actor_root,
                                    zar_path,
                                    csab_file,
                                    target_status,
                                    "unsupported_non_f32_channel_blocks",
                                    target,
                                ),
                                "validation": compact_pose_validation(full_pose_sample),
                            }
                        )
                        continue
                    raise ParseError(
                        f"{csab_file.name}: full-frame pose validation failed: "
                        f"{full_pose_sample.get('status')}"
                    )

                track_export = csab_track_export_from_context(
                    target_model,
                    source_zar=str(zar_path),
                    csab_name=csab_file.name,
                    target_cmb_name=target_name,
                    context=context,
                    format_name="oot3d_csab_skeleton_track_export_v1",
                )
                track_export["target_support_status"] = support_status
                track_path = track_dir / track_export_filename(
                    zar_path.relative_to(actor_root).as_posix(),
                    csab_file.name,
                    target_name,
                )
                track_path.write_text(
                    json.dumps(track_export, indent=2) + "\n",
                    encoding="utf-8",
                    newline="\n",
                )
                exported += 1
                exported_support_status_counts[support_status] += 1
                update_batch_track_counts(aggregate_counts, track_export)
                encoding_counts.update(
                    counter_from_mapping(track_export["counts"].get("encoding_counts"))
                )
                records.append(
                    {
                        "status": "exported",
                        "archive_path": zar_path.relative_to(actor_root).as_posix(),
                        "csab_name": csab_file.name,
                        "target_resolution_status": target_status,
                        "target_cmb_name": target_name,
                        "target_support_status": support_status,
                        "track_export": track_path.relative_to(output_dir).as_posix(),
                        "counts": track_export["counts"],
                        "frame_slot_count": track_export["frame_slot_count"],
                        "validation": compact_pose_validation(full_pose_sample),
                    }
                )
            except Exception as exc:
                failed += 1
                records.append(
                    {
                        "status": "failed",
                        "archive_path": zar_path.relative_to(actor_root).as_posix(),
                        "csab_name": csab_file.name,
                        "reason": str(exc),
                    }
                )

    manifest = {
        "format": "oot3d_csab_skeleton_track_batch_v1",
        "actor_root": str(actor_root),
        "output": str(output_dir),
        "track_dir": str(track_dir),
        "considered_csab": considered_csab,
        "archive_parse_failed": archive_parse_failed,
        "target_unresolved_or_missing": target_unresolved_or_missing,
        "target_not_skinned": target_not_skinned,
        "resolved_skinned_targets": resolved_skinned_targets,
        "target_unsupported_non_f32_channels": target_unsupported_non_f32_channels,
        "exported": exported,
        "failed": failed,
        "support_status_counts": sorted_counter_dict(support_status_counts),
        "exported_support_status_counts": sorted_counter_dict(exported_support_status_counts),
        "unsupported_support_status_counts": sorted_counter_dict(
            unsupported_support_status_counts
        ),
        "encoding_counts": sorted_counter_dict(encoding_counts),
        "counts": aggregate_counts,
        "unsupported_counts": unsupported_counts,
        "records": records,
    }
    manifest_path = output_dir / "csab_skeleton_track_batch_manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    return manifest_path


def parsed_archive_cmb_records(
    archive: ZarArchive,
    zar_path: Path,
) -> tuple[list[dict[str, object]], dict[str, CmbModel]]:
    records: list[dict[str, object]] = []
    models_by_name: dict[str, CmbModel] = {}
    for file in archive.files:
        if not is_cmb_file(file):
            continue
        model = CmbModel.parse(archive.read_file(file), f"{zar_path}!{file.name}")
        models_by_name[file.name] = model
        records.append(
            {
                "embedded_name": file.name,
                "model_name": model.name,
                "bone_count": model.bone_count,
                "support_status": model_support_status(model),
            }
        )
    return records, models_by_name


def resolve_csab_target(
    archive: ZarArchive,
    csab_file: ZarFile,
    cmb_records: list[dict[str, object]],
) -> tuple[str, list[dict[str, object]]]:
    header_candidates = csab_header_candidates(archive.read_file(csab_file))
    skeleton_bone_count = header_candidates.get("skeleton_bone_count_candidate")
    if skeleton_bone_count is None:
        return "missing_skeleton_bone_count_candidate", []
    matches = [
        record for record in cmb_records if record["bone_count"] == skeleton_bone_count
    ]
    if len(matches) == 1:
        match_status = "matches_single_cmb_bone_count"
    elif len(matches) > 1:
        match_status = "matches_multiple_cmb_bone_counts"
    elif cmb_records:
        match_status = "no_matching_cmb_bone_count"
    else:
        match_status = "no_parsed_cmb_model"
    return csab_target_resolution(csab_file.name, match_status, matches)


def skipped_track_record(
    actor_root: Path,
    zar_path: Path,
    csab_file: ZarFile,
    target_status: str,
    reason: str,
    target: dict[str, object] | None = None,
) -> dict[str, object]:
    record: dict[str, object] = {
        "status": "skipped",
        "archive_path": zar_path.relative_to(actor_root).as_posix(),
        "csab_name": csab_file.name,
        "target_resolution_status": target_status,
        "reason": reason,
    }
    if target is not None:
        record["target_cmb_name"] = target.get("embedded_name")
        record["target_support_status"] = target.get("support_status")
    return record


def update_batch_track_counts(
    counts: dict[str, int],
    track_export: dict[str, object],
) -> None:
    track_counts = track_export["counts"]
    if not isinstance(track_counts, dict):
        raise ParseError("track export counts are missing")
    for key in (
        "track_count",
        "channel_count",
        "const_channel_count",
        "keyed_channel_count",
        "keyframe_count",
    ):
        counts[key] += int(track_counts[key])
    counts["frame_slot_count"] += int(track_export["frame_slot_count"])
    full_pose = track_export["validation"]["full_pose_sample"]  # type: ignore[index]
    if not isinstance(full_pose, dict):
        raise ParseError("track export full-frame pose validation is missing")
    for key in (
        "sampled_pose_frames",
        "sampled_channel_values",
        "finite_world_matrix_entries",
        "non_f32_channel_blocks",
    ):
        counts[key] += int(full_pose[key])


def update_unsupported_track_counts(
    counts: dict[str, int],
    full_pose_sample: dict[str, object],
) -> None:
    for key in (
        "sampled_pose_frames",
        "sampled_channel_values",
        "finite_channel_values",
        "non_f32_channel_blocks",
        "world_matrix_entries",
        "finite_world_matrix_entries",
    ):
        counts[key] += int(full_pose_sample.get(key, 0))


def compact_pose_validation(full_pose_sample: dict[str, object]) -> dict[str, object]:
    keys = (
        "valid",
        "status",
        "sampled_pose_frames",
        "target_bone_count",
        "sampled_skeleton_bone_transforms",
        "sampled_animated_bone_transforms",
        "sampled_channel_values",
        "finite_channel_values",
        "non_f32_channel_blocks",
        "world_matrix_entries",
        "finite_world_matrix_entries",
        "slot_sample_counts",
    )
    return {key: full_pose_sample[key] for key in keys if key in full_pose_sample}


def sorted_counter_dict(counter: Counter[str]) -> dict[str, int]:
    return {key: counter[key] for key in sorted(counter)}


def counter_from_mapping(value: object) -> Counter[str]:
    if not isinstance(value, dict):
        return Counter()
    return Counter({str(key): int(count) for key, count in value.items()})


def track_export_filename(archive_path: str, csab_name: str, cmb_name: str) -> str:
    return (
        f"{safe_path_part(archive_path)}__"
        f"{safe_path_part(csab_name)}__"
        f"{safe_path_part(cmb_name)}.json"
    )


def safe_path_part(value: str) -> str:
    safe = []
    for char in value.replace("\\", "/"):
        if char.isalnum():
            safe.append(char)
        else:
            safe.append("_")
    return "".join(safe).strip("_") or "asset"


def is_csab_file(file: ZarFile) -> bool:
    return file.type_name == "csab" or file.name.lower().endswith(".csab")


def is_cmb_file(file: ZarFile) -> bool:
    return file.type_name == "cmb" or file.name.lower().endswith(".cmb")


def find_archive_file(archive: ZarArchive, name: str) -> ZarFile:
    normalized = name.replace("\\", "/").lower()
    matches = [
        file
        for file in archive.files
        if file.name.replace("\\", "/").lower() == normalized
    ]
    if not matches:
        raise ParseError(f"{archive.path}: no embedded file named {name!r}")
    if len(matches) > 1:
        raise ParseError(f"{archive.path}: embedded file name {name!r} is ambiguous")
    return matches[0]


def csab_rigid_track_export(
    csab_data: bytes,
    target_model: CmbModel,
    *,
    source_zar: str,
    csab_name: str,
    target_cmb_name: str,
) -> dict[str, object]:
    context = csab_track_context(csab_data, target_model, csab_name)
    full_pose_sample = context["full_pose_sample"]
    if not isinstance(full_pose_sample, dict) or not full_pose_sample.get("valid"):
        status = full_pose_sample.get("status") if isinstance(full_pose_sample, dict) else "missing"
        raise ParseError(f"{csab_name}: full-frame pose validation failed: {status}")
    return csab_track_export_from_context(
        target_model,
        source_zar=source_zar,
        csab_name=csab_name,
        target_cmb_name=target_cmb_name,
        context=context,
        format_name="oot3d_csab_rigid_track_export_v1",
    )


def csab_track_context(
    csab_data: bytes,
    target_model: CmbModel,
    csab_name: str,
) -> dict[str, object]:
    header_candidates = csab_header_candidates(csab_data)
    node_table = csab_node_table_metadata(csab_data)
    if not node_table.get("valid"):
        raise ParseError(f"{csab_name}: CSAB node table is not valid")
    table_arrays = csab_node_table_arrays(csab_data)
    if table_arrays is None:
        raise ParseError(f"{csab_name}: CSAB node table arrays are missing")

    frame_count = header_candidates.get("frame_count_candidate")
    skeleton_bone_count = header_candidates.get("skeleton_bone_count_candidate")
    animated_bone_count = header_candidates.get("animated_bone_count_candidate")
    if frame_count is None or skeleton_bone_count is None or animated_bone_count is None:
        raise ParseError(f"{csab_name}: CSAB playback header candidates are incomplete")
    if target_model.bone_count != skeleton_bone_count:
        raise ParseError(
            f"{csab_name}: target CMB bone count {target_model.bone_count} does not "
            f"match CSAB skeleton count {skeleton_bone_count}"
        )

    full_pose_sample = csab_playback_pose_sample_metadata(
        csab_data,
        target_model,
        node_table,
        sample_frames=list(range(frame_count + 1)),
        sampler_name="trs_candidate_all_integer_frame_world_matrices",
    )
    if not full_pose_sample.get("valid"):
        return {
            "csab_data": csab_data,
            "header_candidates": header_candidates,
            "node_table": node_table,
            "table_arrays": table_arrays,
            "frame_count": frame_count,
            "skeleton_bone_count": skeleton_bone_count,
            "animated_bone_count": animated_bone_count,
            "full_pose_sample": full_pose_sample,
        }

    return {
        "csab_data": csab_data,
        "header_candidates": header_candidates,
        "node_table": node_table,
        "table_arrays": table_arrays,
        "frame_count": frame_count,
        "skeleton_bone_count": skeleton_bone_count,
        "animated_bone_count": animated_bone_count,
        "full_pose_sample": full_pose_sample,
    }


def csab_track_export_from_context(
    target_model: CmbModel,
    *,
    source_zar: str,
    csab_name: str,
    target_cmb_name: str,
    context: dict[str, object],
    format_name: str,
) -> dict[str, object]:
    csab_data = context["csab_data"]
    table_arrays = context["table_arrays"]
    node_table = context["node_table"]
    full_pose_sample = context["full_pose_sample"]
    if not isinstance(csab_data, bytes):
        raise ParseError(f"{csab_name}: CSAB data is missing from export context")
    if not isinstance(node_table, dict):
        raise ParseError(f"{csab_name}: node table is missing from export context")
    if not isinstance(full_pose_sample, dict) or not full_pose_sample.get("valid"):
        status = full_pose_sample.get("status") if isinstance(full_pose_sample, dict) else "missing"
        raise ParseError(f"{csab_name}: full-frame pose validation failed: {status}")
    bone_to_node_indices, node_offsets = table_arrays
    tracks: list[dict[str, object]] = []
    channel_count = 0
    const_channel_count = 0
    keyed_channel_count = 0
    keyframe_count = 0
    encoding_counts: Counter[str] = Counter()
    target_channel_compatibility_counts: Counter[str] = Counter()

    for bone_index, node_index in enumerate(bone_to_node_indices):
        bone = target_model.skeleton.bones[bone_index]
        if node_index == 0xFFFF:
            continue
        if node_index >= len(node_offsets):
            raise ParseError(f"{csab_name}: bone {bone_index} references node {node_index}")

        channels = csab_node_channel_tracks(csab_data, node_offsets, node_index)
        target_channel_compatibility_counts.update(annotate_target_channel_compatibility(bone, channels))
        channel_count += len(channels)
        for channel in channels:
            encoding_counts[str(channel["encoding"])] += 1
            if str(channel["encoding"]).startswith("constant_"):
                const_channel_count += 1
            elif str(channel["encoding"]).startswith("keyed_"):
                keyed_channel_count += 1
                keyframe_count += int(channel["key_count"])
        tracks.append(
            {
                "bone_index": bone_index,
                "node_index": node_index,
                "base_transform": bone_transform_record(bone),
                "channels": channels,
            }
        )

    export = {
        "format": format_name,
        "source_zar": source_zar,
        "csab_name": csab_name,
        "target_cmb_name": target_cmb_name,
        "target_model_name": target_model.name,
        "frame_count_candidate": context["frame_count"],
        "frame_slot_count": int(context["frame_count"]) + 1,
        "animated_bone_count_candidate": context["animated_bone_count"],
        "skeleton_bone_count_candidate": context["skeleton_bone_count"],
        "channel_slot_semantics": {
            str(slot): semantic for slot, semantic in CHANNEL_SLOT_SEMANTICS.items()
        },
        "counts": {
            "track_count": len(tracks),
            "channel_count": channel_count,
            "const_channel_count": const_channel_count,
            "keyed_channel_count": keyed_channel_count,
            "keyframe_count": keyframe_count,
            "encoding_counts": sorted_counter_dict(encoding_counts),
            "target_channel_compatibility_counts": sorted_counter_dict(target_channel_compatibility_counts),
        },
        "validation": {
            "node_table": node_table,
            "full_pose_sample": full_pose_sample,
        },
        "tracks": tracks,
    }
    return export


def annotate_target_channel_compatibility(
    bone: SkeletonBone,
    channels: list[dict[str, object]],
) -> Counter[str]:
    counts: Counter[str] = Counter()
    for channel in channels:
        status = annotate_target_channel_compatibility_record(bone, channel)
        counts[status] += 1
    return counts


def annotate_target_channel_compatibility_record(
    bone: SkeletonBone,
    channel: dict[str, object],
) -> str:
    slot = int(channel.get("slot", -1))
    encoding = str(channel.get("encoding", ""))
    if slot not in (0, 1, 2) or encoding != "constant_f32":
        channel["apply_to_target"] = True
        channel["target_channel_status"] = "not_constant_translation"
        return "not_constant_translation"

    raw_value = channel.get("value")
    if not isinstance(raw_value, (int, float)) or not math.isfinite(float(raw_value)):
        channel["apply_to_target"] = False
        channel["target_channel_status"] = "invalid_constant_translation"
        return "invalid_constant_translation"

    base_values = (bone.translation.x, bone.translation.y, bone.translation.z)
    base_value = float(base_values[slot])
    value = float(raw_value)
    tolerance = constant_translation_bind_tolerance(base_value)
    delta = value - base_value
    compatible = abs(delta) <= tolerance
    channel["apply_to_target"] = compatible
    channel["target_channel_status"] = (
        "constant_translation_matches_target_bind"
        if compatible
        else "constant_translation_mismatches_target_bind"
    )
    channel["target_bind_value"] = base_value
    channel["target_bind_delta"] = delta
    channel["target_bind_tolerance"] = tolerance
    return str(channel["target_channel_status"])


def constant_translation_bind_tolerance(base_value: float) -> float:
    return max(
        CONSTANT_TRANSLATION_BIND_ABSOLUTE_TOLERANCE,
        abs(base_value) * CONSTANT_TRANSLATION_BIND_RELATIVE_TOLERANCE,
    )


def csab_node_channel_tracks(
    data: bytes,
    node_offsets: list[int],
    node_index: int,
) -> list[dict[str, object]]:
    record_start = CSAB_NODE_OFFSET_BASE + node_offsets[node_index]
    next_record_start = (
        CSAB_NODE_OFFSET_BASE + node_offsets[node_index + 1]
        if node_index + 1 < len(node_offsets)
        else len(data)
    )
    active_offsets = csab_anod_active_channel_offsets(data, record_start)
    channels: list[dict[str, object]] = []
    for active_index, (slot, channel_offset) in enumerate(active_offsets):
        block_start = record_start + channel_offset
        block_end = (
            record_start + active_offsets[active_index + 1][1]
            if active_index + 1 < len(active_offsets)
            else next_record_start
        )
        block_class, key_count = csab_anod_channel_block_class(data, block_start, block_end)
        if block_class == "type1_f32_const_len24":
            channels.append(
                {
                    "slot": slot,
                    "semantic": CHANNEL_SLOT_SEMANTICS.get(slot, f"slot_{slot}"),
                    "encoding": "constant_f32",
                    "value": csab_f32_constant_channel_value(data, block_start),
                }
            )
        elif block_class == "type1_s16_const_len20" and slot in (3, 4, 5):
            channels.append(
                {
                    "slot": slot,
                    "semantic": CHANNEL_SLOT_SEMANTICS.get(slot, f"slot_{slot}"),
                    "encoding": "constant_s16_rotation",
                    "value": csab_s16_constant_rotation_value(data, block_start),
                }
            )
        elif block_class == "type2_f32_keys_len16_plus_count16":
            keys = csab_f32_channel_keys(data, block_start, key_count)
            channels.append(
                {
                    "slot": slot,
                    "semantic": CHANNEL_SLOT_SEMANTICS.get(slot, f"slot_{slot}"),
                    "encoding": "keyed_f32_hermite",
                    "key_count": key_count,
                    "keys": [
                        {
                            "frame": frame,
                            "value": value,
                            "left_tangent": left_tangent,
                            "right_tangent": right_tangent,
                        }
                        for frame, value, left_tangent, right_tangent in keys
                    ],
                }
            )
        elif block_class == "type2_s16_keys_len16_plus_count8" and slot in (3, 4, 5):
            keys = csab_s16_rotation_channel_keys(data, block_start, key_count)
            channels.append(
                {
                    "slot": slot,
                    "semantic": CHANNEL_SLOT_SEMANTICS.get(slot, f"slot_{slot}"),
                    "encoding": "keyed_s16_rotation_hermite",
                    "key_count": key_count,
                    "keys": [
                        {
                            "frame": frame,
                            "value": value,
                            "left_tangent": left_tangent,
                            "right_tangent": right_tangent,
                        }
                        for frame, value, left_tangent, right_tangent in keys
                    ],
                }
            )
        else:
            raise ParseError(
                f"CSAB node {node_index} slot {slot}: unsupported channel block {block_class}"
            )
    return channels


def bone_transform_record(bone: SkeletonBone) -> dict[str, object]:
    return {
        "scale": vec3_record(bone.scale),
        "rotation": vec3_record(bone.rotation),
        "translation": vec3_record(bone.translation),
    }


def vec3_record(value: Vec3) -> dict[str, float]:
    return {
        "x": value.x,
        "y": value.y,
        "z": value.z,
    }
