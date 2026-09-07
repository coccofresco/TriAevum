# Copyright Citra Emulator Project / Azahar Emulator Project
# Copyright TriAevum contributors
# SPDX-License-Identifier: GPL-2.0-or-later

"""Extract Forge inputs from a user-provided decrypted 3DS cartridge image.

The NCSD/NCCH layout and backwards-LZSS code path follow the documented CTR
container semantics implemented by Azahar.  This module deliberately handles
only already-decrypted images; it contains no key or decryption support.
"""

from __future__ import annotations

import hashlib
import shutil
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO


MEDIA_UNIT = 0x200
NCSD_HEADER_SIZE = 0x200
NCCH_HEADER_SIZE = 0x200
EXHEADER_SIZE = 0x800
EXEFS_HEADER_SIZE = 0x200
EXEFS_SECTION_COUNT = 8
COPY_CHUNK_SIZE = 4 * 1024 * 1024
MAX_DECOMPRESSED_CODE_SIZE = 512 * 1024 * 1024
SUPPORTED_ROM_SUFFIXES = frozenset({".3ds", ".cci"})


class CtrRomError(RuntimeError):
    pass


@dataclass(frozen=True)
class ExtractedFile:
    path: Path
    bytes: int
    sha256: str


@dataclass(frozen=True)
class ExtractedTitleInputs:
    container_kind: str
    partition_index: int
    program_id: int
    code: ExtractedFile
    exheader: ExtractedFile
    romfs: ExtractedFile

    def by_kind(self) -> dict[str, ExtractedFile]:
        return {
            "code": self.code,
            "exheader": self.exheader,
            "romfs": self.romfs,
        }


@dataclass(frozen=True)
class _NcchLayout:
    container_kind: str
    partition_index: int
    partition_base: int
    partition_end: int
    program_id: int
    compressed_code: bool
    code_offset: int
    code_size: int
    exheader_offset: int
    romfs_offset: int
    romfs_size: int


def is_supported_rom_path(path: Path) -> bool:
    return path.suffix.lower() in SUPPORTED_ROM_SUFFIXES


def _u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def _u64(data: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", data, offset)[0]


def _checked_region(
    offset: int, size: int, *, lower: int, upper: int, label: str
) -> tuple[int, int]:
    if offset < lower or size <= 0 or offset > upper or size > upper - offset:
        raise CtrRomError(f"{label} lies outside the selected title partition")
    return offset, offset + size


def _read_exact(stream: BinaryIO, offset: int, size: int, label: str) -> bytes:
    stream.seek(offset)
    data = stream.read(size)
    if len(data) != size:
        raise CtrRomError(f"the ROM is truncated while reading {label}")
    return data


def _parse_ncch(
    stream: BinaryIO,
    *,
    file_size: int,
    partition_base: int,
    partition_size: int,
    partition_index: int,
    container_kind: str,
) -> _NcchLayout:
    partition_end = partition_base + partition_size
    _checked_region(
        partition_base,
        NCCH_HEADER_SIZE,
        lower=0,
        upper=file_size,
        label=f"partition {partition_index} NCCH header",
    )
    if partition_end > file_size:
        raise CtrRomError(f"partition {partition_index} exceeds the ROM size")

    header = _read_exact(
        stream,
        partition_base,
        NCCH_HEADER_SIZE,
        f"partition {partition_index} NCCH header",
    )
    if header[0x100:0x104] != b"NCCH":
        raise CtrRomError(f"partition {partition_index} is not an NCCH title")
    if not (header[0x18D] & 0x02):
        raise CtrRomError(f"partition {partition_index} is not executable")
    if _u32(header, 0x180) == 0:
        raise CtrRomError(f"partition {partition_index} has no ExHeader")

    exheader_offset = partition_base + NCCH_HEADER_SIZE
    _checked_region(
        exheader_offset,
        EXHEADER_SIZE,
        lower=partition_base,
        upper=partition_end,
        label="ExHeader",
    )
    exheader = _read_exact(stream, exheader_offset, EXHEADER_SIZE, "ExHeader")

    exefs_units = _u32(header, 0x1A0)
    exefs_size_units = _u32(header, 0x1A4)
    if exefs_units == 0 or exefs_size_units == 0:
        raise CtrRomError(f"partition {partition_index} has no ExeFS")
    exefs_offset = partition_base + exefs_units * MEDIA_UNIT
    exefs_size = exefs_size_units * MEDIA_UNIT
    _, exefs_end = _checked_region(
        exefs_offset,
        exefs_size,
        lower=partition_base,
        upper=partition_end,
        label="ExeFS",
    )
    exefs_header = _read_exact(stream, exefs_offset, EXEFS_HEADER_SIZE, "ExeFS")

    code_offset: int | None = None
    code_size = 0
    for section_index in range(EXEFS_SECTION_COUNT):
        entry = section_index * 0x10
        name = exefs_header[entry : entry + 8].split(b"\0", 1)[0]
        if name != b".code":
            continue
        relative_offset = _u32(exefs_header, entry + 8)
        code_size = _u32(exefs_header, entry + 12)
        code_offset = exefs_offset + EXEFS_HEADER_SIZE + relative_offset
        _checked_region(
            code_offset,
            code_size,
            lower=exefs_offset + EXEFS_HEADER_SIZE,
            upper=exefs_end,
            label="ExeFS .code",
        )
        break
    if code_offset is None:
        raise CtrRomError(
            f"partition {partition_index} has no readable ExeFS .code section; "
            "the ROM may still be encrypted"
        )

    romfs_units = _u32(header, 0x1B0)
    romfs_size_units = _u32(header, 0x1B4)
    if romfs_units == 0 or romfs_size_units == 0:
        raise CtrRomError(f"partition {partition_index} has no RomFS")
    romfs_offset = partition_base + romfs_units * MEDIA_UNIT
    romfs_size = romfs_size_units * MEDIA_UNIT
    _checked_region(
        romfs_offset,
        romfs_size,
        lower=partition_base,
        upper=partition_end,
        label="RomFS",
    )
    if _read_exact(stream, romfs_offset, 4, "RomFS header") != b"IVFC":
        raise CtrRomError(
            "the selected title RomFS is not decrypted or has an invalid IVFC header"
        )

    return _NcchLayout(
        container_kind=container_kind,
        partition_index=partition_index,
        partition_base=partition_base,
        partition_end=partition_end,
        program_id=_u64(header, 0x118),
        compressed_code=bool(exheader[0x0D] & 1),
        code_offset=code_offset,
        code_size=code_size,
        exheader_offset=exheader_offset,
        romfs_offset=romfs_offset,
        romfs_size=romfs_size,
    )


def _find_title_layout(stream: BinaryIO, file_size: int) -> _NcchLayout:
    header = _read_exact(stream, 0, NCSD_HEADER_SIZE, "container header")
    magic = header[0x100:0x104]
    if magic == b"NCCH":
        return _parse_ncch(
            stream,
            file_size=file_size,
            partition_base=0,
            partition_size=file_size,
            partition_index=0,
            container_kind="NCCH",
        )
    if magic != b"NCSD":
        raise CtrRomError("the selected file is not a Nintendo 3DS NCSD/NCCH image")

    failures: list[str] = []
    for partition_index in range(8):
        entry = 0x120 + partition_index * 8
        partition_base = _u32(header, entry) * MEDIA_UNIT
        partition_size = _u32(header, entry + 4) * MEDIA_UNIT
        if partition_base == 0 or partition_size == 0:
            continue
        try:
            return _parse_ncch(
                stream,
                file_size=file_size,
                partition_base=partition_base,
                partition_size=partition_size,
                partition_index=partition_index,
                container_kind="NCSD",
            )
        except CtrRomError as exc:
            failures.append(str(exc))

    detail = "; ".join(failures) if failures else "the partition table is empty"
    raise CtrRomError(f"no usable decrypted application partition was found: {detail}")


def decompress_exefs_code(compressed: bytes) -> bytes:
    """Decode the backwards-LZSS representation used by compressed ExeFS code."""

    if len(compressed) < 8:
        raise CtrRomError("compressed ExeFS .code is too small")
    buffer_top_and_bottom = _u32(compressed, len(compressed) - 8)
    additional_size = _u32(compressed, len(compressed) - 4)
    decompressed_size = len(compressed) + additional_size
    if (
        decompressed_size < len(compressed)
        or decompressed_size > MAX_DECOMPRESSED_CODE_SIZE
    ):
        raise CtrRomError("compressed ExeFS .code declares an invalid output size")

    footer_size = (buffer_top_and_bottom >> 24) & 0xFF
    encoded_size = buffer_top_and_bottom & 0xFFFFFF
    if (
        footer_size < 8
        or footer_size > len(compressed)
        or encoded_size < footer_size
        or encoded_size > len(compressed)
    ):
        raise CtrRomError("compressed ExeFS .code has an invalid footer")

    index = len(compressed) - footer_size
    stop_index = len(compressed) - encoded_size
    output_index = decompressed_size
    output = bytearray(decompressed_size)
    output[: len(compressed)] = compressed

    while index > stop_index:
        index -= 1
        control = compressed[index]
        for _ in range(8):
            if index <= stop_index or output_index == 0:
                break
            if control & 0x80:
                if index < 2:
                    raise CtrRomError(
                        "compressed ExeFS .code back-reference is truncated"
                    )
                index -= 2
                segment = compressed[index] | (compressed[index + 1] << 8)
                segment_size = ((segment >> 12) & 0xF) + 3
                segment_offset = (segment & 0xFFF) + 2
                if output_index < segment_size:
                    raise CtrRomError("compressed ExeFS .code output underflow")
                for _ in range(segment_size):
                    source = output_index + segment_offset
                    if source >= len(output):
                        raise CtrRomError(
                            "compressed ExeFS .code back-reference is out of range"
                        )
                    output_index -= 1
                    output[output_index] = output[source]
            else:
                if index <= stop_index or output_index == 0:
                    raise CtrRomError("compressed ExeFS .code literal is truncated")
                index -= 1
                output_index -= 1
                output[output_index] = compressed[index]
            control = (control << 1) & 0xFF
    return bytes(output)


def _write_bytes(path: Path, data: bytes) -> ExtractedFile:
    path.write_bytes(data)
    return ExtractedFile(path.resolve(), len(data), hashlib.sha256(data).hexdigest())


def _copy_region(
    stream: BinaryIO, offset: int, size: int, destination: Path
) -> ExtractedFile:
    digest = hashlib.sha256()
    remaining = size
    stream.seek(offset)
    with destination.open("xb") as output:
        while remaining:
            chunk = stream.read(min(remaining, COPY_CHUNK_SIZE))
            if not chunk:
                raise CtrRomError("the ROM ended while extracting RomFS")
            output.write(chunk)
            digest.update(chunk)
            remaining -= len(chunk)
    return ExtractedFile(destination.resolve(), size, digest.hexdigest())


def extract_decrypted_rom(
    rom_path: Path,
    output_directory: Path,
    *,
    require_rom_suffix: bool = True,
) -> ExtractedTitleInputs:
    """Extract persistent Forge inputs into a new output directory."""

    source = rom_path.expanduser()
    if source.is_symlink():
        raise CtrRomError("the selected ROM must be a regular local file")
    source = source.resolve()
    if not source.is_file():
        raise CtrRomError(f"the selected ROM does not exist: {source}")
    if require_rom_suffix and not is_supported_rom_path(source):
        raise CtrRomError("Forge accepts decrypted ROMs with .3ds or .cci extension")

    output_directory = output_directory.expanduser().resolve()
    if output_directory.exists():
        raise CtrRomError(
            f"ROM extraction directory already exists: {output_directory}"
        )
    output_directory.parent.mkdir(parents=True, exist_ok=True)
    output_directory.mkdir()

    try:
        with source.open("rb") as stream:
            file_size = source.stat().st_size
            layout = _find_title_layout(stream, file_size)
            exheader_bytes = _read_exact(
                stream, layout.exheader_offset, EXHEADER_SIZE, "ExHeader"
            )
            code_bytes = _read_exact(
                stream, layout.code_offset, layout.code_size, "ExeFS .code"
            )
            if layout.compressed_code:
                code_bytes = decompress_exefs_code(code_bytes)

            exheader = _write_bytes(output_directory / "exheader.bin", exheader_bytes)
            code = _write_bytes(output_directory / "code.bin", code_bytes)
            romfs = _copy_region(
                stream,
                layout.romfs_offset,
                layout.romfs_size,
                output_directory / "romfs.bin",
            )
        return ExtractedTitleInputs(
            container_kind=layout.container_kind,
            partition_index=layout.partition_index,
            program_id=layout.program_id,
            code=code,
            exheader=exheader,
            romfs=romfs,
        )
    except BaseException:
        shutil.rmtree(output_directory, ignore_errors=True)
        raise


def _input_identity(extracted: ExtractedTitleInputs) -> str:
    digest = hashlib.sha256(b"triaevum-extracted-title-inputs-v1\0")
    for kind in ("code", "exheader", "romfs"):
        item = extracted.by_kind()[kind]
        digest.update(kind.encode("ascii"))
        digest.update(b"\0")
        digest.update(bytes.fromhex(item.sha256))
    return digest.hexdigest()


def _digest_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(COPY_CHUNK_SIZE):
            digest.update(chunk)
    return digest.hexdigest()


def publish_extracted_inputs(
    extracted: ExtractedTitleInputs, source_root: Path
) -> ExtractedTitleInputs:
    """Atomically retain an extracted title under its content identity."""

    source_root = source_root.expanduser().resolve()
    source_root.mkdir(parents=True, exist_ok=True)
    staging = extracted.code.path.parent
    if any(item.path.parent != staging for item in extracted.by_kind().values()):
        raise CtrRomError("extracted title inputs do not share one staging directory")

    destination = source_root / _input_identity(extracted)
    if destination.exists():
        if not destination.is_dir() or destination.is_symlink():
            raise CtrRomError(f"private title source path is invalid: {destination}")
        for kind, expected in extracted.by_kind().items():
            existing = destination / expected.path.name
            if (
                not existing.is_file()
                or existing.is_symlink()
                or existing.stat().st_size != expected.bytes
                or _digest_file(existing) != expected.sha256
            ):
                raise CtrRomError(
                    f"existing private {kind} input does not match its content identity"
                )
        shutil.rmtree(staging)
    else:
        staging.replace(destination)

    def retained(item: ExtractedFile) -> ExtractedFile:
        return ExtractedFile(
            path=(destination / item.path.name).resolve(),
            bytes=item.bytes,
            sha256=item.sha256,
        )

    return ExtractedTitleInputs(
        container_kind=extracted.container_kind,
        partition_index=extracted.partition_index,
        program_id=extracted.program_id,
        code=retained(extracted.code),
        exheader=retained(extracted.exheader),
        romfs=retained(extracted.romfs),
    )
