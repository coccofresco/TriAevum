#!/usr/bin/env python3
"""Build source tables for OOT3D cutscene camera `mads/cmad` curve records.

`build_scene_cutscene_camera_blob_table.py` decodes the command `0x97` blob and
its `caad` segment boundaries. This generator lowers the next runtime layer:
the `mads` record directory and the `cmad` channel records consumed by
`FUN_0033cb90`.
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
DEFAULT_CAMERA_BLOB_TABLE = ROOT / "analysis" / "scene_cutscene_camera_blob_table.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_cutscene_camera_cmad_table.json"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_cutscene_camera_cmad_table.md"
DEFAULT_OUT_CMAD_CSV = ROOT / "analysis" / "scene_cutscene_camera_cmad_table.csv"
DEFAULT_OUT_CURVE_CSV = ROOT / "analysis" / "scene_cutscene_camera_cmad_curve_table.csv"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "scene_cutscene_camera_cmad_table.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_scene_cutscene_camera_cmad_table.c"

MAGIC_CMAD = 0x64616D63
CURVE_INTERPOLATION_TYPE = 2
RAW_PREFIX_LIMIT = 0x40


CURVE_SLOT_SPECS: dict[int, list[tuple[int, str, int]]] = {
    1: [
        (0, "csparam_8c", 0x8C),
        (1, "csparam_90", 0x90),
        (2, "csparam_94", 0x94),
    ],
    2: [
        (0, "csparam_80", 0x80),
        (1, "csparam_84", 0x84),
        (2, "csparam_88", 0x88),
    ],
    3: [(0, "csparam_1a2_scaled_s16", 0x1A2)],
    7: [(0, "csparam_144_scaled_float", 0x144)],
    8: [(0, "csparam_d0_float", 0xD0)],
}


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


def read_s16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<h", data, offset)[0]


def read_s32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def require_available(data: bytes, offset: int, size: int, label: str) -> None:
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError(f"{label}: read outside file at 0x{offset:x} size 0x{size:x}")


def raw_prefix_hex(data: bytes, offset: int, size: int) -> str:
    if offset < 0 or offset >= len(data):
        return ""
    return data[offset : offset + min(size, len(data) - offset)].hex()


def source_meta(row: dict[str, Any]) -> dict[str, Any]:
    return {
        key: row[key]
        for key in ("source_kind", "archive_path", "embedded_name", "qdb_index", "qdb_command_index")
        if key in row
    }


def read_zar_embedded(romfs_root: Path, archive_path: str, embedded_name: str, cache: dict[str, ZarArchive]) -> bytes:
    archive = cache.get(archive_path)
    if archive is None:
        archive = ZarArchive.from_path(romfs_root / archive_path)
        cache[archive_path] = archive
    for file in archive.files:
        if file.name == embedded_name:
            return archive.read_file(file)
    raise FileNotFoundError(f"{archive_path}: embedded file not found: {embedded_name}")


def source_cache_key(row: dict[str, Any]) -> str:
    if row.get("source_kind") == "zar_embedded_qdb":
        return f"zar:{row.get('archive_path')}!{row.get('embedded_name')}"
    return f"scene:{row.get('scene_path', '')}"


def load_source_data(
    row: dict[str, Any],
    scene_root: Path,
    romfs_root: Path,
    file_cache: dict[str, bytes],
    zar_cache: dict[str, ZarArchive],
) -> bytes:
    key = source_cache_key(row)
    if key in file_cache:
        return file_cache[key]
    if row.get("source_kind") == "zar_embedded_qdb":
        data = read_zar_embedded(
            romfs_root,
            str(row.get("archive_path", "")),
            str(row.get("embedded_name", "")),
            zar_cache,
        )
    else:
        data = (scene_root / str(row.get("scene_path", ""))).read_bytes()
    file_cache[key] = data
    return data


def decode_segment_cmad_records(
    segment_row: dict[str, Any],
    data: bytes,
    cmad_ref_start: int,
    curve_ref_start: int,
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    scene_path = str(segment_row.get("scene_path", ""))
    segment_index = int_value(segment_row.get("segment_index"))
    segment_absolute_offset = int_value(segment_row.get("segment_absolute_offset"))
    segment_size = int_value(segment_row.get("segment_size"))
    mads_offset = int_value(segment_row.get("mads_offset"))
    mads_absolute_offset = segment_absolute_offset + mads_offset
    mads_size = segment_size - mads_offset
    label = f"{scene_path}:segment{segment_row.get('camera_blob_segment_source_index')}"
    require_available(data, mads_absolute_offset, mads_size, label)
    cmad_count = read_u32(data, mads_absolute_offset + 4)
    require_available(data, mads_absolute_offset + 8, cmad_count * 4, label)
    cmad_offsets = [
        read_u32(data, mads_absolute_offset + 8 + cmad_index * 4)
        for cmad_index in range(cmad_count)
    ]

    cmad_rows: list[dict[str, Any]] = []
    curve_rows: list[dict[str, Any]] = []
    for cmad_index, cmad_offset in enumerate(cmad_offsets):
        next_cmad_offset = (
            cmad_offsets[cmad_index + 1] if cmad_index + 1 < len(cmad_offsets) else mads_size
        )
        cmad_size = next_cmad_offset - cmad_offset
        cmad_absolute_offset = mads_absolute_offset + cmad_offset
        require_available(data, cmad_absolute_offset, max(cmad_size, 0), label)
        require_available(data, cmad_absolute_offset, 0x10, label)
        cmad_magic = read_u32(data, cmad_absolute_offset)
        channel_type = data[cmad_absolute_offset + 4]
        curve_specs = CURVE_SLOT_SPECS.get(channel_type, [])
        curve_start = curve_ref_start + len(curve_rows)
        local_curve_offsets: list[tuple[int, int, str, int]] = []
        for slot_index, curve_role, output_field_offset in curve_specs:
            curve_offset = read_s16(data, cmad_absolute_offset + 8 + slot_index * 2)
            if curve_offset != 0:
                local_curve_offsets.append(
                    (curve_offset, slot_index, curve_role, output_field_offset)
                )
        local_curve_offsets.sort(key=lambda item: item[0])

        for local_curve_index, (
            curve_offset,
            slot_index,
            curve_role,
            output_field_offset,
        ) in enumerate(local_curve_offsets):
            next_curve_offset = (
                local_curve_offsets[local_curve_index + 1][0]
                if local_curve_index + 1 < len(local_curve_offsets)
                else cmad_size
            )
            curve_absolute_offset = cmad_absolute_offset + curve_offset
            curve_size = next_curve_offset - curve_offset
            require_available(data, curve_absolute_offset, max(curve_size, 0), label)
            require_available(data, curve_absolute_offset, 0x10, label)
            curve_rows.append(
                {
                    "camera_curve_source_index": curve_ref_start + len(curve_rows),
                    "cmad_record_source_index": cmad_ref_start + len(cmad_rows),
                    "camera_blob_segment_source_index": int_value(
                        segment_row.get("camera_blob_segment_source_index")
                    ),
                    "camera_blob_source_index": int_value(
                        segment_row.get("camera_blob_source_index")
                    ),
                    "native_command_source_index": int_value(
                        segment_row.get("native_command_source_index")
                    ),
                    "cutscene_source_index": int_value(segment_row.get("cutscene_source_index")),
                    "scene_path": scene_path,
                    "setup_index": int_value(segment_row.get("setup_index")),
                    "local_command_index": int_value(segment_row.get("local_command_index")),
                    "segment_index": segment_index,
                    "cmad_record_index": cmad_index,
                    "channel_type": channel_type,
                    "curve_slot_index": slot_index,
                    "curve_role": curve_role,
                    "output_field_offset": output_field_offset,
                    "output_field_offset_hex": hex_u32(output_field_offset),
                    "curve_offset": curve_offset,
                    "curve_offset_hex": hex_u32(curve_offset),
                    "curve_absolute_offset": curve_absolute_offset,
                    "curve_absolute_offset_hex": hex_u32(curve_absolute_offset),
                    "curve_size": curve_size,
                    "interpolation_type": data[curve_absolute_offset],
                    "point_count": read_s32(data, curve_absolute_offset + 4),
                    "header_word08": read_u32(data, curve_absolute_offset + 8),
                    "header_word08_hex": hex_u32(read_u32(data, curve_absolute_offset + 8)),
                    "header_word0c": read_u32(data, curve_absolute_offset + 0x0C),
                    "header_word0c_hex": hex_u32(read_u32(data, curve_absolute_offset + 0x0C)),
                    "raw_curve_header_prefix_hex": raw_prefix_hex(
                        data,
                        curve_absolute_offset,
                        min(curve_size, RAW_PREFIX_LIMIT),
                    ),
                    **source_meta(segment_row),
                }
            )

        cmad_rows.append(
            {
                "cmad_record_source_index": cmad_ref_start + len(cmad_rows),
                "camera_blob_segment_source_index": int_value(
                    segment_row.get("camera_blob_segment_source_index")
                ),
                "camera_blob_source_index": int_value(segment_row.get("camera_blob_source_index")),
                "native_command_source_index": int_value(
                    segment_row.get("native_command_source_index")
                ),
                "cutscene_source_index": int_value(segment_row.get("cutscene_source_index")),
                "scene_path": scene_path,
                "setup_index": int_value(segment_row.get("setup_index")),
                "local_command_index": int_value(segment_row.get("local_command_index")),
                "segment_index": segment_index,
                "cmad_record_index": cmad_index,
                "mads_record_offset": cmad_offset,
                "mads_record_offset_hex": hex_u32(cmad_offset),
                "cmad_absolute_offset": cmad_absolute_offset,
                "cmad_absolute_offset_hex": hex_u32(cmad_absolute_offset),
                "cmad_record_size": cmad_size,
                "next_cmad_offset": next_cmad_offset,
                "cmad_magic": cmad_magic,
                "cmad_magic_hex": hex_u32(cmad_magic),
                "channel_type": channel_type,
                "curve_ref_start": curve_start,
                "curve_ref_count": len(curve_rows) + curve_ref_start - curve_start,
                "raw_cmad_header_prefix_hex": raw_prefix_hex(
                    data,
                    cmad_absolute_offset,
                    min(cmad_size, RAW_PREFIX_LIMIT),
                ),
                **source_meta(segment_row),
            }
        )
    return cmad_rows, curve_rows


def build_table(
    native_decode_path: Path = DEFAULT_NATIVE_DECODE_TABLE,
    camera_blob_path: Path = DEFAULT_CAMERA_BLOB_TABLE,
) -> dict[str, Any]:
    native_decode = load_json(native_decode_path)
    camera_blob_table = load_json(camera_blob_path)
    scene_root = Path(str(as_dict(native_decode.get("summary")).get("scene_root", "")))
    if not scene_root.is_dir():
        raise FileNotFoundError(f"{scene_root}: scene root from native decode table not found")
    romfs_root = Path(str(as_dict(camera_blob_table.get("summary")).get("romfs_root", "")))
    if not romfs_root.is_dir():
        romfs_root = scene_root.parent

    cmad_rows: list[dict[str, Any]] = []
    curve_rows: list[dict[str, Any]] = []
    file_cache: dict[str, bytes] = {}
    zar_cache: dict[str, ZarArchive] = {}
    for segment_row in as_list(camera_blob_table.get("camera_blob_segment_rows")):
        segment_row = as_dict(segment_row)
        data = load_source_data(segment_row, scene_root, romfs_root, file_cache, zar_cache)
        segment_cmad_rows, segment_curve_rows = decode_segment_cmad_records(
            segment_row,
            data,
            len(cmad_rows),
            len(curve_rows),
        )
        cmad_rows.extend(segment_cmad_rows)
        curve_rows.extend(segment_curve_rows)

    channel_counts = Counter(str(row["channel_type"]) for row in cmad_rows)
    curve_role_counts = Counter(str(row["curve_role"]) for row in curve_rows)
    point_count_counts = Counter(str(row["point_count"]) for row in curve_rows)
    curve_type_counts = Counter(str(row["interpolation_type"]) for row in curve_rows)
    unknown_channel_count = sum(
        1 for row in cmad_rows if int_value(row.get("channel_type")) not in CURVE_SLOT_SPECS
    )
    intro_cmad_rows = [
        row for row in cmad_rows if row.get("scene_path") in {"link_info.zsi", "spot04_info.zsi"}
    ]
    intro_curve_rows = [
        row for row in curve_rows if row.get("scene_path") in {"link_info.zsi", "spot04_info.zsi"}
    ]

    summary = {
        "format": "oot3d_scene_cutscene_camera_cmad_table_v1",
        "source_camera_blob_table": str(camera_blob_path),
        "source_decode_table": str(native_decode_path),
        "scene_root": str(scene_root),
        "romfs_root": str(romfs_root),
        "cmad_record_row_count": len(cmad_rows),
        "camera_curve_row_count": len(curve_rows),
        "title_intro_spot00_cmad_record_count": sum(
            1 for row in cmad_rows if row.get("source_kind") == "zar_embedded_qdb"
        ),
        "title_intro_spot00_camera_curve_count": sum(
            1 for row in curve_rows if row.get("source_kind") == "zar_embedded_qdb"
        ),
        "intro_target_cmad_record_count": len(intro_cmad_rows),
        "intro_target_curve_count": len(intro_curve_rows),
        "unknown_channel_count": unknown_channel_count,
        "channel_type_counts": dict(sorted(channel_counts.items(), key=lambda item: int(item[0]))),
        "curve_role_counts": dict(sorted(curve_role_counts.items())),
        "curve_interpolation_type_counts": dict(
            sorted(curve_type_counts.items(), key=lambda item: int(item[0]))
        ),
        "curve_point_count_counts": dict(
            sorted(point_count_counts.items(), key=lambda item: int(item[0]))
        ),
        "runtime_reference": {
            "evaluator": "FUN_0033cb90 at 0x0033CB90",
            "curve_sampler": "FUN_003087A4 at 0x003087A4",
            "record_directory": "mads + 0x08 + recordIndex * 4",
            "channel_dispatch": "cmad + 0x04",
            "curve_offsets": "signed 16-bit offsets at cmad + 0x08/+0x0A/+0x0C, channel-dependent",
            "frame_input": "current camera/cutscene frame passed as FUN_0033cb90 second argument",
        },
    }
    return {
        "summary": summary,
        "source_policy": {
            "primary_source": "OOT3D command 0x97 camera blobs decoded from native ZSI files",
            "runtime_reference": "code.bin FUN_0033cb90 and FUN_003087A4",
            "strict_n64_policy": "not used for this table",
        },
        "cmad_record_rows": cmad_rows,
        "camera_curve_rows": curve_rows,
    }


CMAD_CSV_COLUMNS = [
    "cmad_record_source_index",
    "camera_blob_segment_source_index",
    "camera_blob_source_index",
    "native_command_source_index",
    "cutscene_source_index",
    "scene_path",
    "setup_index",
    "local_command_index",
    "segment_index",
    "cmad_record_index",
    "mads_record_offset_hex",
    "cmad_absolute_offset_hex",
    "cmad_record_size",
    "next_cmad_offset",
    "cmad_magic_hex",
    "channel_type",
    "curve_ref_start",
    "curve_ref_count",
    "raw_cmad_header_prefix_hex",
]

CURVE_CSV_COLUMNS = [
    "camera_curve_source_index",
    "cmad_record_source_index",
    "camera_blob_segment_source_index",
    "camera_blob_source_index",
    "native_command_source_index",
    "cutscene_source_index",
    "scene_path",
    "setup_index",
    "local_command_index",
    "segment_index",
    "cmad_record_index",
    "channel_type",
    "curve_slot_index",
    "curve_role",
    "output_field_offset_hex",
    "curve_offset_hex",
    "curve_absolute_offset_hex",
    "curve_size",
    "interpolation_type",
    "point_count",
    "header_word08_hex",
    "header_word0c_hex",
    "raw_curve_header_prefix_hex",
]


def write_markdown(path: Path, payload: dict[str, Any]) -> None:
    summary = as_dict(payload.get("summary"))
    lines = [
        "# Scene Cutscene Camera CMAD Table",
        "",
        "Generated source table for the `mads/cmad` records inside OOT3D-native cutscene command `0x97` camera blobs.",
        "",
        "## Summary",
        "",
        f"- `cmad` record rows: {summary.get('cmad_record_row_count')}",
        f"- Camera curve rows: {summary.get('camera_curve_row_count')}",
        f"- Intro target `cmad` rows (`link_info.zsi`, `spot04_info.zsi`): {summary.get('intro_target_cmad_record_count')}",
        f"- Intro target curve rows: {summary.get('intro_target_curve_count')}",
        f"- Unknown channel rows: {summary.get('unknown_channel_count')}",
        f"- Channel types: `{json.dumps(summary.get('channel_type_counts'), sort_keys=True)}`",
        f"- Curve roles: `{json.dumps(summary.get('curve_role_counts'), sort_keys=True)}`",
        f"- Curve interpolation types: `{json.dumps(summary.get('curve_interpolation_type_counts'), sort_keys=True)}`",
        "",
        "## Runtime Mapping",
        "",
        "- `FUN_0033cb90` copies base camera fields from the active `caad` segment, then iterates `mads` records.",
        "- `mads + 0x04` is the `cmad` record count; `mads + 0x08` is the relative record-offset directory.",
        "- `cmad + 0x04` dispatches the channel. Channels `1` and `2` use three s16 curve offsets at `+0x08/+0x0A/+0x0C`; channels `3`, `7`, and `8` use only `+0x08`.",
        "- All decoded curve records currently use interpolation type `2`; their exact interpolation math is in `FUN_003087A4`.",
        "",
        "## Channel Outputs",
        "",
        "| channel | curve roles | target fields in `FUN_0033cb90` |",
        "| ---: | --- | --- |",
        "| 1 | `csparam_8c`, `csparam_90`, `csparam_94` | `param_3 + 0x8c/0x90/0x94` |",
        "| 2 | `csparam_80`, `csparam_84`, `csparam_88` | `param_3 + 0x80/0x84/0x88` |",
        "| 3 | `csparam_1a2_scaled_s16` | `param_3 + 0x1a2` after scale |",
        "| 7 | `csparam_144_scaled_float` | `param_3 + 0x144` after scale |",
        "| 8 | `csparam_d0_float` | `param_3 + 0xd0` |",
        "",
        "## Outputs",
        "",
        "- `include/oot3d/scene_cutscene_camera_cmad_table.h`",
        "- `src/code/z_scene_cutscene_camera_cmad_table.c`",
        "- `analysis/scene_cutscene_camera_cmad_table.json`",
        "- `analysis/scene_cutscene_camera_cmad_table.csv`",
        "- `analysis/scene_cutscene_camera_cmad_curve_table.csv`",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_header(path: Path, payload: dict[str, Any]) -> None:
    cmad_count = len(as_list(payload.get("cmad_record_rows")))
    curve_count = len(as_list(payload.get("camera_curve_rows")))
    lines = [
        "#ifndef OOT3D_SCENE_CUTSCENE_CAMERA_CMAD_TABLE_H",
        "#define OOT3D_SCENE_CUTSCENE_CAMERA_CMAD_TABLE_H",
        "",
        '#include "oot3d/scene.h"',
        '#include "oot3d/scene_cutscene_camera_blob_table.h"',
        "",
        "enum {",
        f"    OOT3D_SCENE_CUTSCENE_CAMERA_CMAD_RECORD_ROW_COUNT = {cmad_count},",
        f"    OOT3D_SCENE_CUTSCENE_CAMERA_CURVE_ROW_COUNT = {curve_count},",
        "    OOT3D_SCENE_CUTSCENE_CAMERA_CMAD_MAGIC = 0x64616D63,",
        "};",
        "",
        "typedef struct {",
        "    u16 cmadRecordSourceIndex;",
        "    u16 cameraBlobSegmentSourceIndex;",
        "    u16 cameraBlobSourceIndex;",
        "    u16 nativeCommandSourceIndex;",
        "    u16 cutsceneSourceIndex;",
        "    u16 setupIndex;",
        "    u16 localCommandIndex;",
        "    u16 segmentIndex;",
        "    u16 cmadRecordIndex;",
        "    u32 madsRecordOffset;",
        "    u32 cmadAbsoluteOffset;",
        "    u32 cmadRecordSize;",
        "    u32 nextCmadOffset;",
        "    u32 cmadMagic;",
        "    u8 channelType;",
        "    u16 curveRefStart;",
        "    u16 curveRefCount;",
        "    const char* scenePath;",
        "    const char* rawCmadHeaderPrefixHex;",
        "} Oot3dSceneCutsceneCameraCmadRecordRow;",
        "",
        "typedef struct {",
        "    u16 cameraCurveSourceIndex;",
        "    u16 cmadRecordSourceIndex;",
        "    u16 cameraBlobSegmentSourceIndex;",
        "    u16 cameraBlobSourceIndex;",
        "    u16 nativeCommandSourceIndex;",
        "    u16 cutsceneSourceIndex;",
        "    u16 setupIndex;",
        "    u16 localCommandIndex;",
        "    u16 segmentIndex;",
        "    u16 cmadRecordIndex;",
        "    u8 channelType;",
        "    u8 curveSlotIndex;",
        "    u32 outputFieldOffset;",
        "    s32 curveOffset;",
        "    u32 curveAbsoluteOffset;",
        "    u32 curveSize;",
        "    u8 interpolationType;",
        "    u16 pointCount;",
        "    u32 headerWord08;",
        "    u32 headerWord0C;",
        "    const char* curveRole;",
        "    const char* scenePath;",
        "    const char* rawCurveHeaderPrefixHex;",
        "} Oot3dSceneCutsceneCameraCurveRow;",
        "",
        "extern const Oot3dSceneCutsceneCameraCmadRecordRow oot3d_scene_cutscene_camera_cmad_record_rows[];",
        "extern const Oot3dSceneCutsceneCameraCurveRow oot3d_scene_cutscene_camera_curve_rows[];",
        "extern const u32 oot3d_scene_cutscene_camera_cmad_record_row_count;",
        "extern const u32 oot3d_scene_cutscene_camera_curve_row_count;",
        "",
        "#endif",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_source(path: Path, payload: dict[str, Any]) -> None:
    cmad_rows = [as_dict(row) for row in as_list(payload.get("cmad_record_rows"))]
    curve_rows = [as_dict(row) for row in as_list(payload.get("camera_curve_rows"))]
    lines = [
        "/* Generated by build_scene_cutscene_camera_cmad_table.py. */",
        "",
        '#include "oot3d/scene_cutscene_camera_cmad_table.h"',
        "",
        "const Oot3dSceneCutsceneCameraCmadRecordRow oot3d_scene_cutscene_camera_cmad_record_rows[] = {",
    ]
    for row in cmad_rows:
        lines.append(
            "    { "
            f"{c_u16(row.get('cmad_record_source_index'))}, "
            f"{c_u16(row.get('camera_blob_segment_source_index'))}, "
            f"{c_u16(row.get('camera_blob_source_index'))}, "
            f"{c_u16(row.get('native_command_source_index'))}, "
            f"{c_u16(row.get('cutscene_source_index'))}, "
            f"{c_u16(row.get('setup_index'))}, "
            f"{c_u16(row.get('local_command_index'))}, "
            f"{c_u16(row.get('segment_index'))}, "
            f"{c_u16(row.get('cmad_record_index'))}, "
            f"{c_u32(row.get('mads_record_offset'))}, "
            f"{c_u32(row.get('cmad_absolute_offset'))}, "
            f"{c_u32(row.get('cmad_record_size'))}, "
            f"{c_u32(row.get('next_cmad_offset'))}, "
            f"{c_u32(row.get('cmad_magic'))}, "
            f"{c_u8(row.get('channel_type'))}, "
            f"{c_u16(row.get('curve_ref_start'))}, "
            f"{c_u16(row.get('curve_ref_count'))}, "
            f"{c_string(row.get('scene_path'))}, "
            f"{c_string(row.get('raw_cmad_header_prefix_hex'))}"
            " },"
        )
    lines.extend(
        [
            "};",
            "",
            "const Oot3dSceneCutsceneCameraCurveRow oot3d_scene_cutscene_camera_curve_rows[] = {",
        ]
    )
    for row in curve_rows:
        lines.append(
            "    { "
            f"{c_u16(row.get('camera_curve_source_index'))}, "
            f"{c_u16(row.get('cmad_record_source_index'))}, "
            f"{c_u16(row.get('camera_blob_segment_source_index'))}, "
            f"{c_u16(row.get('camera_blob_source_index'))}, "
            f"{c_u16(row.get('native_command_source_index'))}, "
            f"{c_u16(row.get('cutscene_source_index'))}, "
            f"{c_u16(row.get('setup_index'))}, "
            f"{c_u16(row.get('local_command_index'))}, "
            f"{c_u16(row.get('segment_index'))}, "
            f"{c_u16(row.get('cmad_record_index'))}, "
            f"{c_u8(row.get('channel_type'))}, "
            f"{c_u8(row.get('curve_slot_index'))}, "
            f"{c_u32(row.get('output_field_offset'))}, "
            f"{c_s32(row.get('curve_offset'))}, "
            f"{c_u32(row.get('curve_absolute_offset'))}, "
            f"{c_u32(row.get('curve_size'))}, "
            f"{c_u8(row.get('interpolation_type'))}, "
            f"{c_u16(row.get('point_count'))}, "
            f"{c_u32(row.get('header_word08'))}, "
            f"{c_u32(row.get('header_word0c'))}, "
            f"{c_string(row.get('curve_role'))}, "
            f"{c_string(row.get('scene_path'))}, "
            f"{c_string(row.get('raw_curve_header_prefix_hex'))}"
            " },"
        )
    lines.extend(
        [
            "};",
            "",
            "const u32 oot3d_scene_cutscene_camera_cmad_record_row_count = OOT3D_SCENE_CUTSCENE_CAMERA_CMAD_RECORD_ROW_COUNT;",
            "const u32 oot3d_scene_cutscene_camera_curve_row_count = OOT3D_SCENE_CUTSCENE_CAMERA_CURVE_ROW_COUNT;",
            "",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_outputs(payload: dict[str, Any]) -> None:
    write_json(DEFAULT_OUT_JSON, payload)
    write_markdown(DEFAULT_OUT_MD, payload)
    write_csv(DEFAULT_OUT_CMAD_CSV, as_list(payload.get("cmad_record_rows")), CMAD_CSV_COLUMNS)
    write_csv(
        DEFAULT_OUT_CURVE_CSV,
        as_list(payload.get("camera_curve_rows")),
        CURVE_CSV_COLUMNS,
    )
    write_header(DEFAULT_OUT_HEADER, payload)
    write_source(DEFAULT_OUT_SOURCE, payload)


def validate(payload: dict[str, Any]) -> list[str]:
    summary = as_dict(payload.get("summary"))
    cmad_rows = [as_dict(row) for row in as_list(payload.get("cmad_record_rows"))]
    curve_rows = [as_dict(row) for row in as_list(payload.get("camera_curve_rows"))]
    errors: list[str] = []
    if int_value(summary.get("cmad_record_row_count")) < 1328:
        errors.append("cmad record row count dropped below baseline of 1328")
    if int_value(summary.get("camera_curve_row_count")) < 3457:
        errors.append("camera curve row count dropped below baseline of 3457")
    if int_value(summary.get("unknown_channel_count")) != 1:
        errors.append("unknown channel count changed from baseline of 1")
    if int_value(summary.get("title_intro_spot00_cmad_record_count")) <= 0:
        errors.append("title intro spot00 cmad records were not decoded")
    if int_value(summary.get("title_intro_spot00_camera_curve_count")) <= 0:
        errors.append("title intro spot00 camera curves were not decoded")
    for row in cmad_rows:
        index = row.get("cmad_record_source_index")
        if int_value(row.get("cmad_magic")) != MAGIC_CMAD:
            errors.append(f"cmad row {index}: cmad magic mismatch")
        if int_value(row.get("cmad_record_size")) <= 0:
            errors.append(f"cmad row {index}: non-positive size")
        start = int_value(row.get("curve_ref_start"))
        count = int_value(row.get("curve_ref_count"))
        if start + count > len(curve_rows):
            errors.append(f"cmad row {index}: curve slice outside table")
        elif any(
            int_value(curve_rows[curve_index].get("cmad_record_source_index")) != int_value(index)
            for curve_index in range(start, start + count)
        ):
            errors.append(f"cmad row {index}: curve slice owner mismatch")
    for row in curve_rows:
        index = row.get("camera_curve_source_index")
        if int_value(row.get("interpolation_type")) != CURVE_INTERPOLATION_TYPE:
            errors.append(f"curve row {index}: interpolation type changed from 2")
        if int_value(row.get("point_count")) <= 0:
            errors.append(f"curve row {index}: non-positive point count")
        if int_value(row.get("curve_size")) <= 0:
            errors.append(f"curve row {index}: non-positive size")
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
        "wrote scene cutscene camera cmad table: "
        f"{summary.get('cmad_record_row_count')} records, "
        f"{summary.get('camera_curve_row_count')} curves"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
