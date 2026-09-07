from __future__ import annotations

from collections import Counter
from dataclasses import dataclass
import json
import math
import struct
from pathlib import Path
from typing import Iterable

from .animation_glb_export import (
    AnimationGlbOptions,
    AnimatedExportPrimitive,
    AnimatedPrimitiveGeometry,
    add_animated_primitive_node,
    add_morph_weight_animation,
    animated_geometry_from_frames,
    export_skinned_animation_glb,
    normalize_animation_interpolation,
    weighted_skin_pose,
)
from .binary import ParseError
from .character_conversion_package import (
    bind_pose_skeleton_bones,
    selected_draw_world_transforms,
)
from .cmb import Color, Vec2, Vec3
from .legacy_fast_resource import Matrix4, multiply_matrix, transform_direction, transform_position
from .skinned_animation import invert_affine_matrix, list_to_vec3, vec3_distance
from .static_glb_export import GlbBuilder, position_bounds, scale_vec3


MINIMAL_RUNTIME_PLAYER_EXPORT_FORMAT = "oot3d_minimal_runtime_player_export_v1"
MINIMAL_RUNTIME_PLAYER_PARITY_AUDIT_FORMAT = "oot3d_minimal_runtime_player_parity_audit_v1"
MINIMAL_RUNTIME_TRACK_EXPORT_AUDIT_FORMAT = "oot3d_minimal_runtime_track_export_audit_v1"
GL_FLOAT = 5126
GL_UNSIGNED_INT = 5125


@dataclass(frozen=True)
class MinimalRuntimePlayerExportOptions:
    output: Path
    audit_output: Path | None = None
    validated_glb_output: Path | None = None
    validated_manifest_output: Path | None = None
    zar_path: Path | None = None
    cmb_name: str | None = None
    csab_name: str = "child/anim/nml_run_free.csab"
    selection_manifest: Path | None = None
    position_scale: float | None = None
    frame_step: int = 1
    fps: float = 60.0
    interpolation: str = "STEP"
    position_tolerance: float = 0.00001
    normal_tolerance: float = 0.00001
    sample_limit: int = 100


@dataclass(frozen=True)
class MinimalRuntimeTrackExportOptions:
    output: Path
    audit_output: Path | None = None
    csab_name: str | None = None
    position_scale: float | None = None
    frame_step: int = 1
    fps: float = 60.0
    interpolation: str = "STEP"
    sample_limit: int = 100


@dataclass(frozen=True)
class RuntimePrimitiveFrameGeometry:
    mesh_index: int
    shape_index: int
    material_index: int
    visibility_id: int
    primitive_index: int
    skinning_mode: int
    name: str
    geometry: AnimatedPrimitiveGeometry


@dataclass(frozen=True)
class BakedGlbPrimitive:
    mesh_index: int
    primitive_index: int
    name: str
    frame_numbers: tuple[int, ...]
    indices: tuple[int, ...]
    positions_by_frame: tuple[tuple[Vec3, ...], ...]
    normals_by_frame: tuple[tuple[Vec3, ...], ...]


def audit_minimal_runtime_player_export(
    character_manifest_path: Path,
    options: MinimalRuntimePlayerExportOptions,
) -> dict[str, object]:
    if options.frame_step <= 0:
        raise ParseError(f"frame step must be positive, got {options.frame_step!r}")
    if not math.isfinite(options.fps) or options.fps <= 0.0:
        raise ParseError(f"fps must be positive and finite, got {options.fps!r}")
    if not math.isfinite(options.position_tolerance) or options.position_tolerance < 0.0:
        raise ParseError(f"position tolerance must be finite and non-negative, got {options.position_tolerance!r}")
    if not math.isfinite(options.normal_tolerance) or options.normal_tolerance < 0.0:
        raise ParseError(f"normal tolerance must be finite and non-negative, got {options.normal_tolerance!r}")
    interpolation = normalize_animation_interpolation(options.interpolation)

    character_manifest = read_json(character_manifest_path)
    if character_manifest.get("format") != "oot3d_character_conversion_manifest_v1":
        raise ParseError(f"{character_manifest_path}: unexpected character conversion manifest format")
    target = require_dict(character_manifest.get("target"), "character manifest target")
    source_manifests = require_dict(character_manifest.get("source_manifests"), "character source manifests")

    bind_pose_path = Path(str(require_dict(target.get("bind_pose"), "target bind_pose").get("export")))
    native_manifest_path = Path(str(source_manifests.get("skinned_bind_pose_native")))
    binding_manifest_path = Path(str(source_manifests.get("skinned_animation_binding")))
    bind_pose_json = read_json(bind_pose_path)
    native_manifest = read_json(native_manifest_path)
    binding_manifest = read_json(binding_manifest_path)
    track_path = find_track_export_file(
        binding_manifest,
        archive_path=str(target.get("archive_path", "")),
        cmb_name=str(options.cmb_name or target.get("target_cmb_name", "")),
        csab_name=options.csab_name,
    )
    track_json = read_json(track_path)

    zar_path = options.zar_path or source_archive_from_native_manifest(native_manifest)
    cmb_name = options.cmb_name or str(target.get("target_cmb_name", ""))
    selection_manifest = options.selection_manifest or selection_manifest_from_native_manifest(native_manifest)
    position_scale = (
        float(options.position_scale)
        if options.position_scale is not None
        else float(native_manifest.get("position_scale", 0.0) or 0.0)
    )
    if position_scale <= 0.0:
        position_scale = 0.0001

    runtime_manifest = export_minimal_runtime_player_glb(
        bind_pose_json,
        native_manifest,
        track_json,
        output=options.output,
        csab_name=options.csab_name,
        position_scale=position_scale,
        frame_step=options.frame_step,
        fps=options.fps,
        interpolation=interpolation,
        sample_limit=options.sample_limit,
    )

    validated_glb_output = options.validated_glb_output or options.output.with_name(
        f"{options.output.stem}__validated_glb_oracle.glb"
    )
    validated_manifest_output = options.validated_manifest_output or validated_glb_output.with_suffix(".manifest.json")
    validated_manifest = export_skinned_animation_glb(
        zar_path,
        AnimationGlbOptions(
            output=validated_glb_output,
            manifest_output=validated_manifest_output,
            cmb_name=cmb_name,
            csab_name=options.csab_name,
            selection_manifest=selection_manifest,
            texture_orientation=None,
            uv_orientation="normal",
            position_scale=position_scale,
            frame_step=options.frame_step,
            fps=options.fps,
            interpolation=interpolation,
            sample_limit=options.sample_limit,
        ),
    )

    runtime_primitives = extract_baked_glb_primitives(options.output)
    validated_primitives = extract_baked_glb_primitives(validated_glb_output)
    comparison = compare_baked_glb_primitives(
        runtime_primitives,
        validated_primitives,
        position_tolerance=options.position_tolerance,
        normal_tolerance=options.normal_tolerance,
        sample_limit=options.sample_limit,
    )

    status = "valid" if comparison["issue_count"] == 0 else "invalid"
    audit = {
        "format": MINIMAL_RUNTIME_PLAYER_PARITY_AUDIT_FORMAT,
        "status": status,
        "character_manifest": str(character_manifest_path),
        "bind_pose_export": str(bind_pose_path),
        "native_bind_pose_manifest": str(native_manifest_path),
        "binding_manifest": str(binding_manifest_path),
        "track_export": str(track_path),
        "source_zar": str(zar_path),
        "cmb_name": cmb_name,
        "csab_name": options.csab_name,
        "selection_manifest": str(selection_manifest) if selection_manifest is not None else None,
        "runtime_glb_output": str(options.output),
        "runtime_manifest": runtime_manifest,
        "validated_glb_output": str(validated_glb_output),
        "validated_manifest_output": str(validated_manifest_output),
        "validated_manifest_counts": validated_manifest.get("counts", {}),
        "position_scale": position_scale,
        "frame_step": options.frame_step,
        "fps": options.fps,
        "interpolation": interpolation,
        "position_tolerance": options.position_tolerance,
        "normal_tolerance": options.normal_tolerance,
        "comparison": comparison,
        "contract": (
            "The minimal runtime export reads the packaged bind-pose JSON and CSAB track JSON, samples the "
            "runtime skeleton, applies pose_world * inverse(bind_world) to skinned mode-1/mode-2 vertices, "
            "and applies the animated bone world matrix directly to rigid mode-0 vertices such as eyes and mouth. "
            "It is compared frame-by-frame against the independently generated validated GLB oracle before any "
            "manual runtime test is considered actionable."
        ),
    }
    audit_path = options.audit_output or options.output.with_suffix(".audit.json")
    audit_path.parent.mkdir(parents=True, exist_ok=True)
    audit_path.write_text(json.dumps(audit, indent=2), encoding="utf-8", newline="\n")
    return audit


def audit_minimal_runtime_track_export(
    character_manifest_path: Path,
    track_export_path: Path,
    options: MinimalRuntimeTrackExportOptions,
) -> dict[str, object]:
    if options.frame_step <= 0:
        raise ParseError(f"frame step must be positive, got {options.frame_step!r}")
    if not math.isfinite(options.fps) or options.fps <= 0.0:
        raise ParseError(f"fps must be positive and finite, got {options.fps!r}")
    interpolation = normalize_animation_interpolation(options.interpolation)

    character_manifest = read_json(character_manifest_path)
    if character_manifest.get("format") != "oot3d_character_conversion_manifest_v1":
        raise ParseError(f"{character_manifest_path}: unexpected character conversion manifest format")
    target = require_dict(character_manifest.get("target"), "character manifest target")
    source_manifests = require_dict(character_manifest.get("source_manifests"), "character source manifests")

    bind_pose_path = Path(str(require_dict(target.get("bind_pose"), "target bind_pose").get("export")))
    native_manifest_path = Path(str(source_manifests.get("skinned_bind_pose_native")))
    bind_pose_json = read_json(bind_pose_path)
    native_manifest = read_json(native_manifest_path)
    track_json = read_json(track_export_path)
    csab_name = options.csab_name or str(track_json.get("csab_name", ""))
    if not csab_name:
        raise ParseError(f"{track_export_path}: track export has no csab_name")

    position_scale = (
        float(options.position_scale)
        if options.position_scale is not None
        else float(native_manifest.get("position_scale", 0.0) or 0.0)
    )
    if position_scale <= 0.0:
        position_scale = 0.0001

    runtime_manifest = export_minimal_runtime_player_glb(
        bind_pose_json,
        native_manifest,
        track_json,
        output=options.output,
        csab_name=csab_name,
        position_scale=position_scale,
        frame_step=options.frame_step,
        fps=options.fps,
        interpolation=interpolation,
        sample_limit=options.sample_limit,
    )
    runtime_primitives = extract_baked_glb_primitives(options.output)
    inspection = inspect_baked_glb_primitives(runtime_primitives, sample_limit=options.sample_limit)
    comparison = {
        "status": inspection["status"],
        "runtime_primitive_count": inspection["primitive_count"],
        "runtime_frame_slot_count": len(runtime_manifest.get("sample_frames", [])),
        "runtime_target_frame_count": len(runtime_manifest.get("target_frames", [])),
        "runtime_vertex_rows": inspection["animated_position_row_count"],
        "finite_position_row_count": inspection["finite_position_row_count"],
        "finite_normal_row_count": inspection["finite_normal_row_count"],
        "issue_count": inspection["issue_count"],
        "issues": inspection["issues"],
    }
    status = "valid" if comparison["issue_count"] == 0 else "invalid"
    audit = {
        "format": MINIMAL_RUNTIME_TRACK_EXPORT_AUDIT_FORMAT,
        "status": status,
        "character_manifest": str(character_manifest_path),
        "bind_pose_export": str(bind_pose_path),
        "native_bind_pose_manifest": str(native_manifest_path),
        "track_export": str(track_export_path),
        "csab_name": csab_name,
        "runtime_glb_output": str(options.output),
        "runtime_manifest": runtime_manifest,
        "position_scale": position_scale,
        "frame_step": options.frame_step,
        "fps": options.fps,
        "interpolation": interpolation,
        "inspection": inspection,
        "comparison": comparison,
        "contract": (
            "This audit feeds a materialized CSAB skeleton-track JSON directly into the same minimal "
            "runtime player export path used for package-data diagnostics. It does not require the "
            "derived CSAB name to exist in the source ZAR. Acceptance here proves the derived track is "
            "mountable by the offline runtime/skinned path and produces finite selected Link child "
            "draw geometry; an installed C++ runtime draw capture is still required before promotion."
        ),
    }
    audit_path = options.audit_output or options.output.with_suffix(".audit.json")
    audit_path.parent.mkdir(parents=True, exist_ok=True)
    audit_path.write_text(json.dumps(audit, indent=2), encoding="utf-8", newline="\n")
    return audit


def export_minimal_runtime_player_glb(
    bind_pose_json: dict[str, object],
    native_manifest: dict[str, object],
    track_json: dict[str, object],
    *,
    output: Path,
    csab_name: str,
    position_scale: float,
    frame_step: int,
    fps: float,
    interpolation: str,
    sample_limit: int,
) -> dict[str, object]:
    if bind_pose_json.get("format") != "oot3d_skinned_bind_pose_export_v1":
        raise ParseError("unexpected bind-pose export format")
    if native_manifest.get("format") != "oot3d_skinned_bind_pose_native_export_v1":
        raise ParseError("unexpected native bind-pose manifest format")
    if track_json.get("format") != "oot3d_csab_skeleton_track_export_v1":
        raise ParseError("unexpected CSAB track export format")
    if normalize_resource_path(track_json.get("csab_name")) != normalize_resource_path(csab_name):
        raise ParseError("CSAB track export does not match requested runtime export")

    frame_count = json_int(track_json.get("frame_count_candidate"))
    if frame_count < 0:
        raise ParseError("track export has no valid frame_count_candidate")
    frame_numbers = list(range(0, frame_count + 1, frame_step))
    if frame_numbers[-1] != frame_count:
        frame_numbers.append(frame_count)
    target_frame_numbers = tuple(frame_numbers[1:])

    skeleton_bones = bind_pose_skeleton_bones(bind_pose_json)
    expected_bone_count = json_int(track_json.get("skeleton_bone_count_candidate"))
    if expected_bone_count >= 0 and expected_bone_count != len(skeleton_bones):
        raise ParseError("CSAB track skeleton count does not match bind-pose skeleton")
    inverse_bind_world_transforms = tuple(invert_affine_matrix(bone[4]) for bone in skeleton_bones)
    tracks = require_list(track_json.get("tracks"), "CSAB tracks")

    world_transforms_by_frame: list[tuple[Matrix4, ...]] = []
    skin_transforms_by_frame: list[tuple[Matrix4, ...]] = []
    for frame in frame_numbers:
        world_transforms = selected_draw_world_transforms(skeleton_bones, tracks, frame)
        skin_transforms = tuple(
            multiply_matrix(world_transforms[bone_index], inverse_bind_world_transforms[bone_index])
            for bone_index in range(len(world_transforms))
        )
        world_transforms_by_frame.append(world_transforms)
        skin_transforms_by_frame.append(skin_transforms)

    selection_keys = selected_primitive_keys(native_manifest)
    primitives = runtime_primitives_from_bind_pose(
        bind_pose_json,
        selection_keys=selection_keys,
        world_transforms_by_frame=tuple(world_transforms_by_frame),
        skin_transforms_by_frame=tuple(skin_transforms_by_frame),
        position_scale=position_scale,
    )

    builder = GlbBuilder()
    builder.json["asset"] = {
        "version": "2.0",
        "generator": "oot3d-asset-tool minimal_runtime_player_export",
    }
    node_indices: list[int] = []
    for primitive in primitives:
        node_indices.append(
            add_animated_primitive_node(
                builder,
                AnimatedExportPrimitive(
                    mesh_index=primitive.mesh_index,
                    shape_index=primitive.shape_index,
                    material_index=primitive.material_index,
                    visibility_id=primitive.visibility_id,
                    primitive_index=primitive.primitive_index,
                    skinning_mode=primitive.skinning_mode,
                    texture_index=None,
                    texture_name=None,
                    name=primitive.name,
                    geometry=primitive.geometry,
                ),
                None,
                target_frame_numbers=target_frame_numbers,
            )
        )
    add_morph_weight_animation(
        builder,
        node_indices,
        frame_numbers=tuple(frame_numbers),
        target_count=max(0, len(frame_numbers) - 1),
        fps=fps,
        interpolation=interpolation,
    )
    builder.write_glb(output)

    counts = Counter()
    for primitive in primitives:
        counts["primitive_count"] += 1
        counts["triangle_count"] += len(primitive.geometry.indices) // 3
        counts["vertex_count"] += len(primitive.geometry.base_positions)
        counts["morph_target_count"] += len(primitive.geometry.target_position_deltas)
        if primitive.skinning_mode == 0:
            counts["rigid_primitive_count"] += 1
        elif primitive.skinning_mode in (1, 2):
            counts["skinned_primitive_count"] += 1

    return {
        "format": MINIMAL_RUNTIME_PLAYER_EXPORT_FORMAT,
        "status": "exported",
        "output": str(output),
        "csab_name": csab_name,
        "frame_count_candidate": frame_count,
        "frame_step": frame_step,
        "fps": fps,
        "sample_frames": frame_numbers,
        "target_frames": list(target_frame_numbers),
        "position_scale": position_scale,
        "selection_primitive_key_count": len(selection_keys),
        "counts": {
            "primitive_count": counts["primitive_count"],
            "skinned_primitive_count": counts["skinned_primitive_count"],
            "rigid_primitive_count": counts["rigid_primitive_count"],
            "triangle_count": counts["triangle_count"],
            "vertex_count": counts["vertex_count"],
            "morph_target_count": counts["morph_target_count"],
        },
        "bounds": position_bounds(
            position
            for primitive in primitives
            for position in primitive.geometry.base_positions
        ),
        "animated_bounds": position_bounds(iter_runtime_animated_positions(primitives)),
        "primitive_records": [
            {
                "name": primitive.name,
                "mesh_index": primitive.mesh_index,
                "shape_index": primitive.shape_index,
                "material_index": primitive.material_index,
                "visibility_id": primitive.visibility_id,
                "primitive_index": primitive.primitive_index,
                "skinning_mode": primitive.skinning_mode,
                "triangle_count": len(primitive.geometry.indices) // 3,
                "vertex_count": len(primitive.geometry.base_positions),
                "morph_target_count": len(primitive.geometry.target_position_deltas),
                "position_source": primitive.geometry.position_source,
                "normal_source": primitive.geometry.normal_source,
                "bone_index": primitive.geometry.bone_index,
            }
            for primitive in primitives[:sample_limit]
        ],
    }


def runtime_primitives_from_bind_pose(
    bind_pose_json: dict[str, object],
    *,
    selection_keys: frozenset[tuple[int, int]],
    world_transforms_by_frame: tuple[tuple[Matrix4, ...], ...],
    skin_transforms_by_frame: tuple[tuple[Matrix4, ...], ...],
    position_scale: float,
) -> list[RuntimePrimitiveFrameGeometry]:
    meshes = require_list(bind_pose_json.get("meshes"), "bind-pose meshes")
    primitives: list[RuntimePrimitiveFrameGeometry] = []
    for mesh in meshes:
        if not isinstance(mesh, dict):
            continue
        mesh_index = json_int(mesh.get("mesh_index"))
        shape_index = json_int(mesh.get("shape_index"))
        material_index = json_int(mesh.get("material_index"))
        visibility_id = json_int(mesh.get("visibility_id"))
        for primitive in require_list(mesh.get("primitives"), "bind-pose mesh primitives"):
            if not isinstance(primitive, dict):
                continue
            primitive_index = json_int(primitive.get("primitive_index"))
            if (mesh_index, primitive_index) not in selection_keys:
                continue
            skinning_mode = json_int(primitive.get("skinning_mode"))
            if skinning_mode in (1, 2):
                geometry = runtime_skinned_geometry(
                    primitive,
                    skin_transforms_by_frame=skin_transforms_by_frame,
                    position_scale=position_scale,
                )
            elif skinning_mode == 0:
                geometry = runtime_rigid_geometry(
                    primitive,
                    world_transforms_by_frame=world_transforms_by_frame,
                    position_scale=position_scale,
                )
            else:
                raise ParseError(f"unsupported selected runtime skinning mode {skinning_mode}")
            primitives.append(
                RuntimePrimitiveFrameGeometry(
                    mesh_index=mesh_index,
                    shape_index=shape_index,
                    material_index=material_index,
                    visibility_id=visibility_id,
                    primitive_index=primitive_index,
                    skinning_mode=skinning_mode,
                    name=(
                        f"minimal_runtime_mesh_{mesh_index:03d}"
                        f"__prim_{primitive_index:03d}"
                        f"__mode_{skinning_mode}"
                    ),
                    geometry=geometry,
                )
            )
    missing = sorted(selection_keys - {(p.mesh_index, p.primitive_index) for p in primitives})
    if missing:
        sample = ", ".join(f"{mesh}:{primitive}" for mesh, primitive in missing[:10])
        raise ParseError(f"selected primitive keys missing from bind-pose export: {sample}")
    return primitives


def runtime_skinned_geometry(
    primitive: dict[str, object],
    *,
    skin_transforms_by_frame: tuple[tuple[Matrix4, ...], ...],
    position_scale: float,
) -> AnimatedPrimitiveGeometry:
    vertices = require_list(primitive.get("vertices"), "skinned runtime primitive vertices")
    indices = read_indices(primitive)
    source_positions: list[Vec3] = []
    source_normals: list[Vec3] = []
    influences: list[object] = []
    texcoords: list[Vec2] = []
    colors: list[Color] = []
    for vertex in vertices:
        if not isinstance(vertex, dict):
            raise ParseError("skinned runtime vertex is not an object")
        source_positions.append(list_to_vec3(vertex.get("source_position")))
        source_normals.append(list_to_vec3(vertex.get("source_normal")))
        influences.append(vertex.get("influences"))
        texcoords.append(list_to_vec2(vertex.get("source_uv0")))
        colors.append(list_to_color(vertex.get("source_color_rgba")))

    frame_positions: list[tuple[Vec3, ...]] = []
    frame_normals: list[tuple[Vec3, ...]] = []
    for skin_transforms in skin_transforms_by_frame:
        positions: list[Vec3] = []
        normals: list[Vec3] = []
        for source_position, source_normal, vertex_influences in zip(source_positions, source_normals, influences):
            position, normal = weighted_skin_pose(
                source_position,
                source_normal,
                vertex_influences,
                skin_transforms,
            )
            positions.append(scale_vec3(position, position_scale))
            normals.append(normal)
        frame_positions.append(tuple(positions))
        frame_normals.append(tuple(normals))

    return animated_geometry_from_frames(
        frame_positions,
        frame_normals,
        texcoords=tuple(texcoords),
        colors=tuple(colors),
        indices=indices,
        position_source="minimal_runtime_pose_world_inverse_bind_world",
        normal_source="minimal_runtime_pose_world_inverse_bind_world",
        bone_index=None,
    )


def runtime_rigid_geometry(
    primitive: dict[str, object],
    *,
    world_transforms_by_frame: tuple[tuple[Matrix4, ...], ...],
    position_scale: float,
) -> AnimatedPrimitiveGeometry:
    vertices = require_list(primitive.get("vertices"), "rigid runtime primitive vertices")
    indices = read_indices(primitive)
    bone_index = rigid_primitive_bone_index(primitive)
    if any(bone_index < 0 or bone_index >= len(transforms) for transforms in world_transforms_by_frame):
        raise ParseError("rigid runtime primitive bone is outside sampled skeleton")

    source_positions: list[Vec3] = []
    source_normals: list[Vec3] = []
    texcoords: list[Vec2] = []
    colors: list[Color] = []
    for vertex in vertices:
        if not isinstance(vertex, dict):
            raise ParseError("rigid runtime vertex is not an object")
        source_positions.append(list_to_vec3(vertex.get("source_position")))
        source_normals.append(list_to_vec3(vertex.get("source_normal")))
        texcoords.append(list_to_vec2(vertex.get("source_uv0")))
        colors.append(list_to_color(vertex.get("source_color_rgba")))

    frame_positions: list[tuple[Vec3, ...]] = []
    frame_normals: list[tuple[Vec3, ...]] = []
    for world_transforms in world_transforms_by_frame:
        transform = world_transforms[bone_index]
        positions: list[Vec3] = []
        normals: list[Vec3] = []
        for source_position, source_normal in zip(source_positions, source_normals):
            positions.append(scale_vec3(transform_position(transform, source_position), position_scale))
            normals.append(transform_direction(transform, source_normal))
        frame_positions.append(tuple(positions))
        frame_normals.append(tuple(normals))

    return animated_geometry_from_frames(
        frame_positions,
        frame_normals,
        texcoords=tuple(texcoords),
        colors=tuple(colors),
        indices=indices,
        position_source="minimal_runtime_rigid_bone_world_source_position",
        normal_source="minimal_runtime_rigid_bone_world_source_normal",
        bone_index=bone_index,
    )


def extract_baked_glb_primitives(path: Path) -> dict[tuple[int, int], BakedGlbPrimitive]:
    document, binary = read_glb(path)
    nodes = require_list(document.get("nodes"), "GLB nodes")
    meshes = require_list(document.get("meshes"), "GLB meshes")
    out: dict[tuple[int, int], BakedGlbPrimitive] = {}
    for node in nodes:
        if not isinstance(node, dict) or "mesh" not in node:
            continue
        node_extras = node.get("extras", {})
        if not isinstance(node_extras, dict):
            continue
        mesh_index = json_int(node_extras.get("mesh_index"))
        primitive_index = json_int(node_extras.get("primitive_index"))
        if mesh_index < 0 or primitive_index < 0:
            continue
        gltf_mesh_index = json_int(node.get("mesh"))
        if gltf_mesh_index < 0 or gltf_mesh_index >= len(meshes):
            raise ParseError(f"{path}: GLB node references invalid mesh index")
        mesh = meshes[gltf_mesh_index]
        if not isinstance(mesh, dict):
            raise ParseError(f"{path}: GLB mesh is not an object")
        gltf_primitives = require_list(mesh.get("primitives"), "GLB mesh primitives")
        if len(gltf_primitives) != 1 or not isinstance(gltf_primitives[0], dict):
            raise ParseError(f"{path}: expected one GLB primitive per exported CMB primitive")
        primitive = gltf_primitives[0]
        attributes = require_dict(primitive.get("attributes"), "GLB primitive attributes")
        base_positions = read_accessor_vec3(document, binary, json_int(attributes.get("POSITION")))
        base_normals = read_accessor_vec3(document, binary, json_int(attributes.get("NORMAL")))
        indices = read_accessor_indices(document, binary, json_int(primitive.get("indices")))
        if any(index < 0 or index >= len(base_positions) for index in indices):
            raise ParseError(f"{path}: GLB index references a position outside the vertex array")
        if any(index < 0 or index >= len(base_normals) for index in indices):
            raise ParseError(f"{path}: GLB index references a normal outside the vertex array")
        target_positions: list[tuple[Vec3, ...]] = []
        target_normals: list[tuple[Vec3, ...]] = []
        for target in require_list(primitive.get("targets", []), "GLB morph targets"):
            if not isinstance(target, dict):
                raise ParseError(f"{path}: GLB morph target is not an object")
            target_positions.append(read_accessor_vec3(document, binary, json_int(target.get("POSITION"))))
            target_normals.append(read_accessor_vec3(document, binary, json_int(target.get("NORMAL"))))
        mesh_extras = mesh.get("extras", {})
        target_frames = mesh_extras.get("targetFrames", []) if isinstance(mesh_extras, dict) else []
        if not isinstance(target_frames, list):
            target_frames = []
        frame_numbers = (0, *tuple(int(frame) for frame in target_frames))
        positions_by_frame = [base_positions]
        normals_by_frame = [base_normals]
        for deltas in target_positions:
            if len(deltas) != len(base_positions):
                raise ParseError(f"{path}: target position count differs from base position count")
            positions_by_frame.append(tuple(add_vec3(base, delta) for base, delta in zip(base_positions, deltas)))
        for deltas in target_normals:
            if len(deltas) != len(base_normals):
                raise ParseError(f"{path}: target normal count differs from base normal count")
            normals_by_frame.append(tuple(add_vec3(base, delta) for base, delta in zip(base_normals, deltas)))
        if len(frame_numbers) != len(positions_by_frame):
            raise ParseError(f"{path}: GLB target frame count differs from morph target count")
        key = (mesh_index, primitive_index)
        if key in out:
            raise ParseError(f"{path}: duplicate GLB CMB primitive key {key}")
        out[key] = BakedGlbPrimitive(
            mesh_index=mesh_index,
            primitive_index=primitive_index,
            name=str(node.get("name") or mesh.get("name") or ""),
            frame_numbers=frame_numbers,
            indices=indices,
            positions_by_frame=tuple(positions_by_frame),
            normals_by_frame=tuple(normals_by_frame),
        )
    return out


def compare_baked_glb_primitives(
    runtime_primitives: dict[tuple[int, int], BakedGlbPrimitive],
    validated_primitives: dict[tuple[int, int], BakedGlbPrimitive],
    *,
    position_tolerance: float,
    normal_tolerance: float,
    sample_limit: int,
) -> dict[str, object]:
    runtime_keys = set(runtime_primitives)
    validated_keys = set(validated_primitives)
    issues: list[dict[str, object]] = []
    counts = Counter()
    max_position_delta = 0.0
    max_normal_delta = 0.0
    max_position_delta_record: dict[str, object] | None = None
    max_normal_delta_record: dict[str, object] | None = None

    for key in sorted(validated_keys - runtime_keys):
        add_compare_issue(issues, sample_limit, "missing_runtime_primitive", key)
    for key in sorted(runtime_keys - validated_keys):
        add_compare_issue(issues, sample_limit, "extra_runtime_primitive", key)

    for key in sorted(runtime_keys & validated_keys):
        runtime = runtime_primitives[key]
        validated = validated_primitives[key]
        if runtime.frame_numbers != validated.frame_numbers:
            add_compare_issue(
                issues,
                sample_limit,
                "frame_number_mismatch",
                key,
                runtime_frames=list(runtime.frame_numbers),
                validated_frames=list(validated.frame_numbers),
            )
            continue
        if len(runtime.positions_by_frame) != len(validated.positions_by_frame):
            add_compare_issue(issues, sample_limit, "position_frame_count_mismatch", key)
            continue
        if len(runtime.normals_by_frame) != len(validated.normals_by_frame):
            add_compare_issue(issues, sample_limit, "normal_frame_count_mismatch", key)
            continue
        if len(runtime.indices) != len(validated.indices):
            add_compare_issue(
                issues,
                sample_limit,
                "index_count_mismatch",
                key,
                runtime_count=len(runtime.indices),
                validated_count=len(validated.indices),
            )
            continue
        counts["matched_primitive_count"] += 1
        for frame_slot, frame_number in enumerate(runtime.frame_numbers):
            runtime_positions = expand_indexed_vec3s(runtime.positions_by_frame[frame_slot], runtime.indices)
            validated_positions = expand_indexed_vec3s(validated.positions_by_frame[frame_slot], validated.indices)
            runtime_normals = expand_indexed_vec3s(runtime.normals_by_frame[frame_slot], runtime.indices)
            validated_normals = expand_indexed_vec3s(validated.normals_by_frame[frame_slot], validated.indices)
            if len(runtime_positions) != len(validated_positions):
                add_compare_issue(
                    issues,
                    sample_limit,
                    "vertex_count_mismatch",
                    key,
                    frame=frame_number,
                    runtime_count=len(runtime_positions),
                    validated_count=len(validated_positions),
                )
                continue
            if len(runtime_normals) != len(validated_normals):
                add_compare_issue(
                    issues,
                    sample_limit,
                    "normal_count_mismatch",
                    key,
                    frame=frame_number,
                    runtime_count=len(runtime_normals),
                    validated_count=len(validated_normals),
                )
                continue
            counts["matched_frame_count"] += 1
            counts["matched_vertex_rows"] += len(runtime_positions)
            for vertex_index, (runtime_position, validated_position) in enumerate(
                zip(runtime_positions, validated_positions)
            ):
                delta = vec3_distance(runtime_position, validated_position)
                if delta > max_position_delta:
                    max_position_delta = delta
                    max_position_delta_record = compare_delta_record(
                        key,
                        frame_number,
                        vertex_index,
                        delta,
                        runtime_position,
                        validated_position,
                    )
                if delta > position_tolerance:
                    counts["position_mismatch_count"] += 1
                    add_compare_issue(
                        issues,
                        sample_limit,
                        "position_mismatch",
                        key,
                        frame=frame_number,
                        vertex_index=vertex_index,
                        delta=round_float(delta),
                        runtime=vec3_record(runtime_position),
                        validated=vec3_record(validated_position),
                    )
            for vertex_index, (runtime_normal, validated_normal) in enumerate(
                zip(runtime_normals, validated_normals)
            ):
                delta = vec3_distance(runtime_normal, validated_normal)
                if delta > max_normal_delta:
                    max_normal_delta = delta
                    max_normal_delta_record = compare_delta_record(
                        key,
                        frame_number,
                        vertex_index,
                        delta,
                        runtime_normal,
                        validated_normal,
                    )
                if delta > normal_tolerance:
                    counts["normal_mismatch_count"] += 1
                    add_compare_issue(
                        issues,
                        sample_limit,
                        "normal_mismatch",
                        key,
                        frame=frame_number,
                        vertex_index=vertex_index,
                        delta=round_float(delta),
                        runtime=vec3_record(runtime_normal),
                        validated=vec3_record(validated_normal),
                    )

    return {
        "status": "valid" if not issues and counts["position_mismatch_count"] == 0 and counts["normal_mismatch_count"] == 0 else "invalid",
        "runtime_primitive_count": len(runtime_primitives),
        "validated_primitive_count": len(validated_primitives),
        "matched_primitive_count": counts["matched_primitive_count"],
        "matched_frame_count": counts["matched_frame_count"],
        "matched_vertex_rows": counts["matched_vertex_rows"],
        "position_mismatch_count": counts["position_mismatch_count"],
        "normal_mismatch_count": counts["normal_mismatch_count"],
        "max_position_delta": round_float(max_position_delta),
        "max_position_delta_record": max_position_delta_record,
        "max_normal_delta": round_float(max_normal_delta),
        "max_normal_delta_record": max_normal_delta_record,
        "issue_count": len(issues),
        "issues": issues,
    }


def inspect_baked_glb_primitives(
    primitives: dict[tuple[int, int], BakedGlbPrimitive],
    *,
    sample_limit: int,
) -> dict[str, object]:
    counts = Counter()
    issues: list[dict[str, object]] = []
    frame_numbers: set[int] = set()
    for key, primitive in sorted(primitives.items()):
        counts["primitive_count"] += 1
        counts["base_position_row_count"] += len(primitive.positions_by_frame[0]) if primitive.positions_by_frame else 0
        counts["base_normal_row_count"] += len(primitive.normals_by_frame[0]) if primitive.normals_by_frame else 0
        if len(primitive.positions_by_frame) != len(primitive.normals_by_frame):
            add_compare_issue(issues, sample_limit, "position_normal_frame_count_mismatch", key)
        if len(primitive.frame_numbers) != len(primitive.positions_by_frame):
            add_compare_issue(issues, sample_limit, "position_frame_number_count_mismatch", key)
        if len(primitive.frame_numbers) != len(primitive.normals_by_frame):
            add_compare_issue(issues, sample_limit, "normal_frame_number_count_mismatch", key)
        frame_numbers.update(int(frame) for frame in primitive.frame_numbers)
        for frame, positions, normals in zip(
            primitive.frame_numbers,
            primitive.positions_by_frame,
            primitive.normals_by_frame,
        ):
            if len(positions) != len(normals):
                add_compare_issue(
                    issues,
                    sample_limit,
                    "position_normal_vertex_count_mismatch",
                    key,
                    frame=frame,
                    position_count=len(positions),
                    normal_count=len(normals),
                )
            counts["animated_position_row_count"] += len(positions)
            counts["animated_normal_row_count"] += len(normals)
            for vertex_index, position in enumerate(positions):
                if vec3_is_finite(position):
                    counts["finite_position_row_count"] += 1
                else:
                    add_compare_issue(
                        issues,
                        sample_limit,
                        "non_finite_position",
                        key,
                        frame=frame,
                        vertex_index=vertex_index,
                        value=vec3_record(position),
                    )
            for vertex_index, normal in enumerate(normals):
                if vec3_is_finite(normal):
                    counts["finite_normal_row_count"] += 1
                else:
                    add_compare_issue(
                        issues,
                        sample_limit,
                        "non_finite_normal",
                        key,
                        frame=frame,
                        vertex_index=vertex_index,
                        value=vec3_record(normal),
                    )

    return {
        "status": "valid" if not issues else "invalid",
        "primitive_count": counts["primitive_count"],
        "target_frame_count": len(frame_numbers),
        "base_position_row_count": counts["base_position_row_count"],
        "base_normal_row_count": counts["base_normal_row_count"],
        "animated_position_row_count": counts["animated_position_row_count"],
        "animated_normal_row_count": counts["animated_normal_row_count"],
        "finite_position_row_count": counts["finite_position_row_count"],
        "finite_normal_row_count": counts["finite_normal_row_count"],
        "issue_count": len(issues),
        "issues": issues,
    }


def find_track_export_file(
    binding_manifest: dict[str, object],
    *,
    archive_path: str,
    cmb_name: str,
    csab_name: str,
) -> Path:
    targets = require_list(binding_manifest.get("targets"), "skinned binding targets")
    normalized_archive = normalize_resource_path(archive_path)
    normalized_cmb = normalize_resource_path(cmb_name)
    normalized_csab = normalize_resource_path(csab_name)
    for target in targets:
        if not isinstance(target, dict):
            continue
        if normalize_resource_path(target.get("archive_path")) != normalized_archive:
            continue
        if normalize_resource_path(target.get("target_cmb_name")) != normalized_cmb:
            continue
        animations = require_list(target.get("animations"), "skinned binding target animations")
        for animation in animations:
            if not isinstance(animation, dict):
                continue
            if normalize_resource_path(animation.get("csab_name")) != normalized_csab:
                continue
            track_path = animation.get("track_export_file")
            if not track_path:
                raise ParseError(f"binding manifest has no track_export_file for {csab_name}")
            return Path(str(track_path))
    raise ParseError(f"binding manifest has no track export for {archive_path}!{cmb_name} / {csab_name}")


def source_archive_from_native_manifest(native_manifest: dict[str, object]) -> Path:
    source = native_manifest.get("source")
    if not isinstance(source, str) or "!" not in source:
        raise ParseError("native bind-pose manifest has no parseable source archive path")
    return Path(source.split("!", 1)[0])


def selection_manifest_from_native_manifest(native_manifest: dict[str, object]) -> Path | None:
    value = native_manifest.get("selection_source")
    if value is None:
        return None
    return Path(str(value))


def selected_primitive_keys(native_manifest: dict[str, object]) -> frozenset[tuple[int, int]]:
    keys = require_list(native_manifest.get("selection_primitive_keys"), "native selection primitive keys")
    out: set[tuple[int, int]] = set()
    for key in keys:
        if not isinstance(key, dict):
            continue
        mesh_index = json_int(key.get("mesh_index"))
        primitive_index = json_int(key.get("primitive_index"))
        if mesh_index >= 0 and primitive_index >= 0:
            out.add((mesh_index, primitive_index))
    if not out:
        raise ParseError("native bind-pose manifest has no selected primitive keys")
    return frozenset(out)


def rigid_primitive_bone_index(primitive: dict[str, object]) -> int:
    palette = primitive.get("bone_palette")
    if isinstance(palette, list):
        bone_indices = [int(item) for item in palette if isinstance(item, int) and not isinstance(item, bool)]
        if len(bone_indices) == 1:
            return bone_indices[0]
    bones: set[int] = set()
    for vertex in require_list(primitive.get("vertices"), "rigid runtime primitive vertices"):
        if not isinstance(vertex, dict):
            continue
        influences = vertex.get("influences")
        if not isinstance(influences, list):
            continue
        for influence in influences:
            if not isinstance(influence, dict):
                continue
            weight = float(influence.get("weight", 0.0) or 0.0)
            bone_index = influence.get("bone_index")
            if weight > 0.0 and isinstance(bone_index, int) and not isinstance(bone_index, bool):
                bones.add(bone_index)
    if len(bones) != 1:
        raise ParseError("rigid runtime primitive does not resolve to exactly one animated bone")
    return next(iter(bones))


def read_glb(path: Path) -> tuple[dict[str, object], bytes]:
    data = path.read_bytes()
    if len(data) < 20:
        raise ParseError(f"{path}: GLB is too short")
    magic, version, total_length = struct.unpack_from("<III", data, 0)
    if magic != 0x46546C67 or version != 2:
        raise ParseError(f"{path}: unsupported GLB header")
    if total_length != len(data):
        raise ParseError(f"{path}: GLB length header does not match file size")
    offset = 12
    document: dict[str, object] | None = None
    binary = b""
    while offset < len(data):
        if offset + 8 > len(data):
            raise ParseError(f"{path}: truncated GLB chunk header")
        chunk_length, chunk_type = struct.unpack_from("<II", data, offset)
        offset += 8
        chunk = data[offset : offset + chunk_length]
        if len(chunk) != chunk_length:
            raise ParseError(f"{path}: truncated GLB chunk")
        offset += chunk_length
        if chunk_type == 0x4E4F534A:
            parsed = json.loads(chunk.rstrip(b" ").decode("utf-8"))
            if not isinstance(parsed, dict):
                raise ParseError(f"{path}: GLB JSON chunk is not an object")
            document = parsed
        elif chunk_type == 0x004E4942:
            binary = bytes(chunk)
    if document is None:
        raise ParseError(f"{path}: missing GLB JSON chunk")
    return document, binary


def read_accessor_vec3(document: dict[str, object], binary: bytes, accessor_index: int) -> tuple[Vec3, ...]:
    if accessor_index < 0:
        raise ParseError("missing GLB VEC3 accessor")
    accessors = require_list(document.get("accessors"), "GLB accessors")
    buffer_views = require_list(document.get("bufferViews"), "GLB bufferViews")
    if accessor_index >= len(accessors) or not isinstance(accessors[accessor_index], dict):
        raise ParseError("GLB accessor index out of range")
    accessor = accessors[accessor_index]
    if json_int(accessor.get("componentType")) != GL_FLOAT or accessor.get("type") != "VEC3":
        raise ParseError("GLB accessor is not a FLOAT VEC3")
    view_index = json_int(accessor.get("bufferView"))
    if view_index < 0 or view_index >= len(buffer_views) or not isinstance(buffer_views[view_index], dict):
        raise ParseError("GLB accessor bufferView index out of range")
    view = buffer_views[view_index]
    count = json_int(accessor.get("count"))
    offset = json_int(view.get("byteOffset"), 0) + json_int(accessor.get("byteOffset"), 0)
    stride = json_int(view.get("byteStride"), 12)
    if stride < 12:
        raise ParseError("GLB VEC3 accessor byteStride is invalid")
    out: list[Vec3] = []
    for index in range(count):
        item_offset = offset + index * stride
        if item_offset + 12 > len(binary):
            raise ParseError("GLB VEC3 accessor reads past BIN chunk")
        out.append(Vec3(*struct.unpack_from("<fff", binary, item_offset)))
    return tuple(out)


def read_accessor_indices(document: dict[str, object], binary: bytes, accessor_index: int) -> tuple[int, ...]:
    if accessor_index < 0:
        raise ParseError("missing GLB index accessor")
    accessors = require_list(document.get("accessors"), "GLB accessors")
    buffer_views = require_list(document.get("bufferViews"), "GLB bufferViews")
    if accessor_index >= len(accessors) or not isinstance(accessors[accessor_index], dict):
        raise ParseError("GLB index accessor index out of range")
    accessor = accessors[accessor_index]
    if json_int(accessor.get("componentType")) != GL_UNSIGNED_INT or accessor.get("type") != "SCALAR":
        raise ParseError("GLB index accessor is not an UNSIGNED_INT SCALAR")
    view_index = json_int(accessor.get("bufferView"))
    if view_index < 0 or view_index >= len(buffer_views) or not isinstance(buffer_views[view_index], dict):
        raise ParseError("GLB index accessor bufferView index out of range")
    view = buffer_views[view_index]
    count = json_int(accessor.get("count"))
    offset = json_int(view.get("byteOffset"), 0) + json_int(accessor.get("byteOffset"), 0)
    stride = json_int(view.get("byteStride"), 4)
    if stride < 4:
        raise ParseError("GLB index accessor byteStride is invalid")
    out: list[int] = []
    for index in range(count):
        item_offset = offset + index * stride
        if item_offset + 4 > len(binary):
            raise ParseError("GLB index accessor reads past BIN chunk")
        out.append(int(struct.unpack_from("<I", binary, item_offset)[0]))
    return tuple(out)


def read_indices(primitive: dict[str, object]) -> tuple[int, ...]:
    raw = require_list(primitive.get("indices"), "runtime primitive indices")
    indices = tuple(int(index) for index in raw if isinstance(index, int) and not isinstance(index, bool))
    if len(indices) != len(raw) or len(indices) % 3 != 0:
        raise ParseError("runtime primitive indices are invalid")
    return indices


def expand_indexed_vec3s(values: tuple[Vec3, ...], indices: tuple[int, ...]) -> tuple[Vec3, ...]:
    return tuple(values[index] for index in indices)


def list_to_vec2(value: object) -> Vec2:
    if not isinstance(value, list) or len(value) != 2:
        raise ParseError("expected a 2-component vector")
    return Vec2(float(value[0]), float(value[1]))


def list_to_color(value: object) -> Color:
    if not isinstance(value, list) or len(value) != 4:
        return Color(255, 255, 255, 255)
    return Color(int(value[0]), int(value[1]), int(value[2]), int(value[3]))


def add_vec3(left: Vec3, right: Vec3) -> Vec3:
    return Vec3(left.x + right.x, left.y + right.y, left.z + right.z)


def vec3_is_finite(value: Vec3) -> bool:
    return math.isfinite(value.x) and math.isfinite(value.y) and math.isfinite(value.z)


def iter_runtime_animated_positions(primitives: Iterable[RuntimePrimitiveFrameGeometry]):
    for primitive in primitives:
        geometry = primitive.geometry
        for position in geometry.base_positions:
            yield position
        for target in geometry.target_position_deltas:
            for base, delta in zip(geometry.base_positions, target):
                yield add_vec3(base, delta)


def compare_delta_record(
    key: tuple[int, int],
    frame: int,
    vertex_index: int,
    delta: float,
    runtime: Vec3,
    validated: Vec3,
) -> dict[str, object]:
    return {
        "mesh_index": key[0],
        "primitive_index": key[1],
        "frame": frame,
        "vertex_index": vertex_index,
        "delta": round_float(delta),
        "runtime": vec3_record(runtime),
        "validated": vec3_record(validated),
    }


def add_compare_issue(
    issues: list[dict[str, object]],
    sample_limit: int,
    kind: str,
    key: tuple[int, int],
    **extra: object,
) -> None:
    if len(issues) >= sample_limit:
        return
    issue = {
        "kind": kind,
        "mesh_index": key[0],
        "primitive_index": key[1],
    }
    issue.update(extra)
    issues.append(issue)


def vec3_record(value: Vec3) -> list[float]:
    return [round_float(value.x), round_float(value.y), round_float(value.z)]


def require_dict(value: object, label: str) -> dict[str, object]:
    if not isinstance(value, dict):
        raise ParseError(f"{label} must be an object")
    return value


def require_list(value: object, label: str) -> list[object]:
    if value is None:
        return []
    if not isinstance(value, list):
        raise ParseError(f"{label} must be an array")
    return value


def read_json(path: Path) -> dict[str, object]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise ParseError(f"{path}: failed to read JSON") from exc
    except json.JSONDecodeError as exc:
        raise ParseError(f"{path}: invalid JSON") from exc
    if not isinstance(value, dict):
        raise ParseError(f"{path}: JSON root must be an object")
    return value


def normalize_resource_path(value: object) -> str:
    return str(value or "").replace("\\", "/").strip()


def json_int(value: object, fallback: int = -1) -> int:
    if isinstance(value, int) and not isinstance(value, bool):
        return value
    return fallback


def round_float(value: float) -> float:
    return round(float(value), 6)
