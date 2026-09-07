from __future__ import annotations

from collections import Counter
from dataclasses import dataclass
import json
import math
import struct
from pathlib import Path

from .actor_inventory import csab_header_candidates
from .binary import ParseError
from .cmb import CmbModel, Color, Material, Primitive, Shape, Texture, Vec2, Vec3
from .character_mesh_selection import (
    load_character_mesh_selection,
    validate_character_mesh_selection,
)
from .csab_tracks import find_archive_file
from .legacy_fast_resource import (
    Matrix4,
    LegacyFastResourceOptions,
    TEXTURE_ORIENTATION_NORMAL,
    material_primary_texture_index_for_profile,
    mesh_texture_export_profile,
    multiply_matrix,
    skeleton_world_transforms,
    transform_direction,
    transform_position,
)
from .skinned_animation import csab_pose_world_transform_samples
from .skinned_export import export_cmb_skinned_bind_pose
from .static_glb_export import (
    DEFAULT_GLB_TEXTURE_ORIENTATION,
    DEFAULT_POSITION_SCALE,
    GL_ARRAY_BUFFER,
    GL_ELEMENT_ARRAY_BUFFER,
    GL_FLOAT,
    GL_TRIANGLES,
    GL_UNSIGNED_INT,
    GlbBuilder,
    add_images,
    add_materials,
    add_issue,
    apply_profile_uv,
    bind_pose_meshes,
    cmb_material_records,
    cmb_texture_records,
    glb_texture_orientation_for_model,
    glb_uv,
    pack_colors,
    pack_indices,
    pack_vec2s,
    pack_vec3s,
    position_bounds,
    primitive_export_name,
    read_color,
    read_vec2,
    read_vec3,
    rigid_uv_offset,
    scale_vec3,
    skinned_primitive_records,
    skinned_uv_offset,
    vec3_max,
    vec3_min,
)
from .zar import ZarArchive


ANIMATION_GLB_MANIFEST_FORMAT = "oot3d_skinned_animation_glb_export_v1"
DEFAULT_ANIMATION_FPS = 60.0


@dataclass(frozen=True)
class AnimationGlbOptions:
    output: Path
    csab_name: str
    cmb_name: str
    manifest_output: Path | None = None
    selection_manifest: Path | None = None
    texture_orientation: str | None = DEFAULT_GLB_TEXTURE_ORIENTATION
    uv_orientation: str = TEXTURE_ORIENTATION_NORMAL
    position_scale: float = DEFAULT_POSITION_SCALE
    frame_step: int = 1
    fps: float = DEFAULT_ANIMATION_FPS
    interpolation: str = "STEP"
    sample_limit: int = 100


@dataclass(frozen=True)
class AnimatedPrimitiveGeometry:
    base_positions: tuple[Vec3, ...]
    base_normals: tuple[Vec3, ...]
    target_position_deltas: tuple[tuple[Vec3, ...], ...]
    target_normal_deltas: tuple[tuple[Vec3, ...], ...]
    texcoords: tuple[Vec2, ...]
    colors: tuple[Color, ...]
    indices: tuple[int, ...]
    position_source: str
    normal_source: str
    bone_index: int | None = None


@dataclass(frozen=True)
class AnimatedExportPrimitive:
    mesh_index: int
    shape_index: int
    material_index: int
    visibility_id: int
    primitive_index: int
    skinning_mode: int
    texture_index: int | None
    texture_name: str | None
    name: str
    geometry: AnimatedPrimitiveGeometry


def export_skinned_animation_glb(zar_path: Path, options: AnimationGlbOptions) -> dict[str, object]:
    if options.frame_step <= 0:
        raise ParseError(f"frame step must be positive, got {options.frame_step!r}")
    if not math.isfinite(options.fps) or options.fps <= 0.0:
        raise ParseError(f"fps must be positive and finite, got {options.fps!r}")
    if not math.isfinite(options.position_scale) or options.position_scale <= 0.0:
        raise ParseError(f"position scale must be positive and finite, got {options.position_scale!r}")
    interpolation = normalize_animation_interpolation(options.interpolation)

    archive = ZarArchive.from_path(zar_path)
    cmb_file = find_archive_file(archive, options.cmb_name)
    csab_file = find_archive_file(archive, options.csab_name)
    cmb_data = archive.read_file(cmb_file)
    csab_data = archive.read_file(csab_file)
    source = f"{zar_path}!{cmb_file.name}"
    model = CmbModel.parse(cmb_data, source)

    selection = (
        load_character_mesh_selection(options.selection_manifest)
        if options.selection_manifest is not None
        else None
    )
    if selection is not None:
        validate_character_mesh_selection(selection, model)

    texture_orientation = (
        options.texture_orientation
        if options.texture_orientation is not None
        else selection.texture_orientation
        if selection is not None
        else None
    )
    uv_orientation = selection.uv_orientation if selection is not None and selection.uv_orientation else options.uv_orientation
    position_scale = selection.position_scale if selection is not None and selection.position_scale else options.position_scale

    bind_pose = export_cmb_skinned_bind_pose(
        cmb_data,
        source=source,
        embedded_name=cmb_file.name,
        sample_limit=options.sample_limit,
    )
    if bind_pose.get("format") != "oot3d_skinned_bind_pose_export_v1":
        raise ParseError(f"{source}: unexpected skinned bind-pose export format")

    frame_count = csab_header_candidates(csab_data).get("frame_count_candidate")
    if frame_count is None:
        raise ParseError(f"{csab_file.name}: missing CSAB frame count candidate")
    sample_frames = list(range(0, int(frame_count) + 1, options.frame_step))
    if sample_frames[-1] != int(frame_count):
        sample_frames.append(int(frame_count))
    pose_context = csab_pose_world_transform_samples(
        csab_data,
        model,
        sample_frames=sample_frames,
    )
    frame_records = pose_context["frames"]
    if not isinstance(frame_records, list) or not frame_records:
        raise ParseError(f"{csab_file.name}: no sampled pose frames")
    frame_numbers = [int(record["frame"]) for record in frame_records if isinstance(record, dict)]
    frame_transforms = [
        record["world_transforms"]
        for record in frame_records
        if isinstance(record, dict)
    ]
    if len(frame_numbers) != len(frame_transforms):
        raise ParseError(f"{csab_file.name}: invalid pose frame records")
    bind_world_transforms = skeleton_world_transforms(model.skeleton)
    inverse_bind_world_transforms = tuple(invert_affine_matrix(transform) for transform in bind_world_transforms)
    frame_skin_transforms = tuple(
        tuple(
            multiply_matrix(pose_transform, inverse_bind_world_transforms[bone_index])
            for bone_index, pose_transform in enumerate(pose_transforms)
        )
        for pose_transforms in frame_transforms
    )

    shipwright_options = LegacyFastResourceOptions(
        output_dir=options.output.parent,
        resource_root="glb/skinned_animation",
        symbol=options.output.stem,
        include_textures=True,
        allow_nonstatic=True,
        texture_orientation=glb_texture_orientation_for_model(model, texture_orientation),
        uv_orientation=uv_orientation,
    )
    texture_profile = mesh_texture_export_profile(model, shipwright_options)

    builder = GlbBuilder()
    builder.json["asset"] = {
        "version": "2.0",
        "generator": "oot3d-asset-tool animation_glb_export",
    }
    image_indices = add_images(builder, model, texture_profile.texture_orientation)
    material_indices = add_materials(builder, model, texture_profile, image_indices)

    bind_pose_mesh_by_index = {
        int(mesh.get("mesh_index", -1)): mesh
        for mesh in bind_pose_meshes(bind_pose)
        if isinstance(mesh.get("mesh_index"), int)
    }
    selection_keys = selection.primitive_keys if selection is not None else all_primitive_keys(model)
    selection_mesh_indices = {mesh_index for mesh_index, _ in selection_keys}

    counts: Counter[str] = Counter()
    issues: list[dict[str, object]] = []
    exported_primitives: list[AnimatedExportPrimitive] = []
    node_indices: list[int] = []
    target_frame_numbers = tuple(frame_numbers[1:])

    for mesh in model.meshes:
        if mesh.index not in selection_mesh_indices:
            counts["filtered_mesh"] += 1
            continue
        if mesh.shape_index < 0 or mesh.shape_index >= len(model.shapes):
            add_issue(issues, options.sample_limit, mesh.index, None, "mesh_references_missing_shape")
            continue
        shape = model.shapes[mesh.shape_index]
        material = (
            model.materials[mesh.material_index]
            if 0 <= mesh.material_index < len(model.materials)
            else None
        )
        texture_index = (
            material_primary_texture_index_for_profile(model, material, texture_profile)
            if material is not None
            else None
        )
        texture = (
            model.textures[texture_index]
            if texture_index is not None and 0 <= texture_index < len(model.textures)
            else None
        )
        skinned_primitives = skinned_primitive_records(bind_pose_mesh_by_index.get(mesh.index))

        for primitive_index, primitive in enumerate(shape.primitives):
            if (mesh.index, primitive_index) not in selection_keys:
                counts["filtered_primitive"] += 1
                continue
            if not primitive.indices:
                counts["empty_primitive"] += 1
                continue

            geometry: AnimatedPrimitiveGeometry | None
            if primitive.skinning_mode in (1, 2):
                primitive_json = skinned_primitives.get(primitive_index)
                if primitive_json is None:
                    add_issue(
                        issues,
                        options.sample_limit,
                        mesh.index,
                        primitive_index,
                        "missing_skinned_bind_pose_primitive",
                    )
                    continue
                geometry = skinned_animated_primitive_geometry(
                    primitive_json,
                    frame_skin_transforms=frame_skin_transforms,
                    material=material,
                    texture=texture,
                    texture_profile=texture_profile,
                    position_scale=position_scale,
                )
                counts["skinned_primitive"] += 1
            elif primitive.skinning_mode == 0:
                geometry = rigid_animated_primitive_geometry(
                    primitive,
                    shape,
                    frame_transforms=frame_transforms,
                    material=material,
                    texture=texture,
                    texture_profile=texture_profile,
                    mesh_index=mesh.index,
                    primitive_index=primitive_index,
                    issues=issues,
                    sample_limit=options.sample_limit,
                    position_scale=position_scale,
                )
                if geometry is None:
                    continue
                counts["rigid_primitive"] += 1
            else:
                add_issue(
                    issues,
                    options.sample_limit,
                    mesh.index,
                    primitive_index,
                    "unsupported_skinning_mode",
                )
                continue

            name = primitive_export_name(
                model,
                mesh_index=mesh.index,
                shape_index=mesh.shape_index,
                material_index=mesh.material_index,
                visibility_id=mesh.visibility_id,
                primitive_index=primitive_index,
                skinning_mode=primitive.skinning_mode,
                texture=texture,
            )
            export_primitive = AnimatedExportPrimitive(
                mesh_index=mesh.index,
                shape_index=mesh.shape_index,
                material_index=mesh.material_index,
                visibility_id=mesh.visibility_id,
                primitive_index=primitive_index,
                skinning_mode=primitive.skinning_mode,
                texture_index=texture.index if texture is not None else None,
                texture_name=texture.name if texture is not None else None,
                name=name,
                geometry=geometry,
            )
            node_indices.append(
                add_animated_primitive_node(
                    builder,
                    export_primitive,
                    material_indices.get(material.index) if material else None,
                    target_frame_numbers=target_frame_numbers,
                )
            )
            exported_primitives.append(export_primitive)
            counts["primitive"] += 1
            counts["triangle"] += len(geometry.indices) // 3
            counts["vertex"] += len(geometry.base_positions)
            counts["morph_target"] += len(geometry.target_position_deltas)

    add_morph_weight_animation(
        builder,
        node_indices,
        frame_numbers=tuple(frame_numbers),
        target_count=max(0, len(frame_numbers) - 1),
        fps=options.fps,
        interpolation=interpolation,
    )

    manifest = {
        "format": ANIMATION_GLB_MANIFEST_FORMAT,
        "source_zar": str(zar_path),
        "cmb_name": cmb_file.name,
        "csab_name": csab_file.name,
        "model_name": model.name,
        "output": str(options.output),
        "selection_source": selection.source_path if selection is not None else None,
        "selection_profile_id": selection.profile_id if selection is not None else None,
        "selection_model_name": selection.model_name if selection is not None else None,
        "selection_primitive_keys": [
            {"mesh_index": mesh_index, "primitive_index": primitive_index}
            for mesh_index, primitive_index in sorted(selection_keys)
        ],
        "texture_orientation": texture_profile.texture_orientation,
        "uv_orientation": texture_profile.uv_orientation,
        "position_scale": position_scale,
        "frame_count_candidate": frame_count,
        "frame_step": options.frame_step,
        "fps": options.fps,
        "interpolation": interpolation,
        "sample_frames": frame_numbers,
        "target_frames": list(target_frame_numbers),
        "frame_times_seconds": [round(frame / options.fps, 6) for frame in frame_numbers],
        "pose_counts": pose_context["counts"],
        "bind_pose_counts": bind_pose["counts"],
        "counts": {
            "mesh_count": len({primitive.mesh_index for primitive in exported_primitives}),
            "primitive_count": counts["primitive"],
            "skinned_primitive_count": counts["skinned_primitive"],
            "rigid_primitive_count": counts["rigid_primitive"],
            "triangle_count": counts["triangle"],
            "vertex_count": counts["vertex"],
            "morph_target_count": counts["morph_target"],
            "filtered_mesh_count": counts["filtered_mesh"],
            "filtered_primitive_count": counts["filtered_primitive"],
            "empty_primitive_count": counts["empty_primitive"],
            "issue_count": len(issues),
        },
        "bounds": position_bounds(
            position
            for primitive in exported_primitives
            for position in primitive.geometry.base_positions
        ),
        "animated_bounds": position_bounds(
            position
            for primitive in exported_primitives
            for position in animated_positions(primitive.geometry)
        ),
        "max_morph_position_delta": round_float(
            max(
                (
                    vec3_length(delta)
                    for primitive in exported_primitives
                    for target in primitive.geometry.target_position_deltas
                    for delta in target
                ),
                default=0.0,
            )
        ),
        "textures": cmb_texture_records(model.textures),
        "materials": cmb_material_records(model.materials, model.textures),
        "mesh_records": [animated_primitive_manifest_record(primitive) for primitive in exported_primitives],
        "issues": issues,
        "animation_contract": (
            "CSAB bone poses are sampled at integer frames. Skinned CMB source_position/source_normal data is "
            "deformed with pose_world * inverse(bind_world) skin matrices. Selected CMB primitives are baked as "
            "glTF morph targets: frame 0 is the base mesh, later frames are position/normal deltas animated through "
            "one-hot weight tracks. This is a diagnostic GLB for animation interpretation, not the final runtime "
            "skinning format."
        ),
    }

    builder.write_glb(options.output)
    manifest_path = options.manifest_output or options.output.with_suffix(".manifest.json")
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8", newline="\n")
    return manifest


def all_primitive_keys(model: CmbModel) -> frozenset[tuple[int, int]]:
    keys: set[tuple[int, int]] = set()
    for mesh in model.meshes:
        if 0 <= mesh.shape_index < len(model.shapes):
            for primitive_index, _primitive in enumerate(model.shapes[mesh.shape_index].primitives):
                keys.add((mesh.index, primitive_index))
    return frozenset(keys)


def skinned_animated_primitive_geometry(
    primitive_json: dict[str, object],
    *,
    frame_skin_transforms,
    material: Material | None,
    texture: Texture | None,
    texture_profile,
    position_scale: float,
) -> AnimatedPrimitiveGeometry:
    vertices_json = primitive_json.get("vertices")
    indices_json = primitive_json.get("indices")
    if not isinstance(vertices_json, list) or not isinstance(indices_json, list):
        raise ParseError("skinned primitive record is missing vertices or indices")

    source_positions: list[Vec3] = []
    source_normals: list[Vec3] = []
    influences: list[object] = []
    profile_uvs: list[Vec2] = []
    colors: list[Color] = []

    for vertex_json in vertices_json:
        if not isinstance(vertex_json, dict):
            raise ParseError("skinned primitive contains a non-object vertex record")
        source_position = read_vec3(vertex_json.get("source_position"))
        source_normal = read_vec3(vertex_json.get("source_normal"), default=Vec3(0.0, 0.0, 1.0))
        source_positions.append(source_position)
        source_normals.append(source_normal)
        influences.append(vertex_json.get("influences"))
        uv = read_vec2(vertex_json.get("source_uv0"))
        profile_uvs.append(apply_profile_uv(uv, material, texture, texture_profile))
        colors.append(read_color(vertex_json.get("source_color_rgba")))

    indices = tuple(int(index) for index in indices_json if isinstance(index, int))
    if len(indices) != len(indices_json) or len(indices) % 3 != 0:
        raise ParseError("skinned primitive index array is invalid")
    if any(index < 0 or index >= len(source_positions) for index in indices):
        raise ParseError("skinned primitive index references an invalid vertex")

    frame_positions: list[tuple[Vec3, ...]] = []
    frame_normals: list[tuple[Vec3, ...]] = []
    for transforms in frame_skin_transforms:
        positions: list[Vec3] = []
        normals: list[Vec3] = []
        for source_position, source_normal, vertex_influences in zip(
            source_positions,
            source_normals,
            influences,
        ):
            pose_position, pose_normal = weighted_skin_pose(
                source_position,
                source_normal,
                vertex_influences,
                transforms,
            )
            positions.append(scale_vec3(pose_position, position_scale))
            normals.append(pose_normal)
        frame_positions.append(tuple(positions))
        frame_normals.append(tuple(normals))

    uv_offset = skinned_uv_offset(profile_uvs, indices, material, texture)
    texcoords = tuple(glb_uv(Vec2(uv.x + uv_offset.x, uv.y + uv_offset.y)) for uv in profile_uvs)
    return animated_geometry_from_frames(
        frame_positions,
        frame_normals,
        texcoords=texcoords,
        colors=tuple(colors),
        indices=indices,
        position_source="csab_weighted_source_position",
        normal_source="csab_weighted_source_normal",
        bone_index=None,
    )


def rigid_animated_primitive_geometry(
    primitive: Primitive,
    shape: Shape,
    *,
    frame_transforms,
    material: Material | None,
    texture: Texture | None,
    texture_profile,
    mesh_index: int,
    primitive_index: int,
    issues: list[dict[str, object]],
    sample_limit: int,
    position_scale: float,
) -> AnimatedPrimitiveGeometry | None:
    if len(primitive.bone_indices) != 1:
        add_issue(
            issues,
            sample_limit,
            mesh_index,
            primitive_index,
            "rigid_primitive_requires_single_bone",
        )
        return None
    bone_index = primitive.bone_indices[0]
    if any(bone_index < 0 or bone_index >= len(transforms) for transforms in frame_transforms):
        add_issue(
            issues,
            sample_limit,
            mesh_index,
            primitive_index,
            "rigid_primitive_bone_out_of_range",
        )
        return None
    if len(primitive.indices) % 3 != 0:
        add_issue(
            issues,
            sample_limit,
            mesh_index,
            primitive_index,
            "rigid_primitive_index_count_not_triangular",
        )
        return None
    if any(index < 0 or index >= len(shape.positions) for index in primitive.indices):
        add_issue(
            issues,
            sample_limit,
            mesh_index,
            primitive_index,
            "rigid_primitive_index_out_of_range",
        )
        return None

    local_index: dict[int, int] = {}
    source_indices: list[int] = []
    remapped_indices: list[int] = []
    for source_index in primitive.indices:
        mapped = local_index.get(source_index)
        if mapped is None:
            mapped = len(source_indices)
            local_index[source_index] = mapped
            source_indices.append(source_index)
        remapped_indices.append(mapped)

    uv_offset = rigid_uv_offset(shape, primitive.indices, material, texture, texture_profile)
    texcoords: list[Vec2] = []
    colors: list[Color] = []
    for source_index in source_indices:
        uv = shape.uv0[source_index] if source_index < len(shape.uv0) else Vec2(0.0, 0.0)
        uv = apply_profile_uv(uv, material, texture, texture_profile)
        texcoords.append(glb_uv(Vec2(uv.x + uv_offset.x, uv.y + uv_offset.y)))
        colors.append(shape.colors[source_index] if source_index < len(shape.colors) else Color(255, 255, 255, 255))

    frame_positions: list[tuple[Vec3, ...]] = []
    frame_normals: list[tuple[Vec3, ...]] = []
    for transforms in frame_transforms:
        transform = transforms[bone_index] if texture_profile.apply_bone_transform else None
        positions: list[Vec3] = []
        normals: list[Vec3] = []
        for source_index in source_indices:
            position = shape.positions[source_index]
            normal = shape.normals[source_index] if source_index < len(shape.normals) else Vec3(0.0, 0.0, 1.0)
            if transform is not None:
                position = transform_position(transform, position)
                normal = transform_direction(transform, normal)
            positions.append(scale_vec3(position, position_scale))
            normals.append(normal)
        frame_positions.append(tuple(positions))
        frame_normals.append(tuple(normals))

    return animated_geometry_from_frames(
        frame_positions,
        frame_normals,
        texcoords=tuple(texcoords),
        colors=tuple(colors),
        indices=tuple(remapped_indices),
        position_source=(
            "csab_rigid_bone_source_position"
            if texture_profile.apply_bone_transform
            else "source_shape_position"
        ),
        normal_source=(
            "csab_rigid_bone_source_normal"
            if texture_profile.apply_bone_transform
            else "source_shape_normal"
        ),
        bone_index=bone_index,
    )


def animated_geometry_from_frames(
    frame_positions: list[tuple[Vec3, ...]],
    frame_normals: list[tuple[Vec3, ...]],
    *,
    texcoords: tuple[Vec2, ...],
    colors: tuple[Color, ...],
    indices: tuple[int, ...],
    position_source: str,
    normal_source: str,
    bone_index: int | None,
) -> AnimatedPrimitiveGeometry:
    if not frame_positions or not frame_normals:
        raise ParseError("animated primitive has no frame geometry")
    base_positions = frame_positions[0]
    base_normals = frame_normals[0]
    vertex_count = len(base_positions)
    if len(base_normals) != vertex_count:
        raise ParseError("animated primitive base position/normal counts differ")
    if any(len(frame) != vertex_count for frame in frame_positions):
        raise ParseError("animated primitive position frame counts differ")
    if any(len(frame) != vertex_count for frame in frame_normals):
        raise ParseError("animated primitive normal frame counts differ")
    if len(texcoords) != vertex_count or len(colors) != vertex_count:
        raise ParseError("animated primitive attribute counts differ")

    position_deltas = tuple(
        tuple(sub_vec3(position, base) for position, base in zip(frame, base_positions))
        for frame in frame_positions[1:]
    )
    normal_deltas = tuple(
        tuple(sub_vec3(normal, base) for normal, base in zip(frame, base_normals))
        for frame in frame_normals[1:]
    )
    return AnimatedPrimitiveGeometry(
        base_positions=base_positions,
        base_normals=base_normals,
        target_position_deltas=position_deltas,
        target_normal_deltas=normal_deltas,
        texcoords=texcoords,
        colors=colors,
        indices=indices,
        position_source=position_source,
        normal_source=normal_source,
        bone_index=bone_index,
    )


def add_animated_primitive_node(
    builder: GlbBuilder,
    primitive: AnimatedExportPrimitive,
    material_index: int | None,
    *,
    target_frame_numbers: tuple[int, ...],
) -> int:
    geometry = primitive.geometry
    position_accessor = builder.add_accessor(
        pack_vec3s(geometry.base_positions),
        component_type=GL_FLOAT,
        count=len(geometry.base_positions),
        accessor_type="VEC3",
        target=GL_ARRAY_BUFFER,
        minimum=vec3_min(geometry.base_positions),
        maximum=vec3_max(geometry.base_positions),
    )
    normal_accessor = builder.add_accessor(
        pack_vec3s(geometry.base_normals),
        component_type=GL_FLOAT,
        count=len(geometry.base_normals),
        accessor_type="VEC3",
        target=GL_ARRAY_BUFFER,
    )
    texcoord_accessor = builder.add_accessor(
        pack_vec2s(geometry.texcoords),
        component_type=GL_FLOAT,
        count=len(geometry.texcoords),
        accessor_type="VEC2",
        target=GL_ARRAY_BUFFER,
    )
    color_accessor = builder.add_accessor(
        pack_colors(geometry.colors),
        component_type=GL_FLOAT,
        count=len(geometry.colors),
        accessor_type="VEC4",
        target=GL_ARRAY_BUFFER,
    )
    index_accessor = builder.add_accessor(
        pack_indices(geometry.indices),
        component_type=GL_UNSIGNED_INT,
        count=len(geometry.indices),
        accessor_type="SCALAR",
        target=GL_ELEMENT_ARRAY_BUFFER,
    )

    targets: list[dict[str, int]] = []
    for position_deltas, normal_deltas in zip(
        geometry.target_position_deltas,
        geometry.target_normal_deltas,
    ):
        targets.append(
            {
                "POSITION": builder.add_accessor(
                    pack_vec3s(position_deltas),
                    component_type=GL_FLOAT,
                    count=len(position_deltas),
                    accessor_type="VEC3",
                    target=GL_ARRAY_BUFFER,
                    minimum=vec3_min(position_deltas),
                    maximum=vec3_max(position_deltas),
                ),
                "NORMAL": builder.add_accessor(
                    pack_vec3s(normal_deltas),
                    component_type=GL_FLOAT,
                    count=len(normal_deltas),
                    accessor_type="VEC3",
                    target=GL_ARRAY_BUFFER,
                ),
            }
        )

    primitive_json: dict[str, object] = {
        "attributes": {
            "POSITION": position_accessor,
            "NORMAL": normal_accessor,
            "TEXCOORD_0": texcoord_accessor,
            "COLOR_0": color_accessor,
        },
        "indices": index_accessor,
        "mode": GL_TRIANGLES,
    }
    if material_index is not None:
        primitive_json["material"] = material_index
    if targets:
        primitive_json["targets"] = targets

    meshes = builder.json["meshes"]
    nodes = builder.json["nodes"]
    scenes = builder.json["scenes"]
    assert isinstance(meshes, list)
    assert isinstance(nodes, list)
    assert isinstance(scenes, list)
    mesh_index = len(meshes)
    meshes.append(
        {
            "name": primitive.name,
            "primitives": [primitive_json],
            "weights": [0.0 for _target in targets],
            "extras": {
                "targetFrames": list(target_frame_numbers),
                "targetNames": [f"frame_{frame:03d}" for frame in target_frame_numbers],
            },
        }
    )
    node_index = len(nodes)
    nodes.append(
        {
            "name": primitive.name,
            "mesh": mesh_index,
            "extras": {
                "mesh_index": primitive.mesh_index,
                "shape_index": primitive.shape_index,
                "material_index": primitive.material_index,
                "visibility_id": primitive.visibility_id,
                "primitive_index": primitive.primitive_index,
                "skinning_mode": primitive.skinning_mode,
                "texture_index": primitive.texture_index,
                "texture_name": primitive.texture_name,
                "position_source": geometry.position_source,
                "normal_source": geometry.normal_source,
                "bone_index": geometry.bone_index,
                "triangle_count": len(geometry.indices) // 3,
                "vertex_count": len(geometry.base_positions),
            },
        }
    )
    scene = scenes[0]
    assert isinstance(scene, dict)
    scene_nodes = scene["nodes"]
    assert isinstance(scene_nodes, list)
    scene_nodes.append(node_index)
    return node_index


def add_morph_weight_animation(
    builder: GlbBuilder,
    node_indices: list[int],
    *,
    frame_numbers: tuple[int, ...],
    target_count: int,
    fps: float,
    interpolation: str,
) -> None:
    if target_count <= 0 or not node_indices:
        return
    times = tuple(float(frame) / fps for frame in frame_numbers)
    input_accessor = builder.add_accessor(
        pack_floats(times),
        component_type=GL_FLOAT,
        count=len(times),
        accessor_type="SCALAR",
        target=None,
        minimum=[min(times)],
        maximum=[max(times)],
    )
    weights: list[float] = []
    for frame_index, _frame in enumerate(frame_numbers):
        for target_index in range(target_count):
            weights.append(1.0 if frame_index == target_index + 1 else 0.0)
    output_accessor = builder.add_accessor(
        pack_floats(tuple(weights)),
        component_type=GL_FLOAT,
        count=len(weights),
        accessor_type="SCALAR",
        target=None,
    )

    animations = builder.json.setdefault("animations", [])
    assert isinstance(animations, list)
    animations.append(
        {
            "name": "csab_baked_morph",
            "samplers": [
                {
                    "input": input_accessor,
                    "output": output_accessor,
                    "interpolation": interpolation,
                }
            ],
            "channels": [
                {
                    "sampler": 0,
                    "target": {
                        "node": node_index,
                        "path": "weights",
                    },
                }
                for node_index in node_indices
            ],
        }
    )


def animated_primitive_manifest_record(primitive: AnimatedExportPrimitive) -> dict[str, object]:
    return {
        "name": primitive.name,
        "mesh_index": primitive.mesh_index,
        "shape_index": primitive.shape_index,
        "material_index": primitive.material_index,
        "visibility_id": primitive.visibility_id,
        "primitive_index": primitive.primitive_index,
        "skinning_mode": primitive.skinning_mode,
        "texture_index": primitive.texture_index,
        "texture_name": primitive.texture_name,
        "position_source": primitive.geometry.position_source,
        "normal_source": primitive.geometry.normal_source,
        "bone_index": primitive.geometry.bone_index,
        "vertex_count": len(primitive.geometry.base_positions),
        "triangle_count": len(primitive.geometry.indices) // 3,
        "morph_target_count": len(primitive.geometry.target_position_deltas),
        "bounds": position_bounds(primitive.geometry.base_positions),
        "animated_bounds": position_bounds(animated_positions(primitive.geometry)),
        "max_morph_position_delta": round_float(
            max(
                (
                    vec3_length(delta)
                    for target in primitive.geometry.target_position_deltas
                    for delta in target
                ),
                default=0.0,
            )
        ),
    }


def normalize_animation_interpolation(value: str) -> str:
    interpolation = value.strip().upper()
    if interpolation not in {"STEP", "LINEAR"}:
        raise ParseError(f"unsupported animation interpolation {value!r}; expected STEP or LINEAR")
    return interpolation


def animated_positions(geometry: AnimatedPrimitiveGeometry):
    for position in geometry.base_positions:
        yield position
    for target in geometry.target_position_deltas:
        for base, delta in zip(geometry.base_positions, target):
            yield Vec3(base.x + delta.x, base.y + delta.y, base.z + delta.z)


def sub_vec3(left: Vec3, right: Vec3) -> Vec3:
    return Vec3(left.x - right.x, left.y - right.y, left.z - right.z)


def weighted_skin_pose(
    position: Vec3,
    normal: Vec3,
    influences: object,
    skin_transforms: tuple[Matrix4, ...],
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
        if bone_index < 0 or bone_index >= len(skin_transforms):
            continue
        skin_transform = skin_transforms[bone_index]
        transformed_position = transform_position(skin_transform, position)
        transformed_normal = transform_direction(skin_transform, normal)
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


def normalize_or_default(value: Vec3, default: Vec3) -> Vec3:
    length = vec3_length(value)
    if length <= 0.000001:
        return default
    return Vec3(value.x / length, value.y / length, value.z / length)


def vec3_length(value: Vec3) -> float:
    return math.sqrt(value.x * value.x + value.y * value.y + value.z * value.z)


def round_float(value: float) -> float:
    return round(float(value), 6)


def pack_floats(values: tuple[float, ...]) -> bytes:
    return b"".join(struct.pack("<f", value) for value in values)
