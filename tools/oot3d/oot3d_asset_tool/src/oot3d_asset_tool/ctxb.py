from __future__ import annotations

import json
import math
from collections import Counter
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

from .binary import BinaryView, ParseError
from .cmb import (
    PICA_TEXTURE_ALPHA,
    PICA_TEXTURE_ETC1,
    PICA_TEXTURE_ETC1A4,
    PICA_TEXTURE_LUMINANCE,
    PICA_TEXTURE_LUMINANCE_ALPHA,
    PICA_TEXTURE_RGB,
    PICA_TEXTURE_RGBA,
    PICA_U8,
    PICA_UNSIGNED_BYTE_4_4,
    PICA_UNSIGNED_4BITS,
    PICA_UNSIGNED_SHORT_4444,
    PICA_UNSIGNED_SHORT_5551,
    PICA_UNSIGNED_SHORT_565,
    Texture,
    sanitize_identifier,
)
from .media_asset_audit import size_summary
from .romfs_inventory import sorted_counter
from .zar import ZarArchive

CTXB_HEADER_SIZE = 0x48
CTXB_TEX_CHUNK_OFFSET = 0x18
CTXB_PAYLOAD_OFFSET = 0x48

TEXTURE_FORMAT_NAMES = {
    PICA_TEXTURE_RGBA: "rgba",
    PICA_TEXTURE_RGB: "rgb",
    PICA_TEXTURE_ALPHA: "alpha",
    PICA_TEXTURE_LUMINANCE: "luminance",
    PICA_TEXTURE_LUMINANCE_ALPHA: "luminance_alpha",
    PICA_TEXTURE_ETC1: "etc1",
    PICA_TEXTURE_ETC1A4: "etc1a4",
}

DATA_TYPE_NAMES = {
    0: "none",
    PICA_U8: "u8",
    PICA_UNSIGNED_BYTE_4_4: "unsigned_byte_4_4",
    PICA_UNSIGNED_4BITS: "unsigned_4bits",
    PICA_UNSIGNED_SHORT_4444: "unsigned_short_4444",
    PICA_UNSIGNED_SHORT_5551: "unsigned_short_5551",
    PICA_UNSIGNED_SHORT_565: "unsigned_short_565",
}


@dataclass(frozen=True)
class CtxbTexture:
    source: str
    name: str
    size: int
    declared_size: int
    header_version: int
    header_word_0c: int
    tex_chunk_offset: int
    payload_offset: int
    tex_chunk_size: int
    texture_count: int
    payload_size: int
    flags: int
    width: int
    height: int
    texture_format: int
    data_type: int
    sampler_word: int
    reserved_tail: bytes
    data: bytes

    @property
    def format_pair(self) -> str:
        return format_pair_key(self.texture_format, self.data_type)

    @property
    def format_name(self) -> str:
        return TEXTURE_FORMAT_NAMES.get(self.texture_format, "unknown")

    @property
    def data_type_name(self) -> str:
        return DATA_TYPE_NAMES.get(self.data_type, "unknown")

    def to_texture(self) -> Texture:
        return Texture(
            index=0,
            name=sanitize_identifier(self.name),
            width=self.width,
            height=self.height,
            texture_format=self.texture_format,
            data_type=self.data_type,
            data=self.data,
        )

    def expected_payload_size(self) -> int | None:
        return expected_texture_payload_size(
            self.width,
            self.height,
            self.texture_format,
            self.data_type,
        )

    def decode_rgba16(self) -> bytes:
        return self.to_texture().as_rgba16()

    def summary(self, *, decode: bool = True) -> dict[str, object]:
        expected_payload = self.expected_payload_size()
        record: dict[str, object] = {
            "source": self.source,
            "name": self.name,
            "size": self.size,
            "declared_size": self.declared_size,
            "declared_size_match": self.declared_size == self.size,
            "header_version": self.header_version,
            "header_word_0c": self.header_word_0c,
            "tex_chunk_offset": self.tex_chunk_offset,
            "payload_offset": self.payload_offset,
            "tex_chunk_size": self.tex_chunk_size,
            "texture_count": self.texture_count,
            "payload_size": self.payload_size,
            "expected_payload_size": expected_payload,
            "expected_payload_size_match": (
                None if expected_payload is None else expected_payload == self.payload_size
            ),
            "flags": self.flags,
            "width": self.width,
            "height": self.height,
            "format": f"0x{self.texture_format:04x}",
            "format_name": self.format_name,
            "data_type": f"0x{self.data_type:04x}",
            "data_type_name": self.data_type_name,
            "format_pair": self.format_pair,
            "sampler_word": f"0x{self.sampler_word:08x}",
            "reserved_tail_zero": all(value == 0 for value in self.reserved_tail),
        }
        if decode:
            rgba16 = self.decode_rgba16()
            record["decoded_rgba16_size"] = len(rgba16)
            record["decoded_rgba16_size_match"] = len(rgba16) == self.width * self.height * 2
        return record


def parse_ctxb(data: bytes, source: str = "<memory>", *, name: str | None = None) -> CtxbTexture:
    view = BinaryView(data, source)
    if len(data) < CTXB_HEADER_SIZE:
        raise ParseError(f"{source}: CTXB is shorter than 0x{CTXB_HEADER_SIZE:x} bytes")
    if view.bytes(0, 4) != b"ctxb":
        raise ParseError(f"{source}: expected CTXB magic")

    declared_size = view.u32(0x04)
    header_version = view.u32(0x08)
    header_word_0c = view.u32(0x0C)
    tex_chunk_offset = view.u32(0x10)
    payload_offset = view.u32(0x14)
    if declared_size != len(data):
        raise ParseError(
            f"{source}: CTXB declared size 0x{declared_size:x} does not match file size 0x{len(data):x}"
        )
    if header_version != 1:
        raise ParseError(f"{source}: CTXB header version is {header_version}, expected 1")
    if header_word_0c != 0:
        raise ParseError(f"{source}: CTXB word_0c is 0x{header_word_0c:x}, expected 0")
    if tex_chunk_offset != CTXB_TEX_CHUNK_OFFSET:
        raise ParseError(
            f"{source}: CTXB tex chunk offset is 0x{tex_chunk_offset:x}, expected 0x{CTXB_TEX_CHUNK_OFFSET:x}"
        )
    if payload_offset != CTXB_PAYLOAD_OFFSET:
        raise ParseError(
            f"{source}: CTXB payload offset is 0x{payload_offset:x}, expected 0x{CTXB_PAYLOAD_OFFSET:x}"
        )
    if view.bytes(tex_chunk_offset, 4) != b"tex ":
        raise ParseError(f"{source}: expected TEX chunk at 0x{tex_chunk_offset:x}")

    tex_chunk_size = view.u32(0x1C)
    texture_count = view.u32(0x20)
    payload_size = view.u32(0x24)
    flags = view.u32(0x28)
    width = view.u16(0x2C)
    height = view.u16(0x2E)
    texture_format = view.u16(0x30)
    data_type = view.u16(0x32)
    sampler_word = view.u32(0x30)
    reserved_tail = view.bytes(0x34, CTXB_HEADER_SIZE - 0x34)

    if tex_chunk_size != 0x30:
        raise ParseError(f"{source}: TEX chunk size is 0x{tex_chunk_size:x}, expected 0x30")
    if texture_count != 1:
        raise ParseError(f"{source}: CTXB texture count is {texture_count}, expected 1")
    if payload_size != len(data) - payload_offset:
        raise ParseError(
            f"{source}: CTXB payload size 0x{payload_size:x} does not match remaining file size "
            f"0x{len(data) - payload_offset:x}"
        )
    if width <= 0 or height <= 0:
        raise ParseError(f"{source}: invalid CTXB dimensions {width}x{height}")

    source_name = name or texture_name_from_source(source)
    return CtxbTexture(
        source=source,
        name=source_name,
        size=len(data),
        declared_size=declared_size,
        header_version=header_version,
        header_word_0c=header_word_0c,
        tex_chunk_offset=tex_chunk_offset,
        payload_offset=payload_offset,
        tex_chunk_size=tex_chunk_size,
        texture_count=texture_count,
        payload_size=payload_size,
        flags=flags,
        width=width,
        height=height,
        texture_format=texture_format,
        data_type=data_type,
        sampler_word=sampler_word,
        reserved_tail=reserved_tail,
        data=view.bytes(payload_offset, payload_size),
    )


def audit_ctxb_textures(
    romfs_root: Path,
    output_path: Path | None = None,
    *,
    sample_limit: int = 100,
    include_records: bool = True,
) -> dict[str, object]:
    if not romfs_root.is_dir():
        raise ParseError(f"{romfs_root}: expected an extracted OOT3D RomFS directory")

    records: list[dict[str, object]] = []
    sample_records: list[dict[str, object]] = []
    parse_errors: list[dict[str, object]] = []
    decode_errors: list[dict[str, object]] = []
    zar_parse_errors: list[dict[str, object]] = []

    source_kind_counts: Counter[str] = Counter()
    top_level_counts: Counter[str] = Counter()
    parent_dir_counts: Counter[str] = Counter()
    format_pair_counts: Counter[str] = Counter()
    format_code_counts: Counter[str] = Counter()
    data_type_counts: Counter[str] = Counter()
    format_name_counts: Counter[str] = Counter()
    dimension_counts: Counter[str] = Counter()
    payload_size_counts: Counter[str] = Counter()
    flags_counts: Counter[str] = Counter()
    expected_payload_size_match_counts: Counter[str] = Counter()
    decoded_rgba16_status_counts: Counter[str] = Counter()
    decoded_rgba16_size_match_counts: Counter[str] = Counter()

    sizes: list[int] = []
    payload_sizes: list[int] = []
    decoded_rgba16_sizes: list[int] = []
    loose_ctxb_count = 0
    embedded_ctxb_count = 0
    parsed_ctxb_count = 0
    zar_archive_count = 0
    zar_with_ctxb_count = 0
    embedded_zar_file_count = 0

    def add_sample(record: dict[str, object]) -> None:
        if len(sample_records) < sample_limit:
            sample_records.append(record)

    def scan_ctxb(
        data: bytes,
        *,
        source: str,
        source_kind: str,
        rel_path: str,
        container_path: str | None = None,
        embedded_name: str | None = None,
    ) -> None:
        nonlocal parsed_ctxb_count

        source_kind_counts[source_kind] += 1
        parts = PurePosixPath(rel_path).parts
        top_level_counts[parts[0] if parts else "<root>"] += 1
        parent = PurePosixPath(rel_path).parent.as_posix()
        parent_dir_counts[parent if parent != "." else "<root>"] += 1

        try:
            ctxb = parse_ctxb(data, source, name=Path(PurePosixPath(rel_path).name).stem)
        except Exception as exc:
            error = {
                "source": source,
                "source_kind": source_kind,
                "path": rel_path,
                "container_path": container_path,
                "embedded_name": embedded_name,
                "reason": str(exc),
            }
            parse_errors.append(error)
            add_sample({**error, "status": "parse_error"})
            return

        parsed_ctxb_count += 1
        sizes.append(ctxb.size)
        payload_sizes.append(ctxb.payload_size)
        format_pair_counts[ctxb.format_pair] += 1
        format_code_counts[f"0x{ctxb.texture_format:04x}"] += 1
        data_type_counts[f"0x{ctxb.data_type:04x}"] += 1
        format_name_counts[ctxb.format_name] += 1
        dimension_counts[f"{ctxb.width}x{ctxb.height}"] += 1
        payload_size_counts[str(ctxb.payload_size)] += 1
        flags_counts[f"0x{ctxb.flags:08x}"] += 1

        expected_payload = ctxb.expected_payload_size()
        if expected_payload is None:
            expected_payload_status = "unknown"
        elif expected_payload == ctxb.payload_size:
            expected_payload_status = "matches"
        else:
            expected_payload_status = "mismatch"
        expected_payload_size_match_counts[expected_payload_status] += 1

        record = {
            "source": source,
            "source_kind": source_kind,
            "path": rel_path,
            "container_path": container_path,
            "embedded_name": embedded_name,
            **ctxb.summary(decode=False),
            "expected_payload_size_match_status": expected_payload_status,
        }

        try:
            rgba16 = ctxb.decode_rgba16()
        except Exception as exc:
            decoded_rgba16_status_counts["error"] += 1
            decode_error = {
                "source": source,
                "source_kind": source_kind,
                "path": rel_path,
                "container_path": container_path,
                "embedded_name": embedded_name,
                "format_pair": ctxb.format_pair,
                "reason": str(exc),
            }
            decode_errors.append(decode_error)
            record["decoded_rgba16_status"] = "error"
            record["decode_error"] = str(exc)
        else:
            decoded_rgba16_status_counts["decoded"] += 1
            decoded_rgba16_sizes.append(len(rgba16))
            decoded_size_match = len(rgba16) == ctxb.width * ctxb.height * 2
            decoded_rgba16_size_match_counts["matches" if decoded_size_match else "mismatch"] += 1
            record["decoded_rgba16_status"] = "decoded"
            record["decoded_rgba16_size"] = len(rgba16)
            record["decoded_rgba16_size_match"] = decoded_size_match

        if include_records:
            records.append(record)
        add_sample(record)

    for path in sorted(romfs_root.rglob("*.ctxb")):
        loose_ctxb_count += 1
        rel_path = path.relative_to(romfs_root).as_posix()
        scan_ctxb(
            path.read_bytes(),
            source=str(path),
            source_kind="loose",
            rel_path=rel_path,
        )

    for path in sorted(romfs_root.rglob("*.zar")):
        zar_archive_count += 1
        rel_path = path.relative_to(romfs_root).as_posix()
        try:
            archive = ZarArchive.from_path(path)
        except Exception as exc:
            zar_parse_errors.append({"path": rel_path, "reason": str(exc)})
            continue

        ctxb_files = [
            file
            for file in archive.files
            if file.type_name == "ctxb" or file.name.lower().endswith(".ctxb")
        ]
        if not ctxb_files:
            continue
        zar_with_ctxb_count += 1
        embedded_zar_file_count += len(archive.files)
        for file in ctxb_files:
            embedded_ctxb_count += 1
            embedded_rel = f"{rel_path}!{file.name}"
            scan_ctxb(
                archive.read_file(file),
                source=f"{path}!{file.name}",
                source_kind="embedded_zar",
                rel_path=embedded_rel,
                container_path=rel_path,
                embedded_name=file.name,
            )

    audit: dict[str, object] = {
        "format": "oot3d_ctxb_texture_audit_v1",
        "romfs_root": str(romfs_root),
        "ctxb_count": loose_ctxb_count + embedded_ctxb_count,
        "loose_ctxb_count": loose_ctxb_count,
        "embedded_ctxb_count": embedded_ctxb_count,
        "parsed_ctxb_count": parsed_ctxb_count,
        "parse_error_count": len(parse_errors),
        "decode_error_count": len(decode_errors),
        "zar_archive_count": zar_archive_count,
        "zar_with_ctxb_count": zar_with_ctxb_count,
        "embedded_zar_file_count": embedded_zar_file_count,
        "zar_parse_error_count": len(zar_parse_errors),
        "source_kind_counts": sorted_counter(source_kind_counts),
        "top_level_counts": sorted_counter(top_level_counts),
        "parent_dir_counts": sorted_counter(parent_dir_counts),
        "size_summary": size_summary(sizes),
        "payload_size_summary": size_summary(payload_sizes),
        "decoded_rgba16_size_summary": size_summary(decoded_rgba16_sizes),
        "format_pair_counts": sorted_counter(format_pair_counts),
        "format_code_counts": sorted_counter(format_code_counts),
        "data_type_counts": sorted_counter(data_type_counts),
        "format_name_counts": sorted_counter(format_name_counts),
        "dimension_counts": sorted_counter(dimension_counts),
        "payload_size_counts": sorted_counter(payload_size_counts),
        "flags_counts": sorted_counter(flags_counts),
        "expected_payload_size_match_counts": sorted_counter(
            expected_payload_size_match_counts
        ),
        "decoded_rgba16_status_counts": sorted_counter(decoded_rgba16_status_counts),
        "decoded_rgba16_size_match_counts": sorted_counter(
            decoded_rgba16_size_match_counts
        ),
        "parse_errors": parse_errors,
        "decode_errors": decode_errors,
        "zar_parse_errors": zar_parse_errors,
        "sample_records": sample_records,
        "records": records if include_records else [],
    }

    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(
            json.dumps(audit, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    return audit


def expected_texture_payload_size(
    width: int,
    height: int,
    texture_format: int,
    data_type: int,
) -> int | None:
    if width <= 0 or height <= 0:
        return None
    if texture_format == PICA_TEXTURE_ETC1 and data_type == 0:
        return etc_payload_size(width, height, has_alpha=False)
    if texture_format == PICA_TEXTURE_ETC1A4 and data_type == 0:
        return etc_payload_size(width, height, has_alpha=True)

    aligned_pixels = aligned_texel_count(width, height)
    if texture_format == PICA_TEXTURE_RGBA and data_type == PICA_U8:
        return aligned_pixels * 4
    if texture_format == PICA_TEXTURE_RGB and data_type == PICA_U8:
        return aligned_pixels * 3
    if texture_format == PICA_TEXTURE_ALPHA and data_type == PICA_U8:
        return aligned_pixels
    if texture_format == PICA_TEXTURE_LUMINANCE and data_type == PICA_U8:
        return aligned_pixels
    if texture_format == PICA_TEXTURE_LUMINANCE and data_type == PICA_UNSIGNED_4BITS:
        return aligned_pixels // 2
    if texture_format == PICA_TEXTURE_LUMINANCE_ALPHA and data_type == PICA_U8:
        return aligned_pixels * 2
    if texture_format == PICA_TEXTURE_LUMINANCE_ALPHA and data_type == PICA_UNSIGNED_BYTE_4_4:
        return aligned_pixels
    if texture_format == PICA_TEXTURE_RGB and data_type == PICA_UNSIGNED_SHORT_565:
        return aligned_pixels * 2
    if texture_format == PICA_TEXTURE_RGBA and data_type in {
        PICA_UNSIGNED_SHORT_4444,
        PICA_UNSIGNED_SHORT_5551,
    }:
        return aligned_pixels * 2
    return None


def aligned_texel_count(width: int, height: int) -> int:
    return aligned_dimension(width) * aligned_dimension(height)


def aligned_dimension(value: int) -> int:
    return max(8, math.ceil(value / 8) * 8)


def etc_payload_size(width: int, height: int, *, has_alpha: bool) -> int:
    tiles_x = max(1, math.ceil(width / 8))
    tiles_y = max(1, math.ceil(height / 8))
    subtile_size = 16 if has_alpha else 8
    return tiles_x * tiles_y * subtile_size * 4


def texture_name_from_source(source: str) -> str:
    tail = source.rsplit("!", 1)[-1].replace("\\", "/")
    return PurePosixPath(tail).stem or "texture_0"


def format_pair_key(texture_format: int, data_type: int) -> str:
    return f"0x{texture_format:04x}/0x{data_type:04x}"
