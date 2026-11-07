"""Bounded model/archive reader for offline catalogs; layouts are title-owned data.

Only encoded base mip bytes are exposed, never decoded pixels. A profile selects
one CMB version and explicit offsets; unknown versions fail rather than silently
producing an empty catalog. User ROM-derived outputs stay outside the repository.
"""
from __future__ import annotations

import struct
from pathlib import Path

try:
    from .water_textures import BITS_PER_PIXEL, _romfs_image_files
except ImportError:
    from water_textures import BITS_PER_PIXEL, _romfs_image_files

MAX_UNPACKED = 64 * 1024 * 1024


def unpack_lzs(data: bytes) -> bytes:
    if not data.startswith(b"LzS\1"):
        return data
    if len(data) < 16:
        raise ValueError("truncated LzS header")
    size, packed = struct.unpack_from("<II", data, 8)
    if packed != len(data) - 16 or not 0 < size <= MAX_UNPACKED:
        raise ValueError("invalid LzS extent")
    out, ring, cursor, write = bytearray(), bytearray(4096), 16, 0xFEE
    while len(out) < size:
        if cursor >= len(data):
            raise ValueError("truncated LzS flags")
        flags = data[cursor]
        cursor += 1
        for bit in range(8):
            if len(out) == size:
                break
            if flags & (1 << bit):
                if cursor >= len(data):
                    raise ValueError("truncated LzS literal")
                value = data[cursor]
                cursor += 1
                out.append(value)
                ring[write] = value
                write = (write + 1) & 4095
            else:
                if cursor + 2 > len(data):
                    raise ValueError("truncated LzS reference")
                a, b = data[cursor:cursor + 2]
                cursor += 2
                read, count = a | ((b & 0xF0) << 4), (b & 15) + 3
                if len(out) + count > size:
                    raise ValueError("oversized LzS reference")
                for _ in range(count):
                    value = ring[read]
                    read = (read + 1) & 4095
                    out.append(value)
                    ring[write] = value
                    write = (write + 1) & 4095
    return bytes(out)


def _u32(data: bytes, at: int) -> int:
    if at < 0 or at + 4 > len(data):
        raise ValueError("model read out of bounds")
    return struct.unpack_from("<I", data, at)[0]


def gar_models(data: bytes):
    if len(data) < 24 or data[:4] != b"GAR\2" or _u32(data, 4) != len(data):
        raise ValueError("unsupported GAR header")
    count = struct.unpack_from("<H", data, 10)[0]
    table, offsets = _u32(data, 16), _u32(data, 20)
    if table + count * 12 > len(data) or offsets + count * 4 > len(data):
        raise ValueError("truncated GAR tables")
    names = set()
    for index in range(count):
        record = table + index * 12
        size, start = _u32(data, record), _u32(data, offsets + index * 4)
        name_at = _u32(data, record + 8)
        end = data.find(b"\0", name_at)
        if end < 0:
            raise ValueError("unterminated GAR name")
        name = data[name_at:end].decode("ascii")
        if name in names or start + size > len(data):
            raise ValueError("invalid or duplicate GAR member")
        names.add(name)
        if name.endswith(".cmb"):
            yield name, data[start:start + size]


def cmb_textures(data: bytes, layout: dict):
    header = layout["header_bytes"]
    if len(data) < header or data[:4] != b"cmb ":
        raise ValueError("truncated CMB header")
    size, version = _u32(data, 4), _u32(data, 8)
    if version != layout["version"] or not header <= size <= len(data):
        raise ValueError("unsupported CMB version/extent")
    data = data[:size]
    chunk = _u32(data, layout["texture_chunk_offset"])
    texture_data = _u32(data, layout["texture_data_offset"])
    if data[chunk:chunk + 4] != b"tex ":
        raise ValueError("missing CMB texture chunk")
    count = _u32(data, chunk + 8)
    if chunk + 12 + count * 36 > len(data):
        raise ValueError("truncated CMB texture entries")
    for index in range(count):
        entry = chunk + 12 + index * 36
        extent, _, _, width, height, fmt, kind, relative = struct.unpack_from("<IHHHHHHI", data, entry)
        name = data[entry + 20:entry + 36].split(b"\0", 1)[0].decode("ascii")
        bits = BITS_PER_PIXEL.get((fmt, kind))
        if bits is None or texture_data == 0:
            continue  # external texture, or a format the renderer cannot hash
        base = ((max(8, width) + 7) & ~7) * ((max(8, height) + 7) & ~7) * bits // 8
        start = texture_data + relative
        if width == 0 or height == 0 or base > extent or start + base > len(data):
            raise ValueError("invalid CMB base mip extent")
        yield name, width, height, data[start:start + base]


def model_textures(path: str, packed: bytes, profile: dict):
    data = unpack_lzs(packed)
    if path.endswith(".zsi"):
        if data[:4].hex() != profile["zsi_magic"]:
            raise ValueError("unsupported ZSI header")
        cursor = 0
        while (at := data.find(b"cmb ", cursor)) >= 0:
            size = _u32(data, at + 4)
            yield from cmb_textures(data[at:], profile["cmb"])
            cursor = at + max(size, 4)
    elif path.endswith((".gar", ".gar.lzs")):
        for _, model in gar_models(data):
            yield from cmb_textures(model, profile["cmb"])
    else:
        raise ValueError("unsupported model container")


def validate_profile(profile: dict):
    try:
        cmb, rules = profile["cmb"], profile["water_rules"]
        valid = (isinstance(profile["title"], str) and bool(profile["title"])
                 and type(profile["revision"]) is int and 0 < profile["revision"] <= 0xFFFFFFFF
                 and bool(profile["directories"]) and all(isinstance(s, str) and s not in ("", ".", "..")
                     and not any(c in s for c in "/\\:") for s in profile["directories"])
                 and bool(profile["suffixes"]) and all(isinstance(s, str) and s for s in profile["suffixes"])
                 and len(profile["zsi_magic"]) == 8
                 and all(c in "0123456789abcdefABCDEF" for c in profile["zsi_magic"])
                 and all(type(cmb[k]) is int for k in ("version", "header_bytes", "texture_chunk_offset", "texture_data_offset"))
                 and 0 <= cmb["version"] <= 0xFFFFFFFF and 12 <= cmb["header_bytes"] <= 4096
                 and 0 <= cmb["texture_chunk_offset"] <= cmb["header_bytes"] - 4
                 and 0 <= cmb["texture_data_offset"] <= cmb["header_bytes"] - 4
                 and all(isinstance(s, str) and s and s.isascii() for s in rules["exclude"])
                 and all(type(r["mask"]) is int and 0 < r["mask"] <= 15 and bool(r["contains"])
                     and all(isinstance(s, str) and s and s.isascii() for s in r["contains"]) for r in rules["roles"]))
    except (KeyError, TypeError, AttributeError):
        valid = False
    if not valid:
        raise ValueError("invalid water catalog profile bounds")


def model_files(romfs: Path, profile: dict):
    validate_profile(profile)
    directories = tuple(profile["directories"])
    def wanted(path):
        return path.endswith(tuple(profile["suffixes"]))
    if romfs.is_dir():
        for directory in directories:
            for path in sorted((romfs / directory).glob("*")):
                if path.is_file() and wanted(path.name):
                    yield f"{directory}/{path.name}", path.read_bytes()
    else:
        for path, data in _romfs_image_files(romfs, directories):
            if wanted(path):
                yield path, data


def water_role(name: str, profile: dict) -> int:
    name = name.lower()
    rules = profile["water_rules"]
    if any(stem in name for stem in rules["exclude"]):
        return 0
    # Ordered semantic rules, no image statistics or per-scene exceptions.
    for rule in rules["roles"]:
        if any(stem in name for stem in rule["contains"]):
            return rule["mask"]
    return 0
