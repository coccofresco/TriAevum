from __future__ import annotations

import hashlib
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from .binary import BinaryView, ParseError

PICA_S8 = 0x1400
PICA_U8 = 0x1401
PICA_S16 = 0x1402
PICA_U16 = 0x1403
PICA_S32 = 0x1404
PICA_U32 = 0x1405
PICA_F32 = 0x1406
PICA_UNSIGNED_BYTE_4_4 = 0x6760
PICA_UNSIGNED_4BITS = 0x6761
PICA_UNSIGNED_SHORT_4444 = 0x8033
PICA_UNSIGNED_SHORT_5551 = 0x8034
PICA_UNSIGNED_SHORT_565 = 0x8363

PICA_TEXTURE_RGBA = 0x6752
PICA_TEXTURE_RGB = 0x6754
PICA_TEXTURE_ALPHA = 0x6756
PICA_TEXTURE_LUMINANCE = 0x6757
PICA_TEXTURE_LUMINANCE_ALPHA = 0x6758
PICA_TEXTURE_ETC1 = 0x675A
PICA_TEXTURE_ETC1A4 = 0x675B

PICA_TEXTURE_WRAP_CLAMP = 0x2900
PICA_TEXTURE_WRAP_REPEAT = 0x2901
PICA_TEXTURE_WRAP_CLAMP_TO_BORDER = 0x812D
PICA_TEXTURE_WRAP_CLAMP_TO_EDGE = 0x812F
PICA_TEXTURE_WRAP_MIRRORED_REPEAT = 0x8370

ATTR_POSITION = 0x01
ATTR_NORMAL = 0x02
ATTR_COLOR = 0x04
ATTR_UV0 = 0x08

OOT3D_MATERIAL_SIZE = 0x15C
OOT3D_MATERIAL_LIGHTING_BLOCK_OFFSET = 0xCC
OOT3D_MATERIAL_LIGHTING_BLOCK_MIN_SIZE = 0x2C

PICA_LIGHTING_LUT_INPUT_NATIVE_BASE = 0x62A0
PICA_LIGHTING_LUT_INPUT_NATIVE_COUNT = 6
PICA_BUMP_TEXTURE_UNIT_NATIVE_BASE = 0x84C0
PICA_BUMP_TEXTURE_UNIT_NATIVE_COUNT = 4
PICA_BUMP_MODE_NATIVE_BASE = 0x62C8
PICA_BUMP_MODE_NATIVE_COUNT = 3
PICA_LIGHTING_CONFIG_NATIVE_BASE = 0x62B0
PICA_LIGHTING_CONFIG_NATIVE_COUNT = 7
PICA_LIGHTING_CONFIG_7_NATIVE = 0x62B7
PICA_LIGHTING_CONFIG_7_VALUE = 8
PICA_UNRESOLVED_62C0_SELECTOR_NATIVE_BASE = 0x62C0
PICA_UNRESOLVED_62C0_SELECTOR_NATIVE_COUNT = 4
PICA_62C0_SELECTOR_NATIVE_BASE = 0x62C0
PICA_62C0_SELECTOR_NATIVE_COUNT = 4
PICA_LUT_INPUT_ABS_D0_NATIVE_BASE = 0x62C0
PICA_LUT_INPUT_ABS_D0_NATIVE_COUNT = 4
MATERIAL_LANE_COPIED_BLOCK_DESTINATION_OFFSET = 0x0C
MATERIAL_LANE_COPIED_BLOCK_PICA_BUMP_TEXTURE_UNIT_BYTE_OFFSET = 0x198
MATERIAL_LANE_COPIED_BLOCK_PICA_BUMP_MODE_BYTE_OFFSET = 0x197
MATERIAL_LANE_COPIED_BLOCK_PICA_LIGHTING_CONFIG_BYTE_OFFSET = 0x194
MATERIAL_LANE_COPIED_BLOCK_PICA_62C0_SELECTOR_BYTE_OFFSET = 0x195
MATERIAL_LANE_COPIED_BLOCK_PICA_LUT_INPUT_ABS_D0_BYTE_OFFSET = 0x195
MATERIAL_LANE_COPIED_BLOCK_PICA_LUT_INPUT_BYTE_OFFSET = 0x1A4
MATERIAL_LANE_COPIED_BLOCK_PICA_LUT_SCALE_BYTE_OFFSET = 0x1A6
MATERIAL_LANE_COPIED_BLOCK_FLAG0_BYTE_OFFSET = 0x1A5
MATERIAL_LANE_COPIED_BLOCK_FLAG1_BYTE_OFFSET = 0x19D
MATERIAL_LANE_COPIED_BLOCK_FLAG2_BYTE_OFFSET = 0x19E
MATERIAL_LANE_COPIED_BLOCK_FLAG3_BYTE_OFFSET = 0x19F
MATERIAL_LANE_COPIED_BLOCK_FLAG4_BYTE_OFFSET = 0x1A0
MATERIAL_LANE_COPIED_BLOCK_FLAG5_BYTE_OFFSET = 0x1A1
MATERIAL_LANE_POPULATE_SOURCE = "codebin_004c6364_material_lighting_block_copy"
RUNTIME_LIGHT_PACKET_PACK_ADDRESS = 0x003FA5D0
RUNTIME_LIGHT_PACKET_PACK_FINAL_UPLOAD_ADDRESS = 0x004093F8
RUNTIME_PAYLOAD3_SCALE_GATE_SEMANTIC = (
    "gates payload3 material scale multiply before native runtime light packet upload"
)
RUNTIME_PAYLOAD3_DESCRIPTOR_SCALE_OFFSETS = [0xB0, 0xB1, 0xB2]
PICA_LUT_INPUT_ABS_D0_REGISTER = 0x1D0
PICA_LUT_INPUT_ABS_D0_REGISTER_NAME = "GPUREG_LIGHTING_LUTINPUT_ABS"
PICA_LUT_INPUT_ABS_D0_FIELD_NAME = "disable_d0"
PICA_LUT_INPUT_ABS_D0_SAMPLER_NAME = "d0"
PICA_LUT_INPUT_ABS_D0_BIT_SHIFT = 1
PICA_LUT_INPUT_ABS_D0_DISABLE_BIT_BY_SELECTOR = [1, 0, 0, 0]
PICA_LUT_INPUT_ABS_D0_PACKING_RULE = "selector <= 1 ? 1 - selector : 0"

PICA_LIGHTING_LUT_SCALE_BY_NATIVE_BITS = {
    0x3F800000: 0,
    0x40000000: 1,
    0x40800000: 2,
    0x41000000: 3,
    0x3E800000: 6,
    0x3F000000: 7,
}


@dataclass(frozen=True)
class Vec2:
    x: float
    y: float


@dataclass(frozen=True)
class Vec3:
    x: float
    y: float
    z: float


@dataclass(frozen=True)
class Color:
    r: int
    g: int
    b: int
    a: int


@dataclass(frozen=True)
class Texture:
    index: int
    name: str
    width: int
    height: int
    texture_format: int
    data_type: int
    data: bytes

    def as_rgba16(self) -> bytes:
        if self.texture_format == PICA_TEXTURE_RGB and self.data_type == PICA_UNSIGNED_SHORT_565:
            linear = detile_ctr_texture(self.data, self.width, self.height, 2)
            return decode_rgb565_to_rgba16(linear, self.width * self.height)
        if self.texture_format == PICA_TEXTURE_RGBA and self.data_type == PICA_UNSIGNED_SHORT_5551:
            return detile_ctr_texture(self.data, self.width, self.height, 2)[
                : self.width * self.height * 2
            ]
        if self.texture_format == PICA_TEXTURE_RGBA and self.data_type == PICA_UNSIGNED_SHORT_4444:
            linear = detile_ctr_texture(self.data, self.width, self.height, 2)
            return decode_rgba4_to_rgba16(linear, self.width * self.height)
        if self.texture_format == PICA_TEXTURE_RGBA and self.data_type == PICA_U8:
            linear = detile_ctr_texture(self.data, self.width, self.height, 4)
            return decode_rgba8_to_rgba16(linear, self.width * self.height)
        if self.texture_format == PICA_TEXTURE_RGB and self.data_type == PICA_U8:
            linear = detile_ctr_texture(self.data, self.width, self.height, 3)
            return decode_rgb8_to_rgba16(linear, self.width * self.height)
        if self.texture_format == PICA_TEXTURE_ALPHA and self.data_type == PICA_U8:
            linear = detile_ctr_texture(self.data, self.width, self.height, 1)
            return decode_a8_to_rgba16(linear, self.width * self.height)
        if self.texture_format == PICA_TEXTURE_LUMINANCE and self.data_type == PICA_U8:
            linear = detile_ctr_texture(self.data, self.width, self.height, 1)
            return decode_l8_to_rgba16(linear, self.width * self.height)
        if self.texture_format == PICA_TEXTURE_LUMINANCE and self.data_type == PICA_UNSIGNED_4BITS:
            linear = detile_ctr_texture_4bpp(self.data, self.width, self.height)
            return decode_l4_to_rgba16(linear, self.width * self.height)
        if self.texture_format == PICA_TEXTURE_LUMINANCE_ALPHA and self.data_type == PICA_U8:
            linear = detile_ctr_texture(self.data, self.width, self.height, 2)
            return decode_la8_to_rgba16(linear, self.width * self.height)
        if (
            self.texture_format == PICA_TEXTURE_LUMINANCE_ALPHA
            and self.data_type == PICA_UNSIGNED_BYTE_4_4
        ):
            linear = detile_ctr_texture(self.data, self.width, self.height, 1)
            return decode_la4_to_rgba16(linear, self.width * self.height)
        if self.texture_format == PICA_TEXTURE_ETC1 and self.data_type == 0:
            return decode_etc1_texture_to_rgba16(self.data, self.width, self.height, has_alpha=False)
        if self.texture_format == PICA_TEXTURE_ETC1A4 and self.data_type == 0:
            return decode_etc1_texture_to_rgba16(self.data, self.width, self.height, has_alpha=True)
        raise ParseError(
            f"unsupported CMB texture {self.name!r}: format 0x{self.texture_format:x}, "
            f"type 0x{self.data_type:x}"
        )


@dataclass(frozen=True)
class MaterialTexture:
    index: int
    min_filter: int
    mag_filter: int
    wrap_s: int
    wrap_t: int


@dataclass(frozen=True)
class TextureCoord:
    matrix_mode: int
    reference_camera: int
    mapping_method: int
    coordinate_index: int
    scale: Vec2
    rotation: float
    translation: Vec2


@dataclass(frozen=True)
class MaterialLightingBlock:
    source_offset: int
    size: int
    raw_block: bytes
    pica_bump_texture_unit_raw: int
    pica_bump_texture_unit: int
    pica_bump_texture_unit_recognized: bool
    pica_bump_mode_raw: int
    pica_bump_mode: int
    pica_bump_mode_recognized: bool
    flag1_raw: int
    flag1: bool
    pica_lighting_config_raw: int
    pica_lighting_config: int
    pica_lighting_config_recognized: bool
    unresolved_enum4_raw: int
    unresolved_enum4_encoded: int
    unresolved_enum4_recognized: bool
    pica_62c0_selector_raw: int
    pica_62c0_selector: int
    pica_62c0_selector_recognized: bool
    pica_lut_input_abs_d0_raw: int
    pica_lut_input_abs_d0_selector: int
    pica_lut_input_abs_d0_selector_recognized: bool
    pica_lut_input_abs_d0_disable_bit: int
    pica_lut_input_abs_d0_disable_bit_resolved: bool
    flag2_raw: int
    flag2: bool
    pica_lut_input_abs_sp_disable_bit: int
    pica_lut_input_abs_sp_disable_bit_resolved: bool
    pica_lut_scale_sp: int
    pica_lut_scale_sp_resolved: bool
    flag3_raw: int
    flag3: bool
    flag4_raw: int
    flag4: bool
    pica_lut_input_fr: int
    pica_lut_input_fr_resolved: bool
    flag5_raw: int
    flag5: bool
    pica_lut_input_abs_fr_disable_bit: int
    pica_lut_input_abs_fr_disable_bit_resolved: bool
    flag0_raw: int
    flag0: bool
    pica_lut_input_abs_rb_disable_bit: int
    pica_lut_input_abs_rb_disable_bit_resolved: bool
    pica_lut_input_raw: int
    pica_lut_input: int
    pica_lut_input_recognized: bool
    pica_lut_input_rb: int
    pica_lut_input_rb_recognized: bool
    pica_lut_scale_source_bits: int
    pica_lut_scale_source_value: float
    pica_lut_scale: int
    pica_lut_scale_recognized: bool
    pica_lut_scale_rb: int
    pica_lut_scale_rb_recognized: bool


@dataclass(frozen=True)
class Material:
    index: int
    texture_indices: tuple[int, int, int]
    cull_back: bool
    alpha_test: bool
    depth_test: bool
    depth_write: bool
    alpha_reference: int = 0
    alpha_function: int = 0x0207
    depth_function: int = 0
    blend_mode: int = 0
    blend_src: int = 0
    blend_dst: int = 0
    blend_equation: int = 0
    color_blend_src: int = 0
    color_blend_dst: int = 0
    color_blend_equation: int = 0
    blend_color_alpha: float = 1.0
    texture_mappers_used: int = 0
    texture_coords_used: int = 0
    texture_mappers: tuple[MaterialTexture, ...] = ()
    texture_coords: tuple[TextureCoord, ...] = ()
    lighting_block: MaterialLightingBlock | None = None
    raw_material: bytes = b""
    fragment_lighting_enabled: bool = False
    vertex_lighting_enabled: bool = False
    hemisphere_lighting_enabled: bool = False
    hemisphere_occlusion_enabled: bool = False


@dataclass(frozen=True)
class Primitive:
    indices: tuple[int, ...]
    skinning_mode: int
    bone_indices: tuple[int, ...]


@dataclass(frozen=True)
class Shape:
    index: int
    flags: int
    auto_flags: int
    positions: tuple[Vec3, ...]
    normals: tuple[Vec3, ...]
    colors: tuple[Color, ...]
    uv0: tuple[Vec2, ...]
    primitives: tuple[Primitive, ...]

    def has_attribute(self, attribute_flag: int) -> bool:
        return bool(self.flags & attribute_flag)

    def has_constant_attribute(self, attribute_flag: int) -> bool:
        return self.has_attribute(attribute_flag) and bool(
            self.auto_flags & attribute_flag
        )


@dataclass(frozen=True)
class Mesh:
    index: int
    shape_index: int
    material_index: int
    visibility_id: int


@dataclass(frozen=True)
class SkeletonBone:
    index: int
    parent_index: int
    scale: Vec3
    rotation: Vec3
    translation: Vec3


@dataclass(frozen=True)
class Skeleton:
    chunk_size: int
    header_word_0c: int
    bones: tuple[SkeletonBone, ...]

    @property
    def bone_count(self) -> int:
        return len(self.bones)

    def root_count(self) -> int:
        return sum(1 for bone in self.bones if bone.parent_index == -1)

    def max_depth(self) -> int:
        max_depth = 0
        for bone in self.bones:
            depth = 0
            parent_index = bone.parent_index
            seen: set[int] = set()
            while parent_index >= 0 and parent_index not in seen:
                if parent_index >= len(self.bones):
                    break
                seen.add(parent_index)
                depth += 1
                parent_index = self.bones[parent_index].parent_index
            max_depth = max(max_depth, depth)
        return max_depth

    def bone_index_mismatch_count(self) -> int:
        return sum(1 for order, bone in enumerate(self.bones) if bone.index != order)

    def parent_out_of_range_count(self) -> int:
        return sum(
            1
            for bone in self.bones
            if bone.parent_index < -1 or bone.parent_index >= len(self.bones)
        )

    def non_identity_scale_count(self) -> int:
        return sum(
            1
            for bone in self.bones
            if not (
                math.isclose(bone.scale.x, 1.0, abs_tol=1e-5)
                and math.isclose(bone.scale.y, 1.0, abs_tol=1e-5)
                and math.isclose(bone.scale.z, 1.0, abs_tol=1e-5)
            )
        )

    def nonzero_rotation_count(self) -> int:
        return sum(
            1
            for bone in self.bones
            if not (
                math.isclose(bone.rotation.x, 0.0, abs_tol=1e-5)
                and math.isclose(bone.rotation.y, 0.0, abs_tol=1e-5)
                and math.isclose(bone.rotation.z, 0.0, abs_tol=1e-5)
            )
        )

    def nonzero_translation_count(self) -> int:
        return sum(
            1
            for bone in self.bones
            if not (
                math.isclose(bone.translation.x, 0.0, abs_tol=1e-5)
                and math.isclose(bone.translation.y, 0.0, abs_tol=1e-5)
                and math.isclose(bone.translation.z, 0.0, abs_tol=1e-5)
            )
        )

    def summary(self) -> dict[str, object]:
        expected_chunk_size = 0x10 + self.bone_count * 0x28
        return {
            "chunk_size": self.chunk_size,
            "expected_chunk_size": expected_chunk_size,
            "chunk_size_delta": self.chunk_size - expected_chunk_size,
            "header_word_0c": self.header_word_0c,
            "bone_count": self.bone_count,
            "root_count": self.root_count(),
            "max_depth": self.max_depth(),
            "bone_index_mismatch_count": self.bone_index_mismatch_count(),
            "parent_out_of_range_count": self.parent_out_of_range_count(),
            "non_identity_scale_count": self.non_identity_scale_count(),
            "nonzero_rotation_count": self.nonzero_rotation_count(),
            "nonzero_translation_count": self.nonzero_translation_count(),
        }


@dataclass(frozen=True)
class CmbModel:
    source: str
    name: str
    version: int
    bone_count: int
    skeleton: Skeleton
    textures: tuple[Texture, ...]
    materials: tuple[Material, ...]
    meshes: tuple[Mesh, ...]
    shapes: tuple[Shape, ...]

    @classmethod
    def from_path(cls, path: Path) -> "CmbModel":
        return cls.parse(path.read_bytes(), str(path))

    @classmethod
    def parse(cls, data: bytes, source: str = "<memory>") -> "CmbModel":
        view = BinaryView(data, source)
        if view.bytes(0, 4) != b"cmb ":
            raise ParseError(f"{source}: expected CMB magic")

        file_size = view.u32(0x04)
        if file_size > len(data):
            raise ParseError(
                f"{source}: header size 0x{file_size:x} exceeds file length 0x{len(data):x}"
            )

        version = view.u32(0x08)
        if version != 6:
            raise ParseError(f"{source}: only OOT3D CMB version 6 is supported, got {version}")

        name = view.cstr(0x10, 0x10)
        skl_off = view.u32(0x24)
        index_count = view.u32(0x20)
        mats_off = view.u32(0x28)
        tex_off = view.u32(0x2C)
        sklm_off = view.u32(0x30)
        vatr_off = view.u32(0x38)
        indices_off = view.u32(0x3C)
        texture_data_off = view.u32(0x40)

        textures = parse_textures(view, tex_off, texture_data_off)
        materials = parse_materials(view, mats_off, tex_off)
        meshes, shapes = parse_sklm(view, sklm_off, vatr_off, indices_off, index_count)
        skeleton = parse_skeleton(view, skl_off)

        return cls(
            source=source,
            name=name,
            version=version,
            bone_count=skeleton.bone_count,
            skeleton=skeleton,
            textures=tuple(textures),
            materials=tuple(materials),
            meshes=tuple(meshes),
            shapes=tuple(shapes),
        )

    def summary(self) -> dict[str, object]:
        triangle_count = 0
        primitive_count = 0
        for shape in self.shapes:
            primitive_count += len(shape.primitives)
            triangle_count += sum(len(prim.indices) // 3 for prim in shape.primitives)
        return {
            "path": self.source,
            "format": "cmb",
            "name": self.name,
            "version": self.version,
            "bone_count": self.bone_count,
            "skeleton": self.skeleton.summary(),
            "static_candidate": self.is_static_candidate(),
            "rigid_export_candidate": self.is_rigid_export_candidate(),
            "textures": [
                {
                    "index": texture.index,
                    "name": texture.name,
                    "width": texture.width,
                    "height": texture.height,
                    "format": f"0x{texture.texture_format:x}",
                    "data_type": f"0x{texture.data_type:x}",
                    "data_size": len(texture.data),
                }
                for texture in self.textures
            ],
            "materials": [
                {
                    "index": material.index,
                    "texture_indices": material.texture_indices,
                    "texture_mappers_used": material.texture_mappers_used,
                    "texture_coords_used": material.texture_coords_used,
                    "raw_material_size": len(material.raw_material),
                    "raw_material_sha256": (
                        hashlib.sha256(material.raw_material).hexdigest()
                        if material.raw_material
                        else None
                    ),
                    "material_lighting_block": material_lighting_block_summary(
                        material.lighting_block
                    ),
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
                            "matrix_mode": coord.matrix_mode,
                            "reference_camera": coord.reference_camera,
                            "mapping_method": coord.mapping_method,
                            "coordinate_index": coord.coordinate_index,
                            "scale": (coord.scale.x, coord.scale.y),
                            "rotation": coord.rotation,
                            "translation": (coord.translation.x, coord.translation.y),
                        }
                        for coord in material.texture_coords
                    ],
                    "cull_back": material.cull_back,
                    "alpha_test": material.alpha_test,
                    "alpha_reference": material.alpha_reference,
                    "alpha_function": f"0x{material.alpha_function:x}",
                    "depth_test": material.depth_test,
                    "depth_write": material.depth_write,
                    "depth_function": f"0x{material.depth_function:x}",
                    "blend_mode": material.blend_mode,
                    "blend_src": f"0x{material.blend_src:x}",
                    "blend_dst": f"0x{material.blend_dst:x}",
                    "blend_equation": f"0x{material.blend_equation:x}",
                    "color_blend_src": f"0x{material.color_blend_src:x}",
                    "color_blend_dst": f"0x{material.color_blend_dst:x}",
                    "color_blend_equation": f"0x{material.color_blend_equation:x}",
                    "blend_color_alpha": material.blend_color_alpha,
                }
                for material in self.materials
            ],
            "mesh_count": len(self.meshes),
            "shape_count": len(self.shapes),
            "primitive_count": primitive_count,
            "triangle_count": triangle_count,
            "vertex_count": sum(len(shape.positions) for shape in self.shapes),
        }

    def is_static_candidate(self) -> bool:
        if self.bone_count != 1:
            return False
        return self.is_rigid_export_candidate()

    def is_rigid_export_candidate(self) -> bool:
        return all(
            primitive.skinning_mode == 0
            and len(primitive.bone_indices) == 1
            and 0 <= primitive.bone_indices[0] < self.bone_count
            for shape in self.shapes
            for primitive in shape.primitives
        )


@dataclass(frozen=True)
class VertexList:
    offset: int
    scale: float
    data_type: int
    mode: int
    constant: tuple[float, float, float, float]


@dataclass(frozen=True)
class VertexListData:
    length: int
    offset: int


def parse_skeleton(view: BinaryView, skl_off: int) -> Skeleton:
    if view.bytes(skl_off, 4) != b"skl ":
        raise ParseError(f"{view.source}: expected SKL chunk at 0x{skl_off:x}")
    chunk_size = view.u32(skl_off + 0x04)
    bone_count = view.u32(skl_off + 0x08)
    header_word_0c = view.u32(skl_off + 0x0C)
    expected_chunk_size = 0x10 + bone_count * 0x28
    if chunk_size != expected_chunk_size:
        raise ParseError(
            f"{view.source}: SKL chunk size 0x{chunk_size:x} does not match "
            f"0x10 + {bone_count} * 0x28"
        )
    view.require(skl_off, chunk_size)

    bones: list[SkeletonBone] = []
    for index in range(bone_count):
        base = skl_off + 0x10 + index * 0x28
        bones.append(
            SkeletonBone(
                index=view.u16(base),
                parent_index=view.s16(base + 0x02),
                scale=Vec3(
                    view.f32(base + 0x04),
                    view.f32(base + 0x08),
                    view.f32(base + 0x0C),
                ),
                rotation=Vec3(
                    view.f32(base + 0x10),
                    view.f32(base + 0x14),
                    view.f32(base + 0x18),
                ),
                translation=Vec3(
                    view.f32(base + 0x1C),
                    view.f32(base + 0x20),
                    view.f32(base + 0x24),
                ),
            )
        )

    return Skeleton(
        chunk_size=chunk_size,
        header_word_0c=header_word_0c,
        bones=tuple(bones),
    )


def parse_textures(view: BinaryView, tex_off: int, texture_data_off: int) -> list[Texture]:
    if view.bytes(tex_off, 4) != b"tex ":
        raise ParseError(f"{view.source}: expected TEX chunk at 0x{tex_off:x}")

    texture_count = view.u32(tex_off + 0x08)
    textures: list[Texture] = []
    for index in range(texture_count):
        entry = tex_off + 0x0C + index * 0x24
        data_size = view.u32(entry)
        _mipmap_count = view.u16(entry + 0x04)
        width = view.u16(entry + 0x08)
        height = view.u16(entry + 0x0A)
        texture_format = view.u16(entry + 0x0C)
        data_type = view.u16(entry + 0x0E)
        rel_data_off = view.u32(entry + 0x10)
        name = view.cstr(entry + 0x14, 0x10) or f"texture_{index}"
        data = view.bytes(texture_data_off + rel_data_off, data_size)
        textures.append(
            Texture(
                index=index,
                name=sanitize_identifier(name),
                width=width,
                height=height,
                texture_format=texture_format,
                data_type=data_type,
                data=data,
            )
        )
    return textures


def parse_materials(view: BinaryView, mats_off: int, tex_off: int) -> list[Material]:
    if view.bytes(mats_off, 4) != b"mats":
        raise ParseError(f"{view.source}: expected MATS chunk at 0x{mats_off:x}")

    material_count = view.u32(mats_off + 0x08)
    if material_count == 0:
        return []

    materials_start = mats_off + 0x0C
    materials_end = materials_start + material_count * OOT3D_MATERIAL_SIZE
    if materials_end > tex_off:
        raise ParseError(
            f"{view.source}: MATS material table exceeds TEX chunk "
            f"(materials end 0x{materials_end:x}, TEX starts 0x{tex_off:x})"
        )

    materials: list[Material] = []
    for index in range(material_count):
        base = materials_start + index * OOT3D_MATERIAL_SIZE
        raw_material = view.bytes(base, OOT3D_MATERIAL_SIZE)
        texture_mappers = tuple(
            read_material_texture(view, base + 0x10 + tex_index * 0x18)
            for tex_index in range(3)
        )
        texture_indices = tuple(mapper.index for mapper in texture_mappers)
        texture_coords = tuple(
            read_texture_coord(view, base + 0x58 + coord_index * 0x18)
            for coord_index in range(3)
        )
        materials.append(
            Material(
                index=index,
                texture_indices=texture_indices,  # type: ignore[arg-type]
                cull_back=bool(view.u8(base + 0x04)),
                alpha_test=bool(view.u8(base + 0x130)),
                depth_test=bool(view.u8(base + 0x134)),
                depth_write=bool(view.u8(base + 0x135)),
                alpha_reference=view.u8(base + 0x131),
                alpha_function=view.u16(base + 0x132),
                depth_function=view.u16(base + 0x136),
                blend_mode=view.u32(base + 0x138),
                blend_src=view.u16(base + 0x13C),
                blend_dst=view.u16(base + 0x13E),
                blend_equation=view.u16(base + 0x140),
                color_blend_src=view.u16(base + 0x144),
                color_blend_dst=view.u16(base + 0x146),
                color_blend_equation=view.u16(base + 0x148),
                blend_color_alpha=view.f32(base + 0x158),
                texture_mappers_used=view.u32(base + 0x08),
                texture_coords_used=view.u32(base + 0x0C),
                texture_mappers=texture_mappers,
                texture_coords=texture_coords,
                lighting_block=parse_material_lighting_block(raw_material),
                raw_material=raw_material,
                fragment_lighting_enabled=bool(view.u8(base + 0x00)),
                vertex_lighting_enabled=bool(view.u8(base + 0x01)),
                hemisphere_lighting_enabled=bool(view.u8(base + 0x02)),
                hemisphere_occlusion_enabled=bool(view.u8(base + 0x03)),
            )
        )
    return materials


def material_lighting_block_summary(
    block: MaterialLightingBlock | None,
) -> dict[str, object] | None:
    if block is None:
        return None

    lut_input_abs_d0 = native_u16_field_summary(
        block,
        0x1C,
        block.pica_lut_input_abs_d0_raw,
        block.pica_lut_input_abs_d0_selector,
        block.pica_lut_input_abs_d0_selector_recognized,
        MATERIAL_LANE_COPIED_BLOCK_PICA_LUT_INPUT_ABS_D0_BYTE_OFFSET,
    )
    lut_input_abs_d0.update(
        {
            "semantic_resolved": True,
            "register": f"0x{PICA_LUT_INPUT_ABS_D0_REGISTER:03x}",
            "register_name": PICA_LUT_INPUT_ABS_D0_REGISTER_NAME,
            "field_name": PICA_LUT_INPUT_ABS_D0_FIELD_NAME,
            "sampler": PICA_LUT_INPUT_ABS_D0_SAMPLER_NAME,
            "bit_shift": PICA_LUT_INPUT_ABS_D0_BIT_SHIFT,
            "pica_disable_bit": block.pica_lut_input_abs_d0_disable_bit,
            "pica_disable_bit_resolved": block.pica_lut_input_abs_d0_disable_bit_resolved,
            "native_selector_disable_bit_by_decoded_value": PICA_LUT_INPUT_ABS_D0_DISABLE_BIT_BY_SELECTOR,
            "packing_rule": PICA_LUT_INPUT_ABS_D0_PACKING_RULE,
            "consumer_address": "0x0040d040",
            "consumer_source": "codebin_0040d040_lighting_lutinput_abs_emit",
        }
    )

    return {
        "source_offset": f"0x{block.source_offset:03x}",
        "size": block.size,
        "raw_sha256": hashlib.sha256(block.raw_block).hexdigest(),
        "pica_bump_texture_unit": native_u16_field_summary(
            block,
            0x10,
            block.pica_bump_texture_unit_raw,
            block.pica_bump_texture_unit,
            block.pica_bump_texture_unit_recognized,
            MATERIAL_LANE_COPIED_BLOCK_PICA_BUMP_TEXTURE_UNIT_BYTE_OFFSET,
        ),
        "pica_bump_mode": native_u16_field_summary(
            block,
            0x12,
            block.pica_bump_mode_raw,
            block.pica_bump_mode,
            block.pica_bump_mode_recognized,
            MATERIAL_LANE_COPIED_BLOCK_PICA_BUMP_MODE_BYTE_OFFSET,
        ),
        "pica_lighting_config": native_u16_field_summary(
            block,
            0x18,
            block.pica_lighting_config_raw,
            block.pica_lighting_config,
            block.pica_lighting_config_recognized,
            MATERIAL_LANE_COPIED_BLOCK_PICA_LIGHTING_CONFIG_BYTE_OFFSET,
        ),
        "pica_62c0_selector": native_u16_field_summary(
            block,
            0x1C,
            block.pica_62c0_selector_raw,
            block.pica_62c0_selector,
            block.pica_62c0_selector_recognized,
            MATERIAL_LANE_COPIED_BLOCK_PICA_62C0_SELECTOR_BYTE_OFFSET,
        ),
        "pica_lut_input_abs_d0": lut_input_abs_d0,
        "pica_lut_input_abs_sp": pica_lut_input_abs_flag_summary(
            block,
            0x14,
            block.flag1_raw,
            block.flag1,
            MATERIAL_LANE_COPIED_BLOCK_FLAG1_BYTE_OFFSET,
            "sp",
            9,
            block.pica_lut_input_abs_sp_disable_bit,
            block.pica_lut_input_abs_sp_disable_bit_resolved,
        ),
        "pica_lut_scale_sp": pica_lut_scale_flag_summary(
            block,
            0x1E,
            block.flag2_raw,
            block.flag2,
            MATERIAL_LANE_COPIED_BLOCK_FLAG2_BYTE_OFFSET,
            "sp",
            8,
            block.pica_lut_scale_sp,
            block.pica_lut_scale_sp_resolved,
        ),
        "pica_lut_input_fr": pica_lut_input_flag_summary(
            block,
            0x20,
            block.flag4_raw,
            block.flag4,
            MATERIAL_LANE_COPIED_BLOCK_FLAG4_BYTE_OFFSET,
            "fr",
            12,
            block.pica_lut_input_fr,
            block.pica_lut_input_fr_resolved,
        ),
        "pica_lut_input_abs_fr": pica_lut_input_abs_flag_summary(
            block,
            0x23,
            block.flag5_raw,
            block.flag5,
            MATERIAL_LANE_COPIED_BLOCK_FLAG5_BYTE_OFFSET,
            "fr",
            13,
            block.pica_lut_input_abs_fr_disable_bit,
            block.pica_lut_input_abs_fr_disable_bit_resolved,
        ),
        "pica_lut_input_abs_rb": pica_lut_input_abs_flag_summary(
            block,
            0x24,
            block.flag0_raw,
            block.flag0,
            MATERIAL_LANE_COPIED_BLOCK_FLAG0_BYTE_OFFSET,
            "rb",
            17,
            block.pica_lut_input_abs_rb_disable_bit,
            block.pica_lut_input_abs_rb_disable_bit_resolved,
        ),
        "unresolved_enum4": native_u16_field_summary(
            block,
            0x1C,
            block.unresolved_enum4_raw,
            block.unresolved_enum4_encoded,
            block.unresolved_enum4_recognized,
            MATERIAL_LANE_COPIED_BLOCK_PICA_62C0_SELECTOR_BYTE_OFFSET,
        ),
        "unresolved_enum4_semantic_resolved": True,
        "unresolved_enum4_semantic_alias": "pica_lut_input_abs_d0",
        "unresolved_enum4_runtime_lane_mapping_resolved": True,
        "pica_lut_input": native_u16_field_summary(
            block,
            0x26,
            block.pica_lut_input_raw,
            block.pica_lut_input,
            block.pica_lut_input_recognized,
            MATERIAL_LANE_COPIED_BLOCK_PICA_LUT_INPUT_BYTE_OFFSET,
        ),
        "pica_lut_input_rb": pica_lut_input_value_summary(
            block,
            0x26,
            block.pica_lut_input_raw,
            block.pica_lut_input_rb,
            block.pica_lut_input_rb_recognized,
            MATERIAL_LANE_COPIED_BLOCK_PICA_LUT_INPUT_BYTE_OFFSET,
            "rb",
            16,
        ),
        "pica_lut_scale": {
            "source_local_offset": "0x28",
            "material_offset": f"0x{block.source_offset + 0x28:03x}",
            "runtime_lane_byte_offset": f"0x{MATERIAL_LANE_COPIED_BLOCK_PICA_LUT_SCALE_BYTE_OFFSET:03x}",
            "runtime_subblock_byte_offset": f"0x{MATERIAL_LANE_COPIED_BLOCK_PICA_LUT_SCALE_BYTE_OFFSET - MATERIAL_LANE_COPIED_BLOCK_DESTINATION_OFFSET:03x}",
            "runtime_lane_populate_source": MATERIAL_LANE_POPULATE_SOURCE,
            "native_bits": f"0x{block.pica_lut_scale_source_bits:08x}",
            "native_value": block.pica_lut_scale_source_value,
            "decoded": block.pica_lut_scale,
            "recognized": block.pica_lut_scale_recognized,
        },
        "pica_lut_scale_rb": pica_lut_scale_value_summary(
            block,
            block.pica_lut_scale_source_bits,
            block.pica_lut_scale_source_value,
            block.pica_lut_scale_rb,
            block.pica_lut_scale_rb_recognized,
            "rb",
            16,
        ),
        "flags": {
            "flag0": native_flag_field_summary(
                block, 0x24, block.flag0_raw, block.flag0, MATERIAL_LANE_COPIED_BLOCK_FLAG0_BYTE_OFFSET
            ),
            "flag1": native_flag_field_summary(
                block, 0x14, block.flag1_raw, block.flag1, MATERIAL_LANE_COPIED_BLOCK_FLAG1_BYTE_OFFSET
            ),
            "flag2": native_flag_field_summary(
                block, 0x1E, block.flag2_raw, block.flag2, MATERIAL_LANE_COPIED_BLOCK_FLAG2_BYTE_OFFSET
            ),
            "flag3": native_flag_field_summary(
                block, 0x1F, block.flag3_raw, block.flag3, MATERIAL_LANE_COPIED_BLOCK_FLAG3_BYTE_OFFSET
            ),
            "flag4": native_flag_field_summary(
                block, 0x20, block.flag4_raw, block.flag4, MATERIAL_LANE_COPIED_BLOCK_FLAG4_BYTE_OFFSET
            ),
            "flag5": runtime_payload3_scale_gate_flag_summary(block),
        },
        "flag_semantics_resolved": False,
        "flag_semantics_partially_resolved": True,
        "resolved_flag_semantics": {
            "flag0": "pica_lut_input_abs_rb_disable_bit",
            "flag1": "pica_lut_input_abs_sp_disable_bit",
            "flag2": "pica_lut_scale_sp",
            "flag4": "pica_lut_input_fr",
            "flag5": [
                "pica_lut_input_abs_fr_disable_bit",
                "runtime_payload3_material_scale_gate",
            ],
        },
        "flag_runtime_lane_mapping_resolved": True,
        "flag_runtime_lane_mapping_source": MATERIAL_LANE_POPULATE_SOURCE,
    }


def native_u16_field_summary(
    block: MaterialLightingBlock,
    local_offset: int,
    native_value: int,
    decoded: int,
    recognized: bool,
    runtime_lane_byte_offset: int | None = None,
) -> dict[str, object]:
    summary: dict[str, object] = {
        "source_local_offset": f"0x{local_offset:02x}",
        "material_offset": f"0x{block.source_offset + local_offset:03x}",
        "native": f"0x{native_value:04x}",
        "decoded": decoded,
        "recognized": recognized,
    }
    if runtime_lane_byte_offset is not None:
        summary["runtime_lane_byte_offset"] = f"0x{runtime_lane_byte_offset:03x}"
        summary["runtime_subblock_byte_offset"] = (
            f"0x{runtime_lane_byte_offset - MATERIAL_LANE_COPIED_BLOCK_DESTINATION_OFFSET:03x}"
        )
        summary["runtime_lane_populate_source"] = MATERIAL_LANE_POPULATE_SOURCE
    return summary


def native_flag_field_summary(
    block: MaterialLightingBlock,
    local_offset: int,
    raw_value: int,
    enabled: bool,
    runtime_lane_byte_offset: int,
) -> dict[str, object]:
    return {
        "source_local_offset": f"0x{local_offset:02x}",
        "material_offset": f"0x{block.source_offset + local_offset:03x}",
        "runtime_lane_byte_offset": f"0x{runtime_lane_byte_offset:03x}",
        "runtime_subblock_byte_offset": (
            f"0x{runtime_lane_byte_offset - MATERIAL_LANE_COPIED_BLOCK_DESTINATION_OFFSET:03x}"
        ),
        "runtime_lane_populate_source": MATERIAL_LANE_POPULATE_SOURCE,
        "raw": raw_value,
        "enabled": enabled,
    }


def pica_lut_input_abs_flag_summary(
    block: MaterialLightingBlock,
    local_offset: int,
    raw_value: int,
    enabled: bool,
    runtime_lane_byte_offset: int,
    sampler: str,
    bit_shift: int,
    disable_bit: int,
    resolved: bool,
) -> dict[str, object]:
    summary = native_flag_field_summary(
        block, local_offset, raw_value, enabled, runtime_lane_byte_offset
    )
    summary.update(
        {
            "semantic_resolved": resolved,
            "register": f"0x{PICA_LUT_INPUT_ABS_D0_REGISTER:03x}",
            "register_name": PICA_LUT_INPUT_ABS_D0_REGISTER_NAME,
            "field_name": f"disable_{sampler}",
            "sampler": sampler,
            "bit_shift": bit_shift,
            "pica_disable_bit": disable_bit,
            "pica_disable_bit_resolved": resolved,
            "packing_rule": "flag ? 0 : 1",
            "consumer_address": "0x0040d040",
            "consumer_source": "codebin_0040d040_lighting_lutinput_abs_emit",
        }
    )
    return summary


def pica_lut_scale_flag_summary(
    block: MaterialLightingBlock,
    local_offset: int,
    raw_value: int,
    enabled: bool,
    runtime_lane_byte_offset: int,
    sampler: str,
    bit_shift: int,
    decoded_scale: int,
    resolved: bool,
) -> dict[str, object]:
    summary = native_flag_field_summary(
        block, local_offset, raw_value, enabled, runtime_lane_byte_offset
    )
    summary.update(
        {
            "semantic_resolved": resolved,
            "register": "0x1d2",
            "register_name": "GPUREG_LIGHTING_LUTINPUT_SCALE",
            "field_name": sampler,
            "sampler": sampler,
            "bit_shift": bit_shift,
            "decoded_lighting_scale": decoded_scale,
            "decoded_lighting_scale_resolved": resolved,
            "native_flag_scale_by_value": [0, 1],
            "consumer_address": "0x0040d040",
            "consumer_source": "codebin_0040d040_lighting_lutinput_scale_emit",
        }
    )
    return summary


def pica_lut_input_flag_summary(
    block: MaterialLightingBlock,
    local_offset: int,
    raw_value: int,
    enabled: bool,
    runtime_lane_byte_offset: int,
    sampler: str,
    bit_shift: int,
    decoded_input: int,
    resolved: bool,
) -> dict[str, object]:
    summary = native_flag_field_summary(
        block, local_offset, raw_value, enabled, runtime_lane_byte_offset
    )
    summary.update(
        {
            "semantic_resolved": resolved,
            "register": "0x1d1",
            "register_name": "GPUREG_LIGHTING_LUTINPUT_SELECT",
            "field_name": sampler,
            "sampler": sampler,
            "bit_shift": bit_shift,
            "decoded_lighting_lut_input": decoded_input,
            "decoded_lighting_lut_input_resolved": resolved,
            "native_flag_lut_input_by_value": [0, 1],
            "consumer_address": "0x0040d040",
            "consumer_source": "codebin_0040d040_lighting_lutinput_select_emit",
        }
    )
    return summary


def pica_lut_input_value_summary(
    block: MaterialLightingBlock,
    local_offset: int,
    raw_value: int,
    decoded_input: int,
    recognized: bool,
    runtime_lane_byte_offset: int,
    sampler: str,
    bit_shift: int,
) -> dict[str, object]:
    summary = native_u16_field_summary(
        block,
        local_offset,
        raw_value,
        decoded_input,
        recognized,
        runtime_lane_byte_offset,
    )
    summary.update(
        {
            "semantic_resolved": recognized,
            "register": "0x1d1",
            "register_name": "GPUREG_LIGHTING_LUTINPUT_SELECT",
            "field_name": sampler,
            "sampler": sampler,
            "bit_shift": bit_shift,
            "decoded_lighting_lut_input": decoded_input,
            "decoded_lighting_lut_input_resolved": recognized,
            "consumer_address": "0x0040d040",
            "consumer_source": "codebin_0040d040_lighting_lutinput_select_emit",
        }
    )
    return summary


def pica_lut_scale_value_summary(
    block: MaterialLightingBlock,
    raw_bits: int,
    source_value: float,
    decoded_scale: int,
    recognized: bool,
    sampler: str,
    bit_shift: int,
) -> dict[str, object]:
    return {
        "source_local_offset": "0x28",
        "material_offset": f"0x{block.source_offset + 0x28:03x}",
        "runtime_lane_byte_offset": f"0x{MATERIAL_LANE_COPIED_BLOCK_PICA_LUT_SCALE_BYTE_OFFSET:03x}",
        "runtime_subblock_byte_offset": f"0x{MATERIAL_LANE_COPIED_BLOCK_PICA_LUT_SCALE_BYTE_OFFSET - MATERIAL_LANE_COPIED_BLOCK_DESTINATION_OFFSET:03x}",
        "runtime_lane_populate_source": "codebin_004c6364_material_lighting_block_copy",
        "native_bits": f"0x{raw_bits:08x}",
        "native_value": source_value,
        "decoded": decoded_scale,
        "recognized": recognized,
        "semantic_resolved": recognized,
        "register": "0x1d2",
        "register_name": "GPUREG_LIGHTING_LUTINPUT_SCALE",
        "field_name": sampler,
        "sampler": sampler,
        "bit_shift": bit_shift,
        "decoded_lighting_scale": decoded_scale,
        "decoded_lighting_scale_resolved": recognized,
        "consumer_address": "0x0040d040",
        "consumer_source": "codebin_0040d040_lighting_lutinput_scale_emit",
    }


def runtime_payload3_scale_gate_flag_summary(block: MaterialLightingBlock) -> dict[str, object]:
    summary = native_flag_field_summary(
        block,
        0x23,
        block.flag5_raw,
        block.flag5,
        MATERIAL_LANE_COPIED_BLOCK_FLAG5_BYTE_OFFSET,
    )
    summary.update(
        {
            "semantic_resolved": True,
            "semantic": RUNTIME_PAYLOAD3_SCALE_GATE_SEMANTIC,
            "consumer_address": f"0x{RUNTIME_LIGHT_PACKET_PACK_ADDRESS:08x}",
            "final_upload_helper_address": f"0x{RUNTIME_LIGHT_PACKET_PACK_FINAL_UPLOAD_ADDRESS:08x}",
            "descriptor_payload_3_scale_offsets": RUNTIME_PAYLOAD3_DESCRIPTOR_SCALE_OFFSETS,
            "behavior_when_disabled": "payload3_rgb_scaled_by_material_descriptor_offsets_0x0b0_0x0b2",
            "behavior_when_enabled": "payload3_rgb_uses_runtime_light_values_without_payload3_material_scale",
            "source": "codebin_003fa5d0_runtime_light_packet_pack",
        }
    )
    return summary


def parse_material_lighting_block(
    raw_material: bytes,
    source_offset: int = OOT3D_MATERIAL_LIGHTING_BLOCK_OFFSET,
) -> MaterialLightingBlock | None:
    if len(raw_material) < source_offset + OOT3D_MATERIAL_LIGHTING_BLOCK_MIN_SIZE:
        return None

    view = BinaryView(raw_material, "cmb material")
    bump_texture_unit_raw = view.u16(source_offset + 0x10)
    bump_texture_unit, bump_texture_unit_recognized = decode_linear_native_enum(
        bump_texture_unit_raw,
        PICA_BUMP_TEXTURE_UNIT_NATIVE_BASE,
        PICA_BUMP_TEXTURE_UNIT_NATIVE_COUNT,
    )
    bump_mode_raw = view.u16(source_offset + 0x12)
    bump_mode, bump_mode_recognized = decode_linear_native_enum(
        bump_mode_raw,
        PICA_BUMP_MODE_NATIVE_BASE,
        PICA_BUMP_MODE_NATIVE_COUNT,
    )
    lighting_config_raw = view.u16(source_offset + 0x18)
    lighting_config, lighting_config_recognized = decode_pica_lighting_config(
        lighting_config_raw
    )
    unresolved_enum4_raw = view.u16(source_offset + 0x1C)
    pica_62c0_selector, pica_62c0_selector_recognized = decode_linear_native_enum(
        unresolved_enum4_raw,
        PICA_62C0_SELECTOR_NATIVE_BASE,
        PICA_62C0_SELECTOR_NATIVE_COUNT,
    )
    lut_input_raw = view.u16(source_offset + 0x26)
    lut_input, lut_input_recognized = decode_linear_native_enum(
        lut_input_raw,
        PICA_LIGHTING_LUT_INPUT_NATIVE_BASE,
        PICA_LIGHTING_LUT_INPUT_NATIVE_COUNT,
    )
    lut_scale_bits = view.u32(source_offset + 0x28)
    lut_scale, lut_scale_recognized = decode_pica_lut_scale(lut_scale_bits)
    flag0_raw = view.u8(source_offset + 0x24)
    flag1_raw = view.u8(source_offset + 0x14)
    flag2_raw = view.u8(source_offset + 0x1E)
    flag3_raw = view.u8(source_offset + 0x1F)
    flag4_raw = view.u8(source_offset + 0x20)
    flag5_raw = view.u8(source_offset + 0x23)

    return MaterialLightingBlock(
        source_offset=source_offset,
        size=OOT3D_MATERIAL_LIGHTING_BLOCK_MIN_SIZE,
        raw_block=view.bytes(source_offset, OOT3D_MATERIAL_LIGHTING_BLOCK_MIN_SIZE),
        pica_bump_texture_unit_raw=bump_texture_unit_raw,
        pica_bump_texture_unit=bump_texture_unit,
        pica_bump_texture_unit_recognized=bump_texture_unit_recognized,
        pica_bump_mode_raw=bump_mode_raw,
        pica_bump_mode=bump_mode,
        pica_bump_mode_recognized=bump_mode_recognized,
        flag1_raw=flag1_raw,
        flag1=bool(flag1_raw),
        pica_lighting_config_raw=lighting_config_raw,
        pica_lighting_config=lighting_config,
        pica_lighting_config_recognized=lighting_config_recognized,
        unresolved_enum4_raw=unresolved_enum4_raw,
        unresolved_enum4_encoded=pica_62c0_selector,
        unresolved_enum4_recognized=pica_62c0_selector_recognized,
        pica_62c0_selector_raw=unresolved_enum4_raw,
        pica_62c0_selector=pica_62c0_selector,
        pica_62c0_selector_recognized=pica_62c0_selector_recognized,
        pica_lut_input_abs_d0_raw=unresolved_enum4_raw,
        pica_lut_input_abs_d0_selector=pica_62c0_selector,
        pica_lut_input_abs_d0_selector_recognized=pica_62c0_selector_recognized,
        pica_lut_input_abs_d0_disable_bit=decode_pica_lut_input_abs_disable_bit(
            pica_62c0_selector
        ),
        pica_lut_input_abs_d0_disable_bit_resolved=pica_62c0_selector_recognized,
        flag2_raw=flag2_raw,
        flag2=bool(flag2_raw),
        pica_lut_input_abs_sp_disable_bit=decode_pica_lut_input_abs_disable_bit(
            1 if flag1_raw else 0
        ),
        pica_lut_input_abs_sp_disable_bit_resolved=True,
        pica_lut_scale_sp=1 if flag2_raw else 0,
        pica_lut_scale_sp_resolved=True,
        flag3_raw=flag3_raw,
        flag3=bool(flag3_raw),
        flag4_raw=flag4_raw,
        flag4=bool(flag4_raw),
        pica_lut_input_fr=1 if flag4_raw else 0,
        pica_lut_input_fr_resolved=True,
        flag5_raw=flag5_raw,
        flag5=bool(flag5_raw),
        pica_lut_input_abs_fr_disable_bit=decode_pica_lut_input_abs_disable_bit(
            1 if flag5_raw else 0
        ),
        pica_lut_input_abs_fr_disable_bit_resolved=True,
        flag0_raw=flag0_raw,
        flag0=bool(flag0_raw),
        pica_lut_input_abs_rb_disable_bit=decode_pica_lut_input_abs_disable_bit(
            1 if flag0_raw else 0
        ),
        pica_lut_input_abs_rb_disable_bit_resolved=True,
        pica_lut_input_raw=lut_input_raw,
        pica_lut_input=lut_input,
        pica_lut_input_recognized=lut_input_recognized,
        pica_lut_input_rb=lut_input,
        pica_lut_input_rb_recognized=lut_input_recognized,
        pica_lut_scale_source_bits=lut_scale_bits,
        pica_lut_scale_source_value=view.f32(source_offset + 0x28),
        pica_lut_scale=lut_scale,
        pica_lut_scale_recognized=lut_scale_recognized,
        pica_lut_scale_rb=lut_scale,
        pica_lut_scale_rb_recognized=lut_scale_recognized,
    )


def decode_linear_native_enum(value: int, base: int, count: int) -> tuple[int, bool]:
    if base <= value < base + count:
        return value - base, True
    return 0, False


def decode_pica_lut_input_abs_disable_bit(selector: int) -> int:
    return 1 - selector if selector <= 1 else 0


def decode_pica_lighting_config(value: int) -> tuple[int, bool]:
    decoded, recognized = decode_linear_native_enum(
        value,
        PICA_LIGHTING_CONFIG_NATIVE_BASE,
        PICA_LIGHTING_CONFIG_NATIVE_COUNT,
    )
    if recognized:
        return decoded, True
    if value == PICA_LIGHTING_CONFIG_7_NATIVE:
        return PICA_LIGHTING_CONFIG_7_VALUE, True
    return 0, False


def decode_pica_lut_scale(bits: int) -> tuple[int, bool]:
    if bits in PICA_LIGHTING_LUT_SCALE_BY_NATIVE_BITS:
        return PICA_LIGHTING_LUT_SCALE_BY_NATIVE_BITS[bits], True
    return 0, False


def read_material_texture(view: BinaryView, offset: int) -> MaterialTexture:
    return MaterialTexture(
        index=view.s16(offset),
        min_filter=view.u16(offset + 0x04),
        mag_filter=view.u16(offset + 0x06),
        wrap_s=view.u16(offset + 0x08),
        wrap_t=view.u16(offset + 0x0A),
    )


def read_texture_coord(view: BinaryView, offset: int) -> TextureCoord:
    return TextureCoord(
        matrix_mode=view.u8(offset),
        reference_camera=view.u8(offset + 0x01),
        mapping_method=view.u8(offset + 0x02),
        coordinate_index=view.u8(offset + 0x03),
        scale=Vec2(view.f32(offset + 0x04), view.f32(offset + 0x08)),
        rotation=view.f32(offset + 0x0C),
        translation=Vec2(view.f32(offset + 0x10), view.f32(offset + 0x14)),
    )


def parse_sklm(
    view: BinaryView,
    sklm_off: int,
    vatr_off: int,
    indices_off: int,
    index_count: int,
) -> tuple[list[Mesh], list[Shape]]:
    if view.bytes(sklm_off, 4) != b"sklm":
        raise ParseError(f"{view.source}: expected SKLM chunk at 0x{sklm_off:x}")
    if view.bytes(vatr_off, 4) != b"vatr":
        raise ParseError(f"{view.source}: expected VATR chunk at 0x{vatr_off:x}")

    mshs_off = sklm_off + view.u32(sklm_off + 0x08)
    shp_off = sklm_off + view.u32(sklm_off + 0x0C)
    meshes = parse_meshes(view, mshs_off)
    shapes = parse_shapes(view, shp_off, vatr_off, indices_off, index_count)
    return meshes, shapes


def parse_meshes(view: BinaryView, mshs_off: int) -> list[Mesh]:
    if view.bytes(mshs_off, 4) != b"mshs":
        raise ParseError(f"{view.source}: expected MSHS chunk at 0x{mshs_off:x}")

    mesh_count = view.u32(mshs_off + 0x08)
    meshes: list[Mesh] = []
    for index in range(mesh_count):
        base = mshs_off + 0x10 + index * 4
        meshes.append(
            Mesh(
                index=index,
                shape_index=view.u16(base),
                material_index=view.u8(base + 0x02),
                visibility_id=view.u8(base + 0x03),
            )
        )
    return meshes


def parse_shapes(
    view: BinaryView,
    shp_off: int,
    vatr_off: int,
    indices_off: int,
    index_count: int,
) -> list[Shape]:
    if view.bytes(shp_off, 4) != b"shp ":
        raise ParseError(f"{view.source}: expected SHP chunk at 0x{shp_off:x}")

    shape_count = view.u32(shp_off + 0x08)
    all_indices = read_indices(view, indices_off, index_count, PICA_U16)
    vatr_data = {
        "position": parse_vld(view, vatr_off + 0x0C),
        "normal": parse_vld(view, vatr_off + 0x14),
        "color": parse_vld(view, vatr_off + 0x1C),
        "uv0": parse_vld(view, vatr_off + 0x24),
    }

    shapes: list[Shape] = []
    for index in range(shape_count):
        sepd_off = shp_off + view.u16(shp_off + 0x10 + index * 2)
        if view.bytes(sepd_off, 4) != b"sepd":
            raise ParseError(f"{view.source}: expected SEPD chunk at 0x{sepd_off:x}")

        prms_count = view.u16(sepd_off + 0x08)
        flags = view.u16(sepd_off + 0x0A)
        auto_flags = view.u16(sepd_off + 0x106)
        vertex_lists = {
            "position": parse_vertex_list(view, sepd_off + 0x24),
            "normal": parse_vertex_list(view, sepd_off + 0x40),
            "color": parse_vertex_list(view, sepd_off + 0x5C),
            "uv0": parse_vertex_list(view, sepd_off + 0x78),
        }

        primitives: list[Primitive] = []
        max_index = 0
        for prms_index in range(prms_count):
            prms_off = sepd_off + view.u16(sepd_off + 0x108 + prms_index * 2)
            primitive = parse_prms(view, prms_off, all_indices, indices_off)
            if primitive.indices:
                max_index = max(max_index, max(primitive.indices))
            primitives.append(primitive)

        vertex_count = max_index + 1 if primitives else 0
        positions = read_vec3_attribute(
            view,
            vatr_off,
            vertex_lists["position"],
            vatr_data["position"],
            vertex_count,
            enabled=bool(flags & ATTR_POSITION),
            constant=bool(auto_flags & ATTR_POSITION),
        )
        normals = read_vec3_attribute(
            view,
            vatr_off,
            vertex_lists["normal"],
            vatr_data["normal"],
            vertex_count,
            enabled=bool(flags & ATTR_NORMAL),
            constant=bool(auto_flags & ATTR_NORMAL),
            default=Vec3(0.0, 0.0, 1.0),
        )
        colors = read_color_attribute(
            view,
            vatr_off,
            vertex_lists["color"],
            vatr_data["color"],
            vertex_count,
            enabled=bool(flags & ATTR_COLOR),
            constant=bool(auto_flags & ATTR_COLOR),
        )
        uv0 = read_vec2_attribute(
            view,
            vatr_off,
            vertex_lists["uv0"],
            vatr_data["uv0"],
            vertex_count,
            enabled=bool(flags & ATTR_UV0),
            constant=bool(auto_flags & ATTR_UV0),
            default=Vec2(0.0, 0.0),
        )

        shapes.append(
            Shape(
                index=index,
                flags=flags,
                auto_flags=auto_flags,
                positions=tuple(positions),
                normals=tuple(normals),
                colors=tuple(colors),
                uv0=tuple(uv0),
                primitives=tuple(primitives),
            )
        )

    return shapes


def parse_prms(
    view: BinaryView,
    prms_off: int,
    all_u16_indices: tuple[int, ...],
    indices_off: int,
) -> Primitive:
    if view.bytes(prms_off, 4) != b"prms":
        raise ParseError(f"{view.source}: expected PRMS chunk at 0x{prms_off:x}")

    prm_count = view.u32(prms_off + 0x08)
    if prm_count != 1:
        raise ParseError(f"{view.source}: PRMS at 0x{prms_off:x} has {prm_count} PRM chunks")

    skinning_mode = view.u16(prms_off + 0x0C)
    bone_count = view.u16(prms_off + 0x0E)
    bone_off = prms_off + view.u32(prms_off + 0x10)
    prm_off = prms_off + view.u32(prms_off + 0x14)
    bone_indices = tuple(view.u16(bone_off + index * 2) for index in range(bone_count))

    if view.bytes(prm_off, 4) != b"prm ":
        raise ParseError(f"{view.source}: expected PRM chunk at 0x{prm_off:x}")
    visible = view.u32(prm_off + 0x08)
    primitive_mode = view.u32(prm_off + 0x0C)
    data_type = view.u16(prm_off + 0x10)
    count = view.u16(prm_off + 0x14)
    first = view.u16(prm_off + 0x16)

    if not visible:
        return Primitive(indices=(), skinning_mode=skinning_mode, bone_indices=bone_indices)
    if primitive_mode != 0:
        raise ParseError(
            f"{view.source}: only triangle primitive mode 0 is supported, got 0x{primitive_mode:x}"
        )
    if count % 3 != 0:
        raise ParseError(f"{view.source}: PRM at 0x{prm_off:x} index count is not divisible by 3")

    if data_type == PICA_U16:
        indices = all_u16_indices[first : first + count]
    else:
        byte_offset = indices_off + first * data_type_size(data_type)
        indices = read_indices(view, byte_offset, count, data_type)
    return Primitive(indices=tuple(indices), skinning_mode=skinning_mode, bone_indices=bone_indices)


def parse_vld(view: BinaryView, offset: int) -> VertexListData:
    return VertexListData(length=view.u32(offset), offset=view.u32(offset + 4))


def parse_vertex_list(view: BinaryView, offset: int) -> VertexList:
    return VertexList(
        offset=view.u32(offset),
        scale=view.f32(offset + 4),
        data_type=view.u16(offset + 8),
        mode=view.u16(offset + 0x0A),
        constant=tuple(view.f32(offset + 0x0C + i * 4) for i in range(4)),  # type: ignore[arg-type]
    )


def read_indices(view: BinaryView, offset: int, count: int, data_type: int) -> tuple[int, ...]:
    readers = {
        PICA_U8: view.u8,
        PICA_U16: view.u16,
        PICA_U32: view.u32,
    }
    if data_type not in readers:
        raise ParseError(f"{view.source}: unsupported index data type 0x{data_type:x}")
    size = data_type_size(data_type)
    reader = readers[data_type]
    return tuple(reader(offset + index * size) for index in range(count))


def read_vec3_attribute(
    view: BinaryView,
    vatr_off: int,
    vertex_list: VertexList,
    vld: VertexListData,
    count: int,
    *,
    enabled: bool,
    constant: bool = False,
    default: Vec3 | None = None,
) -> list[Vec3]:
    if count == 0:
        return []
    if not enabled:
        return [default or Vec3(0.0, 0.0, 0.0)] * count
    if constant or vertex_list.mode != 0:
        x, y, z, _ = vertex_list.constant
        return [Vec3(x, y, z)] * count
    if vld.length == 0:
        if enabled:
            raise ParseError(f"{view.source}: required vec3 attribute has no VATR data")
        return [default or Vec3(0.0, 0.0, 0.0)] * count
    values = read_attribute_values(view, vatr_off, vertex_list, vld, count, 3)
    return [Vec3(row[0], row[1], row[2]) for row in values]


def read_vec2_attribute(
    view: BinaryView,
    vatr_off: int,
    vertex_list: VertexList,
    vld: VertexListData,
    count: int,
    *,
    enabled: bool,
    constant: bool = False,
    default: Vec2 | None = None,
) -> list[Vec2]:
    if count == 0:
        return []
    if not enabled:
        return [default or Vec2(0.0, 0.0)] * count
    if constant or vertex_list.mode != 0:
        x, y, _, _ = vertex_list.constant
        return [Vec2(x, y)] * count
    if vld.length == 0:
        if enabled:
            raise ParseError(f"{view.source}: required vec2 attribute has no VATR data")
        return [default or Vec2(0.0, 0.0)] * count
    values = read_attribute_values(view, vatr_off, vertex_list, vld, count, 2)
    return [Vec2(row[0], row[1]) for row in values]


def read_color_attribute(
    view: BinaryView,
    vatr_off: int,
    vertex_list: VertexList,
    vld: VertexListData,
    count: int,
    *,
    enabled: bool,
    constant: bool = False,
) -> list[Color]:
    if count == 0:
        return []
    if not enabled:
        return [Color(255, 255, 255, 255)] * count
    if constant or vertex_list.mode != 0:
        r, g, b, a = vertex_list.constant
        return [Color(to_u8(r), to_u8(g), to_u8(b), to_u8(a))] * count
    if vld.length == 0:
        return [Color(255, 255, 255, 255)] * count
    values = read_attribute_values(view, vatr_off, vertex_list, vld, count, 4)
    return [Color(to_u8(row[0]), to_u8(row[1]), to_u8(row[2]), to_u8(row[3])) for row in values]


def read_attribute_values(
    view: BinaryView,
    vatr_off: int,
    vertex_list: VertexList,
    vld: VertexListData,
    count: int,
    components: int,
) -> list[tuple[float, ...]]:
    size = data_type_size(vertex_list.data_type)
    start = vatr_off + vld.offset + vertex_list.offset
    needed = count * components * size
    if vertex_list.offset + needed > vld.length:
        raise ParseError(
            f"{view.source}: vertex attribute needs 0x{needed:x} bytes at relative "
            f"0x{vertex_list.offset:x}, but VLD length is 0x{vld.length:x}"
        )

    rows: list[tuple[float, ...]] = []
    for item_index in range(count):
        row: list[float] = []
        for component_index in range(components):
            offset = start + (item_index * components + component_index) * size
            row.append(read_scalar(view, offset, vertex_list.data_type) * vertex_list.scale)
        rows.append(tuple(row))
    return rows


def data_type_size(data_type: int) -> int:
    sizes = {
        PICA_S8: 1,
        PICA_U8: 1,
        PICA_S16: 2,
        PICA_U16: 2,
        PICA_S32: 4,
        PICA_U32: 4,
        PICA_F32: 4,
    }
    try:
        return sizes[data_type]
    except KeyError as exc:
        raise ParseError(f"unsupported PICA data type 0x{data_type:x}") from exc


def read_scalar(view: BinaryView, offset: int, data_type: int) -> float:
    if data_type == PICA_S8:
        return float(view.s8(offset))
    if data_type == PICA_U8:
        return float(view.u8(offset))
    if data_type == PICA_S16:
        return float(view.s16(offset))
    if data_type == PICA_U16:
        return float(view.u16(offset))
    if data_type == PICA_S32:
        return float(view.s32(offset))
    if data_type == PICA_U32:
        return float(view.u32(offset))
    if data_type == PICA_F32:
        return view.f32(offset)
    raise ParseError(f"{view.source}: unsupported scalar data type 0x{data_type:x}")


def decode_rgb565_to_rgba16(data: bytes, pixel_count: int) -> bytes:
    out = bytearray()
    for value in iter_u16(data, pixel_count):
        r5 = (value >> 11) & 0x1F
        g6 = (value >> 5) & 0x3F
        b5 = value & 0x1F
        g5 = g6 >> 1
        out.extend((((r5 << 11) | (g5 << 6) | (b5 << 1) | 1).to_bytes(2, "big")))
    return bytes(out)


def detile_ctr_texture(data: bytes, width: int, height: int, bytes_per_pixel: int) -> bytes:
    tile = 8
    aligned_width = max(tile, math.ceil(width / tile) * tile)
    aligned_height = max(tile, math.ceil(height / tile) * tile)
    out = bytearray(width * height * bytes_per_pixel)

    for y in range(height):
        for x in range(width):
            tile_x = x // tile
            tile_y = y // tile
            in_tile_x = x % tile
            in_tile_y = y % tile
            tile_index = tile_y * (aligned_width // tile) + tile_x
            src_pixel = tile_index * 64 + morton8(in_tile_x, in_tile_y)
            src = src_pixel * bytes_per_pixel
            dst = (y * width + x) * bytes_per_pixel
            if src + bytes_per_pixel > len(data):
                raise ParseError("tiled texture data is truncated")
            out[dst : dst + bytes_per_pixel] = data[src : src + bytes_per_pixel]
    return bytes(out)


def detile_ctr_texture_4bpp(data: bytes, width: int, height: int) -> bytes:
    tile = 8
    aligned_width = max(tile, math.ceil(width / tile) * tile)
    aligned_height = max(tile, math.ceil(height / tile) * tile)
    out = bytearray(width * height)

    for y in range(height):
        for x in range(width):
            tile_x = x // tile
            tile_y = y // tile
            in_tile_x = x % tile
            in_tile_y = y % tile
            tile_index = tile_y * (aligned_width // tile) + tile_x
            src_pixel = tile_index * 64 + morton8(in_tile_x, in_tile_y)
            src = src_pixel // 2
            if src >= len(data):
                raise ParseError("tiled 4bpp texture data is truncated")
            value = data[src]
            if src_pixel & 1:
                value >>= 4
            else:
                value &= 0xF
            out[y * width + x] = value
    return bytes(out)


def morton8(x: int, y: int) -> int:
    return (
        ((x & 0x1) << 0)
        | ((y & 0x1) << 1)
        | ((x & 0x2) << 1)
        | ((y & 0x2) << 2)
        | ((x & 0x4) << 2)
        | ((y & 0x4) << 3)
    )


def decode_rgba4_to_rgba16(data: bytes, pixel_count: int) -> bytes:
    out = bytearray()
    for value in iter_u16(data, pixel_count):
        r5 = expand_4_to_5((value >> 12) & 0xF)
        g5 = expand_4_to_5((value >> 8) & 0xF)
        b5 = expand_4_to_5((value >> 4) & 0xF)
        a1 = 1 if (value & 0xF) >= 8 else 0
        out.extend((((r5 << 11) | (g5 << 6) | (b5 << 1) | a1).to_bytes(2, "big")))
    return bytes(out)


def decode_rgba8_to_rgba16(data: bytes, pixel_count: int) -> bytes:
    if len(data) < pixel_count * 4:
        raise ParseError("RGBA8 texture data is truncated")
    out = bytearray()
    for offset in range(0, pixel_count * 4, 4):
        r, g, b, a = data[offset : offset + 4]
        out.extend(rgba8888_to_rgba16(r, g, b, a))
    return bytes(out)


def decode_rgb8_to_rgba16(data: bytes, pixel_count: int) -> bytes:
    if len(data) < pixel_count * 3:
        raise ParseError("RGB8 texture data is truncated")
    out = bytearray()
    for offset in range(0, pixel_count * 3, 3):
        r, g, b = data[offset : offset + 3]
        out.extend(rgba8888_to_rgba16(r, g, b, 255))
    return bytes(out)


def decode_a8_to_rgba16(data: bytes, pixel_count: int) -> bytes:
    if len(data) < pixel_count:
        raise ParseError("A8 texture data is truncated")
    out = bytearray()
    for alpha in data[:pixel_count]:
        out.extend(rgba8888_to_rgba16(255, 255, 255, alpha))
    return bytes(out)


def decode_l8_to_rgba16(data: bytes, pixel_count: int) -> bytes:
    if len(data) < pixel_count:
        raise ParseError("L8 texture data is truncated")
    out = bytearray()
    for value in data[:pixel_count]:
        out.extend(rgba8888_to_rgba16(value, value, value, 255))
    return bytes(out)


def decode_l4_to_rgba16(data: bytes, pixel_count: int) -> bytes:
    if len(data) < pixel_count:
        raise ParseError("L4 texture data is truncated")
    out = bytearray()
    for value4 in data[:pixel_count]:
        value = (value4 & 0xF) * 17
        out.extend(rgba8888_to_rgba16(value, value, value, 255))
    return bytes(out)


def decode_la8_to_rgba16(data: bytes, pixel_count: int) -> bytes:
    if len(data) < pixel_count * 2:
        raise ParseError("LA8 texture data is truncated")
    out = bytearray()
    for offset in range(0, pixel_count * 2, 2):
        alpha, luminance = data[offset : offset + 2]
        out.extend(rgba8888_to_rgba16(luminance, luminance, luminance, alpha))
    return bytes(out)


def decode_la4_to_rgba16(data: bytes, pixel_count: int) -> bytes:
    if len(data) < pixel_count:
        raise ParseError("LA4 texture data is truncated")
    out = bytearray()
    for value in data[:pixel_count]:
        luminance = expand_4_to_8(value >> 4)
        alpha = expand_4_to_8(value & 0xF)
        out.extend(rgba8888_to_rgba16(luminance, luminance, luminance, alpha))
    return bytes(out)


ETC1_MODIFIER_TABLE: tuple[tuple[int, int], ...] = (
    (2, 8),
    (5, 17),
    (9, 29),
    (13, 42),
    (18, 60),
    (24, 80),
    (33, 106),
    (47, 183),
)


def decode_etc1_texture_to_rgba16(data: bytes, width: int, height: int, *, has_alpha: bool) -> bytes:
    subtile_size = 16 if has_alpha else 8
    tile_size = subtile_size * 4
    tiles_x = max(1, math.ceil(width / 8))
    out = bytearray(width * height * 2)

    for y in range(height):
        for x in range(width):
            tile_x = x // 8
            tile_y = y // 8
            fine_x = x % 8
            fine_y = y % 8
            tile_base = (tile_y * tiles_x + tile_x) * tile_size
            subtile_index = (fine_x // 4) + 2 * (fine_y // 4)
            subtile_base = tile_base + subtile_index * subtile_size
            local_x = fine_x % 4
            local_y = fine_y % 4

            if subtile_base + subtile_size > len(data):
                raise ParseError("ETC1 texture data is truncated")

            alpha = 255
            etc_base = subtile_base
            if has_alpha:
                packed_alpha = int.from_bytes(data[subtile_base : subtile_base + 8], "little")
                alpha_nibble = (packed_alpha >> (4 * (local_x * 4 + local_y))) & 0xF
                alpha = expand_4_to_8(alpha_nibble)
                etc_base += 8

            raw = int.from_bytes(data[etc_base : etc_base + 8], "little")
            r, g, b = sample_etc1_subtile(raw, local_x, local_y)
            out_offset = (y * width + x) * 2
            out[out_offset : out_offset + 2] = rgba8888_to_rgba16(r, g, b, alpha)

    return bytes(out)


def sample_etc1_subtile(raw: int, x: int, y: int) -> tuple[int, int, int]:
    texel = 4 * x + y
    flip = (raw >> 32) & 1
    differential = (raw >> 33) & 1
    table_index_2 = (raw >> 34) & 0x7
    table_index_1 = (raw >> 37) & 0x7

    lookup_x = y if flip else x

    if differential:
        base_r = (raw >> 59) & 0x1F
        base_g = (raw >> 51) & 0x1F
        base_b = (raw >> 43) & 0x1F
        if lookup_x >= 2:
            base_r = base_r + sign_extend((raw >> 56) & 0x7, 3)
            base_g = base_g + sign_extend((raw >> 48) & 0x7, 3)
            base_b = base_b + sign_extend((raw >> 40) & 0x7, 3)
        r = expand_5_to_8(base_r)
        g = expand_5_to_8(base_g)
        b = expand_5_to_8(base_b)
    else:
        if lookup_x < 2:
            r = expand_4_to_8((raw >> 60) & 0xF)
            g = expand_4_to_8((raw >> 52) & 0xF)
            b = expand_4_to_8((raw >> 44) & 0xF)
        else:
            r = expand_4_to_8((raw >> 56) & 0xF)
            g = expand_4_to_8((raw >> 48) & 0xF)
            b = expand_4_to_8((raw >> 40) & 0xF)

    table_index = table_index_1 if lookup_x < 2 else table_index_2
    table_subindex = (raw >> texel) & 1
    negation = (raw >> (16 + texel)) & 1
    modifier = ETC1_MODIFIER_TABLE[table_index][table_subindex]
    if negation:
        modifier = -modifier

    return clamp_u8(r + modifier), clamp_u8(g + modifier), clamp_u8(b + modifier)


def iter_u16(data: bytes, pixel_count: int) -> Iterable[int]:
    if len(data) < pixel_count * 2:
        raise ParseError("16-bit texture data is truncated")
    for offset in range(0, pixel_count * 2, 2):
        yield int.from_bytes(data[offset : offset + 2], "little")


def rgba8888_to_rgba16(r: int, g: int, b: int, a: int) -> bytes:
    value = ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | (1 if a >= 128 else 0)
    return value.to_bytes(2, "big")


def expand_4_to_5(value: int) -> int:
    return ((value << 1) | (value >> 3)) & 0x1F


def expand_4_to_8(value: int) -> int:
    return ((value << 4) | value) & 0xFF


def expand_5_to_8(value: int) -> int:
    return ((value << 3) | (value >> 2)) & 0xFF


def sign_extend(value: int, bits: int) -> int:
    sign_bit = 1 << (bits - 1)
    return (value ^ sign_bit) - sign_bit


def clamp_u8(value: int) -> int:
    return max(0, min(255, value))


def to_u8(value: float) -> int:
    if 0.0 <= value <= 1.0:
        value *= 255.0
    return max(0, min(255, int(round(value))))


def sanitize_identifier(value: str) -> str:
    cleaned = []
    for char in value:
        if char.isalnum() or char == "_":
            cleaned.append(char)
        else:
            cleaned.append("_")
    result = "".join(cleaned).strip("_")
    return result or "unnamed"


def is_power_of_two(value: int) -> bool:
    return value > 0 and (value & (value - 1)) == 0


def texture_mask(value: int) -> int:
    if is_power_of_two(value):
        return int(math.log2(value))
    return 0
