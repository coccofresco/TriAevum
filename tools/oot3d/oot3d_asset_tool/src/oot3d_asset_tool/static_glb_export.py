from __future__ import annotations

import binascii
import json
import math
import struct
import zlib
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from .binary import ParseError
from .cmb import (
    Color,
    CmbModel,
    Material,
    PICA_TEXTURE_WRAP_CLAMP,
    PICA_TEXTURE_WRAP_CLAMP_TO_BORDER,
    PICA_TEXTURE_WRAP_CLAMP_TO_EDGE,
    PICA_TEXTURE_WRAP_MIRRORED_REPEAT,
    PICA_TEXTURE_WRAP_REPEAT,
    Primitive,
    Shape,
    Texture,
    Vec2,
    Vec3,
)
from .character_mesh_selection import load_character_mesh_selection, validate_character_mesh_selection
from .legacy_fast_resource import (
    LegacyFastResourceOptions,
    TEXTURE_ORIENTATION_FLIP_X,
    TEXTURE_ORIENTATION_FLIP_XY,
    TEXTURE_ORIENTATION_FLIP_Y,
    TEXTURE_ORIENTATION_NORMAL,
    integer_repeat_offset,
    material_primary_texture_index_for_profile,
    material_texture_mapper,
    mesh_texture_export_profile,
    normalize_uv_orientation,
    orient_rgba16_image,
    skeleton_world_transforms,
    transform_direction,
    transform_position,
    transform_uv,
)
from .skinned_export import (
    export_cmb_skinned_bind_pose,
    load_cmb_payload,
)


GL_ARRAY_BUFFER = 34962
GL_ELEMENT_ARRAY_BUFFER = 34963
GL_FLOAT = 5126
GL_UNSIGNED_INT = 5125
GL_TRIANGLES = 4
GL_REPEAT = 10497
GL_CLAMP_TO_EDGE = 33071
GL_MIRRORED_REPEAT = 33648
GL_LINEAR = 9729

STATIC_GLB_MANIFEST_FORMAT = "oot3d_static_base_glb_export_v1"
DEFAULT_POSITION_SCALE = 0.0001
DEFAULT_GLB_TEXTURE_ORIENTATION: str | None = None
DEFAULT_SKINNED_GLB_TEXTURE_ORIENTATION = TEXTURE_ORIENTATION_FLIP_Y
DRAW_PROFILE_SELECTED_PRIMITIVES = "selected_primitives"


@dataclass(frozen=True)
class GlbOptions:
    output: Path
    manifest_output: Path | None = None
    selection_manifest: Path | None = None
    cmb_index: int = 0
    cmb_name: str | None = None
    draw_profile: str = "all_meshes"
    texture_orientation: str | None = DEFAULT_GLB_TEXTURE_ORIENTATION
    uv_orientation: str = TEXTURE_ORIENTATION_NORMAL
    position_scale: float = DEFAULT_POSITION_SCALE
    sample_limit: int = 100


@dataclass(frozen=True)
class PrimitiveGeometry:
    positions: tuple[Vec3, ...]
    normals: tuple[Vec3, ...]
    texcoords: tuple[Vec2, ...]
    colors: tuple[Color, ...]
    indices: tuple[int, ...]
    position_source: str
    normal_source: str
    bone_index: int | None = None


@dataclass(frozen=True)
class ExportPrimitive:
    mesh_index: int
    shape_index: int
    material_index: int
    visibility_id: int
    primitive_index: int
    skinning_mode: int
    texture_index: int | None
    texture_name: str | None
    name: str
    geometry: PrimitiveGeometry


class GlbBuilder:
    def __init__(self) -> None:
        self.bin = bytearray()
        self.json: dict[str, object] = {
            "asset": {
                "version": "2.0",
                "generator": "oot3d-asset-tool static_glb_export",
            },
            "buffers": [],
            "bufferViews": [],
            "accessors": [],
            "meshes": [],
            "nodes": [],
            "scenes": [{"nodes": []}],
            "scene": 0,
            "materials": [],
            "images": [],
            "textures": [],
            "samplers": [],
        }

    def align(self, alignment: int = 4) -> None:
        padding = (-len(self.bin)) % alignment
        if padding:
            self.bin.extend(b"\x00" * padding)

    def add_buffer_view(self, data: bytes, *, target: int | None = None) -> int:
        self.align()
        offset = len(self.bin)
        self.bin.extend(data)
        view: dict[str, object] = {
            "buffer": 0,
            "byteOffset": offset,
            "byteLength": len(data),
        }
        if target is not None:
            view["target"] = target
        views = self.json["bufferViews"]
        assert isinstance(views, list)
        views.append(view)
        return len(views) - 1

    def add_accessor(
        self,
        data: bytes,
        *,
        component_type: int,
        count: int,
        accessor_type: str,
        target: int | None,
        minimum: list[float] | None = None,
        maximum: list[float] | None = None,
    ) -> int:
        view_index = self.add_buffer_view(data, target=target)
        accessor: dict[str, object] = {
            "bufferView": view_index,
            "byteOffset": 0,
            "componentType": component_type,
            "count": count,
            "type": accessor_type,
        }
        if minimum is not None:
            accessor["min"] = minimum
        if maximum is not None:
            accessor["max"] = maximum
        accessors = self.json["accessors"]
        assert isinstance(accessors, list)
        accessors.append(accessor)
        return len(accessors) - 1

    def write_glb(self, path: Path) -> None:
        self.align()
        buffers = self.json["buffers"]
        assert isinstance(buffers, list)
        buffers[:] = [{"byteLength": len(self.bin)}]

        json_bytes = json.dumps(self.json, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
        json_padding = (-len(json_bytes)) % 4
        json_chunk = json_bytes + (b" " * json_padding)
        bin_padding = (-len(self.bin)) % 4
        bin_chunk = bytes(self.bin) + (b"\x00" * bin_padding)
        total_length = 12 + 8 + len(json_chunk) + 8 + len(bin_chunk)

        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("wb") as f:
            f.write(struct.pack("<III", 0x46546C67, 2, total_length))
            f.write(struct.pack("<II", len(json_chunk), 0x4E4F534A))
            f.write(json_chunk)
            f.write(struct.pack("<II", len(bin_chunk), 0x004E4942))
            f.write(bin_chunk)


def export_static_base_glb(path: Path, options: GlbOptions) -> dict[str, object]:
    if not math.isfinite(options.position_scale) or options.position_scale <= 0.0:
        raise ParseError(f"position scale must be positive and finite, got {options.position_scale!r}")

    data, source, embedded_name = load_cmb_payload(path, options.cmb_index, options.cmb_name)
    model = CmbModel.parse(data, source)
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
    if not math.isfinite(position_scale) or position_scale <= 0.0:
        raise ParseError(f"position scale must be positive and finite, got {position_scale!r}")

    bind_pose = export_cmb_skinned_bind_pose(
        data,
        source=source,
        embedded_name=embedded_name,
        sample_limit=options.sample_limit,
    )
    if bind_pose.get("format") != "oot3d_skinned_bind_pose_export_v1":
        raise ParseError(f"{source}: unexpected skinned bind-pose export format")

    shipwright_options = LegacyFastResourceOptions(
        output_dir=options.output.parent,
        resource_root="glb/static_base",
        symbol=options.output.stem,
        include_textures=True,
        allow_nonstatic=True,
        texture_orientation=glb_texture_orientation_for_model(model, texture_orientation),
        uv_orientation=uv_orientation,
    )
    texture_profile = mesh_texture_export_profile(model, shipwright_options)
    draw_profile = normalize_draw_profile(options.draw_profile)
    builder = GlbBuilder()
    issues: list[dict[str, object]] = []
    counts: Counter[str] = Counter()

    image_indices = add_images(builder, model, texture_profile.texture_orientation)
    material_indices = add_materials(builder, model, texture_profile, image_indices)
    bind_pose_mesh_by_index = {
        int(mesh.get("mesh_index", -1)): mesh
        for mesh in bind_pose_meshes(bind_pose)
        if isinstance(mesh.get("mesh_index"), int)
    }
    bone_transforms = skeleton_world_transforms(model.skeleton)
    if draw_profile == DRAW_PROFILE_SELECTED_PRIMITIVES:
        if selection is None:
            raise ParseError(f"draw profile {draw_profile!r} requires --selection-manifest")
        selection_primitive_keys = selection.primitive_keys
    else:
        selection_primitive_keys = frozenset()
    selection_mesh_indices = {mesh_index for mesh_index, _ in selection_primitive_keys}
    selection_visibility_ids = {
        mesh.visibility_id for mesh in model.meshes if mesh.index in selection_mesh_indices
    }

    exported_primitives: list[ExportPrimitive] = []
    for mesh in model.meshes:
        if not draw_profile_includes_mesh(
            draw_profile,
            mesh.index,
            mesh.visibility_id,
            selection_mesh_indices,
        ):
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
            if not primitive.indices:
                counts["empty_primitive"] += 1
                continue
            if not draw_profile_includes_primitive(
                draw_profile,
                mesh.index,
                primitive_index,
                primitive.skinning_mode,
                selection_primitive_keys,
            ):
                counts["filtered_primitive"] += 1
                continue

            geometry: PrimitiveGeometry | None
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
                geometry = skinned_primitive_geometry(
                    primitive_json,
                    material=material,
                    texture=texture,
                    texture_profile=texture_profile,
                    position_scale=position_scale,
                )
                counts["skinned_primitive"] += 1
            elif primitive.skinning_mode == 0:
                geometry = rigid_primitive_geometry(
                    primitive,
                    shape,
                    material=material,
                    texture=texture,
                    texture_profile=texture_profile,
                    bone_transforms=bone_transforms,
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

            primitive_name = primitive_export_name(
                model,
                mesh_index=mesh.index,
                shape_index=mesh.shape_index,
                material_index=mesh.material_index,
                visibility_id=mesh.visibility_id,
                primitive_index=primitive_index,
                skinning_mode=primitive.skinning_mode,
                texture=texture,
            )
            export_primitive = ExportPrimitive(
                mesh_index=mesh.index,
                shape_index=mesh.shape_index,
                material_index=mesh.material_index,
                visibility_id=mesh.visibility_id,
                primitive_index=primitive_index,
                skinning_mode=primitive.skinning_mode,
                texture_index=texture.index if texture is not None else None,
                texture_name=texture.name if texture is not None else None,
                name=primitive_name,
                geometry=geometry,
            )
            add_primitive_node(builder, export_primitive, material_indices.get(material.index) if material else None)
            exported_primitives.append(export_primitive)
            counts["primitive"] += 1
            counts["triangle"] += len(geometry.indices) // 3
            counts["vertex"] += len(geometry.positions)

    manifest = {
        "format": STATIC_GLB_MANIFEST_FORMAT,
        "source": source,
        "embedded_name": embedded_name,
        "model_name": model.name,
        "output": str(options.output),
        "draw_profile": draw_profile,
        "selection_source": (
            selection.source_path
            if selection is not None
            else None
        ),
        "selection_profile_id": selection.profile_id if selection is not None else None,
        "selection_model_name": selection.model_name if selection is not None else None,
        "selection_visibility_ids": sorted(selection_visibility_ids),
        "selection_primitive_keys": [
            {"mesh_index": mesh_index, "primitive_index": primitive_index}
            for mesh_index, primitive_index in sorted(selection_primitive_keys)
        ],
        "export_profile": texture_profile.name,
        "position_scale": position_scale,
        "position_source": "source_position for skinned primitives, bone-transformed source shape positions for rigid primitives",
        "normal_source": "source_normal for skinned primitives, bone-transformed source shape normals for rigid primitives",
        "texture_orientation": texture_profile.texture_orientation,
        "uv_orientation": texture_profile.uv_orientation,
        "glb_contract": (
            "This export is a static base-position inspection GLB. Skinned primitives are emitted from CMB "
            "source_position/source_normal, not bind_pose_preview_position; rigid primitives are placed by their "
            "single bone transform when the model profile requires it. Each primitive is a separate named node."
        ),
        "counts": {
            "mesh_count": len({primitive.mesh_index for primitive in exported_primitives}),
            "primitive_count": counts["primitive"],
            "skinned_primitive_count": counts["skinned_primitive"],
            "rigid_primitive_count": counts["rigid_primitive"],
            "triangle_count": counts["triangle"],
            "vertex_count": counts["vertex"],
            "filtered_mesh_count": counts["filtered_mesh"],
            "filtered_primitive_count": counts["filtered_primitive"],
            "empty_primitive_count": counts["empty_primitive"],
            "issue_count": len(issues),
        },
        "bounds": position_bounds(
            position
            for primitive in exported_primitives
            for position in primitive.geometry.positions
        ),
        "textures": cmb_texture_records(model.textures),
        "materials": cmb_material_records(model.materials, model.textures),
        "mesh_records": [primitive_manifest_record(primitive) for primitive in exported_primitives],
        "issues": issues,
    }

    builder.write_glb(options.output)
    manifest_path = options.manifest_output or options.output.with_suffix(".manifest.json")
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8", newline="\n")
    return manifest


def add_images(builder: GlbBuilder, model: CmbModel, texture_orientation: str) -> dict[int, int]:
    image_indices: dict[int, int] = {}
    for texture in model.textures:
        rgba16 = orient_rgba16_image(
            texture.as_rgba16(),
            texture.width,
            texture.height,
            texture_orientation,
        )
        png = encode_png_rgba8(
            texture.width,
            texture.height,
            rgba16_to_rgba8(rgba16),
        )
        buffer_view = builder.add_buffer_view(png)
        images = builder.json["images"]
        assert isinstance(images, list)
        image_indices[texture.index] = len(images)
        images.append(
            {
                "name": texture.name or f"texture_{texture.index:03d}",
                "bufferView": buffer_view,
                "mimeType": "image/png",
            }
        )
    return image_indices


def glb_texture_orientation_for_model(model: CmbModel, override: str | None) -> str | None:
    if override is not None:
        return override
    if any(
        primitive.skinning_mode in (1, 2)
        for shape in model.shapes
        for primitive in shape.primitives
    ):
        return DEFAULT_SKINNED_GLB_TEXTURE_ORIENTATION
    return None


def add_materials(
    builder: GlbBuilder,
    model: CmbModel,
    texture_profile,
    image_indices: dict[int, int],
) -> dict[int, int]:
    material_indices: dict[int, int] = {}
    for material in model.materials:
        texture_index = material_primary_texture_index_for_profile(model, material, texture_profile)
        texture_info: dict[str, object] | None = None
        if texture_index is not None and texture_index in image_indices:
            texture_info = {
                "index": texture_object_index(
                    builder,
                    image_indices[texture_index],
                    material_texture_sampler_index(builder, material, texture_index),
                )
            }

        alpha_factor = max(0.0, min(1.0, material.blend_color_alpha))
        pbr: dict[str, object] = {
            "baseColorFactor": [1.0, 1.0, 1.0, alpha_factor],
            "metallicFactor": 0.0,
            "roughnessFactor": 1.0,
        }
        if texture_info is not None:
            pbr["baseColorTexture"] = texture_info

        material_json: dict[str, object] = {
            "name": material_name(model, material, texture_index),
            "pbrMetallicRoughness": pbr,
            "doubleSided": not material.cull_back,
            "extras": {
                "oot3d_material_index": material.index,
                "oot3d_texture_index": texture_index,
                "oot3d_texture_name": (
                    model.textures[texture_index].name
                    if texture_index is not None and 0 <= texture_index < len(model.textures)
                    else None
                ),
                "texture_indices": list(material.texture_indices),
                "alpha_test": material.alpha_test,
                "alpha_reference": material.alpha_reference,
                "blend_mode": material.blend_mode,
                "depth_write": material.depth_write,
            },
        }
        if material.alpha_test:
            material_json["alphaMode"] = "MASK"
            material_json["alphaCutoff"] = max(0.0, min(1.0, material.alpha_reference / 255.0))
        elif material.blend_mode or not material.depth_write or alpha_factor < 1.0:
            material_json["alphaMode"] = "BLEND"
        else:
            material_json["alphaMode"] = "OPAQUE"

        materials = builder.json["materials"]
        assert isinstance(materials, list)
        material_indices[material.index] = len(materials)
        materials.append(material_json)
    return material_indices


def texture_object_index(builder: GlbBuilder, image_index: int, sampler_index: int) -> int:
    textures = builder.json["textures"]
    assert isinstance(textures, list)
    textures.append({"source": image_index, "sampler": sampler_index})
    return len(textures) - 1


def material_texture_sampler_index(builder: GlbBuilder, material: Material, texture_index: int) -> int:
    mapper = material_texture_mapper(material, texture_index)
    wrap_s = wrap_to_gltf(mapper.wrap_s if mapper is not None else PICA_TEXTURE_WRAP_REPEAT)
    wrap_t = wrap_to_gltf(mapper.wrap_t if mapper is not None else PICA_TEXTURE_WRAP_REPEAT)
    sampler = {
        "magFilter": GL_LINEAR,
        "minFilter": GL_LINEAR,
        "wrapS": wrap_s,
        "wrapT": wrap_t,
    }
    samplers = builder.json["samplers"]
    assert isinstance(samplers, list)
    samplers.append(sampler)
    return len(samplers) - 1


def add_primitive_node(builder: GlbBuilder, primitive: ExportPrimitive, material_index: int | None) -> None:
    geometry = primitive.geometry
    position_accessor = builder.add_accessor(
        pack_vec3s(geometry.positions),
        component_type=GL_FLOAT,
        count=len(geometry.positions),
        accessor_type="VEC3",
        target=GL_ARRAY_BUFFER,
        minimum=vec3_min(geometry.positions),
        maximum=vec3_max(geometry.positions),
    )
    normal_accessor = builder.add_accessor(
        pack_vec3s(geometry.normals),
        component_type=GL_FLOAT,
        count=len(geometry.normals),
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

    meshes = builder.json["meshes"]
    nodes = builder.json["nodes"]
    scenes = builder.json["scenes"]
    assert isinstance(meshes, list)
    assert isinstance(nodes, list)
    assert isinstance(scenes, list)
    mesh_index = len(meshes)
    meshes.append({"name": primitive.name, "primitives": [primitive_json]})
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
                "vertex_count": len(geometry.positions),
            },
        }
    )
    scene = scenes[0]
    assert isinstance(scene, dict)
    scene_nodes = scene["nodes"]
    assert isinstance(scene_nodes, list)
    scene_nodes.append(node_index)


def skinned_primitive_geometry(
    primitive_json: dict[str, object],
    *,
    material: Material | None,
    texture: Texture | None,
    texture_profile,
    position_scale: float,
) -> PrimitiveGeometry:
    vertices_json = primitive_json.get("vertices")
    indices_json = primitive_json.get("indices")
    if not isinstance(vertices_json, list) or not isinstance(indices_json, list):
        raise ParseError("skinned primitive record is missing vertices or indices")

    positions: list[Vec3] = []
    normals: list[Vec3] = []
    profile_uvs: list[Vec2] = []
    colors: list[Color] = []
    for vertex_json in vertices_json:
        if not isinstance(vertex_json, dict):
            raise ParseError("skinned primitive contains a non-object vertex record")
        position = read_vec3(vertex_json.get("source_position"))
        normal = read_vec3(vertex_json.get("source_normal"), default=Vec3(0.0, 0.0, 1.0))
        uv = read_vec2(vertex_json.get("source_uv0"))
        color = read_color(vertex_json.get("source_color_rgba"))
        profile_uvs.append(apply_profile_uv(uv, material, texture, texture_profile))
        positions.append(scale_vec3(position, position_scale))
        normals.append(normal)
        colors.append(color)

    indices = tuple(int(index) for index in indices_json if isinstance(index, int))
    if len(indices) != len(indices_json) or len(indices) % 3 != 0:
        raise ParseError("skinned primitive index array is invalid")
    if any(index < 0 or index >= len(positions) for index in indices):
        raise ParseError("skinned primitive index references an invalid vertex")
    uv_offset = skinned_uv_offset(profile_uvs, indices, material, texture)
    texcoords = tuple(glb_uv(Vec2(uv.x + uv_offset.x, uv.y + uv_offset.y)) for uv in profile_uvs)

    return PrimitiveGeometry(
        positions=tuple(positions),
        normals=tuple(normals),
        texcoords=texcoords,
        colors=tuple(colors),
        indices=indices,
        position_source="source_position",
        normal_source="source_normal",
    )


def rigid_primitive_geometry(
    primitive: Primitive,
    shape: Shape,
    *,
    material: Material | None,
    texture: Texture | None,
    texture_profile,
    bone_transforms,
    mesh_index: int,
    primitive_index: int,
    issues: list[dict[str, object]],
    sample_limit: int,
    position_scale: float,
) -> PrimitiveGeometry | None:
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
    if bone_index < 0 or bone_index >= len(bone_transforms):
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

    transform = bone_transforms[bone_index] if texture_profile.apply_bone_transform else None
    local_index: dict[int, int] = {}
    remapped_indices: list[int] = []
    positions: list[Vec3] = []
    normals: list[Vec3] = []
    texcoords: list[Vec2] = []
    colors: list[Color] = []
    uv_offset = rigid_uv_offset(shape, primitive.indices, material, texture, texture_profile)

    for source_index in primitive.indices:
        mapped = local_index.get(source_index)
        if mapped is None:
            position = shape.positions[source_index]
            normal = shape.normals[source_index] if source_index < len(shape.normals) else Vec3(0.0, 0.0, 1.0)
            if transform is not None:
                position = transform_position(transform, position)
                normal = transform_direction(transform, normal)
            position = scale_vec3(position, position_scale)
            uv = shape.uv0[source_index] if source_index < len(shape.uv0) else Vec2(0.0, 0.0)
            uv = apply_profile_uv(uv, material, texture, texture_profile)
            uv = Vec2(uv.x + uv_offset.x, uv.y + uv_offset.y)
            color = shape.colors[source_index] if source_index < len(shape.colors) else Color(255, 255, 255, 255)

            mapped = len(positions)
            local_index[source_index] = mapped
            positions.append(position)
            normals.append(normal)
            texcoords.append(glb_uv(uv))
            colors.append(color)
        remapped_indices.append(mapped)

    return PrimitiveGeometry(
        positions=tuple(positions),
        normals=tuple(normals),
        texcoords=tuple(texcoords),
        colors=tuple(colors),
        indices=tuple(remapped_indices),
        position_source="source_shape_position_bone_transformed" if transform is not None else "source_shape_position",
        normal_source="source_shape_normal_bone_transformed" if transform is not None else "source_shape_normal",
        bone_index=bone_index,
    )


def scale_vec3(value: Vec3, scale: float) -> Vec3:
    return Vec3(value.x * scale, value.y * scale, value.z * scale)


def apply_profile_uv(
    uv: Vec2,
    material: Material | None,
    texture: Texture | None,
    texture_profile,
) -> Vec2:
    uv = transform_uv(material, uv, texture.index if texture is not None else None)
    orientation = normalize_uv_orientation(texture_profile.uv_orientation)
    if orientation == TEXTURE_ORIENTATION_NORMAL:
        return uv
    flip_x = orientation in (TEXTURE_ORIENTATION_FLIP_X, TEXTURE_ORIENTATION_FLIP_XY)
    flip_y = orientation in (TEXTURE_ORIENTATION_FLIP_Y, TEXTURE_ORIENTATION_FLIP_XY)
    return Vec2((1.0 - uv.x) if flip_x else uv.x, (1.0 - uv.y) if flip_y else uv.y)


def rigid_uv_offset(
    shape: Shape,
    indices: Iterable[int],
    material: Material | None,
    texture: Texture | None,
    texture_profile,
) -> Vec2:
    if material is None or texture is None:
        return Vec2(0.0, 0.0)
    mapper = material_texture_mapper(material, texture.index)
    if mapper is None:
        return Vec2(0.0, 0.0)

    min_s = math.inf
    min_t = math.inf
    for index in indices:
        uv = shape.uv0[index] if index < len(shape.uv0) else Vec2(0.0, 0.0)
        uv = apply_profile_uv(uv, material, texture, texture_profile)
        min_s = min(min_s, uv.x)
        min_t = min(min_t, uv.y)
    if not math.isfinite(min_s) or not math.isfinite(min_t):
        return Vec2(0.0, 0.0)
    return Vec2(
        float(integer_repeat_offset(min_s, mapper.wrap_s)),
        float(integer_repeat_offset(min_t, mapper.wrap_t)),
    )


def skinned_uv_offset(
    profile_uvs: list[Vec2],
    indices: tuple[int, ...],
    material: Material | None,
    texture: Texture | None,
) -> Vec2:
    if material is None or texture is None:
        return Vec2(0.0, 0.0)
    mapper = material_texture_mapper(material, texture.index)
    if mapper is None:
        return Vec2(0.0, 0.0)

    min_s = math.inf
    min_t = math.inf
    for index in indices:
        uv = profile_uvs[index]
        min_s = min(min_s, uv.x)
        min_t = min(min_t, uv.y)
    if not math.isfinite(min_s) or not math.isfinite(min_t):
        return Vec2(0.0, 0.0)
    return Vec2(
        float(integer_repeat_offset(min_s, mapper.wrap_s)),
        float(integer_repeat_offset(min_t, mapper.wrap_t)),
    )


def glb_uv(uv: Vec2) -> Vec2:
    return uv


def bind_pose_meshes(bind_pose: dict[str, object]) -> list[dict[str, object]]:
    meshes = bind_pose.get("meshes")
    if not isinstance(meshes, list):
        return []
    return [mesh for mesh in meshes if isinstance(mesh, dict)]


def skinned_primitive_records(bind_pose_mesh: dict[str, object] | None) -> dict[int, dict[str, object]]:
    if bind_pose_mesh is None:
        return {}
    primitives = bind_pose_mesh.get("primitives")
    if not isinstance(primitives, list):
        return {}
    records: dict[int, dict[str, object]] = {}
    for primitive in primitives:
        if isinstance(primitive, dict) and isinstance(primitive.get("primitive_index"), int):
            records[int(primitive["primitive_index"])] = primitive
    return records


def primitive_export_name(
    model: CmbModel,
    *,
    mesh_index: int,
    shape_index: int,
    material_index: int,
    visibility_id: int,
    primitive_index: int,
    skinning_mode: int,
    texture: Texture | None,
) -> str:
    texture_name = safe_name(texture.name) if texture is not None and texture.name else "no_tex"
    model_name = safe_name(model.name or "cmb")
    return (
        f"{model_name}__mesh_{mesh_index:03d}__shape_{shape_index:03d}"
        f"__mat_{material_index:03d}__vis_{visibility_id:03d}"
        f"__prim_{primitive_index:03d}__mode_{skinning_mode}__tex_{texture_name}"
    )


def material_name(model: CmbModel, material: Material, texture_index: int | None) -> str:
    texture_name = (
        model.textures[texture_index].name
        if texture_index is not None and 0 <= texture_index < len(model.textures)
        else "no_tex"
    )
    return f"mat_{material.index:03d}__tex_{safe_name(texture_name)}"


def safe_name(value: str) -> str:
    return "".join(char if char.isalnum() or char in "._-" else "_" for char in value)


def normalize_draw_profile(value: str) -> str:
    profile = value.strip().lower().replace("-", "_")
    if profile in {
        "all",
        "all_meshes",
        "base_character",
        "character_base",
        "clean_character",
        "selected",
        "selection",
        "selected_primitives",
        "skinned",
        "skinned_modes_only",
        "skinned_primitives_only",
        "technical_skinned",
        "rigid",
        "rigid_primitives_only",
    }:
        return {
            "all": "all_meshes",
            "base_character": DRAW_PROFILE_SELECTED_PRIMITIVES,
            "character_base": DRAW_PROFILE_SELECTED_PRIMITIVES,
            "clean_character": DRAW_PROFILE_SELECTED_PRIMITIVES,
            "selected": DRAW_PROFILE_SELECTED_PRIMITIVES,
            "selection": DRAW_PROFILE_SELECTED_PRIMITIVES,
            "skinned": "skinned_primitives_only",
            "technical_skinned": "skinned_modes_only",
            "rigid": "rigid_primitives_only",
        }.get(profile, profile)
    if profile.startswith("visibility:"):
        _, _, raw_id = profile.partition(":")
        try:
            int(raw_id, 0)
        except ValueError as exc:
            raise ParseError(f"invalid draw profile {value!r}: visibility id is not an integer") from exc
        return profile
    raise ParseError(
        f"unsupported draw profile {value!r}; expected all_meshes, skinned_primitives_only, "
        "skinned_modes_only, rigid_primitives_only, selected_primitives, or visibility:<id>"
    )


def draw_profile_includes_mesh(
    draw_profile: str,
    mesh_index: int,
    visibility_id: int,
    selection_mesh_indices: set[int],
) -> bool:
    if draw_profile == DRAW_PROFILE_SELECTED_PRIMITIVES:
        return mesh_index in selection_mesh_indices
    if not draw_profile.startswith("visibility:"):
        return True
    _, _, raw_id = draw_profile.partition(":")
    return visibility_id == int(raw_id, 0)


def draw_profile_includes_primitive(
    draw_profile: str,
    mesh_index: int,
    primitive_index: int,
    skinning_mode: int,
    selection_primitive_keys: frozenset[tuple[int, int]],
) -> bool:
    if draw_profile == DRAW_PROFILE_SELECTED_PRIMITIVES:
        return (mesh_index, primitive_index) in selection_primitive_keys
    if draw_profile == "skinned_primitives_only":
        return skinning_mode in (1, 2)
    if draw_profile == "skinned_modes_only":
        return skinning_mode in (1, 2)
    if draw_profile == "rigid_primitives_only":
        return skinning_mode == 0
    return True


def primitive_manifest_record(primitive: ExportPrimitive) -> dict[str, object]:
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
        "vertex_count": len(primitive.geometry.positions),
        "triangle_count": len(primitive.geometry.indices) // 3,
        "bounds": position_bounds(primitive.geometry.positions),
    }


def cmb_texture_records(textures: tuple[Texture, ...]) -> list[dict[str, object]]:
    return [
        {
            "index": texture.index,
            "name": texture.name,
            "width": texture.width,
            "height": texture.height,
            "format": f"0x{texture.texture_format:x}",
            "data_type": f"0x{texture.data_type:x}",
            "data_size": len(texture.data),
        }
        for texture in textures
    ]


def cmb_material_records(
    materials: tuple[Material, ...],
    textures: tuple[Texture, ...],
) -> list[dict[str, object]]:
    return [
        {
            "index": material.index,
            "texture_indices": list(material.texture_indices),
            "texture_names": material_texture_names(material, textures),
            "texture_mappers_used": material.texture_mappers_used,
            "texture_coords_used": material.texture_coords_used,
            "texture_mappers": [
                {
                    "index": mapper.index,
                    "min_filter": f"0x{mapper.min_filter:x}",
                    "mag_filter": f"0x{mapper.mag_filter:x}",
                    "wrap_s": f"0x{mapper.wrap_s:x}",
                    "wrap_t": f"0x{mapper.wrap_t:x}",
                }
                for mapper in material.texture_mappers
            ],
            "texture_coords": [
                {
                    "coordinate_index": coord.coordinate_index,
                    "scale": [coord.scale.x, coord.scale.y],
                    "rotation": coord.rotation,
                    "translation": [coord.translation.x, coord.translation.y],
                }
                for coord in material.texture_coords
            ],
            "cull_back": material.cull_back,
            "alpha_test": material.alpha_test,
            "alpha_reference": material.alpha_reference,
            "depth_write": material.depth_write,
            "blend_mode": material.blend_mode,
            "blend_color_alpha": material.blend_color_alpha,
        }
        for material in materials
    ]


def material_texture_names(
    material: Material,
    textures: tuple[Texture, ...],
) -> list[str | None]:
    names: list[str | None] = []
    for texture_index in material.texture_indices:
        if 0 <= texture_index < len(textures):
            names.append(textures[texture_index].name)
        else:
            names.append(None)
    return names


def add_issue(
    issues: list[dict[str, object]],
    sample_limit: int,
    mesh_index: int,
    primitive_index: int | None,
    reason: str,
) -> None:
    if len(issues) >= sample_limit:
        return
    issues.append(
        {
            "mesh_index": mesh_index,
            "primitive_index": primitive_index,
            "reason": reason,
        }
    )


def read_vec3(value: object, *, default: Vec3 | None = None) -> Vec3:
    if isinstance(value, list) and len(value) == 3 and all(isinstance(item, (int, float)) for item in value):
        return Vec3(float(value[0]), float(value[1]), float(value[2]))
    if default is not None:
        return default
    raise ParseError("expected finite Vec3 array")


def read_vec2(value: object) -> Vec2:
    if isinstance(value, list) and len(value) == 2 and all(isinstance(item, (int, float)) for item in value):
        return Vec2(float(value[0]), float(value[1]))
    return Vec2(0.0, 0.0)


def read_color(value: object) -> Color:
    if isinstance(value, list) and len(value) == 4 and all(isinstance(item, int) for item in value):
        return Color(
            max(0, min(255, int(value[0]))),
            max(0, min(255, int(value[1]))),
            max(0, min(255, int(value[2]))),
            max(0, min(255, int(value[3]))),
        )
    return Color(255, 255, 255, 255)


def rgba16_to_rgba8(rgba16: bytes) -> bytes:
    if len(rgba16) % 2 != 0:
        raise ParseError("RGBA16 data has an odd byte count")
    output = bytearray((len(rgba16) // 2) * 4)
    for pixel_index in range(len(rgba16) // 2):
        value = (rgba16[pixel_index * 2] << 8) | rgba16[pixel_index * 2 + 1]
        r5 = (value >> 11) & 0x1F
        g5 = (value >> 6) & 0x1F
        b5 = (value >> 1) & 0x1F
        a1 = value & 0x01
        destination = pixel_index * 4
        output[destination] = (r5 << 3) | (r5 >> 2)
        output[destination + 1] = (g5 << 3) | (g5 >> 2)
        output[destination + 2] = (b5 << 3) | (b5 >> 2)
        output[destination + 3] = 255 if a1 else 0
    return bytes(output)


def encode_png_rgba8(width: int, height: int, rgba8: bytes) -> bytes:
    expected = width * height * 4
    if len(rgba8) != expected:
        raise ParseError(f"RGBA8 PNG source size mismatch: expected {expected}, got {len(rgba8)}")
    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)
        start = y * stride
        raw.extend(rgba8[start : start + stride])
    return (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + png_chunk(b"IDAT", zlib.compress(bytes(raw), level=9))
        + png_chunk(b"IEND", b"")
    )


def png_chunk(chunk_type: bytes, data: bytes) -> bytes:
    crc = binascii.crc32(chunk_type)
    crc = binascii.crc32(data, crc) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + chunk_type + data + struct.pack(">I", crc)


def pack_vec3s(values: tuple[Vec3, ...]) -> bytes:
    return b"".join(struct.pack("<fff", value.x, value.y, value.z) for value in values)


def pack_vec2s(values: tuple[Vec2, ...]) -> bytes:
    return b"".join(struct.pack("<ff", value.x, value.y) for value in values)


def pack_colors(values: tuple[Color, ...]) -> bytes:
    return b"".join(
        struct.pack("<ffff", color.r / 255.0, color.g / 255.0, color.b / 255.0, color.a / 255.0)
        for color in values
    )


def pack_indices(values: tuple[int, ...]) -> bytes:
    return b"".join(struct.pack("<I", value) for value in values)


def vec3_min(values: tuple[Vec3, ...]) -> list[float]:
    if not values:
        return [0.0, 0.0, 0.0]
    return [
        min(value.x for value in values),
        min(value.y for value in values),
        min(value.z for value in values),
    ]


def vec3_max(values: tuple[Vec3, ...]) -> list[float]:
    if not values:
        return [0.0, 0.0, 0.0]
    return [
        max(value.x for value in values),
        max(value.y for value in values),
        max(value.z for value in values),
    ]


def position_bounds(values: Iterable[Vec3]) -> dict[str, object]:
    collected = list(values)
    if not collected:
        return {"valid": False, "min": None, "max": None}
    return {
        "valid": True,
        "min": vec3_min(tuple(collected)),
        "max": vec3_max(tuple(collected)),
    }


def wrap_to_gltf(wrap: int) -> int:
    if wrap == PICA_TEXTURE_WRAP_MIRRORED_REPEAT:
        return GL_MIRRORED_REPEAT
    if wrap in {
        PICA_TEXTURE_WRAP_CLAMP,
        PICA_TEXTURE_WRAP_CLAMP_TO_BORDER,
        PICA_TEXTURE_WRAP_CLAMP_TO_EDGE,
    }:
        return GL_CLAMP_TO_EDGE
    return GL_REPEAT
