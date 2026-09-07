#!/usr/bin/env python3
"""Audit native OOT3D origins for PICA state observed in a validation trace.

The emulator trace is treated only as a query source. A value is promotable to
the engine only when this report can point back to a native asset container or
to code.bin/static code evidence.
"""

from __future__ import annotations

import argparse
import json
import struct
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


IMPORTANT_SCALAR_REGISTERS = {
    "0x010",
    "0x04D",
    "0x04E",
    "0x0E1",
    "0x0E6",
    "0x0E8",
    "0x110",
    "0x111",
}


CODE_BIN_RUNTIME_BASE = 0x00100000

PICA_U8 = 0x1401
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

PICA_TEXTURE_FORMAT_NAMES = {
    PICA_TEXTURE_RGBA: "rgba",
    PICA_TEXTURE_RGB: "rgb",
    PICA_TEXTURE_ALPHA: "alpha",
    PICA_TEXTURE_LUMINANCE: "luminance",
    PICA_TEXTURE_LUMINANCE_ALPHA: "luminance_alpha",
}

PICA_DATA_TYPE_NAMES = {
    0: "none",
    PICA_U8: "u8",
    PICA_UNSIGNED_BYTE_4_4: "unsigned_byte_4_4",
    PICA_UNSIGNED_4BITS: "unsigned_4bits",
    PICA_UNSIGNED_SHORT_4444: "unsigned_short_4444",
    PICA_UNSIGNED_SHORT_5551: "unsigned_short_5551",
    PICA_UNSIGNED_SHORT_565: "unsigned_short_565",
}


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def parse_int(value: Any, fallback: int | None = None) -> int | None:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError:
            return fallback
    return fallback


def hex32(value: int) -> str:
    return f"0x{value & 0xFFFFFFFF:08X}"


def hex_addr(value: int) -> str:
    return f"0x{value:08X}"


def norm_reg(value: Any) -> str:
    parsed = parse_int(value)
    if parsed is None:
        return str(value)
    return f"0x{parsed:03X}"


def find_all(data: bytes, needle: bytes, limit: int = 16) -> list[int]:
    hits: list[int] = []
    start = 0
    while len(hits) < limit:
        index = data.find(needle, start)
        if index < 0:
            break
        hits.append(index)
        start = index + 1
    return hits


def read_le_u16(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 2 > len(data):
        raise ValueError(f"u16 read outside buffer at 0x{offset:X}")
    return struct.unpack_from("<H", data, offset)[0]


def read_le_u32(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise ValueError(f"u32 read outside buffer at 0x{offset:X}")
    return struct.unpack_from("<I", data, offset)[0]


def read_zar_c_string(data: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(data):
        raise ValueError(f"string read outside buffer at 0x{offset:X}")
    end = data.find(b"\x00", offset)
    if end < 0:
        raise ValueError(f"unterminated string at 0x{offset:X}")
    return data[offset:end].decode("ascii", errors="replace")


def expected_pica_payload_size(
    width: int, height: int, texture_format: int, data_type: int
) -> int | None:
    pixels = width * height
    if data_type == PICA_U8:
        if texture_format == PICA_TEXTURE_RGBA:
            return pixels * 4
        if texture_format == PICA_TEXTURE_RGB:
            return pixels * 3
        if texture_format in (PICA_TEXTURE_ALPHA, PICA_TEXTURE_LUMINANCE):
            return pixels
        if texture_format == PICA_TEXTURE_LUMINANCE_ALPHA:
            return pixels * 2
    if texture_format == PICA_TEXTURE_RGB and data_type == PICA_UNSIGNED_SHORT_565:
        return pixels * 2
    if texture_format == PICA_TEXTURE_RGBA and data_type in (
        PICA_UNSIGNED_SHORT_4444,
        PICA_UNSIGNED_SHORT_5551,
    ):
        return pixels * 2
    if texture_format == PICA_TEXTURE_LUMINANCE and data_type == PICA_UNSIGNED_4BITS:
        return (pixels + 1) // 2
    if (
        texture_format == PICA_TEXTURE_LUMINANCE_ALPHA
        and data_type == PICA_UNSIGNED_BYTE_4_4
    ):
        return pixels
    return None


def summarize_ctxb_payload(data: bytes) -> dict[str, Any]:
    if len(data) < 0x48:
        raise ValueError("CTXB shorter than 0x48-byte header")
    if data[:4] != b"ctxb":
        raise ValueError("missing CTXB magic")
    declared_size = read_le_u32(data, 0x04)
    header_version = read_le_u32(data, 0x08)
    header_word_0c = read_le_u32(data, 0x0C)
    tex_chunk_offset = read_le_u32(data, 0x10)
    payload_offset = read_le_u32(data, 0x14)
    tex_magic = data[tex_chunk_offset : tex_chunk_offset + 4]
    tex_chunk_size = read_le_u32(data, 0x1C)
    texture_count = read_le_u32(data, 0x20)
    payload_size = read_le_u32(data, 0x24)
    flags = read_le_u32(data, 0x28)
    width = read_le_u16(data, 0x2C)
    height = read_le_u16(data, 0x2E)
    texture_format = read_le_u16(data, 0x30)
    data_type = read_le_u16(data, 0x32)
    sampler_word = read_le_u32(data, 0x30)
    expected_payload = expected_pica_payload_size(
        width, height, texture_format, data_type
    )
    return {
        "magic": data[:4].decode("ascii", errors="replace"),
        "declared_size": declared_size,
        "declared_size_match": declared_size == len(data),
        "header_version": header_version,
        "header_word_0c": header_word_0c,
        "tex_chunk_offset": f"0x{tex_chunk_offset:X}",
        "payload_offset": f"0x{payload_offset:X}",
        "tex_magic": tex_magic.decode("ascii", errors="replace"),
        "tex_chunk_size": tex_chunk_size,
        "texture_count": texture_count,
        "payload_size": payload_size,
        "payload_size_match": payload_size == len(data) - payload_offset,
        "expected_payload_size": expected_payload,
        "expected_payload_size_match": (
            None if expected_payload is None else expected_payload == payload_size
        ),
        "flags": f"0x{flags:08X}",
        "width": width,
        "height": height,
        "format": f"0x{texture_format:04X}",
        "format_name": PICA_TEXTURE_FORMAT_NAMES.get(texture_format, "unknown"),
        "data_type": f"0x{data_type:04X}",
        "data_type_name": PICA_DATA_TYPE_NAMES.get(data_type, "unknown"),
        "format_pair": f"0x{texture_format:04X}/0x{data_type:04X}",
        "sampler_word": f"0x{sampler_word:08X}",
        "reserved_tail_zero": all(value == 0 for value in data[0x34:0x48]),
    }


def parse_zar_type_local_files(archive_path: Path, type_name_filter: str) -> list[dict[str, Any]]:
    data = archive_path.read_bytes()
    if data[:4] != b"ZAR\x01":
        raise ValueError("missing ZAR magic")
    archive_size = read_le_u32(data, 0x04)
    type_count = read_le_u16(data, 0x08)
    file_count = read_le_u16(data, 0x0A)
    type_section = read_le_u32(data, 0x0C)
    meta_section = read_le_u32(data, 0x10)
    data_section = read_le_u32(data, 0x14)
    if archive_size > len(data):
        raise ValueError(
            f"declared archive size 0x{archive_size:X} exceeds file length 0x{len(data):X}"
        )

    file_names: list[str] = []
    file_sizes: list[int] = []
    for file_index in range(file_count):
        meta_offset = meta_section + file_index * 8
        file_sizes.append(read_le_u32(data, meta_offset))
        name_offset = read_le_u32(data, meta_offset + 4)
        file_names.append(read_zar_c_string(data, name_offset))

    data_offsets = [
        read_le_u32(data, data_section + file_index * 4)
        for file_index in range(file_count)
    ]

    files: list[dict[str, Any]] = []
    for type_index in range(type_count):
        entry_offset = type_section + type_index * 0x10
        typed_count = read_le_u32(data, entry_offset)
        list_offset = read_le_u32(data, entry_offset + 4)
        name_offset = read_le_u32(data, entry_offset + 8)
        type_name = read_zar_c_string(data, name_offset)
        if type_name != type_name_filter:
            continue
        for local_index in range(typed_count):
            file_index = read_le_u32(data, list_offset + local_index * 4)
            if file_index >= file_count:
                raise ValueError(
                    f"type {type_name} references invalid file index {file_index}"
                )
            data_offset = data_offsets[file_index]
            file_size = file_sizes[file_index]
            summary: dict[str, Any] | None = None
            if type_name == "ctxb":
                summary = summarize_ctxb_payload(data[data_offset : data_offset + file_size])
            files.append(
                {
                    "type_name": type_name,
                    "type_index": type_index,
                    "type_local_index": local_index,
                    "file_index": file_index,
                    "name": file_names[file_index],
                    "size": file_size,
                    "data_offset": f"0x{data_offset:X}",
                    "ctxb_summary": summary,
                }
            )
    return files


def romfs_path_from_rom_path(romfs_root: Path | None, rom_path: str) -> Path | None:
    if romfs_root is None or not rom_path.startswith("rom:/"):
        return None
    relative = rom_path[len("rom:/") :].replace("/", "\\")
    return romfs_root / relative


def collect_kankyo_extracted_zar_ctxb_inventory(kankyo_root: Path | None) -> dict[str, Any]:
    requested_ids = list(range(0x44, 0x4C))
    base = {
        "available": False,
        "requested_ctxb_ids": [f"0x{value:02X}" for value in requested_ids],
        "requested_ctxb_id_min": requested_ids[0],
        "requested_ctxb_id_max": requested_ids[-1],
    }
    if kankyo_root is None:
        return {
            **base,
            "reason": "no --kankyo-root supplied",
        }
    if not kankyo_root.is_dir():
        return {
            **base,
            "root": str(kankyo_root),
            "reason": "kankyo root does not exist",
        }

    archives: list[dict[str, Any]] = []
    parse_errors: list[dict[str, str]] = []
    total_type_counts: Counter[str] = Counter()
    total_file_count = 0
    total_ctxb_count = 0
    max_file_count = 0
    max_file_index = -1
    max_ctxb_type_local_index = -1
    max_ctxb_count_per_archive = 0

    for archive_path in sorted(kankyo_root.glob("*.zar")):
        try:
            data = archive_path.read_bytes()
            if data[:4] != b"ZAR\x01":
                raise ValueError("missing ZAR magic")
            archive_size = read_le_u32(data, 0x04)
            type_count = read_le_u16(data, 0x08)
            file_count = read_le_u16(data, 0x0A)
            type_section = read_le_u32(data, 0x0C)
            meta_section = read_le_u32(data, 0x10)
            data_section = read_le_u32(data, 0x14)
            if archive_size > len(data):
                raise ValueError(
                    f"declared archive size 0x{archive_size:X} exceeds file length 0x{len(data):X}"
                )

            file_names: list[str] = []
            file_sizes: list[int] = []
            for file_index in range(file_count):
                meta_offset = meta_section + file_index * 8
                file_sizes.append(read_le_u32(data, meta_offset))
                name_offset = read_le_u32(data, meta_offset + 4)
                file_names.append(read_zar_c_string(data, name_offset))

            data_offsets = [
                read_le_u32(data, data_section + file_index * 4)
                for file_index in range(file_count)
            ]

            type_counts: Counter[str] = Counter()
            ctxb_files: list[dict[str, Any]] = []
            type_entries: list[dict[str, Any]] = []
            for type_index in range(type_count):
                entry_offset = type_section + type_index * 0x10
                typed_count = read_le_u32(data, entry_offset)
                list_offset = read_le_u32(data, entry_offset + 4)
                name_offset = read_le_u32(data, entry_offset + 8)
                type_name = read_zar_c_string(data, name_offset)
                file_indices = [
                    read_le_u32(data, list_offset + typed_index * 4)
                    for typed_index in range(typed_count)
                ]
                for file_index in file_indices:
                    if file_index >= file_count:
                        raise ValueError(
                            f"type {type_name} references invalid file index {file_index}"
                        )
                type_counts[type_name] += typed_count
                type_entries.append(
                    {
                        "type_index": type_index,
                        "type_name": type_name,
                        "typed_count": typed_count,
                        "file_indices": file_indices,
                    }
                )
                if type_name == "ctxb":
                    for local_index, file_index in enumerate(file_indices):
                        ctxb_files.append(
                            {
                                "ctxb_local_index": local_index,
                                "file_index": file_index,
                                "name": file_names[file_index],
                                "size": file_sizes[file_index],
                                "data_offset": f"0x{data_offsets[file_index]:X}",
                            }
                        )

            total_file_count += file_count
            total_type_counts.update(type_counts)
            total_ctxb_count += len(ctxb_files)
            max_file_count = max(max_file_count, file_count)
            if file_count:
                max_file_index = max(max_file_index, file_count - 1)
            max_ctxb_count_per_archive = max(max_ctxb_count_per_archive, len(ctxb_files))
            if ctxb_files:
                max_ctxb_type_local_index = max(
                    max_ctxb_type_local_index,
                    max(row["ctxb_local_index"] for row in ctxb_files),
                )

            archives.append(
                {
                    "name": archive_path.name,
                    "path": str(archive_path),
                    "archive_size": archive_size,
                    "type_count": type_count,
                    "file_count": file_count,
                    "type_counts": dict(sorted(type_counts.items())),
                    "ctxb_files": ctxb_files,
                    "type_entries": type_entries,
                }
            )
        except Exception as exc:  # pragma: no cover - diagnostic report path
            parse_errors.append({"path": str(archive_path), "error": str(exc)})

    direct_possible = any(
        archive.get("type_counts", {}).get("ctxb", 0) > requested_ids[-1]
        for archive in archives
    )
    return {
        **base,
        "available": True,
        "root": str(kankyo_root),
        "archive_count": len(archives),
        "parse_error_count": len(parse_errors),
        "parse_errors": parse_errors,
        "total_file_count": total_file_count,
        "total_type_counts": dict(sorted(total_type_counts.items())),
        "total_ctxb_file_count": total_ctxb_count,
        "max_file_count": max_file_count,
        "max_file_index": max_file_index,
        "max_ctxb_count_per_archive": max_ctxb_count_per_archive,
        "max_ctxb_type_local_index": max_ctxb_type_local_index,
        "direct_individual_archive_ctxb_index_possible": direct_possible,
        "interpretation": (
            "Requested native CTXB ids 0x44..0x4B are not addressable as local CTXB "
            "indices in any individual extracted kankyo ZAR; engine support must decode "
            "the runtime source-id/provider resource table instead of treating these ids "
            "as raw archive positions."
        ),
        "archives": archives,
    }


def source_path_from_model_source(source: str) -> tuple[str, str]:
    if "!" not in source:
        return source, ""
    path, entry = source.split("!", 1)
    return path, entry


def collect_native_source_files(summary: dict[str, Any], manifest: dict[str, Any] | None) -> list[dict[str, Any]]:
    sources: dict[str, dict[str, Any]] = {}

    def add(path_text: str, role: str, entry: str = "") -> None:
        if not path_text:
            return
        path = Path(path_text)
        key = str(path)
        current = sources.setdefault(
            key,
            {
                "path": key,
                "exists": path.is_file(),
                "roles": [],
                "entries": [],
            },
        )
        if role not in current["roles"]:
            current["roles"].append(role)
        if entry and entry not in current["entries"]:
            current["entries"].append(entry)

    room = summary.get("room", {})
    add(str(room.get("source", "")), "room_zsi_embedded_cmb")

    lighting = summary.get("native_pica_lighting", {})
    add(str(lighting.get("scene_zsi", "")), "scene_zsi_light_settings")
    add(str(lighting.get("room_zsi", "")), "room_zsi_light_settings")

    for model in summary.get("native_actor_visuals", {}).get("models", []):
        add(str(model.get("archive_path", "")), f"actor_archive:{model.get('actor_name', '')}",
            str(model.get("cmb_name", "")))

    for binding in summary.get("native_pica_trace_asset_bindings", {}).get("bindings", []):
        candidate = binding.get("resolved_candidate")
        if not isinstance(candidate, dict):
            continue
        path_text, entry = source_path_from_model_source(str(candidate.get("model_source", "")))
        add(path_text, f"resolved_draw_asset:{candidate.get('model_scope', '')}", entry)

    if manifest is not None:
        link_source = manifest.get("sources", {}).get("link_child", {})
        if isinstance(link_source, dict):
            conversion_manifest = Path(str(link_source.get("path", "")))
            if conversion_manifest.is_file():
                try:
                    conversion = read_json(conversion_manifest)
                    archive = conversion.get("source_archives", {}).get("model_archive", "")
                    if archive:
                        actor_root = Path(str(summary.get("asset_graph", {}).get("actor_archive_root", "")))
                        if actor_root:
                            add(str(actor_root / archive), "link_child_model_archive",
                                str(conversion.get("source_archives", {}).get("model_cmb", "")))
                except Exception as exc:  # pragma: no cover - diagnostic only
                    add(str(conversion_manifest), f"link_child_conversion_manifest_unreadable:{exc}")

    return sorted(sources.values(), key=lambda item: item["path"])


def collect_binding_provenance(summary: dict[str, Any]) -> dict[str, Any]:
    bindings = summary.get("native_pica_trace_asset_bindings", {}).get("bindings", [])
    rows: list[dict[str, Any]] = []
    status_counts: Counter[str] = Counter()
    texture_status_counts: Counter[str] = Counter()
    for binding in bindings:
        candidate = binding.get("resolved_candidate")
        if not isinstance(candidate, dict):
            continue
        trace_textures = [
            texture
            for texture in binding.get("trace_enabled_textures", [])
            if isinstance(texture, dict) and texture.get("enabled")
        ]
        trace_texture = trace_textures[0] if trace_textures else {}
        source_path, source_entry = source_path_from_model_source(str(candidate.get("model_source", "")))
        texture_status = "untextured_or_no_trace_texture"
        if candidate.get("trace_texture_format_mapping_available"):
            texture_status = (
                "format_confirmed_in_native_asset"
                if candidate.get("native_texture_format_matches_trace")
                else "format_mismatch_needs_asset_or_stage_origin"
            )
        elif candidate.get("trace_texture_format", -1) >= 0:
            texture_status = "trace_format_mapping_not_known"

        status_counts[str(binding.get("status", ""))] += 1
        texture_status_counts[texture_status] += 1
        rows.append(
            {
                "trace_draw_index": binding.get("trace_draw_index"),
                "status": binding.get("status"),
                "texture_status": texture_status,
                "model_scope": candidate.get("model_scope"),
                "model_name": candidate.get("model_name"),
                "source_file": source_path,
                "source_entry": source_entry,
                "batch_index": candidate.get("batch_index"),
                "shape_index": candidate.get("shape_index"),
                "material_index": candidate.get("material_index"),
                "texture_index": candidate.get("texture_index"),
                "texture_name": candidate.get("texture_name"),
                "trace_texture_format": candidate.get("trace_texture_format_name"),
                "trace_texture_width": trace_texture.get("width"),
                "trace_texture_height": trace_texture.get("height"),
                "trace_texture_unit": trace_texture.get("unit"),
                "trace_texture_address": trace_texture.get("address"),
                "trace_vertex_count": binding.get("trace_vertex_count"),
                "trace_mapped_native_format": {
                    "texture_format": candidate.get("trace_mapped_native_cmb_texture_format"),
                    "data_type": candidate.get("trace_mapped_native_cmb_texture_data_type"),
                },
                "native_texture_format": {
                    "texture_format": candidate.get("native_cmb_texture_format"),
                    "data_type": candidate.get("native_cmb_texture_data_type"),
                },
                "native_texture_width": candidate.get("texture_width"),
                "native_texture_height": candidate.get("texture_height"),
                "native_vertex_count": candidate.get("vertex_count"),
                "native_material_raw_fnv1a64": candidate.get("native_material_raw_fnv1a64"),
                "texture_env_selected_stage_index": candidate.get("texture_env_selected_stage_index"),
                "origin_class": (
                    "native_cmb_material_and_texture"
                    if texture_status == "format_confirmed_in_native_asset"
                    else "native_container_candidate_requires_deeper_stage_or_asset_lookup"
                ),
            }
        )
    return {
        "binding_count": len(rows),
        "status_counts": dict(status_counts),
        "texture_status_counts": dict(texture_status_counts),
        "bindings": rows,
    }


def flatten_archive_texture_catalog(summary: dict[str, Any]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    catalog = summary.get("native_archive_texture_catalog", {})
    if not isinstance(catalog, dict):
        return rows
    for archive in catalog.get("archives", []):
        if not isinstance(archive, dict):
            continue
        archive_path = str(archive.get("archive_path", ""))
        source_refs = archive.get("source_refs", [])
        for cmb in archive.get("cmbs", []):
            if not isinstance(cmb, dict) or cmb.get("parse_status") != "parsed":
                continue
            for texture in cmb.get("textures", []):
                if not isinstance(texture, dict):
                    continue
                rows.append(
                    {
                        "archive_path": archive_path,
                        "source_refs": source_refs,
                        "cmb_entry_index": cmb.get("entry_index"),
                        "cmb_entry_name": cmb.get("entry_name"),
                        "cmb_entry_type": cmb.get("entry_type"),
                        "texture_index": texture.get("texture_index"),
                        "texture_name": texture.get("texture_name"),
                        "width": texture.get("width"),
                        "height": texture.get("height"),
                        "texture_format": texture.get("texture_format"),
                        "data_type": texture.get("data_type"),
                        "format_name": texture.get("format_name"),
                        "origin_class": "native_resolved_zar_cmb_texture_catalog",
                    }
                )
    for model in catalog.get("loaded_models", []):
        if not isinstance(model, dict) or model.get("parse_status") != "parsed":
            continue
        source_path = str(model.get("source_path", ""))
        for texture in model.get("textures", []):
            if not isinstance(texture, dict):
                continue
            rows.append(
                {
                    "archive_path": source_path,
                    "source_refs": [
                        {
                            "source_kind": model.get("source_kind"),
                            "source": model.get("source"),
                            "source_entry": model.get("source_entry"),
                        }
                    ],
                    "cmb_entry_index": -1,
                    "cmb_entry_name": model.get("source_entry"),
                    "cmb_entry_type": "cmb",
                    "texture_index": texture.get("texture_index"),
                    "texture_name": texture.get("texture_name"),
                    "width": texture.get("width"),
                    "height": texture.get("height"),
                    "texture_format": texture.get("texture_format"),
                    "data_type": texture.get("data_type"),
                    "format_name": texture.get("format_name"),
                    "origin_class": "native_loaded_cmb_texture_catalog",
                }
            )
    return rows


def flatten_catalog_draw_primitives(summary: dict[str, Any]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    catalog = summary.get("native_archive_texture_catalog", {})
    if not isinstance(catalog, dict):
        return rows

    def append_rows(
        source_path: str,
        source_refs: Any,
        cmb_entry_index: Any,
        cmb_entry_name: Any,
        primitive_rows: Any,
        origin_class: str,
    ) -> None:
        if not isinstance(primitive_rows, list):
            return
        for primitive in primitive_rows:
            if not isinstance(primitive, dict):
                continue
            texture = primitive.get("primary_texture", {})
            if not isinstance(texture, dict) or not texture.get("available"):
                continue
            rows.append(
                {
                    "archive_path": source_path,
                    "source_refs": source_refs,
                    "cmb_entry_index": cmb_entry_index,
                    "cmb_entry_name": cmb_entry_name,
                    "mesh_index": primitive.get("mesh_index"),
                    "shape_index": primitive.get("shape_index"),
                    "material_index": primitive.get("material_index"),
                    "primitive_index": primitive.get("primitive_index"),
                    "skinning_mode": primitive.get("skinning_mode"),
                    "vertex_count": primitive.get("vertex_count"),
                    "triangle_count": primitive.get("triangle_count"),
                    "texture_index": texture.get("resolved_texture_index", texture.get("texture_index")),
                    "texture_name": texture.get("texture_name"),
                    "width": texture.get("width"),
                    "height": texture.get("height"),
                    "texture_format": texture.get("texture_format"),
                    "data_type": texture.get("data_type"),
                    "format_name": texture.get("format_name"),
                    "origin_class": origin_class,
                }
            )

    for archive in catalog.get("archives", []):
        if not isinstance(archive, dict):
            continue
        archive_path = str(archive.get("archive_path", ""))
        source_refs = archive.get("source_refs", [])
        for cmb in archive.get("cmbs", []):
            if not isinstance(cmb, dict) or cmb.get("parse_status") != "parsed":
                continue
            append_rows(
                archive_path,
                source_refs,
                cmb.get("entry_index"),
                cmb.get("entry_name"),
                cmb.get("draw_primitives"),
                "native_resolved_zar_cmb_draw_primitive_catalog",
            )
    for model in catalog.get("loaded_models", []):
        if not isinstance(model, dict) or model.get("parse_status") != "parsed":
            continue
        source_path = str(model.get("source_path", ""))
        append_rows(
            source_path,
            [
                {
                    "source_kind": model.get("source_kind"),
                    "source": model.get("source"),
                    "source_entry": model.get("source_entry"),
                }
            ],
            -1,
            model.get("source_entry"),
            model.get("draw_primitives"),
            "native_loaded_cmb_draw_primitive_catalog",
        )
    return rows


def same_path(left: str, right: str) -> bool:
    if not left or not right:
        return False
    return str(Path(left)).lower() == str(Path(right)).lower()


def collect_mismatch_native_texture_search(
    summary: dict[str, Any], binding_provenance: dict[str, Any]
) -> dict[str, Any]:
    catalog_rows = flatten_archive_texture_catalog(summary)
    primitive_rows = flatten_catalog_draw_primitives(summary)
    mismatch_rows = [
        row
        for row in binding_provenance.get("bindings", [])
        if row.get("texture_status") == "format_mismatch_needs_asset_or_stage_origin"
    ]
    searches: list[dict[str, Any]] = []
    status_counts: Counter[str] = Counter()

    for row in mismatch_rows:
        target_format = row.get("trace_mapped_native_format", {}).get("texture_format")
        target_data_type = row.get("trace_mapped_native_format", {}).get("data_type")
        target_width = row.get("trace_texture_width")
        target_height = row.get("trace_texture_height")
        target_vertex_count = row.get("trace_vertex_count")
        matches = [
            texture
            for texture in catalog_rows
            if texture.get("texture_format") == target_format
            and texture.get("data_type") == target_data_type
            and texture.get("width") == target_width
            and texture.get("height") == target_height
        ]
        same_archive_matches = [
            texture for texture in matches if same_path(str(texture.get("archive_path", "")), str(row.get("source_file", "")))
        ]
        primitive_matches = [
            primitive
            for primitive in primitive_rows
            if primitive.get("texture_format") == target_format
            and primitive.get("data_type") == target_data_type
            and primitive.get("width") == target_width
            and primitive.get("height") == target_height
            and primitive.get("vertex_count") == target_vertex_count
        ]
        same_archive_primitive_matches = [
            primitive
            for primitive in primitive_matches
            if same_path(str(primitive.get("archive_path", "")), str(row.get("source_file", "")))
        ]
        if same_archive_matches:
            status = "exact_native_texture_found_in_same_archive"
        elif matches:
            status = "exact_native_texture_found_in_other_resolved_archive"
        else:
            status = "exact_native_texture_not_found_in_resolved_archive_catalog"
        if same_archive_primitive_matches:
            draw_owner_status = "exact_native_texture_and_vertex_owner_found_in_same_archive"
        elif same_archive_matches:
            draw_owner_status = "same_source_texture_found_but_no_same_source_vertex_owner_match"
        elif primitive_matches:
            draw_owner_status = "exact_native_texture_and_vertex_owner_found_in_other_resolved_archive"
        else:
            draw_owner_status = "exact_native_texture_found_but_no_vertex_owner_match"
        status_counts[status] += 1
        searches.append(
            {
                "trace_draw_index": row.get("trace_draw_index"),
                "source_file": row.get("source_file"),
                "source_entry": row.get("source_entry"),
                "resolved_texture_name": row.get("texture_name"),
                "trace_texture_query": {
                    "format": row.get("trace_texture_format"),
                    "width": target_width,
                    "height": target_height,
                    "vertex_count": target_vertex_count,
                    "native_texture_format": target_format,
                    "native_data_type": target_data_type,
                    "address": row.get("trace_texture_address"),
                },
                "status": status,
                "draw_owner_status": draw_owner_status,
                "same_archive_match_count": len(same_archive_matches),
                "resolved_archive_match_count": len(matches),
                "same_archive_primitive_owner_match_count": len(same_archive_primitive_matches),
                "resolved_primitive_owner_match_count": len(primitive_matches),
                "same_archive_matches": same_archive_matches[:16],
                "resolved_archive_matches": matches[:16],
                "same_archive_primitive_owner_matches": same_archive_primitive_matches[:16],
                "resolved_primitive_owner_matches": primitive_matches[:16],
                "origin_class": "native_archive_catalog_query_from_trace_mismatch",
            }
        )

    return {
        "catalog_texture_count": len(catalog_rows),
        "catalog_draw_primitive_count": len(primitive_rows),
        "mismatch_count": len(mismatch_rows),
        "status_counts": dict(status_counts),
        "draw_owner_status_counts": dict(Counter(row["draw_owner_status"] for row in searches)),
        "searches": searches,
    }


def collect_lighting_provenance(summary: dict[str, Any]) -> dict[str, Any]:
    lighting = summary.get("native_pica_lighting", {})
    records = []
    active_setup = lighting.get("active_setup_index")
    for record in lighting.get("light_settings", []):
        if record.get("setup_index") != active_setup:
            continue
        actor_packet = record.get("native_actor_vs_light_packet_color_candidate", {})
        env_settings = record.get("native_env_light_settings", {})
        records.append(
            {
                "source_file": lighting.get("scene_zsi"),
                "source_kind": lighting.get("source_kind"),
                "setup_index": record.get("setup_index"),
                "record_index": record.get("index"),
                "record_offset": record.get("offset"),
                "entry_size": record.get("entry_size"),
                "layout": record.get("layout"),
                "env_light_settings_available": record.get("native_env_light_settings_available"),
                "env_light_settings": env_settings,
                "env_light_settings_record_sources": {
                    "ambient_color": {
                        "offset": "0x00",
                        "source": "oot3d_zsi_light_settings_record_0x1c.env_light_settings_bgr_u8",
                    },
                    "light0_direction": {
                        "offset": "0x03",
                        "source": "oot3d_zsi_light_settings_record_0x1c.signed_vec3",
                    },
                    "light0_color": {
                        "offset": "0x06",
                        "source": "oot3d_zsi_light_settings_record_0x1c.env_light_settings_bgr_u8",
                    },
                    "light1_direction": {
                        "offset": "0x09",
                        "source": "oot3d_zsi_light_settings_record_0x1c.signed_vec3",
                    },
                    "light1_color": {
                        "offset": "0x0C",
                        "source": "oot3d_zsi_light_settings_record_0x1c.env_light_settings_bgr_u8",
                    },
                },
                "actor_vs_packet_candidate_available": record.get(
                    "native_actor_vs_light_packet_color_candidate_available"
                ),
                "actor_vs_packet_color_offsets": {
                    "ambient": actor_packet.get("ambient_color_source"),
                    "diffuse0": actor_packet.get("diffuse0_color_source"),
                    "diffuse1": actor_packet.get("diffuse1_color_source"),
                },
                "actor_vs_packet_colors": {
                    "ambient": actor_packet.get("ambient_color"),
                    "diffuse0": actor_packet.get("diffuse0_color"),
                    "diffuse1": actor_packet.get("diffuse1_color"),
                },
                "origin_class": "native_zsi_light_settings_record",
            }
        )
    return {
        "available": bool(lighting.get("available")),
        "decoded_from_native_zsi": bool(lighting.get("decoded_from_native_zsi")),
        "scene_zsi": lighting.get("scene_zsi"),
        "active_setup_index": active_setup,
        "active_record_count": len(records),
        "records": records,
    }


def collect_scalar_values(trace: dict[str, Any]) -> list[dict[str, Any]]:
    by_register: dict[str, Counter[int]] = defaultdict(Counter)
    names: dict[str, str] = {}
    for write in trace.get("writes", []):
        reg = norm_reg(write.get("cmd_id"))
        if reg not in IMPORTANT_SCALAR_REGISTERS:
            continue
        value = parse_int(write.get("value"))
        if value is None:
            continue
        by_register[reg][value] += 1
        names[reg] = str(write.get("name", ""))

    rows: list[dict[str, Any]] = []
    for reg in sorted(by_register):
        for value, count in by_register[reg].most_common(16):
            if value == 0:
                continue
            rows.append(
                {
                    "register": reg,
                    "register_name": names.get(reg, ""),
                    "value": hex32(value),
                    "count": count,
                }
            )
    return rows


def search_value_origins(
    values: list[dict[str, Any]],
    code_bin: Path | None,
    native_sources: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    code_data = code_bin.read_bytes() if code_bin and code_bin.is_file() else b""
    asset_bytes: dict[str, bytes] = {}
    for source in native_sources:
        path = Path(source["path"])
        if path.is_file() and path.stat().st_size <= 64 * 1024 * 1024:
            try:
                asset_bytes[source["path"]] = path.read_bytes()
            except OSError:
                pass

    rows: list[dict[str, Any]] = []
    for value_row in values:
        value = parse_int(value_row["value"])
        if value is None:
            continue
        needle = struct.pack("<I", value & 0xFFFFFFFF)
        code_hits = find_all(code_data, needle) if code_data else []
        asset_hits = []
        for path_text, data in asset_bytes.items():
            hits = find_all(data, needle, limit=8)
            if hits:
                asset_hits.append(
                    {
                        "path": path_text,
                        "offsets": [f"0x{offset:X}" for offset in hits],
                        "hit_count_limited": len(hits),
                    }
                )
        origin_class = "not_found_as_static_le32_literal"
        if asset_hits:
            origin_class = "found_in_native_asset_bytes"
        if code_hits:
            origin_class = (
                "found_in_code_bin_and_native_asset_bytes" if asset_hits else "found_in_code_bin"
            )
        rows.append(
            {
                **value_row,
                "origin_class": origin_class,
                "code_bin_offsets": [f"0x{offset:X}" for offset in code_hits],
                "native_asset_hits": asset_hits,
            }
        )
    return rows


def read_code_word(code_data: bytes, runtime_address: int) -> int | None:
    offset = runtime_address - CODE_BIN_RUNTIME_BASE
    if offset < 0 or offset + 4 > len(code_data):
        return None
    return struct.unpack_from("<I", code_data, offset)[0]


def read_code_c_string(code_data: bytes, runtime_address: int) -> str:
    offset = runtime_address - CODE_BIN_RUNTIME_BASE
    if offset < 0 or offset >= len(code_data):
        return ""
    end = code_data.find(b"\x00", offset)
    if end < 0:
        return ""
    return code_data[offset:end].decode("ascii", errors="replace")


def read_code_u16_string(code_data: bytes, runtime_address: int, max_bytes: int = 512) -> str:
    offset = runtime_address - CODE_BIN_RUNTIME_BASE
    if offset < 0 or offset >= len(code_data):
        return ""
    end = offset
    limit = min(len(code_data), offset + max_bytes)
    while end + 1 < limit:
        if code_data[end] == 0 and code_data[end + 1] == 0:
            break
        end += 2
    return code_data[offset:end].decode("utf-16le", errors="replace")


def collect_native_object_bank_inventory(
    code_bin: Path | None, romfs_root: Path | None
) -> dict[str, Any]:
    requested_ctxb_ids = list(range(0x44, 0x4C))
    base = {
        "available": False,
        "requested_ctxb_ids": [f"0x{value:02X}" for value in requested_ctxb_ids],
    }
    if code_bin is None or not code_bin.is_file():
        return {**base, "reason": "no code.bin supplied"}
    code_data = code_bin.read_bytes()
    object_table_address = read_code_word(code_data, 0x0032E414)
    update_bank_object_table_address = read_code_word(code_data, 0x002E4FD0)
    if object_table_address is None:
        return {**base, "reason": "object table literal missing"}

    object_names = {
        1: "OBJECT_GAMEPLAY_KEEP",
        2: "OBJECT_GAMEPLAY_FIELD_KEEP",
        3: "OBJECT_GAMEPLAY_DANGEON_KEEP",
    }
    source_rows: list[dict[str, Any]] = []
    for object_id in [1, 2, 3]:
        entry_address = object_table_address + object_id * 0x44
        rom_path = read_code_c_string(code_data, entry_address)
        archive_path = romfs_path_from_rom_path(romfs_root, rom_path)
        ctxb_files: list[dict[str, Any]] = []
        requested_files: list[dict[str, Any]] = []
        parse_error = ""
        if archive_path is not None and archive_path.is_file():
            try:
                ctxb_files = parse_zar_type_local_files(archive_path, "ctxb")
                for requested_id in requested_ctxb_ids:
                    if requested_id < len(ctxb_files):
                        row = ctxb_files[requested_id]
                        requested_files.append(
                            {
                                "requested_id": f"0x{requested_id:02X}",
                                "type_local_index": row["type_local_index"],
                                "file_index": row["file_index"],
                                "name": row["name"],
                                "size": row["size"],
                                "data_offset": row["data_offset"],
                                "ctxb_summary": row.get("ctxb_summary"),
                            }
                        )
                    else:
                        requested_files.append(
                            {
                                "requested_id": f"0x{requested_id:02X}",
                                "missing": True,
                            }
                        )
            except Exception as exc:  # pragma: no cover - diagnostic report path
                parse_error = str(exc)
        source_rows.append(
            {
                "source_id": object_id,
                "object_name": object_names.get(object_id, ""),
                "object_table_entry_address": hex_addr(entry_address),
                "rom_path": rom_path,
                "resolved_archive_path": str(archive_path) if archive_path is not None else "",
                "archive_exists": bool(archive_path is not None and archive_path.is_file()),
                "ctxb_count": len(ctxb_files),
                "requested_ctxb_entries": requested_files,
                "parse_error": parse_error,
            }
        )

    source_id_1 = next((row for row in source_rows if row["source_id"] == 1), {})
    source_id_1_entries = source_id_1.get("requested_ctxb_entries", [])
    all_source_id_1_requested_present = bool(source_id_1_entries) and all(
        not row.get("missing") for row in source_id_1_entries
    )
    return {
        **base,
        "available": True,
        "romfs_root": str(romfs_root) if romfs_root is not None else "",
        "object_table_pointer_literal_spawn": "0x0032E414",
        "object_table_pointer_literal_update_bank": "0x002E4FD0",
        "object_table_runtime_address": hex32(object_table_address),
        "object_table_runtime_address_update_bank": hex32(update_bank_object_table_address)
        if update_bank_object_table_address is not None
        else "",
        "object_table_stride_bytes": "0x44",
        "source_rows": source_rows,
        "source_id_1_requested_ctxb_entries_present": all_source_id_1_requested_present,
        "interpretation": (
            "Object bank source id 1 resolves through the native object table to "
            "rom:/actor/zelda_keep.zar; its local CTXB indices 0x44..0x4B are the "
            "kankyo rain/thunder/storm payloads consumed by z_kankyo."
        ),
        "remaining_gap": (
            "decode the CTXB payload contents and the descriptor/light-list binding "
            "that consumes these decoded kankyo textures/effects"
        ),
    }


def code_instruction_checks(
    code_bin: Path | None, checks: list[tuple[int, int, str]]
) -> list[dict[str, Any]]:
    if code_bin is None or not code_bin.is_file():
        return []
    code_data = code_bin.read_bytes()
    rows: list[dict[str, Any]] = []
    for runtime_address, expected_word, meaning in checks:
        actual_word = read_code_word(code_data, runtime_address)
        rows.append(
            {
                "runtime_address": hex_addr(runtime_address),
                "code_bin_offset": hex_addr(runtime_address - CODE_BIN_RUNTIME_BASE),
                "expected_word": hex32(expected_word),
                "actual_word": hex32(actual_word) if actual_word is not None else "",
                "verified": actual_word == expected_word,
                "meaning": meaning,
            }
        )
    return rows


def material_draw_dispatch_structure(code_bin: Path | None) -> dict[str, Any]:
    checks = code_instruction_checks(
        code_bin,
        [
            (0x00452860, 0xE7B01107, "load native CMB mesh record pointer for current draw entry"),
            (0x00452864, 0xE5D12003, "read CMB mesh byte +0x03 as visibility id"),
            (0x00452874, 0xE5D18002, "read CMB mesh byte +0x02 as material/lane index"),
            (0x0045287C, 0xE3A01073, "load material lane stride word count 0x73"),
            (0x00452880, 0xE1610188, "multiply CMB material index by 0x73"),
            (0x00452888, 0xE080B101, "select runtime material lane base + index * 0x1CC"),
            (0x00452898, 0xE5D00002, "read selected lane state byte +0x02 as PICA setup gate"),
            (0x004528B0, 0xEB00AB7D, "call 0x47D6AC material/PICA setup path when gate is enabled"),
            (0x004528D8, 0xE12FFF32, "call first draw callback with selected material lane pointer"),
            (0x004528F0, 0xE12FFF33, "call second draw callback with selected material lane pointer and material index"),
            (0x00452908, 0xE12FFF33, "call third draw callback with selected material lane pointer and material index"),
        ],
    )
    return {
        "available": bool(checks),
        "origin_class": "code_bin_verified_material_draw_dispatch_contract",
        "runtime_range": "0x00452854..0x00452920",
        "code_bin": str(code_bin) if code_bin else "",
        "all_instruction_checks_verified": bool(checks) and all(row["verified"] for row in checks),
        "instruction_checks": checks,
        "native_struct_contract": {
            "mesh_record_source": "native CMB mesh entry selected by draw table",
            "cmb_mesh_material_index_offset": "0x02",
            "cmb_mesh_visibility_id_offset": "0x03",
            "runtime_material_lane_index_source": "cmb_mesh.material_index",
            "runtime_material_lane_stride_words": "0x73",
            "runtime_material_lane_stride_bytes": "0x1CC",
            "runtime_material_lane_base_expression": "material_lane_table + cmb_mesh.material_index * 0x1CC",
            "material_pica_setup_gate": "byte at pointer stored in selected material lane + 0x02",
            "material_pica_setup_function": "0x0047D6AC",
            "draw_callbacks_receive": "draw context, selected runtime material lane pointer, and material/lane index",
        },
    }


def light_vector_prep_loop_structure(
    code_bin: Path | None,
    lighting_provenance: dict[str, Any] | None = None,
    romfs_root: Path | None = None,
) -> dict[str, Any]:
    checks = code_instruction_checks(
        code_bin,
        [
            (0x0031317C, 0xED9F9AAF, "load disabled-slot fallback z literal -1.0f from 0x00313440"),
            (0x00313180, 0xE3A06000, "initialize light slot loop counter to 0"),
            (0x00313184, 0xE0860086, "compute slot + slot*2 before stride scaling"),
            (0x00313188, 0xE0854280, "select runtime light packet slot as base + slot*0x60"),
            (0x0031318C, 0xED940A35, "read slot enable/intensity field at +0xD4"),
            (0x00313194, 0xE35005FE, "compare enable/intensity field against 1.0f"),
            (0x0031319C, 0xE2843098, "pass slot color/packet source pointer +0x98 to helper 0x466EA0"),
            (0x003131A0, 0xE2842088, "pass slot color/packet source pointer +0x88 to helper 0x466EA0"),
            (0x003131AC, 0xEB054F3B, "call pre-vector light upload helper 0x00466EA0"),
            (0x003131B0, 0xED940A32, "read source vector x from slot +0xC8"),
            (0x003131BC, 0xED940A33, "read source vector y from slot +0xCC"),
            (0x003131C8, 0xED940A34, "read source vector z from slot +0xD0"),
            (0x003131D4, 0xEB00EBC5, "transform source vector through the prepared matrix at stack +0x40"),
            (0x00313238, 0xE28490D8, "select prepared vector output field at slot +0xD8"),
            (0x00313250, 0xE889100E, "write transformed vector packet to slot +0xD8..+0xE4"),
            (0x00313258, 0xED848A36, "write disabled-slot fallback vector x=0 at +0xD8"),
            (0x0031325C, 0xED848A37, "write disabled-slot fallback vector y=0 at +0xDC"),
            (0x00313260, 0xED849A38, "write disabled-slot fallback vector z=-1 at +0xE0"),
            (0x0031326C, 0xED840A39, "copy enable/intensity field into prepared packet +0xE4"),
            (0x00313278, 0xEB054F20, "call prepared-vector upload helper 0x00466F00"),
            (0x00313280, 0xE3560003, "compare slot loop counter against native slot count 3"),
            (0x00313284, 0xBAFFFFBE, "loop over the three native light packet slots"),
        ],
    )
    producer_checks = code_instruction_checks(
        code_bin,
        [
            (0x003130A4, 0xE92D43F0, "full light packet prep function entry"),
            (0x003130C8, 0x0A000071, "r1 == 0 selects alternate upload path at 0x00313294"),
            (0x0030F524, 0x1B000EDE, "conditional scene/material caller invokes 0x003130A4"),
            (0x003FBBD8, 0xEBFC5D31, "render-path caller invokes 0x003130A4"),
            (0x003FB714, 0xEBFC5FF2, "light list builder classifies native light command id"),
            (0x003FB730, 0xEBFC5FD8, "light list builder appends record payload through 0x00313698"),
            (0x003FB754, 0xEBFC5FBD, "light list builder appends disabled/default entry through 0x00313650"),
            (0x003FB8D4, 0xEBFC5FE2, "light list builder finalizes packet/list through 0x00313864"),
            (0x0031365C, 0xE58310D4, "light-list default entry writer stores entry id at +0xD4"),
            (0x00313668, 0xE58120D8, "light-list default entry writer stores fallback word at +0xD8"),
            (0x00313678, 0xE58320DC, "light-list default entry writer stores fallback word at +0xDC"),
            (0x00313684, 0xE58210E0, "light-list default entry writer stores fallback word at +0xE0"),
            (0x00313690, 0xE5801010, "light-list default entry writer increments list default-entry count at +0x10"),
        ],
    )
    runtime_packet_buffer_checks = code_instruction_checks(
        code_bin,
        [
            (0x004730A4, 0xE3A01F6E, "render-context init allocates 0x1B8 bytes for the ctx+0x358 packet buffer"),
            (0x004730B4, 0xE1A01007, "pass native source/default record pointer to packet-buffer initializer"),
            (0x004730B8, 0xE5840358, "store allocated packet buffer at render context +0x358"),
            (0x004730BC, 0xEBFB56B3, "initialize ctx+0x358 through 0x00348B90"),
            (0x004730C0, 0xE3A01C26, "pass 0x2600-sized native source block to 0x00348A64"),
            (0x004730D0, 0xE5940358, "reload ctx+0x358 for native source binding"),
            (0x004730DC, 0xEBFB5660, "bind/copy native source data into ctx+0x358 through 0x00348A64"),
            (0x00348B98, 0xE3A02E12, "0x00348B90 copies 0x120 bytes into packet buffer +0x04"),
            (0x00348BA0, 0xEB00A2E4, "0x00348B90 calls bulk copy helper 0x00371738"),
            (0x003FBBC4, 0xE2840018, "render path prepares the matrix/context at render ctx +0x18"),
            (0x003FBBCC, 0xE5940358, "render path loads the ctx+0x358 packet buffer"),
            (0x003FBBD0, 0xE2842018, "render path passes render ctx +0x18 as transform/source context"),
            (0x003FBBD4, 0xE3A01001, "render path selects the enabled upload path"),
            (0x003FBBD8, 0xEBFC5D31, "render path consumes ctx+0x358 through 0x003130A4"),
        ],
    )
    default_packet_layout_checks = code_instruction_checks(
        code_bin,
        [
            (0x00347258, 0xE92D4070, "native light packet default-layout initializer entry"),
            (0x0034726C, 0xE3A02060, "default-layout initializer uses a 0x60-byte slot stride"),
            (0x0034727C, 0xE2800088, "default-layout initializer selects packet slot payload base +0x88"),
            (0x00347304, 0xE0840280, "default-layout initializer advances by slot*0x60"),
            (0x00347314, 0xED800A22, "default-layout initializer writes source payload A at slot +0x88"),
            (0x0034732C, 0xED800A26, "default-layout initializer writes source payload B at slot +0x98"),
            (0x00347360, 0xED800A32, "default-layout initializer writes source vector x at slot +0xC8"),
            (0x00347364, 0xED801A33, "default-layout initializer writes source vector y at slot +0xCC"),
            (0x0034735C, 0xED800A34, "default-layout initializer writes source vector z at slot +0xD0"),
            (0x00347368, 0xED802A35, "default-layout initializer writes enable/intensity at slot +0xD4"),
        ],
    )
    source_provider_checks = code_instruction_checks(
        code_bin,
        [
            (0x00471F74, 0xE92D5FF0, "z_movie resource init function entry"),
            (0x00471F88, 0xE24DDE5E, "z_movie resource init reserves the 0x5E0-byte stack frame"),
            (0x00471FE4, 0xE3A0000D, "select native runtime source provider id 13"),
            (0x00471FEC, 0xEBF9BC77, "look up provider id 13 through 0x002E11D0"),
            (0x00471FF4, 0xE58D05D0, "save provider id 13 result at stack +0x5D0"),
            (0x00473050, 0xE59D25D0, "pass provider id 13 result to 0x00348A64 for ctx+0x354"),
            (0x004730D4, 0xE59D25D0, "pass provider id 13 result to 0x00348A64 for ctx+0x358"),
            (0x002E11D0, 0xE59F1004, "0x002E11D0 loads the runtime provider table pointer"),
            (0x002E11D4, 0xE7910100, "0x002E11D0 returns provider_table[id]"),
            (0x002E11DC, 0x0055B490, "runtime provider table address"),
        ],
    )
    z_movie_resource_context_checks = code_instruction_checks(
        code_bin,
        [
            (0x00472064, 0xE7977100, "z_movie selects a language-specific hint-movie path from the path table"),
            (0x00472160, 0xE12FFF3C, "z_movie allocates a 0x54-byte resource context through the allocator vtable"),
            (0x00472174, 0xE58101A4, "z_movie stores the entry-zero resource context at object +0x1A4"),
            (0x004721C8, 0xE58101A4, "z_movie stores additional resource contexts at object +0x1A4 + index*4"),
            (0x004725E0, 0xE59421A4, "z_movie ctx+0x350 binding loads source context entry zero from object +0x1A4"),
            (0x004DB1B8, 0x004F7E00, "z_movie hint-movie path table slot 0 points to JP CTXB path"),
            (0x004DB1D8, 0x004F814A, "z_movie hint-movie path table slot 8 points to EU Italian CTXB path"),
        ],
    )
    surface_light_setting_selection_checks = code_instruction_checks(
        code_bin,
        [
            (0x002C1E10, 0xE3A03001, "SurfaceType light-setting getter selects native field id 1"),
            (0x002C1E18, 0xEB01809A, "SurfaceType light-setting getter reads the raw field through 0x00322088"),
            (0x002C1E1C, 0xE1A00A80, "SurfaceType light-setting getter shifts the raw field left by 21"),
            (0x002C1E20, 0xE1A00DA0, "SurfaceType light-setting getter extracts a five-bit light-setting index"),
            (0x0032F140, 0xEBFE4B32, "player floor collision reads SurfaceType light setting through 0x002C1E10"),
            (0x0032F144, 0xE1A01000, "player floor collision passes the SurfaceType light setting as r1"),
            (0x0032F148, 0xE1A0000B, "player floor collision passes PlayState as r0"),
            (0x0032F14C, 0xEBFFEFFA, "player floor collision invokes native light-setting change helper 0x0032B13C"),
            (0x0032B13C, 0xE2800A03, "light-setting change helper selects the play environment region at play +0x3000"),
            (0x0032B140, 0xE5D02235, "light-setting change helper loads current setting from play +0x3235"),
            (0x0032B144, 0xE1520001, "light-setting change helper compares current and requested settings"),
            (0x0032B164, 0xE351001F, "light-setting change helper compares requested setting against 31"),
            (0x0032B16C, 0x23A01000, "light-setting change helper normalizes requested settings >= 31 to 0"),
            (0x0032B170, 0xED800A96, "light-setting change helper resets transition/blend weight at play +0x3258"),
            (0x0032B174, 0xE5C02236, "light-setting change helper stores previous setting at play +0x3236"),
            (0x0032B178, 0xE5C01235, "light-setting change helper stores current setting at play +0x3235"),
        ],
    )
    environment_light_setting_transition_reset_checks = code_instruction_checks(
        code_bin,
        [
            (0x002D0AF8, 0xE59500D4, "single verified caller passes its PlayState pointer from context +0xD4"),
            (0x002D0AFC, 0xEB07A12F, "caller invokes environment light-setting transition reset helper 0x004B8FC0"),
            (0x004B8FC0, 0xE2800A03, "transition reset helper selects the play environment region at play +0x3000"),
            (0x004B8FC4, 0xE5D011B0, "transition reset helper reads environment mode/state byte at play +0x31B0"),
            (0x004B8FD4, 0xE5D11001, "inactive path reads global fallback setting byte from 0x00531EB4 + 1"),
            (0x004B8FD8, 0xE5C011B1, "inactive path stores fallback byte to play +0x31B1"),
            (0x004B8FDC, 0xE5C011B2, "inactive path stores fallback byte to play +0x31B2"),
            (0x004B8FE8, 0xE5C01234, "active path clears transition state byte at play +0x3234"),
            (0x004B8FF4, 0xE5C01237, "active path stores 0xFF sentinel at play +0x3237"),
            (0x004B8FF8, 0xED800A96, "active path stores 1.0f transition/blend weight at play +0x3258"),
            (0x004B8FFC, 0xE5D02235, "active path loads current light setting from play +0x3235"),
            (0x004B9000, 0xE5D01236, "active path loads previous light setting from play +0x3236"),
            (0x004B9004, 0xE5C02236, "active path stores current light setting into previous slot play +0x3236"),
            (0x004B9008, 0xE5C01235, "active path stores previous light setting into current slot play +0x3235"),
            (0x004B9010, 0xEAF85C6E, "helper tail-branches to 0x002D01D0 after resetting the transition state"),
            (0x004B9014, 0x00531EB4, "inactive-path global fallback byte source pointer literal"),
            (0x004B9018, 0x3F800000, "active-path 1.0f transition weight literal"),
        ],
    )
    environment_light_setting_request_helper_checks = code_instruction_checks(
        code_bin,
        [
            (0x00316D74, 0xE2800A03, "request helper selects the play environment region at play +0x3000"),
            (0x00316D78, 0xE351001F, "request helper compares requested setting against 31"),
            (0x00316D7C, 0xE5D021B0, "request helper reads environment mode/state byte at play +0x31B0"),
            (0x00316D80, 0x03A01000, "request helper normalizes requested setting 31 to 0"),
            (0x00316D84, 0xE3520000, "request helper branches by active/inactive environment state"),
            (0x00316D8C, 0x15C02234, "active path clears transition state byte at play +0x3234"),
            (0x00316D90, 0x15C01237, "active path stores requested setting target at play +0x3237"),
            (0x00316D98, 0xE59F301C, "inactive path loads global fallback byte source pointer literal"),
            (0x00316D9C, 0xE5D021B2, "inactive path reads previous fallback byte from play +0x31B2"),
            (0x00316DA0, 0xE5C32001, "inactive path writes previous fallback byte to 0x00531EB4 + 1"),
            (0x00316DA4, 0xE5D021B1, "inactive path reads current fallback byte from play +0x31B1"),
            (0x00316DA8, 0xE1520001, "inactive path compares current fallback byte with requested setting"),
            (0x00316DAC, 0x15C011B1, "inactive path stores requested setting to play +0x31B1 when changed"),
            (0x00316DB0, 0x15C011B2, "inactive path stores requested setting to play +0x31B2 when changed"),
            (0x00316DB8, 0xEAFEE504, "request helper tail-branches to 0x002D01D0 after handling the request"),
            (0x00316DBC, 0x00531EB4, "inactive-path global fallback byte source pointer literal"),
            (0x002D09D0, 0xE59500D4, "Camera_CheckWater caller loads PlayState pointer from context +0xD4"),
            (0x002D09D4, 0xE59D101C, "Camera_CheckWater caller loads the requested setting from stack +0x1C"),
            (0x002D09D8, 0xEB0118E5, "Camera_CheckWater caller invokes environment light-setting request helper"),
            (0x003B6870, 0xE3A06000, "unsymbolized actor/gameplay block initializes r6 to requested setting 0"),
            (0x003B6880, 0xE1A01006, "unsymbolized actor/gameplay block passes requested setting 0 from r6"),
            (0x003B68A8, 0xE1A00007, "unsymbolized actor/gameplay block passes PlayState in r7"),
            (0x003B68AC, 0xEBFD8130, "unsymbolized actor/gameplay block invokes environment light-setting request helper"),
            (0x003B6BA8, 0xE3A06000, "unsymbolized actor/gameplay block initializes r6 to requested setting 0"),
            (0x003B6BB0, 0xE1A01006, "unsymbolized actor/gameplay block passes requested setting 0 from r6"),
            (0x003B6BB4, 0xE1A00007, "unsymbolized actor/gameplay block passes PlayState in r7"),
            (0x003B6BBC, 0xEBFD806C, "unsymbolized actor/gameplay block invokes environment light-setting request helper"),
            (0x003B7058, 0xE3A01000, "unsymbolized actor/gameplay block sets requested setting 0"),
            (0x003B7074, 0xE1A00007, "unsymbolized actor/gameplay block passes PlayState in r7"),
            (0x003B7078, 0xEBFD7F3D, "unsymbolized actor/gameplay block invokes environment light-setting request helper"),
            (0x003B70E0, 0xE3A01001, "unsymbolized actor/gameplay block sets requested setting 1"),
            (0x003B70E4, 0xE1A00007, "unsymbolized actor/gameplay block passes PlayState in r7"),
            (0x003B70E8, 0xEBFD7F21, "unsymbolized actor/gameplay block invokes environment light-setting request helper"),
            (0x003B7218, 0xE3A01000, "unsymbolized actor/gameplay block sets requested setting 0"),
            (0x003B721C, 0xE1A00007, "unsymbolized actor/gameplay block passes PlayState in r7"),
            (0x003B7220, 0xEBFD7ED3, "unsymbolized actor/gameplay block invokes environment light-setting request helper"),
        ],
    )
    provider_table_population_checks = code_instruction_checks(
        code_bin,
        [
            (0x004811B4, 0xE92D43F0, "provider table population function entry"),
            (0x004811BC, 0xE24DDFED, "reserve 0x3B4-byte provider population stack frame"),
            (0x00481230, 0xE28D5FC3, "select temporary provider handle table at sp+0x30C"),
            (0x00481234, 0xE5906F3C, "load runtime language/menu prefix index from global +0xF3C"),
            (0x00481238, 0xE7983104, "load slot suffix pointer from CTXB suffix table"),
            (0x00481244, 0xE7972106, "load language menu prefix pointer"),
            (0x00481248, 0xEBF9EC56, "format provider resource path through 0x2FC3A8"),
            (0x00481258, 0xEBFA8F39, "build/open provider resource handle through 0x324F44"),
            (0x0048126C, 0xEBFA0023, "open provider handle through 0x301300"),
            (0x00481270, 0xE7850104, "store temporary provider handle by slot"),
            (0x00481280, 0xE59F7100, "load allocator/global pointer"),
            (0x00481284, 0xE59F9100, "load provider table pointer literal"),
            (0x00481290, 0xE7950104, "load temporary handle for provider slot"),
            (0x004812E8, 0xE7890104, "write opened provider object into provider table slot"),
            (0x00481314, 0xE3540010, "iterate exactly 16 provider slots"),
            (0x00481318, 0xBAFFFFDC, "loop over provider slots"),
            (0x00481368, 0x004D3FE8, "literal pointer to provider prefix/suffix table"),
            (0x00481384, 0x004D4050, "literal pointer to provider path format string"),
            (0x0048138C, 0x0055B490, "literal pointer to provider table"),
        ],
    )
    default_record_source_checks = code_instruction_checks(
        code_bin,
        [
            (0x00472F8C, 0xE59F1244, "load static default subtable pointer 0x004DB7AC"),
            (0x00472F90, 0xE3A020C0, "copy 0xC0 bytes into stack subtable r9"),
            (0x00472F98, 0xEBFBF9E6, "copy default subtable through 0x00371738"),
            (0x00472F9C, 0xE59F1238, "load static default subtable pointer 0x004DB86C"),
            (0x00472FA0, 0xE3A02C01, "copy 0x100 bytes into stack subtable sp+0x1E0"),
            (0x00472FA8, 0xEBFBF9E2, "copy default subtable through 0x00371738"),
            (0x00472FB0, 0xE59F1228, "load static default subtable pointer 0x004DB96C"),
            (0x00472FB4, 0xE3A02080, "copy 0x80 bytes into stack subtable r6"),
            (0x00472FBC, 0xEBFBF9DD, "copy default subtable through 0x00371738"),
            (0x00472FC0, 0xE59F021C, "load static default subtable pointer 0x004DB9EC"),
            (0x00472FC4, 0xE28D7E13, "select stack subtable sp+0x130"),
            (0x00472FE4, 0xE2477028, "rewind copied 0x28-byte stack subtable pointer"),
            (0x00472FE8, 0xE59F11F8, "load static default record table pointer 0x004DBA1C"),
            (0x00472FEC, 0xE3A02F46, "copy 0x118 bytes into default record stack base sp+0x18"),
            (0x00472FF0, 0xEBFBF9D0, "copy default record through 0x00371738"),
            (0x00472FF8, 0xE58D601C, "patch default record +0x04 with stack subtable r6"),
            (0x00472FFC, 0xE58D9018, "patch default record +0x00 with stack subtable r9"),
            (0x00473000, 0xE58D0020, "patch default record +0x08 with stack subtable sp+0x1E0"),
            (0x00473004, 0xE58D7028, "patch default record +0x10 with stack subtable sp+0x130"),
            (0x0047302C, 0xE28D7018, "select default record stack base sp+0x18 as r7"),
            (0x004730B4, 0xE1A01007, "pass r7 default record to ctx+0x358 initializer"),
            (0x004731D8, 0x004DB7AC, "literal pointer to default subtable copied into r9"),
            (0x004731DC, 0x004DB86C, "literal pointer to default subtable copied into sp+0x1E0"),
            (0x004731E0, 0x004DB96C, "literal pointer to default subtable copied into r6"),
            (0x004731E4, 0x004DB9EC, "literal pointer to 0x28-byte default subtable copied into sp+0x130"),
            (0x004731E8, 0x004DBA1C, "literal pointer to main default record copied into sp+0x18"),
        ],
    )
    runtime_descriptor_binding_checks = code_instruction_checks(
        code_bin,
        [
            (0x004725C8, 0xE5840350, "z_movie init stores allocated native descriptor object at object +0x350"),
            (0x004725CC, 0xEBFB596F, "z_movie init initializes object +0x350 through 0x00348B90"),
            (0x004725D0, 0xE3A01C26, "z_movie init prepares 0x2600-sized native source binding for object +0x350"),
            (0x004725DC, 0xE5940350, "z_movie init reloads object +0x350 for native source binding"),
            (0x004725E0, 0xE59421A4, "z_movie init passes native source pointer from object +0x1A4 entry zero"),
            (0x004725EC, 0xEBFB591C, "z_movie init binds object +0x350 source through 0x00348A64"),
            (0x003F81E8, 0xEBFDC9CD, "verified Ganon2 draw setup selects native descriptor id 7 through 0x0036A924"),
            (0x003F81EC, 0xE5840350, "verified Ganon2 draw setup stores selected native descriptor pointer at context +0x350"),
            (0x003FB6E8, 0xE5940350, "light-list builder loads native descriptor object from context +0x350"),
            (0x003FB73C, 0xE5901000, "light-list builder loads descriptor record pointer from ctx+0x350 object"),
            (0x003FB740, 0xE591101C, "light-list builder reads descriptor flags at record +0x1C"),
            (0x003FB744, 0xE3110080, "light-list builder tests descriptor flag 0x80 before selecting block/default"),
        ],
    )
    runtime_descriptor_selector_checks = code_instruction_checks(
        code_bin,
        [
            (0x0036A930, 0xE2840B0E, "descriptor selector starts from resource context +0x3800"),
            (0x0036A93C, 0xE2800F96, "descriptor selector adds +0x258 to form lookup table base +0x3A58"),
            (0x0036A940, 0xEBFFE4B2, "descriptor selector resolves source id through 0x00363C10"),
            (0x0036A948, 0xE3500013, "descriptor selector rejects resolved indices >= 19"),
            (0x0036A950, 0xE59F10B0, "descriptor selector loads resource-entry availability offset literal"),
            (0x0036A954, 0xE0840380, "descriptor selector applies 0x80-byte resource entry stride"),
            (0x0036A958, 0xE7911000, "descriptor selector tests resource entry pointer at context +0x3A64 + index*0x80"),
            (0x0036A960, 0x12800B0E, "descriptor selector adds resource context +0x3800 when entry exists"),
            (0x0036A964, 0x12800F97, "descriptor selector adds +0x25C, giving context +0x3A5C + index*0x80"),
            (0x0036A970, 0xE2807010, "descriptor selector passes context +0x3A6C + index*0x80 to resource subselector"),
            (0x0036A9C0, 0xE590417C, "descriptor selector loads backend selector object from global +0x17C"),
            (0x0036A9C4, 0xE5950178, "descriptor selector copies caller context field +0x178 into selector object +0x08"),
            (0x0036A9D0, 0xEBFFB948, "descriptor selector calls 0x00358EF8 with resource entry payload and descriptor id"),
            (0x0036A9E8, 0xE12FFF33, "descriptor selector invokes selected backend vtable method"),
            (0x0036AA08, 0x00003A64, "descriptor selector resource-entry availability offset literal"),
            (0x00363C10, 0xE52D4004, "source id to resource-table index resolver entry"),
            (0x00363C14, 0xE5D04000, "source id resolver reads resource table entry count byte"),
            (0x00363C30, 0xE08C3382, "source id resolver applies 0x80-byte entry stride"),
            (0x00363C34, 0xE1D330F4, "source id resolver reads signed source id at entry +0x04"),
            (0x00363C40, 0xE1530001, "source id resolver compares absolute source id against requested id"),
            (0x00363C48, 0xE1A00002, "source id resolver returns matching entry index"),
            (0x00363C80, 0xE1D328F4, "source id resolver also tests the next paired entry source id at +0x84"),
            (0x00363CB0, 0xE3E00000, "source id resolver returns -1 when no resource entry matches"),
        ],
    )
    native_resource_context_payload_checks = code_instruction_checks(
        code_bin,
        [
            (0x0036A950, 0xE59F10B0, "descriptor selector loads resource-entry availability offset literal"),
            (0x0036A954, 0xE0840380, "descriptor selector applies 0x80-byte resource entry stride"),
            (0x0036A958, 0xE7911000, "descriptor selector tests resource entry pointer at context +0x3A64 + index*0x80"),
            (0x0036A960, 0x12800B0E, "descriptor selector adds resource context +0x3800 when entry exists"),
            (0x0036A964, 0x12800F97, "descriptor selector adds +0x25C to reach entry header base"),
            (0x0036A970, 0xE2807010, "descriptor selector passes context +0x3A6C + index*0x80 payload"),
            (0x0036AA08, 0x00003A64, "descriptor selector availability pointer offset literal"),
            (0x0045040C, 0xE3A01001, "z_kankyo requests source id 1 for current kankyo variant payload"),
            (0x00450410, 0xEBFC4DFE, "z_kankyo resolves source id 1 through 0x00363C10"),
            (0x00450420, 0xE59F1588, "z_kankyo loads resource-entry availability offset literal"),
            (0x00450424, 0xE0850380, "z_kankyo applies 0x80-byte resource entry stride"),
            (0x00450428, 0xE7911000, "z_kankyo tests resource entry pointer at play +0x3A64 + index*0x80"),
            (0x00450430, 0x12800B0E, "z_kankyo adds resource context +0x3800 when entry exists"),
            (0x00450434, 0x12800F97, "z_kankyo adds +0x25C to reach entry header base"),
            (0x00450440, 0xE2800010, "z_kankyo passes play +0x3A6C + index*0x80 payload to CTXB resolver"),
            (0x00450444, 0xE58D08CC, "z_kankyo stores selected source-id 1 payload pointer for later CTXB calls"),
            (0x004509B0, 0x00003A64, "z_kankyo availability pointer offset literal"),
            (0x0044E930, 0xE1A01009, "native consumer passes source id 1 through shared payload lookup"),
            (0x0044E93C, 0xEBFC54B3, "native consumer resolves source id 1 through 0x00363C10"),
            (0x0044E94C, 0xE59F137C, "native consumer loads resource-entry availability offset literal"),
            (0x0044E950, 0xE0870380, "native consumer applies 0x80-byte resource entry stride"),
            (0x0044E954, 0xE7911000, "native consumer tests resource entry pointer at play +0x3A64 + index*0x80"),
            (0x0044E96C, 0xE2805010, "native consumer selects payload +0x10 after resource entry header"),
            (0x0045216C, 0xE3A01001, "fbdemo wipe path requests source id 1"),
            (0x00452174, 0xEBFC46A5, "fbdemo wipe path resolves source id 1 through 0x00363C10"),
            (0x004521A8, 0xE2806010, "fbdemo wipe path selects payload +0x10 after resource entry header"),
            (0x0046216C, 0xE3A01001, "z_lights path requests source id 1"),
            (0x00462174, 0xEBFC06A5, "z_lights path resolves source id 1 through 0x00363C10"),
            (0x004621A4, 0xE2800010, "z_lights path selects payload +0x10 after resource entry header"),
        ],
    )
    native_object_bank_population_checks = code_instruction_checks(
        code_bin,
        [
            (0x0032E21C, 0xE92D41F0, "Object_Spawn entry"),
            (0x0032E228, 0xE5D00000, "Object_Spawn reads current object bank count byte"),
            (0x0032E230, 0xE0840380, "Object_Spawn selects entry base with 0x80-byte stride"),
            (0x0032E234, 0xE1C010B4, "Object_Spawn stores requested object id at entry +0x04"),
            (0x0032E238, 0xE59F01D4, "Object_Spawn loads native object table pointer literal"),
            (0x0032E23C, 0xE0811201, "Object_Spawn computes object table id * 0x44 part 1"),
            (0x0032E240, 0xE0806101, "Object_Spawn computes object table id * 0x44 entry pointer"),
            (0x0032E244, 0xE5960040, "Object_Spawn reads cached converted object path at object table entry +0x40"),
            (0x0032E254, 0xEBFFDB5D, "Object_Spawn converts object table path through 0x00324FD0"),
            (0x0032E258, 0xE5860040, "Object_Spawn caches converted object path at object table entry +0x40"),
            (0x0032E268, 0xE580700C, "Object_Spawn stores object archive/path provider pointer at entry +0x0C"),
            (0x0032E270, 0xEB0087A5, "Object_Spawn allocates object archive buffer through 0x0035010C"),
            (0x0032E288, 0xE59F2188, "Object_Spawn loads object path conversion limit literal"),
            (0x0032E29C, 0xEBFFDB28, "Object_Spawn copies/converts object path through 0x00324F44"),
            (0x0032E2B8, 0xEBFFDAFB, "Object_Spawn creates archive load request through 0x00324EAC"),
            (0x0032E2CC, 0xE5810010, "Object_Spawn stores request handle at entry +0x10"),
            (0x0032E2E0, 0xEBFFB5B6, "Object_Spawn waits/polls object request through 0x0031B9C0"),
            (0x0032E3C0, 0xEBFFB575, "Object_Spawn releases completed object request through 0x0031B99C"),
            (0x0032E3D0, 0xE5810010, "Object_Spawn clears request handle at entry +0x10"),
            (0x0032E3DC, 0xE590200C, "Object_Spawn reloads entry +0x0C provider/path pointer"),
            (0x0032E3E8, 0xE5B01008, "Object_Spawn loads entry +0x08 archive buffer and advances base"),
            (0x0032E3F0, 0xE280000C, "Object_Spawn selects entry +0x14 provider object for ZAR setup"),
            (0x0032E3F4, 0xEBFFB34A, "Object_Spawn initializes entry +0x14 through ZAR_SetupZARInfo"),
            (0x0032E404, 0xE5C40000, "Object_Spawn increments object bank count byte"),
            (0x0032E414, 0x0053CCF4, "Object_Spawn literal pointer to native object table"),
            (0x002E4EA0, 0xE92D4FF0, "Object_UpdateBank entry"),
            (0x002E4EC4, 0xE1D410F0, "Object_UpdateBank reads signed object id at entry +0x04"),
            (0x002E4ECC, 0xAA000038, "Object_UpdateBank only starts loads for negative pending object ids"),
            (0x002E4EE0, 0xE0800200, "Object_UpdateBank computes object table id * 0x44 part 1"),
            (0x002E4EE4, 0xE0897100, "Object_UpdateBank computes object table id * 0x44 entry pointer"),
            (0x002E4EF8, 0xEB010034, "Object_UpdateBank converts object table path through 0x00324FD0"),
            (0x002E4EFC, 0xE5870040, "Object_UpdateBank caches converted object path at object table entry +0x40"),
            (0x002E4F08, 0xE584A008, "Object_UpdateBank stores object archive/path provider pointer at entry +0x0C"),
            (0x002E4F0C, 0xEB01AC7E, "Object_UpdateBank allocates object archive buffer through 0x0035010C"),
            (0x002E4F14, 0xE5840004, "Object_UpdateBank stores allocated archive buffer at entry +0x08"),
            (0x002E4F3C, 0xEB010000, "Object_UpdateBank copies/converts object path through 0x00324F44"),
            (0x002E4F58, 0xEB00FFD3, "Object_UpdateBank creates archive load request through 0x00324EAC"),
            (0x002E4F5C, 0xE584000C, "Object_UpdateBank stores request handle at entry +0x10"),
            (0x002E4F6C, 0xEB00DA93, "Object_UpdateBank waits/polls object request through 0x0031B9C0"),
            (0x002E4F80, 0xEB00DA85, "Object_UpdateBank releases completed object request through 0x0031B99C"),
            (0x002E4F94, 0xE1C400B0, "Object_UpdateBank restores pending negative id to positive object id"),
            (0x002E4F98, 0xE5942008, "Object_UpdateBank reloads entry +0x08 provider/path pointer"),
            (0x002E4FA4, 0xE5941004, "Object_UpdateBank reloads entry +0x04 archive buffer"),
            (0x002E4FAC, 0xE2840010, "Object_UpdateBank selects entry +0x14 provider object for ZAR setup"),
            (0x002E4FB0, 0xEB00D85B, "Object_UpdateBank initializes entry +0x14 through ZAR_SetupZARInfo"),
            (0x002E4FBC, 0xE2844080, "Object_UpdateBank advances by 0x80-byte object bank entry stride"),
            (0x002E4FD0, 0x0053CCF4, "Object_UpdateBank literal pointer to native object table"),
        ],
    )
    native_ctxb_descriptor_binding_checks = code_instruction_checks(
        code_bin,
        [
            (0x00348A64, 0xE92D47F0, "descriptor/source binder 0x00348A64 entry"),
            (0x00348A70, 0xE1A06001, "save descriptor slot index from r1"),
            (0x00348A80, 0xE1A07002, "save decoded native CTXB/source pointer from r2"),
            (0x00348A84, 0xE1A09003, "save caller halfword token from r3"),
            (0x00348A8C, 0xEBFF2C96, "initialize temporary binding state through 0x00313CEC"),
            (0x00348A98, 0xE0861086, "compute slot + slot*2 before descriptor stride scaling"),
            (0x00348A9C, 0xE0844201, "select descriptor slot base as object + slot*0x30"),
            (0x00348AA0, 0xE584013C, "store converted stack token at slot record +0x13C"),
            (0x00348AAC, 0xE5840140, "store converted caller token at slot record +0x140"),
            (0x00348AB8, 0xE5840144, "store converted stack token at slot record +0x144"),
            (0x00348AC4, 0xE5840148, "store converted stack token at slot record +0x148"),
            (0x00348AD0, 0xE2840F4F, "pass slot record +0x13C into descriptor helper 0x0030835C"),
            (0x00348AE0, 0xE2800024, "select decoded CTXB texture header region at source +0x24"),
            (0x00348AE4, 0xE1D020F4, "read signed texture/header parameter at source +0x28"),
            (0x00348AF0, 0xEBFEFE0C, "bind source +0x28 parameter with slot field +0x140 through 0x00308328"),
            (0x00348B04, 0xE590004C, "read decoded texture payload pointer at source +0x4C"),
            (0x00348B08, 0xE5840158, "store decoded texture payload pointer at slot record +0x158"),
            (0x00348B18, 0xE1D000B8, "read native CTXB width field at source +0x2C"),
            (0x00348B24, 0xE1C505BC, "store native CTXB width at slot record +0x15C"),
            (0x00348B30, 0xE1D000BA, "read native CTXB height field at source +0x2E"),
            (0x00348B38, 0xE1C505BE, "store native CTXB height at slot record +0x15E"),
            (0x00348B40, 0xEBFEFDAE, "derive slot-side sampler/texture mode through 0x00308200"),
            (0x00348B44, 0xE5840160, "store derived sampler/texture mode at slot record +0x160"),
            (0x00348B54, 0xE1D050BE, "read native CTXB data-type field at source +0x32"),
            (0x00348B64, 0xE1D000BC, "read native CTXB format field at source +0x30"),
            (0x00348B6C, 0xEBFEFD42, "pack native CTXB format/data-type through 0x0030807C"),
            (0x00348B70, 0xE5840164, "store packed native CTXB format/data-type at slot record +0x164"),
            (0x00348B84, 0xFFFFFC18, "literal -1000 consumed by descriptor helper 0x0030835C"),
            (0x00348B88, 0x00000000, "zero literal consumed by descriptor helper 0x0030828C"),
            (0x00348B8C, 0x00000DE1, "literal 0x0DE1 consumed by descriptor helper 0x00308200"),
        ],
    )
    native_kankyo_descriptor_instance_checks = code_instruction_checks(
        code_bin,
        [
            (0x0034897C, 0xE92D41F0, "native descriptor runtime instance helper 0x0034897C entry"),
            (0x0034899C, 0xE3A0302E, "helper 0x0034897C allocates/requests runtime container class 0x2E"),
            (0x003489AC, 0xE3A01FD7, "helper 0x0034897C requests 0x35C-byte runtime container storage"),
            (0x003489D0, 0xE3A0302F, "helper 0x0034897C allocates/requests effect instance class 0x2F"),
            (0x003489E0, 0xE3A01F79, "helper 0x0034897C requests 0x1E4-byte effect instance storage"),
            (0x003489F4, 0x1BFDF141, "helper 0x0034897C initializes the 0x1E4-byte instance through 0x002C4F00"),
            (0x00348A00, 0xE5850354, "helper 0x0034897C stores the effect instance at runtime container +0x354"),
            (0x00348A20, 0xE3A01F8D, "helper 0x0034897C requests 0x234-byte shared descriptor backing storage when caller did not pass one"),
            (0x00348A3C, 0xE58641DC, "helper 0x0034897C stores the shared descriptor backing pointer at instance +0x1DC"),
            (0x00348A44, 0xE5854358, "helper 0x0034897C stores the shared descriptor backing pointer at runtime container +0x358"),
            (0x00340D00, 0xE92D41F0, "native descriptor runtime instance helper 0x00340D00 entry"),
            (0x00340D20, 0xE3A03052, "helper 0x00340D00 allocates/requests runtime container class 0x52"),
            (0x00340D30, 0xE3A01FD7, "helper 0x00340D00 requests 0x35C-byte runtime container storage"),
            (0x00340D54, 0xE3A03053, "helper 0x00340D00 allocates/requests effect instance class 0x53"),
            (0x00340D64, 0xE3A01FA3, "helper 0x00340D00 requests 0x28C-byte effect instance storage"),
            (0x00340D78, 0x1B0558D8, "helper 0x00340D00 initializes the 0x28C-byte instance through 0x004970E0"),
            (0x00340D84, 0xE5850354, "helper 0x00340D00 stores the effect instance at runtime container +0x354"),
            (0x00340DA4, 0xE3A01F8D, "helper 0x00340D00 requests 0x234-byte shared descriptor backing storage when caller did not pass one"),
            (0x00340DC0, 0xE58641DC, "helper 0x00340D00 stores the shared descriptor backing pointer at instance +0x1DC"),
            (0x00340DC8, 0xE5854358, "helper 0x00340D00 stores the shared descriptor backing pointer at runtime container +0x358"),
            (0x0045059C, 0xE59411E4, "z_kankyo passes rain descriptor object +0x1E4 to runtime helper"),
            (0x004505A8, 0xEBFBC1D4, "z_kankyo creates rain runtime instance through 0x00340D00"),
            (0x004505B4, 0xE58401E8, "z_kankyo stores rain runtime instance at object +0x1E8"),
            (0x00450730, 0xE59411EC, "z_kankyo passes ripple descriptor object +0x1EC to runtime helper"),
            (0x00450738, 0xEBFBC170, "z_kankyo creates ripple runtime instance through 0x00340D00"),
            (0x00450740, 0xE58401F0, "z_kankyo stores ripple runtime instance at object +0x1F0"),
            (0x00450838, 0xE59B01F4, "z_kankyo reloads each thunder descriptor object before materialization"),
            (0x00450840, 0xEBFBE0E7, "z_kankyo materializes each thunder descriptor object through 0x00348BE4"),
            (0x00450890, 0xE59B11F4, "z_kankyo passes each thunder descriptor object to runtime helper"),
            (0x00450898, 0xEBFBE037, "z_kankyo creates each thunder runtime instance through 0x0034897C"),
            (0x004508A0, 0xE58B0224, "z_kankyo stores each thunder runtime instance at object +0x224 + loop*4"),
            (0x00450AEC, 0xE5941270, "z_kankyo passes storm descriptor object +0x270 to runtime helper"),
            (0x00450AF4, 0xEBFBDFA0, "z_kankyo creates storm runtime instance through 0x0034897C"),
            (0x00450AF8, 0xE5840274, "z_kankyo stores storm runtime instance at object +0x274"),
            (0x00450AFC, 0xE5901178, "z_kankyo reads storm runtime instance flags at +0x178"),
            (0x00450B04, 0xE5801178, "z_kankyo writes storm runtime instance flags with bit 0x10 set"),
            (0x00450B0C, 0xE5840278, "z_kankyo clears storm follow-up runtime pointer at object +0x278"),
            (0x004600B4, 0xEBFC5645, "z_kankyo thunder update samples the native random/timing helper before selecting an animation payload"),
            (0x004600C0, 0xEEBD0AC0, "z_kankyo thunder update converts the sampled frame selector to an integer"),
            (0x004600D0, 0xE3C11003, "z_kankyo thunder update clears the low two bits to build a modulo-four base"),
            (0x004600D4, 0xE0400001, "z_kankyo thunder update computes selector % 4 for CTXB payload selection"),
            (0x004600DC, 0xE0870100, "z_kankyo thunder update selects object base plus selector*4"),
            (0x004600E0, 0xE590C254, "z_kankyo thunder update loads CTXB payload object +0x254 + selector*4"),
            (0x004600E8, 0xE0870105, "z_kankyo thunder update selects the thunder descriptor/runtime slot base using loop index*4"),
            (0x004600F4, 0xE59001F4, "z_kankyo thunder update loads descriptor object +0x1F4 + loop*4"),
            (0x004600FC, 0xE3A01000, "z_kankyo thunder update binds selected CTXB payload into descriptor slot 0"),
            (0x00460100, 0xEBFBA257, "z_kankyo thunder update rebinds the selected 0x46..0x49 CTXB payload through 0x00348A64"),
            (0x0046030C, 0xE5901224, "z_kankyo thunder update writes transform data through runtime instance object +0x224 + loop*4"),
            (0x00460468, 0xE5900224, "z_kankyo thunder update submits/updates runtime instance object +0x224 + loop*4 through 0x00371EAC"),
        ],
    )
    runtime_descriptor_callsite_checks = code_instruction_checks(
        code_bin,
        [
            (0x003F8170, 0xE51F50C4, "Ganon2 callsite loads source id literal 0x0153 into r5"),
            (0x003F8178, 0xE3A03005, "Ganon2 callsite selects descriptor id 5"),
            (0x003F817C, 0xE1A02005, "Ganon2 callsite passes source id 0x0153 as r2"),
            (0x003F8184, 0xEBFDC9E6, "Ganon2 callsite invokes descriptor selector for ctx+0x344"),
            (0x003F8188, 0xE5840344, "Ganon2 callsite stores descriptor id 5 result at ctx+0x344"),
            (0x003F8190, 0xE3A03006, "Ganon2 callsite selects descriptor id 6"),
            (0x003F8194, 0xE1A02005, "Ganon2 callsite passes source id 0x0153 as r2 again"),
            (0x003F819C, 0xEBFDC9E0, "Ganon2 callsite invokes descriptor selector for ctx+0x348"),
            (0x003F81A0, 0xE5840348, "Ganon2 callsite stores descriptor id 6 result at ctx+0x348"),
            (0x003F81CC, 0xE3A03007, "Ganon2 callsite selects descriptor id 7"),
            (0x003F81D0, 0xE1A02005, "Ganon2 callsite passes source id 0x0153 for descriptor id 7"),
            (0x003F81E8, 0xEBFDC9CD, "Ganon2 callsite invokes descriptor selector for ctx+0x350"),
            (0x003F81EC, 0xE5840350, "Ganon2 callsite stores descriptor id 7 result at ctx+0x350"),
            (0x003F80B4, 0x00000153, "Ganon2 descriptor source id literal"),
        ],
    )
    light_list_helper_checks = code_instruction_checks(
        code_bin,
        [
            (0x0040FCA0, 0xE5902000, "helper 0x40FCA0 loads native light descriptor pointer"),
            (0x0040FCAC, 0xE592201C, "helper 0x40FCA0 reads descriptor flags at +0x1C"),
            (0x0040FCBC, 0xE3120080, "helper 0x40FCA0 gates a 12-byte-per-record block on flag 0x80"),
            (0x0040FCE4, 0xE3120010, "helper 0x40FCA0 gates an 8-byte-per-record block on flag 0x10"),
            (0x0040FD08, 0xE5900000, "helper 0x40FD08 loads native light descriptor pointer"),
            (0x0040FD10, 0xE590000C, "helper 0x40FD08 reads descriptor record count at +0x0C"),
            (0x0040FD20, 0xE0810100, "helper 0x40FD08 returns normalized_count * 12"),
            (0x0040FD28, 0xE5902000, "helper 0x40FD28 loads native light descriptor pointer"),
            (0x0040FD44, 0xE3120080, "helper 0x40FD28 gates a second 12-byte-per-record block on flag 0x80"),
            (0x0040FD70, 0xE5902000, "helper 0x40FD70 loads native light descriptor pointer"),
            (0x0040FD94, 0xE3110080, "helper 0x40FD70 gates the second 12-byte-per-record block on flag 0x80"),
            (0x0040FDB4, 0xE3110010, "helper 0x40FD70 gates the 8-byte-per-record block on flag 0x10"),
            (0x0040FDD4, 0xE3110008, "helper 0x40FD70 gates the 16-byte-per-record block on flag 0x08"),
            (0x0040FDF0, 0xE0800002, "helper 0x40FD70 returns accumulated native payload offset"),
            (0x00409270, 0xEB00008F, "dynamic light-list path calls 0x4094B4 to compute payload word count"),
            (0x00409288, 0xEBFC2902, "dynamic light-list path appends payload through 0x313698"),
            (0x004092AC, 0xEB000096, "dynamic light-list path appends packed vector/default slot through 0x40950C"),
            (0x004092D8, 0xEBFC2961, "dynamic light-list path finalizes the native PICA light list through 0x313864"),
            (0x00409520, 0xE58310D4, "packer 0x40950C stores light-list slot id/enable at +0xD4"),
            (0x0040952C, 0xED910A00, "packer 0x40950C reads source float payload words"),
            (0x00409588, 0xE58210D8, "packer 0x40950C stores packed payload word 0 at +0xD8"),
            (0x004095A8, 0xE58210DC, "packer 0x40950C stores packed payload word 1 at +0xDC"),
            (0x004095C8, 0xE58210E0, "packer 0x40950C stores packed payload word 2 at +0xE0"),
        ],
    )
    native_kankyo_resource_bridge_checks = code_instruction_checks(
        code_bin,
        [
            (0x004502E0, 0xE2850B0E, "z_kankyo init selects play/resource context region at play +0x3800"),
            (0x004502E4, 0xE2800F96, "z_kankyo init selects resource context +0x3858"),
            (0x004502E8, 0xE3A01002, "z_kankyo init requests native resource-table source id 2"),
            (0x004502F0, 0xEBFC4E46, "z_kankyo init resolves source id 2 through resource-table scanner 0x00363C10"),
            (0x00450318, 0xE59F10C8, "z_kankyo init loads kankyo_common.zar path literal pointer"),
            (0x00450324, 0xEBFC8503, "z_kankyo init copies the kankyo_common.zar path/template structure"),
            (0x00450330, 0xEBFA38E0, "z_kankyo init opens/builds the kankyo_common provider handle"),
            (0x00450338, 0xE5840264, "z_kankyo init stores opened kankyo provider at play/environment object +0x264"),
            (0x004503E8, 0x004DA6CC, "literal pointer to rom:/kankyo/kankyo_common.zar"),
            (0x00450408, 0xE59D08D0, "z_kankyo init reloads native resource context pointer"),
            (0x0045040C, 0xE3A01001, "z_kankyo init requests resource-table source id 1 for the current kankyo variant"),
            (0x00450410, 0xEBFC4DFE, "z_kankyo init resolves source id 1 through resource-table scanner 0x00363C10"),
            (0x00450420, 0xE59F1588, "z_kankyo init loads the native kankyo variant pointer table"),
            (0x00450424, 0xE0850380, "z_kankyo init applies 0x80-byte resource-table stride for the variant slot"),
            (0x00450434, 0x12800F97, "z_kankyo init selects resource context +0x385C when the variant table entry exists"),
            (0x00450440, 0xE2800010, "z_kankyo init skips the native resource-table entry header to select payload data"),
            (0x00450444, 0xE58D08CC, "z_kankyo init stores the selected kankyo payload pointer on the stack"),
            (0x004504F4, 0xE3A01044, "z_kankyo init requests kankyo subresource id 0x44"),
            (0x004504F8, 0xEBFC89E4, "z_kankyo init resolves subresource 0x44 through 0x00372C90"),
            (0x0045053C, 0xE3A01000, "z_kankyo init binds subresource 0x44 as descriptor slot 0"),
            (0x0045054C, 0xEBFBE144, "z_kankyo init binds subresource 0x44 through descriptor/source binder 0x00348A64"),
            (0x00450690, 0xE3A01045, "z_kankyo init requests kankyo subresource id 0x45"),
            (0x00450694, 0xEBFC897D, "z_kankyo init resolves subresource 0x45 through 0x00372C90"),
            (0x004506DC, 0xE3A01000, "z_kankyo init binds subresource 0x45 as descriptor slot 0"),
            (0x004506E0, 0xEBFBE0DF, "z_kankyo init binds subresource 0x45 through descriptor/source binder 0x00348A64"),
            (0x004507B4, 0xE2861046, "z_kankyo init loops over kankyo subresource ids 0x46..0x49"),
            (0x004507B8, 0xEBFC8934, "z_kankyo init resolves subresources 0x46..0x49 through 0x00372C90"),
            (0x004507C8, 0xE5810254, "z_kankyo init stores resolved subresources 0x46..0x49 at object +0x254 + index*4"),
            (0x00450818, 0xE58B01F4, "z_kankyo init stores each thunder descriptor object at object +0x1F4 + loop*4"),
            (0x00450828, 0xE5942254, "z_kankyo init reloads object +0x254, the first resolved 0x46 thunder CTXB payload, for binding"),
            (0x00450830, 0xE3A01000, "z_kankyo init binds the thunder descriptor object as descriptor slot 0"),
            (0x00450834, 0xEBFBE08A, "z_kankyo init binds the object +0x254 thunder payload through 0x00348A64"),
            (0x004508B0, 0xE356000C, "z_kankyo init creates 12 thunder descriptor objects from the first resolved thunder payload"),
            (0x00450984, 0xE3A0104A, "z_kankyo init requests kankyo subresource id 0x4A"),
            (0x0045098C, 0xEBFC88BF, "z_kankyo init resolves subresource 0x4A through 0x00372C90"),
            (0x00450994, 0xE3A0104B, "z_kankyo init requests kankyo subresource id 0x4B"),
            (0x0045099C, 0xEBFC88BB, "z_kankyo init resolves subresource 0x4B through 0x00372C90"),
            (0x00450A6C, 0xE5940270, "z_kankyo init loads the descriptor object allocated for subresources 0x4A/0x4B"),
            (0x00450A78, 0xE3A01000, "z_kankyo init selects descriptor slot 0 for subresource 0x4A"),
            (0x00450A7C, 0xEBFBDFF8, "z_kankyo init binds subresource 0x4A as descriptor slot 0 through 0x00348A64"),
            (0x00450A8C, 0xE5940270, "z_kankyo init reloads the descriptor object allocated for subresources 0x4A/0x4B"),
            (0x00450A98, 0xE3A01001, "z_kankyo init selects descriptor slot 1 for subresource 0x4B"),
            (0x00450A9C, 0xEBFBDFF0, "z_kankyo init binds subresource 0x4B as descriptor slot 1 through 0x00348A64"),
            (0x004488B4, 0xE1A00004, "environment update caller passes PlayState to native environment sync helper"),
            (0x004488B8, 0xEB002098, "environment update caller invokes z_kankyo sync helper 0x00450B20"),
            (0x00450B20, 0xE2803A02, "z_kankyo sync helper selects play +0x2000"),
            (0x00450B28, 0xE2833FA6, "z_kankyo sync helper selects environment subregion play +0x2298"),
            (0x00450B34, 0xE5930258, "z_kankyo sync helper reads word at env subregion +0x258"),
            (0x00450B38, 0xE593325C, "z_kankyo sync helper reads word at env subregion +0x25C"),
            (0x00450B40, 0x15810020, "z_kankyo sync helper optionally mirrors +0x258 to global fallback +0x20"),
            (0x00450B44, 0x15813024, "z_kankyo sync helper optionally mirrors +0x25C to global fallback +0x24"),
            (0x00450B48, 0xE5820260, "z_kankyo sync helper stores previous +0x258 value to env subregion +0x260"),
            (0x00450B4C, 0xE5823264, "z_kankyo sync helper stores previous +0x25C value to env subregion +0x264"),
            (0x00450B50, 0xE5920270, "z_kankyo sync helper reads env subregion +0x270"),
            (0x00450B54, 0xE5820274, "z_kankyo sync helper stores previous +0x270 value to env subregion +0x274"),
        ],
    )
    native_resource_resolver_checks = code_instruction_checks(
        code_bin,
        [
            (0x00363C10, 0xE52D4004, "source-id resolver 0x00363C10 entry"),
            (0x00363C14, 0xE5D04000, "source-id resolver reads resource-table entry count byte"),
            (0x00363C30, 0xE08C3382, "source-id resolver selects entry base with 0x80-byte stride"),
            (0x00363C34, 0xE1D330F4, "source-id resolver reads signed source id at entry +0x04"),
            (0x00363C3C, 0xB2633000, "source-id resolver normalizes negative source ids by absolute value"),
            (0x00363C40, 0xE1530001, "source-id resolver compares normalized source id against requested id"),
            (0x00363C48, 0xE1A00002, "source-id resolver returns the matching entry index"),
            (0x00363C80, 0xE1D328F4, "source-id resolver reads paired entry source id at next entry +0x04"),
            (0x00363C94, 0xE49D4004, "source-id resolver pops before returning the paired matching entry index"),
            (0x00363CB0, 0xE3E00000, "source-id resolver returns -1 when no entry matches"),
            (0x00372C90, 0xE92D4070, "subresource resolver 0x00372C90 entry"),
            (0x00372C9C, 0xE5941028, "subresource resolver reads active resource-table index at provider +0x28"),
            (0x00372CA8, 0x1594200C, "subresource resolver reads provider table pointer at provider +0x0C"),
            (0x00372CAC, 0x17921201, "subresource resolver reads active-table subresource count from table[index * 0x10]"),
            (0x00372CB4, 0xE1510005, "subresource resolver bounds-checks requested subresource id against active count"),
            (0x00372CBC, 0xE5940054, "subresource resolver reads lazy cache table at provider +0x54"),
            (0x00372CC0, 0xE7900105, "subresource resolver reads cached decoded payload pointer at cache[id]"),
            (0x00372CF4, 0xE5941028, "subresource resolver reloads active resource-table index for uncached decode"),
            (0x00372D04, 0xE5943014, "subresource resolver reads offset indirection table at provider +0x14"),
            (0x00372D08, 0xE7922001, "subresource resolver reads active subresource offset-list base"),
            (0x00372D14, 0xE7922105, "subresource resolver reads subresource offset-list entry for requested id"),
            (0x00372D18, 0xE7932102, "subresource resolver resolves final payload offset through provider +0x14 indirection"),
            (0x00372D28, 0xEBFE3961, "subresource resolver decodes/opens payload through helper 0x003012B4"),
            (0x00372D30, 0xE7810105, "subresource resolver stores decoded payload pointer back into cache[id]"),
            (0x00372D38, 0xE5940054, "subresource resolver returns cached decoded payload pointer"),
        ],
    )
    native_provider_open_checks = code_instruction_checks(
        code_bin,
        [
            (0x002DE6B8, 0xE92D4030, "provider open helper 0x002DE6B8 entry"),
            (0x002DE6BC, 0xE24DDF83, "provider open helper reserves a 0x20C-byte temporary request/path frame"),
            (0x002DE6C0, 0xE59F206C, "provider open helper loads path conversion limit literal"),
            (0x002DE6C8, 0xEB011A1D, "provider open helper converts/copies caller path through 0x00324F44"),
            (0x002DE6DC, 0xEB008B07, "provider open helper creates a native resource request through 0x00301300"),
            (0x002DE6EC, 0xEB00F4B3, "provider open helper polls/waits for request state through 0x0031B9C0"),
            (0x002DE6F8, 0x1A000005, "provider open helper loops until the request is ready"),
            (0x002DE714, 0xE1A00004, "provider open helper passes request object to handle extractor"),
            (0x002DE724, 0xEB00F49C, "provider open helper releases the native resource request through 0x0031B99C"),
            (0x002DE730, 0xE8BD8030, "provider open helper returns extracted provider handle"),
            (0x002DE734, 0x00000106, "provider open helper path conversion limit literal"),
            (0x00301314, 0xE3A00F46, "resource request constructor allocates 0x118 bytes"),
            (0x00301334, 0xE5840000, "resource request constructor writes vtable/control pointer"),
            (0x00301348, 0xEB0011B4, "resource request constructor copies converted path into request +0x14"),
            (0x00301354, 0xE584800C, "resource request constructor stores caller request mode at +0x0C"),
            (0x00301358, 0xE5845010, "resource request constructor clears loaded handle/result pointer at +0x10"),
            (0x0030135C, 0xE5C45008, "resource request constructor initializes request status byte +0x08 to 0"),
            (0x0030137C, 0xEB0016AF, "resource request constructor queues/registers request through 0x00306E40"),
            (0x00303EA8, 0xE5D01008, "provider handle extractor reads request status byte +0x08"),
            (0x00303EAC, 0xE3510001, "provider handle extractor requires status 1"),
            (0x00303EB0, 0x05900010, "provider handle extractor returns loaded handle at request +0x10 when ready"),
            (0x00303EB4, 0x13A00000, "provider handle extractor returns null when request is not ready"),
            (0x0031B9C0, 0xE92D41F0, "request wait/poll helper 0x0031B9C0 entry"),
            (0x0031BA9C, 0xE5D50008, "request wait loop reloads request status byte +0x08"),
            (0x0031BAA4, 0x1AFFFFCE, "request wait loop repeats until request status becomes 1"),
            (0x0031B99C, 0xE3500000, "request release helper ignores null requests"),
            (0x0031B9B0, 0xEB000002, "request release helper waits/polls before releasing"),
            (0x0031B9BC, 0xEA00D0AA, "request release helper tail-calls free/release helper 0x0034FC6C"),
        ],
    )
    native_zar_resolver_family_checks = code_instruction_checks(
        code_bin,
        [
            (0x00358EF8, 0xE92D4070, "public-symbol ZAR_GetCMBByIndex resolver entry"),
            (0x00358F04, 0xE594101C, "CMB resolver reads active table index at provider +0x1C"),
            (0x00358F10, 0x1594200C, "CMB resolver reads provider table pointer at provider +0x0C"),
            (0x00358F14, 0x17921201, "CMB resolver reads active-table count from table[index * 0x10]"),
            (0x00358F1C, 0xE1510005, "CMB resolver bounds-checks requested index"),
            (0x00358F24, 0xE594004C, "CMB resolver reads decoded CMB cache table at provider +0x4C"),
            (0x00358F28, 0xE7900105, "CMB resolver reads cached decoded CMB pointer"),
            (0x00358F6C, 0xE5943014, "CMB resolver reads shared offset indirection table at provider +0x14"),
            (0x00358F7C, 0xE7922105, "CMB resolver reads CMB offset-list entry for requested index"),
            (0x00358F80, 0xE7932102, "CMB resolver resolves final CMB payload offset through provider +0x14"),
            (0x00358F88, 0xEBFF1D32, "CMB resolver decodes/opens CMB payload through 0x00320458"),
            (0x00358F90, 0xE7810105, "CMB resolver stores decoded CMB pointer back into cache[index]"),
            (0x00358FD8, 0xE594004C, "CMB resolver reloads decoded CMB cache table"),
            (0x00358FE4, 0xE5801038, "CMB resolver tags decoded CMB object with callback/static pointer at +0x38"),
            (0x00358FF4, 0xEBFF1BDA, "CMB resolver finalizes decoded CMB object through 0x0031FF64"),
            (0x00372C90, 0xE92D4070, "public-symbol ZAR_GetCTXBByIndex resolver entry"),
            (0x00372C9C, 0xE5941028, "CTXB resolver reads active table index at provider +0x28"),
            (0x00372CBC, 0xE5940054, "CTXB resolver reads decoded CTXB cache table at provider +0x54"),
            (0x00372D04, 0xE5943014, "CTXB resolver shares the same offset indirection table at provider +0x14"),
            (0x00372D28, 0xEBFE3961, "CTXB resolver decodes/opens CTXB payload through 0x003012B4"),
        ],
    )
    native_zar_setup_checks = code_instruction_checks(
        code_bin,
        [
            (0x0031B124, 0xE92D47F0, "public-symbol ZAR_SetupZARInfo entry"),
            (0x0031B130, 0xE5801018, "ZAR setup stores source archive pointer at provider +0x18"),
            (0x0031B134, 0xE580A00C, "ZAR setup clears provider table pointer before header decode"),
            (0x0031B13C, 0xE8800006, "ZAR setup stores base archive pointer and secondary pointer at provider +0x00/+0x04"),
            (0x0031B154, 0xE584000C, "ZAR setup stores archive table pointer at provider +0x0C from header +0x0C"),
            (0x0031B160, 0xE5840010, "ZAR setup stores archive name/data table pointer at provider +0x10 from header +0x10"),
            (0x0031B16C, 0xE5840014, "ZAR setup stores offset indirection table at provider +0x14 from header +0x14"),
            (0x0031B17C, 0xE584201C, "ZAR setup initializes first type active-index field at provider +0x1C to -1"),
            (0x0031B1A8, 0xE59F87D8, "ZAR setup loads native type-name table pointer"),
            (0x0031B1C4, 0xE7980185, "ZAR setup loads a type-name pointer from the native type-name table"),
            (0x0031B1C8, 0xEBFFA091, "ZAR setup compares archive type names through string compare helper 0x00303414"),
            (0x0031B1D4, 0x0580701C, "ZAR setup stores matched archive section index at provider +0x1C + type_slot*4"),
            (0x0031B1E0, 0xE355000B, "ZAR setup scans eleven native type slots"),
            (0x0031B1FC, 0xE584A04C, "ZAR setup clears decoded CMB cache pointer at provider +0x4C"),
            (0x0031B200, 0xE584A050, "ZAR setup clears decoded CSAB cache pointer at provider +0x50"),
            (0x0031B204, 0xE584A054, "ZAR setup clears decoded CTXB cache pointer at provider +0x54"),
            (0x0031B220, 0xE594001C, "ZAR setup reads CMB active-index field provider +0x1C"),
            (0x0031B248, 0xE584004C, "ZAR setup allocates decoded CMB cache table at provider +0x4C"),
            (0x0031B294, 0xE5940020, "ZAR setup reads CSAB active-index field provider +0x20"),
            (0x0031B2B8, 0xE5840050, "ZAR setup allocates decoded CSAB cache table at provider +0x50"),
            (0x0031B434, 0xE5940028, "ZAR setup reads CTXB active-index field provider +0x28"),
            (0x0031B454, 0xEB00D32C, "ZAR setup allocates decoded CTXB cache table through allocator 0x0035010C"),
            (0x0031B458, 0xE5840054, "ZAR setup stores decoded CTXB cache table at provider +0x54"),
            (0x0031B4A4, 0xE5D12000, "ZAR setup reads CTXB payload magic byte 0"),
            (0x0031B4A8, 0xE3520063, "ZAR setup checks CTXB magic byte 'c'"),
            (0x0031B4B0, 0x03520074, "ZAR setup checks CTXB magic byte 't'"),
            (0x0031B4B8, 0x03520078, "ZAR setup checks CTXB magic byte 'x'"),
            (0x0031B4C8, 0xE5941054, "ZAR setup reloads decoded CTXB cache table"),
            (0x0031B4CC, 0xE781A100, "ZAR setup initializes CTXB cache entry to null for matching ctxb payloads"),
            (0x0031B988, 0x0050BBA0, "literal pointer to native ZAR type-name table"),
        ],
    )
    code_data = code_bin.read_bytes() if code_bin is not None and code_bin.is_file() else b""
    native_object_bank_inventory = collect_native_object_bank_inventory(code_bin, romfs_root)
    literal_constants: list[dict[str, Any]] = []
    for runtime_address, name in [
        (0x00313438, "enabled_gate_one"),
        (0x0031343C, "disabled_fallback_zero"),
        (0x00313440, "disabled_fallback_negative_z"),
    ]:
        word = read_code_word(code_data, runtime_address) if code_data else None
        literal_constants.append(
            {
                "name": name,
                "runtime_address": hex_addr(runtime_address),
                "code_bin_offset": hex_addr(runtime_address - CODE_BIN_RUNTIME_BASE),
                "value": hex32(word) if word is not None else "",
            }
        )
    default_record_literal_pointers: list[dict[str, Any]] = []
    for runtime_address, name, byte_count, stack_destination in [
        (0x004731D8, "default_subtable_r9", "0xC0", "sp+0x2E0"),
        (0x004731DC, "default_subtable_sp_0x1e0", "0x100", "sp+0x1E0"),
        (0x004731E0, "default_subtable_r6", "0x80", "sp+0x160"),
        (0x004731E4, "default_subtable_sp_0x130", "0x28", "sp+0x130"),
        (0x004731E8, "default_record_main_sp_0x18", "0x118", "sp+0x18"),
    ]:
        word = read_code_word(code_data, runtime_address) if code_data else None
        default_record_literal_pointers.append(
            {
                "name": name,
                "literal_pointer_address": hex_addr(runtime_address),
                "table_runtime_address": hex32(word) if word is not None else "",
                "byte_count": byte_count,
                "stack_destination": stack_destination,
            }
        )

    provider_table_address = 0x004D3FE8
    provider_slot_suffix_table_address = provider_table_address + 10 * 4
    provider_language_prefixes: list[dict[str, Any]] = []
    provider_slot_suffixes: list[dict[str, Any]] = []
    for index in range(10):
        pointer_address = provider_table_address + index * 4
        pointer = read_code_word(code_data, pointer_address) if code_data else None
        provider_language_prefixes.append(
            {
                "index": index,
                "pointer_table_address": hex_addr(pointer_address),
                "string_pointer": hex32(pointer) if pointer is not None else "",
                "text": read_code_c_string(code_data, pointer) if pointer is not None else "",
            }
        )
    for index in range(16):
        pointer_address = provider_slot_suffix_table_address + index * 4
        pointer = read_code_word(code_data, pointer_address) if code_data else None
        provider_slot_suffixes.append(
            {
                "slot": index,
                "pointer_table_address": hex_addr(pointer_address),
                "string_pointer": hex32(pointer) if pointer is not None else "",
                "text": read_code_c_string(code_data, pointer) if pointer is not None else "",
            }
        )
    selected_provider_suffix = (
        provider_slot_suffixes[13]["text"] if len(provider_slot_suffixes) > 13 else ""
    )
    provider_path_format = read_code_c_string(code_data, 0x004D4050) if code_data else ""
    z_movie_hint_movie_paths: list[dict[str, Any]] = []
    z_movie_hint_movie_path_table = 0x004DB1B8
    for index in range(10):
        pointer_address = z_movie_hint_movie_path_table + index * 4
        pointer = read_code_word(code_data, pointer_address) if code_data else None
        z_movie_hint_movie_paths.append(
            {
                "language_index": index,
                "pointer_table_address": hex_addr(pointer_address),
                "string_pointer": hex32(pointer) if pointer is not None else "",
                "text": read_code_u16_string(code_data, pointer) if pointer is not None else "",
            }
        )
    z_movie_resource_context_debug_path = (
        read_code_c_string(code_data, 0x004724E4) if code_data else ""
    )
    ganon2_descriptor_callsite_debug_path = (
        read_code_c_string(code_data, 0x003F80D4) if code_data else ""
    )
    z_kankyo_debug_path = read_code_c_string(code_data, 0x00450A49) if code_data else ""
    kankyo_common_path = read_code_c_string(code_data, 0x004DA6CC) if code_data else ""
    native_resource_resolver_contract = {
        "source_id_resolver": {
            "function": "0x00363C10",
            "input": "resource_context table pointer, requested native source id",
            "entry_count_field": "table +0x00 byte",
            "entry_stride_bytes": "0x80",
            "source_id_field": "entry +0x04 signed halfword",
            "comparison_rule": "negative source ids are compared by absolute value",
            "return_value": "matching resource-table index, or -1 when absent",
            "paired_scan_note": "the optimized loop checks two adjacent 0x80-byte records at a time after an odd first entry",
        },
        "subresource_resolver": {
            "function": "0x00372C90",
            "input": "provider/resource payload pointer and native subresource id",
            "active_table_index_field": "provider +0x28",
            "provider_table_pointer_field": "provider +0x0C",
            "offset_indirection_table_field": "provider +0x14",
            "decoded_payload_cache_field": "provider +0x54",
            "active_subresource_count_source": "[provider+0x0C][active_index * 0x10]",
            "bounds_rule": "requested id must be lower than the active subresource count; otherwise the resolver returns null",
            "offset_resolution": "base provider payload + offset_table[subresource_offset_list[id]]",
            "decode_helper": "0x003012B4",
            "cache_behavior": "decoded payload pointers are cached in provider+0x54 at id * 4 and reused on later calls",
        },
        "kankyo_ids_interpretation": "z_kankyo subresources 0x44..0x4B are native provider subresource ids resolved through 0x00372C90; they must not be treated as direct ZAR file indices or hardcoded archive positions",
        "engine_requirement": "implement provider-table/subresource-table decoding before binding kankyo lighting/effect payloads in the engine",
        "remaining_gap": "identify where the resource provider object is populated from native kankyo container/resource tables and map each id 0x44..0x4B to a concrete native payload name/type",
    }
    native_resource_context_payload_contract = {
        "resource_context_table_base": "play +0x3800 +0x258",
        "source_id_resolver": "0x00363C10",
        "resource_entry_stride_bytes": "0x80",
        "entry_source_id_field": "entry +0x04 signed halfword",
        "entry_availability_pointer_field": "entry +0x0C",
        "entry_availability_pointer_absolute_offset": "play +0x3A64 + index*0x80",
        "entry_header_base_expression": "play +0x3800 +0x25C + index*0x80",
        "entry_payload_base_expression": "play +0x3A6C + index*0x80",
        "selection_rule": (
            "if Object_GetIndex(resource_context_table, source_id) returns index < 19 "
            "and the availability pointer at entry +0x0C is non-null, consumers pass "
            "entry +0x14 as the provider/subresource payload"
        ),
        "verified_payload_lookup_consumers": [
            {
                "callsite": "0x00450410",
                "source_id": 1,
                "scope": "z_kankyo current environment variant",
                "payload_consumer": "ZAR_GetCTXBByIndex ids 0x44..0x4B",
            },
            {
                "callsite": "0x0036A940",
                "source_id": "caller supplied",
                "scope": "native descriptor selector",
                "payload_consumer": "ZAR_GetCMBByIndex/descriptor id path",
            },
            {
                "callsite": "0x0044E93C",
                "source_id": 1,
                "scope": "native environment/kankyo adjacent consumer",
                "payload_consumer": "ZAR_GetCMBByIndex id 0x2D",
            },
            {
                "callsite": "0x00452174",
                "source_id": 1,
                "scope": "fbdemo wipe path",
                "payload_consumer": "ZAR_GetCTXBByIndex ids 0x40..0x43",
            },
            {
                "callsite": "0x00462174",
                "source_id": 1,
                "scope": "z_lights path",
                "payload_consumer": "ZAR_GetCTXBByIndex id 1",
            },
        ],
        "meaning": "source id 1 is not a single hardcoded archive; it selects a runtime resource-context entry whose payload is then consumed by native ZAR subresource resolvers",
        "remaining_gap": "use the object-bank population contract to decode the selected provider payload contents and downstream descriptor/light-list binding",
    }
    native_object_bank_population_contract = {
        "object_spawn_function": "0x0032E21C",
        "object_update_bank_function": "0x002E4EA0",
        "public_symbols_source": "tools/oot3d/decomp_support/symbols/manual_symbols.csv",
        "object_table_pointer": native_object_bank_inventory.get(
            "object_table_runtime_address", ""
        ),
        "object_table_stride_bytes": "0x44",
        "object_table_entry_path_field": "entry +0x00 inline rom:/ path string",
        "object_table_entry_cached_path_field": "entry +0x40",
        "object_bank_entry_stride_bytes": "0x80",
        "object_bank_entry_fields": {
            "+0x00": "bank count is stored before entries; entries begin at base + count*0x80",
            "+0x04": "signed object id/source id",
            "+0x08": "loaded archive buffer pointer",
            "+0x0C": "converted path/provider pointer",
            "+0x10": "pending request handle while loading",
            "+0x14": "ZAR provider object initialized by ZAR_SetupZARInfo",
        },
        "source_id_1_mapping": next(
            (
                row
                for row in native_object_bank_inventory.get("source_rows", [])
                if row.get("source_id") == 1
            ),
            {},
        ),
        "source_id_2_mapping": next(
            (
                row
                for row in native_object_bank_inventory.get("source_rows", [])
                if row.get("source_id") == 2
            ),
            {},
        ),
        "meaning": (
            "Object_Spawn/Object_UpdateBank populate the same 0x80-byte object bank "
            "entries consumed by z_kankyo; source id 1 maps through the native object "
            "table to zelda_keep.zar, where CTXB local indices 0x44..0x4B are concrete "
            "kankyo rain/thunder/storm textures."
        ),
        "remaining_gap": (
            "promote the decoded CTXB payload bodies through the native descriptor "
            "slot contract and downstream light-list/effect binding"
        ),
    }
    native_ctxb_descriptor_binding_contract = {
        "function": "0x00348A64",
        "slot_index_argument": "r1",
        "decoded_source_argument": "r2",
        "caller_token_argument": "r3",
        "slot_stride_bytes": "0x30",
        "slot_record_base_expression": "descriptor_object + 0x13C + slot_index * 0x30",
        "source_texture_header_base": "decoded_source +0x24",
        "source_fields": {
            "+0x28": "signed texture/header parameter consumed with slot +0x140",
            "+0x2C": "native CTXB width copied to slot +0x15C",
            "+0x2E": "native CTXB height copied to slot +0x15E",
            "+0x30": "native CTXB format consumed by 0x0030807C",
            "+0x32": "native CTXB data type consumed by 0x0030807C",
            "+0x4C": "decoded texture payload pointer copied to slot +0x158",
        },
        "slot_fields": {
            "+0x13C/+0x140/+0x144/+0x148": "converted caller/stack descriptor tokens",
            "+0x158": "decoded texture payload pointer",
            "+0x15C": "native CTXB width",
            "+0x15E": "native CTXB height",
            "+0x160": "derived sampler/texture mode from slot index and literal 0x0DE1",
            "+0x164": "packed native CTXB format/data-type",
        },
        "literal_parameters": {
            "0x00348B84": "-1000",
            "0x00348B88": "0",
            "0x00348B8C": "0x0DE1",
        },
        "meaning": (
            "0x00348A64 is the native bridge from a decoded CTXB/source object into "
            "descriptor texture slots; it preserves source width, height, format, "
            "data type, and decoded payload pointer instead of substituting a runtime "
            "N64-style texture."
        ),
        "remaining_gap": (
            "wire the engine-side CTXB descriptor slot object into the downstream "
            "descriptor/light-list/effect consumer instead of binding kankyo CTXB ids "
            "directly by hand"
        ),
    }
    native_provider_open_contract = {
        "provider_open_helper": "0x002DE6B8",
        "caller_scope": "z_kankyo init",
        "opened_path": "rom:/kankyo/kankyo_common.zar",
        "path_conversion_helper": "0x00324F44",
        "path_conversion_limit": "0x106",
        "temporary_stack_frame_size": "0x20C",
        "resource_request_constructor": "0x00301300",
        "resource_request_size": "0x118",
        "request_path_field": "request +0x14",
        "request_mode_field": "request +0x0C",
        "request_loaded_handle_field": "request +0x10",
        "request_status_field": "request +0x08",
        "request_ready_value": 1,
        "request_wait_or_poll_helper": "0x0031B9C0",
        "request_handle_extractor": "0x00303EA8",
        "request_release_helper": "0x0031B99C",
        "return_value": "loaded provider handle from request +0x10 when request +0x08 == 1, otherwise null",
        "meaning": "z_kankyo obtains a native provider handle by loading the kankyo_common.zar path through the game's resource-request system before subresource ids are resolved",
        "remaining_gap": "decompile the provider object's internal table initialization after the archive request completes, especially fields +0x0C/+0x14/+0x28/+0x54 consumed by 0x00372C90",
    }
    native_zar_resolver_family_contract = {
        "symbol_source": "tools/oot3d/decomp_support/symbols/manual_symbols.csv",
        "symbols": [
            {
                "name": "ZAR_GetCMBByIndex",
                "address": "0x00358EF8",
                "active_index_field": "provider +0x1C",
                "decoded_cache_field": "provider +0x4C",
                "decode_helper": "0x00320458",
            },
            {
                "name": "ZAR_GetCTXBByIndex",
                "address": "0x00372C90",
                "active_index_field": "provider +0x28",
                "decoded_cache_field": "provider +0x54",
                "decode_helper": "0x003012B4",
            },
        ],
        "shared_fields": {
            "provider_table_pointer": "provider +0x0C",
            "offset_indirection_table": "provider +0x14",
            "active_count_expression": "[provider+0x0C][active_index * 0x10]",
        },
        "meaning": "the kankyo subresource calls at 0x00372C90 are CTXB-index lookups in the native ZAR resolver family, not generic file-index or trace-derived texture substitutions",
        "remaining_gap": "map the provider's active CTXB table index +0x28 and CTXB cache +0x54 back to concrete kankyo archive records after provider table initialization is decoded",
    }
    native_zar_setup_type_table: list[dict[str, Any]] = []
    native_zar_type_table_address = (
        read_code_word(code_data, 0x0031B988) if code_data else None
    )
    if native_zar_type_table_address is not None:
        for index in range(11):
            entry_address = native_zar_type_table_address + index * 8
            name_pointer = read_code_word(code_data, entry_address)
            type_id = read_code_word(code_data, entry_address + 4)
            native_zar_setup_type_table.append(
                {
                    "type_slot": index,
                    "name_pointer": hex32(name_pointer) if name_pointer is not None else "",
                    "name": read_code_c_string(code_data, name_pointer)
                    if name_pointer is not None
                    else "",
                    "type_id": type_id,
                    "active_index_field": hex_addr(0x1C + index * 4),
                }
            )
    native_zar_setup_contract = {
        "function": "0x0031B124",
        "symbol": "ZAR_SetupZARInfo",
        "symbol_source": "tools/oot3d/decomp_support/symbols/manual_symbols.csv",
        "archive_header_fields": {
            "provider +0x0C": "archive base + header word 0x0C",
            "provider +0x10": "archive base + header word 0x10",
            "provider +0x14": "archive base + header word 0x14",
        },
        "type_table_pointer_literal": "0x0031B988",
        "type_table_address": hex32(native_zar_type_table_address)
        if native_zar_type_table_address is not None
        else "",
        "type_table": native_zar_setup_type_table,
        "type_active_index_base": "provider +0x1C",
        "type_active_index_stride_bytes": 4,
        "ctxb_type_slot": 3,
        "ctxb_active_index_field": "provider +0x28",
        "ctxb_cache_field": "provider +0x54",
        "ctxb_cache_initialization": "allocates count*4 for the active ctxb section, then initializes cache entries to null only for payloads with ctxb magic",
        "meaning": "provider +0x28 is the active archive section index for the native ctxb type slot; provider +0x54 is the decoded CTXB cache used by ZAR_GetCTXBByIndex",
        "remaining_gap": "use this type-slot mapping to resolve the active kankyo CTXB section records selected by source ids 1/2 into concrete archive payload names",
    }
    native_kankyo_resource_bridge_contract = {
        "debug_source_file": z_kankyo_debug_path,
        "common_archive_path": kankyo_common_path,
        "resource_context_pointer": "play +0x3800 +0x258",
        "resource_source_ids": [
            {
                "source_id": 2,
                "resolver": "0x00363C10",
                "meaning": "kankyo init checks OBJECT_GAMEPLAY_FIELD_KEEP availability before opening the common kankyo provider",
            },
            {
                "source_id": 1,
                "resolver": "0x00363C10",
                "meaning": "kankyo init selects OBJECT_GAMEPLAY_KEEP, whose zelda_keep.zar CTXB table contains kankyo ids 0x44..0x4B",
            },
        ],
        "variant_table": {
            "table_pointer_literal": "0x00450420",
            "entry_stride_bytes": "0x80",
            "payload_base_expression": "play +0x3800 + object_bank_index*0x80 +0x25C +0x10",
        },
        "subresource_ids": ["0x44", "0x45", "0x46..0x49", "0x4A", "0x4B"],
        "subresource_resolver": "0x00372C90",
        "descriptor_binder": "0x00348A64",
        "descriptor_slot_bindings": [
            {
                "subresource_id": "0x44",
                "descriptor_object_field": "object +0x1E4",
                "descriptor_slot": 0,
                "binding_callsite": "0x0045054C",
            },
            {
                "subresource_id": "0x45",
                "descriptor_object_field": "object +0x1EC",
                "descriptor_slot": 0,
                "binding_callsite": "0x004506E0",
            },
            {
                "subresource_id": "0x46",
                "descriptor_object_field": "object +0x1F4 + loop*4",
                "descriptor_slot": 0,
                "binding_callsite": "0x00450834",
                "descriptor_object_count": 12,
                "note": "init resolves 0x46..0x49 into object +0x254/+0x258/+0x25C/+0x260 and seeds all 12 descriptor objects from object +0x254; the update consumer at 0x004600D8..0x00460100 later selects object +0x254 + selector*4 and rebinds the active thunder CTXB payload",
            },
            {
                "subresource_id": "0x4A",
                "descriptor_object_field": "object +0x270",
                "descriptor_slot": 0,
                "binding_callsite": "0x00450A7C",
            },
            {
                "subresource_id": "0x4B",
                "descriptor_object_field": "object +0x270",
                "descriptor_slot": 1,
                "binding_callsite": "0x00450A9C",
            },
        ],
        "sync_helper": {
            "function": "0x00450B20",
            "single_verified_caller": "0x004488B8",
            "source_region": "play +0x2000 +0x298",
            "copied_fields": {
                "+0x258": "+0x260",
                "+0x25C": "+0x264",
                "+0x270": "+0x274",
            },
            "optional_global_mirror": "0x00531EB4 +0x20/+0x24 when caller r1 is non-zero",
        },
        "meaning": "native code points environment lighting/effects setup at kankyo_common.zar and OBJECT_GAMEPLAY_KEEP kankyo CTXB subresources before the PICA light packet is built; descriptor slot arguments are now verified for the init bindings, including the 0x4A/0x4B two-slot descriptor object",
        "remaining_gap": "decompile the z_kankyo update/draw path beyond the verified thunder CTXB selector, and map active kankyo runtime instances into the ctx+0x350/+0x354/+0x358 light/effect descriptor consumed by 0x003FBBD8 -> 0x003130A4",
    }
    native_kankyo_descriptor_instance_contract = {
        "runtime_instance_helpers": [
            {
                "function": "0x00340D00",
                "container_storage_size": "0x35C",
                "instance_storage_size": "0x28C",
                "container_class_id": "0x52",
                "instance_class_id": "0x53",
                "backing_storage_class_id": "0x5D",
                "instance_initializer": "0x004970E0",
            },
            {
                "function": "0x0034897C",
                "container_storage_size": "0x35C",
                "instance_storage_size": "0x1E4",
                "container_class_id": "0x2E",
                "instance_class_id": "0x2F",
                "backing_storage_class_id": "0x39",
                "instance_initializer": "0x002C4F00",
            },
        ],
        "shared_runtime_container_fields": {
            "+0x354": "created effect/runtime instance",
            "+0x358": "shared descriptor backing pointer",
        },
        "shared_instance_fields": {
            "+0x1DC": "shared descriptor backing pointer when caller did not pass one",
        },
        "materialization_helper": "0x00348BE4",
        "callsite_bindings": [
            {
                "subresource_id": "0x44",
                "descriptor_object_field": "object +0x1E4",
                "runtime_instance_field": "object +0x1E8",
                "runtime_helper": "0x00340D00",
                "runtime_callsite": "0x004505A8",
            },
            {
                "subresource_id": "0x45",
                "descriptor_object_field": "object +0x1EC",
                "runtime_instance_field": "object +0x1F0",
                "runtime_helper": "0x00340D00",
                "runtime_callsite": "0x00450738",
            },
            {
                "subresource_id": "0x46",
                "descriptor_object_field": "object +0x1F4 + loop*4",
                "runtime_instance_field": "object +0x224 + loop*4",
                "runtime_helper": "0x0034897C",
                "runtime_callsite": "0x00450898",
                "materialization_callsite": "0x00450840",
                "descriptor_object_count": 12,
            },
            {
                "subresource_id": "0x4A/0x4B",
                "descriptor_object_field": "object +0x270",
                "runtime_instance_field": "object +0x274",
                "runtime_helper": "0x0034897C",
                "runtime_callsite": "0x00450AF4",
                "post_create_flag_update": "runtime instance +0x178 |= 0x10",
                "cleared_follow_up_field": "object +0x278",
            },
        ],
        "thunder_update_consumer": {
            "range": "0x004600B4..0x00460468",
            "selector_rule": "integer sample % 4 selects object +0x254 + selector*4",
            "resolved_payload_fields": [
                "object +0x254",
                "object +0x258",
                "object +0x25C",
                "object +0x260",
            ],
            "descriptor_slot": 0,
            "descriptor_field": "object +0x1F4 + loop*4",
            "descriptor_rebind_callsite": "0x00460100",
            "runtime_instance_field": "object +0x224 + loop*4",
            "runtime_update_callsite": "0x00460468",
        },
        "meaning": "z_kankyo does not stop at CTXB descriptor slot binding: native code materializes descriptor objects and creates runtime effect instances through shared helpers before update/draw consumers run",
        "remaining_gap": "decompile the draw/effect path after the verified thunder update and storm runtime creation, especially how thunder instance array object+0x224 and storm instance object+0x274 feed the later effect/light packet path",
    }

    lighting_provenance = lighting_provenance or {}
    zsi_source_links = [
        {
            "zsi_record_layout": "oot3d_pica_light_settings_record_0x1c",
            "zsi_record_field": "light0_direction",
            "zsi_record_offset": "0x03",
            "runtime_packet_field": "source_vector_xyz",
            "runtime_packet_offsets": ["0xC8", "0xCC", "0xD0"],
            "link_status": "native ZSI record layout decoded; caller population into the runtime packet remains to decompile",
        },
        {
            "zsi_record_layout": "oot3d_pica_light_settings_record_0x1c",
            "zsi_record_field": "light1_direction",
            "zsi_record_offset": "0x09",
            "runtime_packet_field": "source_vector_xyz",
            "runtime_packet_offsets": ["0xC8", "0xCC", "0xD0"],
            "link_status": "native ZSI record layout decoded; caller population into the runtime packet remains to decompile",
        },
        {
            "zsi_record_layout": "oot3d_pica_light_settings_record_0x1c",
            "zsi_record_field": "light0_color/light1_color",
            "zsi_record_offsets": ["0x06", "0x0C"],
            "runtime_packet_fields": ["slot_color_source_0x88", "slot_color_source_0x98"],
            "link_status": "code.bin loop passes packet color source pointers to 0x00466EA0; exact source packing caller remains to decompile",
        },
    ]
    return {
        "available": bool(checks),
        "origin_class": "code_bin_verified_light_vector_prep_loop_contract",
        "runtime_range": "0x0031317C..0x00313284",
        "code_bin": str(code_bin) if code_bin else "",
        "all_instruction_checks_verified": bool(checks) and all(row["verified"] for row in checks),
        "instruction_checks": checks,
        "producer_instruction_checks": producer_checks,
        "all_producer_instruction_checks_verified": bool(producer_checks)
        and all(row["verified"] for row in producer_checks),
        "light_list_helper_instruction_checks": light_list_helper_checks,
        "all_light_list_helper_instruction_checks_verified": bool(light_list_helper_checks)
        and all(row["verified"] for row in light_list_helper_checks),
        "literal_constants": literal_constants,
        "native_struct_contract": {
            "prep_function": "0x003130A4",
            "packet_name": "oot3d_runtime_vs_light_packet",
            "slot_name": "oot3d_runtime_vs_light_packet_slot_0x60",
            "slot_count": 3,
            "slot_stride_bytes": "0x60",
            "source_color_or_upload_payload_a_offset": "0x88",
            "source_color_or_upload_payload_b_offset": "0x98",
            "source_vector_offsets": { "x": "0xC8", "y": "0xCC", "z": "0xD0" },
            "enable_or_intensity_offset": "0xD4",
            "prepared_vector_offsets": { "x": "0xD8", "y": "0xDC", "z": "0xE0" },
            "prepared_enable_or_intensity_offset": "0xE4",
            "enabled_gate_literal": "0x3F800000",
            "disabled_fallback_vector": [0.0, 0.0, -1.0],
            "pre_vector_upload_helper": "0x00466EA0",
            "prepared_vector_upload_helper": "0x00466F00",
            "transform_source": "source_vector_xyz is transformed by the matrix prepared at stack +0x40 before prepared_vector_xyz upload",
            "semantic_ownership": "native code.bin runtime light-packet preparation fed by decoded OOT3D ZSI light-settings records, not emulator uniform replacement data",
        },
        "producer_candidates": [
            {
                "runtime_range": "0x003FB6DC..0x003FB8D8",
                "name": "oot3d_native_light_list_builder_candidate",
                "evidence": [
                    "initializes list counters at render context +0x50/+0x5C/+0x60",
                    "classifies native light command ids through 0x003136E4",
                    "appends light records through 0x00313698",
                    "appends disabled/default light-list entries through 0x00313650",
                    "finalizes the list through 0x00313864 before the render path calls 0x003130A4",
                ],
                "remaining_gap": "native descriptor pointer consumed by the 0x0040FC* helpers must be tied back to selected ZSI light records",
            },
            {
                "runtime_range": "0x00313650..0x00313694",
                "name": "oot3d_native_pica_light_list_default_entry_writer",
                "evidence": [
                    "uses the light-list default-entry counter at +0x10",
                    "stores a 16-byte default entry at +0xD4/+0xD8/+0xDC/+0xE0 plus count*0x10",
                    "increments the light-list default-entry counter",
                ],
                "remaining_gap": "this is not the direct 0x003130A4 source-vector writer; the ZSI-to-runtime-packet population path remains upstream",
            },
        ],
        "runtime_packet_buffer_contract": {
            "render_context_packet_buffer_slot": "ctx + 0x358",
            "allocation_range": "0x00473090..0x004730DC",
            "allocated_size": "0x1B8",
            "initializer": "0x00348B90",
            "native_source_binding_helper": "0x00348A64",
            "native_source_binding_block_size": "0x2600",
            "render_path_consumer_range": "0x003FBBC4..0x003FBBD8",
            "render_path_consumer_function": "0x003130A4",
            "copy_helper": "0x00371738",
            "copy_contract": "0x00348B90 bulk-copies 0x120 bytes from the native source/default record pointer into packet buffer +0x04 before render consumption",
            "remaining_gap": "identify the native source/default record selected as r7 in the 0x00473090 allocation path and tie it to selected ZSI light-setting records",
        },
        "all_runtime_packet_buffer_checks_verified": bool(runtime_packet_buffer_checks)
        and all(row["verified"] for row in runtime_packet_buffer_checks),
        "runtime_packet_buffer_instruction_checks": runtime_packet_buffer_checks,
        "native_default_light_packet_layout": {
            "initializer": "0x00347258",
            "slot_count": 3,
            "slot_stride_bytes": "0x60",
            "source_payload_offsets": ["0x88", "0x98"],
            "source_vector_offsets": ["0xC8", "0xCC", "0xD0"],
            "enable_or_intensity_offset": "0xD4",
            "meaning": "native code initializer confirms the same three-slot packet layout read by 0x003130A4; it is layout/default evidence, not proof of final ZSI population by itself",
        },
        "all_default_packet_layout_checks_verified": bool(default_packet_layout_checks)
        and all(row["verified"] for row in default_packet_layout_checks),
        "default_packet_layout_instruction_checks": default_packet_layout_checks,
        "runtime_source_provider_contract": {
            "provider_lookup_function": "0x002E11D0",
            "provider_table_runtime_address": "0x0055B490",
            "selected_provider_id": 13,
            "provider_result_stack_slot": "sp + 0x5D0",
            "consumer_helper": "0x00348A64",
            "ctx_0x358_consumer_instruction": "0x004730D4",
            "meaning": "native runtime provider id 13 supplies the resource pointer passed to 0x00348A64 for ctx+0x358; provider table population resolves slot 13 to a menu CTXB provider, not a scene/ZSI light-settings provider",
            "remaining_gap": "identify the actual source-vector population path for ctx+0x358; provider id 13 must not be promoted as the ZSI light source",
        },
        "all_runtime_source_provider_checks_verified": bool(source_provider_checks)
        and all(row["verified"] for row in source_provider_checks),
        "runtime_source_provider_instruction_checks": source_provider_checks,
        "z_movie_resource_context_contract": {
            "init_function": "0x00471F74",
            "debug_source_path": z_movie_resource_context_debug_path,
            "resource_context_table": "object +0x1A4",
            "entry_stride": "4-byte pointer",
            "entry_count": 51,
            "entry_zero_population_range": "0x00472148..0x00472180",
            "additional_entries_population_range": "0x00472198..0x004721CC",
            "entry_zero_path_table": hex_addr(z_movie_hint_movie_path_table),
            "entry_zero_path_selector": "runtime language index from global +0xF3C selects one UTF-16 rom:/menu path",
            "entry_zero_paths": z_movie_hint_movie_paths,
            "ctx_0x350_consumer_instruction": "0x004725E0",
            "ctx_0x350_binding_helper": "0x00348A64",
            "classification": "verified_native_ctxb_descriptor_source_but_not_link_house_lighting",
            "meaning": "the verified object +0x1A4 source used by the 0x004725E0 ctx+0x350 binding is a z_movie hint-movie CTXB resource context table; it must not be promoted as scene/link_info.zsi lighting evidence",
            "remaining_gap": "follow active scene/ZSI light-setting runtime population separately through scene environment code and the native ZSI records, not through this z_movie menu path",
        },
        "all_z_movie_resource_context_checks_verified": bool(z_movie_resource_context_checks)
        and all(row["verified"] for row in z_movie_resource_context_checks),
        "z_movie_resource_context_instruction_checks": z_movie_resource_context_checks,
        "surface_light_setting_selection_contract": {
            "surface_type_light_setting_getter": "0x002C1E10",
            "surface_type_field_reader": "0x00322088",
            "field_id": 1,
            "field_extract": "(raw << 21) >> 27, producing a five-bit native light-setting index",
            "player_collision_callsite": "0x0032F140..0x0032F14C",
            "light_setting_change_helper": "0x0032B13C",
            "play_environment_region": "play +0x3000",
            "current_light_setting_offset": "play +0x3235",
            "previous_light_setting_offset": "play +0x3236",
            "transition_blend_weight_offset": "play +0x3258",
            "normalization_rule": "requested setting >= 31 is normalized to 0 before storage",
            "native_light_settings_source": lighting_provenance.get("scene_zsi", ""),
            "active_setup_index": lighting_provenance.get("active_setup_index"),
            "active_record_count": lighting_provenance.get("active_record_count", 0),
            "source_layout": "oot3d_pica_light_settings_record_0x1c",
            "meaning": "active room light-setting selection is driven by native collision SurfaceType data and scene ZSI light-setting records, not by z_movie CTXB resources or emulator trace constants",
            "remaining_gap": "identify the OOT3D environment update/copy-blend function that consumes play +0x3235 and the scene light-settings list, then tie its output to the 0x003130A4 PICA light packet",
        },
        "all_surface_light_setting_selection_checks_verified": bool(surface_light_setting_selection_checks)
        and all(row["verified"] for row in surface_light_setting_selection_checks),
        "surface_light_setting_selection_instruction_checks": surface_light_setting_selection_checks,
        "environment_light_setting_transition_reset_contract": {
            "helper": "0x004B8FC0",
            "single_verified_caller": "0x002D0AFC",
            "caller_argument_source": "context +0xD4, interpreted by the helper as PlayState because it immediately selects play +0x3000",
            "play_environment_region": "play +0x3000",
            "mode_or_state_offset": "play +0x31B0",
            "inactive_fallback_source": "byte at 0x00531EB4 + 1",
            "inactive_fallback_destinations": ["play +0x31B1", "play +0x31B2"],
            "transition_state_offset": "play +0x3234",
            "current_light_setting_offset": "play +0x3235",
            "previous_light_setting_offset": "play +0x3236",
            "transition_sentinel_offset": "play +0x3237",
            "transition_blend_weight_offset": "play +0x3258",
            "transition_blend_weight_literal": "1.0f / 0x3F800000",
            "active_path_behavior": "clear transition state, set 0xFF sentinel, set blend weight to 1.0f, then swap current and previous light-setting bytes",
            "meaning": "native code confirms a second environment-state transition path around the same play +0x3235/+0x3236/+0x3258 fields; it is reset/swap evidence, not the scene ZSI record consumer by itself",
            "remaining_gap": "identify the update/copy-blend function that reads the selected light-setting index and copies or blends scene/link_info.zsi records into the runtime PICA light packet",
        },
        "all_environment_light_setting_transition_reset_checks_verified": bool(
            environment_light_setting_transition_reset_checks
        )
        and all(row["verified"] for row in environment_light_setting_transition_reset_checks),
        "environment_light_setting_transition_reset_instruction_checks": (
            environment_light_setting_transition_reset_checks
        ),
        "environment_light_setting_request_helper_contract": {
            "helper": "0x00316D74",
            "verified_callers": [
                "0x002D09D8",
                "0x003B68AC",
                "0x003B6BBC",
                "0x003B7078",
                "0x003B70E8",
                "0x003B7220",
            ],
            "caller_classification": [
                {
                    "callsite": "0x002D09D8",
                    "symbol": "Camera_CheckWater",
                    "classification": "camera water route",
                    "requested_setting_source": "stack +0x1C",
                    "play_source": "camera/context +0xD4",
                    "meaning": "camera-side request path only; not the global scene ZSI record copy/blend consumer",
                },
                {
                    "callsite": "0x003B68AC",
                    "enclosing_symbol_neighborhood": "after EnBubble_Wait at 0x003B4FCC, before EnKz_Mweep at 0x003B8E4C",
                    "classification": "unsymbolized actor/gameplay camera-state block",
                    "requested_setting": 0,
                    "play_source": "r7",
                    "meaning": "actor/gameplay request path only; exact actor range still needs decompilation before engine promotion",
                },
                {
                    "callsite": "0x003B6BBC",
                    "enclosing_symbol_neighborhood": "after EnBubble_Wait at 0x003B4FCC, before EnKz_Mweep at 0x003B8E4C",
                    "classification": "unsymbolized actor/gameplay camera-state block",
                    "requested_setting": 0,
                    "play_source": "r7",
                    "meaning": "actor/gameplay request path only; exact actor range still needs decompilation before engine promotion",
                },
                {
                    "callsite": "0x003B7078",
                    "enclosing_symbol_neighborhood": "after EnBubble_Wait at 0x003B4FCC, before EnKz_Mweep at 0x003B8E4C",
                    "classification": "unsymbolized actor/gameplay camera-state block",
                    "requested_setting": 0,
                    "play_source": "r7",
                    "meaning": "actor/gameplay request path only; exact actor range still needs decompilation before engine promotion",
                },
                {
                    "callsite": "0x003B70E8",
                    "enclosing_symbol_neighborhood": "after EnBubble_Wait at 0x003B4FCC, before EnKz_Mweep at 0x003B8E4C",
                    "classification": "unsymbolized actor/gameplay camera-state block",
                    "requested_setting": 1,
                    "play_source": "r7",
                    "meaning": "actor/gameplay request path only; exact actor range still needs decompilation before engine promotion",
                },
                {
                    "callsite": "0x003B7220",
                    "enclosing_symbol_neighborhood": "after EnBubble_Wait at 0x003B4FCC, before EnKz_Mweep at 0x003B8E4C",
                    "classification": "unsymbolized actor/gameplay camera-state block",
                    "requested_setting": 0,
                    "play_source": "r7",
                    "meaning": "actor/gameplay request path only; exact actor range still needs decompilation before engine promotion",
                },
            ],
            "play_environment_region": "play +0x3000",
            "normalization_rule": "requested setting == 31 is normalized to 0 before target/fallback handling",
            "mode_or_state_offset": "play +0x31B0",
            "inactive_fallback_source": "byte at 0x00531EB4 + 1 receives the previous play +0x31B2 value",
            "inactive_fallback_offsets": ["play +0x31B1", "play +0x31B2"],
            "active_transition_state_offset": "play +0x3234",
            "active_target_light_setting_offset": "play +0x3237",
            "active_path_behavior": "when environment state is active, clear transition state and store the requested target light-setting byte at play +0x3237",
            "inactive_path_behavior": "when environment state is inactive, mirror the previous fallback byte to global fallback storage and update play +0x31B1/+0x31B2 if the requested setting changed",
            "meaning": "native code separates a requested/target light-setting path from the current/previous selection path; this constrains the environment state machine but is not the ZSI record copy/blend consumer by itself",
            "remaining_gap": "decompile the verified callers and the update/copy-blend function that consumes the resulting current/previous/target state to populate light packet source fields",
        },
        "all_environment_light_setting_request_helper_checks_verified": bool(
            environment_light_setting_request_helper_checks
        )
        and all(row["verified"] for row in environment_light_setting_request_helper_checks),
        "environment_light_setting_request_helper_instruction_checks": (
            environment_light_setting_request_helper_checks
        ),
        "native_kankyo_resource_bridge_contract": native_kankyo_resource_bridge_contract,
        "all_native_kankyo_resource_bridge_checks_verified": bool(
            native_kankyo_resource_bridge_checks
        )
        and all(row["verified"] for row in native_kankyo_resource_bridge_checks),
        "native_kankyo_resource_bridge_instruction_checks": native_kankyo_resource_bridge_checks,
        "native_resource_resolver_contract": native_resource_resolver_contract,
        "all_native_resource_resolver_checks_verified": bool(
            native_resource_resolver_checks
        )
        and all(row["verified"] for row in native_resource_resolver_checks),
        "native_resource_resolver_instruction_checks": native_resource_resolver_checks,
        "native_resource_context_payload_contract": native_resource_context_payload_contract,
        "all_native_resource_context_payload_checks_verified": bool(
            native_resource_context_payload_checks
        )
        and all(row["verified"] for row in native_resource_context_payload_checks),
        "native_resource_context_payload_instruction_checks": (
            native_resource_context_payload_checks
        ),
        "native_object_bank_population_contract": native_object_bank_population_contract,
        "native_object_bank_inventory": native_object_bank_inventory,
        "all_native_object_bank_population_checks_verified": bool(
            native_object_bank_population_checks
        )
        and all(row["verified"] for row in native_object_bank_population_checks),
        "native_object_bank_population_instruction_checks": (
            native_object_bank_population_checks
        ),
        "native_ctxb_descriptor_binding_contract": native_ctxb_descriptor_binding_contract,
        "all_native_ctxb_descriptor_binding_checks_verified": bool(
            native_ctxb_descriptor_binding_checks
        )
        and all(row["verified"] for row in native_ctxb_descriptor_binding_checks),
        "native_ctxb_descriptor_binding_instruction_checks": (
            native_ctxb_descriptor_binding_checks
        ),
        "native_kankyo_descriptor_instance_contract": native_kankyo_descriptor_instance_contract,
        "all_native_kankyo_descriptor_instance_checks_verified": bool(
            native_kankyo_descriptor_instance_checks
        )
        and all(row["verified"] for row in native_kankyo_descriptor_instance_checks),
        "native_kankyo_descriptor_instance_instruction_checks": (
            native_kankyo_descriptor_instance_checks
        ),
        "native_provider_open_contract": native_provider_open_contract,
        "all_native_provider_open_checks_verified": bool(native_provider_open_checks)
        and all(row["verified"] for row in native_provider_open_checks),
        "native_provider_open_instruction_checks": native_provider_open_checks,
        "native_zar_resolver_family_contract": native_zar_resolver_family_contract,
        "all_native_zar_resolver_family_checks_verified": bool(
            native_zar_resolver_family_checks
        )
        and all(row["verified"] for row in native_zar_resolver_family_checks),
        "native_zar_resolver_family_instruction_checks": native_zar_resolver_family_checks,
        "native_zar_setup_contract": native_zar_setup_contract,
        "all_native_zar_setup_checks_verified": bool(native_zar_setup_checks)
        and all(row["verified"] for row in native_zar_setup_checks),
        "native_zar_setup_instruction_checks": native_zar_setup_checks,
        "provider_table_population_contract": {
            "population_function": "0x004811B4",
            "provider_table_runtime_address": "0x0055B490",
            "slot_count": 16,
            "path_format_string_address": "0x004D4050",
            "path_format": provider_path_format,
            "language_prefix_table_address": hex_addr(provider_table_address),
            "slot_suffix_table_address": hex_addr(provider_slot_suffix_table_address),
            "language_prefixes": provider_language_prefixes,
            "slot_suffixes": provider_slot_suffixes,
            "selected_provider_id": 13,
            "selected_provider_suffix": selected_provider_suffix,
            "selected_provider_path_template": f"rom:/menu/<language>/{selected_provider_suffix}",
            "meaning": "provider table slot 13 is a native menu CTXB provider; this proves the provider-table structure and disproves using this slot as direct room light-source evidence",
            "remaining_gap": "find whether the source-vector fields consumed by 0x003130A4 are populated through a different scene/ZSI provider path or by a later copy into the ctx+0x358 packet buffer",
        },
        "all_provider_table_population_checks_verified": bool(provider_table_population_checks)
        and all(row["verified"] for row in provider_table_population_checks),
        "provider_table_population_instruction_checks": provider_table_population_checks,
        "runtime_default_record_contract": {
            "default_record_stack_base": "sp + 0x18",
            "selected_register": "r7",
            "initializer_consumer": "0x00348B90",
            "ctx_0x358_pass_instruction": "0x004730B4",
            "copy_helper": "0x00371738",
            "literal_pointers": default_record_literal_pointers,
            "patched_stack_pointers": [
                {"record_offset": "0x00", "source": "sp+0x2E0"},
                {"record_offset": "0x04", "source": "sp+0x160"},
                {"record_offset": "0x08", "source": "sp+0x1E0"},
                {"record_offset": "0x10", "source": "sp+0x130"},
            ],
            "meaning": "ctx+0x358 receives a code-origin default record assembled from static code.bin tables before the runtime provider id 13 source is bound through 0x00348A64",
            "remaining_gap": "decode the static default record fields and the runtime provider id 13 payload fields into named engine structures",
        },
        "all_runtime_default_record_checks_verified": bool(default_record_source_checks)
        and all(row["verified"] for row in default_record_source_checks),
        "runtime_default_record_instruction_checks": default_record_source_checks,
        "runtime_light_descriptor_binding_contract": {
            "descriptor_context_slot": "ctx + 0x350",
            "render_init_range": "0x004725C8..0x004725EC",
            "render_init_initializer": "0x00348B90",
            "render_init_source_binding_helper": "0x00348A64",
            "verified_init_scope": "z_movie.cpp object/resource context",
            "render_init_source_pointer": "z_movie object +0x1A4 entry zero",
            "verified_draw_setup_callsite_scope": "OBJECT_GANON2 path only",
            "verified_draw_setup_selector": "0x0036A924",
            "verified_draw_setup_selector_id": 7,
            "verified_draw_setup_store_instruction": "0x003F81EC",
            "light_list_builder_range": "0x003FB6DC..0x003FB8D8",
            "light_list_builder_descriptor_load": "0x003FB6E8",
            "descriptor_record_pointer_offset": "object +0x00",
            "descriptor_flag_offset": "record +0x1C",
            "meaning": "native light-list descriptor consumption is fed by a runtime ctx+0x350 object initialized from native source data; the currently verified selector callsite is OBJECT_GANON2-specific and must not be promoted as the active demo binding",
            "remaining_gap": "identify the active Link house/current room selector source path and decompile the render context +0x1A4 source to tie descriptor id 7 back to native CMB/ZSI material or scene records",
        },
        "all_runtime_light_descriptor_binding_checks_verified": bool(runtime_descriptor_binding_checks)
        and all(row["verified"] for row in runtime_descriptor_binding_checks),
        "runtime_light_descriptor_binding_instruction_checks": runtime_descriptor_binding_checks,
        "runtime_light_descriptor_selector_contract": {
            "selector_function": "0x0036A924",
            "source_id_resolver": "0x00363C10",
            "source_id_resolver_input": "resource table base at resource context +0x3A58 and requested source id",
            "source_id_resolver_entry_count_offset": "table +0x00",
            "source_id_resolver_entry_stride": "0x80",
            "source_id_resolver_entry_id_offset": "entry +0x04 signed, absolute value compared",
            "source_id_resolver_not_found": "-1, masked to 0xFF by caller and rejected by index >= 19",
            "max_resolved_resource_index": 18,
            "resource_entry_pointer_test_offset": "resource context +0x3A64 + index*0x80",
            "resource_entry_payload_base": "resource context +0x3A6C + index*0x80",
            "selected_descriptor_id_from_draw_setup": 7,
            "subselector_function": "0x00358EF8",
            "backend_selector_global": "0x005BE5B8",
            "backend_selector_object_offset": "global +0x17C",
            "caller_context_field_copied_to_selector": "caller context +0x178 -> selector object +0x08",
            "meaning": "descriptor id 7 is selected from a native resource-table entry resolved by source id, then passed through the backend selector; the selector is data-driven by resource table entries rather than a hardcoded light descriptor",
            "remaining_gap": "decode the verified OBJECT_GANON2 source id 0x0153 descriptor payload, then identify the separate active Link house/current room source path",
        },
        "all_runtime_light_descriptor_selector_checks_verified": bool(runtime_descriptor_selector_checks)
        and all(row["verified"] for row in runtime_descriptor_selector_checks),
        "runtime_light_descriptor_selector_instruction_checks": runtime_descriptor_selector_checks,
        "runtime_light_descriptor_callsite_contract": {
            "callsite_range": "0x003F8170..0x003F81EC",
            "debug_source_path": ganon2_descriptor_callsite_debug_path,
            "source_id_literal_address": "0x003F80B4",
            "source_id": "0x0153",
            "source_id_semantic_annotation": "OBJECT_GANON2",
            "native_archive_hint": "actor/zelda_ganon2.zar",
            "descriptor_ids_selected": [5, 6, 7],
            "descriptor_id_to_context_slot": {
                "5": "ctx + 0x344",
                "6": "ctx + 0x348",
                "7": "ctx + 0x350",
            },
            "meaning": "this verified callsite ties descriptor id 7 at ctx+0x350 to source id 0x0153/OBJECT_GANON2 for the Ganon2 path; it is not evidence for the active Link house room binding",
            "remaining_gap": "decode the resolved OBJECT_GANON2 resource-table payload into concrete native CMB/ZSI entries, and separately identify the active Link house ctx+0x350 source path",
        },
        "all_runtime_light_descriptor_callsite_checks_verified": bool(runtime_descriptor_callsite_checks)
        and all(row["verified"] for row in runtime_descriptor_callsite_checks),
        "runtime_light_descriptor_callsite_instruction_checks": runtime_descriptor_callsite_checks,
        "native_pica_light_list_contract": {
            "descriptor_pointer_source": "native descriptor pointer loaded from the render/material light context",
            "descriptor_record_count_offset": "0x0C",
            "descriptor_flag_offset": "0x1C",
            "record_count_normalization": "count == 0 selects 4 records",
            "append_record_function": "0x00313698",
            "default_entry_function": "0x00313650",
            "dynamic_default_packer_function": "0x0040950C",
            "finalize_function": "0x00313864",
            "emit_function": "0x004090CC",
            "emitted_registers": ["0x200", "0x2BB", "0x232"],
            "payload_word_count_function": "0x004094B4",
            "offset_calculators": [
                {
                    "function": "0x0040FD08",
                    "formula": "normalized_count * 12",
                    "used_for": "first 12-byte-per-record light-list payload block",
                },
                {
                    "function": "0x0040FD28",
                    "formula": "normalized_count * 12 + (flag_0x80 ? 0 : normalized_count * 12)",
                    "used_for": "payload block after optional flag 0x80 block",
                },
                {
                    "function": "0x0040FCA0",
                    "formula": "normalized_count * 12 + (flag_0x80 ? 0 : normalized_count * 12) + (flag_0x10 ? 0 : normalized_count * 8)",
                    "used_for": "payload block after optional flag 0x80 and flag 0x10 blocks",
                },
                {
                    "function": "0x0040FD70",
                    "formula": "normalized_count * 12 + (flag_0x80 ? 0 : normalized_count * 12) + (flag_0x10 ? 0 : normalized_count * 8) + (flag_0x08 ? 0 : normalized_count * 16)",
                    "used_for": "payload block after optional flag 0x80, 0x10, and 0x08 blocks",
                },
            ],
            "dynamic_packer": {
                "function": "0x0040950C",
                "source_payload": "four source floats at the r2 payload pointer",
                "slot_field": "+0xD4",
                "packed_output_fields": ["+0xD8", "+0xDC", "+0xE0"],
                "meaning": "native light-list packed PICA payload writer; this is not the 0x003130A4 runtime source-vector reader",
            },
            "remaining_gap": "descriptor pointer source must still be tied to the selected ZSI light-setting record stream and to the ctx+0x358 source/default record copied before 0x003130A4",
        },
        "native_asset_source_links": zsi_source_links,
        "active_zsi_light_record_count": lighting_provenance.get("active_record_count", 0),
        "zsi_light_source_file": lighting_provenance.get("scene_zsi", ""),
    }


def framebuffer_flush_register_emit_structure(code_bin: Path | None) -> dict[str, Any]:
    literal_table_address = 0x004E2E90
    generic_writer_address = 0x00307BD8
    checks = code_instruction_checks(
        code_bin,
        [
            (0x00438584, 0xE59F0070, "load literal table pointer from 0x004385FC"),
            (0x0043858C, 0xE890000E, "load three framebuffer register payload words"),
            (0x00438594, 0xE885000E, "copy the three payload words into the stack packet"),
            (0x0043859C, 0xE3A0200F, "set scalar writer mask to 0xF for register 0x111"),
            (0x004385A4, 0xE3A02001, "set scalar writer count to 1 for register 0x111"),
            (0x004385AC, 0xE2821E11, "build register id 0x111 as 1 + 0x110"),
            (0x004385B0, 0xEBFB3D88, "call generic scalar writer 0x00307BD8 for register 0x111"),
            (0x004385B4, 0xE2853004, "select literal table word 1 for register 0x110"),
            (0x004385C8, 0xE3A01E11, "load register id 0x110"),
            (0x004385D0, 0xEBFB3D80, "call generic scalar writer 0x00307BD8 for register 0x110"),
            (0x004385D4, 0xE2853008, "select literal table word 2 for register 0x010"),
            (0x004385E8, 0xE3A01010, "load register id 0x010"),
            (0x004385F0, 0xEBFB3D78, "call generic scalar writer 0x00307BD8 for register 0x010"),
            (0x004385FC, literal_table_address, "literal table runtime pointer"),
        ],
    )
    code_data = code_bin.read_bytes() if code_bin is not None and code_bin.is_file() else b""
    literal_words: list[dict[str, Any]] = []
    for index in range(3):
        runtime_address = literal_table_address + index * 4
        word = read_code_word(code_data, runtime_address) if code_data else None
        literal_words.append(
            {
                "index": index,
                "runtime_address": hex_addr(runtime_address),
                "code_bin_offset": hex_addr(runtime_address - CODE_BIN_RUNTIME_BASE),
                "value": hex32(word) if word is not None else "",
            }
        )
    register_sequence = [
        {
            "order": 0,
            "register": "0x111",
            "register_source": "1 + 0x110",
            "payload_literal_index": 0,
            "payload_word": literal_words[0]["value"],
            "writer": hex_addr(generic_writer_address),
            "word_count": 1,
            "mask": "0xF",
            "semantic_name": "framebuffer_flush_pair_control_plus_one",
        },
        {
            "order": 1,
            "register": "0x110",
            "register_source": "literal register id 0x110",
            "payload_literal_index": 1,
            "payload_word": literal_words[1]["value"],
            "writer": hex_addr(generic_writer_address),
            "word_count": 1,
            "mask": "0xF",
            "semantic_name": "framebuffer_flush_pair_control",
        },
        {
            "order": 2,
            "register": "0x010",
            "register_source": "literal register id 0x010",
            "payload_literal_index": 2,
            "payload_word": literal_words[2]["value"],
            "writer": hex_addr(generic_writer_address),
            "word_count": 1,
            "mask": "0xF",
            "semantic_name": "framebuffer_flush_final_control",
        },
    ]
    return {
        "available": bool(checks),
        "origin_class": "code_bin_verified_framebuffer_flush_register_emit_contract",
        "runtime_range": "0x00438550..0x004385F0",
        "code_bin": str(code_bin) if code_bin else "",
        "all_instruction_checks_verified": bool(checks) and all(row["verified"] for row in checks),
        "instruction_checks": checks,
        "literal_table_runtime_address": hex_addr(literal_table_address),
        "literal_table_code_bin_offset": hex_addr(literal_table_address - CODE_BIN_RUNTIME_BASE),
        "literal_table_words": literal_words,
        "register_sequence": register_sequence,
        "native_struct_contract": {
            "emitter_name": "oot3d_framebuffer_flush_register_emit_00438550",
            "literal_table_name": "oot3d_framebuffer_flush_register_defaults_004E2E90",
            "literal_table_word_count": 3,
            "generic_scalar_writer": hex_addr(generic_writer_address),
            "backend_state_class": "code_origin_backend_framebuffer_register_state",
            "register_emit_order": ["0x111", "0x110", "0x010"],
            "semantic_ownership": "0x00438550 emits backend/framebuffer flush state; ownership is code.bin, not asset/material state",
        },
    }


def primitive_owner_scope_hint(owner: dict[str, Any]) -> str:
    for ref in owner.get("source_refs", []):
        if not isinstance(ref, dict):
            continue
        source_kind = ref.get("source_kind")
        if source_kind == "link_actor_model_cmb":
            return "link_child"
        if source_kind == "room_zsi_embedded_cmb":
            return "room"
        if source_kind == "room_actor":
            return "room_actor"
        if source_kind == "room_object":
            return "room_object"
    return ""


def primitive_owner_summary(owner: dict[str, Any]) -> dict[str, Any]:
    if not owner:
        return {}
    return {
        "archive_path": owner.get("archive_path", ""),
        "cmb_entry_name": owner.get("cmb_entry_name", ""),
        "mesh_index": owner.get("mesh_index"),
        "shape_index": owner.get("shape_index"),
        "material_index": owner.get("material_index"),
        "primitive_index": owner.get("primitive_index"),
        "vertex_count": owner.get("vertex_count"),
        "texture_index": owner.get("texture_index"),
        "texture_name": owner.get("texture_name"),
        "format_name": owner.get("format_name", ""),
        "scope_hint": primitive_owner_scope_hint(owner),
    }


def collect_material_dispatch_mismatch_analysis(
    binding_provenance: dict[str, Any],
    mismatch_search: dict[str, Any],
    dispatch: dict[str, Any],
) -> dict[str, Any]:
    searches_by_draw = {
        row.get("trace_draw_index"): row
        for row in mismatch_search.get("searches", [])
        if isinstance(row, dict)
    }
    rows: list[dict[str, Any]] = []
    for row in binding_provenance.get("bindings", []):
        if row.get("texture_status") != "format_mismatch_needs_asset_or_stage_origin":
            continue
        search = searches_by_draw.get(row.get("trace_draw_index"), {})
        code_lane_index = row.get("material_index")
        static_conflict = (
            row.get("trace_mapped_native_format", {}).get("texture_format")
            != row.get("native_texture_format", {}).get("texture_format")
            or row.get("trace_mapped_native_format", {}).get("data_type")
            != row.get("native_texture_format", {}).get("data_type")
            or row.get("trace_texture_width") != row.get("native_texture_width")
            or row.get("trace_texture_height") != row.get("native_texture_height")
        )
        primitive_owner_matches = (
            search.get("same_archive_primitive_owner_matches", [])
            or search.get("resolved_primitive_owner_matches", [])
            or []
        )
        exact_primitive_owner = (
            primitive_owner_matches[0]
            if primitive_owner_matches and isinstance(primitive_owner_matches[0], dict)
            else {}
        )
        exact_owner_scope_hint = primitive_owner_scope_hint(exact_primitive_owner)
        exact_owner_same_as_resolved_candidate = bool(
            exact_primitive_owner
            and same_path(
                str(exact_primitive_owner.get("archive_path", "")),
                str(row.get("source_file", "")),
            )
            and exact_primitive_owner.get("texture_index") == row.get("texture_index")
            and exact_primitive_owner.get("material_index") == row.get("material_index")
        )
        if exact_primitive_owner and static_conflict and not exact_owner_same_as_resolved_candidate:
            implication = "current_asset_binding_is_false_owner_candidate"
            if exact_owner_scope_hint == "link_child":
                next_action = "bind draw to native Link CMB draw/visibility path before changing room material state"
            elif exact_owner_scope_hint in {"room_actor", "room_object"}:
                next_action = "bind draw to native actor/object effect draw path in code.bin before promoting owner"
            else:
                next_action = "bind draw to exact native primitive owner before promoting material or texture state"
        elif search.get("draw_owner_status") == "same_source_texture_found_but_no_same_source_vertex_owner_match":
            implication = "same_source_texture_exists_but_code_contract_keeps_current_mesh_material_lane"
            next_action = "decompile runtime lane population or material animation for this CMB material index"
        elif search.get("draw_owner_status") == "exact_native_texture_and_vertex_owner_found_in_other_resolved_archive":
            implication = "current_visible_asset_binding_is_false_owner_candidate"
            next_action = "bind draw to native shadow/effect/object draw path in code.bin before promoting owner"
        else:
            implication = "native_owner_unresolved"
            next_action = "continue asset/code ownership search"
        rows.append(
            {
                "trace_draw_index": row.get("trace_draw_index"),
                "resolved_model_scope": row.get("model_scope"),
                "resolved_model_name": row.get("model_name"),
                "source_file": row.get("source_file"),
                "source_entry": row.get("source_entry"),
                "cmb_mesh_material_index": row.get("material_index"),
                "code_runtime_material_lane_index": code_lane_index,
                "code_lane_source": dispatch.get("native_struct_contract", {}).get(
                    "runtime_material_lane_index_source", ""
                ),
                "code_lane_stride_bytes": dispatch.get("native_struct_contract", {}).get(
                    "runtime_material_lane_stride_bytes", ""
                ),
                "static_cmb_texture": {
                    "texture_index": row.get("texture_index"),
                    "texture_name": row.get("texture_name"),
                    "format": row.get("native_texture_format"),
                    "width": row.get("native_texture_width"),
                    "height": row.get("native_texture_height"),
                },
                "trace_texture_query": search.get("trace_texture_query", {}),
                "static_cmb_texture_conflicts_with_trace": static_conflict,
                "same_source_texture_match_count": search.get("same_archive_match_count", 0),
                "same_source_primitive_owner_match_count": search.get(
                    "same_archive_primitive_owner_match_count", 0
                ),
                "resolved_primitive_owner_match_count": search.get(
                    "resolved_primitive_owner_match_count", 0
                ),
                "exact_primitive_owner_candidate": primitive_owner_summary(exact_primitive_owner),
                "exact_primitive_owner_scope_hint": exact_owner_scope_hint,
                "exact_primitive_owner_same_as_resolved_candidate": exact_owner_same_as_resolved_candidate,
                "draw_owner_status": search.get("draw_owner_status", ""),
                "implication": implication,
                "next_action": next_action,
                "origin_class": "code_bin_material_dispatch_contract_applied_to_mismatch",
            }
        )
    return {
        "available": bool(rows),
        "dispatch_contract_verified": bool(dispatch.get("all_instruction_checks_verified")),
        "analysis_count": len(rows),
        "rows": rows,
    }


def code_structure_evidence(
    code_bin: Path | None,
    lighting_provenance: dict[str, Any] | None = None,
    romfs_root: Path | None = None,
) -> list[dict[str, Any]]:
    if code_bin is None or not code_bin.is_file():
        return []
    light_vector_prep = light_vector_prep_loop_structure(
        code_bin, lighting_provenance, romfs_root
    )
    material_dispatch = material_draw_dispatch_structure(code_bin)
    framebuffer_flush = framebuffer_flush_register_emit_structure(code_bin)
    return [
        {
            "name": "native_light_vector_prep_loop",
            "origin_class": "code_bin_disassembly_structure",
            "code_bin": str(code_bin),
            "runtime_range": "0x0031317C..0x00313284",
            "evidence": [
                "loop counter r6 compares against 3, matching three native light slots",
                "slot pointer is r5 + (slot + slot*2) << 5, giving a 0x60-byte light packet stride",
                "slot +0xD4 gates enabled lights against 1.0f",
                "slot +0xC8/+0xCC/+0xD0 is transformed and normalized before upload",
                "slot +0xD8/+0xDC/+0xE0 and +0xE4 hold the prepared vector/intensity packet",
                "calls 0x466EA0 before vector normalization and 0x466F00 after packet preparation",
            ],
            "structured_contract": light_vector_prep.get("native_struct_contract", {}),
            "instruction_checks": light_vector_prep.get("instruction_checks", []),
            "producer_instruction_checks": light_vector_prep.get(
                "producer_instruction_checks", []
            ),
            "all_producer_instruction_checks_verified": light_vector_prep.get(
                "all_producer_instruction_checks_verified", False
            ),
            "light_list_helper_instruction_checks": light_vector_prep.get(
                "light_list_helper_instruction_checks", []
            ),
            "all_light_list_helper_instruction_checks_verified": light_vector_prep.get(
                "all_light_list_helper_instruction_checks_verified", False
            ),
            "runtime_packet_buffer_contract": light_vector_prep.get(
                "runtime_packet_buffer_contract", {}
            ),
            "runtime_packet_buffer_instruction_checks": light_vector_prep.get(
                "runtime_packet_buffer_instruction_checks", []
            ),
            "all_runtime_packet_buffer_checks_verified": light_vector_prep.get(
                "all_runtime_packet_buffer_checks_verified", False
            ),
            "native_default_light_packet_layout": light_vector_prep.get(
                "native_default_light_packet_layout", {}
            ),
            "default_packet_layout_instruction_checks": light_vector_prep.get(
                "default_packet_layout_instruction_checks", []
            ),
            "all_default_packet_layout_checks_verified": light_vector_prep.get(
                "all_default_packet_layout_checks_verified", False
            ),
            "runtime_source_provider_contract": light_vector_prep.get(
                "runtime_source_provider_contract", {}
            ),
            "runtime_source_provider_instruction_checks": light_vector_prep.get(
                "runtime_source_provider_instruction_checks", []
            ),
            "all_runtime_source_provider_checks_verified": light_vector_prep.get(
                "all_runtime_source_provider_checks_verified", False
            ),
            "z_movie_resource_context_contract": light_vector_prep.get(
                "z_movie_resource_context_contract", {}
            ),
            "z_movie_resource_context_instruction_checks": light_vector_prep.get(
                "z_movie_resource_context_instruction_checks", []
            ),
            "all_z_movie_resource_context_checks_verified": light_vector_prep.get(
                "all_z_movie_resource_context_checks_verified", False
            ),
            "surface_light_setting_selection_contract": light_vector_prep.get(
                "surface_light_setting_selection_contract", {}
            ),
            "surface_light_setting_selection_instruction_checks": light_vector_prep.get(
                "surface_light_setting_selection_instruction_checks", []
            ),
            "all_surface_light_setting_selection_checks_verified": light_vector_prep.get(
                "all_surface_light_setting_selection_checks_verified", False
            ),
            "environment_light_setting_transition_reset_contract": light_vector_prep.get(
                "environment_light_setting_transition_reset_contract", {}
            ),
            "environment_light_setting_transition_reset_instruction_checks": (
                light_vector_prep.get(
                    "environment_light_setting_transition_reset_instruction_checks", []
                )
            ),
            "all_environment_light_setting_transition_reset_checks_verified": (
                light_vector_prep.get(
                    "all_environment_light_setting_transition_reset_checks_verified", False
                )
            ),
            "environment_light_setting_request_helper_contract": light_vector_prep.get(
                "environment_light_setting_request_helper_contract", {}
            ),
            "environment_light_setting_request_helper_instruction_checks": (
                light_vector_prep.get(
                    "environment_light_setting_request_helper_instruction_checks", []
                )
            ),
            "all_environment_light_setting_request_helper_checks_verified": (
                light_vector_prep.get(
                    "all_environment_light_setting_request_helper_checks_verified", False
                )
            ),
            "native_kankyo_resource_bridge_contract": light_vector_prep.get(
                "native_kankyo_resource_bridge_contract", {}
            ),
            "native_kankyo_resource_bridge_instruction_checks": light_vector_prep.get(
                "native_kankyo_resource_bridge_instruction_checks", []
            ),
            "all_native_kankyo_resource_bridge_checks_verified": light_vector_prep.get(
                "all_native_kankyo_resource_bridge_checks_verified", False
            ),
            "native_resource_resolver_contract": light_vector_prep.get(
                "native_resource_resolver_contract", {}
            ),
            "native_resource_resolver_instruction_checks": light_vector_prep.get(
                "native_resource_resolver_instruction_checks", []
            ),
            "all_native_resource_resolver_checks_verified": light_vector_prep.get(
                "all_native_resource_resolver_checks_verified", False
            ),
            "native_provider_open_contract": light_vector_prep.get(
                "native_provider_open_contract", {}
            ),
            "native_provider_open_instruction_checks": light_vector_prep.get(
                "native_provider_open_instruction_checks", []
            ),
            "all_native_provider_open_checks_verified": light_vector_prep.get(
                "all_native_provider_open_checks_verified", False
            ),
            "native_zar_resolver_family_contract": light_vector_prep.get(
                "native_zar_resolver_family_contract", {}
            ),
            "native_zar_resolver_family_instruction_checks": light_vector_prep.get(
                "native_zar_resolver_family_instruction_checks", []
            ),
            "all_native_zar_resolver_family_checks_verified": light_vector_prep.get(
                "all_native_zar_resolver_family_checks_verified", False
            ),
            "native_zar_setup_contract": light_vector_prep.get(
                "native_zar_setup_contract", {}
            ),
            "native_zar_setup_instruction_checks": light_vector_prep.get(
                "native_zar_setup_instruction_checks", []
            ),
            "all_native_zar_setup_checks_verified": light_vector_prep.get(
                "all_native_zar_setup_checks_verified", False
            ),
            "provider_table_population_contract": light_vector_prep.get(
                "provider_table_population_contract", {}
            ),
            "provider_table_population_instruction_checks": light_vector_prep.get(
                "provider_table_population_instruction_checks", []
            ),
            "all_provider_table_population_checks_verified": light_vector_prep.get(
                "all_provider_table_population_checks_verified", False
            ),
            "runtime_default_record_contract": light_vector_prep.get(
                "runtime_default_record_contract", {}
            ),
            "runtime_default_record_instruction_checks": light_vector_prep.get(
                "runtime_default_record_instruction_checks", []
            ),
            "all_runtime_default_record_checks_verified": light_vector_prep.get(
                "all_runtime_default_record_checks_verified", False
            ),
            "runtime_light_descriptor_binding_contract": light_vector_prep.get(
                "runtime_light_descriptor_binding_contract", {}
            ),
            "runtime_light_descriptor_binding_instruction_checks": light_vector_prep.get(
                "runtime_light_descriptor_binding_instruction_checks", []
            ),
            "all_runtime_light_descriptor_binding_checks_verified": light_vector_prep.get(
                "all_runtime_light_descriptor_binding_checks_verified", False
            ),
            "runtime_light_descriptor_selector_contract": light_vector_prep.get(
                "runtime_light_descriptor_selector_contract", {}
            ),
            "runtime_light_descriptor_selector_instruction_checks": light_vector_prep.get(
                "runtime_light_descriptor_selector_instruction_checks", []
            ),
            "all_runtime_light_descriptor_selector_checks_verified": light_vector_prep.get(
                "all_runtime_light_descriptor_selector_checks_verified", False
            ),
            "runtime_light_descriptor_callsite_contract": light_vector_prep.get(
                "runtime_light_descriptor_callsite_contract", {}
            ),
            "runtime_light_descriptor_callsite_instruction_checks": light_vector_prep.get(
                "runtime_light_descriptor_callsite_instruction_checks", []
            ),
            "all_runtime_light_descriptor_callsite_checks_verified": light_vector_prep.get(
                "all_runtime_light_descriptor_callsite_checks_verified", False
            ),
            "literal_constants": light_vector_prep.get("literal_constants", []),
            "producer_candidates": light_vector_prep.get("producer_candidates", []),
            "native_pica_light_list_contract": light_vector_prep.get(
                "native_pica_light_list_contract", {}
            ),
            "native_asset_source_links": light_vector_prep.get("native_asset_source_links", []),
            "all_instruction_checks_verified": light_vector_prep.get(
                "all_instruction_checks_verified", False
            ),
            "promotable_meaning": "engine light packets should be decoded as three native OOT3D code-prepared PICA VS light slots, not copied from a frame dump",
        },
        {
            "name": "native_material_draw_state_dispatch",
            "origin_class": "code_bin_disassembly_structure",
            "code_bin": str(code_bin),
            "runtime_range": "0x00452854..0x00452920",
            "evidence": [
                "material entry index is multiplied by 0x73 and then by 4, implying a 0x1CC-byte runtime material/effect record lane",
                "CMB mesh byte +0x02 is loaded as the material/lane index and byte +0x03 is used as the visibility gate index",
                "the lane is selected before mesh draw callbacks and before optional PICA state setup",
                "byte at selected lane pointer +0x02 gates the 0x47D6AC PICA/material state path",
                "draw callbacks follow after the optional PICA state call, so this is source state setup, not a post-render artifact",
            ],
            "structured_contract": material_dispatch.get("native_struct_contract", {}),
            "instruction_checks": material_dispatch.get("instruction_checks", []),
            "all_instruction_checks_verified": material_dispatch.get("all_instruction_checks_verified", False),
            "promotable_meaning": "material PICA state should be traced back to native CMB-derived runtime material records and the 0x47D6AC setup path",
        },
        {
            "name": "native_pica_material_scalar_emit",
            "origin_class": "code_bin_disassembly_structure",
            "code_bin": str(code_bin),
            "runtime_range": "0x0047D6AC..0x0047FF2C",
            "evidence": [
                "0x47D6AC dispatches enabled material state through 0x47FE44 and 0x47FED8",
                "0x47FED8 emits PICA register 0x0E6 with one word and mask 0xF",
                "0x47FF10 emits PICA register 0x0E8 with 0x80 words and mask 0xF",
                "literal packet words 0x000500E0 and 0x000F00E1 sit immediately before this path and correspond to the observed 0x0E0/0x0E1 state packet shape",
            ],
            "promotable_meaning": "fog/light LUT and related scalar PICA state must be recovered from the native material state path or its source tables, not embedded from trace values",
        },
        {
            "name": "native_framebuffer_flush_register_emit",
            "origin_class": "code_bin_disassembly_structure",
            "code_bin": str(code_bin),
            "runtime_range": "0x00438550..0x004385F0",
            "evidence": [
                "loads a three-word literal table from 0x004E2E90 into a stack packet",
                "calls generic scalar writer 0x307BD8 for registers 0x111, 0x110, and 0x010",
                "0x111 is built as 1 + 0x110, 0x110 is emitted directly, and 0x010 is emitted last",
            ],
            "structured_contract": framebuffer_flush.get("native_struct_contract", {}),
            "instruction_checks": framebuffer_flush.get("instruction_checks", []),
            "literal_table_words": framebuffer_flush.get("literal_table_words", []),
            "register_sequence": framebuffer_flush.get("register_sequence", []),
            "all_instruction_checks_verified": framebuffer_flush.get(
                "all_instruction_checks_verified", False
            ),
            "promotable_meaning": "framebuffer flush/invalidate/finalize register writes are code-generated backend state and should be represented as code-origin PICA state",
        },
        {
            "name": "generic_pica_command_writers",
            "origin_class": "code_bin_disassembly_structure",
            "code_bin": str(code_bin),
            "runtime_range": "0x00307BD8..0x00307D84",
            "evidence": [
                "0x307BD8 constructs scalar PICA command headers from register id, mask, and word count",
                "0x307C94 emits vector/uniform data and writes float lanes in PICA command order",
                "these writers are generic packet emitters; semantic ownership belongs to their callers",
            ],
            "promotable_meaning": "engine import should attach semantics to caller-level structures, while retaining generic writer shape only as packet-format evidence",
        },
    ]


def write_markdown(report: dict[str, Any], path: Path) -> None:
    lines = [
        "# OOT3D PICA Native Origin Audit",
        "",
        "The emulator trace is used only as a list of questions. Promotable data must resolve to native OOT3D assets or code.bin/static code evidence.",
        "",
        "## Summary",
        "",
        f"- Native source files: {len(report['native_source_files'])}",
        f"- Draw bindings: {report['asset_binding_provenance']['binding_count']}",
        f"- Lighting records in active setup: {report['lighting_provenance']['active_record_count']}",
        f"- Scalar register values checked: {len(report['scalar_register_value_origins'])}",
        f"- Code structures identified: {len(report['code_structure_evidence'])}",
        f"- Light vector prep instruction checks verified: {report['light_vector_prep_loop_structure'].get('all_instruction_checks_verified', False)}",
        f"- Runtime light packet buffer checks verified: {report['light_vector_prep_loop_structure'].get('all_runtime_packet_buffer_checks_verified', False)}",
        f"- Default light packet layout checks verified: {report['light_vector_prep_loop_structure'].get('all_default_packet_layout_checks_verified', False)}",
        f"- Runtime source provider checks verified: {report['light_vector_prep_loop_structure'].get('all_runtime_source_provider_checks_verified', False)}",
        f"- z_movie resource context checks verified: {report['light_vector_prep_loop_structure'].get('all_z_movie_resource_context_checks_verified', False)}",
        f"- Surface light-setting selection checks verified: {report['light_vector_prep_loop_structure'].get('all_surface_light_setting_selection_checks_verified', False)}",
        f"- Environment light-setting transition reset checks verified: {report['light_vector_prep_loop_structure'].get('all_environment_light_setting_transition_reset_checks_verified', False)}",
        f"- Environment light-setting request helper checks verified: {report['light_vector_prep_loop_structure'].get('all_environment_light_setting_request_helper_checks_verified', False)}",
        f"- Provider table population checks verified: {report['light_vector_prep_loop_structure'].get('all_provider_table_population_checks_verified', False)}",
        f"- Native resource-context payload checks verified: {report['light_vector_prep_loop_structure'].get('all_native_resource_context_payload_checks_verified', False)}",
        f"- Native object-bank population checks verified: {report['light_vector_prep_loop_structure'].get('all_native_object_bank_population_checks_verified', False)}",
        f"- Native CTXB descriptor binding checks verified: {report['light_vector_prep_loop_structure'].get('all_native_ctxb_descriptor_binding_checks_verified', False)}",
        f"- Runtime default record checks verified: {report['light_vector_prep_loop_structure'].get('all_runtime_default_record_checks_verified', False)}",
        f"- Runtime light descriptor binding checks verified: {report['light_vector_prep_loop_structure'].get('all_runtime_light_descriptor_binding_checks_verified', False)}",
        f"- Runtime light descriptor selector checks verified: {report['light_vector_prep_loop_structure'].get('all_runtime_light_descriptor_selector_checks_verified', False)}",
        f"- Runtime light descriptor callsite checks verified: {report['light_vector_prep_loop_structure'].get('all_runtime_light_descriptor_callsite_checks_verified', False)}",
        f"- Native light-list helper checks verified: {report['light_vector_prep_loop_structure'].get('all_light_list_helper_instruction_checks_verified', False)}",
        f"- Kankyo extracted CTXB direct-index coverage: {report['native_kankyo_extracted_zar_ctxb_inventory'].get('direct_individual_archive_ctxb_index_possible', False)}",
        f"- Material dispatch instruction checks verified: {report['material_draw_dispatch_structure'].get('all_instruction_checks_verified', False)}",
        f"- Framebuffer flush instruction checks verified: {report['framebuffer_flush_register_emit_structure'].get('all_instruction_checks_verified', False)}",
        f"- Native texture catalog entries: {report['mismatch_native_texture_search']['catalog_texture_count']}",
        f"- Native draw primitive catalog entries: {report['mismatch_native_texture_search']['catalog_draw_primitive_count']}",
        "",
        "## Binding Texture Origin Counts",
        "",
    ]
    for key, value in report["asset_binding_provenance"]["texture_status_counts"].items():
        lines.append(f"- `{key}`: {value}")

    lines.extend(["", "## Texture/Material Mismatches", ""])
    mismatches = [
        row
        for row in report["asset_binding_provenance"]["bindings"]
        if row["texture_status"] == "format_mismatch_needs_asset_or_stage_origin"
    ]
    if not mismatches:
        lines.append("- None")
    else:
        for row in mismatches:
            lines.append(
                f"- draw {row['trace_draw_index']}: trace `{row['trace_texture_format']}` -> "
                f"{row['model_scope']} `{row['model_name']}` texture `{row['texture_name']}` "
                f"from `{row['source_file']}`"
            )

    lines.extend(["", "## Mismatch Native Texture Search", ""])
    search = report["mismatch_native_texture_search"]
    if not search["searches"]:
        lines.append("- None")
    else:
        for key, value in sorted(search["status_counts"].items()):
            lines.append(f"- `{key}`: {value}")
        for key, value in sorted(search["draw_owner_status_counts"].items()):
            lines.append(f"- `{key}`: {value}")
        lines.append("")
        for row in search["searches"]:
            query = row["trace_texture_query"]
            lines.append(
                f"- draw {row['trace_draw_index']}: `{query['format']}` "
                f"{query['width']}x{query['height']} vertices={query['vertex_count']} -> "
                f"`{row['status']}`, owner `{row['draw_owner_status']}` "
                f"same_archive={row['same_archive_match_count']} resolved_archives={row['resolved_archive_match_count']} "
                f"primitive_owners={row['resolved_primitive_owner_match_count']}"
            )
            for match in row["same_archive_matches"][:4]:
                lines.append(
                    f"  - same archive `{match['archive_path']}` `{match['cmb_entry_name']}` "
                    f"texture `{match['texture_name']}` index {match['texture_index']}"
                )
            if not row["same_archive_matches"]:
                for match in row["resolved_archive_matches"][:4]:
                    lines.append(
                        f"  - other archive `{match['archive_path']}` `{match['cmb_entry_name']}` "
                        f"texture `{match['texture_name']}` index {match['texture_index']}"
                    )
            owner_matches = row["same_archive_primitive_owner_matches"] or row["resolved_primitive_owner_matches"]
            for match in owner_matches[:4]:
                lines.append(
                    f"  - owner `{match['archive_path']}` `{match['cmb_entry_name']}` "
                    f"mesh {match['mesh_index']} material {match['material_index']} primitive {match['primitive_index']} "
                    f"texture `{match['texture_name']}`"
                )

    lines.extend(["", "## Material Dispatch Contract", ""])
    dispatch = report["material_draw_dispatch_structure"]
    if not dispatch.get("available"):
        lines.append("- Not available")
    else:
        contract = dispatch.get("native_struct_contract", {})
        lines.append(
            f"- Verified: `{dispatch.get('all_instruction_checks_verified')}`; "
            f"runtime range `{dispatch.get('runtime_range')}`"
        )
        lines.append(
            f"- CMB mesh material index offset `{contract.get('cmb_mesh_material_index_offset')}` "
            f"selects runtime lane stride `{contract.get('runtime_material_lane_stride_bytes')}`"
        )
        lines.append(
            f"- CMB mesh visibility id offset `{contract.get('cmb_mesh_visibility_id_offset')}` "
            "gates draw visibility before material setup"
        )

    lines.extend(["", "## Native Light Vector Prep Contract", ""])
    light_vector_prep = report["light_vector_prep_loop_structure"]
    if not light_vector_prep.get("available"):
        lines.append("- Not available")
    else:
        contract = light_vector_prep.get("native_struct_contract", {})
        lines.append(
            f"- Verified: `{light_vector_prep.get('all_instruction_checks_verified')}`; "
            f"runtime range `{light_vector_prep.get('runtime_range')}`"
        )
        lines.append(
            f"- Packet `{contract.get('packet_name')}` has `{contract.get('slot_count')}` "
            f"slots of stride `{contract.get('slot_stride_bytes')}`"
        )
        lines.append(
            f"- Prep function `{contract.get('prep_function')}`; producer checks "
            f"`{light_vector_prep.get('all_producer_instruction_checks_verified')}`"
        )
        lines.append(
            "- Source vector offsets "
            f"`x={contract.get('source_vector_offsets', {}).get('x')}, "
            f"y={contract.get('source_vector_offsets', {}).get('y')}, "
            f"z={contract.get('source_vector_offsets', {}).get('z')}` -> "
            "prepared vector offsets "
            f"`x={contract.get('prepared_vector_offsets', {}).get('x')}, "
            f"y={contract.get('prepared_vector_offsets', {}).get('y')}, "
            f"z={contract.get('prepared_vector_offsets', {}).get('z')}`; "
            f"gate/intensity `{contract.get('enable_or_intensity_offset')}`"
        )
        lines.append(
            f"- Disabled-slot fallback vector `{contract.get('disabled_fallback_vector')}`; "
            f"active ZSI light records `{light_vector_prep.get('active_zsi_light_record_count')}`"
        )
        buffer_contract = light_vector_prep.get("runtime_packet_buffer_contract", {})
        if buffer_contract:
            lines.append(
                f"- Runtime packet buffer `{buffer_contract.get('render_context_packet_buffer_slot')}` "
                f"allocated in `{buffer_contract.get('allocation_range')}` with size "
                f"`{buffer_contract.get('allocated_size')}`; checks "
                f"`{light_vector_prep.get('all_runtime_packet_buffer_checks_verified')}`"
            )
            lines.append(
                f"- Runtime buffer consumer `{buffer_contract.get('render_path_consumer_function')}` "
                f"range `{buffer_contract.get('render_path_consumer_range')}`; "
                f"{buffer_contract.get('copy_contract')}"
            )
            lines.append(
                f"- Runtime buffer remaining gap: {buffer_contract.get('remaining_gap')}"
            )
        default_layout = light_vector_prep.get("native_default_light_packet_layout", {})
        if default_layout:
            lines.append(
                f"- Native default packet layout `{default_layout.get('initializer')}` confirms "
                f"{default_layout.get('slot_count')} slots of stride "
                f"`{default_layout.get('slot_stride_bytes')}`; checks "
                f"`{light_vector_prep.get('all_default_packet_layout_checks_verified')}`"
            )
            lines.append(
                f"- Default layout fields: source payloads "
                f"`{', '.join(default_layout.get('source_payload_offsets', []))}`, "
                f"source vectors `{', '.join(default_layout.get('source_vector_offsets', []))}`, "
                f"enable `{default_layout.get('enable_or_intensity_offset')}`"
            )
            lines.append(f"- Default layout meaning: {default_layout.get('meaning')}")
        source_provider = light_vector_prep.get("runtime_source_provider_contract", {})
        if source_provider:
            lines.append(
                f"- Runtime source provider `{source_provider.get('provider_lookup_function')}` "
                f"id `{source_provider.get('selected_provider_id')}` -> "
                f"`{source_provider.get('provider_result_stack_slot')}`; checks "
                f"`{light_vector_prep.get('all_runtime_source_provider_checks_verified')}`"
            )
            lines.append(f"- Runtime source provider meaning: {source_provider.get('meaning')}")
            lines.append(
                f"- Runtime source provider remaining gap: "
                f"{source_provider.get('remaining_gap')}"
            )
        z_movie_context = light_vector_prep.get("z_movie_resource_context_contract", {})
        if z_movie_context:
            entry_zero_paths = z_movie_context.get("entry_zero_paths", [])
            sample_paths = [
                row.get("text", "")
                for row in entry_zero_paths
                if row.get("language_index") in (0, 8)
            ]
            lines.append(
                f"- z_movie resource context `{z_movie_context.get('resource_context_table')}` "
                f"entry zero is populated in `{z_movie_context.get('entry_zero_population_range')}` "
                f"from path table `{z_movie_context.get('entry_zero_path_table')}`; checks "
                f"`{light_vector_prep.get('all_z_movie_resource_context_checks_verified')}`"
            )
            lines.append(
                f"- z_movie debug source: `{z_movie_context.get('debug_source_path')}`; "
                f"sample paths `{', '.join(sample_paths)}`"
            )
            lines.append(
                f"- z_movie classification: `{z_movie_context.get('classification')}`"
            )
            lines.append(f"- z_movie meaning: {z_movie_context.get('meaning')}")
            lines.append(
                f"- z_movie remaining gap: {z_movie_context.get('remaining_gap')}"
            )
        surface_light_setting = light_vector_prep.get(
            "surface_light_setting_selection_contract", {}
        )
        if surface_light_setting:
            lines.append(
                f"- Surface light-setting selection getter "
                f"`{surface_light_setting.get('surface_type_light_setting_getter')}` "
                f"field `{surface_light_setting.get('field_id')}` -> "
                f"`{surface_light_setting.get('field_extract')}`; checks "
                f"`{light_vector_prep.get('all_surface_light_setting_selection_checks_verified')}`"
            )
            lines.append(
                f"- Surface light-setting collision callsite "
                f"`{surface_light_setting.get('player_collision_callsite')}` calls "
                f"`{surface_light_setting.get('light_setting_change_helper')}`; current "
                f"`{surface_light_setting.get('current_light_setting_offset')}`, previous "
                f"`{surface_light_setting.get('previous_light_setting_offset')}`"
            )
            lines.append(
                f"- Surface light-setting source: "
                f"`{surface_light_setting.get('native_light_settings_source')}`, setup "
                f"`{surface_light_setting.get('active_setup_index')}`, records "
                f"`{surface_light_setting.get('active_record_count')}`, layout "
                f"`{surface_light_setting.get('source_layout')}`"
            )
            lines.append(
                f"- Surface light-setting meaning: "
                f"{surface_light_setting.get('meaning')}"
            )
            lines.append(
                f"- Surface light-setting remaining gap: "
                f"{surface_light_setting.get('remaining_gap')}"
            )
        transition_reset = light_vector_prep.get(
            "environment_light_setting_transition_reset_contract", {}
        )
        if transition_reset:
            lines.append(
                f"- Environment light-setting transition reset helper "
                f"`{transition_reset.get('helper')}` is called at "
                f"`{transition_reset.get('single_verified_caller')}`; checks "
                f"`{light_vector_prep.get('all_environment_light_setting_transition_reset_checks_verified')}`"
            )
            lines.append(
                f"- Transition reset fields: mode `{transition_reset.get('mode_or_state_offset')}`, "
                f"state `{transition_reset.get('transition_state_offset')}`, current "
                f"`{transition_reset.get('current_light_setting_offset')}`, previous "
                f"`{transition_reset.get('previous_light_setting_offset')}`, blend "
                f"`{transition_reset.get('transition_blend_weight_offset')}`"
            )
            lines.append(
                f"- Transition reset behavior: "
                f"{transition_reset.get('active_path_behavior')}"
            )
            lines.append(
                f"- Transition reset meaning: {transition_reset.get('meaning')}"
            )
            lines.append(
                f"- Transition reset remaining gap: "
                f"{transition_reset.get('remaining_gap')}"
            )
        request_helper = light_vector_prep.get(
            "environment_light_setting_request_helper_contract", {}
        )
        if request_helper:
            lines.append(
                f"- Environment light-setting request helper "
                f"`{request_helper.get('helper')}` callers "
                f"`{', '.join(request_helper.get('verified_callers', []))}`; checks "
                f"`{light_vector_prep.get('all_environment_light_setting_request_helper_checks_verified')}`"
            )
            lines.append(
                f"- Request helper fields: mode `{request_helper.get('mode_or_state_offset')}`, "
                f"active target `{request_helper.get('active_target_light_setting_offset')}`, "
                f"active state `{request_helper.get('active_transition_state_offset')}`, "
                f"inactive fallback `{', '.join(request_helper.get('inactive_fallback_offsets', []))}`"
            )
            lines.append(
                f"- Request helper normalization: "
                f"{request_helper.get('normalization_rule')}"
            )
            for caller in request_helper.get("caller_classification", []):
                caller_name = caller.get("symbol") or caller.get("enclosing_symbol_neighborhood")
                requested = caller.get("requested_setting", caller.get("requested_setting_source"))
                lines.append(
                    f"- Request helper caller `{caller.get('callsite')}`: "
                    f"{caller.get('classification')} (`{caller_name}`), requested "
                    f"`{requested}`, PlayState `{caller.get('play_source')}`; "
                    f"{caller.get('meaning')}"
                )
            lines.append(
                f"- Request helper meaning: {request_helper.get('meaning')}"
            )
            lines.append(
                f"- Request helper remaining gap: "
                f"{request_helper.get('remaining_gap')}"
            )
        native_kankyo = light_vector_prep.get(
            "native_kankyo_resource_bridge_contract", {}
        )
        if native_kankyo:
            sync_helper = native_kankyo.get("sync_helper", {})
            lines.append(
                f"- Native kankyo bridge `{native_kankyo.get('debug_source_file')}` "
                f"opens `{native_kankyo.get('common_archive_path')}`; checks "
                f"`{light_vector_prep.get('all_native_kankyo_resource_bridge_checks_verified')}`"
            )
            lines.append(
                f"- Native kankyo resource ids: "
                f"{', '.join(str(row.get('source_id')) for row in native_kankyo.get('resource_source_ids', []))}; "
                f"subresources `{', '.join(native_kankyo.get('subresource_ids', []))}` "
                f"through `{native_kankyo.get('subresource_resolver')}` and "
                f"`{native_kankyo.get('descriptor_binder')}`"
            )
            for binding in native_kankyo.get("descriptor_slot_bindings", []):
                if not isinstance(binding, dict):
                    continue
                note = binding.get("note")
                note_text = f"; {note}" if note else ""
                lines.append(
                    f"- Native kankyo descriptor binding `{binding.get('binding_callsite')}`: "
                    f"subresource `{binding.get('subresource_id')}` -> "
                    f"`{binding.get('descriptor_object_field')}` slot "
                    f"`{binding.get('descriptor_slot')}`{note_text}"
                )
            lines.append(
                f"- Native kankyo sync helper `{sync_helper.get('function')}` caller "
                f"`{sync_helper.get('single_verified_caller')}` copies "
                f"`{sync_helper.get('copied_fields')}` from "
                f"`{sync_helper.get('source_region')}`"
            )
            lines.append(
                f"- Native kankyo meaning: {native_kankyo.get('meaning')}"
            )
            lines.append(
                f"- Native kankyo remaining gap: {native_kankyo.get('remaining_gap')}"
            )
        native_resolver = light_vector_prep.get(
            "native_resource_resolver_contract", {}
        )
        if native_resolver:
            source_resolver = native_resolver.get("source_id_resolver", {})
            subresource_resolver = native_resolver.get("subresource_resolver", {})
            lines.append(
                f"- Native resource resolver `{source_resolver.get('function')}` maps "
                f"source ids through `{source_resolver.get('entry_stride_bytes')}` records "
                f"at `{source_resolver.get('source_id_field')}`; checks "
                f"`{light_vector_prep.get('all_native_resource_resolver_checks_verified')}`"
            )
            lines.append(
                f"- Native subresource resolver `{subresource_resolver.get('function')}` "
                f"bounds-checks ids via `{subresource_resolver.get('active_subresource_count_source')}`, "
                f"resolves offsets through `{subresource_resolver.get('offset_indirection_table_field')}`, "
                f"and caches decoded payloads at `{subresource_resolver.get('decoded_payload_cache_field')}`"
            )
            lines.append(
                f"- Native resolver meaning: {native_resolver.get('kankyo_ids_interpretation')}"
            )
            lines.append(
                f"- Native resolver engine requirement: {native_resolver.get('engine_requirement')}"
            )
            lines.append(
                f"- Native resolver remaining gap: {native_resolver.get('remaining_gap')}"
            )
        resource_payload = light_vector_prep.get(
            "native_resource_context_payload_contract", {}
        )
        if resource_payload:
            lines.append(
                f"- Native resource-context payload lookup base "
                f"`{resource_payload.get('resource_context_table_base')}` uses stride "
                f"`{resource_payload.get('resource_entry_stride_bytes')}`; checks "
                f"`{light_vector_prep.get('all_native_resource_context_payload_checks_verified')}`"
            )
            lines.append(
                f"- Native resource-context payload expression: "
                f"`{resource_payload.get('entry_payload_base_expression')}` after availability "
                f"`{resource_payload.get('entry_availability_pointer_absolute_offset')}`"
            )
            for consumer in resource_payload.get("verified_payload_lookup_consumers", []):
                lines.append(
                    f"- Resource-context consumer `{consumer.get('callsite')}`: "
                    f"source `{consumer.get('source_id')}`, "
                    f"{consumer.get('scope')} -> {consumer.get('payload_consumer')}"
                )
            lines.append(
                f"- Native resource-context payload meaning: {resource_payload.get('meaning')}"
            )
            lines.append(
                f"- Native resource-context payload remaining gap: "
                f"{resource_payload.get('remaining_gap')}"
            )
        object_bank = light_vector_prep.get(
            "native_object_bank_population_contract", {}
        )
        object_bank_inventory = light_vector_prep.get("native_object_bank_inventory", {})
        if object_bank:
            lines.append(
                f"- Native object bank `{object_bank.get('object_spawn_function')}` / "
                f"`{object_bank.get('object_update_bank_function')}` populates "
                f"`{object_bank.get('object_bank_entry_stride_bytes')}` entries from object table "
                f"`{object_bank.get('object_table_pointer')}`; checks "
                f"`{light_vector_prep.get('all_native_object_bank_population_checks_verified')}`"
            )
            for row in object_bank_inventory.get("source_rows", []):
                if row.get("source_id") not in (1, 2, 3):
                    continue
                lines.append(
                    f"- Object source `{row.get('source_id')}` `{row.get('object_name')}` -> "
                    f"`{row.get('rom_path')}` CTXB count `{row.get('ctxb_count')}`"
                )
                requested_entries = [
                    entry
                    for entry in row.get("requested_ctxb_entries", [])
                    if not entry.get("missing")
                ]
                for entry in requested_entries:
                    ctxb_summary = entry.get("ctxb_summary") or {}
                    texture_text = ""
                    if isinstance(ctxb_summary, dict) and ctxb_summary:
                        texture_text = (
                            f" `{ctxb_summary.get('width')}x{ctxb_summary.get('height')}` "
                            f"`{ctxb_summary.get('format_name')}/"
                            f"{ctxb_summary.get('data_type_name')}`"
                        )
                    lines.append(
                        f"- Object source `{row.get('source_id')}` CTXB `{entry.get('requested_id')}` "
                        f"-> `{entry.get('name')}` file index `{entry.get('file_index')}` "
                        f"size `{entry.get('size')}`{texture_text}"
                    )
            lines.append(
                f"- Native object bank meaning: {object_bank.get('meaning')}"
            )
            lines.append(
                f"- Native object bank remaining gap: {object_bank.get('remaining_gap')}"
            )
        ctxb_binding = light_vector_prep.get(
            "native_ctxb_descriptor_binding_contract", {}
        )
        if ctxb_binding:
            source_fields = ctxb_binding.get("source_fields", {})
            slot_fields = ctxb_binding.get("slot_fields", {})
            lines.append(
                f"- Native CTXB descriptor binding `{ctxb_binding.get('function')}` "
                f"uses slot stride `{ctxb_binding.get('slot_stride_bytes')}` and base "
                f"`{ctxb_binding.get('slot_record_base_expression')}`; checks "
                f"`{light_vector_prep.get('all_native_ctxb_descriptor_binding_checks_verified')}`"
            )
            lines.append(
                f"- Native CTXB descriptor source fields: width `{source_fields.get('+0x2C')}`, "
                f"height `{source_fields.get('+0x2E')}`, format `{source_fields.get('+0x30')}`, "
                f"data type `{source_fields.get('+0x32')}`, payload `{source_fields.get('+0x4C')}`"
            )
            lines.append(
                f"- Native CTXB descriptor slot fields: payload `{slot_fields.get('+0x158')}`, "
                f"size `{slot_fields.get('+0x15C')}` / `{slot_fields.get('+0x15E')}`, "
                f"packed format `{slot_fields.get('+0x164')}`"
            )
            lines.append(
                f"- Native CTXB descriptor binding meaning: {ctxb_binding.get('meaning')}"
            )
            lines.append(
                f"- Native CTXB descriptor binding remaining gap: "
                f"{ctxb_binding.get('remaining_gap')}"
            )
        kankyo_instances = light_vector_prep.get(
            "native_kankyo_descriptor_instance_contract", {}
        )
        if kankyo_instances:
            helper_text = ", ".join(
                f"{row.get('function')}({row.get('instance_storage_size')})"
                for row in kankyo_instances.get("runtime_instance_helpers", [])
                if isinstance(row, dict)
            )
            lines.append(
                f"- Native kankyo descriptor runtime instances use helpers "
                f"`{helper_text}`; checks "
                f"`{light_vector_prep.get('all_native_kankyo_descriptor_instance_checks_verified')}`"
            )
            for binding in kankyo_instances.get("callsite_bindings", []):
                if not isinstance(binding, dict):
                    continue
                lines.append(
                    f"- Native kankyo runtime binding `{binding.get('runtime_callsite')}`: "
                    f"subresource `{binding.get('subresource_id')}` descriptor "
                    f"`{binding.get('descriptor_object_field')}` -> runtime "
                    f"`{binding.get('runtime_instance_field')}` through "
                    f"`{binding.get('runtime_helper')}`"
                )
            thunder_update = kankyo_instances.get("thunder_update_consumer", {})
            if isinstance(thunder_update, dict) and thunder_update:
                lines.append(
                    f"- Native kankyo thunder update `{thunder_update.get('range')}`: "
                    f"{thunder_update.get('selector_rule')}; rebinds "
                    f"`{thunder_update.get('descriptor_field')}` slot "
                    f"`{thunder_update.get('descriptor_slot')}` at "
                    f"`{thunder_update.get('descriptor_rebind_callsite')}` and updates "
                    f"`{thunder_update.get('runtime_instance_field')}` at "
                    f"`{thunder_update.get('runtime_update_callsite')}`"
                )
            lines.append(
                f"- Native kankyo runtime instance meaning: "
                f"{kankyo_instances.get('meaning')}"
            )
            lines.append(
                f"- Native kankyo runtime instance remaining gap: "
                f"{kankyo_instances.get('remaining_gap')}"
            )
        native_provider_open = light_vector_prep.get(
            "native_provider_open_contract", {}
        )
        if native_provider_open:
            lines.append(
                f"- Native provider open `{native_provider_open.get('provider_open_helper')}` "
                f"loads `{native_provider_open.get('opened_path')}` for "
                f"`{native_provider_open.get('caller_scope')}` through request constructor "
                f"`{native_provider_open.get('resource_request_constructor')}`; checks "
                f"`{light_vector_prep.get('all_native_provider_open_checks_verified')}`"
            )
            lines.append(
                f"- Native provider request fields: path `{native_provider_open.get('request_path_field')}`, "
                f"status `{native_provider_open.get('request_status_field')}` ready "
                f"`{native_provider_open.get('request_ready_value')}`, handle "
                f"`{native_provider_open.get('request_loaded_handle_field')}`"
            )
            lines.append(
                f"- Native provider open meaning: {native_provider_open.get('meaning')}"
            )
            lines.append(
                f"- Native provider open remaining gap: {native_provider_open.get('remaining_gap')}"
            )
        native_zar_family = light_vector_prep.get(
            "native_zar_resolver_family_contract", {}
        )
        if native_zar_family:
            symbol_rows = native_zar_family.get("symbols", [])
            symbol_text = ", ".join(
                f"{row.get('name')}@{row.get('address')}"
                for row in symbol_rows
                if isinstance(row, dict)
            )
            shared_fields = native_zar_family.get("shared_fields", {})
            lines.append(
                f"- Native ZAR resolver family `{symbol_text}` shares "
                f"`{shared_fields.get('provider_table_pointer')}` and "
                f"`{shared_fields.get('offset_indirection_table')}`; checks "
                f"`{light_vector_prep.get('all_native_zar_resolver_family_checks_verified')}`"
            )
            for row in symbol_rows:
                if not isinstance(row, dict):
                    continue
                lines.append(
                    f"- Native ZAR resolver `{row.get('name')}` uses active index "
                    f"`{row.get('active_index_field')}`, decoded cache "
                    f"`{row.get('decoded_cache_field')}`, decode helper "
                    f"`{row.get('decode_helper')}`"
                )
            lines.append(
                f"- Native ZAR resolver meaning: {native_zar_family.get('meaning')}"
            )
            lines.append(
                f"- Native ZAR resolver remaining gap: {native_zar_family.get('remaining_gap')}"
            )
        native_zar_setup = light_vector_prep.get("native_zar_setup_contract", {})
        if native_zar_setup:
            type_table = native_zar_setup.get("type_table", [])
            type_names = ", ".join(
                str(row.get("name"))
                for row in type_table
                if isinstance(row, dict) and row.get("name")
            )
            lines.append(
                f"- Native ZAR setup `{native_zar_setup.get('symbol')}` "
                f"`{native_zar_setup.get('function')}` builds provider tables from "
                f"`{native_zar_setup.get('type_table_address')}`; checks "
                f"`{light_vector_prep.get('all_native_zar_setup_checks_verified')}`"
            )
            lines.append(
                f"- Native ZAR type slots: {type_names}"
            )
            lines.append(
                f"- Native ZAR CTXB slot `{native_zar_setup.get('ctxb_type_slot')}` "
                f"maps to active index `{native_zar_setup.get('ctxb_active_index_field')}` "
                f"and cache `{native_zar_setup.get('ctxb_cache_field')}`"
            )
            lines.append(
                f"- Native ZAR setup meaning: {native_zar_setup.get('meaning')}"
            )
            lines.append(
                f"- Native ZAR setup remaining gap: {native_zar_setup.get('remaining_gap')}"
            )
        kankyo_ctxb_inventory = report.get("native_kankyo_extracted_zar_ctxb_inventory", {})
        if kankyo_ctxb_inventory.get("available"):
            lines.append(
                f"- Extracted kankyo ZAR CTXB inventory: "
                f"{kankyo_ctxb_inventory.get('archive_count')} archives, "
                f"{kankyo_ctxb_inventory.get('total_ctxb_file_count')} CTXB files, "
                f"max local CTXB index `{kankyo_ctxb_inventory.get('max_ctxb_type_local_index')}`; "
                f"requested native ids "
                f"`{', '.join(kankyo_ctxb_inventory.get('requested_ctxb_ids', []))}`"
            )
            lines.append(
                f"- Extracted kankyo direct CTXB-index coverage: "
                f"`{kankyo_ctxb_inventory.get('direct_individual_archive_ctxb_index_possible')}`; "
                f"{kankyo_ctxb_inventory.get('interpretation')}"
            )
            for archive in kankyo_ctxb_inventory.get("archives", []):
                ctxb_names = ", ".join(
                    f"{row.get('ctxb_local_index')}:{row.get('name')}"
                    for row in archive.get("ctxb_files", [])
                )
                if not ctxb_names:
                    ctxb_names = "none"
                lines.append(
                    f"- Kankyo archive `{archive.get('name')}` CTXB local entries: {ctxb_names}"
                )
        provider_population = light_vector_prep.get("provider_table_population_contract", {})
        if provider_population:
            lines.append(
                f"- Provider table population `{provider_population.get('population_function')}` "
                f"fills `{provider_population.get('slot_count')}` slots at "
                f"`{provider_population.get('provider_table_runtime_address')}`; checks "
                f"`{light_vector_prep.get('all_provider_table_population_checks_verified')}`"
            )
            lines.append(
                f"- Provider slot `{provider_population.get('selected_provider_id')}` -> "
                f"`{provider_population.get('selected_provider_path_template')}` via format "
                f"`{provider_population.get('path_format')}`"
            )
            lines.append(
                f"- Provider table meaning: {provider_population.get('meaning')}"
            )
            lines.append(
                f"- Provider table remaining gap: "
                f"{provider_population.get('remaining_gap')}"
            )
        default_record = light_vector_prep.get("runtime_default_record_contract", {})
        if default_record:
            lines.append(
                f"- Runtime default record `{default_record.get('default_record_stack_base')}` "
                f"selected as `{default_record.get('selected_register')}` and consumed by "
                f"`{default_record.get('initializer_consumer')}`; checks "
                f"`{light_vector_prep.get('all_runtime_default_record_checks_verified')}`"
            )
            for pointer in default_record.get("literal_pointers", []):
                lines.append(
                    f"- Default record literal `{pointer.get('name')}` "
                    f"`{pointer.get('table_runtime_address')}` -> "
                    f"`{pointer.get('stack_destination')}` bytes `{pointer.get('byte_count')}`"
                )
            lines.append(f"- Runtime default record meaning: {default_record.get('meaning')}")
            lines.append(
                f"- Runtime default record remaining gap: "
                f"{default_record.get('remaining_gap')}"
            )
        descriptor_binding = light_vector_prep.get(
            "runtime_light_descriptor_binding_contract", {}
        )
        if descriptor_binding:
            lines.append(
                f"- Runtime light descriptor `{descriptor_binding.get('descriptor_context_slot')}` "
                f"initialized in `{descriptor_binding.get('render_init_range')}` and selected by "
                f"`{descriptor_binding.get('verified_draw_setup_selector')}` id "
                f"`{descriptor_binding.get('verified_draw_setup_selector_id')}` "
                f"({descriptor_binding.get('verified_draw_setup_callsite_scope')}); checks "
                f"`{light_vector_prep.get('all_runtime_light_descriptor_binding_checks_verified')}`"
            )
            lines.append(
                f"- Runtime light descriptor consumed by builder "
                f"`{descriptor_binding.get('light_list_builder_range')}`; flags at "
                f"`{descriptor_binding.get('descriptor_flag_offset')}`"
            )
            lines.append(
                f"- Runtime light descriptor meaning: "
                f"{descriptor_binding.get('meaning')}"
            )
            lines.append(
                f"- Runtime light descriptor remaining gap: "
                f"{descriptor_binding.get('remaining_gap')}"
            )
        descriptor_selector = light_vector_prep.get(
            "runtime_light_descriptor_selector_contract", {}
        )
        if descriptor_selector:
            lines.append(
                f"- Runtime light descriptor selector "
                f"`{descriptor_selector.get('selector_function')}` uses resolver "
                f"`{descriptor_selector.get('source_id_resolver')}` and stride "
                f"`{descriptor_selector.get('source_id_resolver_entry_stride')}`; checks "
                f"`{light_vector_prep.get('all_runtime_light_descriptor_selector_checks_verified')}`"
            )
            lines.append(
                f"- Runtime light descriptor selector payload base: "
                f"`{descriptor_selector.get('resource_entry_payload_base')}`, "
                f"selected descriptor id "
                f"`{descriptor_selector.get('selected_descriptor_id_from_draw_setup')}`"
            )
            lines.append(
                f"- Runtime light descriptor selector meaning: "
                f"{descriptor_selector.get('meaning')}"
            )
            lines.append(
                f"- Runtime light descriptor selector remaining gap: "
                f"{descriptor_selector.get('remaining_gap')}"
            )
        descriptor_callsite = light_vector_prep.get(
            "runtime_light_descriptor_callsite_contract", {}
        )
        if descriptor_callsite:
            descriptor_ids = ", ".join(
                str(value) for value in descriptor_callsite.get("descriptor_ids_selected", [])
            )
            lines.append(
                f"- Runtime light descriptor callsite `{descriptor_callsite.get('callsite_range')}` "
                f"source `{descriptor_callsite.get('source_id')}` "
                f"`{descriptor_callsite.get('source_id_semantic_annotation')}` -> descriptors "
                f"`{descriptor_ids}`; checks "
                f"`{light_vector_prep.get('all_runtime_light_descriptor_callsite_checks_verified')}`"
            )
            lines.append(
                f"- Runtime light descriptor callsite debug source: "
                f"`{descriptor_callsite.get('debug_source_path')}`; archive hint "
                f"`{descriptor_callsite.get('native_archive_hint')}`"
            )
            lines.append(
                f"- Runtime light descriptor callsite meaning: "
                f"{descriptor_callsite.get('meaning')}"
            )
            lines.append(
                f"- Runtime light descriptor callsite remaining gap: "
                f"{descriptor_callsite.get('remaining_gap')}"
            )
        for row in light_vector_prep.get("native_asset_source_links", []):
            zsi_offset = row.get("zsi_record_offset")
            if zsi_offset is None:
                zsi_offset = ", ".join(row.get("zsi_record_offsets", []))
            runtime_field = row.get("runtime_packet_field")
            if runtime_field is None:
                runtime_field = ", ".join(row.get("runtime_packet_fields", []))
            lines.append(
                f"- ZSI `{row.get('zsi_record_field')}` at `{zsi_offset}` "
                f"links to `{runtime_field}`: "
                f"{row.get('link_status')}"
            )
        for row in light_vector_prep.get("producer_candidates", []):
            lines.append(
                f"- Producer candidate `{row.get('name')}` `{row.get('runtime_range')}`: "
                f"{row.get('remaining_gap')}"
            )
        light_list = light_vector_prep.get("native_pica_light_list_contract", {})
        lines.append(
            f"- Native PICA light-list helpers verified: "
            f"`{light_vector_prep.get('all_light_list_helper_instruction_checks_verified')}`"
        )
        lines.append(
            f"- Light-list descriptor count `{light_list.get('descriptor_record_count_offset')}`, "
            f"flags `{light_list.get('descriptor_flag_offset')}`, emit function "
            f"`{light_list.get('emit_function')}`, registers `{', '.join(light_list.get('emitted_registers', []))}`"
        )
        for row in light_list.get("offset_calculators", []):
            lines.append(
                f"- Offset helper `{row.get('function')}`: `{row.get('formula')}`"
            )
        packer = light_list.get("dynamic_packer", {})
        if packer:
            lines.append(
                f"- Dynamic packer `{packer.get('function')}` writes "
                f"`{', '.join(packer.get('packed_output_fields', []))}` from "
                f"`{packer.get('source_payload')}`"
            )

    lines.extend(["", "## Framebuffer Flush Register Emit Contract", ""])
    framebuffer_flush = report["framebuffer_flush_register_emit_structure"]
    if not framebuffer_flush.get("available"):
        lines.append("- Not available")
    else:
        contract = framebuffer_flush.get("native_struct_contract", {})
        lines.append(
            f"- Verified: `{framebuffer_flush.get('all_instruction_checks_verified')}`; "
            f"runtime range `{framebuffer_flush.get('runtime_range')}`"
        )
        lines.append(
            f"- Emitter `{contract.get('emitter_name')}` uses literal table "
            f"`{contract.get('literal_table_name')}` at "
            f"`{framebuffer_flush.get('literal_table_runtime_address')}`"
        )
        for row in framebuffer_flush.get("register_sequence", []):
            lines.append(
                f"- order {row['order']}: register `{row['register']}` <- "
                f"literal[{row['payload_literal_index']}] `{row['payload_word']}` "
                f"via writer `{row['writer']}`"
            )

    lines.extend(["", "## Material Dispatch Mismatch Analysis", ""])
    dispatch_analysis = report["material_dispatch_mismatch_analysis"]
    if not dispatch_analysis.get("rows"):
        lines.append("- None")
    else:
        for row in dispatch_analysis["rows"]:
            owner = row.get("exact_primitive_owner_candidate", {})
            owner_text = ""
            if owner:
                owner_text = (
                    f"; owner `{owner.get('scope_hint')}` `{owner.get('archive_path')}` "
                    f"mesh `{owner.get('mesh_index')}` material `{owner.get('material_index')}` "
                    f"texture `{owner.get('texture_name')}`"
                )
            lines.append(
                f"- draw {row['trace_draw_index']}: CMB material `{row['cmb_mesh_material_index']}` -> "
                f"runtime lane `{row['code_runtime_material_lane_index']}`; "
                f"`{row['implication']}`{owner_text}; next `{row['next_action']}`"
            )

    lines.extend(["", "## Scalar Register Literal Search", ""])
    counts = Counter(row["origin_class"] for row in report["scalar_register_value_origins"])
    for key, value in sorted(counts.items()):
        lines.append(f"- `{key}`: {value}")

    lines.extend(["", "## Code Structure Evidence", ""])
    for item in report["code_structure_evidence"]:
        lines.append(f"- `{item['name']}` `{item['runtime_range']}`: {item['promotable_meaning']}")

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scene-summary", type=Path, required=True)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--code-bin", type=Path)
    parser.add_argument("--romfs-root", type=Path)
    parser.add_argument("--kankyo-root", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--markdown-output", type=Path)
    args = parser.parse_args()

    summary = read_json(args.scene_summary)
    trace = read_json(args.trace)
    manifest = read_json(args.manifest) if args.manifest and args.manifest.is_file() else None
    romfs_root = args.romfs_root
    if romfs_root is None and args.kankyo_root is not None:
        romfs_root = args.kankyo_root.parent

    native_sources = collect_native_source_files(summary, manifest)
    scalar_values = collect_scalar_values(trace)
    binding_provenance = collect_binding_provenance(summary)
    mismatch_search = collect_mismatch_native_texture_search(summary, binding_provenance)
    lighting_provenance = collect_lighting_provenance(summary)
    light_vector_prep = light_vector_prep_loop_structure(
        args.code_bin, lighting_provenance, romfs_root
    )
    material_dispatch = material_draw_dispatch_structure(args.code_bin)
    framebuffer_flush = framebuffer_flush_register_emit_structure(args.code_bin)
    kankyo_ctxb_inventory = collect_kankyo_extracted_zar_ctxb_inventory(args.kankyo_root)
    report = {
        "format": "oot3d_pica_native_origin_audit_v1",
        "policy": {
            "trace_usage": "validation_query_only",
            "promotable_runtime_source": "native_assets_or_code_bin_only",
            "uses_runtime_n64_asset_substitution": False,
            "uses_emulator_dump_as_asset_source": False,
        },
        "inputs": {
            "scene_summary": str(args.scene_summary),
            "trace": str(args.trace),
            "manifest": str(args.manifest) if args.manifest else "",
            "code_bin": str(args.code_bin) if args.code_bin else "",
            "romfs_root": str(romfs_root) if romfs_root else "",
            "kankyo_root": str(args.kankyo_root) if args.kankyo_root else "",
        },
        "native_source_files": native_sources,
        "asset_binding_provenance": binding_provenance,
        "native_archive_texture_catalog": summary.get("native_archive_texture_catalog", {}),
        "mismatch_native_texture_search": mismatch_search,
        "lighting_provenance": lighting_provenance,
        "light_vector_prep_loop_structure": light_vector_prep,
        "native_kankyo_extracted_zar_ctxb_inventory": kankyo_ctxb_inventory,
        "material_draw_dispatch_structure": material_dispatch,
        "framebuffer_flush_register_emit_structure": framebuffer_flush,
        "material_dispatch_mismatch_analysis": collect_material_dispatch_mismatch_analysis(
            binding_provenance, mismatch_search, material_dispatch
        ),
        "code_structure_evidence": code_structure_evidence(
            args.code_bin, lighting_provenance, romfs_root
        ),
        "scalar_register_value_origins": search_value_origins(
            scalar_values, args.code_bin, native_sources
        ),
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    if args.markdown_output:
        args.markdown_output.parent.mkdir(parents=True, exist_ok=True)
        write_markdown(report, args.markdown_output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
