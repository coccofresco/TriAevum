"""MM3D EUR room/material emitter facts from the user's assets (no pixels analysed).

Run with --code-bin, --romfs and --out. Scene ids come from the native EUR
resource table, not a hand-maintained scene list. CMB v10 supplies semantic
texture names, encoded base-level identities and rigid room geometry. Placement,
clustering, category budgets and gains reuse the existing scene-light mechanism.
This does not classify arbitrary fullbright materials or install PBR emission maps.
"""
from __future__ import annotations
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools/triaevum_release"))
import scene_lights as shared
from water_textures import BITS_PER_PIXEL, _romfs_image_files, content_hash


def decompress(data: bytes) -> bytes:
    if not data.startswith(b"LzS\1"):
        return data
    if len(data) < 16:
        raise ValueError("truncated LzS")
    size, packed = struct.unpack_from("<II", data, 8)
    if packed != len(data) - 16 or not 0 < size <= 64 * 1024 * 1024:
        raise ValueError("invalid LzS extent")
    out, ring, at, write = bytearray(), bytearray(4096), 16, 0xFEE
    while len(out) < size:
        if at >= len(data):
            raise ValueError("truncated LzS flags")
        flags, at = data[at], at + 1
        for bit in range(8):
            if len(out) == size:
                break
            if flags & (1 << bit):
                if at >= len(data):
                    raise ValueError("truncated LzS literal")
                values, at = [data[at]], at + 1
            else:
                if at + 2 > len(data):
                    raise ValueError("truncated LzS backreference")
                a, b = data[at:at + 2]
                at += 2
                read, values = a | ((b & 0xF0) << 4), None
                count = (b & 15) + 3
                if len(out) + count > size:
                    raise ValueError("oversized LzS backreference")
                for _ in range(count):
                    value = ring[read]
                    read = (read + 1) & 4095
                    out.append(value)
                    ring[write] = value
                    write = (write + 1) & 4095
            if values:
                out.extend(values)
                ring[write] = values[0]
                write = (write + 1) & 4095
    return bytes(out)


def room_fingerprint(data: bytes) -> str:
    if data[:4] != b"ZSI\x09" or len(data) < 24:
        raise ValueError("not a ZSI v9 room")
    header = bytearray(data[:16])
    for at in range(16, min(len(data), 272) - 7, 8):
        command = bytearray(data[at:at + 8])
        if command[0] in (1, 0xA):
            command[2:] = bytes(6)  # native relocation flag + pointer
        header.extend(command)
        if command[0] == 0x14:
            return f"{content_hash(header):016x}"
    raise ValueError("room command header has no End")


class Model(shared.Model):
    uv_attribute, shape_auto_flags, shape_primitives = 4, 0x122, 0x124

    def __init__(self, data: bytes, offset: int):
        if data[offset:offset + 4] != b"cmb " or offset + 0x48 > len(data):
            raise ValueError("not a CMB")
        size, version = struct.unpack_from("<II", data, offset + 4)
        if version != 10 or size < 0x48 or offset + size > len(data):
            raise ValueError("not a bounded CMB v10")
        self.data = data[offset:offset + size]
        d = self.data
        count, skl, _, mats, tex, sklm, _, vatr, indices, texture_data = self._unpack("<10I", 0x20)
        for chunk, magic in ((skl, b"skl "), (mats, b"mats"), (tex, b"tex "), (sklm, b"sklm"), (vatr, b"vatr")):
            if d[chunk:chunk + 4] != magic:
                raise ValueError("missing CMB chunk")
        self.textures, self.materials, self.uv_transforms = [], [], []
        for i in range(self._u32(tex + 8)):
            entry = tex + 12 + i * 36
            size, _, _, width, height, fmt, kind, relative = self._unpack("<IHHHHHHI", entry)
            bits = BITS_PER_PIXEL.get((fmt, kind), 0)
            base = ((max(8, width) + 7) & ~7) * ((max(8, height) + 7) & ~7) * bits // 8
            start = texture_data + relative
            pixels = d[start:start + base] if texture_data and 0 < base <= size and start + base <= len(d) else b""
            name = d[entry + 20:entry + 36].split(b"\0", 1)[0].decode("ascii", "strict")
            self.textures.append((name, width, height, fmt, kind, pixels))
        for i in range(self._u32(mats + 8)):
            base = mats + 12 + i * 0x16C
            textures = [self._unpack("<h", base + 0x10 + k * 0x18)[0] for k in range(3)]
            # Native blend fields retain their v6 offsets, unlike record size.
            additive = bool(self._u32(base + 0x138)) and self._unpack("<H", base + 0x13E)[0] == 1
            self.materials.append((textures, additive))
            su, sv, _, tu, tv = self._unpack("<5f", base + 0x5C)
            self.uv_transforms.append((su or 1, sv or 1, tu, tv))
        mshs, shp = sklm + self._u32(sklm + 8), sklm + self._u32(sklm + 12)
        if d[mshs:mshs + 4] != b"mshs" or d[shp:shp + 4] != b"shp ":
            raise ValueError("missing CMB meshes")
        self.meshes = [self._unpack("<HB", mshs + 16 + i * 12) for i in range(self._u32(mshs + 8))]
        self._shape_count, self._bones = self._u32(shp + 8), self._bone_transforms(skl, 0x2C)
        self._shp, self._vatr, self._indices, self._index_count = shp, vatr, indices, count
        self._shapes, self._uvs = {}, {}


def category(name: str, additive: bool) -> str | None:
    name = name.lower()
    if re.search(r"syoku|shoku|torch|taimatsu|rousoku|candle|water|mizu|shadow|kage", name):
        return None  # actor-owned flames / non-emitting effects
    if additive and re.search(r"door.*(?:glow|light)|(?:glow|light).*door", name):
        return "doorway"
    if re.search(r"(?:^|_)(?:halo|ran[td]an|lantern|lamp)(?:_|\d|$)", name):
        return "lamp"
    if re.search(r"(?:^|_)mado(?:_|\d|$)|window|(?:^|_)win(?:_|\d|$)|neon", name):
        return "window-lamp"
    if re.search(r"maho_star|mark_lt|mark_tam|magic|glow", name):
        return "magic-glow"
    if re.search(r"hikari|lightshaft|light_shaft", name):
        return "light-shaft"
    if re.search(r"fire|fir\d|yougan|magma|lava|hinoko|hono", name):
        return "fire-lava"
    return None


COLOUR = {"fire-lava": shared.FIRE_COLOUR, "lamp": shared.LAMP_TINT,
          "window-lamp": shared.LAMP_TINT, "light-shaft": shared.DAYLIGHT,
          "magic-glow": [0.5, 0.65, 1.0], "doorway": shared.DAYLIGHT}


def model_emitters(model: Model) -> list[dict]:
    groups, facts, result = defaultdict(list), {}, []
    for shape, material in model.meshes:
        if material < len(model.materials):
            groups[material].append(shape)
    for material in groups:
        indices, additive = model.materials[material]
        if indices and 0 <= indices[0] < len(model.textures):
            texture = model.textures[indices[0]]
            facts[material] = (texture, additive, category(texture[0], additive))
    window_glow = any(additive and cat == "window-lamp" for _, additive, cat in facts.values())
    for material, shapes in groups.items():
        if material not in facts:
            continue
        (name, _, _, _, _, pixels), additive, cat = facts[material]
        if not cat or not pixels or (cat == "window-lamp" and not additive and window_glow):
            continue
        triangles = [tuple(((shape, index), point) for index, point in tri)
                     for shape in shapes for tri in model.triangles(shape)]
        boxes = shared.objects(triangles, 10 if cat == "doorway" else 40)
        if cat == "doorway":
            emitters = [e for low, high, _ in boxes
                        if (e := shared.doorway_emitter(low, high, shared.model_centre(model))) is not None]
        else:
            emitters = shared.emitters_for(cat, COLOUR[cat], boxes)
        for emitter in emitters:
            emitter.update(category=cat, texture=name, surfaces=[f"{content_hash(pixels):016x}"],
                           material=material, surfaceRole="authored-room-emitter-or-fake-emitter",
                           independentEmission=True)
            result.append(emitter)
    return result


def scene_ids(code: bytes) -> dict[str, int]:
    # EUR table: native scene resources, 0x70-byte records. Guard its semantic
    # anchors rather than importing N64 scene ids or a hand-written scene list.
    base = 0x69B34C - 0x100000
    result = {}
    for index in range(0x75):
        at = base + index * 0x70
        name = code[at:at + 0x60].split(b"\0", 1)[0].decode("ascii", "strict")
        match = re.fullmatch(r"/scenes/(.+)_info\.zsi", name)
        if not match:
            raise ValueError("unsupported EUR native scene resource table")
        result.setdefault(match[1], index)
    if result.get("z2_yadoya") != 0x61 or result.get("z2_miturin") != 0x1B:
        raise ValueError("EUR scene-table guard failed")
    return result


def generate(code: bytes, files) -> dict:
    ids, rooms, rejected, unreferenced = scene_ids(code), [], [], []
    for name, packed in files:
        match = re.fullmatch(r"scenes/(.+)_(\d+)_info\.zsi", name)
        if not match:
            continue
        try:
            data = decompress(packed)
            if match[1] not in ids:
                unreferenced.append(dict(source=name, sourceSha256=hashlib.sha256(data).hexdigest(),
                                         reason="not-referenced-by-native-scene-table"))
                continue
            fingerprint = room_fingerprint(data)
            emitters, search = [], 0
            while (offset := data.find(b"cmb ", search)) >= 0:
                search = offset + 4
                # Reject an unsupported room as a whole, not a partial catalog.
                emitters += model_emitters(Model(data, offset))
            emitters = shared.consolidate(emitters)
            for emitter in emitters:
                emitter.pop("weight", None)
            rooms.append(dict(scene=ids[match[1]], room=int(match[2]), fingerprint=fingerprint,
                              source=name, sourceSha256=hashlib.sha256(data).hexdigest(), emitters=emitters))
        except (ValueError, struct.error, UnicodeError) as error:
            rejected.append(dict(source=name, reason=str(error)))
    return dict(schema="triaevum.scene-lights", version=1, title="mm3d", generatorRevision=1,
                fingerprintMode="mm3d-zsi-header-v1", rooms=rooms, rejected=rejected,
                unreferencedRooms=unreferenced)


def forge_profile(catalog: dict, generator_sha256: str) -> dict:
    """Publisher analysis only; native Forge verifies every room before reuse.

    Contains derived placements/identities, never model or texture bytes. New
    or altered rooms require publisher regeneration, not a user-side compiler.
    """
    if catalog.get("title") != "mm3d" or catalog.get("rejected") or not catalog.get("rooms"):
        raise ValueError("cannot publish incomplete MM3D room analysis")
    return {"version": 1, "title": "mm3d", "directory": "scenes",
            "filename_suffix": "_info.zsi", "index_separator": "_",
            "generator_sha256": generator_sha256,
            "catalog_json": json.dumps(catalog, sort_keys=True, separators=(",", ":"), ensure_ascii=True, allow_nan=False),
            "excluded_rooms": catalog.get("unreferencedRooms", [])}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--code-bin", type=Path, required=True)
    parser.add_argument("--romfs", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    catalog = generate(args.code_bin.read_bytes(), _romfs_image_files(args.romfs, ("scenes",)))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(catalog, indent=2) + "\n", encoding="utf-8")
    print(f"rooms={len(catalog['rooms'])} emitters={sum(len(r['emitters']) for r in catalog['rooms'])} rejected={len(catalog['rejected'])}")


if __name__ == "__main__":
    main()
