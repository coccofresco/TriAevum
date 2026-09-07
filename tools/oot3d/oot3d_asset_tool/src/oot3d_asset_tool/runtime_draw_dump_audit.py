from __future__ import annotations

from collections import Counter
import json
import math
from pathlib import Path
import struct

from .binary import ParseError
from .character_conversion_package import (
    bind_pose_skeleton_bones,
    csab_channel_applies_to_target,
    json_int,
    rigid_primitive_bone_index,
    selected_primitive_keys,
    sample_csab_track_channel,
)
from .skinned_animation import list_to_vec3, vec3_distance
from .animation_glb_export import weighted_skin_pose
from .legacy_fast_resource import transform_direction


RUNTIME_DRAW_DUMP_AUDIT_FORMAT = "oot3d_link_child_runtime_draw_dump_audit_v1"
PICA_TEXTURE_WRAP_REPEAT = 0x2901
FNV64_OFFSET = 14695981039346656037
FNV64_PRIME = 1099511628211
U64_MASK = (1 << 64) - 1


def audit_runtime_draw_dump(
    character_manifest_path: Path,
    runtime_dump_path: Path,
    output: Path,
    *,
    track_export_path: Path | None = None,
    segment_continuity_audit_path: Path | None = None,
    anb_semantic_audit_path: Path | None = None,
    position_tolerance: float = 1.0,
    normal_tolerance: float = 0.0001,
    sample_limit: int = 100,
) -> dict[str, object]:
    if not math.isfinite(position_tolerance) or position_tolerance < 0.0:
        raise ParseError(f"position tolerance must be finite and non-negative, got {position_tolerance!r}")
    if not math.isfinite(normal_tolerance) or normal_tolerance < 0.0:
        raise ParseError(f"normal tolerance must be finite and non-negative, got {normal_tolerance!r}")

    character_manifest = read_json(character_manifest_path)
    runtime_dump = read_json(runtime_dump_path)
    if character_manifest.get("format") != "oot3d_character_conversion_manifest_v1":
        raise ParseError(f"{character_manifest_path}: unexpected character conversion manifest format")
    if runtime_dump.get("format") != "oot3d_link_child_runtime_draw_dump_v1":
        raise ParseError(f"{runtime_dump_path}: unexpected runtime draw dump format")

    csab_name = normalize_resource_path(runtime_dump.get("csab_name"))
    frame = json_int(runtime_dump.get("frame"), -1)
    if not csab_name:
        raise ParseError("runtime draw dump has no csab_name")
    if frame < 0:
        raise ParseError("runtime draw dump has no valid frame")
    segment_normalization_offset = segment_offset_for_csab(
        read_json(segment_continuity_audit_path) if segment_continuity_audit_path is not None else None,
        csab_name,
    )
    anb_semantic_audit = read_json(anb_semantic_audit_path) if anb_semantic_audit_path is not None else None

    source_manifests = require_dict(character_manifest.get("source_manifests"), "character source manifests")
    target = link_child_target_from_binding(Path(str(source_manifests.get("skinned_animation_binding"))))
    bind_pose_json = read_json(Path(str(target["bind_pose"]["export"])))
    native_manifest = read_json(Path(str(source_manifests.get("skinned_bind_pose_native"))))
    if track_export_path is None:
        track_record = animation_record_by_csab(target, csab_name)
        resolved_track_export_path = Path(str(track_record["track_export_file"]))
    else:
        resolved_track_export_path = track_export_path
    track_json = read_json(resolved_track_export_path)
    track_csab_name = normalize_resource_path(track_json.get("csab_name"))
    if track_csab_name != csab_name:
        raise ParseError(
            f"{resolved_track_export_path}: track CSAB {track_csab_name!r} does not match runtime dump {csab_name!r}"
        )

    expected_primitives = expected_runtime_primitive_samples(
        bind_pose_json,
        native_manifest,
        track_json,
        frame=frame,
        segment_normalization_offset=segment_normalization_offset,
    )
    actual_primitives = runtime_dump_primitives(runtime_dump)
    comparison = compare_runtime_primitive_samples(
        actual_primitives,
        expected_primitives,
        runtime_dump=runtime_dump,
        expected_segment_normalization_offset=segment_normalization_offset,
        expected_anb_semantic_audit=anb_semantic_audit,
        position_tolerance=position_tolerance,
        normal_tolerance=normal_tolerance,
        sample_limit=sample_limit,
    )

    status = "valid" if comparison["issue_count"] == 0 else "invalid"
    audit = {
        "format": RUNTIME_DRAW_DUMP_AUDIT_FORMAT,
        "status": status,
        "character_manifest": str(character_manifest_path),
        "runtime_dump": str(runtime_dump_path),
        "csab_name": csab_name,
        "n64_animation_name": runtime_dump.get("n64_animation_name"),
        "selection_source": runtime_dump.get("selection_source"),
        "animation_resource_path": runtime_dump.get("animation_resource_path"),
        "track_export": str(resolved_track_export_path),
        "frame": frame,
        "frame_slots": runtime_dump.get("frame_slots"),
        "segment_continuity_audit": str(segment_continuity_audit_path) if segment_continuity_audit_path else None,
        "anb_semantic_audit": str(anb_semantic_audit_path) if anb_semantic_audit_path else None,
        "expected_segment_normalization_offset": rounded_vec3(segment_normalization_offset),
        "actual_segment_normalization_offset": runtime_dump.get("segment_normalization_offset"),
        "actual_has_segment_normalization_offset": runtime_dump.get("has_segment_normalization_offset"),
        "actual_anb_runtime_state": runtime_dump.get("anb_runtime_state"),
        "position_tolerance": position_tolerance,
        "normal_tolerance": normal_tolerance,
        "expected_primitive_count": len(expected_primitives),
        "actual_primitive_count": len(actual_primitives),
        "comparison": comparison,
        "contract": (
            "The in-game C++ runtime dump is compared against the same selected primitive set and CSAB frame "
            "computed offline from the canonical character conversion inputs. Bounds and sampled vertex rows must "
            "match per mesh_index/primitive_index before a visual runtime test is treated as trustworthy."
        ),
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(audit, indent=2), encoding="utf-8", newline="\n")
    return audit


def expected_runtime_primitive_samples(
    bind_pose_json: dict[str, object],
    native_manifest: dict[str, object],
    track_json: dict[str, object],
    *,
    frame: int,
    segment_normalization_offset,
) -> dict[tuple[int, int], dict[str, object]]:
    selected_keys = selected_primitive_keys(native_manifest)
    primitives = selected_primitives_with_mesh(bind_pose_json, selected_keys)
    skeleton_bones = bind_pose_skeleton_bones(bind_pose_json)
    inverse_bind_world_transforms = tuple(
        matrix_invert_affine_runtime_float(matrix)
        for _parent, _translation, _rotation, _scale, matrix in skeleton_bones
    )
    tracks = track_json.get("tracks", [])
    if not isinstance(tracks, list):
        raise ParseError("CSAB track export has no track array")
    frame_count_candidate = json_int(track_json.get("frame_count_candidate"), -1)
    if frame_count_candidate >= 0 and frame > frame_count_candidate:
        raise ParseError("runtime draw dump frame is outside CSAB frame domain")

    world_transforms = selected_draw_world_transforms_runtime_float(skeleton_bones, tracks, frame)
    skin_transforms = tuple(
        matrix_multiply_runtime_float(world_transforms[bone_index], inverse_bind_world_transforms[bone_index])
        for bone_index in range(len(world_transforms))
    )

    out: dict[tuple[int, int], dict[str, object]] = {}
    uv_orientation = normalize_resource_path(native_manifest.get("uv_orientation") or "normal")
    for mesh_index, primitive_index, material_index, primitive in primitives:
        out[(mesh_index, primitive_index)] = primitive_sample(
            mesh_index,
            primitive_index,
            material_index,
            primitive,
            bind_pose_json=bind_pose_json,
            uv_orientation=uv_orientation,
            world_transforms=world_transforms,
            skin_transforms=skin_transforms,
            segment_normalization_offset=segment_normalization_offset,
        )
    return out


def primitive_sample(
    mesh_index: int,
    primitive_index: int,
    material_index: int,
    primitive: dict[str, object],
    *,
    bind_pose_json: dict[str, object],
    uv_orientation: str,
    world_transforms,
    skin_transforms,
    segment_normalization_offset,
) -> dict[str, object]:
    vertices = require_list(primitive.get("vertices"), "runtime primitive vertices")
    indices = require_list(primitive.get("indices"), "runtime primitive indices")
    skinning_mode = json_int(primitive.get("skinning_mode"), 0)
    rigid_bone_index = rigid_primitive_bone_index(primitive) if skinning_mode == 0 else -1
    if skinning_mode == 0 and (rigid_bone_index < 0 or rigid_bone_index >= len(world_transforms)):
        raise ParseError("selected runtime rigid primitive bone is outside sampled skeleton")

    counts = Counter()
    max_position_delta = 0.0
    bounds: dict[str, list[float]] = {}
    sample_vertices: list[dict[str, object]] = []
    packed_row_prefixes: list[dict[str, object]] = []
    max_delta_vertex: dict[str, object] | None = None
    material = runtime_material_info(bind_pose_json, material_index)
    texture = runtime_texture_info(bind_pose_json, int(material["primary_texture_index"]))
    uv_offset = runtime_primitive_uv_offset(primitive, material, uv_orientation)
    hashes = {
        "vertex_row_hash": FNV64_OFFSET,
        "packed_position_hash": FNV64_OFFSET,
        "packed_texcoord_hash": FNV64_OFFSET,
        "packed_color_normal_hash": FNV64_OFFSET,
        "source_index_hash": FNV64_OFFSET,
    }
    for row, index in enumerate(indices):
        if not isinstance(index, int) or index < 0 or index >= len(vertices):
            raise ParseError("selected runtime primitive index is outside its vertex array")
        vertex = vertices[index]
        if not isinstance(vertex, dict):
            raise ParseError("selected runtime primitive vertex is not an object")
        source_position = list_to_vec3(vertex.get("source_position"))
        source_normal = list_to_vec3(vertex.get("source_normal"))
        if skinning_mode == 0:
            position = transform_position_runtime_float(world_transforms[rigid_bone_index], source_position)
            normal = transform_direction(world_transforms[rigid_bone_index], source_normal)
        else:
            position = weighted_position_runtime_float(
                source_position,
                vertex.get("influences"),
                skin_transforms,
            )
            _double_position, normal = weighted_skin_pose(
                source_position,
                source_normal,
                vertex.get("influences"),
                skin_transforms,
            )
        position = apply_segment_normalization_offset_runtime_float(position, segment_normalization_offset)
        position_finite = vec3_is_finite(position)
        normal_finite = vec3_is_finite(normal)
        delta = vec3_distance(position, source_position)
        counts["vertex_rows"] += 1
        if delta > 0.0001:
            counts["changed_position_rows"] += 1
        if position_finite and normal_finite:
            counts["finite_pose_rows"] += 1
        if position_finite:
            add_vec3_bounds(bounds, position)
        sample = {
            "triangle_index": row // 3,
            "corner_index": row % 3,
            "vertex_index": json_int(vertex.get("vertex_index"), -1),
            "position": rounded_vec3(position),
            "normal": rounded_vec3(normal),
            "delta_from_bind": round_float(delta),
        }
        packed = packed_runtime_vertex(
            vertex,
            position,
            normal,
            material,
            texture,
            uv_orientation,
            uv_offset,
        )
        hash_packed_vertex_row(hashes, row, sample["vertex_index"], packed)
        packed_row_prefixes.append(packed_row_prefix_sample(row, sample, packed, hashes))
        if len(sample_vertices) < 4:
            sample_vertices.append(sample)
        if delta >= max_position_delta:
            max_position_delta = delta
            max_delta_vertex = sample

    return {
        "mesh_index": mesh_index,
        "primitive_index": primitive_index,
        "skinning_mode": skinning_mode,
        "rigid_bone_index": rigid_bone_index,
        "triangle_count": len(indices) // 3,
        "vertex_rows": counts["vertex_rows"],
        "finite_pose_rows": counts["finite_pose_rows"],
        "changed_position_rows": counts["changed_position_rows"],
        "validation_error_count": counts["vertex_rows"] - counts["finite_pose_rows"],
        "max_position_delta_from_bind": round_float(max_position_delta),
        "position_bounds": rounded_bounds(bounds),
        "vertex_row_hash": str(hashes["vertex_row_hash"]),
        "packed_position_hash": str(hashes["packed_position_hash"]),
        "packed_texcoord_hash": str(hashes["packed_texcoord_hash"]),
        "packed_color_normal_hash": str(hashes["packed_color_normal_hash"]),
        "source_index_hash": str(hashes["source_index_hash"]),
        "packed_row_prefixes": packed_row_prefixes,
        "sample_vertices": sample_vertices,
        "max_delta_vertex": max_delta_vertex,
    }


def runtime_float(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", float(value)))[0]


def vec3_like(reference, x: float, y: float, z: float):
    return type(reference)(x, y, z)


def vec3_runtime_float(value):
    return vec3_like(value, runtime_float(value.x), runtime_float(value.y), runtime_float(value.z))


def matrix_runtime_float(matrix):
    return tuple(tuple(runtime_float(item) for item in row) for row in matrix)


def matrix_identity_runtime_float():
    return (
        (1.0, 0.0, 0.0, 0.0),
        (0.0, 1.0, 0.0, 0.0),
        (0.0, 0.0, 1.0, 0.0),
        (0.0, 0.0, 0.0, 1.0),
    )


def matrix_multiply_runtime_float(left, right):
    left = matrix_runtime_float(left)
    right = matrix_runtime_float(right)
    rows: list[tuple[float, float, float, float]] = []
    for row in range(4):
        values: list[float] = []
        for col in range(4):
            value = runtime_float(0.0)
            for k_index in range(4):
                product = runtime_float(runtime_float(left[row][k_index]) * runtime_float(right[k_index][col]))
                value = runtime_float(value + product)
            values.append(value)
        rows.append(tuple(values))  # type: ignore[arg-type]
    return tuple(rows)


def matrix_scale_runtime_float(scale):
    scale = vec3_runtime_float(scale)
    return (
        (scale.x, 0.0, 0.0, 0.0),
        (0.0, scale.y, 0.0, 0.0),
        (0.0, 0.0, scale.z, 0.0),
        (0.0, 0.0, 0.0, 1.0),
    )


def matrix_translate_runtime_float(translation):
    translation = vec3_runtime_float(translation)
    return (
        (1.0, 0.0, 0.0, translation.x),
        (0.0, 1.0, 0.0, translation.y),
        (0.0, 0.0, 1.0, translation.z),
        (0.0, 0.0, 0.0, 1.0),
    )


def matrix_rotate_x_runtime_float(angle: float):
    angle = runtime_float(angle)
    sine = runtime_float(math.sin(angle))
    cosine = runtime_float(math.cos(angle))
    return (
        (1.0, 0.0, 0.0, 0.0),
        (0.0, cosine, runtime_float(-sine), 0.0),
        (0.0, sine, cosine, 0.0),
        (0.0, 0.0, 0.0, 1.0),
    )


def matrix_rotate_y_runtime_float(angle: float):
    angle = runtime_float(angle)
    sine = runtime_float(math.sin(angle))
    cosine = runtime_float(math.cos(angle))
    return (
        (cosine, 0.0, sine, 0.0),
        (0.0, 1.0, 0.0, 0.0),
        (runtime_float(-sine), 0.0, cosine, 0.0),
        (0.0, 0.0, 0.0, 1.0),
    )


def matrix_rotate_z_runtime_float(angle: float):
    angle = runtime_float(angle)
    sine = runtime_float(math.sin(angle))
    cosine = runtime_float(math.cos(angle))
    return (
        (cosine, runtime_float(-sine), 0.0, 0.0),
        (sine, cosine, 0.0, 0.0),
        (0.0, 0.0, 1.0, 0.0),
        (0.0, 0.0, 0.0, 1.0),
    )


def local_trs_matrix_runtime_float(translation, rotation, scale):
    return matrix_multiply_runtime_float(
        matrix_translate_runtime_float(translation),
        matrix_multiply_runtime_float(
            matrix_rotate_z_runtime_float(rotation.z),
            matrix_multiply_runtime_float(
                matrix_rotate_y_runtime_float(rotation.y),
                matrix_multiply_runtime_float(matrix_rotate_x_runtime_float(rotation.x), matrix_scale_runtime_float(scale)),
            ),
        ),
    )


def matrix_invert_affine_runtime_float(matrix):
    matrix = matrix_runtime_float(matrix)
    a00 = matrix[0][0]
    a01 = matrix[0][1]
    a02 = matrix[0][2]
    a10 = matrix[1][0]
    a11 = matrix[1][1]
    a12 = matrix[1][2]
    a20 = matrix[2][0]
    a21 = matrix[2][1]
    a22 = matrix[2][2]

    det = runtime_float(
        runtime_float(a00 * runtime_float(runtime_float(a11 * a22) - runtime_float(a12 * a21)))
        - runtime_float(a01 * runtime_float(runtime_float(a10 * a22) - runtime_float(a12 * a20)))
        + runtime_float(a02 * runtime_float(runtime_float(a10 * a21) - runtime_float(a11 * a20)))
    )
    if abs(det) <= 0.000000001:
        raise ParseError("bind-pose skeleton bone has a singular bind_world_matrix")
    inv_det = runtime_float(1.0 / det)
    r00 = runtime_float(runtime_float(runtime_float(a11 * a22) - runtime_float(a12 * a21)) * inv_det)
    r01 = runtime_float(runtime_float(runtime_float(a02 * a21) - runtime_float(a01 * a22)) * inv_det)
    r02 = runtime_float(runtime_float(runtime_float(a01 * a12) - runtime_float(a02 * a11)) * inv_det)
    r10 = runtime_float(runtime_float(runtime_float(a12 * a20) - runtime_float(a10 * a22)) * inv_det)
    r11 = runtime_float(runtime_float(runtime_float(a00 * a22) - runtime_float(a02 * a20)) * inv_det)
    r12 = runtime_float(runtime_float(runtime_float(a02 * a10) - runtime_float(a00 * a12)) * inv_det)
    r20 = runtime_float(runtime_float(runtime_float(a10 * a21) - runtime_float(a11 * a20)) * inv_det)
    r21 = runtime_float(runtime_float(runtime_float(a01 * a20) - runtime_float(a00 * a21)) * inv_det)
    r22 = runtime_float(runtime_float(runtime_float(a00 * a11) - runtime_float(a01 * a10)) * inv_det)
    tx = matrix[0][3]
    ty = matrix[1][3]
    tz = matrix[2][3]
    return (
        (r00, r01, r02, runtime_float(-runtime_float(runtime_float(r00 * tx) + runtime_float(r01 * ty) + runtime_float(r02 * tz)))),
        (r10, r11, r12, runtime_float(-runtime_float(runtime_float(r10 * tx) + runtime_float(r11 * ty) + runtime_float(r12 * tz)))),
        (r20, r21, r22, runtime_float(-runtime_float(runtime_float(r20 * tx) + runtime_float(r21 * ty) + runtime_float(r22 * tz)))),
        (0.0, 0.0, 0.0, 1.0),
    )


def selected_draw_world_transforms_runtime_float(skeleton_bones, tracks: list[object], frame: int):
    translations = [vec3_runtime_float(bone[1]) for bone in skeleton_bones]
    rotations = [vec3_runtime_float(bone[2]) for bone in skeleton_bones]
    scales = [vec3_runtime_float(bone[3]) for bone in skeleton_bones]

    for track in tracks:
        if not isinstance(track, dict):
            raise ParseError("CSAB track record is not an object")
        bone_index = json_int(track.get("bone_index"))
        if bone_index < 0 or bone_index >= len(skeleton_bones):
            raise ParseError("CSAB track references a bone outside the skeleton")
        translation = vec3_runtime_float(skeleton_bones[bone_index][1])
        rotation = vec3_runtime_float(skeleton_bones[bone_index][2])
        scale = vec3_runtime_float(skeleton_bones[bone_index][3])
        channels = track.get("channels", [])
        if not isinstance(channels, list):
            raise ParseError("CSAB track has no channel array")
        for channel in channels:
            if not isinstance(channel, dict):
                continue
            value = runtime_float(sample_csab_track_channel(channel, runtime_float(float(frame))))
            if math.isfinite(value):
                if not csab_channel_applies_to_target(channel, skeleton_bones[bone_index][1], value):
                    continue
                slot = json_int(channel.get("slot"))
                if slot == 0:
                    translation = vec3_like(translation, value, translation.y, translation.z)
                elif slot == 1:
                    translation = vec3_like(translation, translation.x, value, translation.z)
                elif slot == 2:
                    translation = vec3_like(translation, translation.x, translation.y, value)
                elif slot == 3:
                    rotation = vec3_like(rotation, value, rotation.y, rotation.z)
                elif slot == 4:
                    rotation = vec3_like(rotation, rotation.x, value, rotation.z)
                elif slot == 5:
                    rotation = vec3_like(rotation, rotation.x, rotation.y, value)
                elif slot == 6:
                    scale = vec3_like(scale, value, scale.y, scale.z)
                elif slot == 7:
                    scale = vec3_like(scale, scale.x, value, scale.z)
                elif slot == 8:
                    scale = vec3_like(scale, scale.x, scale.y, value)
        translations[bone_index] = vec3_runtime_float(translation)
        rotations[bone_index] = vec3_runtime_float(rotation)
        scales[bone_index] = vec3_runtime_float(scale)

    world = []
    for bone_index, (parent_index, _translation, _rotation, _scale, _matrix) in enumerate(skeleton_bones):
        local = local_trs_matrix_runtime_float(translations[bone_index], rotations[bone_index], scales[bone_index])
        if parent_index < 0:
            world.append(local)
        else:
            if parent_index >= bone_index:
                raise ParseError("bind-pose skeleton parent order is not topological")
            world.append(matrix_multiply_runtime_float(world[parent_index], local))
    return tuple(world)


def runtime_dot4(a: float, b: float, c: float, d: float) -> float:
    value = runtime_float(0.0)
    for item in (a, b, c, d):
        value = runtime_float(value + runtime_float(item))
    return value


def transform_position_runtime_float(transform, position):
    transform = matrix_runtime_float(transform)
    x = runtime_float(position.x)
    y = runtime_float(position.y)
    z = runtime_float(position.z)
    return vec3_like(
        position,
        runtime_dot4(
            runtime_float(transform[0][0] * x),
            runtime_float(transform[0][1] * y),
            runtime_float(transform[0][2] * z),
            transform[0][3],
        ),
        runtime_dot4(
            runtime_float(transform[1][0] * x),
            runtime_float(transform[1][1] * y),
            runtime_float(transform[1][2] * z),
            transform[1][3],
        ),
        runtime_dot4(
            runtime_float(transform[2][0] * x),
            runtime_float(transform[2][1] * y),
            runtime_float(transform[2][2] * z),
            transform[2][3],
        ),
    )


def weighted_position_runtime_float(position, influences: object, skin_transforms):
    if not isinstance(influences, list):
        raise ParseError("skinned vertex row has no influence list")
    out = vec3_like(position, 0.0, 0.0, 0.0)
    for influence in influences:
        if not isinstance(influence, dict):
            continue
        bone_index = json_int(influence.get("bone_index"), -1)
        weight = runtime_float(float(influence.get("weight", 0.0) or 0.0))
        if bone_index < 0 or bone_index >= len(skin_transforms) or weight <= 0.0:
            continue
        transformed = transform_position_runtime_float(skin_transforms[bone_index], position)
        out = vec3_like(
            position,
            runtime_float(out.x + runtime_float(transformed.x * weight)),
            runtime_float(out.y + runtime_float(transformed.y * weight)),
            runtime_float(out.z + runtime_float(transformed.z * weight)),
        )
    return out


def apply_segment_normalization_offset_runtime_float(position, segment_normalization_offset):
    return vec3_like(
        position,
        runtime_float(runtime_float(position.x) + runtime_float(segment_normalization_offset.x)),
        runtime_float(runtime_float(position.y) + runtime_float(segment_normalization_offset.y)),
        runtime_float(runtime_float(position.z) + runtime_float(segment_normalization_offset.z)),
    )


def compare_runtime_primitive_samples(
    actual: dict[tuple[int, int], dict[str, object]],
    expected: dict[tuple[int, int], dict[str, object]],
    *,
    runtime_dump: dict[str, object],
    expected_segment_normalization_offset,
    expected_anb_semantic_audit: dict[str, object] | None,
    position_tolerance: float,
    normal_tolerance: float,
    sample_limit: int,
) -> dict[str, object]:
    issues: list[dict[str, object]] = []
    accepted_boundary_mismatches: list[dict[str, object]] = []
    counts = Counter()
    max_bound_delta = 0.0
    max_bound_delta_record: dict[str, object] | None = None
    segment_offset_comparison = compare_segment_normalization_offset(
        runtime_dump,
        expected_segment_normalization_offset,
        position_tolerance,
    )
    for issue in segment_offset_comparison["issues"]:
        if len(issues) < sample_limit:
            issues.append(issue)
    counts.update(segment_offset_comparison["counts"])
    anb_comparison = compare_anb_runtime_state(runtime_dump, expected_anb_semantic_audit)
    for issue in anb_comparison["issues"]:
        if len(issues) < sample_limit:
            issues.append(issue)
    counts.update(anb_comparison["counts"])
    actual_keys = set(actual)
    expected_keys = set(expected)
    for key in sorted(expected_keys - actual_keys):
        add_issue(issues, sample_limit, "missing_actual_primitive", key)
    for key in sorted(actual_keys - expected_keys):
        add_issue(issues, sample_limit, "extra_actual_primitive", key)

    for key in sorted(actual_keys & expected_keys):
        actual_record = actual[key]
        expected_record = expected[key]
        counts["matched_primitive_count"] += 1
        for field in (
            "skinning_mode",
            "triangle_count",
            "vertex_rows",
            "finite_pose_rows",
            "changed_position_rows",
            "validation_error_count",
        ):
            if actual_record.get(field) != expected_record.get(field):
                add_issue(
                    issues,
                    sample_limit,
                    f"{field}_mismatch",
                    key,
                    actual=actual_record.get(field),
                    expected=expected_record.get(field),
                )
        for axis in ("x", "y", "z"):
            for edge in ("min", "max"):
                actual_value = bounds_value(actual_record.get("position_bounds"), axis, edge)
                expected_value = bounds_value(expected_record.get("position_bounds"), axis, edge)
                if actual_value is None or expected_value is None:
                    add_issue(issues, sample_limit, "position_bounds_missing", key, axis=axis, edge=edge)
                    continue
                delta = abs(actual_value - expected_value)
                if delta > max_bound_delta:
                    max_bound_delta = delta
                    max_bound_delta_record = {
                        "mesh_index": key[0],
                        "primitive_index": key[1],
                        "axis": axis,
                        "edge": edge,
                        "actual": actual_value,
                        "expected": expected_value,
                        "delta": delta,
                    }
                if delta > position_tolerance:
                    counts["position_bounds_mismatch_count"] += 1
                    add_issue(
                        issues,
                        sample_limit,
                        "position_bounds_mismatch",
                        key,
                        axis=axis,
                        edge=edge,
                        actual=actual_value,
                        expected=expected_value,
                        delta=delta,
                    )
        actual_delta = float(actual_record.get("max_position_delta_from_bind", 0.0) or 0.0)
        expected_delta = float(expected_record.get("max_position_delta_from_bind", 0.0) or 0.0)
        if abs(actual_delta - expected_delta) > position_tolerance:
            counts["max_delta_mismatch_count"] += 1
            add_issue(
                issues,
                sample_limit,
                "max_position_delta_from_bind_mismatch",
                key,
                actual=actual_delta,
                expected=expected_delta,
                delta=abs(actual_delta - expected_delta),
            )
        for field in (
            "source_index_hash",
            "packed_position_hash",
            "packed_texcoord_hash",
            "packed_color_normal_hash",
            "vertex_row_hash",
        ):
            actual_hash = normalize_hash(actual_record.get(field))
            expected_hash = normalize_hash(expected_record.get(field))
            if actual_hash != expected_hash:
                boundary_summary = packed_row_boundary_mismatch_summary(actual_record, expected_record, field)
                if boundary_summary is not None and boundary_summary.get("accepted") is True:
                    counts[f"accepted_{field}_boundary_mismatch_count"] += 1
                    counts["accepted_packed_row_boundary_mismatch_count"] += 1
                    if len(accepted_boundary_mismatches) < sample_limit:
                        accepted_boundary_mismatches.append(
                            {
                                "reason": f"accepted_{field}_boundary_mismatch",
                                "mesh_index": key[0],
                                "primitive_index": key[1],
                                "actual": actual_hash,
                                "expected": expected_hash,
                                "boundary_summary": boundary_summary,
                            }
                        )
                    continue
                counts[f"{field}_mismatch_count"] += 1
                add_issue(
                    issues,
                    sample_limit,
                    f"{field}_mismatch",
                    key,
                    actual=actual_hash,
                    expected=expected_hash,
                    first_mismatch=first_packed_row_prefix_mismatch(actual_record, expected_record, field),
                )

    return {
        "actual_primitive_count": len(actual),
        "expected_primitive_count": len(expected),
        "matched_primitive_count": counts["matched_primitive_count"],
        "position_bounds_mismatch_count": counts["position_bounds_mismatch_count"],
        "max_delta_mismatch_count": counts["max_delta_mismatch_count"],
        "source_index_hash_mismatch_count": counts["source_index_hash_mismatch_count"],
        "packed_position_hash_mismatch_count": counts["packed_position_hash_mismatch_count"],
        "packed_texcoord_hash_mismatch_count": counts["packed_texcoord_hash_mismatch_count"],
        "packed_color_normal_hash_mismatch_count": counts["packed_color_normal_hash_mismatch_count"],
        "vertex_row_hash_mismatch_count": counts["vertex_row_hash_mismatch_count"],
        "accepted_packed_position_hash_boundary_mismatch_count": counts[
            "accepted_packed_position_hash_boundary_mismatch_count"
        ],
        "accepted_packed_texcoord_hash_boundary_mismatch_count": counts[
            "accepted_packed_texcoord_hash_boundary_mismatch_count"
        ],
        "accepted_packed_color_normal_hash_boundary_mismatch_count": counts[
            "accepted_packed_color_normal_hash_boundary_mismatch_count"
        ],
        "accepted_vertex_row_hash_boundary_mismatch_count": counts[
            "accepted_vertex_row_hash_boundary_mismatch_count"
        ],
        "accepted_packed_row_boundary_mismatch_count": counts["accepted_packed_row_boundary_mismatch_count"],
        "accepted_packed_row_boundary_mismatches": accepted_boundary_mismatches,
        "segment_normalization_offset_mismatch_count": counts["segment_normalization_offset_mismatch_count"],
        "has_segment_normalization_offset_mismatch_count": counts["has_segment_normalization_offset_mismatch_count"],
        "expected_segment_normalization_offset": rounded_vec3(expected_segment_normalization_offset),
        "actual_segment_normalization_offset": runtime_dump.get("segment_normalization_offset"),
        "expected_has_segment_normalization_offset": segment_offset_is_nonzero(expected_segment_normalization_offset),
        "actual_has_segment_normalization_offset": runtime_dump.get("has_segment_normalization_offset"),
        "anb_runtime_state_mismatch_count": counts["anb_runtime_state_mismatch_count"],
        "anb_runtime_global_contract_mismatch_count": counts["anb_runtime_global_contract_mismatch_count"],
        "anb_runtime_state_scope": anb_comparison["scope"],
        "anb_runtime_global_contract_mismatches": anb_comparison["global_contract_mismatches"],
        "expected_anb_runtime_state": anb_comparison["expected"],
        "actual_anb_runtime_state": anb_comparison["actual"],
        "max_position_bounds_delta": round_float(max_bound_delta),
        "max_position_bounds_delta_record": max_bound_delta_record,
        "issue_count": len(issues),
        "issues": issues,
        "normal_tolerance": normal_tolerance,
    }


def first_packed_row_prefix_mismatch(
    actual_record: dict[str, object],
    expected_record: dict[str, object],
    field: str,
) -> dict[str, object] | None:
    actual_rows = actual_record.get("packed_row_prefixes")
    expected_rows = expected_record.get("packed_row_prefixes")
    if not isinstance(actual_rows, list) or not isinstance(expected_rows, list):
        return None

    shared_count = min(len(actual_rows), len(expected_rows))
    for row_index in range(shared_count):
        actual_row = actual_rows[row_index]
        expected_row = expected_rows[row_index]
        if not isinstance(actual_row, dict) or not isinstance(expected_row, dict):
            continue
        if normalize_hash(actual_row.get(field)) != normalize_hash(expected_row.get(field)):
            return {
                "row_index": row_index,
                "field": field,
                "actual": compact_packed_row_prefix(actual_row),
                "expected": compact_packed_row_prefix(expected_row),
            }
    if len(actual_rows) != len(expected_rows):
        return {
            "row_index": shared_count,
            "field": field,
            "actual_row_count": len(actual_rows),
            "expected_row_count": len(expected_rows),
        }
    return None


def packed_row_boundary_mismatch_summary(
    actual_record: dict[str, object],
    expected_record: dict[str, object],
    field: str,
) -> dict[str, object] | None:
    actual_rows = actual_record.get("packed_row_prefixes")
    expected_rows = expected_record.get("packed_row_prefixes")
    if not isinstance(actual_rows, list) or not isinstance(expected_rows, list):
        return None
    if len(actual_rows) != len(expected_rows):
        return None

    differences: list[dict[str, object]] = []
    for row_index, (actual_row, expected_row) in enumerate(zip(actual_rows, expected_rows)):
        if not isinstance(actual_row, dict) or not isinstance(expected_row, dict):
            return None
        row_difference = packed_row_boundary_difference(actual_row, expected_row, field)
        if row_difference is None:
            continue
        if row_difference.get("accepted") is not True:
            return None
        if len(differences) < 8:
            differences.append(row_difference)

    if not differences:
        return None
    return {
        "accepted": True,
        "field": field,
        "difference_count": sum(
            1
            for actual_row, expected_row in zip(actual_rows, expected_rows)
            if isinstance(actual_row, dict)
            and isinstance(expected_row, dict)
            and packed_row_boundary_difference(actual_row, expected_row, field) is not None
        ),
        "samples": differences,
    }


def packed_row_boundary_difference(
    actual_row: dict[str, object],
    expected_row: dict[str, object],
    field: str,
) -> dict[str, object] | None:
    if actual_row.get("row_index") != expected_row.get("row_index"):
        return {"accepted": False, "reason": "row_index_mismatch"}
    if actual_row.get("vertex_index") != expected_row.get("vertex_index"):
        return {"accepted": False, "reason": "vertex_index_mismatch"}

    checks: list[tuple[str, float]] = []
    if field == "packed_position_hash":
        checks.append(("packed_ob", 0.001))
    elif field == "packed_color_normal_hash":
        checks.append(("packed_cn", 0.001))
    elif field == "vertex_row_hash":
        checks.extend((("packed_ob", 0.001), ("packed_cn", 0.001)))
        if list(actual_row.get("packed_tc") or []) != list(expected_row.get("packed_tc") or []):
            return {"accepted": False, "reason": "packed_tc_mismatch"}
    elif field == "packed_texcoord_hash":
        return None
    else:
        return None

    accepted_kinds: list[str] = []
    for packed_field, float_tolerance in checks:
        actual_packed = list(actual_row.get(packed_field) or [])
        expected_packed = list(expected_row.get(packed_field) or [])
        if actual_packed == expected_packed:
            continue
        if len(actual_packed) != len(expected_packed):
            return {"accepted": False, "reason": f"{packed_field}_length_mismatch"}
        if any(abs(int(actual) - int(expected)) > 1 for actual, expected in zip(actual_packed, expected_packed)):
            return {"accepted": False, "reason": f"{packed_field}_not_unit_boundary"}
        if packed_field == "packed_ob":
            if max_vec_delta(actual_row.get("position"), expected_row.get("position")) > float_tolerance:
                return {"accepted": False, "reason": "position_delta_exceeds_boundary_tolerance"}
        if packed_field == "packed_cn":
            if max_vec_delta(actual_row.get("normal"), expected_row.get("normal")) > float_tolerance:
                return {"accepted": False, "reason": "normal_delta_exceeds_boundary_tolerance"}
        accepted_kinds.append(packed_field)

    if not accepted_kinds:
        return None
    return {
        "accepted": True,
        "kinds": accepted_kinds,
        "actual": compact_packed_row_prefix(actual_row),
        "expected": compact_packed_row_prefix(expected_row),
    }


def max_vec_delta(actual: object, expected: object) -> float:
    if not isinstance(actual, list) or not isinstance(expected, list) or len(actual) != len(expected):
        return math.inf
    deltas: list[float] = []
    for actual_value, expected_value in zip(actual, expected):
        try:
            deltas.append(abs(float(actual_value) - float(expected_value)))
        except (TypeError, ValueError):
            return math.inf
    return max(deltas) if deltas else math.inf


def compact_packed_row_prefix(row: dict[str, object]) -> dict[str, object]:
    out: dict[str, object] = {}
    for field in (
        "row_index",
        "triangle_index",
        "corner_index",
        "vertex_index",
        "position",
        "normal",
        "packed_ob",
        "packed_tc",
        "packed_cn",
        "source_index_hash",
        "packed_position_hash",
        "packed_texcoord_hash",
        "packed_color_normal_hash",
        "vertex_row_hash",
    ):
        if field in row:
            out[field] = row[field]
    return out


def selected_primitives_with_mesh(
    bind_pose_json: dict[str, object],
    selected_keys: frozenset[tuple[int, int]],
) -> list[tuple[int, int, int, dict[str, object]]]:
    meshes = require_list(bind_pose_json.get("meshes"), "bind-pose meshes")
    out: list[tuple[int, int, int, dict[str, object]]] = []
    for mesh in meshes:
        if not isinstance(mesh, dict):
            continue
        mesh_index = json_int(mesh.get("mesh_index"))
        material_index = json_int(mesh.get("material_index"), -1)
        primitives = mesh.get("primitives", [])
        if not isinstance(primitives, list):
            continue
        for primitive in primitives:
            if not isinstance(primitive, dict):
                continue
            primitive_index = json_int(primitive.get("primitive_index"))
            if (mesh_index, primitive_index) in selected_keys:
                out.append((mesh_index, primitive_index, material_index, primitive))
    missing = sorted(
        selected_keys
        - {(mesh_index, primitive_index) for mesh_index, primitive_index, _material_index, _p in out}
    )
    if missing:
        sample = ", ".join(f"{mesh}:{primitive}" for mesh, primitive in missing[:10])
        raise ParseError(f"selected primitive keys missing from bind-pose export: {sample}")
    return out


def segment_offset_for_csab(segment_continuity: dict[str, object] | None, csab_name: str):
    if segment_continuity is None:
        return list_to_vec3([0.0, 0.0, 0.0])
    fmt = str(segment_continuity.get("format") or "")
    if fmt not in {
        "oot3d_character_segment_continuity_audit_v1",
        "oot3d_character_segment_continuity_contract_v1",
    }:
        raise ParseError("unexpected segment continuity resource format")
    status = str(segment_continuity.get("status") or segment_continuity.get("contract_status") or "")
    if status not in {"valid", "ready"}:
        raise ParseError("segment continuity resource is not ready")

    plan = segment_continuity.get("normalization_plan")
    segment_records: object = None
    if isinstance(plan, dict):
        segment_records = plan.get("segments")
        policy = normalize_resource_path(plan.get("policy"))
    else:
        segment_records = segment_continuity.get("segments")
        policy = normalize_resource_path(segment_continuity.get("normalization_policy"))
    if policy and policy != "cumulative_segment_root_offset":
        raise ParseError("unsupported segment continuity normalization policy")
    if not isinstance(segment_records, list):
        raise ParseError("segment continuity resource has no segment records")

    for segment in segment_records:
        if not isinstance(segment, dict):
            continue
        if normalize_resource_path(segment.get("csab_name")) != csab_name:
            continue
        offset = list_to_vec3(segment.get("normalization_offset"))
        if not vec3_is_finite(offset):
            raise ParseError("segment continuity normalization offset is not finite")
        return offset
    return list_to_vec3([0.0, 0.0, 0.0])


def apply_segment_normalization_offset(position, offset):
    if not segment_offset_is_nonzero(offset):
        return position
    return type(position)(
        position.x + offset.x,
        position.y + offset.y,
        position.z + offset.z,
    )


def segment_offset_is_nonzero(offset) -> bool:
    return abs(offset.x) > 0.0001 or abs(offset.y) > 0.0001 or abs(offset.z) > 0.0001


def compare_segment_normalization_offset(
    runtime_dump: dict[str, object],
    expected_offset,
    position_tolerance: float,
) -> dict[str, object]:
    issues: list[dict[str, object]] = []
    counts = Counter()
    expected_has_offset = segment_offset_is_nonzero(expected_offset)
    actual_has_raw = runtime_dump.get("has_segment_normalization_offset")
    actual_has_offset = bool(actual_has_raw) if isinstance(actual_has_raw, bool) else False
    if actual_has_raw is not None and actual_has_offset != expected_has_offset:
        counts["has_segment_normalization_offset_mismatch_count"] += 1
        issues.append(
            {
                "reason": "has_segment_normalization_offset_mismatch",
                "actual": actual_has_raw,
                "expected": expected_has_offset,
            }
        )

    actual_offset_raw = runtime_dump.get("segment_normalization_offset")
    if actual_offset_raw is None:
        if expected_has_offset:
            counts["segment_normalization_offset_mismatch_count"] += 1
            issues.append(
                {
                    "reason": "segment_normalization_offset_missing",
                    "actual": None,
                    "expected": rounded_vec3(expected_offset),
                }
            )
        return {"counts": counts, "issues": issues}

    try:
        actual_offset = list_to_vec3(actual_offset_raw)
    except ParseError as exc:
        counts["segment_normalization_offset_mismatch_count"] += 1
        issues.append(
            {
                "reason": "segment_normalization_offset_invalid",
                "actual": actual_offset_raw,
                "expected": rounded_vec3(expected_offset),
                "error": str(exc),
            }
        )
        return {"counts": counts, "issues": issues}

    axis_deltas = {
        "x": abs(actual_offset.x - expected_offset.x),
        "y": abs(actual_offset.y - expected_offset.y),
        "z": abs(actual_offset.z - expected_offset.z),
    }
    if any(delta > position_tolerance for delta in axis_deltas.values()):
        counts["segment_normalization_offset_mismatch_count"] += 1
        issues.append(
            {
                "reason": "segment_normalization_offset_mismatch",
                "actual": rounded_vec3(actual_offset),
                "expected": rounded_vec3(expected_offset),
                "delta": {axis: round_float(delta) for axis, delta in axis_deltas.items()},
            }
        )
    return {"counts": counts, "issues": issues}


def compare_anb_runtime_state(
    runtime_dump: dict[str, object],
    expected_anb_semantic_audit: dict[str, object] | None,
) -> dict[str, object]:
    counts = Counter()
    issues: list[dict[str, object]] = []
    actual = runtime_dump.get("anb_runtime_state")
    if expected_anb_semantic_audit is None:
        return {
            "counts": counts,
            "issues": issues,
            "expected": None,
            "actual": actual,
            "scope": "not_checked",
            "global_contract_mismatches": [],
        }
    if expected_anb_semantic_audit.get("format") != "oot3d_anb_semantic_candidate_audit_v1":
        raise ParseError("unexpected ANB semantic audit format")

    expected = {
        "channel_count": json_int(expected_anb_semantic_audit.get("channel_count"), 0),
        "semantic_candidate_ready": True,
        "semantic_matched_record_count": json_int(expected_anb_semantic_audit.get("matched_anb_record_count"), 0),
        "semantic_compared_record_count": json_int(expected_anb_semantic_audit.get("compared_record_count"), 0),
        "semantic_candidate_channel_count": json_int(expected_anb_semantic_audit.get("candidate_channel_count"), 0),
        "semantic_strong_candidate_count": json_int(expected_anb_semantic_audit.get("strong_candidate_count"), 0),
        "semantic_stable_candidate_channel_count": json_int(
            expected_anb_semantic_audit.get("stable_candidate_channel_count"),
            0,
        ),
        "semantic_compared_frame_component_pair_count": json_int(
            expected_anb_semantic_audit.get("total_compared_frame_component_pairs"),
            0,
        ),
    }
    status_counts = expected_anb_semantic_audit.get("status_counts")
    if isinstance(status_counts, dict):
        expected["semantic_skipped_frame_mismatch_count"] = json_int(
            status_counts.get("skipped_frame_count_mismatch"),
            0,
        )

    if not isinstance(actual, dict):
        counts["anb_runtime_state_mismatch_count"] += 1
        issues.append(
            {
                "reason": "anb_runtime_state_missing",
                "actual": actual,
                "expected": expected,
            }
        )
        return {
            "counts": counts,
            "issues": issues,
            "expected": expected,
            "actual": actual,
            "scope": "missing",
            "global_contract_mismatches": [],
        }

    global_contract_mismatches: list[dict[str, object]] = []
    for field, expected_value in expected.items():
        actual_value = actual.get(field)
        if actual_value != expected_value:
            global_contract_mismatches.append(
                {
                    "field": field,
                    "actual": actual_value,
                    "expected": expected_value,
                }
            )
    if not global_contract_mismatches:
        return {
            "counts": counts,
            "issues": issues,
            "expected": expected,
            "actual": actual,
            "scope": "global_contract",
            "global_contract_mismatches": [],
        }

    counts["anb_runtime_global_contract_mismatch_count"] = len(global_contract_mismatches)
    if valid_anb_runtime_profile_subset(actual, expected):
        return {
            "counts": counts,
            "issues": issues,
            "expected": expected,
            "actual": actual,
            "scope": "profile_subset",
            "global_contract_mismatches": global_contract_mismatches,
        }

    for mismatch in global_contract_mismatches:
        counts["anb_runtime_state_mismatch_count"] += 1
        issues.append(
            {
                "reason": "anb_runtime_state_mismatch",
                "field": mismatch["field"],
                "actual": mismatch["actual"],
                "expected": mismatch["expected"],
            }
        )
    return {
        "counts": counts,
        "issues": issues,
        "expected": expected,
        "actual": actual,
        "scope": "invalid_or_unexpected_profile",
        "global_contract_mismatches": global_contract_mismatches,
    }


def valid_anb_runtime_profile_subset(actual: dict[str, object], expected_global: dict[str, object]) -> bool:
    if actual.get("raw_ir_ready") is False:
        return False
    if actual.get("semantic_candidate_ready") is not True:
        return False
    if json_int(actual.get("channel_count"), -1) != json_int(expected_global.get("channel_count"), -2):
        return False

    bounded_fields = (
        "semantic_matched_record_count",
        "semantic_compared_record_count",
        "semantic_candidate_channel_count",
        "semantic_strong_candidate_count",
        "semantic_stable_candidate_channel_count",
        "semantic_compared_frame_component_pair_count",
    )
    for field in bounded_fields:
        actual_value = json_int(actual.get(field), -1)
        expected_value = json_int(expected_global.get(field), -1)
        if actual_value < 0 or expected_value < 0 or actual_value > expected_value:
            return False

    if json_int(actual.get("semantic_compared_record_count"), 0) <= 0:
        return False
    if json_int(actual.get("semantic_candidate_channel_count"), 0) <= 0:
        return False
    if json_int(actual.get("semantic_strong_candidate_count"), 0) <= 0:
        return False
    if json_int(actual.get("semantic_compared_frame_component_pair_count"), 0) <= 0:
        return False
    if json_int(actual.get("semantic_stable_candidate_channel_count"), 0) > json_int(
        actual.get("semantic_candidate_channel_count"),
        0,
    ):
        return False

    skipped = json_int(actual.get("semantic_skipped_frame_mismatch_count"), 0)
    matched = json_int(actual.get("semantic_matched_record_count"), 0)
    compared = json_int(actual.get("semantic_compared_record_count"), 0)
    return skipped >= 0 and matched >= compared and skipped <= matched


def runtime_dump_primitives(runtime_dump: dict[str, object]) -> dict[tuple[int, int], dict[str, object]]:
    records = require_list(runtime_dump.get("primitive_samples"), "runtime dump primitive_samples")
    out: dict[tuple[int, int], dict[str, object]] = {}
    for record in records:
        if not isinstance(record, dict):
            continue
        key = (json_int(record.get("mesh_index")), json_int(record.get("primitive_index")))
        if key[0] < 0 or key[1] < 0:
            continue
        if key in out:
            raise ParseError(f"runtime draw dump contains duplicate primitive {key}")
        out[key] = record
    return out


def runtime_material_info(bind_pose_json: dict[str, object], material_index: int) -> dict[str, object]:
    materials = require_list(bind_pose_json.get("materials"), "bind-pose materials")
    material = find_record_by_index(materials, material_index)
    if material is None:
        raise ParseError("selected runtime primitive references a missing material descriptor")

    primary_texture_index = -1
    primary_slot: int | None = None
    wrap_s = 0
    wrap_t = 0
    texture_mappers = material.get("texture_mappers", [])
    if isinstance(texture_mappers, list):
        mapper_limit = min(
            len(texture_mappers),
            max(0, json_intish(material.get("texture_mappers_used"), len(texture_mappers))),
        )
        for slot in range(mapper_limit):
            mapper = texture_mappers[slot]
            if not isinstance(mapper, dict):
                continue
            texture_index = json_intish(mapper.get("index"), -1)
            if texture_index < 0:
                continue
            primary_slot = slot
            primary_texture_index = texture_index
            wrap_s = json_intish(mapper.get("wrap_s"), 0)
            wrap_t = json_intish(mapper.get("wrap_t"), 0)
            break

    if primary_texture_index < 0:
        indices = material.get("texture_indices", [])
        if isinstance(indices, list):
            for slot, value in enumerate(indices):
                texture_index = json_intish(value, -1)
                if texture_index >= 0:
                    primary_slot = slot
                    primary_texture_index = texture_index
                    break

    uv_scale = (1.0, 1.0)
    uv_rotation = 0.0
    uv_translation = (0.0, 0.0)
    texture_coords = material.get("texture_coords", [])
    if primary_slot is not None and isinstance(texture_coords, list):
        coord_limit = min(
            len(texture_coords),
            max(0, json_intish(material.get("texture_coords_used"), len(texture_coords))),
        )
        if primary_slot < coord_limit and isinstance(texture_coords[primary_slot], dict):
            coord = texture_coords[primary_slot]
            if json_intish(coord.get("coordinate_index"), 0) == 0:
                uv_scale = json_vec2_tuple(coord.get("scale"), (1.0, 1.0))
                uv_rotation = float(coord.get("rotation", 0.0) or 0.0)
                uv_translation = json_vec2_tuple(coord.get("translation"), (0.0, 0.0))

    return {
        "primary_texture_index": primary_texture_index,
        "wrap_s": wrap_s,
        "wrap_t": wrap_t,
        "uv_scale": uv_scale,
        "uv_rotation": uv_rotation,
        "uv_translation": uv_translation,
    }


def runtime_texture_info(bind_pose_json: dict[str, object], texture_index: int) -> dict[str, object]:
    if texture_index < 0:
        return {"width": 32, "height": 32}
    textures = require_list(bind_pose_json.get("textures"), "bind-pose textures")
    texture = find_record_by_index(textures, texture_index)
    if texture is None:
        raise ParseError("selected runtime material references a missing texture descriptor")
    return {
        "width": max(1, json_intish(texture.get("width"), 32)),
        "height": max(1, json_intish(texture.get("height"), 32)),
    }


def find_record_by_index(records: list[object], index: int) -> dict[str, object] | None:
    for record in records:
        if isinstance(record, dict) and json_intish(record.get("index"), -1) == index:
            return record
    return None


def runtime_primitive_uv_offset(
    primitive: dict[str, object],
    material: dict[str, object],
    uv_orientation: str,
) -> tuple[float, float]:
    vertices = require_list(primitive.get("vertices"), "runtime primitive vertices")
    indices = require_list(primitive.get("indices"), "runtime primitive indices")
    min_s = math.inf
    min_t = math.inf
    for index in indices:
        if not isinstance(index, int) or index < 0 or index >= len(vertices):
            raise ParseError("selected runtime primitive index is outside its vertex array")
        vertex = vertices[index]
        if not isinstance(vertex, dict):
            raise ParseError("selected runtime primitive vertex is not an object")
        s, t = apply_runtime_primitive_uv(json_vec2_tuple(vertex.get("source_uv0"), (0.0, 0.0)), material, uv_orientation)
        min_s = min(min_s, s)
        min_t = min(min_t, t)
    if not math.isfinite(min_s) or not math.isfinite(min_t):
        return (0.0, 0.0)
    return (
        float(runtime_integer_repeat_offset(min_s, int(material["wrap_s"]))),
        float(runtime_integer_repeat_offset(min_t, int(material["wrap_t"]))),
    )


def runtime_integer_repeat_offset(min_uv: float, wrap: int) -> int:
    if min_uv >= 0.0 or wrap != PICA_TEXTURE_WRAP_REPEAT:
        return 0
    return math.ceil(-min_uv)


def packed_runtime_vertex(
    vertex: dict[str, object],
    position,
    normal,
    material: dict[str, object],
    texture: dict[str, object],
    uv_orientation: str,
    uv_offset: tuple[float, float],
) -> dict[str, object]:
    s, t = apply_runtime_primitive_uv(json_vec2_tuple(vertex.get("source_uv0"), (0.0, 0.0)), material, uv_orientation)
    s += uv_offset[0]
    t += uv_offset[1]
    color = json_color(vertex.get("source_color_rgba"))
    if color == [255, 255, 255, 255]:
        packed_cn = [
            normal_component_to_u8(normal.x),
            normal_component_to_u8(normal.y),
            normal_component_to_u8(normal.z),
            255,
        ]
    else:
        packed_cn = color
    return {
        "packed_ob": [
            vertex_coord_to_s16(position.x),
            vertex_coord_to_s16(position.y),
            vertex_coord_to_s16(position.z),
        ],
        "packed_flag": 0,
        "packed_tc": [
            vertex_coord_to_s16(s * float(texture["width"]) * 32.0),
            vertex_coord_to_s16(t * float(texture["height"]) * 32.0),
        ],
        "packed_cn": packed_cn,
    }


def packed_row_prefix_sample(
    row_index: int,
    sample: dict[str, object],
    packed: dict[str, object],
    hashes: dict[str, int],
) -> dict[str, object]:
    return {
        "row_index": row_index,
        "triangle_index": sample["triangle_index"],
        "corner_index": sample["corner_index"],
        "vertex_index": sample["vertex_index"],
        "position": sample["position"],
        "normal": sample["normal"],
        "packed_ob": packed["packed_ob"],
        "packed_tc": packed["packed_tc"],
        "packed_cn": packed["packed_cn"],
        "source_index_hash": str(hashes["source_index_hash"]),
        "packed_position_hash": str(hashes["packed_position_hash"]),
        "packed_texcoord_hash": str(hashes["packed_texcoord_hash"]),
        "packed_color_normal_hash": str(hashes["packed_color_normal_hash"]),
        "vertex_row_hash": str(hashes["vertex_row_hash"]),
    }


def apply_runtime_primitive_uv(
    source_uv: tuple[float, float],
    material: dict[str, object],
    uv_orientation: str,
) -> tuple[float, float]:
    uv_scale = material["uv_scale"]
    uv_translation = material["uv_translation"]
    s = source_uv[0] * float(uv_scale[0])
    t = source_uv[1] * float(uv_scale[1])
    rotation = float(material["uv_rotation"])
    if rotation != 0.0:
        cos_r = math.cos(rotation)
        sin_r = math.sin(rotation)
        s, t = (s * cos_r) - (t * sin_r), (s * sin_r) + (t * cos_r)
    s += float(uv_translation[0])
    t += float(uv_translation[1])
    flip_x = uv_orientation in {"flip_x", "flip_xy"}
    flip_y = uv_orientation in {"flip_y", "flip_xy"}
    return ((1.0 - s) if flip_x else s, (1.0 - t) if flip_y else t)


def hash_packed_vertex_row(
    hashes: dict[str, int],
    row_index: int,
    vertex_index: object,
    packed: dict[str, object],
) -> None:
    vertex_index_int = json_int(vertex_index, -1)
    hashes["vertex_row_hash"] = hash_u64(hashes["vertex_row_hash"], row_index)
    hashes["vertex_row_hash"] = hash_u64(hashes["vertex_row_hash"], signed_to_u64(vertex_index_int))
    hashes["source_index_hash"] = hash_u64(hashes["source_index_hash"], row_index)
    hashes["source_index_hash"] = hash_u64(hashes["source_index_hash"], signed_to_u64(vertex_index_int))

    for value in packed["packed_ob"]:  # type: ignore[index]
        hashes["vertex_row_hash"] = hash_s16(hashes["vertex_row_hash"], int(value))
        hashes["packed_position_hash"] = hash_s16(hashes["packed_position_hash"], int(value))
    hashes["vertex_row_hash"] = hash_s16(hashes["vertex_row_hash"], int(packed["packed_flag"]))

    for value in packed["packed_tc"]:  # type: ignore[index]
        hashes["vertex_row_hash"] = hash_s16(hashes["vertex_row_hash"], int(value))
        hashes["packed_texcoord_hash"] = hash_s16(hashes["packed_texcoord_hash"], int(value))

    for value in packed["packed_cn"]:  # type: ignore[index]
        hashes["vertex_row_hash"] = hash_byte(hashes["vertex_row_hash"], int(value))
        hashes["packed_color_normal_hash"] = hash_byte(hashes["packed_color_normal_hash"], int(value))


def hash_byte(hash_value: int, value: int) -> int:
    return ((hash_value ^ (value & 0xff)) * FNV64_PRIME) & U64_MASK


def hash_u64(hash_value: int, value: int) -> int:
    out = hash_value
    value &= U64_MASK
    for index in range(8):
        out = hash_byte(out, (value >> (index * 8)) & 0xff)
    return out


def hash_s16(hash_value: int, value: int) -> int:
    bits = value & 0xffff
    return hash_byte(hash_byte(hash_value, bits & 0xff), (bits >> 8) & 0xff)


def signed_to_u64(value: int) -> int:
    return value & U64_MASK


def vertex_coord_to_s16(value: float) -> int:
    if not math.isfinite(value):
        return 0
    if value < -32768.0:
        return -32768
    if value > 32767.0:
        return 32767
    return cxx_lround(value)


def normal_component_to_u8(value: float) -> int:
    if not math.isfinite(value):
        return 0
    scaled = cxx_lround(max(-1.0, min(1.0, value)) * 127.0)
    scaled = max(-128, min(127, scaled))
    return scaled if scaled >= 0 else 256 + scaled


def cxx_lround(value: float) -> int:
    return math.floor(value + 0.5) if value >= 0.0 else math.ceil(value - 0.5)


def json_vec2_tuple(value: object, default: tuple[float, float]) -> tuple[float, float]:
    if isinstance(value, list) and len(value) >= 2:
        return (float(value[0]), float(value[1]))
    return default


def json_color(value: object) -> list[int]:
    if isinstance(value, list) and len(value) == 4:
        return [max(0, min(255, json_intish(channel, 0))) for channel in value]
    return [255, 255, 255, 255]


def json_intish(value: object, default: int = 0) -> int:
    if isinstance(value, int) and not isinstance(value, bool):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError:
            return default
    return default


def normalize_hash(value: object) -> str:
    if isinstance(value, int) and not isinstance(value, bool):
        return str(value)
    if isinstance(value, str):
        return value
    return ""


def link_child_target_from_binding(binding_manifest_path: Path) -> dict[str, object]:
    binding = read_json(binding_manifest_path)
    targets = require_list(binding.get("targets"), "skinned animation binding targets")
    for target in targets:
        if (
            isinstance(target, dict)
            and target.get("archive_path") == "zelda_link_child_new.zar"
            and target.get("target_cmb_name") == "child/model/childlink_v2.cmb"
        ):
            return target
    raise ParseError("could not find Link child target in skinned animation binding manifest")


def animation_record_by_csab(target: dict[str, object], csab_name: str) -> dict[str, object]:
    animations = require_list(target.get("animations"), "Link child target animations")
    for animation in animations:
        if isinstance(animation, dict) and normalize_resource_path(animation.get("csab_name")) == csab_name:
            return animation
    raise ParseError(f"could not find Link child CSAB track export for {csab_name}")


def bounds_value(bounds: object, axis: str, edge: str) -> float | None:
    if not isinstance(bounds, dict):
        return None
    axis_bounds = bounds.get(axis)
    if not isinstance(axis_bounds, dict):
        return None
    value = axis_bounds.get(edge)
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        return float(value)
    return None


def add_issue(
    issues: list[dict[str, object]],
    sample_limit: int,
    reason: str,
    key: tuple[int, int],
    **extra: object,
) -> None:
    if len(issues) >= sample_limit:
        return
    issue = {
        "reason": reason,
        "mesh_index": key[0],
        "primitive_index": key[1],
    }
    issue.update(extra)
    issues.append(issue)


def read_json(path: Path) -> dict[str, object]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise ParseError(f"{path}: could not read JSON: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise ParseError(f"{path}: invalid JSON: {exc}") from exc
    if not isinstance(value, dict):
        raise ParseError(f"{path}: expected JSON object")
    return value


def require_dict(value: object, label: str) -> dict[str, object]:
    if not isinstance(value, dict):
        raise ParseError(f"{label} is not an object")
    return value


def require_list(value: object, label: str) -> list[object]:
    if not isinstance(value, list):
        raise ParseError(f"{label} is not an array")
    return value


def normalize_resource_path(value: object) -> str:
    return str(value or "").replace("\\", "/")


def vec3_is_finite(value) -> bool:
    return math.isfinite(value.x) and math.isfinite(value.y) and math.isfinite(value.z)


def add_vec3_bounds(bounds: dict[str, list[float]], value) -> None:
    for axis, axis_value in (("x", value.x), ("y", value.y), ("z", value.z)):
        if axis not in bounds:
            bounds[axis] = [axis_value, axis_value]
        else:
            bounds[axis][0] = min(bounds[axis][0], axis_value)
            bounds[axis][1] = max(bounds[axis][1], axis_value)


def rounded_bounds(bounds: dict[str, list[float]]) -> dict[str, dict[str, float]]:
    return {
        axis: {"min": round_float(values[0]), "max": round_float(values[1])}
        for axis, values in sorted(bounds.items())
    }


def rounded_vec3(value) -> list[float]:
    return [round_float(value.x), round_float(value.y), round_float(value.z)]


def round_float(value: float) -> float:
    return round(float(value), 6)
