"""Local scene light catalog for the renderer, from the user's RomFS.

The rooms' own light sources are drawn into their meshes: lava and fire
surfaces, windows and lamps, light shafts, magic glows. A torch stand is not
one: the flame it holds is drawn by an actor (a torch, a fire object, the
drawbridge), which the runtime lights while the title draws its flame. The
title bakes their light into vertex colours; the Modern renderer lights the
scene with them instead. Each room's materials are classified by their first
texture's name; the triangles of an emitting material become objects
(connected triangles, merged when close), each object one point light
(lava: area clusters), coloured by its texture's mean. Plain white additive
volumes shaped like an opening are doorways: daylight, lit near their outer
end. Each light names its surfaces: the content hashes of the textures it is
the light of (the renderer's base-level hash). The runtime lights it with the
strength the game draws them with, so a glow the game fades by day (lit
windows, halos) is dark by day. Where a room draws its lit windows as
additive glow, the opaque panes are not lights. The catalog is
generated on the user's machine from the user's rooms; the release supplies
the emitters it computed for the supported rooms, keyed by each room file's
SHA-256 (precomputed_tables), and only rooms it does not cover are analysed
here. Format (JSON):

    {"schema": "triaevum.scene-lights", "version": 1, "rooms": [
        {"scene": 10, "room": 1, "masterQuest": false, "fingerprint": "<16 hex>",
         "emitters": [{"category": "window-lamp", "position": [x, y, z], "radius": r,
                       "gain": g, "colour": [r, g, b], "texture": "name",
                       "surfaces": ["<16 hex>"]}]}]}

A scene's standard and Master Quest rooms share scene and room numbers; the
runtime tells them apart by the fingerprint of the room file it has loaded.
"""

from __future__ import annotations

import copy
import hashlib
import json
import math
import re
import struct
from collections import defaultdict
from pathlib import Path
from typing import Callable, Iterator

try:
    from .common import atomic_write_bytes, atomic_write_json, load_json_object
    from .water_textures import BITS_PER_PIXEL, _romfs_image_files, content_hash
except ImportError:
    from common import atomic_write_bytes, atomic_write_json, load_json_object
    from water_textures import BITS_PER_PIXEL, _romfs_image_files, content_hash

CATALOG_SCHEMA = "triaevum.scene-lights"
CATALOG_VERSION = 1
# Bumped with any change to the emitters' placement, so installed catalogs
# are regenerated.
GENERATOR_REVISION = 4
# A merged light names at most this many surfaces.
MAX_SURFACES = 2

# Scene file stems by scene id (the title's scene table order).
SCENE_NAMES = {
    0: "ydan", 1: "ddan", 2: "bdan", 3: "bmori1", 4: "hidan", 5: "mizusin", 6: "jyasinzou", 7: "hakadan",
    8: "hakadan_ch", 9: "ice_doukutu", 10: "ganon", 11: "men", 12: "gerudoway", 13: "ganontika",
    14: "ganon_sonogo", 15: "ganontikasonogo", 16: "takaraya", 17: "ydan_boss", 18: "ddan_boss",
    19: "bdan_boss", 20: "moriboss", 21: "fire_bs", 22: "mizusin_boss", 23: "jyasinzou_boss",
    24: "hakadan_boss", 25: "ganon_boss", 26: "ganon_final", 27: "entra_day", 28: "entra_night",
    29: "entra_ruins", 30: "market_alley", 31: "market_alley_n", 32: "market_day", 33: "market_night",
    34: "market_ruins", 35: "shrine", 36: "shrine_n", 37: "shrine_r", 38: "k_home", 39: "k_home3",
    40: "k_home4", 41: "k_home5", 42: "kakariko", 43: "kakariko_home3", 44: "shop", 45: "kokiri",
    46: "shop_golon", 47: "zoora", 48: "shop_drag", 49: "shop_alley", 50: "shop_night", 51: "shop_face",
    52: "link", 53: "kakariko_impa", 54: "stable", 55: "labo", 56: "hylia_labo", 57: "tent", 58: "hut",
    59: "daiyousei_izumi", 60: "yousei_izumi_tate", 61: "yousei_izumi_yoko", 62: "kakusiana",
    63: "hakaana", 64: "hakaana2", 65: "hakaana_ouke", 66: "syatekijyou", 67: "tokinoma", 68: "kenjyanoma",
    69: "hairal_niwa", 70: "hairal_niwa_n", 71: "hiral_demo", 72: "hakasitarelay", 73: "turibori",
    74: "nakaniwa", 75: "bowling", 76: "souko", 77: "miharigoya", 78: "mahouya", 79: "ganon_demo",
    80: "kinsuta", 81: "spot00", 82: "spot01", 83: "spot02", 84: "spot03", 85: "spot04", 86: "spot05",
    87: "spot06", 88: "spot07", 89: "spot08", 90: "spot09", 91: "spot10", 92: "spot11", 93: "spot12",
    94: "spot13", 95: "spot15", 96: "spot16", 97: "spot17", 98: "spot18", 99: "spot20", 100: "ganon_tou",
    107: "spot99",
}
SCENE_IDS = {name: scene for scene, name in SCENE_NAMES.items()}
ROOM_FILE = re.compile(r"scene/(.+?)_(\d+)(_dd)?_info\.zsi")

GL_ONE = 0x0001
PICA_U8, PICA_S8, PICA_U16, PICA_S16, PICA_U32, PICA_S32, PICA_F32 = (
    0x1401, 0x1400, 0x1403, 0x1402, 0x1405, 0x1404, 0x1406)
SCALAR = {PICA_S8: "b", PICA_U8: "B", PICA_S16: "h", PICA_U16: "H", PICA_S32: "i", PICA_U32: "I", PICA_F32: "f"}
MATERIAL_SIZE = 0x15C

# Per category: the merge distance of neighbouring emitters, the most kept per
# room, the light's gain (calibrated in the lab against the title's own
# lighting: these sources add to light the rooms already bake) and the
# largest reach a merged emitter may grow to.
MERGE = {"window-lamp": 300, "magic-glow": 300, "fire-lava": 500, "light-shaft": 200, "lamp": 60, "doorway": 150}
CAP = {"window-lamp": 12, "magic-glow": 8, "fire-lava": 12, "light-shaft": 8, "lamp": 16, "doorway": 8}
GAIN = {"window-lamp": 0.45, "magic-glow": 0.6, "fire-lava": 0.45, "light-shaft": 0.55, "lamp": 0.5, "doorway": 1.6}
REACH = {"window-lamp": 700, "magic-glow": 500, "fire-lava": 1000, "light-shaft": 600, "lamp": 500, "doorway": 1800}
# Daylight through a doorway: the openings' glow volumes are plain white.
DAYLIGHT = [1.0, 0.95, 0.85]
# A doorway's glow volume: opening height and the largest horizontal extent.
DOORWAY_HEIGHT = (90.0, 400.0)
DOORWAY_WIDTH = 500.0
# Lamps drawn inside a shared atlas: texture name -> [(u0, v0, u1, v1, colour)]
# boxes in the material's transformed first texture coordinates (wrapped into
# 0..1); only the triangles whose centre falls in a box are the lamp.
LAMP_REGIONS: dict[str, list[tuple]] = {
    "rm_ten_obj_02": [(0.750, 0.550, 0.875, 0.815, (1.0, 0.84, 0.52))],   # the tent's lantern
    "tar_rm_kana_01": [(0.500, 0.000, 0.750, 0.285, (1.0, 0.80, 0.45))],  # the warehouse's lanterns
    "kok_rm_obj_04": [(0.0, 0.508, 1.0, 1.0, (1.0, 0.55, 0.22))],         # a Kokiri house's painted hearth
}
# Lamp bodies the rooms draw without a glow card (shared atlases, so named):
# the light's colour, as their atlas's mean says nothing about the lamp.
LAMP_BODIES = {
    "shop_any_lant_0": [1.0, 0.8, 0.5],    # Bazaar lanterns
    "rm_shat_torc_01": [1.0, 0.8, 0.45],   # shooting gallery wall lanterns
    "shop_bomchu_bin": [1.0, 0.75, 0.55],  # Bombchu shop's glowing jars
    "turi_rantan_lig": [1.0, 0.82, 0.5],   # fishing pond lanterns
}
FIRE_COLOUR = [1.0, 0.55, 0.22]
# Lamplight behind a window: its pale texture tinted toward a warm interior.
LAMP_TINT = [1.0, 0.78, 0.5]


# --- Model files --------------------------------------------------------------------------------------------

class Model:
    """The parts of a version-6 CMB the catalog needs: textures (name, format,
    base level), materials (texture indices, blending) and per mesh its
    material and triangles in the model's space (world space for rooms):
    positions placed by their primitive's bone (rigid, one bone); skinned
    primitives are left out. Raises ValueError on anything out of bounds."""

    def __init__(self, data: bytes, offset: int = 0):
        if data[offset:offset + 4] != b"cmb " or offset + 0x44 > len(data):
            raise ValueError("not a CMB")
        size, version = struct.unpack_from("<II", data, offset + 4)
        if version != 6 or size < 0x44 or offset + size > len(data):
            raise ValueError("unsupported CMB")
        self.data = data[offset:offset + size]
        d = self.data
        index_count, skl, mats, tex, sklm, _, vatr, indices, texture_data = struct.unpack_from("<9I", d, 0x20)
        for chunk, magic in ((skl, b"skl "), (mats, b"mats"), (tex, b"tex "), (sklm, b"sklm"), (vatr, b"vatr")):
            if d[chunk:chunk + 4] != magic:
                raise ValueError("CMB chunk is missing")
        self.textures = []
        for i in range(self._u32(tex + 8)):
            entry = tex + 0x0C + i * 0x24
            data_size, _, _, width, height, fmt, kind, relative = self._unpack("<IHHHHHHI", entry)
            name = d[entry + 0x14:entry + 0x24].split(b"\0", 1)[0].decode("ascii", "replace")
            bits = BITS_PER_PIXEL.get((fmt, kind))
            start = texture_data + relative
            # The base level as stored (8x8 tiles); later levels are skipped.
            base = ((max(8, width) + 7) & ~7) * ((max(8, height) + 7) & ~7) * bits // 8 if bits else 0
            size = min(base, data_size)
            self.textures.append((name, width, height, fmt, kind, d[start:start + size] if start + size <= len(d) else b""))
        self.materials = []
        self.uv_transforms = []
        for i in range(self._u32(mats + 8)):
            base = mats + 0x0C + i * MATERIAL_SIZE
            texture_indices = [self._unpack("<h", base + 0x10 + k * 0x18)[0] for k in range(3)]
            blend_mode = self._u32(base + 0x138)
            blend_dst = self._unpack("<H", base + 0x13E)[0]
            self.materials.append((texture_indices, bool(blend_mode) and blend_dst == GL_ONE))
            # The first texture coordinate's scale and translation (rotation
            # is not used by the rooms' lamps).
            su, sv, _, tu, tv = self._unpack("<5f", base + 0x58 + 4)
            self.uv_transforms.append((su or 1.0, sv or 1.0, tu, tv))
        mshs = sklm + self._u32(sklm + 8)
        shp = sklm + self._u32(sklm + 0x0C)
        if d[mshs:mshs + 4] != b"mshs" or d[shp:shp + 4] != b"shp ":
            raise ValueError("CMB mesh chunks are missing")
        self.meshes = [self._unpack("<HB", mshs + 0x10 + i * 4) for i in range(self._u32(mshs + 8))]
        self._shape_count = self._u32(shp + 8)
        self._bones = self._bone_transforms(skl)
        self._shp, self._vatr, self._indices, self._index_count = shp, vatr, indices, index_count
        self._shapes: dict[int, list] = {}
        self._uvs: dict[int, list] = {}

    def _unpack(self, fmt: str, offset: int) -> tuple:
        if offset < 0 or offset + struct.calcsize(fmt) > len(self.data):
            raise ValueError("CMB read out of bounds")
        return struct.unpack_from(fmt, self.data, offset)

    def _u32(self, offset: int) -> int:
        return self._unpack("<I", offset)[0]

    def _bone_transforms(self, skl: int) -> list:
        """Each bone's bind transform to model space (3x4 rows): scale, then
        rotations about X, Y and Z, then translation, under its parent's."""
        count = self._u32(skl + 8)
        bones = [self._unpack("<hh9f", skl + 0x10 + i * 0x28) for i in range(count)]
        local = []
        for _, _, sx, sy, sz, rx, ry, rz, tx, ty, tz in bones:
            cx, snx, cy, sny, cz, snz = math.cos(rx), math.sin(rx), math.cos(ry), math.sin(ry), math.cos(rz), math.sin(rz)
            # Rz * Ry * Rx, scaled per column.
            r = [[cz * cy, cz * sny * snx - snz * cx, cz * sny * cx + snz * snx],
                 [snz * cy, snz * sny * snx + cz * cx, snz * sny * cx - cz * snx],
                 [-sny, cy * snx, cy * cx]]
            local.append([[r[i][0] * sx, r[i][1] * sy, r[i][2] * sz, (tx, ty, tz)[i]] for i in range(3)])
        world: list = [None] * count
        for index in range(count):
            # The chain up to a resolved bone or the root, then down it.
            chain, current = [], index
            while current >= 0 and world[current] is None:
                if current >= count or len(chain) > count:
                    raise ValueError("CMB bone hierarchy is cyclic or out of range")
                chain.append(current)
                current = bones[current][1]
            for bone in reversed(chain):
                parent = bones[bone][1]
                if parent < 0:
                    world[bone] = local[bone]
                    continue
                a, b = world[parent], local[bone]
                world[bone] = [[sum(a[i][k] * b[k][j] for k in range(3)) + (a[i][3] if j == 3 else 0.0)
                                for j in range(4)] for i in range(3)]
        return world

    def triangles(self, shape: int) -> list:
        """The shape's visible rigid triangles, corners as ((shape, vertex)
        key, model-space position)."""
        if shape in self._shapes:
            return self._shapes[shape]
        if not 0 <= shape < self._shape_count:
            raise ValueError("CMB mesh shape is out of range")
        d = self.data
        triangles = []
        sepd = self._shp + self._unpack("<H", self._shp + 0x10 + shape * 2)[0]
        if d[sepd:sepd + 4] != b"sepd":
            raise ValueError("CMB shape chunk is missing")
        primitive_count, flags = self._unpack("<HH", sepd + 8)
        auto_flags = self._unpack("<H", sepd + 0x106)[0]
        offset, scale, kind, mode = self._unpack("<IfHH", sepd + 0x24)
        constant = self._unpack("<3f", sepd + 0x30)
        length, start = self._unpack("<II", self._vatr + 0x0C)
        stream = self._vatr + start + offset
        fixed = not (flags & 1) or (auto_flags & 1) or mode != 0
        code = SCALAR.get(kind)
        if code is None or not math.isfinite(scale) or not all(math.isfinite(c) for c in constant):
            raise ValueError("CMB positions are not readable")
        fmt, width = "<" + code * 3, struct.calcsize("<" + code) * 3
        # The first texture coordinates (flag 8), for lamps inside atlases.
        uv_offset, uv_scale, uv_kind, uv_mode = self._unpack("<IfHH", sepd + 0x78)
        uv_constant = self._unpack("<2f", sepd + 0x84)
        uv_length, uv_start = self._unpack("<II", self._vatr + 0x24)
        uv_code = SCALAR.get(uv_kind)
        uv_fixed = not (flags & 8) or (auto_flags & 8) or uv_mode != 0 or uv_code is None
        uv_width = struct.calcsize("<" + uv_code) * 2 if uv_code else 0

        def uv(index: int) -> tuple:
            if uv_fixed or not math.isfinite(uv_scale):
                return uv_constant
            if uv_offset + (index + 1) * uv_width > uv_length:
                raise ValueError("CMB texture coordinate is out of range")
            s, t_ = self._unpack("<" + uv_code * 2, self._vatr + uv_start + uv_offset + index * uv_width)
            return (s * uv_scale, t_ * uv_scale)

        uvs = []

        def position(index: int) -> tuple:
            if fixed:
                return constant
            if offset + (index + 1) * width > length:
                raise ValueError("CMB vertex is out of range")
            x, y, z = self._unpack(fmt, stream + index * width)
            return (x * scale, y * scale, z * scale)

        for p in range(primitive_count):
            prms = sepd + self._unpack("<H", sepd + 0x108 + p * 2)[0]
            if d[prms:prms + 4] != b"prms":
                raise ValueError("CMB primitive chunk is missing")
            skinning, bone_count, bone_table, relative = self._unpack("<HHII", prms + 0x0C)
            prm = prms + relative
            if d[prm:prm + 4] != b"prm ":
                raise ValueError("CMB primitive chunk is missing")
            visible, primitive_mode, index_type, _, count, first = self._unpack("<IIHHHH", prm + 8)
            index_code = {PICA_U8: "B", PICA_U16: "H", PICA_U32: "I"}.get(index_type)
            if not visible or primitive_mode != 0 or index_code is None:
                continue
            # Skinned primitives move with their bones at run time: left out.
            if skinning != 0 or bone_count != 1:
                continue
            bone = self._unpack("<H", prms + bone_table)[0]
            if bone >= len(self._bones):
                raise ValueError("CMB primitive bone is out of range")
            m = self._bones[bone]
            # The first index is counted in 16-bit units whatever the index type,
            # and the indices stay within the model's index buffer.
            if first * 2 + count * struct.calcsize(index_code) > self._index_count * 2:
                raise ValueError("CMB primitive indices are out of range")
            values = self._unpack(f"<{count}{index_code}", self._indices + first * 2)
            for i in range(0, count - 2, 3):
                corners = []
                for k in range(3):
                    x, y, z = position(values[i + k])
                    placed = tuple(m[r][0] * x + m[r][1] * y + m[r][2] * z + m[r][3] for r in range(3))
                    if not all(math.isfinite(v) for v in placed):
                        raise ValueError("CMB vertex is not finite")
                    corners.append((values[i + k], placed))
                triangles.append(tuple(corners))
                coordinates = [uv(values[i + k]) for k in range(3)]
                uvs.append((sum(c[0] for c in coordinates) / 3, sum(c[1] for c in coordinates) / 3))
        self._shapes[shape] = triangles
        self._uvs[shape] = uvs
        return triangles

    def triangle_uvs(self, shape: int) -> list:
        """The texture coordinates at the centre of each of triangles(shape)."""
        self.triangles(shape)
        return self._uvs[shape]


def embedded_models(data: bytes) -> Iterator[Model]:
    search = 0
    while (offset := data.find(b"cmb ", search)) >= 0:
        search = offset + 1
        try:
            yield Model(data, offset)
        except (ValueError, struct.error):
            continue


# --- Texture colour -----------------------------------------------------------------------------------------

ETC1_MODIFIERS = ((2, 8), (5, 17), (9, 29), (13, 42), (18, 60), (24, 80), (33, 106), (47, 183))


def _etc1_texels(block: int) -> Iterator[tuple[int, int, int]]:
    flip, differential = (block >> 32) & 1, (block >> 33) & 1
    tables = ((block >> 37) & 7, (block >> 34) & 7)
    if differential:
        base = [(block >> s) & 31 for s in (59, 51, 43)]
        delta = [((block >> s) & 7) - (8 if (block >> s) & 4 else 0) for s in (56, 48, 40)]
        halves = [[(c << 3) | (c >> 2) for c in base],
                  [((b + e) << 3) | ((b + e) >> 2) for b, e in zip(base, delta)]]
    else:
        halves = [[((block >> s) & 15) * 17 for s in (60, 52, 44)], [((block >> s) & 15) * 17 for s in (56, 48, 40)]]
    for x in range(4):
        for y in range(4):
            texel = 4 * x + y
            half = (y if flip else x) >= 2
            modifier = ETC1_MODIFIERS[tables[half]][(block >> texel) & 1]
            if (block >> (16 + texel)) & 1:
                modifier = -modifier
            yield tuple(max(0, min(255, c + modifier)) for c in halves[half])


def texels(fmt: int, kind: int, data: bytes) -> Iterator[tuple[int, int, int, int]]:
    """(r, g, b, a) of every stored texel; their order (the PICA tiling) does
    not matter for a mean."""
    if (fmt, kind) == (0x6752, PICA_U8):
        for i in range(0, len(data) - 3, 4):
            a, b, g, r = data[i:i + 4]
            yield r, g, b, a
    elif (fmt, kind) == (0x6754, PICA_U8):
        for i in range(0, len(data) - 2, 3):
            b, g, r = data[i:i + 3]
            yield r, g, b, 255
    elif (fmt, kind) in ((0x6752, 0x8034), (0x6754, 0x8363), (0x6752, 0x8033)):
        for (v,) in struct.iter_unpack("<H", data[:len(data) & ~1]):
            if kind == 0x8034:
                yield (v >> 11 & 31) * 255 // 31, (v >> 6 & 31) * 255 // 31, (v >> 1 & 31) * 255 // 31, (v & 1) * 255
            elif kind == 0x8363:
                yield (v >> 11 & 31) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31, 255
            else:
                yield (v >> 12 & 15) * 17, (v >> 8 & 15) * 17, (v >> 4 & 15) * 17, (v & 15) * 17
    elif (fmt, kind) == (0x6758, PICA_U8):
        for i in range(0, len(data) - 1, 2):
            a, l = data[i:i + 2]
            yield l, l, l, a
    elif (fmt, kind) == (0x6758, 0x6760):
        for v in data:
            yield (v >> 4) * 17, (v >> 4) * 17, (v >> 4) * 17, (v & 15) * 17
    elif (fmt, kind) == (0x6757, PICA_U8):
        for v in data:
            yield v, v, v, 255
    elif (fmt, kind) == (0x6757, 0x6761):
        for v in data:
            for n in (v & 15, v >> 4):
                yield n * 17, n * 17, n * 17, 255
    elif (fmt, kind) == (0x6756, PICA_U8):
        for v in data:
            yield 255, 255, 255, v
    elif (fmt, kind) == (0x6756, 0x6761):
        for v in data:
            for n in (v & 15, v >> 4):
                yield 255, 255, 255, n * 17
    elif fmt in (0x675A, 0x675B):
        alpha = fmt == 0x675B
        stride = 16 if alpha else 8
        for i in range(0, len(data) - stride + 1, stride):
            alphas = int.from_bytes(data[i:i + 8], "little") if alpha else None
            block = int.from_bytes(data[i + (8 if alpha else 0):i + stride], "little")
            for n, (r, g, b) in enumerate(_etc1_texels(block)):
                yield r, g, b, ((alphas >> (4 * n)) & 15) * 17 if alpha else 255


def texel_grid(fmt: int, kind: int, data: bytes, width: int, height: int) -> list[list[tuple]] | None:
    """The base level's (r, g, b, a) texels by position (row 0 as stored),
    undoing the PICA 8x8 tiling (Morton order inside a tile; ETC1 tiles hold
    four 4x4 blocks)."""
    if width <= 0 or height <= 0 or width > 1024 or height > 1024:
        return None
    grid = [[(0, 0, 0, 0)] * width for _ in range(height)]
    tiles_x = (max(8, width) + 7) // 8
    if fmt in (0x675A, 0x675B):
        alpha = fmt == 0x675B
        stride = 16 if alpha else 8
        for index in range(len(data) // stride):
            tile, sub = divmod(index, 4)
            tile_y, tile_x = divmod(tile, tiles_x)
            base = index * stride
            alphas = int.from_bytes(data[base:base + 8], "little") if alpha else 0
            block = int.from_bytes(data[base + (8 if alpha else 0):base + stride], "little")
            for n, (r, g, b) in enumerate(_etc1_texels(block)):
                x = tile_x * 8 + (sub % 2) * 4 + n // 4
                y = tile_y * 8 + (sub // 2) * 4 + n % 4
                if x < width and y < height:
                    grid[y][x] = (r, g, b, ((alphas >> (4 * n)) & 15) * 17 if alpha else 255)
        return grid
    for index, texel in enumerate(texels(fmt, kind, data)):
        tile, m = divmod(index, 64)
        tile_y, tile_x = divmod(tile, tiles_x)
        x = tile_x * 8 + sum(((m >> (2 * i)) & 1) << i for i in range(3))
        y = tile_y * 8 + sum(((m >> (2 * i + 1)) & 1) << i for i in range(3))
        if x < width and y < height:
            grid[y][x] = texel
    return grid


def is_halo(fmt: int, kind: int, data: bytes, width: int, height: int) -> bool:
    """A glow card's texture: bright in the middle, dark at the rim, round and
    smooth around its centre (the halos the rooms draw around their lamps,
    whatever their names)."""
    if width != height or width < 16:
        return False
    grid = texel_grid(fmt, kind, data, width, height)
    if grid is None:
        return False
    weighted = fmt in (0x6752, 0x675B, 0x6758, 0x6756)
    rings, sectors = defaultdict(list), [[] for _ in range(8)]
    centre = (width - 1) / 2
    for y, row in enumerate(grid):
        for x, (r, g, b, a) in enumerate(row):
            value = max(r, g, b) / 255.0 * (a / 255.0 if weighted else 1.0)
            dx, dy = (x - centre) / (width / 2), (y - centre) / (height / 2)
            d = math.hypot(dx, dy)
            rings[min(4, int(d * 4))].append(value)
            if 0.2 <= d <= 0.6:
                sectors[int((math.atan2(dy, dx) + math.pi) / (2 * math.pi) * 8) % 8].append(value)
    mean = lambda values: sum(values) / len(values) if values else 0.0
    middle, rim = mean(rings[0]), (mean(rings[3]) + mean(rings[4])) / 2
    band = [mean(s) for s in sectors if s]
    level = mean(band)
    spread = math.sqrt(mean([(v - level) ** 2 for v in band])) / level if level > 0 else 1.0
    return middle >= 0.45 and rim <= 0.12 and middle - rim >= 0.4 and level >= 0.4 and spread <= 0.35


def texture_colour(fmt: int, kind: int, data: bytes) -> list[float] | None:
    """The texture's emitted colour: the mean of its opaque texels, the bright
    ones weighted more."""
    total, weight = [0.0, 0.0, 0.0], 0.0
    for r, g, b, a in texels(fmt, kind, data):
        if a < 128:
            continue
        w = max(r, g, b) / 255.0
        total[0] += r / 255.0 * w
        total[1] += g / 255.0 * w
        total[2] += b / 255.0 * w
        weight += w
    if weight <= 0.0:
        return None
    return [c / weight for c in total]


# --- Classification and emitters ----------------------------------------------------------------------------

def category(first_texture: str, additive: bool, halo: bool = False) -> str | None:
    """The emitting category of a material: a glow card's halo or a named lamp
    body is a lamp; otherwise its first texture's name decides."""
    if (additive and halo) or first_texture in LAMP_BODIES or first_texture in LAMP_REGIONS:
        return "lamp"
    first = first_texture.lower()
    if re.search(r"doughnut|^ido_|kja_|mir_decal", first):
        return None
    # A torch stand is not a light: the flame on it is (an actor's, lit by
    # the runtime while it burns). An unlit stand, or one whose flame burns
    # only at night, gives no light.
    if re.search(r"shokudai|shoku_|syoku|torch|taimatsu|rousoku|candle", first):
        return None
    if re.search(r"mado|window|lamp|neon", first):
        return "window-lamp"
    if re.search(r"mark_lt|mark_tam|maho_star", first):
        return "magic-glow"
    if re.search(r"hikari|light", first):
        return "light-shaft"
    if re.search(r"fire|fir\d|mgm|yougan|magma|lava|hinoko|hono", first):
        return "fire-lava"
    # The fire scenes' additive glow overlays on walls and floors (lava light).
    if additive and re.search(r"^(dg05_.*_al|dg_dod_.*t|dg_g_kabe_.*_t|gc_kabe_\d+t|gcp_kabe_\d+_t)$", first):
        return "fire-lava"
    return None


def is_daylight(colour: list[float] | None) -> bool:
    """A plain white texture: the glow the rooms fill their openings with."""
    return colour is not None and sum(colour) / 3 >= 0.95 and max(colour) - min(colour) <= 0.02


def doorway_emitter(low: list[float], high: list[float], centre: list[float]) -> dict | None:
    """A white additive volume shaped like an opening (a doorway's height, a
    corridor's width, not flat) lets daylight in: a light near its outer end,
    the end away from the room's centre, lighting the way inward. Other white
    volumes (mist sheets, fog banks) are not doorways."""
    size = [high[k] - low[k] for k in range(3)]
    if not DOORWAY_HEIGHT[0] <= size[1] <= DOORWAY_HEIGHT[1] or max(size[0], size[2]) > DOORWAY_WIDTH \
            or min(size) < 25.0:
        return None
    middle = [(low[k] + high[k]) / 2 for k in range(3)]
    # The horizontal axis along which the opening lies farthest from the room's centre, for its size.
    axis = max((0, 2), key=lambda k: abs(middle[k] - centre[k]) / max(size[k], 1.0))
    position = list(middle)
    position[axis] += math.copysign(size[axis] * 0.35, middle[axis] - centre[axis])
    position[1] = low[1] + 0.6 * size[1]
    reach = max(size[1], size[0], size[2]) * 7.0
    return {"position": position, "radius": round(min(REACH["doorway"], max(900.0, reach))),
            "gain": GAIN["doorway"], "colour": list(DAYLIGHT), "weight": size[0] * size[1] * size[2]}


def objects(triangles: list, merge: float = 40.0) -> list:
    """Connected triangle sets (shared vertices), merged when their bounding
    boxes come within `merge` units: [low corner, high corner, triangles]."""
    if not triangles:
        return []
    parent = list(range(len(triangles)))

    def find(a: int) -> int:
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    owner: dict = {}
    for t, triangle in enumerate(triangles):
        for key, _ in triangle:
            if key in owner:
                parent[find(t)] = find(owner[key])
            else:
                owner[key] = t
    groups = defaultdict(list)
    for t, triangle in enumerate(triangles):
        groups[find(t)].append(tuple(p for _, p in triangle))
    boxes = []
    for tris in groups.values():
        points = [p for tri in tris for p in tri]
        boxes.append([[min(p[k] for p in points) for k in range(3)],
                      [max(p[k] for p in points) for k in range(3)], tris])
    merged = True
    while merged:
        merged = False
        for i in range(len(boxes)):
            for j in range(i + 1, len(boxes)):
                a, b = boxes[i], boxes[j]
                if all(a[0][k] - merge <= b[1][k] and b[0][k] - merge <= a[1][k] for k in range(3)):
                    a[0] = [min(a[0][k], b[0][k]) for k in range(3)]
                    a[1] = [max(a[1][k], b[1][k]) for k in range(3)]
                    a[2] += b[2]
                    del boxes[j]
                    merged = True
                    break
            if merged:
                break
    return boxes


def area_normal(tris) -> tuple[list[float], float]:
    n, area = [0.0, 0.0, 0.0], 0.0
    for a, b, c in tris:
        u = [b[k] - a[k] for k in range(3)]
        v = [c[k] - a[k] for k in range(3)]
        cross = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
        n = [n[k] + cross[k] for k in range(3)]
        area += 0.5 * math.sqrt(sum(c * c for c in cross))
    length = math.sqrt(sum(c * c for c in n)) or 1.0
    return [c / length for c in n], area


def emitters_for(cat: str, colour: list[float], boxes: list) -> list[dict]:
    out = []
    if cat == "fire-lava":
        # 600-unit cells over the triangles' centres weighted by area, the
        # largest first; raised off the surface along its facing.
        cells = defaultdict(lambda: [0.0, [0.0, 0.0, 0.0], [0.0, 0.0, 0.0]])
        for _, _, tris in boxes:
            for tri in tris:
                normal, area = area_normal([tri])
                middle = [sum(p[k] for p in tri) / 3 for k in range(3)]
                cell = cells[tuple(int(middle[k] // 600) for k in range(3))]
                cell[0] += area
                cell[1] = [cell[1][k] + middle[k] * area for k in range(3)]
                cell[2] = [cell[2][k] + normal[k] * area for k in range(3)]
        for area, sum_position, sum_normal in sorted(cells.values(), key=lambda c: -c[0])[:8]:
            if area < 2500:
                continue
            p = [sum_position[k] / area for k in range(3)]
            length = math.sqrt(sum(v * v for v in sum_normal)) or 1.0
            normal = [v / length for v in sum_normal]
            p = [p[k] + normal[k] * 60 for k in range(3)] if abs(normal[1]) < 0.5 else [p[0], p[1] + 60, p[2]]
            out.append({"position": p, "radius": round(min(900, 300 + math.sqrt(area) * 0.8)), "gain": GAIN[cat],
                        "colour": colour, "weight": area})
        return out
    for low, high, tris in boxes:
        size = max(high[k] - low[k] for k in range(3))
        middle = [(low[k] + high[k]) / 2 for k in range(3)]
        normal, area = area_normal(tris)
        if cat == "lamp":
            # A halo is centred on its lamp; a lamp body is the lamp.
            out.append({"position": middle, "radius": round(min(REACH[cat], max(250, size * 2.5))),
                        "gain": GAIN[cat], "colour": colour, "weight": 1.0})
        elif cat == "window-lamp":
            p = [middle[k] + normal[k] * 40 for k in range(3)] if area > 0 else middle
            out.append({"position": p, "radius": round(min(600, 250 + size * 0.6)), "gain": GAIN[cat],
                        "colour": colour, "weight": 1.0})
        elif cat == "magic-glow":
            out.append({"position": middle, "radius": round(min(500, 250 + size)), "gain": GAIN[cat],
                        "colour": colour, "weight": 1.0})
        elif cat == "light-shaft":
            # Where the shaft lands, lighting the ground around it.
            if high[1] - low[1] < 60:
                continue
            width = max(high[0] - low[0], high[2] - low[2])
            out.append({"position": [middle[0], low[1] + 40, middle[2]],
                        "radius": round(min(600, max(400, width * 1.5))), "gain": GAIN[cat], "colour": colour,
                        "weight": 1.0})
    return out


def consolidate(emitters: list[dict]) -> list[dict]:
    """Per category, emitters closer than its merge distance become one
    (weighted position, reach grown to cover both); the strongest are kept up
    to the category's cap."""
    kept = []
    for cat in MERGE:
        group = [dict(e) for e in emitters if e["category"] == cat]
        merged = True
        while merged:
            merged = False
            for i in range(len(group)):
                for j in range(i + 1, len(group)):
                    a, b = group[i], group[j]
                    distance = math.dist(a["position"], b["position"])
                    if distance < MERGE[cat]:
                        w = a["weight"] + b["weight"]
                        a["position"] = [(a["position"][k] * a["weight"] + b["position"][k] * b["weight"]) / w
                                         for k in range(3)]
                        a["radius"] = round(min(REACH[cat], max(a["radius"], b["radius"]) + 0.5 * distance))
                        a["weight"] = w
                        # The merged light is the light of both emitters' surfaces.
                        for surface in b.get("surfaces", []):
                            if surface not in a.setdefault("surfaces", []) and len(a["surfaces"]) < MAX_SURFACES:
                                a["surfaces"].append(surface)
                        del group[j]
                        merged = True
                        break
                if merged:
                    break
        group.sort(key=lambda e: -e["weight"])
        kept += group[:CAP[cat]]
    return kept


FINGERPRINT_OFFSET, FINGERPRINT_BYTES = 0x10, 128


def room_fingerprint(data: bytes) -> str:
    """The room file's identity as the runtime sees it loaded: the start of
    its command list (the title loads the file verbatim and points at its
    commands, after the 16-byte header), hashed like the renderer's content
    hashes (FNV-1a 64 with the renderer's offset basis 1469598103934665603,
    not the standard one)."""
    return f"{content_hash(data[FINGERPRINT_OFFSET:FINGERPRINT_OFFSET + FINGERPRINT_BYTES]):016x}"


def model_centre(model: Model) -> list[float]:
    """The centre of the model's bounding box (its room, for a room model)."""
    if getattr(model, "_centre", None) is None:
        points = []
        for shape, _ in model.meshes:
            try:
                points += [p for tri in model.triangles(shape) for _, p in tri]
            except ValueError:
                continue
        model._centre = [(min(p[k] for p in points) + max(p[k] for p in points)) / 2 for k in range(3)] \
            if points else [0.0, 0.0, 0.0]
    return model._centre


def model_emitters(model: Model) -> list[dict]:
    """The emitters of one model, before the room's consolidation."""
    emitters = []
    shapes_by_material = defaultdict(list)
    for shape, material in model.meshes:
        if material < len(model.materials):
            shapes_by_material[material].append(shape)
    materials = {}
    for material in shapes_by_material:
        texture_indices, additive = model.materials[material]
        textures = [model.textures[i] for i in texture_indices if 0 <= i < len(model.textures)]
        if textures:
            name, width, height, fmt, kind, pixels = textures[0]
            materials[material] = (textures[0], additive,
                                   category(name, additive, additive and is_halo(fmt, kind, pixels, width, height)))
    # Lit windows drawn as additive glow: the glow is the light, the panes are glass.
    window_glow = any(cat == "window-lamp" and additive for _, additive, cat in materials.values())
    for material, shapes in shapes_by_material.items():
        if material not in materials:
            continue
        (name, width, height, fmt, kind, pixels), additive, cat = materials[material]
        if cat == "window-lamp" and not additive and window_glow:
            cat = None
        surface = f"{content_hash(pixels):016x}"
        raw = texture_colour(fmt, kind, pixels)
        boxes = None
        if additive and is_daylight(raw):
            # Doorway-shaped white volumes let daylight in; the material's other
            # objects keep its named category.
            triangles = [tuple(((shape, index), p) for index, p in tri)
                         for shape in shapes for tri in model.triangles(shape)]
            boxes = []
            for low, high, tris in objects(triangles, 10.0):
                emitter = doorway_emitter(low, high, model_centre(model))
                if emitter is None:
                    boxes.append((low, high, tris))
                else:
                    emitter.update({"category": "doorway", "texture": name, "surfaces": [surface]})
                    emitters.append(emitter)
        if not cat:
            continue
        # Fire found by loose name stems must look like fire.
        if cat == "fire-lava" and raw is not None and not (raw[0] >= raw[2] and max(raw) > 0.25):
            continue
        peak = max(raw) if raw else 0.0
        colour = [c / peak for c in raw] if peak > 0.0 else [1.0, 0.7, 0.4]
        # Greyscale fire textures take their colour from the combiner.
        if cat == "fire-lava" and min(colour) > 0.85:
            colour = list(FIRE_COLOUR)
        elif cat == "window-lamp":
            colour = [c * w for c, w in zip(colour, LAMP_TINT)]
        elif cat == "lamp" and name in LAMP_BODIES:
            colour = list(LAMP_BODIES[name])
        # An atlas lamp is only its regions' triangles, each region its colour.
        regions = LAMP_REGIONS.get(name) if cat == "lamp" else None
        if boxes is not None and regions is None:
            for emitter in emitters_for(cat, colour, boxes):
                emitter.update({"category": cat, "texture": name, "surfaces": [surface]})
                emitters.append(emitter)
            continue
        batches = [(colour, (0.0, 0.0, 1.0, 1.0))] if regions is None else [(list(r[4]), r[:4]) for r in regions]
        su, sv, tu, tv = model.uv_transforms[material]
        for batch_colour, (u0, v0, u1, v1) in batches:
            triangles = []
            for shape in shapes:
                for tri, (u, v) in zip(model.triangles(shape), model.triangle_uvs(shape)):
                    if regions is not None:
                        u, v = (u * su + tu) % 1.0, (v * sv + tv) % 1.0
                        if not (u0 <= u <= u1 and v0 <= v <= v1):
                            continue
                    triangles.append(tuple(((shape, index), p) for index, p in tri))
            # A lamp's crossed glow cards coincide, and separate lamps may hang
            # close: halos group tightly; bodies (frame and glass apart) loosely.
            merge = 10.0 if cat == "lamp" and additive else 40.0
            for emitter in emitters_for(cat, batch_colour, objects(triangles, merge)):
                emitter.update({"category": cat, "texture": name, "surfaces": [surface]})
                emitters.append(emitter)
    return emitters


def finite(emitter: dict) -> bool:
    values = [*emitter["position"], *emitter["colour"], emitter["radius"], emitter["gain"], emitter["weight"]]
    return all(isinstance(v, (int, float)) and math.isfinite(v) for v in values)


def room_emitters(data: bytes) -> list[dict]:
    """The room's emitters; a model that does not read cleanly (out of
    bounds, not finite) adds none."""
    emitters = []
    for model in embedded_models(data):
        try:
            found = model_emitters(model)
        except (ValueError, struct.error, ArithmeticError):
            continue
        if all(finite(e) for e in found):
            emitters += found
    kept = consolidate(emitters)
    for e in kept:
        e["position"] = [round(v, 1) for v in e["position"]]
        e["colour"] = [round(v, 3) for v in e["colour"]]
        del e["weight"]
    return kept


def romfs_room_files(romfs: Path) -> Iterator[tuple[str, bytes]]:
    """The scenes' room files, from a RomFS image or an extracted RomFS
    directory."""
    if romfs.is_dir():
        for path in sorted((romfs / "scene").glob("*_info.zsi")):
            if ROOM_FILE.fullmatch(f"scene/{path.name}"):
                yield f"scene/{path.name}", path.read_bytes()
        return
    for relative, data in _romfs_image_files(romfs, ("scene",)):
        if ROOM_FILE.fullmatch(relative):
            yield relative, data


def build_catalog(romfs: Path, known: dict[str, list] | None = None) -> tuple[bytes, dict]:
    """The catalog; `known` holds the release's emitters by room file
    SHA-256 (precomputed_tables), used for exactly those rooms."""
    rooms, counts, reused = [], defaultdict(int), 0
    for path, data in romfs_room_files(romfs):
        match = ROOM_FILE.fullmatch(path)
        if not match or match.group(1) not in SCENE_IDS:
            continue
        digest = hashlib.sha256(data).hexdigest() if known else None
        if digest in (known or {}):
            emitters = copy.deepcopy(known[digest])
            reused += 1
        else:
            emitters = room_emitters(data)
        if not emitters:
            continue
        rooms.append({"scene": SCENE_IDS[match.group(1)], "room": int(match.group(2)),
                      "masterQuest": bool(match.group(3)), "fingerprint": room_fingerprint(data),
                      "emitters": emitters})
    rooms.sort(key=lambda r: (r["scene"], r["room"], r["masterQuest"]))
    # A standard and a Master Quest room whose command lists begin alike
    # cannot be told apart at run time: one entry when their lights agree,
    # the standard room's when they differ.
    unique, ambiguous = {}, 0
    for r in rooms:
        key = (r["scene"], r["room"], r["fingerprint"])
        if key in unique:
            ambiguous += unique[key]["emitters"] != r["emitters"]
            continue
        unique[key] = r
    rooms = list(unique.values())
    for r in rooms:
        for e in r["emitters"]:
            counts[e["category"]] += 1
    payload = json.dumps({"schema": CATALOG_SCHEMA, "version": CATALOG_VERSION, "rooms": rooms},
                         separators=(",", ":"), allow_nan=False).encode("ascii")
    summary = {"rooms": len(rooms), "emitters": dict(sorted(counts.items()))}
    if known:
        summary["precomputed_rooms"] = reused
    if ambiguous:
        summary["ambiguous_rooms"] = ambiguous
    return payload, summary


def prepare_scene_lights(*, data_root: Path, recipe: dict, romfs: Path, table: Path | None = None,
                         report: Callable[[str, str], None] = lambda *_: None) -> Path | None:
    """The catalog for this RomFS, generated once and reused while its receipt
    matches. A release table (integrity-checked by the caller) supplies the
    rooms it covers."""
    source = recipe.get("inputs", {}).get("romfs", {}).get("sha256")
    if not source:
        return None
    identity = {"catalog_version": CATALOG_VERSION, "generator_revision": GENERATOR_REVISION,
                "romfs_sha256": source}
    known = None
    if table is not None:
        try:
            from .precomputed_tables import read_scene_lights_table
        except ImportError:
            from precomputed_tables import read_scene_lights_table
        known = read_scene_lights_table(table)["rooms"]
        identity["table_sha256"] = hashlib.sha256(table.read_bytes()).hexdigest()
    key = hashlib.sha256(repr(sorted(identity.items())).encode("ascii")).hexdigest()
    directory = data_root / "cache" / "scene-lights" / key
    catalog = directory / "scene_lights.json"
    receipt_path = directory / "receipt.json"
    if catalog.is_file() and receipt_path.is_file():
        receipt = load_json_object(receipt_path)
        if receipt.get("source") == identity and \
                receipt.get("catalog_sha256") == hashlib.sha256(catalog.read_bytes()).hexdigest():
            return catalog
    report("scene_lights", "Cataloguing the rooms' light sources...")
    payload, summary = build_catalog(romfs, known)
    directory.mkdir(parents=True, exist_ok=True)
    atomic_write_bytes(catalog, payload)
    atomic_write_json(receipt_path, {"source": identity, "summary": summary,
                                     "catalog_sha256": hashlib.sha256(payload).hexdigest()})
    return catalog


if __name__ == "__main__":
    import sys
    payload, summary = build_catalog(Path(sys.argv[1]))
    Path(sys.argv[2]).write_bytes(payload)
    print(summary)
