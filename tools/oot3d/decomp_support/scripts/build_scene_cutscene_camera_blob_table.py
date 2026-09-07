#!/usr/bin/env python3
"""Build source tables for OOT3D native cutscene camera blob command 0x97.

The native cutscene command table identifies command `0x97` as a u32-sized
blob. `Cutscene_ProcessCommands` mounts that blob, reads its segment directory,
selects the active segment from `segment + 0x08/+0x0C`, then passes both the
`caad` segment and its `mads` payload to the camera evaluator.
"""

from __future__ import annotations

import csv
import json
import struct
import sys
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ASSET_TOOL_SRC = ROOT.parent / "oot3d_asset_tool" / "src"
if ASSET_TOOL_SRC.is_dir():
    sys.path.insert(0, str(ASSET_TOOL_SRC))

from oot3d_asset_tool.zar import ZarArchive

DEFAULT_NATIVE_DECODE_TABLE = ROOT / "analysis" / "scene_cutscene_native_decode_table.json"
DEFAULT_TITLE_INTRO_TABLE = ROOT / "analysis" / "title_intro_source_table.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_cutscene_camera_blob_table.json"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_cutscene_camera_blob_table.md"
DEFAULT_OUT_BLOB_CSV = ROOT / "analysis" / "scene_cutscene_camera_blob_table.csv"
DEFAULT_OUT_SEGMENT_CSV = ROOT / "analysis" / "scene_cutscene_camera_blob_segment_table.csv"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "scene_cutscene_camera_blob_table.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_scene_cutscene_camera_blob_table.c"

COMMAND_CAMERA_BLOB_97 = 0x97
MAGIC_CCB = 0x00626363
MAGIC_CAAD = 0x64616163
MAGIC_MADS = 0x7364616D
MAGIC_CMAD = 0x64616D63
NO_INDEX = 0xFFFF
NO_OFFSET = 0xFFFFFFFF
RAW_PREFIX_LIMIT = 0x60
TITLE_INTRO_SPOT00_ARCHIVE = "scene/spot00.zar"


def as_dict(value: Any) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def as_list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def int_value(value: Any, default: int = 0) -> int:
    try:
        if value is None:
            return default
        return int(value)
    except (TypeError, ValueError):
        return default


def hex_u32(value: Any) -> str:
    return f"0x{int_value(value) & 0xFFFFFFFF:08x}"


def c_string(value: Any) -> str:
    text = "" if value is None else str(value)
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def c_u8(value: Any) -> str:
    return f"{int_value(value) & 0xFF}u"


def c_u16(value: Any) -> str:
    return f"{int_value(value) & 0xFFFF}u"


def c_u32(value: Any) -> str:
    return f"{int_value(value) & 0xFFFFFFFF}u"


def c_s32(value: Any) -> str:
    raw = int_value(value)
    return str(max(-2147483648, min(2147483647, raw)))


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True, allow_nan=False)
        handle.write("\n")


def csv_value(value: Any) -> str:
    if isinstance(value, list):
        return "; ".join(str(item) for item in value)
    if isinstance(value, dict):
        return json.dumps(value, sort_keys=True)
    return str(value)


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=fieldnames,
            extrasaction="ignore",
            lineterminator="\n",
        )
        writer.writeheader()
        for row in rows:
            writer.writerow({key: csv_value(row.get(key, "")) for key in fieldnames})


def read_u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def read_s32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def require_available(data: bytes, offset: int, size: int, label: str) -> None:
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError(f"{label}: read outside file at 0x{offset:x} size 0x{size:x}")


def raw_prefix_hex(data: bytes, offset: int, size: int) -> str:
    if offset < 0 or offset >= len(data):
        return ""
    return data[offset : offset + min(size, len(data) - offset)].hex()


def decode_blob_command(
    command_row: dict[str, Any],
    cutscene_row: dict[str, Any],
    scene_root: Path,
    blob_index: int,
    segment_ref_start: int,
    data_override: bytes | None = None,
    scene_path_override: str | None = None,
    source_meta: dict[str, Any] | None = None,
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    scene_path = scene_path_override if scene_path_override is not None else str(command_row.get("scene_path", ""))
    data = data_override if data_override is not None else (scene_root / scene_path).read_bytes()
    source_meta = source_meta or {}
    command_offset = int_value(command_row.get("command_offset"), NO_OFFSET)
    label = f"{scene_path}:{hex_u32(command_offset)}"
    require_available(data, command_offset, 8, label)
    command_id = read_u32(data, command_offset)
    command_blob_size = read_u32(data, command_offset + 4)
    if command_id != COMMAND_CAMERA_BLOB_97:
        raise ValueError(f"{label}: expected command 0x97, found {hex_u32(command_id)}")
    if command_blob_size != int_value(command_row.get("blob_size")):
        raise ValueError(
            f"{label}: command blob size {command_blob_size} does not match decoded table "
            f"{command_row.get('blob_size')}"
        )

    blob_data_offset = command_offset + 8
    require_available(data, blob_data_offset, command_blob_size, label)
    require_available(data, blob_data_offset, 0x18, label)
    blob_magic = read_u32(data, blob_data_offset)
    blob_version = read_u32(data, blob_data_offset + 4)
    logical_blob_size = read_u32(data, blob_data_offset + 8)
    reserved_word0c = read_u32(data, blob_data_offset + 0x0C)
    segment_count = read_u32(data, blob_data_offset + 0x10)
    header_word14 = read_u32(data, blob_data_offset + 0x14)
    segment_directory_end_offset = 0x18 + segment_count * 4

    if logical_blob_size > command_blob_size:
        raise ValueError(f"{label}: logical ccb size exceeds command blob size")
    if segment_directory_end_offset > logical_blob_size:
        raise ValueError(f"{label}: segment directory outside logical ccb size")
    require_available(data, blob_data_offset + 0x18, segment_count * 4, label)
    segment_offsets = [
        read_u32(data, blob_data_offset + 0x18 + segment_index * 4)
        for segment_index in range(segment_count)
    ]

    segments: list[dict[str, Any]] = []
    previous_offset = -1
    for segment_index, segment_offset in enumerate(segment_offsets):
        next_segment_offset = (
            segment_offsets[segment_index + 1]
            if segment_index + 1 < len(segment_offsets)
            else logical_blob_size
        )
        segment_size = next_segment_offset - segment_offset
        segment_absolute_offset = blob_data_offset + segment_offset
        require_available(data, segment_absolute_offset, max(segment_size, 0), label)
        require_available(data, segment_absolute_offset, 0x50, label)

        segment_magic = read_u32(data, segment_absolute_offset)
        segment_word04 = read_u32(data, segment_absolute_offset + 4)
        start_frame = read_s32(data, segment_absolute_offset + 8)
        end_frame = read_s32(data, segment_absolute_offset + 0x0C)
        base_csparam_8c_bits = read_u32(data, segment_absolute_offset + 0x18)
        base_csparam_90_bits = read_u32(data, segment_absolute_offset + 0x1C)
        base_csparam_94_bits = read_u32(data, segment_absolute_offset + 0x20)
        base_csparam_80_bits = read_u32(data, segment_absolute_offset + 0x24)
        base_csparam_84_bits = read_u32(data, segment_absolute_offset + 0x28)
        base_csparam_88_bits = read_u32(data, segment_absolute_offset + 0x2C)
        base_csparam_1a2_source_bits = read_u32(data, segment_absolute_offset + 0x30)
        base_csparam_144_source_bits = read_u32(data, segment_absolute_offset + 0x3C)
        base_csparam_d0_bits = read_u32(data, segment_absolute_offset + 0x44)
        mads_offset = read_u32(data, segment_absolute_offset + 0x48)
        mads_absolute_offset = segment_absolute_offset + mads_offset
        require_available(data, mads_absolute_offset, 0x0C, label)
        mads_magic = read_u32(data, mads_absolute_offset)
        mads_type = read_u32(data, mads_absolute_offset + 4)
        mads_header_size = read_u32(data, mads_absolute_offset + 8)
        cmad_offset = mads_offset + mads_header_size
        cmad_absolute_offset = segment_absolute_offset + cmad_offset
        require_available(data, cmad_absolute_offset, 4, label)
        cmad_magic = read_u32(data, cmad_absolute_offset)

        segments.append(
            {
                "camera_blob_segment_source_index": segment_ref_start + segment_index,
                "camera_blob_source_index": blob_index,
                "native_command_source_index": int_value(
                    command_row.get("native_command_source_index")
                ),
                "cutscene_source_index": int_value(command_row.get("cutscene_source_index")),
                "scene_path": scene_path,
                "setup_index": int_value(command_row.get("setup_index")),
                "local_command_index": int_value(command_row.get("local_command_index")),
                "segment_index": segment_index,
                "segment_offset": segment_offset,
                "segment_offset_hex": hex_u32(segment_offset),
                "segment_absolute_offset": segment_absolute_offset,
                "segment_absolute_offset_hex": hex_u32(segment_absolute_offset),
                "segment_size": segment_size,
                "next_segment_offset": next_segment_offset,
                "segment_magic": segment_magic,
                "segment_magic_hex": hex_u32(segment_magic),
                "segment_word04": segment_word04,
                "start_frame": start_frame,
                "end_frame": end_frame,
                "base_csparam_8c_bits": base_csparam_8c_bits,
                "base_csparam_8c_bits_hex": hex_u32(base_csparam_8c_bits),
                "base_csparam_90_bits": base_csparam_90_bits,
                "base_csparam_90_bits_hex": hex_u32(base_csparam_90_bits),
                "base_csparam_94_bits": base_csparam_94_bits,
                "base_csparam_94_bits_hex": hex_u32(base_csparam_94_bits),
                "base_csparam_80_bits": base_csparam_80_bits,
                "base_csparam_80_bits_hex": hex_u32(base_csparam_80_bits),
                "base_csparam_84_bits": base_csparam_84_bits,
                "base_csparam_84_bits_hex": hex_u32(base_csparam_84_bits),
                "base_csparam_88_bits": base_csparam_88_bits,
                "base_csparam_88_bits_hex": hex_u32(base_csparam_88_bits),
                "base_csparam_1a2_source_bits": base_csparam_1a2_source_bits,
                "base_csparam_1a2_source_bits_hex": hex_u32(base_csparam_1a2_source_bits),
                "base_csparam_144_source_bits": base_csparam_144_source_bits,
                "base_csparam_144_source_bits_hex": hex_u32(base_csparam_144_source_bits),
                "base_csparam_d0_bits": base_csparam_d0_bits,
                "base_csparam_d0_bits_hex": hex_u32(base_csparam_d0_bits),
                "mads_offset": mads_offset,
                "mads_offset_hex": hex_u32(mads_offset),
                "mads_size": max(0, segment_size - mads_offset),
                "mads_magic": mads_magic,
                "mads_magic_hex": hex_u32(mads_magic),
                "mads_type": mads_type,
                "mads_header_size": mads_header_size,
                "cmad_offset": cmad_offset,
                "cmad_offset_hex": hex_u32(cmad_offset),
                "cmad_magic": cmad_magic,
                "cmad_magic_hex": hex_u32(cmad_magic),
                "raw_segment_header_prefix_hex": raw_prefix_hex(
                    data,
                    segment_absolute_offset,
                    min(segment_size, RAW_PREFIX_LIMIT),
                ),
                **source_meta,
            }
        )
        if segment_offset <= previous_offset:
            raise ValueError(f"{label}: non-increasing segment offset at index {segment_index}")
        previous_offset = segment_offset

    blob_row = {
        "camera_blob_source_index": blob_index,
        "native_command_source_index": int_value(command_row.get("native_command_source_index")),
        "cutscene_source_index": int_value(command_row.get("cutscene_source_index")),
        "scene_id": int_value(cutscene_row.get("scene_id"), 0xFF),
        "scene_id_hex": cutscene_row.get("scene_id_hex", ""),
        "scene_path": scene_path,
        "scene_stem": cutscene_row.get("scene_stem", ""),
        "setup_index": int_value(command_row.get("setup_index")),
        "payload_symbol": cutscene_row.get("payload_symbol", ""),
        "local_command_index": int_value(command_row.get("local_command_index")),
        "command_offset": command_offset,
        "command_offset_hex": hex_u32(command_offset),
        "command_blob_size": command_blob_size,
        "blob_data_offset": blob_data_offset,
        "blob_data_offset_hex": hex_u32(blob_data_offset),
        "blob_magic": blob_magic,
        "blob_magic_hex": hex_u32(blob_magic),
        "blob_version": blob_version,
        "logical_blob_size": logical_blob_size,
        "tail_padding_size": command_blob_size - logical_blob_size,
        "reserved_word0c": reserved_word0c,
        "segment_count": segment_count,
        "header_word14": header_word14,
        "header_word14_hex": hex_u32(header_word14),
        "segment_directory_end_offset": segment_directory_end_offset,
        "segment_ref_start": segment_ref_start,
        "segment_ref_count": len(segments),
        "first_segment_offset": segment_offsets[0] if segment_offsets else NO_OFFSET,
        "first_segment_offset_hex": ""
        if not segment_offsets
        else hex_u32(segment_offsets[0]),
        "raw_blob_header_prefix_hex": raw_prefix_hex(data, blob_data_offset, 0x60),
        **source_meta,
    }
    return blob_row, segments


def read_zar_embedded(romfs_root: Path, archive_path: str, embedded_name: str, cache: dict[str, ZarArchive]) -> bytes:
    archive = cache.get(archive_path)
    if archive is None:
        archive = ZarArchive.from_path(romfs_root / archive_path)
        cache[archive_path] = archive
    for file in archive.files:
        if file.name == embedded_name:
            return archive.read_file(file)
    raise FileNotFoundError(f"{archive_path}: embedded file not found: {embedded_name}")


def append_title_intro_spot00_camera_blobs(
    title_table_path: Path,
    scene_root: Path,
    blob_rows: list[dict[str, Any]],
    segment_rows: list[dict[str, Any]],
) -> tuple[int, int, str]:
    if not title_table_path.is_file():
        return 0, 0, ""

    title_table = load_json(title_table_path)
    summary = as_dict(title_table.get("summary"))
    romfs_root = Path(str(summary.get("romfs_root", "")))
    if not romfs_root.is_dir():
        romfs_root = scene_root.parent

    qdb_rows = [as_dict(row) for row in as_list(title_table.get("scene_zar_qdb_rows"))]
    command_rows = [as_dict(row) for row in as_list(title_table.get("scene_zar_qdb_command_rows"))]
    zar_cache: dict[str, ZarArchive] = {}
    added_blobs = 0
    added_segments = 0

    for qdb_row in qdb_rows:
        archive_path = str(qdb_row.get("archive_path", ""))
        if archive_path != TITLE_INTRO_SPOT00_ARCHIVE:
            continue
        embedded_name = str(qdb_row.get("embedded_name", ""))
        qdb_data = read_zar_embedded(romfs_root, archive_path, embedded_name, zar_cache)
        command_ref_start = int_value(qdb_row.get("command_ref_start"))
        command_ref_count = int_value(qdb_row.get("command_ref_count"))
        for command_index in range(command_ref_start, command_ref_start + command_ref_count):
            if command_index < 0 or command_index >= len(command_rows):
                continue
            command_row = dict(command_rows[command_index])
            if int_value(command_row.get("command_id")) != COMMAND_CAMERA_BLOB_97:
                continue
            if command_row.get("blob_size") in (None, ""):
                command_row["blob_size"] = int_value(command_row.get("total_size")) - 8
            command_row["native_command_source_index"] = NO_INDEX
            command_row["cutscene_source_index"] = NO_INDEX
            command_row["scene_path"] = embedded_name
            command_row["setup_index"] = 0
            source_meta = {
                "source_kind": "zar_embedded_qdb",
                "archive_path": archive_path,
                "embedded_name": embedded_name,
                "qdb_index": int_value(qdb_row.get("qdb_index")),
                "qdb_command_index": int_value(command_row.get("qdb_command_index")),
            }
            cutscene_row = {
                "scene_id": 0,
                "scene_id_hex": "0x00",
                "scene_stem": Path(embedded_name.replace("\\", "/")).stem,
                "payload_symbol": "oot3d_title_intro_spot00_qdb",
            }
            before_segments = len(segment_rows)
            blob_row, segments = decode_blob_command(
                command_row,
                cutscene_row,
                scene_root,
                len(blob_rows),
                len(segment_rows),
                data_override=qdb_data,
                scene_path_override=embedded_name,
                source_meta=source_meta,
            )
            blob_rows.append(blob_row)
            segment_rows.extend(segments)
            added_blobs += 1
            added_segments += len(segment_rows) - before_segments

    return added_blobs, added_segments, str(romfs_root)


def build_table(native_decode_path: Path = DEFAULT_NATIVE_DECODE_TABLE) -> dict[str, Any]:
    native_decode = load_json(native_decode_path)
    scene_root = Path(str(as_dict(native_decode.get("summary")).get("scene_root", "")))
    if not scene_root.is_dir():
        raise FileNotFoundError(f"{scene_root}: scene root from native decode table not found")

    cutscene_by_index = {
        int_value(row.get("cutscene_source_index")): as_dict(row)
        for row in as_list(native_decode.get("cutscene_rows"))
        if isinstance(row, dict)
    }
    blob_rows: list[dict[str, Any]] = []
    segment_rows: list[dict[str, Any]] = []
    for command_row in as_list(native_decode.get("native_command_rows")):
        command_row = as_dict(command_row)
        if int_value(command_row.get("command_id")) != COMMAND_CAMERA_BLOB_97:
            continue
        cutscene_row = cutscene_by_index.get(int_value(command_row.get("cutscene_source_index")), {})
        blob_row, segments = decode_blob_command(
            command_row,
            cutscene_row,
            scene_root,
            len(blob_rows),
            len(segment_rows),
        )
        blob_rows.append(blob_row)
        segment_rows.extend(segments)

    title_blob_count, title_segment_count, romfs_root = append_title_intro_spot00_camera_blobs(
        DEFAULT_TITLE_INTRO_TABLE,
        scene_root,
        blob_rows,
        segment_rows,
    )

    tail_padding_counts = Counter(str(row["tail_padding_size"]) for row in blob_rows)
    segment_count_counts = Counter(str(row["segment_count"]) for row in blob_rows)
    scene_counts = Counter(str(row["scene_path"]) for row in blob_rows)
    mads_type_counts = Counter(str(row["mads_type"]) for row in segment_rows)
    intro_blob_rows = [
        row for row in blob_rows if row.get("scene_path") in {"link_info.zsi", "spot04_info.zsi"}
    ]
    intro_segment_rows = [
        row
        for row in segment_rows
        if blob_rows[int_value(row.get("camera_blob_source_index"))].get("scene_path")
        in {"link_info.zsi", "spot04_info.zsi"}
    ]

    summary = {
        "format": "oot3d_scene_cutscene_camera_blob_table_v1",
        "source_decode_table": str(native_decode_path),
        "source_title_intro_table": str(DEFAULT_TITLE_INTRO_TABLE),
        "scene_root": str(scene_root),
        "romfs_root": romfs_root,
        "camera_blob_row_count": len(blob_rows),
        "camera_blob_segment_row_count": len(segment_rows),
        "intro_target_blob_count": len(intro_blob_rows),
        "intro_target_segment_count": len(intro_segment_rows),
        "title_intro_spot00_camera_blob_count": title_blob_count,
        "title_intro_spot00_camera_segment_count": title_segment_count,
        "tail_padding_size_counts": dict(sorted(tail_padding_counts.items())),
        "segment_count_counts": dict(sorted(segment_count_counts.items(), key=lambda item: int(item[0]))),
        "scene_blob_counts": dict(sorted(scene_counts.items())),
        "mads_type_counts": dict(sorted(mads_type_counts.items(), key=lambda item: int(item[0]))),
        "runtime_selection": {
            "interpreter": "Cutscene_ProcessCommands at 0x002C5BA0, command 0x97",
            "mount_helper": "FUN_00494620(csCtx+0x88, command+8)",
            "segment_count_helper": "FUN_0032b69c(csCtx+0x88)",
            "segment_directory": "mounted_base + 0x18 + segmentIndex * 4",
            "active_frame_rule": "segment.startFrame < csCtx.curFrame && csCtx.curFrame < segment.endFrame",
            "segment_evaluator": "FUN_0033cb90(&segment, frame, csCtx+0x94)",
            "camera_apply": "FUN_0033cb1c(camera, csCtx+0x94, play+0x20ac, 0)",
        },
        "native_layout": {
            "command_id": "0x00000097",
            "command_size_word_offset": 4,
            "blob_magic": hex_u32(MAGIC_CCB),
            "blob_version": 3,
            "blob_segment_count_offset": 0x10,
            "blob_segment_directory_offset": 0x18,
            "segment_magic": hex_u32(MAGIC_CAAD),
            "segment_index_offset": 0x04,
            "segment_start_frame_offset": 0x08,
            "segment_end_frame_offset": 0x0C,
            "segment_mads_offset_field": 0x48,
            "mads_magic": hex_u32(MAGIC_MADS),
            "cmad_magic": hex_u32(MAGIC_CMAD),
        },
    }
    return {
        "summary": summary,
        "source_policy": {
            "primary_source": "OOT3D ZSI scene command 0x17 native command 0x97 blobs plus title spot00.zar QDB command 0x97 blobs",
            "runtime_reference": "code.bin Cutscene_ProcessCommands command 0x97 branch",
            "strict_n64_policy": "not used for this table",
        },
        "camera_blob_rows": blob_rows,
        "camera_blob_segment_rows": segment_rows,
    }


BLOB_CSV_COLUMNS = [
    "camera_blob_source_index",
    "native_command_source_index",
    "cutscene_source_index",
    "scene_id_hex",
    "scene_id",
    "scene_path",
    "source_kind",
    "archive_path",
    "embedded_name",
    "qdb_index",
    "qdb_command_index",
    "scene_stem",
    "setup_index",
    "payload_symbol",
    "local_command_index",
    "command_offset_hex",
    "command_blob_size",
    "blob_data_offset_hex",
    "blob_magic_hex",
    "blob_version",
    "logical_blob_size",
    "tail_padding_size",
    "reserved_word0c",
    "segment_count",
    "header_word14_hex",
    "segment_directory_end_offset",
    "segment_ref_start",
    "segment_ref_count",
    "first_segment_offset_hex",
    "raw_blob_header_prefix_hex",
]

SEGMENT_CSV_COLUMNS = [
    "camera_blob_segment_source_index",
    "camera_blob_source_index",
    "native_command_source_index",
    "cutscene_source_index",
    "scene_path",
    "source_kind",
    "archive_path",
    "embedded_name",
    "qdb_index",
    "qdb_command_index",
    "setup_index",
    "local_command_index",
    "segment_index",
    "segment_offset_hex",
    "segment_absolute_offset_hex",
    "segment_size",
    "next_segment_offset",
    "segment_magic_hex",
    "segment_word04",
    "start_frame",
    "end_frame",
    "base_csparam_8c_bits_hex",
    "base_csparam_90_bits_hex",
    "base_csparam_94_bits_hex",
    "base_csparam_80_bits_hex",
    "base_csparam_84_bits_hex",
    "base_csparam_88_bits_hex",
    "base_csparam_1a2_source_bits_hex",
    "base_csparam_144_source_bits_hex",
    "base_csparam_d0_bits_hex",
    "mads_offset_hex",
    "mads_size",
    "mads_magic_hex",
    "mads_type",
    "mads_header_size",
    "cmad_offset_hex",
    "cmad_magic_hex",
    "raw_segment_header_prefix_hex",
]


def write_markdown(path: Path, payload: dict[str, Any]) -> None:
    summary = as_dict(payload.get("summary"))
    blob_rows = [as_dict(row) for row in as_list(payload.get("camera_blob_rows"))]
    intro_rows = [
        row for row in blob_rows if row.get("scene_path") in {"link_info.zsi", "spot04_info.zsi"}
    ]
    lines = [
        "# Scene Cutscene Camera Blob Table",
        "",
        "Generated source table for OOT3D-native cutscene command `0x97` camera blobs.",
        "",
        "## Summary",
        "",
        f"- Camera blob rows: {summary.get('camera_blob_row_count')}",
        f"- Camera blob segment rows: {summary.get('camera_blob_segment_row_count')}",
        f"- Intro target blobs (`link_info.zsi`, `spot04_info.zsi`): {summary.get('intro_target_blob_count')}",
        f"- Intro target segments: {summary.get('intro_target_segment_count')}",
        f"- Tail padding sizes: `{json.dumps(summary.get('tail_padding_size_counts'), sort_keys=True)}`",
        f"- Segment counts: `{json.dumps(summary.get('segment_count_counts'), sort_keys=True)}`",
        f"- `mads` types: `{json.dumps(summary.get('mads_type_counts'), sort_keys=True)}`",
        "",
        "## Native Layout",
        "",
        "- Command `0x97` stores a u32 blob size at command `+0x04`; blob bytes begin at command `+0x08`.",
        "- The blob header is `ccb`, version `3`, then a logical size, zero word, segment count, and a segment offset directory at `+0x18`.",
        "- Each segment begins with `caad`; word `+0x04` matches the segment index, `+0x08/+0x0C` are the runtime frame selection range.",
        "- Segment base camera fields consumed by `FUN_0033CB90` are promoted from `+0x18..+0x44` as exact float bit-patterns.",
        "- Segment `+0x48` points to a `mads` block, whose payload begins with `cmad`; these boundaries are decoded, but interpolation fields remain raw until `FUN_0033cb90` is lowered.",
        "- Runtime selection uses strict bounds: `startFrame < currentFrame && currentFrame < endFrame`.",
        "",
        "## Intro Targets",
        "",
        "| blob | command | scene | setup | command offset | logical size | segments | segment rows |",
        "| ---: | ---: | --- | ---: | ---: | ---: | ---: | --- |",
    ]
    for row in intro_rows:
        segment_slice = f"{row.get('segment_ref_start')}+{row.get('segment_ref_count')}"
        lines.append(
            f"| {row.get('camera_blob_source_index')} | {row.get('native_command_source_index')} | "
            f"{row.get('scene_path')} | {row.get('setup_index')} | `{row.get('command_offset_hex')}` | "
            f"{row.get('logical_blob_size')} | {row.get('segment_count')} | `{segment_slice}` |"
        )
    lines.extend(
        [
            "",
            "## Outputs",
            "",
            "- `include/oot3d/scene_cutscene_camera_blob_table.h`",
            "- `src/code/z_scene_cutscene_camera_blob_table.c`",
            "- `analysis/scene_cutscene_camera_blob_table.json`",
            "- `analysis/scene_cutscene_camera_blob_table.csv`",
            "- `analysis/scene_cutscene_camera_blob_segment_table.csv`",
            "",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_header(path: Path, payload: dict[str, Any]) -> None:
    blob_count = len(as_list(payload.get("camera_blob_rows")))
    segment_count = len(as_list(payload.get("camera_blob_segment_rows")))
    lines = [
        "#ifndef OOT3D_SCENE_CUTSCENE_CAMERA_BLOB_TABLE_H",
        "#define OOT3D_SCENE_CUTSCENE_CAMERA_BLOB_TABLE_H",
        "",
        '#include "oot3d/scene.h"',
        '#include "oot3d/scene_cutscene_native_source_table.h"',
        "",
        "enum {",
        f"    OOT3D_SCENE_CUTSCENE_CAMERA_BLOB_ROW_COUNT = {blob_count},",
        f"    OOT3D_SCENE_CUTSCENE_CAMERA_BLOB_SEGMENT_ROW_COUNT = {segment_count},",
        "    OOT3D_SCENE_CUTSCENE_CAMERA_BLOB_MAGIC_CCB = 0x00626363,",
        "    OOT3D_SCENE_CUTSCENE_CAMERA_BLOB_MAGIC_CAAD = 0x64616163,",
        "    OOT3D_SCENE_CUTSCENE_CAMERA_BLOB_MAGIC_MADS = 0x7364616D,",
        "    OOT3D_SCENE_CUTSCENE_CAMERA_BLOB_MAGIC_CMAD = 0x64616D63,",
        "};",
        "",
        "typedef struct {",
        "    u16 cameraBlobSourceIndex;",
        "    u16 nativeCommandSourceIndex;",
        "    u16 cutsceneSourceIndex;",
        "    u8 sceneId;",
        "    u16 setupIndex;",
        "    u16 localCommandIndex;",
        "    u32 commandOffset;",
        "    u32 commandBlobSize;",
        "    u32 blobDataOffset;",
        "    u32 blobMagic;",
        "    u32 blobVersion;",
        "    u32 logicalBlobSize;",
        "    u32 tailPaddingSize;",
        "    u32 reservedWord0C;",
        "    u16 segmentCount;",
        "    u32 headerWord14;",
        "    u32 segmentDirectoryEndOffset;",
        "    u16 segmentRefStart;",
        "    u16 segmentRefCount;",
        "    u32 firstSegmentOffset;",
        "    const char* scenePath;",
        "    const char* sceneStem;",
        "    const char* payloadSymbol;",
        "    const char* rawBlobHeaderPrefixHex;",
        "} Oot3dSceneCutsceneCameraBlobRow;",
        "",
        "typedef struct {",
        "    u16 cameraBlobSegmentSourceIndex;",
        "    u16 cameraBlobSourceIndex;",
        "    u16 nativeCommandSourceIndex;",
        "    u16 cutsceneSourceIndex;",
        "    u16 setupIndex;",
        "    u16 localCommandIndex;",
        "    u16 segmentIndex;",
        "    u32 segmentOffset;",
        "    u32 segmentAbsoluteOffset;",
        "    u32 segmentSize;",
        "    u32 nextSegmentOffset;",
        "    u32 segmentMagic;",
        "    u32 segmentWord04;",
        "    s32 startFrame;",
        "    s32 endFrame;",
        "    u32 baseCsParam8CBits;",
        "    u32 baseCsParam90Bits;",
        "    u32 baseCsParam94Bits;",
        "    u32 baseCsParam80Bits;",
        "    u32 baseCsParam84Bits;",
        "    u32 baseCsParam88Bits;",
        "    u32 baseCsParam1A2SourceBits;",
        "    u32 baseCsParam144SourceBits;",
        "    u32 baseCsParamD0Bits;",
        "    u32 madsOffset;",
        "    u32 madsSize;",
        "    u32 madsMagic;",
        "    u32 madsType;",
        "    u32 madsHeaderSize;",
        "    u32 cmadOffset;",
        "    u32 cmadMagic;",
        "    const char* scenePath;",
        "    const char* rawSegmentHeaderPrefixHex;",
        "} Oot3dSceneCutsceneCameraBlobSegmentRow;",
        "",
        "extern const Oot3dSceneCutsceneCameraBlobRow oot3d_scene_cutscene_camera_blob_rows[];",
        "extern const Oot3dSceneCutsceneCameraBlobSegmentRow oot3d_scene_cutscene_camera_blob_segment_rows[];",
        "extern const u32 oot3d_scene_cutscene_camera_blob_row_count;",
        "extern const u32 oot3d_scene_cutscene_camera_blob_segment_row_count;",
        "",
        "#endif",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_source(path: Path, payload: dict[str, Any]) -> None:
    blob_rows = [as_dict(row) for row in as_list(payload.get("camera_blob_rows"))]
    segment_rows = [as_dict(row) for row in as_list(payload.get("camera_blob_segment_rows"))]
    lines = [
        "/* Generated by build_scene_cutscene_camera_blob_table.py. */",
        "",
        '#include "oot3d/scene_cutscene_camera_blob_table.h"',
        "",
        "const Oot3dSceneCutsceneCameraBlobRow oot3d_scene_cutscene_camera_blob_rows[] = {",
    ]
    for row in blob_rows:
        lines.append(
            "    { "
            f"{c_u16(row.get('camera_blob_source_index'))}, "
            f"{c_u16(row.get('native_command_source_index'))}, "
            f"{c_u16(row.get('cutscene_source_index'))}, "
            f"{c_u8(row.get('scene_id'))}, "
            f"{c_u16(row.get('setup_index'))}, "
            f"{c_u16(row.get('local_command_index'))}, "
            f"{c_u32(row.get('command_offset'))}, "
            f"{c_u32(row.get('command_blob_size'))}, "
            f"{c_u32(row.get('blob_data_offset'))}, "
            f"{c_u32(row.get('blob_magic'))}, "
            f"{c_u32(row.get('blob_version'))}, "
            f"{c_u32(row.get('logical_blob_size'))}, "
            f"{c_u32(row.get('tail_padding_size'))}, "
            f"{c_u32(row.get('reserved_word0c'))}, "
            f"{c_u16(row.get('segment_count'))}, "
            f"{c_u32(row.get('header_word14'))}, "
            f"{c_u32(row.get('segment_directory_end_offset'))}, "
            f"{c_u16(row.get('segment_ref_start'))}, "
            f"{c_u16(row.get('segment_ref_count'))}, "
            f"{c_u32(row.get('first_segment_offset'))}, "
            f"{c_string(row.get('scene_path'))}, "
            f"{c_string(row.get('scene_stem'))}, "
            f"{c_string(row.get('payload_symbol'))}, "
            f"{c_string(row.get('raw_blob_header_prefix_hex'))}"
            " },"
        )
    lines.extend(
        [
            "};",
            "",
            "const Oot3dSceneCutsceneCameraBlobSegmentRow oot3d_scene_cutscene_camera_blob_segment_rows[] = {",
        ]
    )
    for row in segment_rows:
        lines.append(
            "    { "
            f"{c_u16(row.get('camera_blob_segment_source_index'))}, "
            f"{c_u16(row.get('camera_blob_source_index'))}, "
            f"{c_u16(row.get('native_command_source_index'))}, "
            f"{c_u16(row.get('cutscene_source_index'))}, "
            f"{c_u16(row.get('setup_index'))}, "
            f"{c_u16(row.get('local_command_index'))}, "
            f"{c_u16(row.get('segment_index'))}, "
            f"{c_u32(row.get('segment_offset'))}, "
            f"{c_u32(row.get('segment_absolute_offset'))}, "
            f"{c_u32(row.get('segment_size'))}, "
            f"{c_u32(row.get('next_segment_offset'))}, "
            f"{c_u32(row.get('segment_magic'))}, "
            f"{c_u32(row.get('segment_word04'))}, "
            f"{c_s32(row.get('start_frame'))}, "
            f"{c_s32(row.get('end_frame'))}, "
            f"{c_u32(row.get('base_csparam_8c_bits'))}, "
            f"{c_u32(row.get('base_csparam_90_bits'))}, "
            f"{c_u32(row.get('base_csparam_94_bits'))}, "
            f"{c_u32(row.get('base_csparam_80_bits'))}, "
            f"{c_u32(row.get('base_csparam_84_bits'))}, "
            f"{c_u32(row.get('base_csparam_88_bits'))}, "
            f"{c_u32(row.get('base_csparam_1a2_source_bits'))}, "
            f"{c_u32(row.get('base_csparam_144_source_bits'))}, "
            f"{c_u32(row.get('base_csparam_d0_bits'))}, "
            f"{c_u32(row.get('mads_offset'))}, "
            f"{c_u32(row.get('mads_size'))}, "
            f"{c_u32(row.get('mads_magic'))}, "
            f"{c_u32(row.get('mads_type'))}, "
            f"{c_u32(row.get('mads_header_size'))}, "
            f"{c_u32(row.get('cmad_offset'))}, "
            f"{c_u32(row.get('cmad_magic'))}, "
            f"{c_string(row.get('scene_path'))}, "
            f"{c_string(row.get('raw_segment_header_prefix_hex'))}"
            " },"
        )
    lines.extend(
        [
            "};",
            "",
            "const u32 oot3d_scene_cutscene_camera_blob_row_count = OOT3D_SCENE_CUTSCENE_CAMERA_BLOB_ROW_COUNT;",
            "const u32 oot3d_scene_cutscene_camera_blob_segment_row_count = OOT3D_SCENE_CUTSCENE_CAMERA_BLOB_SEGMENT_ROW_COUNT;",
            "",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_outputs(payload: dict[str, Any]) -> None:
    write_json(DEFAULT_OUT_JSON, payload)
    write_markdown(DEFAULT_OUT_MD, payload)
    write_csv(DEFAULT_OUT_BLOB_CSV, as_list(payload.get("camera_blob_rows")), BLOB_CSV_COLUMNS)
    write_csv(
        DEFAULT_OUT_SEGMENT_CSV,
        as_list(payload.get("camera_blob_segment_rows")),
        SEGMENT_CSV_COLUMNS,
    )
    write_header(DEFAULT_OUT_HEADER, payload)
    write_source(DEFAULT_OUT_SOURCE, payload)


def validate(payload: dict[str, Any]) -> list[str]:
    summary = as_dict(payload.get("summary"))
    blob_rows = [as_dict(row) for row in as_list(payload.get("camera_blob_rows"))]
    segment_rows = [as_dict(row) for row in as_list(payload.get("camera_blob_segment_rows"))]
    errors: list[str] = []
    if int_value(summary.get("camera_blob_row_count")) < 103:
        errors.append("camera blob row count dropped below baseline of 103")
    if int_value(summary.get("camera_blob_segment_row_count")) < 548:
        errors.append("camera blob segment row count dropped below baseline of 548")
    if int_value(summary.get("intro_target_blob_count")) != 11:
        errors.append("intro target blob count changed from baseline of 11")
    if int_value(summary.get("intro_target_segment_count")) != 47:
        errors.append("intro target segment count changed from baseline of 47")
    if int_value(summary.get("title_intro_spot00_camera_blob_count")) != 6:
        errors.append("title intro spot00 camera blob count should be 6")
    if int_value(summary.get("title_intro_spot00_camera_segment_count")) <= 0:
        errors.append("title intro spot00 camera segments were not decoded")
    for row in blob_rows:
        index = row.get("camera_blob_source_index")
        if int_value(row.get("blob_magic")) != MAGIC_CCB:
            errors.append(f"blob {index}: ccb magic mismatch")
        if int_value(row.get("blob_version")) != 3:
            errors.append(f"blob {index}: unexpected ccb version")
        if int_value(row.get("reserved_word0c")) != 0:
            errors.append(f"blob {index}: reserved word +0x0c is non-zero")
        if int_value(row.get("tail_padding_size")) % 16 != 0:
            errors.append(f"blob {index}: tail padding is not 16-byte aligned")
        if int_value(row.get("first_segment_offset")) != int_value(
            row.get("segment_directory_end_offset")
        ):
            errors.append(f"blob {index}: first segment does not follow directory")
        start = int_value(row.get("segment_ref_start"))
        count = int_value(row.get("segment_ref_count"))
        if start + count > len(segment_rows):
            errors.append(f"blob {index}: segment slice outside table")
        elif any(
            int_value(segment_rows[segment_index].get("camera_blob_source_index"))
            != int_value(index)
            for segment_index in range(start, start + count)
        ):
            errors.append(f"blob {index}: segment slice owner mismatch")
    for row in segment_rows:
        index = row.get("camera_blob_segment_source_index")
        if int_value(row.get("segment_magic")) != MAGIC_CAAD:
            errors.append(f"segment {index}: caad magic mismatch")
        if int_value(row.get("segment_word04")) != int_value(row.get("segment_index")):
            errors.append(f"segment {index}: word +0x04 does not match segment index")
        if int_value(row.get("start_frame")) > int_value(row.get("end_frame")):
            errors.append(f"segment {index}: start frame after end frame")
        if int_value(row.get("mads_offset")) != 0x4C:
            errors.append(f"segment {index}: mads offset changed from +0x4c")
        if int_value(row.get("mads_magic")) != MAGIC_MADS:
            errors.append(f"segment {index}: mads magic mismatch")
        if int_value(row.get("mads_header_size")) != 8 + int_value(row.get("mads_type")) * 4:
            errors.append(f"segment {index}: mads header size formula mismatch")
        if int_value(row.get("cmad_magic")) != MAGIC_CMAD:
            errors.append(f"segment {index}: cmad magic mismatch")
        if int_value(row.get("cmad_offset")) >= int_value(row.get("segment_size")):
            errors.append(f"segment {index}: cmad offset outside segment")
    return errors


def main() -> int:
    payload = build_table()
    errors = validate(payload)
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1
    write_outputs(payload)
    summary = as_dict(payload.get("summary"))
    print(
        "wrote scene cutscene camera blob table: "
        f"{summary.get('camera_blob_row_count')} blobs, "
        f"{summary.get('camera_blob_segment_row_count')} segments"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
