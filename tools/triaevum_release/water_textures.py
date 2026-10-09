"""Local water texture catalog for the renderer, from the user's RomFS.

The title draws its water with a small set of textures. Their identities (the
renderer's base-level content hash and size) and roles are read from the
model files (scene rooms in ZSI, scene and actor ZAR archives) and classified
by texture name. The catalog is private, ROM-derived data: it is generated on
the user's machine, never shipped. Format (text):

    triaevum-water-textures 1
    <content hash of the base level, 16 hex> <width> <height> <role mask>

Roles: 1 surface, 2 river, 4 waterfall, 8 underwater (a light layer on the
submerged ground and walls, not water itself). A texture shared by several
roles has several bits; the renderer decides by the draw's shape.
"""

from __future__ import annotations

import hashlib
import re
import struct
from pathlib import Path
from typing import Callable, Iterator

try:
    from .common import atomic_write_bytes, atomic_write_json, load_json_object
except ImportError:
    from common import atomic_write_bytes, atomic_write_json, load_json_object

CATALOG_VERSION = 1
CATALOG_HEADER = f"triaevum-water-textures {CATALOG_VERSION}"
ROLE_SURFACE = 1
ROLE_RIVER = 2
ROLE_WATERFALL = 4
ROLE_UNDERWATER = 8

NCCH_ROMFS_SUPERBLOCK_SIZE = 0x1000
INVALID_ROMFS_OFFSET = 0xFFFFFFFF

# Bits per pixel by CMB (PICA format, data type) enumerations.
BITS_PER_PIXEL = {
    (0x6752, 0x1401): 32, (0x6754, 0x1401): 24, (0x6752, 0x8034): 16, (0x6752, 0x8033): 16,
    (0x6754, 0x8363): 16, (0x6758, 0x1401): 16, (0x6758, 0x6760): 8, (0x6757, 0x1401): 8,
    (0x6756, 0x1401): 8, (0x6757, 0x6761): 4, (0x6756, 0x6761): 4, (0x675A, 0x0000): 4,
    (0x675B, 0x0000): 8,
}

# Texture names: water and its kinds; excluded are names that share a water
# prefix but are not water (Water Temple walls and floors, fountain walls,
# mist, bubbles, weeds, statues...).
WATER_NAME = re.compile(
    r"(water|wate$|riv|kawa|taki|idomizu|mizusoko|bmizu|hole_mizu|niwa_mizu|drag_mizu|rm_mizu|"
    r"ydan_mizu|wsea|hwat|sea0)", re.I)
EXCLUDED_NAME = re.compile(
    r"(kabe|yuka|hasira|_ten|iwa|emblem|futi|hari|toge|tunagi|_obj|ranma|_dg_|_bs_|kasumi|(?<![a-z])awa_|nawa|"
    r"angel|hikari|entran|mizukusa|mizuzou|sumi|huchi|fallmaster)", re.I)


def role_of(name: str) -> int:
    name = name.lower()
    if "taki" in name:
        return ROLE_WATERFALL
    if "uvwater" in name:
        return ROLE_UNDERWATER
    if re.search(r"riv|kawa", name):
        return ROLE_RIVER
    return ROLE_SURFACE


def water_role(name: str) -> int:
    if not WATER_NAME.search(name) or EXCLUDED_NAME.search(name):
        return 0
    return role_of(name)


def content_hash(data: bytes) -> int:
    """The renderer's texture content hash (HashNativeBytes): FNV-1a 64 with
    its offset basis 1469598103934665603."""
    value = 1469598103934665603
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def cmb_textures(data: bytes, offset: int = 0) -> Iterator[tuple[str, int, int, bytes]]:
    """(name, width, height, base level bytes) of a version-6 CMB's textures."""
    if data[offset:offset + 4] != b"cmb " or offset + 0x44 > len(data):
        return
    size, version = struct.unpack_from("<II", data, offset + 4)
    if version != 6 or size < 0x44 or offset + size > len(data):
        return
    texture_chunk, texture_data = struct.unpack_from("<I", data, offset + 0x2C)[0], \
        struct.unpack_from("<I", data, offset + 0x40)[0]
    chunk = offset + texture_chunk
    if data[chunk:chunk + 4] != b"tex ":
        raise ValueError("CMB texture chunk is missing")
    count = struct.unpack_from("<I", data, chunk + 8)[0]
    for index in range(count):
        entry = chunk + 0x0C + index * 0x24
        data_size, _, _, width, height, texture_format, data_type, relative = struct.unpack_from(
            "<IHHHHHHI", data, entry)
        name = data[entry + 0x14:entry + 0x24].split(b"\0", 1)[0].decode("ascii", "replace")
        bits = BITS_PER_PIXEL.get((texture_format, data_type))
        if bits is None:
            continue
        # The renderer hashes the base level as stored: 8x8 tiles.
        base = ((max(8, width) + 7) & ~7) * ((max(8, height) + 7) & ~7) * bits // 8
        start = offset + texture_data + relative
        if base > data_size or start + base > len(data):
            continue
        yield name, width, height, data[start:start + base]


def zsi_textures(data: bytes) -> Iterator[tuple[str, int, int, bytes]]:
    """Textures of the CMB models embedded in a room's ZSI."""
    search = 0
    while (offset := data.find(b"cmb ", search)) >= 0:
        search = offset + 1
        yield from cmb_textures(data, offset)


def zar_textures(data: bytes) -> Iterator[tuple[str, int, int, bytes]]:
    """Textures of the CMB models in a ZAR archive."""
    if data[:4] != b"ZAR\x01":
        return
    type_count, file_count, type_section, meta_section, data_section = struct.unpack_from("<HHIII", data, 8)

    def c_string(offset: int) -> str:
        return data[offset:data.index(b"\0", offset)].decode("ascii", "replace")

    cmb_files: set[int] = set()
    for type_index in range(type_count):
        typed_count, list_offset, name_offset = struct.unpack_from("<III", data, type_section + type_index * 0x10)
        if c_string(name_offset) == "cmb":
            cmb_files.update(struct.unpack_from(f"<{typed_count}I", data, list_offset))
    for index in range(file_count):
        size, name_offset = struct.unpack_from("<II", data, meta_section + index * 8)
        if index not in cmb_files and not c_string(name_offset).endswith(".cmb"):
            continue
        start = struct.unpack_from("<I", data, data_section + index * 4)[0]
        yield from cmb_textures(data[start:start + size])


def _romfs_image_files(image: Path, directories: tuple[str, ...]) -> Iterator[tuple[str, bytes]]:
    """Files directly under the named top-level directories of a raw-IVFC or
    Level-3 RomFS image."""
    with image.open("rb") as stream:
        level3 = NCCH_ROMFS_SUPERBLOCK_SIZE if stream.read(4) == b"IVFC" else 0
        stream.seek(level3)
        header = struct.unpack("<10I", stream.read(0x28))
        if header[0] != 0x28:
            raise ValueError("RomFS has no Level-3 header")
        stream.seek(level3 + header[3])
        directory_table = stream.read(header[4])
        stream.seek(level3 + header[7])
        file_table = stream.read(header[8])
        data_base = level3 + header[9]

        def directory(offset: int) -> tuple[int, int, int, str]:
            _, sibling, child, first_file, _, name_size = struct.unpack_from("<6I", directory_table, offset)
            return sibling, child, first_file, directory_table[offset + 24:offset + 24 + name_size].decode("utf-16-le")

        _, child, _, _ = directory(0)
        seen: set[int] = set()
        while child != INVALID_ROMFS_OFFSET and child not in seen:
            seen.add(child)
            sibling, _, first_file, name = directory(child)
            if name in directories:
                entry, files_seen = first_file, set()
                while entry != INVALID_ROMFS_OFFSET and entry not in files_seen:
                    files_seen.add(entry)
                    _, next_file, offset, size, _, name_size = struct.unpack_from("<IIQQII", file_table, entry)
                    file_name = file_table[entry + 32:entry + 32 + name_size].decode("utf-16-le")
                    stream.seek(data_base + offset)
                    yield f"{name}/{file_name}", stream.read(size)
                    entry = next_file
            child = sibling


def romfs_model_files(romfs: Path) -> Iterator[tuple[str, bytes]]:
    """Scene room ZSIs and scene/actor ZARs, from a RomFS image or an
    extracted RomFS directory."""
    wanted = lambda path: path.endswith(".zar") or (path.startswith("scene/") and path.endswith("_info.zsi"))
    if romfs.is_dir():
        for directory in ("scene", "actor"):
            for path in sorted((romfs / directory).glob("*")):
                relative = f"{directory}/{path.name}"
                if path.is_file() and wanted(relative):
                    yield relative, path.read_bytes()
        return
    for relative, data in _romfs_image_files(romfs, ("scene", "actor")):
        if wanted(relative):
            yield relative, data


def build_catalog(romfs: Path) -> tuple[bytes, dict]:
    roles: dict[tuple[int, int, int], int] = {}
    textures = models = 0
    for path, data in romfs_model_files(romfs):
        models += 1
        for name, width, height, base in (zsi_textures(data) if path.endswith(".zsi") else zar_textures(data)):
            textures += 1
            role = water_role(name)
            if role:
                key = (content_hash(base), width, height)
                roles[key] = roles.get(key, 0) | role
    lines = [CATALOG_HEADER] + [f"{h:016x} {w} {ht} {r}" for (h, w, ht), r in sorted(roles.items())]
    return ("\n".join(lines) + "\n").encode("ascii"), {
        "model_files": models, "textures": textures, "water_textures": len(roles)}


def prepare_water_textures(*, data_root: Path, recipe: dict, romfs: Path,
                           report: Callable[[str, str], None] = lambda *_: None) -> Path | None:
    """The catalog for this RomFS, generated once and reused while its receipt
    matches."""
    source = recipe.get("inputs", {}).get("romfs", {}).get("sha256")
    if not source:
        return None
    identity = {"catalog_version": CATALOG_VERSION, "romfs_sha256": source}
    key = hashlib.sha256(repr(sorted(identity.items())).encode("ascii")).hexdigest()
    directory = data_root / "cache" / "water" / key
    catalog = directory / "water_textures.txt"
    receipt_path = directory / "receipt.json"
    if catalog.is_file() and receipt_path.is_file():
        receipt = load_json_object(receipt_path)
        if receipt.get("source") == identity and \
                receipt.get("catalog_sha256") == hashlib.sha256(catalog.read_bytes()).hexdigest():
            return catalog
    report("water", "Cataloguing water textures...")
    payload, summary = build_catalog(romfs)
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
