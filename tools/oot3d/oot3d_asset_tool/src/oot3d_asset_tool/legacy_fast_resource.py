from __future__ import annotations

import json
import math
import struct
from dataclasses import dataclass
from pathlib import Path
from xml.sax.saxutils import escape

from .binary import ParseError
from .cmb import (
    CmbModel,
    Color,
    Material,
    MaterialTexture,
    Mesh,
    PICA_TEXTURE_WRAP_CLAMP,
    PICA_TEXTURE_WRAP_CLAMP_TO_BORDER,
    PICA_TEXTURE_WRAP_CLAMP_TO_EDGE,
    PICA_TEXTURE_WRAP_MIRRORED_REPEAT,
    PICA_TEXTURE_WRAP_REPEAT,
    Shape,
    Skeleton,
    SkeletonBone,
    Texture,
    TextureCoord,
    Vec2,
    Vec3,
    texture_mask,
)
from .texture_stage import raw_texture_stage_selector

FAST_RESOURCE_TEXTURE = 0x4F544558
FAST_TEXTURE_RGBA16 = 2
MAX_F3D_VERTICES = 32
MAX_F3D_TEXTURE_STAGES = 2
G_TX_RENDERTILE = 0
G_TX_LOADTILE = 7
G_TEXTURE_IMAGE_FRAC = 2
FLOAT_EPSILON = 0.000001
TEXTURE_ORIENTATION_NORMAL = "normal"
TEXTURE_ORIENTATION_FLIP_X = "flip_x"
TEXTURE_ORIENTATION_FLIP_Y = "flip_y"
TEXTURE_ORIENTATION_FLIP_XY = "flip_xy"
TEXTURE_ORIENTATIONS = frozenset(
    {
        TEXTURE_ORIENTATION_NORMAL,
        TEXTURE_ORIENTATION_FLIP_X,
        TEXTURE_ORIENTATION_FLIP_Y,
        TEXTURE_ORIENTATION_FLIP_XY,
    }
)
PRIMARY_TEXTURE_MATERIAL_SLOT = "material_slot"
PRIMARY_TEXTURE_STAGE_RESOLVED = "stage_resolved"
SECONDARY_TEXTURE_MATERIAL_SLOT = "material_slot"
SECONDARY_TEXTURE_STAGE_RESOLVED = "stage_resolved"
TILE_MODE_EXPLICIT = "explicit"
TILE_MODE_COMPAT_EMPTY = "compat_empty"


@dataclass(frozen=True)
class ConvertedResource:
    kind: str
    path: str
    file: str


@dataclass(frozen=True)
class ConversionResult:
    manifest_path: Path
    resources: tuple[ConvertedResource, ...]


@dataclass(frozen=True)
class LegacyFastResourceOptions:
    output_dir: Path
    resource_root: str
    symbol: str
    include_textures: bool = True
    allow_nonstatic: bool = False
    texture_orientation: str | None = None
    uv_orientation: str = TEXTURE_ORIENTATION_NORMAL


@dataclass(frozen=True)
class MeshTextureExportProfile:
    """Resolved mesh/texture policy for one CMB export."""

    name: str
    texture_orientation: str
    baked_texture_orientation: str
    uv_orientation: str
    primary_texture_policy: str
    secondary_texture_policy: str
    baked_secondary_texture_policy: str
    bind_baked_secondary_to_material: bool
    tile_mode_policy: str
    apply_bone_transform: bool
    static_default_normal: bool


@dataclass(frozen=True)
class LegacyFastResourceVertex:
    x: int
    y: int
    z: int
    s: int
    t: int
    r: int
    g: int
    b: int
    a: int


@dataclass(frozen=True)
class TextureLayout:
    width: int
    height: int


Matrix4 = tuple[
    tuple[float, float, float, float],
    tuple[float, float, float, float],
    tuple[float, float, float, float],
    tuple[float, float, float, float],
]


def mesh_texture_export_profile(model: CmbModel, options: LegacyFastResourceOptions) -> MeshTextureExportProfile:
    return mesh_texture_export_profile_from_values(
        model,
        texture_orientation=options.texture_orientation,
        uv_orientation=options.uv_orientation,
    )


def mesh_texture_export_profile_from_values(
    model: CmbModel,
    *,
    texture_orientation: str | None = None,
    uv_orientation: str = TEXTURE_ORIENTATION_NORMAL,
) -> MeshTextureExportProfile:
    scene_static = is_scene_static_model(model)
    texture_orientation = normalize_texture_orientation(
        texture_orientation or default_texture_orientation_for_model(model)
    )
    uv_orientation = normalize_uv_orientation(uv_orientation)
    if scene_static:
        return MeshTextureExportProfile(
            name="scene_static",
            texture_orientation=texture_orientation,
            baked_texture_orientation=TEXTURE_ORIENTATION_FLIP_XY,
            uv_orientation=uv_orientation,
            primary_texture_policy=PRIMARY_TEXTURE_MATERIAL_SLOT,
            secondary_texture_policy=SECONDARY_TEXTURE_MATERIAL_SLOT,
            baked_secondary_texture_policy=SECONDARY_TEXTURE_STAGE_RESOLVED,
            bind_baked_secondary_to_material=False,
            tile_mode_policy=TILE_MODE_COMPAT_EMPTY,
            apply_bone_transform=False,
            static_default_normal=True,
        )
    if is_skinned_model(model):
        return MeshTextureExportProfile(
            name="skinned_character",
            texture_orientation=texture_orientation,
            baked_texture_orientation=TEXTURE_ORIENTATION_FLIP_XY,
            uv_orientation=uv_orientation,
            primary_texture_policy=PRIMARY_TEXTURE_MATERIAL_SLOT,
            secondary_texture_policy=SECONDARY_TEXTURE_MATERIAL_SLOT,
            baked_secondary_texture_policy=SECONDARY_TEXTURE_MATERIAL_SLOT,
            bind_baked_secondary_to_material=True,
            tile_mode_policy=TILE_MODE_EXPLICIT,
            apply_bone_transform=True,
            static_default_normal=False,
        )
    return MeshTextureExportProfile(
        name="rigid_stage_resolved",
        texture_orientation=texture_orientation,
        baked_texture_orientation=TEXTURE_ORIENTATION_FLIP_XY,
        uv_orientation=uv_orientation,
        primary_texture_policy=PRIMARY_TEXTURE_STAGE_RESOLVED,
        secondary_texture_policy=SECONDARY_TEXTURE_STAGE_RESOLVED,
        baked_secondary_texture_policy=SECONDARY_TEXTURE_STAGE_RESOLVED,
        bind_baked_secondary_to_material=True,
        tile_mode_policy=TILE_MODE_EXPLICIT,
        apply_bone_transform=True,
        static_default_normal=False,
    )


def is_skinned_model(model: CmbModel) -> bool:
    return any(
        primitive.skinning_mode in (1, 2)
        for shape in model.shapes
        for primitive in shape.primitives
    )


def default_texture_orientation_for_model(model: CmbModel) -> str:
    return TEXTURE_ORIENTATION_FLIP_Y if is_scene_static_model(model) else TEXTURE_ORIENTATION_FLIP_XY


def is_scene_static_model(model: CmbModel) -> bool:
    return model_source_is_scene(model) and model.is_static_candidate()


def export_static_model(model: CmbModel, options: LegacyFastResourceOptions) -> ConversionResult:
    if not options.allow_nonstatic and not model.is_rigid_export_candidate():
        raise ParseError(
            f"{model.source}: not a rigid export candidate "
            f"(bone_count={model.bone_count}; static export supports rigid mode-0 CMBs)"
        )
    profile = mesh_texture_export_profile(model, options)

    output_dir = options.output_dir
    resource_root = normalize_resource_root(options.resource_root)
    output_dir.mkdir(parents=True, exist_ok=True)

    resources: list[ConvertedResource] = []

    texture_paths = export_textures(model, options, profile, resources) if options.include_textures else {}
    material_paths = export_materials(model, options, profile, texture_paths, resources)

    top_path = resource_root + "/" + options.symbol
    top_file = output_dir / options.symbol
    top_calls: list[str] = []

    for mesh in model.meshes:
        if mesh.shape_index >= len(model.shapes):
            raise ParseError(f"mesh {mesh.index} references missing shape {mesh.shape_index}")
        shape = model.shapes[mesh.shape_index]
        material = model.materials[mesh.material_index] if mesh.material_index < len(model.materials) else None
        material_path = material_paths.get(material.index) if material else None
        texture = material_texture(model, material, profile=profile)
        mesh_resources = export_mesh(model, mesh, shape, options, profile, material, material_path, texture, resources)
        top_calls.extend(mesh_resources)

    top_xml = ["<DisplayList Version=\"0\">"]
    for call_path in top_calls:
        top_xml.append(f"\t<CallDisplayList Path=\"{escape(call_path)}\"/>")
    top_xml.append("\t<EndDisplayList/>")
    top_xml.append("</DisplayList>")
    write_text(top_file, "\n".join(top_xml) + "\n")
    resources.append(ConvertedResource("DisplayList", top_path, str(top_file)))

    manifest = {
        "source": model.source,
        "source_format": "cmb",
        "model_name": model.name,
        "resource_root": resource_root,
        "symbol": options.symbol,
        "export_profile": profile.name,
        "texture_orientation": profile.texture_orientation,
        "baked_texture_orientation": profile.baked_texture_orientation,
        "uv_orientation": profile.uv_orientation,
        "limits": {
            "geometry": "rigid CMB mesh conversion",
            "materials": "basic F3D textured/lit approximation",
            "max_vertices_per_load": MAX_F3D_VERTICES,
        },
        "counts": model.summary(),
        "resources": [resource.__dict__ for resource in resources],
    }
    manifest_path = output_dir / "manifest.json"
    write_text(manifest_path, json.dumps(manifest, indent=2) + "\n")
    return ConversionResult(manifest_path=manifest_path, resources=tuple(resources))


def export_textures(
    model: CmbModel,
    options: LegacyFastResourceOptions,
    profile: MeshTextureExportProfile,
    resources: list[ConvertedResource],
) -> dict[int, str]:
    texture_paths: dict[int, str] = {}
    resource_root = normalize_resource_root(options.resource_root)
    used_filenames: set[str] = set()

    for texture in model.textures:
        filename = unique_texture_filename(texture, used_filenames)
        used_filenames.add(filename)
        resource_path = f"{resource_root}/{filename}"
        write_texture_resource(
            options.output_dir / filename,
            texture,
            texture_orientation=profile.texture_orientation,
        )
        texture_paths[texture.index] = resource_path
        resources.append(ConvertedResource("Texture", resource_path, str(options.output_dir / filename)))
    return texture_paths


def export_materials(
    model: CmbModel,
    options: LegacyFastResourceOptions,
    profile: MeshTextureExportProfile,
    texture_paths: dict[int, str],
    resources: list[ConvertedResource],
) -> dict[int, str]:
    material_paths: dict[int, str] = {}
    resource_root = normalize_resource_root(options.resource_root)

    for material in model.materials:
        name = f"{options.symbol}_mat_{material.index}"
        resource_path = f"{resource_root}/{name}"
        file_path = options.output_dir / name
        texture_index = material_primary_texture_index_for_profile(model, material, profile)
        texture = model.textures[texture_index] if texture_index is not None and texture_index < len(model.textures) else None
        texture_path = texture_paths.get(texture.index) if texture else None
        secondary_texture = material_secondary_texture_for_profile(model, material, texture_index, profile)
        secondary_texture_path = texture_paths.get(secondary_texture.index) if secondary_texture else None
        secondary_texture_layout = None
        candidate_secondary_texture = baked_secondary_texture_candidate_for_profile(
            model,
            material,
            texture_index,
            secondary_texture,
            profile,
        )
        if options.include_textures and texture is not None and candidate_secondary_texture is not None:
            baked_secondary_texture = export_baked_secondary_texture(
                model,
                material,
                texture_index,
                texture,
                candidate_secondary_texture,
                options,
                profile,
                resources,
            )
            if baked_secondary_texture is not None and profile.bind_baked_secondary_to_material:
                secondary_texture = candidate_secondary_texture
                secondary_texture_path, secondary_texture_layout = baked_secondary_texture
        write_text(
            file_path,
            material_xml(
                material,
                texture,
                texture_path,
                secondary_texture=secondary_texture,
                secondary_texture_path=secondary_texture_path,
                secondary_texture_layout=secondary_texture_layout,
                tile_mode_policy=profile.tile_mode_policy,
            ),
        )
        material_paths[material.index] = resource_path
        resources.append(ConvertedResource("DisplayList", resource_path, str(file_path)))

    return material_paths


def export_baked_secondary_texture(
    model: CmbModel,
    material: Material,
    primary_index: int | None,
    primary_texture: Texture,
    secondary_texture: Texture,
    options: LegacyFastResourceOptions,
    profile: MeshTextureExportProfile,
    resources: list[ConvertedResource],
) -> tuple[str, TextureLayout] | None:
    slot = secondary_material_texture_slot(material, primary_index)
    resource_root = normalize_resource_root(options.resource_root)
    if secondary_texture_coord_export_status(material, primary_index) == "baked_texture":
        filename = transformed_secondary_texture_filename(secondary_texture, material, slot)
        resource_path = f"{resource_root}/{filename}"
        file_path = options.output_dir / filename
        image = bake_secondary_texture_rgba16(
            primary_texture,
            secondary_texture,
            material,
            primary_index,
        )
        write_texture_resource_image(
            file_path,
            primary_texture.width,
            primary_texture.height,
            image,
            texture_orientation=profile.baked_texture_orientation,
        )
        resources.append(ConvertedResource("Texture", resource_path, str(file_path)))
        return resource_path, TextureLayout(primary_texture.width, primary_texture.height)

    extra_texture_index = raw_texture_stage_baked_extra_texture_index(
        model,
        material,
        primary_index,
        secondary_texture.index,
    )
    if extra_texture_index is None:
        return None
    extra_texture = model.textures[extra_texture_index]
    filename = raw_texture_stage_baked_secondary_texture_filename(
        secondary_texture,
        extra_texture,
        material,
    )
    resource_path = f"{resource_root}/{filename}"
    file_path = options.output_dir / filename
    image = bake_raw_texture_stage_extra_rgba16(
        secondary_texture,
        extra_texture,
        material,
        primary_index,
    )
    write_texture_resource_image(
        file_path,
        secondary_texture.width,
        secondary_texture.height,
        image,
        texture_orientation=profile.baked_texture_orientation,
    )
    resources.append(ConvertedResource("Texture", resource_path, str(file_path)))
    return resource_path, TextureLayout(secondary_texture.width, secondary_texture.height)


def export_mesh(
    model: CmbModel,
    mesh: Mesh,
    shape: Shape,
    options: LegacyFastResourceOptions,
    profile: MeshTextureExportProfile,
    material: Material | None,
    material_path: str | None,
    texture: Texture | None,
    resources: list[ConvertedResource],
) -> list[str]:
    resource_root = normalize_resource_root(options.resource_root)
    mesh_name = f"{options.symbol}_mesh_{mesh.index}"
    mesh_path = f"{resource_root}/{mesh_name}"
    mesh_file = options.output_dir / mesh_name
    bone_transforms = skeleton_world_transforms(model.skeleton)

    batch_paths: list[str] = []
    for primitive_index, primitive in enumerate(shape.primitives):
        if primitive.skinning_mode not in (0,):
            raise ParseError(
                f"mesh {mesh.index} primitive {primitive_index} uses skinning mode "
                f"{primitive.skinning_mode}; static export currently supports rigid mode 0"
            )
        if len(primitive.bone_indices) != 1:
            raise ParseError(
                f"mesh {mesh.index} primitive {primitive_index} references "
                f"{len(primitive.bone_indices)} bones; rigid export requires exactly one"
            )
        bone_index = primitive.bone_indices[0]
        if bone_index < 0 or bone_index >= len(bone_transforms):
            raise ParseError(
                f"mesh {mesh.index} primitive {primitive_index} references missing bone {bone_index}"
            )
        bone_transform = bone_transforms[bone_index] if profile.apply_bone_transform else None
        triangles = list(chunk_triangles(primitive.indices))
        uv_overrides = uv_orientation_overrides(
            shape,
            material,
            triangles,
            texture,
            profile.uv_orientation,
        )
        uv_offset = mesh_uv_offset(
            shape,
            material,
            triangles,
            texture,
            uv_overrides=uv_overrides,
        )
        for batch_index, batch in enumerate(
            batch_triangles_for_shape(
                shape,
                texture,
                material,
                triangles,
                uv_offset,
                uv_overrides=uv_overrides,
                static_default_normal=profile.static_default_normal,
                vertex_transform=bone_transform,
            )
        ):
            vtx_name = f"{mesh_name}_prim_{primitive_index}_batch_{batch_index}_vtx"
            tri_name = f"{mesh_name}_prim_{primitive_index}_batch_{batch_index}_tri"
            vtx_path = f"{resource_root}/{vtx_name}"
            tri_path = f"{resource_root}/{tri_name}"
            write_text(options.output_dir / vtx_name, vertex_xml(batch.vertices))
            write_text(options.output_dir / tri_name, triangles_xml(vtx_path, batch.triangles))
            resources.append(ConvertedResource("Vertex", vtx_path, str(options.output_dir / vtx_name)))
            resources.append(ConvertedResource("DisplayList", tri_path, str(options.output_dir / tri_name)))
            batch_paths.append(tri_path)

    mesh_xml = ["<DisplayList Version=\"0\">"]
    mesh_xml.append("\t<ClearGeometryMode G_CULL_BACK=\"1\" G_CULL_FRONT=\"1\" G_CULL_BOTH=\"1\"/>")
    geometry_attrs = [
        'G_ZBUFFER="1"',
        'G_SHADE="1"',
        'G_LIGHTING="1"',
        'G_SHADING_SMOOTH="1"',
    ]
    if material is None or material.cull_back:
        geometry_attrs.insert(2, 'G_CULL_BACK="1"')
    mesh_xml.append(f"\t<SetGeometryMode {' '.join(geometry_attrs)}/>")
    if material_path:
        mesh_xml.append(f"\t<CallDisplayList Path=\"{escape(material_path)}\"/>")
    for batch_path in batch_paths:
        mesh_xml.append(f"\t<CallDisplayList Path=\"{escape(batch_path)}\"/>")
    mesh_xml.append("\t<EndDisplayList/>")
    mesh_xml.append("</DisplayList>")
    write_text(mesh_file, "\n".join(mesh_xml) + "\n")
    resources.append(ConvertedResource("DisplayList", mesh_path, str(mesh_file)))
    return [mesh_path]


@dataclass(frozen=True)
class TriangleBatch:
    vertices: tuple[LegacyFastResourceVertex, ...]
    triangles: tuple[tuple[int, int, int], ...]


def batch_triangles(triangles: list[tuple[int, int, int]]) -> list[TriangleBatchBuilder]:
    batches: list[TriangleBatchBuilder] = []
    current = TriangleBatchBuilder()
    for triangle in triangles:
        if not current.can_add(triangle):
            batches.append(current)
            current = TriangleBatchBuilder()
        current.add(triangle)
    if current.triangles:
        batches.append(current)
    return batches


class TriangleBatchBuilder:
    def __init__(self) -> None:
        self.index_map: dict[int, int] = {}
        self.original_indices: list[int] = []
        self.triangles: list[tuple[int, int, int]] = []

    def can_add(self, triangle: tuple[int, int, int]) -> bool:
        new_count = len(self.index_map)
        for index in triangle:
            if index not in self.index_map:
                new_count += 1
        return new_count <= MAX_F3D_VERTICES

    def add(self, triangle: tuple[int, int, int]) -> None:
        local_triangle: list[int] = []
        for index in triangle:
            if index not in self.index_map:
                self.index_map[index] = len(self.original_indices)
                self.original_indices.append(index)
            local_triangle.append(self.index_map[index])
        self.triangles.append(tuple(local_triangle))  # type: ignore[arg-type]


def batch_triangles_for_shape(
    shape: Shape,
    texture: Texture | None,
    material: Material | None,
    triangles: list[tuple[int, int, int]],
    uv_offset: Vec2 | None = None,
    *,
    uv_overrides: dict[int, Vec2] | None = None,
    static_default_normal: bool = False,
    vertex_transform: Matrix4 | None = None,
) -> list[TriangleBatch]:
    builders = batch_triangles(triangles)
    result: list[TriangleBatch] = []
    for builder in builders:
        vertices = tuple(
            to_legacy_fast_resource_vertex(
                shape,
                texture,
                material,
                original,
                uv_offset=uv_offset,
                uv_override=uv_overrides.get(original) if uv_overrides is not None else None,
                static_default_normal=static_default_normal,
                vertex_transform=vertex_transform,
            )
            for original in builder.original_indices
        )
        result.append(TriangleBatch(vertices=vertices, triangles=tuple(builder.triangles)))
    return result


def chunk_triangles(indices: tuple[int, ...]) -> list[tuple[int, int, int]]:
    return [tuple(indices[index : index + 3]) for index in range(0, len(indices), 3)]  # type: ignore[list-item]


def triangles_xml(vertex_path: str, triangles: tuple[tuple[int, int, int], ...]) -> str:
    lines = ["<DisplayList Version=\"0\">"]
    lines.append(
        f"\t<LoadVertices Path=\"{escape(vertex_path)}\" VertexBufferIndex=\"0\" "
        f"VertexOffset=\"0\" Count=\"{max_vertex_index(triangles) + 1}\"/>"
    )
    index = 0
    while index < len(triangles):
        first = triangles[index]
        if index + 1 < len(triangles):
            second = triangles[index + 1]
            lines.append(
                "\t<Triangles2 "
                f"V00=\"{first[0]}\" V01=\"{first[1]}\" V02=\"{first[2]}\" Flag0=\"0\" "
                f"V10=\"{second[0]}\" V11=\"{second[1]}\" V12=\"{second[2]}\" Flag1=\"0\"/>"
            )
            index += 2
        else:
            lines.append(
                f"\t<Triangle1 V00=\"{first[0]}\" V01=\"{first[1]}\" "
                f"V02=\"{first[2]}\" Flag0=\"0\"/>"
            )
            index += 1
    lines.append("\t<EndDisplayList/>")
    lines.append("</DisplayList>")
    return "\n".join(lines) + "\n"


def vertex_xml(vertices: tuple[LegacyFastResourceVertex, ...]) -> str:
    lines = ["<Vertex Version=\"0\">"]
    for vertex in vertices:
        lines.append(
            "\t<Vtx "
            f"X=\"{vertex.x}\" Y=\"{vertex.y}\" Z=\"{vertex.z}\" "
            f"S=\"{vertex.s}\" T=\"{vertex.t}\" "
            f"R=\"{vertex.r}\" G=\"{vertex.g}\" B=\"{vertex.b}\" A=\"{vertex.a}\"/>"
        )
    lines.append("</Vertex>")
    return "\n".join(lines) + "\n"


def material_xml(
    material: Material,
    texture: Texture | None,
    texture_path: str | None,
    *,
    secondary_texture: Texture | None = None,
    secondary_texture_path: str | None = None,
    secondary_texture_layout: TextureLayout | None = None,
    tile_mode_policy: str = TILE_MODE_EXPLICIT,
) -> str:
    lines = ["<DisplayList Version=\"0\">"]
    lines.append("\t<PipeSync/>")
    alpha_compare = alpha_compare_mode(material)
    prim_alpha = material_prim_alpha(material)
    render_mode, render_mode_2 = material_render_modes(material)
    if texture and texture_path:
        primary_index = texture.index if texture else None
        mapper = material_texture_mapper(material, primary_index)
        secondary_mapper = secondary_material_texture_mapper(material, primary_index)
        has_secondary = (
            secondary_texture is not None
            and secondary_texture_path is not None
            and secondary_mapper is not None
        )
        sampler = load_texture_sampler_attributes(texture, mapper)
        lines.append(textured_combine_lerp(material, use_texel1=has_secondary))
        lines.append(
            "\t<SetOtherMode Cmd=\"G_SETOTHERMODE_H\" Sft=\"4\" Length=\"20\" "
            "G_AD_NOISE=\"1\" G_CD_MAGICSQ=\"1\" G_CK_NONE=\"1\" G_TC_FILT=\"1\" "
            "G_TF_BILERP=\"1\" G_TT_NONE=\"1\" G_TL_TILE=\"1\" G_TD_CLAMP=\"1\" "
            "G_TP_PERSP=\"1\" G_CYC_2CYCLE=\"1\" G_PM_NPRIMITIVE=\"1\"/>"
        )
        lines.append(
            "\t<SetOtherMode Cmd=\"G_SETOTHERMODE_L\" Sft=\"0\" Length=\"32\" "
            f"G_ZS_PIXEL=\"1\" {render_mode}=\"1\" {render_mode_2}=\"1\"/>"
        )
        if alpha_compare:
            lines.append(
                f"\t<SetBlendColor R=\"0\" G=\"0\" B=\"0\" A=\"{material.alpha_reference}\"/>"
            )
        lines.append(f"\t<SetAlphaCompare Mode=\"{alpha_compare}\"/>")
        lines.append("\t<Texture S=\"65535\" T=\"65535\" Level=\"0\" Tile=\"0\" On=\"1\"/>")
        lines.append(
            f"\t<SetPrimColor M=\"0\" L=\"0\" R=\"255\" G=\"255\" B=\"255\" A=\"{prim_alpha}\"/>"
        )
        lines.extend(
            texture_load_tile_xml(
                texture,
                texture_path,
                sampler,
                tile_mode_policy=tile_mode_policy,
            )
        )
        if has_secondary:
            secondary_load_texture = secondary_texture_layout or secondary_texture
            secondary_sampler = load_texture_sampler_attributes(secondary_load_texture, secondary_mapper)
            lines.extend(
                texture_load_tile_xml(
                    secondary_load_texture,
                    secondary_texture_path,
                    secondary_sampler,
                    render_tile=1,
                    tmem=256,
                    tile_mode_policy=tile_mode_policy,
                )
            )
    else:
        lines.append(
            "\t<SetCombineLERP A0=\"G_CCMUX_SHADE\" B0=\"G_CCMUX_0\" C0=\"G_CCMUX_PRIMITIVE\" "
            "D0=\"G_CCMUX_0\" Aa0=\"G_ACMUX_SHADE\" Ab0=\"G_ACMUX_0\" Ac0=\"G_ACMUX_PRIMITIVE\" "
            "Ad0=\"G_ACMUX_0\" A1=\"G_CCMUX_COMBINED\" B1=\"G_CCMUX_0\" "
            "C1=\"G_CCMUX_PRIMITIVE\" D1=\"G_CCMUX_0\" Aa1=\"G_ACMUX_COMBINED\" "
            "Ab1=\"G_ACMUX_0\" Ac1=\"G_ACMUX_PRIMITIVE\" Ad1=\"G_ACMUX_0\"/>"
        )
        lines.append(
            "\t<SetOtherMode Cmd=\"G_SETOTHERMODE_L\" Sft=\"0\" Length=\"32\" "
            f"G_ZS_PIXEL=\"1\" {render_mode}=\"1\" {render_mode_2}=\"1\"/>"
        )
        if alpha_compare:
            lines.append(
                f"\t<SetBlendColor R=\"0\" G=\"0\" B=\"0\" A=\"{material.alpha_reference}\"/>"
            )
        lines.append(f"\t<SetAlphaCompare Mode=\"{alpha_compare}\"/>")
        lines.append(
            f"\t<SetPrimColor M=\"0\" L=\"0\" R=\"255\" G=\"255\" B=\"255\" A=\"{prim_alpha}\"/>"
        )
    lines.append("\t<EndDisplayList/>")
    lines.append("</DisplayList>")
    return "\n".join(lines) + "\n"


def textured_combine_lerp(material: Material, *, use_texel1: bool = False) -> str:
    if use_texel1:
        return (
            "\t<SetCombineLERP A0=\"G_CCMUX_TEXEL0\" B0=\"G_CCMUX_0\" C0=\"G_CCMUX_TEXEL1\" "
            "D0=\"G_CCMUX_0\" Aa0=\"G_ACMUX_TEXEL0\" Ab0=\"G_ACMUX_0\" Ac0=\"G_ACMUX_TEXEL1\" "
            "Ad0=\"G_ACMUX_0\" A1=\"G_CCMUX_COMBINED\" B1=\"G_CCMUX_0\" "
            "C1=\"G_CCMUX_SHADE\" D1=\"G_CCMUX_0\" Aa1=\"G_ACMUX_COMBINED\" "
            "Ab1=\"G_ACMUX_0\" Ac1=\"G_ACMUX_0\" Ad1=\"G_ACMUX_0\"/>"
        )
    if material_is_blended(material):
        return (
            "\t<SetCombineLERP A0=\"G_CCMUX_TEXEL0\" B0=\"G_CCMUX_0\" C0=\"G_CCMUX_SHADE\" "
            "D0=\"G_CCMUX_0\" Aa0=\"G_ACMUX_0\" Ab0=\"G_ACMUX_0\" Ac0=\"G_ACMUX_0\" "
            "Ad0=\"G_ACMUX_TEXEL0\" A1=\"G_CCMUX_COMBINED\" B1=\"G_CCMUX_0\" "
            "C1=\"G_CCMUX_PRIMITIVE\" D1=\"G_CCMUX_0\" Aa1=\"G_ACMUX_COMBINED\" "
            "Ab1=\"G_ACMUX_0\" Ac1=\"G_ACMUX_PRIMITIVE\" Ad1=\"G_ACMUX_0\"/>"
        )
    return (
        "\t<SetCombineLERP A0=\"G_CCMUX_TEXEL0\" B0=\"G_CCMUX_0\" C0=\"G_CCMUX_SHADE\" "
        "D0=\"G_CCMUX_0\" Aa0=\"G_ACMUX_0\" Ab0=\"G_ACMUX_0\" Ac0=\"G_ACMUX_0\" "
        "Ad0=\"G_ACMUX_TEXEL0\" A1=\"G_CCMUX_COMBINED\" B1=\"G_CCMUX_0\" "
        "C1=\"G_CCMUX_PRIMITIVE\" D1=\"G_CCMUX_0\" Aa1=\"G_ACMUX_0\" "
        "Ab1=\"G_ACMUX_0\" Ac1=\"G_ACMUX_0\" Ad1=\"G_ACMUX_COMBINED\"/>"
    )


def alpha_compare_mode(material: Material) -> int:
    return 1 if material.alpha_test else 0


def material_render_modes(material: Material) -> tuple[str, str]:
    if material.alpha_test:
        return "G_RM_AA_ZB_TEX_EDGE", "G_RM_AA_ZB_TEX_EDGE2"
    if material_is_blended(material):
        return "G_RM_AA_ZB_XLU_SURF", "G_RM_AA_ZB_XLU_SURF2"
    return "G_RM_AA_ZB_OPA_SURF", "G_RM_AA_ZB_OPA_SURF2"


def material_is_blended(material: Material) -> bool:
    return not material.alpha_test and (material.blend_mode != 0 or not material.depth_write)


def material_prim_alpha(material: Material) -> int:
    if not material_is_blended(material):
        return 255
    if not math.isfinite(material.blend_color_alpha):
        return 255
    return clamp_int(round(material.blend_color_alpha * 255.0), 0, 255)


def load_texture_sampler_attributes(texture: Texture | TextureLayout, mapper: MaterialTexture | None) -> str:
    wrap_s = mapper.wrap_s if mapper else None
    wrap_t = mapper.wrap_t if mapper else None
    s_attrs, mask_s = texture_axis_sampler_attributes("CMS", wrap_s, texture.width)
    t_attrs, mask_t = texture_axis_sampler_attributes("CMT", wrap_t, texture.height)
    return f"{s_attrs} {t_attrs} MaskS=\"{mask_s}\" MaskT=\"{mask_t}\""


def texture_load_tile_xml(
    texture: Texture | TextureLayout,
    texture_path: str,
    sampler: str,
    *,
    render_tile: int = G_TX_RENDERTILE,
    tmem: int = 0,
    tile_mode_policy: str = TILE_MODE_EXPLICIT,
) -> list[str]:
    mask_s = extract_int_attr(sampler, "MaskS")
    mask_t = extract_int_attr(sampler, "MaskT")
    cms0, cms1 = tile_axis_modes(sampler, "CMS", tile_mode_policy=tile_mode_policy)
    cmt0, cmt1 = tile_axis_modes(sampler, "CMT", tile_mode_policy=tile_mode_policy)
    line = ((texture.width * 2) + 7) >> 3
    lrs = (texture.width - 1) << G_TEXTURE_IMAGE_FRAC
    lrt = (texture.height - 1) << G_TEXTURE_IMAGE_FRAC
    tile_attrs = (
        'Format="G_IM_FMT_RGBA" Size="G_IM_SIZ_16b" '
        f'Palette="0" Cms0="{cms0}" Cms1="{cms1}" Cmt0="{cmt0}" Cmt1="{cmt1}" '
        f'MaskS="{mask_s}" MaskT="{mask_t}" ShiftS="0" ShiftT="0"'
    )
    return [
        f'\t<SetTextureImage Path="{escape(texture_path)}" Format="G_IM_FMT_RGBA" '
        f'Size="G_IM_SIZ_16b" Width="{texture.width}"/>',
        "\t<TileSync/>",
        f'\t<SetTile {tile_attrs} Line="0" TMem="{tmem}" Tile="{G_TX_LOADTILE}"/>',
        "\t<LoadSync/>",
        f'\t<LoadTile T="{G_TX_LOADTILE}" Uls="0" Ult="0" Lrs="{lrs}" Lrt="{lrt}"/>',
        "\t<PipeSync/>",
        f'\t<SetTile {tile_attrs} Line="{line}" TMem="{tmem}" Tile="{render_tile}"/>',
        f'\t<SetTileSize T="{render_tile}" Uls="0" Ult="0" Lrs="{lrs}" Lrt="{lrt}"/>',
    ]


def extract_int_attr(attrs: str, name: str) -> int:
    prefix = f'{name}="'
    start = attrs.find(prefix)
    if start < 0:
        return 0
    start += len(prefix)
    end = attrs.find('"', start)
    if end < 0:
        return 0
    return int(attrs[start:end])


def tile_axis_modes(attrs: str, prefix: str, *, tile_mode_policy: str = TILE_MODE_EXPLICIT) -> tuple[str, str]:
    mirror = f"{prefix}_TXMirror=\"1\"" in attrs
    clamp = f"{prefix}_TXClamp=\"1\"" in attrs
    if tile_mode_policy == TILE_MODE_COMPAT_EMPTY:
        return ("G_TX_MIRROR" if mirror else "", "G_TX_CLAMP" if clamp else "")
    return (
        "G_TX_CLAMP" if clamp else "G_TX_WRAP",
        "G_TX_MIRROR" if mirror else "G_TX_NOMIRROR",
    )


def texture_axis_sampler_attributes(prefix: str, wrap: int | None, size: int) -> tuple[str, int]:
    if wrap == PICA_TEXTURE_WRAP_MIRRORED_REPEAT:
        return f"{prefix}_TXMirror=\"1\" {prefix}_TXWrap=\"1\"", texture_mask(size)
    if wrap in {
        PICA_TEXTURE_WRAP_CLAMP,
        PICA_TEXTURE_WRAP_CLAMP_TO_BORDER,
        PICA_TEXTURE_WRAP_CLAMP_TO_EDGE,
    }:
        return f"{prefix}_TXNoMirror=\"1\" {prefix}_TXClamp=\"1\"", 0
    return f"{prefix}_TXNoMirror=\"1\" {prefix}_TXWrap=\"1\"", texture_mask(size)


def normalize_texture_orientation(value: str) -> str:
    normalized = value.strip().lower().replace("-", "_")
    aliases = {
        "none": TEXTURE_ORIENTATION_NORMAL,
        "normal": TEXTURE_ORIENTATION_NORMAL,
        "flipx": TEXTURE_ORIENTATION_FLIP_X,
        "flip_x": TEXTURE_ORIENTATION_FLIP_X,
        "flipy": TEXTURE_ORIENTATION_FLIP_Y,
        "flip_y": TEXTURE_ORIENTATION_FLIP_Y,
        "flipxy": TEXTURE_ORIENTATION_FLIP_XY,
        "flip_x_y": TEXTURE_ORIENTATION_FLIP_XY,
        "flip_xy": TEXTURE_ORIENTATION_FLIP_XY,
    }
    orientation = aliases.get(normalized, normalized)
    if orientation not in TEXTURE_ORIENTATIONS:
        valid = ", ".join(sorted(TEXTURE_ORIENTATIONS))
        raise ParseError(f"unsupported texture orientation {value!r}; expected one of {valid}")
    return orientation


def normalize_uv_orientation(value: str) -> str:
    try:
        return normalize_texture_orientation(value)
    except ParseError as exc:
        valid = ", ".join(sorted(TEXTURE_ORIENTATIONS))
        raise ParseError(f"unsupported UV orientation {value!r}; expected one of {valid}") from exc


def orient_rgba16_image(image: bytes, width: int, height: int, texture_orientation: str) -> bytes:
    expected_size = width * height * 2
    if len(image) != expected_size:
        raise ParseError(
            f"RGBA16 image size mismatch for {width}x{height}: "
            f"expected {expected_size} bytes, got {len(image)}"
        )

    orientation = normalize_texture_orientation(texture_orientation)
    if orientation == TEXTURE_ORIENTATION_NORMAL:
        return image

    flip_x = orientation in (TEXTURE_ORIENTATION_FLIP_X, TEXTURE_ORIENTATION_FLIP_XY)
    flip_y = orientation in (TEXTURE_ORIENTATION_FLIP_Y, TEXTURE_ORIENTATION_FLIP_XY)
    output = bytearray(expected_size)
    for y in range(height):
        source_y = height - 1 - y if flip_y else y
        for x in range(width):
            source_x = width - 1 - x if flip_x else x
            source = ((source_y * width) + source_x) * 2
            destination = ((y * width) + x) * 2
            output[destination : destination + 2] = image[source : source + 2]
    return bytes(output)


def write_texture_resource(
    path: Path,
    texture: Texture,
    *,
    texture_orientation: str = TEXTURE_ORIENTATION_NORMAL,
) -> None:
    write_texture_resource_image(
        path,
        texture.width,
        texture.height,
        texture.as_rgba16(),
        texture_orientation=texture_orientation,
    )


def write_texture_resource_image(
    path: Path,
    width: int,
    height: int,
    image: bytes,
    *,
    texture_orientation: str = TEXTURE_ORIENTATION_NORMAL,
) -> None:
    image = orient_rgba16_image(image, width, height, texture_orientation)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as f:
        f.write(bytes([0, 0, 0, 0]))
        f.write(struct.pack("<I", FAST_RESOURCE_TEXTURE))
        f.write(struct.pack("<I", 1))
        f.write(struct.pack("<Q", 0xDEADBEEFDEADBEEF))
        f.write(struct.pack("<I", 0))
        f.write(struct.pack("<Q", 0))
        f.write(struct.pack("<I", 0))
        while f.tell() < 0x40:
            f.write(struct.pack("<I", 0))
        f.write(struct.pack("<I", FAST_TEXTURE_RGBA16))
        f.write(struct.pack("<I", width))
        f.write(struct.pack("<I", height))
        f.write(struct.pack("<I", 0))
        f.write(struct.pack("<f", 1.0))
        f.write(struct.pack("<f", 1.0))
        f.write(struct.pack("<I", len(image)))
        f.write(image)


def material_primary_texture_index_for_profile(
    model: CmbModel,
    material: Material,
    profile: MeshTextureExportProfile,
) -> int | None:
    if profile.primary_texture_policy == PRIMARY_TEXTURE_MATERIAL_SLOT:
        return first_valid_texture(material)
    return material_primary_texture_index(model, material)


def material_secondary_texture_for_profile(
    model: CmbModel,
    material: Material,
    primary_index: int | None,
    profile: MeshTextureExportProfile,
) -> Texture | None:
    if profile.secondary_texture_policy == SECONDARY_TEXTURE_MATERIAL_SLOT:
        return material_slot_secondary_texture(model, material, primary_index)
    return secondary_material_texture(model, material, primary_index)


def baked_secondary_texture_candidate_for_profile(
    model: CmbModel,
    material: Material,
    primary_index: int | None,
    material_secondary_texture: Texture | None,
    profile: MeshTextureExportProfile,
) -> Texture | None:
    if profile.baked_secondary_texture_policy == SECONDARY_TEXTURE_MATERIAL_SLOT:
        return material_secondary_texture
    return secondary_material_texture(model, material, primary_index)


def first_valid_texture(material: Material) -> int | None:
    slot = first_valid_texture_slot(material)
    if slot is None:
        return None
    if slot < len(material.texture_mappers):
        return material.texture_mappers[slot].index
    if slot < len(material.texture_indices):
        return material.texture_indices[slot]
    return None


def material_primary_texture_index(model: CmbModel, material: Material) -> int | None:
    parsed_texture_index = first_valid_texture(material)
    raw_pair_primary = raw_texture_stage_primary_pair_index(
        model,
        material,
        parsed_texture_index,
    )
    if raw_pair_primary is not None:
        return raw_pair_primary
    raw_single_primary = raw_texture_stage_single_valid_primary_index(
        model,
        material,
        parsed_texture_index,
    )
    if raw_single_primary is not None:
        return raw_single_primary
    raw_direct_single_primary = raw_texture_stage_single_direct_primary_index(
        model,
        material,
        parsed_texture_index,
    )
    if raw_direct_single_primary is not None:
        return raw_direct_single_primary
    raw_material_ref_single_primary = raw_texture_stage_single_material_ref_primary_index(
        model,
        material,
        parsed_texture_index,
    )
    if raw_material_ref_single_primary is not None:
        return raw_material_ref_single_primary
    raw_scene_direct_single_primary = raw_texture_stage_single_scene_direct_primary_index(
        model,
        material,
        parsed_texture_index,
    )
    if raw_scene_direct_single_primary is not None:
        return raw_scene_direct_single_primary
    raw_prefix_primary = raw_texture_stage_prefix_primary_index(
        model,
        material,
        parsed_texture_index,
    )
    if raw_prefix_primary is not None:
        return raw_prefix_primary
    raw_repeated_primary = raw_texture_stage_repeated_mapper_prefix_primary_index(
        model,
        material,
        parsed_texture_index,
    )
    if raw_repeated_primary is not None:
        return raw_repeated_primary
    raw_active_mapper_primary = raw_texture_stage_active_mapper_pair_primary_index(
        model,
        material,
        parsed_texture_index,
    )
    if raw_active_mapper_primary is not None:
        return raw_active_mapper_primary
    raw_primary_with_exported_secondary = (
        raw_texture_stage_direct_primary_with_exported_secondary_index(
            model,
            material,
            parsed_texture_index,
        )
    )
    if raw_primary_with_exported_secondary is not None:
        return raw_primary_with_exported_secondary
    if (
        parsed_texture_index is not None
        and 0 <= parsed_texture_index < len(model.textures)
    ):
        return parsed_texture_index
    return raw_texture_stage_primary_index(model, material)


def raw_texture_stage_primary_pair_index(
    model: CmbModel,
    material: Material,
    parsed_primary_index: int | None,
) -> int | None:
    pair = single_mapper_raw_texture_stage_pair(material)
    if pair is None or parsed_primary_index is None:
        return None
    if not 0 <= parsed_primary_index < len(model.textures):
        return None
    if parsed_primary_index in pair:
        return None
    primary_index, secondary_index = pair
    if not 0 <= primary_index < len(model.textures):
        return None
    if not 0 <= secondary_index < len(model.textures):
        return None
    if raw_stage_pair_has_ambiguous_scene_material_reference(
        model,
        primary_index,
        secondary_index,
    ):
        return None
    return primary_index


def raw_texture_stage_single_valid_primary_index(
    model: CmbModel,
    material: Material,
    parsed_primary_index: int | None,
) -> int | None:
    pair = single_mapper_raw_texture_stage_pair(material)
    if pair is None or parsed_primary_index is None:
        return None
    if not 0 <= parsed_primary_index < len(model.textures):
        return None
    if parsed_primary_index in pair:
        return None
    primary_index, secondary_index = pair
    if not 0 <= primary_index < len(model.textures):
        return None
    if 0 <= secondary_index < len(model.textures):
        return None
    return primary_index


def raw_texture_stage_single_direct_primary_index(
    model: CmbModel,
    material: Material,
    parsed_primary_index: int | None,
) -> int | None:
    if (
        parsed_primary_index is None
        or not 0 <= parsed_primary_index < len(model.textures)
        or secondary_stage_mapper_slot(material) is not None
        or model_source_is_scene(model)
    ):
        return None
    mapper_count = material.texture_mappers_used or len(material.texture_mappers)
    active_mapper_count = sum(
        1
        for mapper in material.texture_mappers[:mapper_count]
        if mapper.index >= 0
    )
    if active_mapper_count != 1:
        return None

    selector = raw_texture_stage_selector(material)
    if selector["stage_count"] != 1:
        return None
    stage_indices = selector["stage_indices"]
    if not isinstance(stage_indices, list) or len(stage_indices) != 1:
        return None
    raw_primary_index = stage_indices[0]
    if not isinstance(raw_primary_index, int):
        return None
    if raw_primary_index == parsed_primary_index:
        return None
    if not 0 <= raw_primary_index < len(model.textures):
        return None
    if raw_stage_is_material_index_reference(model, raw_primary_index):
        return None
    return raw_primary_index


def has_raw_texture_stage_single_direct_primary_candidate(
    model: CmbModel,
    material: Material,
    primary_index: int | None,
) -> bool:
    return (
        raw_texture_stage_single_direct_primary_index(
            model,
            material,
            first_valid_texture(material),
        )
        == primary_index
    )


def raw_texture_stage_single_material_ref_primary_index(
    model: CmbModel,
    material: Material,
    parsed_primary_index: int | None,
) -> int | None:
    if (
        parsed_primary_index is None
        or not 0 <= parsed_primary_index < len(model.textures)
        or secondary_stage_mapper_slot(material) is not None
        or model_source_is_scene(model)
    ):
        return None
    mapper_count = material.texture_mappers_used or len(material.texture_mappers)
    active_mapper_count = sum(
        1
        for mapper in material.texture_mappers[:mapper_count]
        if mapper.index >= 0
    )
    if active_mapper_count != 1:
        return None

    selector = raw_texture_stage_selector(material)
    if selector["stage_count"] != 1:
        return None
    stage_indices = selector["stage_indices"]
    if not isinstance(stage_indices, list) or len(stage_indices) != 1:
        return None
    raw_primary_index = stage_indices[0]
    if not isinstance(raw_primary_index, int):
        return None
    if raw_primary_index == parsed_primary_index:
        return None
    if not 0 <= raw_primary_index < len(model.textures):
        return None
    if not raw_stage_is_material_primary_reference(model, raw_primary_index):
        return None
    return raw_primary_index


def has_raw_texture_stage_single_material_ref_primary_candidate(
    model: CmbModel,
    material: Material,
    primary_index: int | None,
) -> bool:
    return (
        raw_texture_stage_single_material_ref_primary_index(
            model,
            material,
            first_valid_texture(material),
        )
        == primary_index
    )


def raw_texture_stage_single_scene_direct_primary_index(
    model: CmbModel,
    material: Material,
    parsed_primary_index: int | None,
) -> int | None:
    # Kokiri scene CMBs use these single raw slots like material-table order,
    # not as reliable replacements for already valid texture mappers.
    return None


def has_raw_texture_stage_single_scene_direct_primary_candidate(
    model: CmbModel,
    material: Material,
    primary_index: int | None,
) -> bool:
    return (
        raw_texture_stage_single_scene_direct_primary_index(
            model,
            material,
            first_valid_texture(material),
        )
        == primary_index
    )


def model_source_is_scene(model: CmbModel) -> bool:
    source = model.source.replace("\\", "/")
    return "/scene/" in f"/{source}"


def raw_texture_stage_prefix_primary_index(
    model: CmbModel,
    material: Material,
    parsed_primary_index: int | None,
) -> int | None:
    pair = raw_texture_stage_prefix_pair_from_exported_secondary(
        model,
        material,
        parsed_primary_index,
    )
    return None if pair is None else pair[0]


def raw_texture_stage_prefix_pair_from_exported_secondary(
    model: CmbModel,
    material: Material,
    parsed_primary_index: int | None,
) -> tuple[int, int] | None:
    if (
        parsed_primary_index is None
        or not 0 <= parsed_primary_index < len(model.textures)
    ):
        return None
    parsed_secondary_slot = secondary_valid_texture_slot(material, parsed_primary_index)
    if parsed_secondary_slot is None:
        return None
    parsed_secondary_index = material.texture_mappers[parsed_secondary_slot].index
    if not 0 <= parsed_secondary_index < len(model.textures):
        return None

    selector = raw_texture_stage_selector(material)
    stage_indices = selector["stage_indices"]
    if not isinstance(stage_indices, list) or len(stage_indices) < 2:
        return None
    if model_source_is_scene(model):
        mapper_count = material.texture_mappers_used or len(material.texture_mappers)
        active_mapper_indices = {
            mapper.index
            for mapper in material.texture_mappers[:mapper_count]
            if mapper.index >= 0
        }
        if (
            not model.is_static_candidate()
            or len(active_mapper_indices) != 2
            or selector["stage_count"] != 3
            or len(stage_indices) != 3
            or not all(
                isinstance(index, int) and 0 <= index < len(model.textures)
                for index in stage_indices
            )
        ):
            return None
    if parsed_primary_index in stage_indices:
        return None

    raw_primary_index = stage_indices[0]
    raw_secondary_index = stage_indices[1]
    if not isinstance(raw_primary_index, int) or not isinstance(raw_secondary_index, int):
        return None
    if raw_primary_index != parsed_secondary_index:
        return None
    if raw_primary_index == raw_secondary_index:
        return None
    if not 0 <= raw_secondary_index < len(model.textures):
        return None
    if model_source_is_scene(model) and raw_secondary_index not in active_mapper_indices:
        return None
    return raw_primary_index, raw_secondary_index


def raw_texture_stage_prefix_secondary_index(
    model: CmbModel,
    material: Material,
    primary_index: int | None,
) -> int | None:
    pair = raw_texture_stage_prefix_pair_from_exported_secondary(
        model,
        material,
        first_valid_texture(material),
    )
    if pair is None or primary_index != pair[0]:
        return None
    return pair[1]


def raw_texture_stage_repeated_mapper_prefix_primary_index(
    model: CmbModel,
    material: Material,
    parsed_primary_index: int | None,
) -> int | None:
    pair = raw_texture_stage_repeated_mapper_prefix_pair(
        model,
        material,
        parsed_primary_index,
    )
    return None if pair is None else pair[0]


def raw_texture_stage_repeated_mapper_prefix_secondary_index(
    model: CmbModel,
    material: Material,
    primary_index: int | None,
) -> int | None:
    pair = raw_texture_stage_repeated_mapper_prefix_pair(
        model,
        material,
        first_valid_texture(material),
    )
    if pair is None or primary_index != pair[0]:
        return None
    return pair[1]


def raw_texture_stage_active_mapper_pair_primary_index(
    model: CmbModel,
    material: Material,
    parsed_primary_index: int | None,
) -> int | None:
    pair = raw_texture_stage_active_mapper_pair(
        model,
        material,
        parsed_primary_index,
    )
    return None if pair is None else pair[0]


def raw_texture_stage_active_mapper_pair_secondary_index(
    model: CmbModel,
    material: Material,
    primary_index: int | None,
) -> int | None:
    pair = raw_texture_stage_active_mapper_pair(
        model,
        material,
        first_valid_texture(material),
    )
    if pair is None or primary_index != pair[0]:
        return None
    return pair[1]


def raw_texture_stage_primary_prefix_secondary_index(
    model: CmbModel,
    material: Material,
    primary_index: int | None,
) -> int | None:
    pair = raw_texture_stage_primary_prefix_secondary_pair(
        model,
        material,
        primary_index,
    )
    if pair is None:
        return None
    return pair[1]


def raw_texture_stage_primary_prefix_secondary_pair(
    model: CmbModel,
    material: Material,
    primary_index: int | None,
) -> tuple[int, int] | None:
    parsed_primary_index = first_valid_texture(material)
    if (
        primary_index is None
        or parsed_primary_index is None
        or primary_index != parsed_primary_index
        or not 0 <= primary_index < len(model.textures)
    ):
        return None

    selector = raw_texture_stage_selector(material)
    if selector["stage_count"] != 2:
        return None
    stage_indices = selector["stage_indices"]
    if not isinstance(stage_indices, list) or len(stage_indices) != 2:
        return None
    raw_primary_index = stage_indices[0]
    raw_secondary_index = stage_indices[1]
    if not isinstance(raw_primary_index, int) or not isinstance(raw_secondary_index, int):
        return None
    if raw_primary_index != primary_index:
        return None
    if raw_secondary_index == primary_index:
        return None
    if not 0 <= raw_secondary_index < len(model.textures):
        return None

    parsed_secondary_slot = secondary_valid_texture_slot(material, primary_index)
    if parsed_secondary_slot is None or parsed_secondary_slot >= len(material.texture_mappers):
        return None
    if material.texture_mappers[parsed_secondary_slot].index == raw_secondary_index:
        return None
    if raw_stage_is_material_primary_reference(model, raw_secondary_index):
        return None
    return raw_primary_index, raw_secondary_index


def has_raw_texture_stage_primary_prefix_secondary_candidate(
    model: CmbModel,
    material: Material,
    primary_index: int | None,
) -> bool:
    pair = raw_texture_stage_primary_prefix_secondary_pair(
        model,
        material,
        primary_index,
    )
    return pair is not None and primary_index == pair[0]


def raw_stage_is_material_primary_reference(
    model: CmbModel,
    raw_stage_index: int,
) -> bool:
    if raw_stage_index < 0 or raw_stage_index >= len(model.materials):
        return False
    return first_valid_texture(model.materials[raw_stage_index]) == raw_stage_index


def raw_stage_is_material_index_reference(
    model: CmbModel,
    raw_stage_index: int,
) -> bool:
    return 0 <= raw_stage_index < len(model.materials)


def raw_stage_is_ambiguous_scene_material_reference(
    model: CmbModel,
    raw_stage_index: int,
) -> bool:
    return (
        model_source_is_scene(model)
        and raw_stage_is_material_index_reference(model, raw_stage_index)
        and not raw_stage_is_material_primary_reference(model, raw_stage_index)
    )


def raw_stage_pair_has_ambiguous_scene_material_reference(
    model: CmbModel,
    primary_index: int,
    secondary_index: int,
) -> bool:
    return raw_stage_is_ambiguous_scene_material_reference(
        model,
        primary_index,
    ) or raw_stage_is_ambiguous_scene_material_reference(
        model,
        secondary_index,
    )


def raw_texture_stage_direct_primary_with_exported_secondary_index(
    model: CmbModel,
    material: Material,
    parsed_primary_index: int | None,
) -> int | None:
    pair = raw_texture_stage_direct_primary_with_exported_secondary_pair(
        model,
        material,
        parsed_primary_index,
    )
    if pair is None:
        return None
    return pair[0]


def raw_texture_stage_direct_primary_with_exported_secondary_pair(
    model: CmbModel,
    material: Material,
    parsed_primary_index: int | None,
) -> tuple[int, int] | None:
    if (
        parsed_primary_index is None
        or not 0 <= parsed_primary_index < len(model.textures)
    ):
        return None
    parsed_secondary_slot = secondary_valid_texture_slot(material, parsed_primary_index)
    if parsed_secondary_slot is None or parsed_secondary_slot >= len(material.texture_mappers):
        return None
    parsed_secondary_index = material.texture_mappers[parsed_secondary_slot].index
    if not 0 <= parsed_secondary_index < len(model.textures):
        return None

    selector = raw_texture_stage_selector(material)
    if selector["stage_count"] != 2:
        return None
    stage_indices = selector["stage_indices"]
    if not isinstance(stage_indices, list) or len(stage_indices) != 2:
        return None
    raw_primary_index = stage_indices[0]
    raw_secondary_index = stage_indices[1]
    if not isinstance(raw_primary_index, int) or not isinstance(raw_secondary_index, int):
        return None
    if parsed_primary_index in (raw_primary_index, raw_secondary_index):
        return None
    if raw_secondary_index != parsed_secondary_index:
        return None
    if raw_primary_index == raw_secondary_index:
        return None
    if not 0 <= raw_primary_index < len(model.textures):
        return None
    if raw_stage_is_material_primary_reference(model, raw_primary_index):
        return None
    return raw_primary_index, raw_secondary_index


def has_raw_texture_stage_direct_primary_with_exported_secondary_candidate(
    model: CmbModel,
    material: Material,
    primary_index: int | None,
) -> bool:
    pair = raw_texture_stage_direct_primary_with_exported_secondary_pair(
        model,
        material,
        first_valid_texture(material),
    )
    return pair is not None and primary_index == pair[0]


def raw_texture_stage_active_mapper_pair(
    model: CmbModel,
    material: Material,
    parsed_primary_index: int | None,
) -> tuple[int, int] | None:
    pair = raw_texture_stage_active_mapper_pair_indices(material, parsed_primary_index)
    if pair is None:
        return None
    primary_index, secondary_index = pair
    if not 0 <= primary_index < len(model.textures):
        return None
    if not 0 <= secondary_index < len(model.textures):
        return None
    return primary_index, secondary_index


def raw_texture_stage_active_mapper_pair_indices(
    material: Material,
    parsed_primary_index: int | None,
) -> tuple[int, int] | None:
    if parsed_primary_index is None or parsed_primary_index < 0:
        return None

    selector = raw_texture_stage_selector(material)
    if selector["stage_count"] != 2:
        return None
    stage_indices = selector["stage_indices"]
    if not isinstance(stage_indices, list) or len(stage_indices) != 2:
        return None
    raw_primary_index = stage_indices[0]
    raw_secondary_index = stage_indices[1]
    if not isinstance(raw_primary_index, int) or not isinstance(raw_secondary_index, int):
        return None
    if raw_primary_index == raw_secondary_index:
        return None
    if parsed_primary_index in (raw_primary_index, raw_secondary_index):
        return None

    parsed_secondary_slot = secondary_valid_texture_slot(material, parsed_primary_index)
    if parsed_secondary_slot is None or parsed_secondary_slot >= len(material.texture_mappers):
        return None
    if material.texture_mappers[parsed_secondary_slot].index != raw_secondary_index:
        return None

    mapper_count = material.texture_mappers_used or len(material.texture_mappers)
    active_mapper_indices = {
        mapper.index
        for mapper in material.texture_mappers[:mapper_count]
        if mapper.index >= 0
    }
    if raw_primary_index not in active_mapper_indices:
        return None
    if raw_secondary_index not in active_mapper_indices:
        return None
    return raw_primary_index, raw_secondary_index


def has_raw_texture_stage_active_mapper_pair_candidate(
    material: Material,
    primary_index: int | None,
) -> bool:
    pair = raw_texture_stage_active_mapper_pair_indices(
        material,
        first_valid_texture(material),
    )
    return pair is not None and primary_index == pair[0]


def has_raw_texture_stage_exported_order_candidate(
    material: Material,
    primary_index: int | None,
    secondary_index: int | None,
) -> bool:
    if primary_index is None or secondary_index is None:
        return False
    if primary_index == secondary_index:
        return False
    selector = raw_texture_stage_selector(material)
    if int(selector.get("stage_count", 0) or 0) != 2:
        return False
    stage_indices = selector.get("stage_indices")
    if not isinstance(stage_indices, list) or len(stage_indices) != 2:
        return False
    raw_primary_index = stage_indices[0]
    raw_secondary_index = stage_indices[1]
    return raw_primary_index == primary_index and raw_secondary_index == secondary_index


def raw_texture_stage_repeated_mapper_prefix_pair(
    model: CmbModel,
    material: Material,
    parsed_primary_index: int | None,
) -> tuple[int, int] | None:
    if (
        parsed_primary_index is None
        or not 0 <= parsed_primary_index < len(model.textures)
        or model_source_is_scene(model)
    ):
        return None
    active_slots = repeated_active_texture_mapper_slots(material)
    if active_slots is None:
        return None
    first_slot, second_slot = active_slots
    if material.texture_mappers[first_slot].index != parsed_primary_index:
        return None
    if material.texture_mappers[second_slot].index != parsed_primary_index:
        return None

    selector = raw_texture_stage_selector(material)
    stage_indices = selector["stage_indices"]
    if not isinstance(stage_indices, list) or len(stage_indices) < 2:
        return None
    raw_primary_index = stage_indices[0]
    raw_secondary_index = stage_indices[1]
    if not isinstance(raw_primary_index, int) or not isinstance(raw_secondary_index, int):
        return None
    if raw_primary_index == raw_secondary_index:
        return None
    if raw_primary_index == parsed_primary_index or raw_secondary_index == parsed_primary_index:
        return None
    if not 0 <= raw_primary_index < len(model.textures):
        return None
    if not 0 <= raw_secondary_index < len(model.textures):
        return None
    return raw_primary_index, raw_secondary_index


def repeated_active_texture_mapper_slots(material: Material) -> tuple[int, int] | None:
    mapper_count = material.texture_mappers_used or len(material.texture_mappers)
    active_slots = [
        slot
        for slot, mapper in enumerate(material.texture_mappers[:mapper_count])
        if mapper.index >= 0
    ]
    if len(active_slots) <= 1:
        return None
    active_indices = {
        material.texture_mappers[slot].index
        for slot in active_slots
    }
    if len(active_indices) != 1:
        return None
    return active_slots[0], active_slots[1]


def has_raw_texture_stage_single_stage_duplicate_same_texture_candidate(
    material: Material,
    primary_index: int | None,
) -> bool:
    if primary_index is None or primary_index < 0:
        return False
    selector = raw_texture_stage_selector(material)
    if int(selector.get("stage_count", 0) or 0) != 1:
        return False
    stage_indices = selector.get("stage_indices")
    if not isinstance(stage_indices, list) or len(stage_indices) != 1:
        return False
    raw_primary_index = stage_indices[0]
    if raw_primary_index != primary_index:
        return False
    mapper_count = material.texture_mappers_used or len(material.texture_mappers)
    active_indices = [
        mapper.index
        for mapper in material.texture_mappers[:mapper_count]
        if mapper.index >= 0
    ]
    if len(active_indices) <= 1:
        return False
    return all(index == primary_index for index in active_indices)


def has_raw_texture_stage_repeated_mapper_prefix_candidate(
    material: Material,
    primary_index: int | None,
) -> bool:
    if primary_index is None:
        return False
    parsed_primary_index = first_valid_texture(material)
    if parsed_primary_index is None:
        return False
    active_slots = repeated_active_texture_mapper_slots(material)
    if active_slots is None:
        return False
    if material.texture_mappers[active_slots[0]].index != parsed_primary_index:
        return False
    selector = raw_texture_stage_selector(material)
    stage_indices = selector["stage_indices"]
    if not isinstance(stage_indices, list) or len(stage_indices) < 2:
        return False
    raw_primary_index = stage_indices[0]
    raw_secondary_index = stage_indices[1]
    return (
        isinstance(raw_primary_index, int)
        and isinstance(raw_secondary_index, int)
        and raw_primary_index == primary_index
        and raw_primary_index != raw_secondary_index
        and raw_primary_index != parsed_primary_index
        and raw_secondary_index != parsed_primary_index
        and raw_secondary_index >= 0
    )


def has_raw_texture_stage_repeated_primary_raw_secondary_candidate(
    model: CmbModel,
    material: Material,
    primary_index: int | None,
    secondary_index: int | None,
) -> bool:
    if primary_index is None or secondary_index is None:
        return False
    if primary_index == secondary_index:
        return False
    if not model.is_static_candidate() or not model_source_is_scene(model):
        return False
    active_slots = repeated_active_texture_mapper_slots(material)
    if active_slots is None:
        return False
    if material.texture_mappers[active_slots[0]].index != primary_index:
        return False
    if material.texture_mappers[active_slots[1]].index != primary_index:
        return False
    selector = raw_texture_stage_selector(material)
    if int(selector.get("stage_count", 0) or 0) != 2:
        return False
    stage_indices = selector.get("stage_indices")
    if not isinstance(stage_indices, list) or len(stage_indices) < 2:
        return False
    raw_primary_index = stage_indices[0]
    raw_secondary_index = stage_indices[1]
    if not isinstance(raw_primary_index, int) or not isinstance(raw_secondary_index, int):
        return False
    if raw_primary_index != primary_index or raw_secondary_index != secondary_index:
        return False
    if secondary_texture_coord_export_status(material, primary_index) == "not_exported":
        return False
    return True


def raw_texture_stage_repeated_mapper_prefix_secondary_slot(
    material: Material,
    primary_index: int | None,
) -> int | None:
    if not has_raw_texture_stage_repeated_mapper_prefix_candidate(material, primary_index):
        return None
    active_slots = repeated_active_texture_mapper_slots(material)
    return None if active_slots is None else active_slots[1]


def has_raw_texture_stage_prefix_selected_primary_candidate(
    material: Material,
    primary_index: int | None,
) -> bool:
    parsed_primary_index = first_valid_texture(material)
    if primary_index is None or parsed_primary_index is None:
        return False
    if primary_index == parsed_primary_index:
        return False
    parsed_secondary_slot = secondary_valid_texture_slot(material, parsed_primary_index)
    if parsed_secondary_slot is None or parsed_secondary_slot >= len(material.texture_mappers):
        return False
    parsed_secondary_index = material.texture_mappers[parsed_secondary_slot].index
    selector = raw_texture_stage_selector(material)
    stage_indices = selector["stage_indices"]
    if not isinstance(stage_indices, list) or len(stage_indices) < 2:
        return False
    if parsed_primary_index in stage_indices:
        return False
    raw_primary_index = stage_indices[0]
    raw_secondary_index = stage_indices[1]
    if not isinstance(raw_primary_index, int) or not isinstance(raw_secondary_index, int):
        return False
    return (
        raw_primary_index == primary_index
        and raw_primary_index == parsed_secondary_index
        and raw_secondary_index != primary_index
    )


def raw_texture_stage_primary_index(model: CmbModel, material: Material) -> int | None:
    if first_valid_texture(material) is not None:
        return None
    mapper_count = material.texture_mappers_used or len(material.texture_mappers)
    if any(mapper.index >= 0 for mapper in material.texture_mappers[:mapper_count]):
        return None
    selector = raw_texture_stage_selector(material)
    if selector["stage_count"] != 1:
        return None
    stage_indices = selector["stage_indices"]
    if not isinstance(stage_indices, list) or len(stage_indices) != 1:
        return None
    texture_index = stage_indices[0]
    if isinstance(texture_index, int) and 0 <= texture_index < len(model.textures):
        return texture_index
    return None


def first_valid_texture_slot(material: Material) -> int | None:
    if material.texture_mappers:
        mapper_count = material.texture_mappers_used or len(material.texture_mappers)
        for slot, mapper in enumerate(material.texture_mappers[:mapper_count]):
            if mapper.index < 0:
                continue
            return slot

    for slot, index in enumerate(material.texture_indices):
        if index >= 0:
            return slot
    return None


def material_texture_mapper(
    material: Material,
    primary_index: int | None = None,
) -> MaterialTexture | None:
    slot = material_primary_texture_slot(material, primary_index)
    if slot is None or slot >= len(material.texture_mappers):
        return None
    return material.texture_mappers[slot]


def material_slot_secondary_texture(
    model: CmbModel,
    material: Material,
    primary_index: int | None,
) -> Texture | None:
    slot = secondary_valid_texture_slot(material, primary_index)
    if slot is None:
        return None
    texture_index = material.texture_mappers[slot].index
    if texture_index < 0 or texture_index >= len(model.textures):
        return None
    return model.textures[texture_index]


def secondary_material_texture(model: CmbModel, material: Material, primary_index: int | None) -> Texture | None:
    texture_index = raw_texture_stage_repeated_mapper_prefix_secondary_index(
        model,
        material,
        primary_index,
    )
    if texture_index is None:
        texture_index = raw_texture_stage_active_mapper_pair_secondary_index(
            model,
            material,
            primary_index,
        )
    if texture_index is None:
        texture_index = raw_texture_stage_prefix_secondary_index(model, material, primary_index)
    if texture_index is None:
        texture_index = raw_texture_stage_primary_prefix_secondary_index(
            model,
            material,
            primary_index,
        )
    if (
        texture_index is None
        and has_raw_texture_stage_single_stage_duplicate_same_texture_candidate(
            material,
            primary_index,
        )
    ):
        return None
    if texture_index is None:
        slot = secondary_valid_texture_slot(material, primary_index)
        if slot is not None:
            texture_index = material.texture_mappers[slot].index
        else:
            if secondary_stage_mapper_slot(material) is not None:
                texture_index = raw_texture_stage_secondary_index(model, material, primary_index)
            else:
                texture_index = raw_texture_stage_single_mapper_secondary_index(
                    model,
                    material,
                    primary_index,
                )
            if texture_index is None:
                same_texture_slot = raw_texture_stage_mapper_slot_same_texture_secondary_slot(
                    material,
                    primary_index,
                )
                if same_texture_slot is None:
                    same_texture_slot = same_texture_transformed_secondary_slot(
                        material,
                        primary_index,
                    )
                if same_texture_slot is None:
                    return None
                texture_index = material.texture_mappers[same_texture_slot].index
    if texture_index < 0 or texture_index >= len(model.textures):
        return None
    return model.textures[texture_index]


def raw_texture_stage_baked_extra_texture_index(
    model: CmbModel,
    material: Material,
    primary_index: int | None,
    secondary_index: int | None,
) -> int | None:
    if primary_index is None or secondary_index is None:
        return None
    if primary_index == secondary_index:
        return None
    if not model.is_static_candidate() or not model_source_is_scene(model):
        return None
    if not (0 <= primary_index < len(model.textures)):
        return None
    if not (0 <= secondary_index < len(model.textures)):
        return None

    selector = raw_texture_stage_selector(material)
    if selector["stage_count"] != 3:
        return None
    stage_indices = selector["stage_indices"]
    if not isinstance(stage_indices, list) or len(stage_indices) != 3:
        return None
    raw_primary, raw_secondary, raw_extra = stage_indices
    if not all(isinstance(index, int) for index in (raw_primary, raw_secondary, raw_extra)):
        return None
    if raw_primary != primary_index or raw_secondary != secondary_index:
        return None
    if raw_extra in (primary_index, secondary_index):
        return None
    if raw_extra < 0 or raw_extra >= len(model.textures):
        return None

    primary_slot = material_primary_texture_slot(material, primary_index)
    secondary_slot = secondary_material_texture_slot(material, primary_index)
    primary_coord = active_texture_coord_for_slot(material, primary_slot)
    secondary_coord = active_texture_coord_for_slot(material, secondary_slot)
    if texture_coord_is_transformed(primary_coord):
        return None
    if texture_coord_is_transformed(secondary_coord):
        return None
    if secondary_texture_coord_export_status(material, primary_index) != "identity":
        return None
    return raw_extra


def has_raw_texture_stage_baked_extra_texture_candidate(
    model: CmbModel,
    material: Material,
    primary_index: int | None,
    secondary_index: int | None,
) -> bool:
    return (
        raw_texture_stage_baked_extra_texture_index(
            model,
            material,
            primary_index,
            secondary_index,
        )
        is not None
    )


def secondary_material_texture_mapper(
    material: Material,
    primary_index: int | None = None,
) -> MaterialTexture | None:
    primary = first_valid_texture(material) if primary_index is None else primary_index
    repeated_prefix_slot = raw_texture_stage_repeated_mapper_prefix_secondary_slot(
        material,
        primary,
    )
    if repeated_prefix_slot is not None and repeated_prefix_slot < len(material.texture_mappers):
        return material.texture_mappers[repeated_prefix_slot]
    if has_raw_texture_stage_prefix_selected_primary_candidate(material, primary):
        return material_texture_mapper(material, primary)
    slot = secondary_material_texture_slot(material, primary)
    if slot is None:
        if (
            not has_single_mapper_raw_secondary_candidate(material, primary)
            and single_mapper_raw_texture_stage_pair(material) is None
        ):
            return None
        return material_texture_mapper(material, primary)
    return material.texture_mappers[slot]


def secondary_material_texture_slot(material: Material, primary_index: int | None) -> int | None:
    repeated_prefix_slot = raw_texture_stage_repeated_mapper_prefix_secondary_slot(
        material,
        primary_index,
    )
    if repeated_prefix_slot is not None:
        return repeated_prefix_slot
    if has_raw_texture_stage_prefix_selected_primary_candidate(material, primary_index):
        return None
    if has_raw_texture_stage_single_stage_duplicate_same_texture_candidate(
        material,
        primary_index,
    ):
        return None
    slot = secondary_valid_texture_slot(material, primary_index)
    if slot is not None:
        return slot
    slot = raw_texture_stage_mapper_slot_same_texture_secondary_slot(material, primary_index)
    if slot is not None:
        return slot
    slot = same_texture_transformed_secondary_slot(material, primary_index)
    if slot is not None:
        return slot
    return secondary_stage_mapper_slot(material)


def secondary_valid_texture_slot(material: Material, primary_index: int | None) -> int | None:
    if not material.texture_mappers:
        return None
    mapper_count = material.texture_mappers_used or len(material.texture_mappers)
    for slot, mapper in enumerate(material.texture_mappers[:mapper_count]):
        if slot == first_valid_texture_slot(material):
            continue
        if mapper.index < 0:
            continue
        if primary_index is not None and mapper.index == primary_index:
            continue
        return slot
    return None


def raw_texture_stage_mapper_slot_same_texture_secondary_slot(
    material: Material,
    primary_index: int | None,
) -> int | None:
    if primary_index is None or primary_index < 0:
        return None
    selector = raw_texture_stage_selector(material)
    if int(selector.get("stage_count", 0) or 0) < 2:
        return None
    stage_indices = selector.get("stage_indices")
    if not isinstance(stage_indices, list) or len(stage_indices) < 2:
        return None
    raw_primary_slot = stage_indices[0]
    raw_secondary_slot = stage_indices[1]
    if not isinstance(raw_primary_slot, int) or not isinstance(raw_secondary_slot, int):
        return None
    mapper_count = material.texture_mappers_used or len(material.texture_mappers)
    if raw_primary_slot < 0 or raw_primary_slot >= mapper_count:
        return None
    if raw_secondary_slot < 0 or raw_secondary_slot >= mapper_count:
        return None
    if raw_primary_slot == raw_secondary_slot:
        return None
    primary_slot = material_primary_texture_slot(material, primary_index)
    if raw_primary_slot != primary_slot:
        return None
    primary_mapper = material.texture_mappers[raw_primary_slot]
    secondary_mapper = material.texture_mappers[raw_secondary_slot]
    if primary_mapper.index != primary_index or secondary_mapper.index != primary_index:
        return None
    primary_coord = active_texture_coord_for_slot(material, raw_primary_slot)
    secondary_coord = active_texture_coord_for_slot(material, raw_secondary_slot)
    if not primary_texture_coord_can_bake(primary_coord):
        return None
    if not secondary_texture_coord_can_bake(secondary_coord):
        return None
    if texture_coords_numerically_differ(primary_coord, secondary_coord):
        return None
    return raw_secondary_slot


def has_raw_texture_stage_mapper_slot_same_texture_candidate(
    material: Material,
    primary_index: int | None,
) -> bool:
    return raw_texture_stage_mapper_slot_same_texture_secondary_slot(
        material,
        primary_index,
    ) is not None


def same_texture_transformed_secondary_slot(
    material: Material,
    primary_index: int | None,
) -> int | None:
    if primary_index is None or primary_index < 0:
        return None
    selector = raw_texture_stage_selector(material)
    if int(selector.get("stage_count", 0) or 0) < 2:
        return None
    primary_slot = material_primary_texture_slot(material, primary_index)
    primary_coord = active_texture_coord_for_slot(material, primary_slot)
    if not primary_texture_coord_can_bake(primary_coord):
        return None
    mapper_count = material.texture_mappers_used or len(material.texture_mappers)
    for slot, mapper in enumerate(material.texture_mappers[:mapper_count]):
        if slot == primary_slot:
            continue
        if mapper.index != primary_index:
            continue
        coord = active_texture_coord_for_slot(material, slot)
        if not secondary_texture_coord_can_bake(coord):
            continue
        if not texture_coords_numerically_differ(primary_coord, coord):
            continue
        return slot
    return None


def has_same_texture_transformed_secondary_candidate(
    material: Material,
    primary_index: int | None,
) -> bool:
    return same_texture_transformed_secondary_slot(material, primary_index) is not None


def texture_coords_numerically_differ(
    first: TextureCoord | None,
    second: TextureCoord | None,
) -> bool:
    if first is None or second is None:
        return False
    return (
        first.coordinate_index != second.coordinate_index
        or abs(first.scale.x - second.scale.x) > FLOAT_EPSILON
        or abs(first.scale.y - second.scale.y) > FLOAT_EPSILON
        or abs(first.rotation - second.rotation) > FLOAT_EPSILON
        or abs(first.translation.x - second.translation.x) > FLOAT_EPSILON
        or abs(first.translation.y - second.translation.y) > FLOAT_EPSILON
    )


def secondary_stage_mapper_slot(material: Material) -> int | None:
    if not material.texture_mappers:
        return None
    primary_slot = first_valid_texture_slot(material)
    mapper_count = material.texture_mappers_used or len(material.texture_mappers)
    for slot, mapper in enumerate(material.texture_mappers[:mapper_count]):
        if slot == primary_slot:
            continue
        if mapper.index < 0:
            continue
        return slot
    return None


def raw_texture_stage_secondary_index(
    model: CmbModel,
    material: Material,
    primary_index: int | None,
) -> int | None:
    selector = raw_texture_stage_selector(material)
    stage_indices = selector["stage_indices"]
    if not isinstance(stage_indices, list):
        return None
    if model_source_is_scene(model):
        if not stage_indices or stage_indices[0] != primary_index:
            return None
    for texture_index in stage_indices:
        if not isinstance(texture_index, int):
            continue
        if primary_index is not None and texture_index == primary_index:
            continue
        if 0 <= texture_index < len(model.textures):
            return texture_index
    return None


def raw_texture_stage_single_mapper_secondary_index(
    model: CmbModel,
    material: Material,
    primary_index: int | None,
) -> int | None:
    if not has_single_mapper_raw_secondary_candidate(material, primary_index):
        return None
    selector = raw_texture_stage_selector(material)
    stage_indices = selector["stage_indices"]
    if not isinstance(stage_indices, list) or len(stage_indices) < 2:
        return None
    texture_index = stage_indices[1]
    if not isinstance(texture_index, int):
        return None
    if raw_stage_is_ambiguous_scene_material_reference(model, texture_index):
        return None
    if 0 <= texture_index < len(model.textures):
        return texture_index
    return None


def has_single_mapper_raw_secondary_candidate(
    material: Material,
    primary_index: int | None,
) -> bool:
    pair = single_mapper_raw_texture_stage_pair(material)
    if pair is None or primary_index is None:
        return False
    return pair[0] == primary_index and pair[1] != primary_index


def single_mapper_raw_texture_stage_pair(material: Material) -> tuple[int, int] | None:
    if secondary_stage_mapper_slot(material) is not None:
        return None
    primary_slot = first_valid_texture_slot(material)
    if primary_slot is None:
        return None
    mapper_count = material.texture_mappers_used or len(material.texture_mappers)
    active_mapper_count = sum(
        1
        for mapper in material.texture_mappers[:mapper_count]
        if mapper.index >= 0
    )
    if active_mapper_count != 1:
        return None
    selector = raw_texture_stage_selector(material)
    if selector["stage_count"] != 2:
        return None
    stage_indices = selector["stage_indices"]
    if not isinstance(stage_indices, list) or len(stage_indices) < 2:
        return None
    primary_index = stage_indices[0]
    secondary_index = stage_indices[1]
    if not isinstance(primary_index, int) or not isinstance(secondary_index, int):
        return None
    if primary_index == secondary_index:
        return None
    return primary_index, secondary_index


def secondary_texture_coord_export_status(material: Material, primary_index: int | None) -> str:
    if not secondary_texture_coord_is_transformed(material, primary_index):
        return "identity"
    if secondary_texture_coord_bake_supported(material, primary_index):
        return "baked_texture"
    return "not_exported"


def secondary_texture_coord_bake_supported(material: Material, primary_index: int | None) -> bool:
    primary_slot = material_primary_texture_slot(material, primary_index)
    secondary_slot = secondary_material_texture_slot(material, primary_index)
    if secondary_slot is None:
        return False
    primary_coord = active_texture_coord_for_slot(material, primary_slot)
    secondary_coord = active_texture_coord_for_slot(material, secondary_slot)
    return (
        primary_texture_coord_can_bake(primary_coord)
        and secondary_texture_coord_can_bake(secondary_coord)
        and texture_coord_is_transformed(secondary_coord)
    )


def secondary_texture_coord_is_transformed(material: Material, primary_index: int | None) -> bool:
    secondary_slot = secondary_material_texture_slot(material, primary_index)
    secondary_coord = active_texture_coord_for_slot(material, secondary_slot)
    return texture_coord_is_transformed(secondary_coord)


def primary_texture_coord_can_bake(coord: TextureCoord | None) -> bool:
    if coord is None:
        return True
    return (
        coord.coordinate_index == 0
        and math.isfinite(coord.scale.x)
        and math.isfinite(coord.scale.y)
        and abs(coord.scale.x) > FLOAT_EPSILON
        and abs(coord.scale.y) > FLOAT_EPSILON
        and math.isfinite(coord.rotation)
        and math.isfinite(coord.translation.x)
        and math.isfinite(coord.translation.y)
    )


def secondary_texture_coord_can_bake(coord: TextureCoord | None) -> bool:
    return primary_texture_coord_can_bake(coord)


def bake_secondary_texture_rgba16(
    primary_texture: Texture,
    secondary_texture: Texture,
    material: Material,
    primary_index: int | None,
) -> bytes:
    primary_slot = material_primary_texture_slot(material, primary_index)
    secondary_slot = secondary_material_texture_slot(material, primary_index)
    primary_coord = active_texture_coord_for_slot(material, primary_slot)
    secondary_coord = active_texture_coord_for_slot(material, secondary_slot)
    secondary_mapper = secondary_material_texture_mapper(material, primary_index)
    source = secondary_texture.as_rgba16()
    output = bytearray(primary_texture.width * primary_texture.height * 2)

    for y in range(primary_texture.height):
        for x in range(primary_texture.width):
            rendered_uv = Vec2(
                (x + 0.5) / primary_texture.width,
                (y + 0.5) / primary_texture.height,
            )
            base_uv = invert_texture_coord(primary_coord, rendered_uv)
            source_uv = apply_texture_coord_for_bake(secondary_coord, base_uv)
            source_x = texture_sample_axis(
                source_uv.x,
                secondary_texture.width,
                secondary_mapper.wrap_s if secondary_mapper else None,
            )
            source_y = texture_sample_axis(
                source_uv.y,
                secondary_texture.height,
                secondary_mapper.wrap_t if secondary_mapper else None,
            )
            source_offset = ((source_y * secondary_texture.width) + source_x) * 2
            output_offset = ((y * primary_texture.width) + x) * 2
            output[output_offset : output_offset + 2] = source[source_offset : source_offset + 2]

    return bytes(output)


def bake_raw_texture_stage_extra_rgba16(
    secondary_texture: Texture,
    extra_texture: Texture,
    material: Material,
    primary_index: int | None,
) -> bytes:
    secondary = secondary_texture.as_rgba16()
    extra = extra_texture.as_rgba16()
    output = bytearray(secondary_texture.width * secondary_texture.height * 2)
    mapper = secondary_material_texture_mapper(material, primary_index)

    for y in range(secondary_texture.height):
        for x in range(secondary_texture.width):
            uv = Vec2(
                (x + 0.5) / secondary_texture.width,
                (y + 0.5) / secondary_texture.height,
            )
            extra_x = texture_sample_axis(
                uv.x,
                extra_texture.width,
                mapper.wrap_s if mapper else None,
            )
            extra_y = texture_sample_axis(
                uv.y,
                extra_texture.height,
                mapper.wrap_t if mapper else None,
            )
            secondary_offset = ((y * secondary_texture.width) + x) * 2
            extra_offset = ((extra_y * extra_texture.width) + extra_x) * 2
            output[secondary_offset : secondary_offset + 2] = multiply_rgba16_pixels(
                secondary[secondary_offset : secondary_offset + 2],
                extra[extra_offset : extra_offset + 2],
            )

    return bytes(output)


def multiply_rgba16_pixels(left: bytes, right: bytes) -> bytes:
    left_value = int.from_bytes(left[:2], "big")
    right_value = int.from_bytes(right[:2], "big")
    left_r = (left_value >> 11) & 0x1F
    left_g = (left_value >> 6) & 0x1F
    left_b = (left_value >> 1) & 0x1F
    left_a = left_value & 0x01
    right_r = (right_value >> 11) & 0x1F
    right_g = (right_value >> 6) & 0x1F
    right_b = (right_value >> 1) & 0x1F
    right_a = right_value & 0x01
    value = (
        ((left_r * right_r + 15) // 31) << 11
        | ((left_g * right_g + 15) // 31) << 6
        | ((left_b * right_b + 15) // 31) << 1
        | (left_a & right_a)
    )
    return value.to_bytes(2, "big")


def texture_sample_axis(uv: float, size: int, wrap: int | None) -> int:
    if size <= 1:
        return 0
    texel = math.floor(uv * size)
    if wrap == PICA_TEXTURE_WRAP_MIRRORED_REPEAT:
        period = size * 2
        texel %= period
        if texel >= size:
            texel = period - 1 - texel
        return texel
    if wrap in {
        PICA_TEXTURE_WRAP_CLAMP,
        PICA_TEXTURE_WRAP_CLAMP_TO_BORDER,
        PICA_TEXTURE_WRAP_CLAMP_TO_EDGE,
    }:
        return clamp_int(texel, 0, size - 1)
    return texel % size


def transformed_secondary_texture_filename(
    texture: Texture,
    material: Material,
    slot: int | None,
) -> str:
    base = texture.name or f"texture_{texture.index}"
    slot_name = "uvx" if slot is None else f"uv{slot}"
    return f"{base}_mat_{material.index}_{slot_name}.rgba16"


def raw_texture_stage_baked_secondary_texture_filename(
    secondary_texture: Texture,
    extra_texture: Texture,
    material: Material,
) -> str:
    secondary_name = secondary_texture.name or f"texture_{secondary_texture.index}"
    extra_name = extra_texture.name or f"texture_{extra_texture.index}"
    return f"{secondary_name}_mat_{material.index}_stage2_{extra_name}.rgba16"


def material_texture(
    model: CmbModel,
    material: Material | None,
    *,
    profile: MeshTextureExportProfile | None = None,
) -> Texture | None:
    if material is None:
        return None
    profile = profile or mesh_texture_export_profile_from_values(model)
    texture_index = material_primary_texture_index_for_profile(model, material, profile)
    if texture_index is None or texture_index >= len(model.textures):
        return None
    return model.textures[texture_index]


def skeleton_world_transforms(skeleton: Skeleton) -> tuple[Matrix4, ...]:
    local_transforms = tuple(bone_local_transform(bone) for bone in skeleton.bones)
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


def bone_local_transform(bone: SkeletonBone) -> Matrix4:
    scale = bone.scale
    rotation = bone.rotation
    translation = bone.translation
    scale_matrix = matrix_scale(scale)
    rotate_x = matrix_rotate_x(rotation.x)
    rotate_y = matrix_rotate_y(rotation.y)
    rotate_z = matrix_rotate_z(rotation.z)
    translate = matrix_translate(translation)
    return multiply_matrix(
        translate,
        multiply_matrix(
            rotate_z,
            multiply_matrix(rotate_y, multiply_matrix(rotate_x, scale_matrix)),
        ),
    )


def matrix_scale(scale: Vec3) -> Matrix4:
    return (
        (scale.x, 0.0, 0.0, 0.0),
        (0.0, scale.y, 0.0, 0.0),
        (0.0, 0.0, scale.z, 0.0),
        (0.0, 0.0, 0.0, 1.0),
    )


def matrix_translate(translation: Vec3) -> Matrix4:
    return (
        (1.0, 0.0, 0.0, translation.x),
        (0.0, 1.0, 0.0, translation.y),
        (0.0, 0.0, 1.0, translation.z),
        (0.0, 0.0, 0.0, 1.0),
    )


def matrix_rotate_x(angle: float) -> Matrix4:
    sine = math.sin(angle)
    cosine = math.cos(angle)
    return (
        (1.0, 0.0, 0.0, 0.0),
        (0.0, cosine, -sine, 0.0),
        (0.0, sine, cosine, 0.0),
        (0.0, 0.0, 0.0, 1.0),
    )


def matrix_rotate_y(angle: float) -> Matrix4:
    sine = math.sin(angle)
    cosine = math.cos(angle)
    return (
        (cosine, 0.0, sine, 0.0),
        (0.0, 1.0, 0.0, 0.0),
        (-sine, 0.0, cosine, 0.0),
        (0.0, 0.0, 0.0, 1.0),
    )


def matrix_rotate_z(angle: float) -> Matrix4:
    sine = math.sin(angle)
    cosine = math.cos(angle)
    return (
        (cosine, -sine, 0.0, 0.0),
        (sine, cosine, 0.0, 0.0),
        (0.0, 0.0, 1.0, 0.0),
        (0.0, 0.0, 0.0, 1.0),
    )


def multiply_matrix(left: Matrix4, right: Matrix4) -> Matrix4:
    return tuple(
        tuple(sum(left[row][k] * right[k][col] for k in range(4)) for col in range(4))
        for row in range(4)
    )  # type: ignore[return-value]


def transform_position(transform: Matrix4, position: Vec3) -> Vec3:
    x = (
        transform[0][0] * position.x
        + transform[0][1] * position.y
        + transform[0][2] * position.z
        + transform[0][3]
    )
    y = (
        transform[1][0] * position.x
        + transform[1][1] * position.y
        + transform[1][2] * position.z
        + transform[1][3]
    )
    z = (
        transform[2][0] * position.x
        + transform[2][1] * position.y
        + transform[2][2] * position.z
        + transform[2][3]
    )
    return Vec3(x, y, z)


def transform_direction(transform: Matrix4, direction: Vec3) -> Vec3:
    x = (
        transform[0][0] * direction.x
        + transform[0][1] * direction.y
        + transform[0][2] * direction.z
    )
    y = (
        transform[1][0] * direction.x
        + transform[1][1] * direction.y
        + transform[1][2] * direction.z
    )
    z = (
        transform[2][0] * direction.x
        + transform[2][1] * direction.y
        + transform[2][2] * direction.z
    )
    length = math.sqrt(x * x + y * y + z * z)
    if length <= FLOAT_EPSILON:
        return direction
    return Vec3(x / length, y / length, z / length)


def to_legacy_fast_resource_vertex(
    shape: Shape,
    texture: Texture | None,
    material: Material | None,
    index: int,
    *,
    uv_offset: Vec2 | None = None,
    uv_override: Vec2 | None = None,
    static_default_normal: bool = False,
    vertex_transform: Matrix4 | None = None,
) -> LegacyFastResourceVertex:
    if index >= len(shape.positions):
        raise ParseError(f"shape {shape.index}: vertex index {index} has no position")

    position = shape.positions[index]
    normal = shape.normals[index] if index < len(shape.normals) else Vec3(0.0, 0.0, 1.0)
    if vertex_transform is not None:
        position = transform_position(vertex_transform, position)
        normal = transform_direction(vertex_transform, normal)
    elif static_default_normal and normal == Vec3(0.0, 0.0, 1.0):
        normal = Vec3(0.0, 1.0, 0.0)
    color = shape.colors[index] if index < len(shape.colors) else Color(255, 255, 255, 255)
    if uv_override is not None:
        uv = uv_override
    else:
        uv = shape.uv0[index] if index < len(shape.uv0) else Vec2(0.0, 0.0)
        uv = transform_uv(material, uv, texture.index if texture else None)
    if uv_offset is not None:
        uv = Vec2(uv.x + uv_offset.x, uv.y + uv_offset.y)

    width = texture.width if texture else 32
    height = texture.height if texture else 32
    if color == Color(255, 255, 255, 255):
        r, g, b = normal_to_n64(normal)
        a = 255
    else:
        r, g, b, a = color.r, color.g, color.b, color.a

    return LegacyFastResourceVertex(
        x=round_s16(position.x),
        y=round_s16(position.y),
        z=round_s16(position.z),
        s=round_s16(uv.x * width * 32.0),
        t=round_s16(uv.y * height * 32.0),
        r=r,
        g=g,
        b=b,
        a=a,
    )


def mesh_uv_offset(
    shape: Shape,
    material: Material | None,
    triangles: list[tuple[int, int, int]],
    texture: Texture | None = None,
    *,
    uv_overrides: dict[int, Vec2] | None = None,
) -> Vec2:
    primary_index = texture.index if texture else None
    mapper = material_texture_mapper(material, primary_index) if material else None
    if mapper is None:
        return Vec2(0.0, 0.0)

    min_s = math.inf
    min_t = math.inf
    for triangle in triangles:
        for index in triangle:
            if index >= len(shape.uv0):
                continue
            if uv_overrides is not None and index in uv_overrides:
                uv = uv_overrides[index]
            else:
                uv = transform_uv(material, shape.uv0[index], primary_index)
            min_s = min(min_s, uv.x)
            min_t = min(min_t, uv.y)

    if not math.isfinite(min_s) or not math.isfinite(min_t):
        return Vec2(0.0, 0.0)

    offset_s = integer_repeat_offset(min_s, mapper.wrap_s)
    offset_t = integer_repeat_offset(min_t, mapper.wrap_t)
    return Vec2(float(offset_s), float(offset_t))


def uv_orientation_overrides(
    shape: Shape,
    material: Material | None,
    triangles: list[tuple[int, int, int]],
    texture: Texture | None = None,
    uv_orientation: str = TEXTURE_ORIENTATION_NORMAL,
) -> dict[int, Vec2]:
    orientation = normalize_uv_orientation(uv_orientation)
    if orientation == TEXTURE_ORIENTATION_NORMAL or not triangles:
        return {}

    flip_x = orientation in (TEXTURE_ORIENTATION_FLIP_X, TEXTURE_ORIENTATION_FLIP_XY)
    flip_y = orientation in (TEXTURE_ORIENTATION_FLIP_Y, TEXTURE_ORIENTATION_FLIP_XY)
    primary_index = texture.index if texture else None
    overrides: dict[int, Vec2] = {}

    vertex_indices = sorted({index for triangle in triangles for index in triangle})
    for index in vertex_indices:
        uv = transform_uv(
            material,
            shape.uv0[index] if index < len(shape.uv0) else Vec2(0.0, 0.0),
            primary_index,
        )
        overrides[index] = Vec2(
            (1.0 - uv.x) if flip_x else uv.x,
            (1.0 - uv.y) if flip_y else uv.y,
        )

    return overrides


def triangle_vertex_components(
    triangles: list[tuple[int, int, int]],
) -> list[list[tuple[int, int, int]]]:
    if not triangles:
        return []

    parent = list(range(len(triangles)))

    def find(index: int) -> int:
        while parent[index] != index:
            parent[index] = parent[parent[index]]
            index = parent[index]
        return index

    def union(left: int, right: int) -> None:
        left_root = find(left)
        right_root = find(right)
        if left_root != right_root:
            parent[right_root] = left_root

    first_triangle_for_vertex: dict[int, int] = {}
    for triangle_index, triangle in enumerate(triangles):
        for vertex_index in set(triangle):
            previous = first_triangle_for_vertex.get(vertex_index)
            if previous is None:
                first_triangle_for_vertex[vertex_index] = triangle_index
            else:
                union(triangle_index, previous)

    groups: dict[int, list[tuple[int, int, int]]] = {}
    for triangle_index, triangle in enumerate(triangles):
        groups.setdefault(find(triangle_index), []).append(triangle)
    return list(groups.values())


def integer_repeat_offset(min_uv: float, wrap: int) -> int:
    if min_uv >= 0.0 or wrap != PICA_TEXTURE_WRAP_REPEAT:
        return 0
    return int(math.ceil(-min_uv))


def transform_uv(
    material: Material | None,
    uv: Vec2,
    primary_index: int | None = None,
) -> Vec2:
    coord = material_texture_coord(material, primary_index)
    if coord is None:
        return uv
    return apply_texture_coord(coord, uv)


def material_texture_coord(
    material: Material | None,
    primary_index: int | None = None,
) -> TextureCoord | None:
    if material is None:
        return None
    slot = material_primary_texture_slot(material, primary_index)
    return texture_coord_for_slot(material, slot)


def material_primary_texture_slot(
    material: Material,
    primary_index: int | None = None,
) -> int | None:
    if primary_index is not None:
        mapper_count = material.texture_mappers_used or len(material.texture_mappers)
        for slot, mapper in enumerate(material.texture_mappers[:mapper_count]):
            if mapper.index == primary_index:
                return slot
    return first_valid_texture_slot(material)


def active_texture_coord_for_slot(material: Material, slot: int | None) -> TextureCoord | None:
    if slot is None or slot >= len(material.texture_coords):
        return None
    if material.texture_coords_used and slot >= material.texture_coords_used:
        return None
    return material.texture_coords[slot]


def texture_coord_for_slot(material: Material, slot: int | None) -> TextureCoord | None:
    coord = active_texture_coord_for_slot(material, slot)
    if coord is None:
        return None
    if coord.coordinate_index != 0:
        return None
    return coord


def apply_texture_coord(coord: TextureCoord, uv: Vec2) -> Vec2:
    s = uv.x * coord.scale.x
    t = uv.y * coord.scale.y
    if coord.rotation:
        cos_r = math.cos(coord.rotation)
        sin_r = math.sin(coord.rotation)
        s, t = (s * cos_r - t * sin_r, s * sin_r + t * cos_r)
    return Vec2(s + coord.translation.x, t + coord.translation.y)


def apply_texture_coord_for_bake(coord: TextureCoord | None, uv: Vec2) -> Vec2:
    if coord is None:
        return uv
    return apply_texture_coord(coord, uv)


def invert_texture_coord(coord: TextureCoord | None, uv: Vec2) -> Vec2:
    if coord is None:
        return uv
    s = uv.x - coord.translation.x
    t = uv.y - coord.translation.y
    if coord.rotation:
        cos_r = math.cos(-coord.rotation)
        sin_r = math.sin(-coord.rotation)
        s, t = (s * cos_r - t * sin_r, s * sin_r + t * cos_r)
    return Vec2(s / coord.scale.x, t / coord.scale.y)


def texture_coord_is_transformed(coord: TextureCoord | None) -> bool:
    if coord is None:
        return False
    return (
        coord.coordinate_index != 0
        or abs(coord.scale.x - 1.0) > FLOAT_EPSILON
        or abs(coord.scale.y - 1.0) > FLOAT_EPSILON
        or abs(coord.rotation) > FLOAT_EPSILON
        or abs(coord.translation.x) > FLOAT_EPSILON
        or abs(coord.translation.y) > FLOAT_EPSILON
    )


def normal_to_n64(normal: Vec3) -> tuple[int, int, int]:
    return (
        signed_byte_to_u8(round(max(-1.0, min(1.0, normal.x)) * 127.0)),
        signed_byte_to_u8(round(max(-1.0, min(1.0, normal.y)) * 127.0)),
        signed_byte_to_u8(round(max(-1.0, min(1.0, normal.z)) * 127.0)),
    )


def signed_byte_to_u8(value: int) -> int:
    value = max(-128, min(127, value))
    return value if value >= 0 else 256 + value


def round_s16(value: float) -> int:
    rounded = int(round(value))
    return max(-32768, min(32767, rounded))


def clamp_int(value: int, minimum: int, maximum: int) -> int:
    return max(minimum, min(maximum, value))


def max_vertex_index(triangles: tuple[tuple[int, int, int], ...]) -> int:
    return max((vertex for triangle in triangles for vertex in triangle), default=-1)


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def normalize_resource_root(value: str) -> str:
    return value.replace("\\", "/").strip("/")


def unique_texture_filename(texture: Texture, used_filenames: set[str]) -> str:
    base = texture.name or f"texture_{texture.index}"
    filename = f"{base}.rgba16"
    if filename not in used_filenames:
        return filename
    return f"{base}_{texture.index}.rgba16"


def convert_shape_batches(
    shape: Shape,
    texture: Texture | None,
    primitive_indices: tuple[int, ...],
    material: Material | None = None,
) -> list[TriangleBatch]:
    return batch_triangles_for_shape(shape, texture, material, chunk_triangles(primitive_indices))
