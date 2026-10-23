#!/usr/bin/env python3
"""Build the optional TopScreen CTXB payload override pack."""

from __future__ import annotations

import argparse
import struct
import zipfile
from pathlib import Path


# SingleScreen 3.0 UI atlas (Items shortcut column, HUD, D-pad, Ocarina
# arrows). TopScreen 2.1.1 is retired: none of its textures is imported.
SINGLESCREEN_PROFILE_TEXTURES = (
    ("custom_ui01.ctxb", "oot3d/topscreen/3.0/ui_atlas"),
)
SINGLESCREEN_ROMFS_PREFIXES = (
    "SingleScreen_3.0/EUR/Emulator/load/mods/0004000000033600/romfs/",
    "SingleScreen_3.0/EUR/3DS Hardware/luma/titles/0004000000033600/romfs/",
)
CTXB_HEADER_SIZE = 0x48
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


def build_texture_pack(*, singlescreen_archive: Path | None = None,
                       singlescreen_ui_atlas: bytes | None = None) -> bytes:
    """Build the O3TU v2 pack from the SingleScreen 3.0 UI atlas.

    singlescreen_ui_atlas is the custom_ui01.ctxb TriAevum ships (Forge, Switch
    staging); singlescreen_archive is a developer's local 3.0 zip. Only that
    atlas is read: never an executable patch or a TopScreen 2.1.1 file."""
    if (singlescreen_archive is None) == (singlescreen_ui_atlas is None):
        raise ValueError("provide exactly one SingleScreen 3.0 source")
    profile_textures: list[tuple[str, int, int, int, bytes]] = []

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

    if singlescreen_archive is not None:
        with zipfile.ZipFile(singlescreen_archive) as singlescreen:
            names = set(singlescreen.namelist())
            prefix = next((candidate for candidate in SINGLESCREEN_ROMFS_PREFIXES
                           if candidate + "menu/custom_ui01.ctxb" in names), None)
            if prefix is None:
                raise ValueError("archive has no supported EUR SingleScreen 3.0 RomFS")
            for texture, semantic in SINGLESCREEN_PROFILE_TEXTURES:
                parse_profile_texture(singlescreen.read(f"{prefix}menu/{texture}"), semantic)
    else:
        parse_profile_texture(singlescreen_ui_atlas, SINGLESCREEN_PROFILE_TEXTURES[0][1])

    output = bytearray(b"O3TU")
    output += struct.pack("<III", 2, 0, len(profile_textures))
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
    source.add_argument("--singlescreen-ui-atlas", type=Path,
                        help="custom_ui01.ctxb, as shipped in the repository")
    source.add_argument("--singlescreen-archive", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    output = build_texture_pack(
        singlescreen_archive=args.singlescreen_archive,
        singlescreen_ui_atlas=(args.singlescreen_ui_atlas.read_bytes()
                               if args.singlescreen_ui_atlas else None))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print(f"wrote {len(output)} bytes: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())