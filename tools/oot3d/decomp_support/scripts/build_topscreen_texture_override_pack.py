#!/usr/bin/env python3
"""Build the optional TopScreen CTXB payload override pack."""

from __future__ import annotations

import argparse
import struct
import zipfile
from pathlib import Path


LANGUAGES = (
    "00_JP_JAPANESE", "01_US_ENGLISH", "02_EU_ENGLISH",
    "03_EU_GERMAN", "04_EU_FRENCH", "05_US_FRENCH",
    "06_EU_SPANISH", "07_US_SPANISH", "08_EU_ITALIAN",
)
TEXTURES = ("menu_top_parts00.ctxb", "menu_cursor00.ctxb")
CTXB_HEADER_SIZE = 0x48
PROFILE_TEXTURES = (
    ("custom_menu00.ctxb", "oot3d/topscreen/2.1.1/menu_atlas"),
    ("custom_font00.ctxb", "oot3d/topscreen/2.1.1/font_atlas"),
)

PICA_FORMATS = {
    (0x6752, 0x1401): 0,
    (0x6754, 0x1401): 1,
    (0x6752, 0x8034): 2,
    (0x6754, 0x8363): 3,
    (0x6752, 0x8033): 4,
    (0x6758, 0x1401): 5,
    (0x6757, 0x1401): 7,
    (0x6756, 0x1401): 8,
    (0x6758, 0x6760): 9,
    (0x6757, 0x6761): 10,
    (0x6756, 0x6761): 11,
    (0x675A, 0x0000): 12,
    (0x675B, 0x0000): 13,
}
NCCH_ROMFS_SUPERBLOCK_SIZE = 0x1000
INVALID_ROMFS_OFFSET = 0xFFFFFFFF


def fnv1a64(data: bytes) -> int:
    value = 0xCBF29CE484222325
    for byte in data:
        value ^= byte
        value = (value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def read_level3_romfs_file(image: Path, relative_path: str) -> bytes:
    """Read one file from a raw-IVFC or Level-3 service-view RomFS."""
    with image.open("rb") as stream:
        stream.seek(0, 2)
        image_size = stream.tell()
        stream.seek(0)
        prefix = stream.read(4)
        level3_base = NCCH_ROMFS_SUPERBLOCK_SIZE if prefix == b"IVFC" else 0
        if level3_base + 0x28 > image_size:
            raise ValueError("truncated original RomFS image")
        stream.seek(level3_base)
        header_data = stream.read(0x28)
        header = struct.unpack("<10I", header_data)
        if header[0] != 0x28:
            raise ValueError("original RomFS has no Level-3 header")
        directory_metadata_offset = header[3]
        directory_metadata_size = header[4]
        file_metadata_offset = header[7]
        file_metadata_size = header[8]
        file_data_offset = header[9]

        def read_metadata(offset: int, table_offset: int, table_size: int,
                          fixed_size: int) -> bytes:
            if offset >= table_size or fixed_size > table_size - offset:
                raise ValueError("RomFS metadata offset is outside its table")
            stream.seek(level3_base + table_offset + offset)
            data = stream.read(fixed_size)
            if len(data) != fixed_size:
                raise ValueError("truncated RomFS metadata")
            return data

        def read_directory(offset: int) -> tuple[int, int, str]:
            fixed = read_metadata(
                offset, directory_metadata_offset, directory_metadata_size,
                24)
            _, sibling, child_directory, child_file, _, name_size = (
                struct.unpack("<6I", fixed))
            if name_size % 2 != 0 or 24 + name_size > (
                    directory_metadata_size - offset):
                raise ValueError("invalid RomFS directory name")
            name_data = stream.read(name_size)
            if len(name_data) != name_size:
                raise ValueError("truncated RomFS directory name")
            return sibling, child_directory, child_file, name_data.decode(
                "utf-16-le")

        def read_file(offset: int) -> tuple[int, int, int, str]:
            fixed = read_metadata(
                offset, file_metadata_offset, file_metadata_size, 32)
            _, sibling, data_offset, data_size, _, name_size = struct.unpack(
                "<IIQQII", fixed)
            if name_size % 2 != 0 or 32 + name_size > (
                    file_metadata_size - offset):
                raise ValueError("invalid RomFS file name")
            name_data = stream.read(name_size)
            if len(name_data) != name_size:
                raise ValueError("truncated RomFS file name")
            return sibling, data_offset, data_size, name_data.decode(
                "utf-16-le")

        parts = tuple(part for part in relative_path.split("/") if part)
        if not parts:
            raise ValueError("empty RomFS path")
        directory_offset = 0
        for component in parts[:-1]:
            _, child_directory, _, _ = read_directory(directory_offset)
            visited: set[int] = set()
            while child_directory != INVALID_ROMFS_OFFSET:
                if child_directory in visited:
                    raise ValueError("cyclic RomFS directory list")
                visited.add(child_directory)
                sibling, _, _, name = read_directory(child_directory)
                if name == component:
                    directory_offset = child_directory
                    break
                child_directory = sibling
            else:
                raise FileNotFoundError(relative_path)

        _, _, child_file, _ = read_directory(directory_offset)
        visited_files: set[int] = set()
        while child_file != INVALID_ROMFS_OFFSET:
            if child_file in visited_files:
                raise ValueError("cyclic RomFS file list")
            visited_files.add(child_file)
            sibling, data_offset, data_size, name = read_file(child_file)
            if name == parts[-1]:
                absolute_data_offset = level3_base + file_data_offset + data_offset
                if (absolute_data_offset > image_size or
                        data_size > image_size - absolute_data_offset):
                    raise ValueError("RomFS file payload is outside the image")
                stream.seek(absolute_data_offset)
                data = stream.read(data_size)
                if len(data) != data_size:
                    raise ValueError("truncated RomFS file payload")
                return data
            child_file = sibling
    raise FileNotFoundError(relative_path)


def build_texture_pack(*, archive: Path | None = None,
                       mod_romfs: Path | None = None,
                       original_romfs: Path | None = None,
                       original_romfs_image: Path | None = None) -> bytes:
    if (archive is None) == (mod_romfs is None):
        raise ValueError("provide exactly one replacement source")
    if (original_romfs is None) == (original_romfs_image is None):
        raise ValueError("provide exactly one original RomFS source")
    entries_by_original: dict[int, tuple[int, bytes, str]] = {}
    profile_textures: list[tuple[str, int, int, int, bytes]] = []

    def add_entry(original: bytes, replacement: bytes, label: str) -> None:
        if original[:CTXB_HEADER_SIZE] != replacement[:CTXB_HEADER_SIZE]:
            raise ValueError(f"CTXB header mismatch: {label}")
        original_payload = original[CTXB_HEADER_SIZE:]
        replacement_payload = replacement[CTXB_HEADER_SIZE:]
        if len(original_payload) != len(replacement_payload):
            raise ValueError(f"CTXB payload size mismatch: {label}")
        original_hash = fnv1a64(original_payload)
        replacement_hash = fnv1a64(replacement_payload)
        existing = entries_by_original.get(original_hash)
        if existing is not None:
            if (existing[0], existing[1]) != (replacement_hash,
                                               replacement_payload):
                raise ValueError(
                    "one original CTXB payload maps to conflicting "
                    f"replacements: {existing[2]} and {label}")
            return
        entries_by_original[original_hash] = (
            replacement_hash, replacement_payload, label)

    def parse_profile_texture(data: bytes, semantic: str) -> None:
        if len(data) < CTXB_HEADER_SIZE or data[:4] != b"ctxb":
            raise ValueError(f"invalid profile CTXB: {semantic}")
        width, height, raw_format, raw_type = struct.unpack_from(
            "<HHHH", data, 0x2C)
        native_format = PICA_FORMATS.get((raw_format, raw_type))
        if native_format is None:
            raise ValueError(
                f"unsupported profile CTXB format: {semantic}: "
                f"0x{raw_format:04x}/0x{raw_type:04x}")
        payload = data[CTXB_HEADER_SIZE:]
        if width == 0 or height == 0 or not payload:
            raise ValueError(f"empty profile CTXB: {semantic}")
        profile_textures.append(
            (semantic, width, height, native_format, payload))

    if archive is not None:
        archive_context = zipfile.ZipFile(archive)
        names = set(archive_context.namelist())

        def find_archive_romfs_prefix() -> str:
            candidates = (
                "TopScreenMod/romfs/",
                "TopScreen2.1.1/EUR/Emulator/load/mods/"
                "0004000000033600/romfs/",
                "TopScreen2.1.1/EUR/3DS Hardware/luma/titles/"
                "0004000000033600/romfs/",
            )
            for candidate in candidates:
                if candidate + "menu/01_US_ENGLISH/menu_cursor00.ctxb" in names:
                    return candidate
            raise ValueError("archive has no supported EUR TopScreen RomFS")

        archive_romfs_prefix = find_archive_romfs_prefix()
        read_replacement = lambda language, texture: archive_context.read(
            f"{archive_romfs_prefix}menu/{language}/{texture}")
        read_profile_texture = lambda texture: archive_context.read(
            f"{archive_romfs_prefix}menu/{texture}")
    else:
        archive_context = None
        read_replacement = lambda language, texture: (
            mod_romfs / "menu" / language / texture).read_bytes()
        read_profile_texture = lambda texture: (
            mod_romfs / "menu" / texture).read_bytes()
    try:
        for language in LANGUAGES:
            for texture in TEXTURES:
                replacement = read_replacement(language, texture)
                if original_romfs is not None:
                    original = (
                        original_romfs / "menu" / language /
                        texture).read_bytes()
                else:
                    original = read_level3_romfs_file(
                        original_romfs_image,
                        f"menu/{language}/{texture}")
                add_entry(original, replacement, f"{language}/{texture}")
        for texture, semantic in PROFILE_TEXTURES:
            parse_profile_texture(read_profile_texture(texture), semantic)
    finally:
        if archive_context is not None:
            archive_context.close()

    entries = [
        (original_hash, replacement_hash, payload)
        for original_hash, (replacement_hash, payload, _) in
        entries_by_original.items()
    ]

    output = bytearray(b"O3TU")
    output += struct.pack("<III", 2, len(entries), len(profile_textures))
    for original_hash, replacement_hash, payload in entries:
        output += struct.pack("<QQI", original_hash, replacement_hash, len(payload))
        output += payload
    for semantic, width, height, native_format, payload in profile_textures:
        encoded_semantic = semantic.encode("utf-8")
        output += struct.pack(
            "<IHHB3xIQ", len(encoded_semantic), width, height,
            native_format, len(payload), fnv1a64(payload))
        output += encoded_semantic
        output += payload
    return bytes(output)


def main() -> int:
    parser = argparse.ArgumentParser()
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--archive", type=Path)
    source.add_argument("--mod-romfs", type=Path)
    originals = parser.add_mutually_exclusive_group(required=True)
    originals.add_argument("--original-romfs", type=Path)
    originals.add_argument("--original-romfs-image", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    output = build_texture_pack(
        archive=args.archive, mod_romfs=args.mod_romfs,
        original_romfs=args.original_romfs,
        original_romfs_image=args.original_romfs_image)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print(f"wrote {len(output)} bytes: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
