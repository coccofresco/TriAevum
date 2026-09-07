from __future__ import annotations

from collections import Counter
import json
import math
from pathlib import Path

from .actor_inventory import (
    CSAB_NODE_OFFSET_BASE,
    csab_anod_active_channel_offsets,
    csab_anod_channel_block_class,
    csab_channel_values_by_node,
    csab_header_candidates,
    csab_node_table_arrays,
    csab_node_table_metadata,
    csab_pose_bone_from_channel_values,
    compose_world_transforms,
    model_support_status,
)
from .binary import ParseError
from .cmb import CmbModel, Vec3
from .csab_tracks import (
    CONSTANT_TRANSLATION_BIND_ABSOLUTE_TOLERANCE,
    CONSTANT_TRANSLATION_BIND_RELATIVE_TOLERANCE,
    SKINNED_TARGET_SUPPORT_PREFIX,
    find_archive_file,
    parsed_archive_cmb_records,
    resolve_csab_target,
    safe_path_part,
    skipped_track_record,
)
from .legacy_fast_resource import (
    Matrix4,
    bone_local_transform,
    multiply_matrix,
    skeleton_world_transforms,
    transform_direction,
    transform_position,
)
from .skinned_export import (
    export_cmb_skinned_bind_pose,
    normalize_or_default,
    round_float,
    vec3_is_finite,
    vec3_to_list,
)
from .zar import ZarArchive

SKINNED_ANIMATION_POSE_BATCH_MANIFEST_NAME = "skinned_animation_pose_batch_manifest.json"


def export_skinned_animation_pose_samples(
    zar_path: Path,
    csab_name: str,
    cmb_name: str,
    output_path: Path | None = None,
    *,
    sample_frames: list[int] | None = None,
    sample_limit: int = 25,
) -> dict[str, object]:
    archive = ZarArchive.from_path(zar_path)
    csab_file = find_archive_file(archive, csab_name)
    cmb_file = find_archive_file(archive, cmb_name)
    csab_data = archive.read_file(csab_file)
    cmb_data = archive.read_file(cmb_file)
    target_model = CmbModel.parse(cmb_data, f"{zar_path}!{cmb_file.name}")

    bind_export = export_cmb_skinned_bind_pose(
        cmb_data,
        source=f"{zar_path}!{cmb_file.name}",
        embedded_name=cmb_file.name,
        sample_limit=sample_limit,
    )
    if int(bind_export["counts"]["skinned_primitive_count"]) <= 0:  # type: ignore[index]
        raise ParseError(f"{cmb_file.name}: target CMB has no skinned primitives")

    pose_sample = skinned_animation_pose_sample_export(
        csab_data,
        target_model,
        bind_export,
        source_zar=str(zar_path),
        csab_name=csab_file.name,
        target_cmb_name=cmb_file.name,
        sample_frames=sample_frames,
        sample_limit=sample_limit,
    )
    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(
            json.dumps(pose_sample, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    return pose_sample


def batch_export_skinned_animation_pose_samples(
    actor_root: Path,
    output_dir: Path,
    *,
    sample_limit: int = 0,
) -> Path:
    if not actor_root.is_dir():
        raise ParseError(f"{actor_root}: expected an extracted OOT3D actor directory")

    output_dir.mkdir(parents=True, exist_ok=True)
    sample_dir = output_dir / "pose_samples"
    sample_dir.mkdir(parents=True, exist_ok=True)
    for old_sample in sample_dir.glob("*.json"):
        old_sample.unlink()

    records: list[dict[str, object]] = []
    considered_csab = 0
    archive_parse_failed = 0
    target_unresolved_or_missing = 0
    target_not_skinned = 0
    resolved_skinned_targets = 0
    exported = 0
    failed = 0
    support_status_counts: Counter[str] = Counter()
    exported_support_status_counts: Counter[str] = Counter()
    aggregate_counts: Counter[str] = Counter()
    aggregate_pose_counts: Counter[str] = Counter()
    aggregate_bind_counts: Counter[str] = Counter()

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
            if not (
                csab_file.type_name == "csab"
                or csab_file.name.lower().endswith(".csab")
            ):
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
                target_file = find_archive_file(archive, target_name)
                cmb_data = archive.read_file(target_file)
                bind_export = export_cmb_skinned_bind_pose(
                    cmb_data,
                    source=f"{zar_path}!{target_name}",
                    embedded_name=target_name,
                    sample_limit=sample_limit,
                )
                pose_export = skinned_animation_pose_sample_export(
                    archive.read_file(csab_file),
                    target_model,
                    bind_export,
                    source_zar=str(zar_path),
                    csab_name=csab_file.name,
                    target_cmb_name=target_name,
                    sample_frames=None,
                    sample_limit=sample_limit,
                )

                sample_path = sample_dir / skinned_animation_pose_sample_filename(
                    zar_path.relative_to(actor_root).as_posix(),
                    csab_file.name,
                    target_name,
                )
                sample_path.write_text(
                    json.dumps(pose_export, indent=2) + "\n",
                    encoding="utf-8",
                    newline="\n",
                )
                exported += 1
                exported_support_status_counts[support_status] += 1
                add_export_counts(aggregate_counts, pose_export["counts"])
                add_export_counts(aggregate_pose_counts, pose_export["pose_counts"])
                add_export_counts(aggregate_bind_counts, pose_export["bind_pose_counts"])
                records.append(
                    {
                        "status": "exported",
                        "archive_path": zar_path.relative_to(actor_root).as_posix(),
                        "csab_name": csab_file.name,
                        "target_resolution_status": target_status,
                        "target_cmb_name": target_name,
                        "target_support_status": support_status,
                        "pose_sample_export": sample_path.relative_to(output_dir).as_posix(),
                        "frame_count_candidate": pose_export["frame_count_candidate"],
                        "sample_frames": pose_export["sample_frames"],
                        "counts": pose_export["counts"],
                        "pose_counts": pose_export["pose_counts"],
                        "bind_pose_counts": pose_export["bind_pose_counts"],
                        "validation": pose_export["validation"],
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
        "format": "oot3d_skinned_animation_pose_batch_v1",
        "actor_root": str(actor_root),
        "output": str(output_dir),
        "pose_sample_dir": str(sample_dir),
        "sample_limit": sample_limit,
        "considered_csab": considered_csab,
        "archive_parse_failed": archive_parse_failed,
        "target_unresolved_or_missing": target_unresolved_or_missing,
        "target_not_skinned": target_not_skinned,
        "resolved_skinned_targets": resolved_skinned_targets,
        "exported": exported,
        "failed": failed,
        "support_status_counts": dict(sorted(support_status_counts.items())),
        "exported_support_status_counts": dict(sorted(exported_support_status_counts.items())),
        "counts": dict(sorted(aggregate_counts.items())),
        "pose_counts": dict(sorted(aggregate_pose_counts.items())),
        "bind_pose_counts": dict(sorted(aggregate_bind_counts.items())),
        "records": records,
    }
    manifest_path = output_dir / SKINNED_ANIMATION_POSE_BATCH_MANIFEST_NAME
    manifest_path.write_text(
        json.dumps(manifest, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    return manifest_path


def skinned_animation_pose_sample_export(
    csab_data: bytes,
    target_model: CmbModel,
    bind_export: dict[str, object],
    *,
    source_zar: str,
    csab_name: str,
    target_cmb_name: str,
    sample_frames: list[int] | None,
    sample_limit: int,
) -> dict[str, object]:
    pose_context = csab_pose_world_transform_samples(
        csab_data,
        target_model,
        sample_frames=sample_frames,
    )
    frame_records = []
    aggregate = Counter()
    aggregate_position_bounds: dict[str, list[float]] = {}
    max_position_delta = 0.0

    vertex_rows = list(iter_bind_pose_vertex_rows(bind_export))
    bind_world_transforms = skeleton_world_transforms(target_model.skeleton)
    inverse_bind_world_transforms = tuple(invert_affine_matrix(transform) for transform in bind_world_transforms)
    for frame_record in pose_context["frames"]:  # type: ignore[index]
        if not isinstance(frame_record, dict):
            raise ParseError(f"{csab_name}: invalid pose frame record")
        frame = int(frame_record["frame"])
        transforms = frame_record["world_transforms"]
        if not isinstance(transforms, tuple):
            raise ParseError(f"{csab_name}: frame {frame} has no world transforms")
        skin_transforms = tuple(
            multiply_matrix(pose_transform, inverse_bind_world_transforms[bone_index])
            for bone_index, pose_transform in enumerate(transforms)
        )

        frame_position_bounds: dict[str, list[float]] = {}
        frame_normal_length_bounds: dict[str, list[float]] = {}
        frame_vertex_samples: list[dict[str, object]] = []
        frame_counts = Counter()

        for row in vertex_rows:
            position, normal = weighted_pose_preview(
                list_to_vec3(row["source_position"]),
                list_to_vec3(row["source_normal"]),
                row["influences"],
                skin_transforms,
            )
            delta = vec3_distance(
                position,
                list_to_vec3(row["source_position"]),
            )
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
                add_vec3_bounds(frame_position_bounds, position)
                add_vec3_bounds(aggregate_position_bounds, position)
            if normal_finite:
                frame_counts["finite_normal_rows"] += 1
                aggregate["finite_normal_rows"] += 1
                add_scalar_bounds(frame_normal_length_bounds, "normal", vec3_length(normal))
            if position_finite and normal_finite:
                frame_counts["finite_pose_rows"] += 1
                aggregate["finite_pose_rows"] += 1

            if len(frame_vertex_samples) < sample_limit:
                frame_vertex_samples.append(
                    {
                        "mesh_index": row["mesh_index"],
                        "shape_index": row["shape_index"],
                        "primitive_index": row["primitive_index"],
                        "vertex_index": row["vertex_index"],
                        "source_position": row["source_position"],
                        "bind_pose_preview_position": row["bind_pose_preview_position"],
                        "pose_position": vec3_to_list(position),
                        "pose_normal": vec3_to_list(normal),
                        "position_delta_from_source": round_float(delta),
                        "influences": row["influences"],
                    }
                )

        frame_records.append(
            {
                "frame": frame,
                "counts": {
                    "sampled_vertex_rows": frame_counts["sampled_vertex_rows"],
                    "finite_position_rows": frame_counts["finite_position_rows"],
                    "finite_normal_rows": frame_counts["finite_normal_rows"],
                    "changed_position_rows": frame_counts["changed_position_rows"],
                    "validation_error_count": (
                        frame_counts["sampled_vertex_rows"]
                        - frame_counts["finite_pose_rows"]
                    ),
                },
                "position_bounds": rounded_vec3_bounds(frame_position_bounds),
                "normal_length_bounds": rounded_scalar_bounds(frame_normal_length_bounds),
                "vertex_samples": frame_vertex_samples,
            }
        )

    validation_error_count = aggregate["sampled_vertex_rows"] - aggregate["finite_pose_rows"]
    return {
        "format": "oot3d_skinned_animation_pose_sample_v1",
        "source_zar": source_zar,
        "csab_name": csab_name,
        "target_cmb_name": target_cmb_name,
        "target_model_name": target_model.name,
        "target_support_status": model_support_status(target_model),
        "frame_count_candidate": pose_context["frame_count_candidate"],
        "sample_frames": pose_context["sample_frames"],
        "bind_pose_format": bind_export["format"],
        "bind_pose_counts": bind_export["counts"],
        "pose_counts": pose_context["counts"],
        "counts": {
            "sampled_frames": len(frame_records),
            "source_vertex_rows": len(vertex_rows),
            "sampled_vertex_rows": aggregate["sampled_vertex_rows"],
            "finite_position_rows": aggregate["finite_position_rows"],
            "finite_normal_rows": aggregate["finite_normal_rows"],
            "finite_pose_rows": aggregate["finite_pose_rows"],
            "changed_position_rows": aggregate["changed_position_rows"],
            "max_position_delta_from_source": round_float(max_position_delta),
            "max_position_delta_from_bind": round_float(max_position_delta),
            "validation_error_count": validation_error_count,
        },
        "position_bounds": rounded_vec3_bounds(aggregate_position_bounds),
        "frames": frame_records,
        "validation": {
            "valid": validation_error_count == 0
            and int(pose_context["counts"]["non_f32_channel_blocks"]) == 0,
            "status": (
                "valid"
                if validation_error_count == 0
                and int(pose_context["counts"]["non_f32_channel_blocks"]) == 0
                else "invalid"
            ),
            "skinning_contract": (
                "weighted CMB source vertex rows sampled with "
                "pose_world * inverse(bind_world) CSAB skin matrices"
            ),
        },
    }


def csab_pose_world_transform_samples(
    data: bytes,
    target_model: CmbModel,
    *,
    sample_frames: list[int] | None,
) -> dict[str, object]:
    node_table = csab_node_table_metadata(data)
    if not node_table.get("valid"):
        raise ParseError("CSAB node table is not valid")
    table_arrays = csab_node_table_arrays(data)
    if table_arrays is None:
        raise ParseError("CSAB node table arrays are missing")

    header_candidates = csab_header_candidates(data)
    frame_count = header_candidates.get("frame_count_candidate")
    skeleton_bone_count = header_candidates.get("skeleton_bone_count_candidate")
    if frame_count is None or skeleton_bone_count is None:
        raise ParseError("CSAB playback header candidates are incomplete")
    if target_model.bone_count != skeleton_bone_count:
        raise ParseError(
            f"target CMB bone count {target_model.bone_count} does not match "
            f"CSAB skeleton count {skeleton_bone_count}"
        )

    frames = normalized_sample_frames(sample_frames, frame_count)
    bone_to_node_indices, node_offsets = table_arrays
    constant_translation_slots_by_node = csab_constant_translation_slots_by_node(
        data,
        node_offsets,
    )
    counts = Counter()
    slot_sample_counts: Counter[str] = Counter()
    frame_records: list[dict[str, object]] = []

    for frame in frames:
        channel_values_by_node, frame_metadata = csab_channel_values_by_node(
            data,
            node_offsets,
            float(frame),
        )
        counts["sampled_channel_values"] += int(frame_metadata["sampled_channel_values"])
        counts["finite_channel_values"] += int(frame_metadata["finite_channel_values"])
        counts["non_f32_channel_blocks"] += int(frame_metadata["non_f32_channel_blocks"])
        slot_sample_counts.update(frame_metadata["slot_sample_counts"])  # type: ignore[arg-type]

        local_transforms: list[Matrix4] = []
        animated_bone_transforms = 0
        for bone_index, bone in enumerate(target_model.skeleton.bones):
            node_index = bone_to_node_indices[bone_index]
            values = channel_values_by_node.get(node_index, {}) if node_index != 0xFFFF else {}
            if node_index != 0xFFFF:
                animated_bone_transforms += 1
                values, skipped_constant_translation_values = target_compatible_channel_values(
                    bone,
                    values,
                    constant_translation_slots_by_node.get(node_index, set()),
                )
                counts["target_incompatible_constant_translation_values"] += skipped_constant_translation_values
            pose_bone = csab_pose_bone_from_channel_values(bone, values)
            local_transforms.append(bone_local_transform(pose_bone))

        world_transforms = compose_world_transforms(
            target_model.skeleton,
            tuple(local_transforms),
        )
        finite_world_matrix_entries = finite_matrix_entries(world_transforms)
        counts["sampled_pose_frames"] += 1
        counts["sampled_skeleton_bone_transforms"] += target_model.bone_count
        counts["sampled_animated_bone_transforms"] += animated_bone_transforms
        counts["world_matrix_entries"] += target_model.bone_count * 16
        counts["finite_world_matrix_entries"] += finite_world_matrix_entries
        frame_records.append(
            {
                "frame": frame,
                "world_transforms": world_transforms,
                "finite_world_matrix_entries": finite_world_matrix_entries,
            }
        )

    return {
        "frame_count_candidate": frame_count,
        "sample_frames": frames,
        "counts": {
            "sampled_pose_frames": counts["sampled_pose_frames"],
            "sampled_skeleton_bone_transforms": counts[
                "sampled_skeleton_bone_transforms"
            ],
            "sampled_animated_bone_transforms": counts[
                "sampled_animated_bone_transforms"
            ],
            "sampled_channel_values": counts["sampled_channel_values"],
            "finite_channel_values": counts["finite_channel_values"],
            "non_f32_channel_blocks": counts["non_f32_channel_blocks"],
            "world_matrix_entries": counts["world_matrix_entries"],
            "finite_world_matrix_entries": counts["finite_world_matrix_entries"],
            "target_incompatible_constant_translation_values": counts[
                "target_incompatible_constant_translation_values"
            ],
            "slot_sample_counts": dict(sorted(slot_sample_counts.items())),
        },
        "frames": frame_records,
    }


def csab_constant_translation_slots_by_node(
    data: bytes,
    node_offsets: list[int],
) -> dict[int, set[int]]:
    slots_by_node: dict[int, set[int]] = {}
    for node_index, relative_offset in enumerate(node_offsets):
        record_start = CSAB_NODE_OFFSET_BASE + relative_offset
        next_record_start = (
            CSAB_NODE_OFFSET_BASE + node_offsets[node_index + 1]
            if node_index + 1 < len(node_offsets)
            else len(data)
        )
        active_offsets = csab_anod_active_channel_offsets(data, record_start)
        for active_index, (slot, channel_offset) in enumerate(active_offsets):
            if slot not in (0, 1, 2):
                continue
            block_start = record_start + channel_offset
            block_end = (
                record_start + active_offsets[active_index + 1][1]
                if active_index + 1 < len(active_offsets)
                else next_record_start
            )
            block_class, _key_count = csab_anod_channel_block_class(data, block_start, block_end)
            if block_class == "type1_f32_const_len24":
                slots_by_node.setdefault(node_index, set()).add(slot)
    return slots_by_node


def target_compatible_channel_values(
    bone,
    values: dict[int, float],
    constant_translation_slots: set[int],
) -> tuple[dict[int, float], int]:
    if not constant_translation_slots:
        return values, 0

    out: dict[int, float] | None = None
    skipped = 0
    base_values = (bone.translation.x, bone.translation.y, bone.translation.z)
    for slot in constant_translation_slots:
        value = values.get(slot)
        if value is None:
            continue
        base_value = float(base_values[slot])
        if abs(float(value) - base_value) <= constant_translation_bind_tolerance(base_value):
            continue
        if out is None:
            out = dict(values)
        out.pop(slot, None)
        skipped += 1
    return (out if out is not None else values), skipped


def constant_translation_bind_tolerance(base_value: float) -> float:
    return max(
        CONSTANT_TRANSLATION_BIND_ABSOLUTE_TOLERANCE,
        abs(base_value) * CONSTANT_TRANSLATION_BIND_RELATIVE_TOLERANCE,
    )


def normalized_sample_frames(sample_frames: list[int] | None, frame_count: int) -> list[int]:
    if sample_frames is None:
        sample_frames = [0, frame_count // 2, frame_count]
    frames = sorted(set(int(frame) for frame in sample_frames))
    if not frames:
        raise ParseError("at least one sample frame is required")
    invalid = [frame for frame in frames if frame < 0 or frame > frame_count]
    if invalid:
        raise ParseError(
            f"sample frames outside 0..{frame_count}: "
            + ", ".join(str(frame) for frame in invalid)
        )
    return frames


def iter_bind_pose_vertex_rows(bind_export: dict[str, object]):
    for mesh in bind_export["meshes"]:  # type: ignore[index]
        for primitive in mesh["primitives"]:  # type: ignore[index]
            for vertex in primitive["vertices"]:
                yield {
                    "mesh_index": mesh["mesh_index"],
                    "shape_index": mesh["shape_index"],
                    "primitive_index": primitive["primitive_index"],
                    **vertex,
                }


def weighted_pose_preview(
    position: Vec3,
    normal: Vec3,
    influences: object,
    bone_transforms: tuple[Matrix4, ...],
) -> tuple[Vec3, Vec3]:
    if not isinstance(influences, list):
        raise ParseError("skinned vertex row has no influence list")
    out_position = Vec3(0.0, 0.0, 0.0)
    out_normal = Vec3(0.0, 0.0, 0.0)
    for influence in influences:
        if not isinstance(influence, dict):
            continue
        bone_index = influence.get("bone_index")
        weight = float(influence.get("weight", 0.0))
        if bone_index is None or weight <= 0.0:
            continue
        bone_index = int(bone_index)
        if bone_index < 0 or bone_index >= len(bone_transforms):
            continue
        transformed_position = transform_position(bone_transforms[bone_index], position)
        transformed_normal = transform_direction(bone_transforms[bone_index], normal)
        out_position = Vec3(
            out_position.x + transformed_position.x * weight,
            out_position.y + transformed_position.y * weight,
            out_position.z + transformed_position.z * weight,
        )
        out_normal = Vec3(
            out_normal.x + transformed_normal.x * weight,
            out_normal.y + transformed_normal.y * weight,
            out_normal.z + transformed_normal.z * weight,
        )
    return out_position, normalize_or_default(out_normal, normal)


def invert_affine_matrix(matrix: Matrix4) -> Matrix4:
    a00, a01, a02 = matrix[0][0], matrix[0][1], matrix[0][2]
    a10, a11, a12 = matrix[1][0], matrix[1][1], matrix[1][2]
    a20, a21, a22 = matrix[2][0], matrix[2][1], matrix[2][2]
    det = (
        a00 * (a11 * a22 - a12 * a21)
        - a01 * (a10 * a22 - a12 * a20)
        + a02 * (a10 * a21 - a11 * a20)
    )
    if abs(det) <= 0.000000001:
        raise ParseError("cannot invert singular bind-pose bone matrix")
    inv_det = 1.0 / det
    r00 = (a11 * a22 - a12 * a21) * inv_det
    r01 = (a02 * a21 - a01 * a22) * inv_det
    r02 = (a01 * a12 - a02 * a11) * inv_det
    r10 = (a12 * a20 - a10 * a22) * inv_det
    r11 = (a00 * a22 - a02 * a20) * inv_det
    r12 = (a02 * a10 - a00 * a12) * inv_det
    r20 = (a10 * a21 - a11 * a20) * inv_det
    r21 = (a01 * a20 - a00 * a21) * inv_det
    r22 = (a00 * a11 - a01 * a10) * inv_det

    tx, ty, tz = matrix[0][3], matrix[1][3], matrix[2][3]
    return (
        (r00, r01, r02, -(r00 * tx + r01 * ty + r02 * tz)),
        (r10, r11, r12, -(r10 * tx + r11 * ty + r12 * tz)),
        (r20, r21, r22, -(r20 * tx + r21 * ty + r22 * tz)),
        (0.0, 0.0, 0.0, 1.0),
    )


def finite_matrix_entries(transforms: tuple[Matrix4, ...]) -> int:
    return sum(
        1
        for transform in transforms
        for row in transform
        for value in row
        if math.isfinite(value)
    )


def list_to_vec3(value: object) -> Vec3:
    if not isinstance(value, list) or len(value) != 3:
        raise ParseError("expected a 3-component vector")
    return Vec3(float(value[0]), float(value[1]), float(value[2]))


def vec3_distance(left: Vec3, right: Vec3) -> float:
    dx = left.x - right.x
    dy = left.y - right.y
    dz = left.z - right.z
    return math.sqrt(dx * dx + dy * dy + dz * dz)


def vec3_length(value: Vec3) -> float:
    return math.sqrt(value.x * value.x + value.y * value.y + value.z * value.z)


def add_vec3_bounds(bounds: dict[str, list[float]], value: Vec3) -> None:
    add_scalar_bounds(bounds, "x", value.x)
    add_scalar_bounds(bounds, "y", value.y)
    add_scalar_bounds(bounds, "z", value.z)


def add_scalar_bounds(bounds: dict[str, list[float]], key: str, value: float) -> None:
    current = bounds.get(key)
    if current is None:
        bounds[key] = [value, value]
    else:
        current[0] = min(current[0], value)
        current[1] = max(current[1], value)


def rounded_vec3_bounds(bounds: dict[str, list[float]]) -> dict[str, dict[str, float]]:
    return {
        axis: {"min": round_float(values[0]), "max": round_float(values[1])}
        for axis, values in sorted(bounds.items())
    }


def rounded_scalar_bounds(bounds: dict[str, list[float]]) -> dict[str, dict[str, float]]:
    return {
        key: {"min": round_float(values[0]), "max": round_float(values[1])}
        for key, values in sorted(bounds.items())
    }


def add_export_counts(target: Counter[str], source: object) -> None:
    if not isinstance(source, dict):
        return
    for key, value in source.items():
        if isinstance(value, int) and not isinstance(value, bool):
            target[str(key)] += value


def skinned_animation_pose_sample_filename(
    archive_path: str,
    csab_name: str,
    cmb_name: str,
) -> str:
    return (
        f"{safe_path_part(archive_path)}__"
        f"{safe_path_part(csab_name)}__"
        f"{safe_path_part(cmb_name)}.json"
    )
