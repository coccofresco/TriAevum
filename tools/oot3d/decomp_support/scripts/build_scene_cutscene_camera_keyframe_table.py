#!/usr/bin/env python3
"""Build source tables for OOT3D cutscene camera curve keyframes.

The `mads/cmad` generator identifies camera curve records inside native
command `0x97` blobs. This generator lowers interpolation type 2 curves to the
actual 0x10-byte point records consumed by `FUN_003087A4`, and records any
native `strt` string tables that follow the sampled points in the asset data.
"""

from __future__ import annotations

import csv
import json
import re
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

DEFAULT_CMAD_TABLE = ROOT / "analysis" / "scene_cutscene_camera_cmad_table.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_cutscene_camera_keyframe_table.json"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_cutscene_camera_keyframe_table.md"
DEFAULT_OUT_KEYFRAME_CSV = ROOT / "analysis" / "scene_cutscene_camera_keyframe_table.csv"
DEFAULT_OUT_STRT_CSV = ROOT / "analysis" / "scene_cutscene_camera_strt_table.csv"
DEFAULT_OUT_STRT_LABEL_CSV = ROOT / "analysis" / "scene_cutscene_camera_strt_label_table.csv"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "scene_cutscene_camera_keyframe_table.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_scene_cutscene_camera_keyframe_table.c"

INTERPOLATION_TYPE_HERMITE = 2
KEYFRAME_RECORD_SIZE = 0x10
CURVE_HEADER_SIZE = 0x10
MAGIC_STRT_BYTES = b"strt"
MAGIC_STRT_U32 = 0x74727473
LABEL_RE = re.compile(r"^C(?P<camera>[0-9]+)_(?P<start>[0-9]+)_(?P<end>[0-9]+)$")


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


def read_s32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def read_u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def read_f32(data: bytes, offset: int) -> float:
    return struct.unpack_from("<f", data, offset)[0]


def require_available(data: bytes, offset: int, size: int, label: str) -> None:
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError(f"{label}: read outside file at 0x{offset:x} size 0x{size:x}")


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


def align4(value: int) -> int:
    return (value + 3) & ~3


def decode_strt(
    data: bytes,
    offset: int,
    curve_row: dict[str, Any],
    cmad_row: dict[str, Any],
    strt_source_index: int,
    label_ref_start: int,
) -> tuple[dict[str, Any] | None, list[dict[str, Any]]]:
    if offset + 12 > len(data) or data[offset : offset + 4] != MAGIC_STRT_BYTES:
        return None, []

    label_count = read_u32(data, offset + 4)
    if label_count < 1 or label_count > 0x100:
        raise ValueError(f"curve {curve_row.get('camera_curve_source_index')}: invalid strt label count")
    require_available(data, offset + 8, label_count * 4, "strt label offsets")

    label_offsets = [read_u32(data, offset + 8 + label_index * 4) for label_index in range(label_count)]
    if label_offsets[0] != 0:
        raise ValueError(f"curve {curve_row.get('camera_curve_source_index')}: strt label offset[0] is not zero")
    if any(label_offsets[index] >= label_offsets[index + 1] for index in range(label_count - 1)):
        raise ValueError(f"curve {curve_row.get('camera_curve_source_index')}: strt label offsets are not sorted")

    string_data_offset = offset + 8 + label_count * 4
    final_label_start = string_data_offset + label_offsets[-1]
    final_label_end = data.find(b"\0", final_label_start)
    if final_label_end < 0:
        raise ValueError(f"curve {curve_row.get('camera_curve_source_index')}: unterminated final strt label")
    string_data_size = final_label_end + 1 - string_data_offset
    strt_size = 8 + label_count * 4 + align4(string_data_size)
    require_available(data, offset, strt_size, "strt payload")

    curve_declared_end_offset = int_value(curve_row.get("curve_absolute_offset")) + int_value(curve_row.get("curve_size"))
    cmad_declared_end_offset = int_value(cmad_row.get("cmad_absolute_offset")) + int_value(cmad_row.get("cmad_record_size"))
    label_rows: list[dict[str, Any]] = []
    for label_index in range(label_count):
        label_start = label_offsets[label_index]
        if label_index + 1 < label_count:
            label_end = label_offsets[label_index + 1]
        else:
            label_end = final_label_end + 1 - string_data_offset
        raw_label = data[string_data_offset + label_start : string_data_offset + label_end]
        if not raw_label or raw_label[-1] != 0:
            raise ValueError(f"curve {curve_row.get('camera_curve_source_index')}: unterminated strt label")
        label_text = raw_label[:-1].decode("ascii")
        parsed = LABEL_RE.match(label_text)
        label_rows.append(
            {
                "strt_label_source_index": label_ref_start + len(label_rows),
                "strt_source_index": strt_source_index,
                "camera_curve_source_index": int_value(curve_row.get("camera_curve_source_index")),
                "label_index": label_index,
                "label_start": label_start,
                "label_end": label_end,
                "label": label_text,
                "parsed_label": bool(parsed),
                "parsed_camera_index": int(parsed.group("camera")) if parsed else -1,
                "parsed_start_frame": int(parsed.group("start")) if parsed else -1,
                "parsed_end_frame": int(parsed.group("end")) if parsed else -1,
                **source_meta(curve_row),
            }
        )

    strt_row = {
        "strt_source_index": strt_source_index,
        "camera_curve_source_index": int_value(curve_row.get("camera_curve_source_index")),
        "cmad_record_source_index": int_value(curve_row.get("cmad_record_source_index")),
        "camera_blob_segment_source_index": int_value(curve_row.get("camera_blob_segment_source_index")),
        "camera_blob_source_index": int_value(curve_row.get("camera_blob_source_index")),
        "native_command_source_index": int_value(curve_row.get("native_command_source_index")),
        "cutscene_source_index": int_value(curve_row.get("cutscene_source_index")),
        "scene_path": str(curve_row.get("scene_path", "")),
        "setup_index": int_value(curve_row.get("setup_index")),
        "local_command_index": int_value(curve_row.get("local_command_index")),
        "segment_index": int_value(curve_row.get("segment_index")),
        "cmad_record_index": int_value(curve_row.get("cmad_record_index")),
        "channel_type": int_value(curve_row.get("channel_type")),
        "curve_slot_index": int_value(curve_row.get("curve_slot_index")),
        "curve_role": str(curve_row.get("curve_role", "")),
        "strt_absolute_offset": offset,
        "strt_absolute_offset_hex": hex_u32(offset),
        "strt_magic": MAGIC_STRT_U32,
        "strt_magic_hex": hex_u32(MAGIC_STRT_U32),
        "label_count": label_count,
        "label_ref_start": label_ref_start,
        "label_ref_count": len(label_rows),
        "strt_size": strt_size,
        "curve_declared_end_offset": curve_declared_end_offset,
        "curve_declared_end_offset_hex": hex_u32(curve_declared_end_offset),
        "cmad_declared_end_offset": cmad_declared_end_offset,
        "cmad_declared_end_offset_hex": hex_u32(cmad_declared_end_offset),
        "extends_past_curve_size": offset + strt_size > curve_declared_end_offset,
        "extends_past_cmad_size": offset + strt_size > cmad_declared_end_offset,
        "raw_strt_hex": data[offset : offset + strt_size].hex(),
        **source_meta(curve_row),
    }
    return strt_row, label_rows


def build_table(cmad_table_path: Path = DEFAULT_CMAD_TABLE) -> dict[str, Any]:
    cmad_table = load_json(cmad_table_path)
    summary = as_dict(cmad_table.get("summary"))
    scene_root = Path(str(summary.get("scene_root", "")))
    if not scene_root.is_dir():
        raise FileNotFoundError(f"{scene_root}: scene root from cmad table not found")

    cmad_rows = [as_dict(row) for row in as_list(cmad_table.get("cmad_record_rows"))]
    curve_rows = [as_dict(row) for row in as_list(cmad_table.get("camera_curve_rows"))]
    keyframe_rows: list[dict[str, Any]] = []
    strt_rows: list[dict[str, Any]] = []
    strt_label_rows: list[dict[str, Any]] = []
    file_cache: dict[str, bytes] = {}
    zar_cache: dict[str, ZarArchive] = {}
    romfs_root = Path(str(summary.get("romfs_root", "")))
    if not romfs_root.is_dir():
        romfs_root = scene_root.parent
    point_count_counts: Counter[str] = Counter()
    keyframe_role_counts: Counter[str] = Counter()
    strt_label_count_counts: Counter[str] = Counter()
    min_frame: int | None = None
    max_frame: int | None = None
    max_point_count = 0

    curve_keyframe_ref_rows: list[dict[str, Any]] = []
    for curve_row in curve_rows:
        curve_index = int_value(curve_row.get("camera_curve_source_index"))
        scene_path = str(curve_row.get("scene_path", ""))
        data = load_source_data(curve_row, scene_root, romfs_root, file_cache, zar_cache)
        curve_offset = int_value(curve_row.get("curve_absolute_offset"))
        curve_size = int_value(curve_row.get("curve_size"))
        point_count = int_value(curve_row.get("point_count"))
        interpolation_type = int_value(curve_row.get("interpolation_type"))
        label = f"{scene_path}:curve{curve_index}"
        require_available(data, curve_offset, CURVE_HEADER_SIZE, label)
        if interpolation_type != INTERPOLATION_TYPE_HERMITE:
            raise ValueError(f"{label}: unsupported interpolation type {interpolation_type}")
        require_available(data, curve_offset + CURVE_HEADER_SIZE, point_count * KEYFRAME_RECORD_SIZE, label)
        max_point_count = max(max_point_count, point_count)
        point_count_counts[str(point_count)] += 1

        previous_frame: int | None = None
        keyframe_ref_start = len(keyframe_rows)
        for point_index in range(point_count):
            point_offset = curve_offset + CURVE_HEADER_SIZE + point_index * KEYFRAME_RECORD_SIZE
            frame = read_s32(data, point_offset)
            if previous_frame is not None and frame < previous_frame:
                raise ValueError(f"{label}: keyframe frames are not sorted")
            previous_frame = frame
            min_frame = frame if min_frame is None else min(min_frame, frame)
            max_frame = frame if max_frame is None else max(max_frame, frame)
            value_bits = read_u32(data, point_offset + 4)
            tangent_in_bits = read_u32(data, point_offset + 8)
            tangent_out_bits = read_u32(data, point_offset + 0x0C)
            keyframe_rows.append(
                {
                    "keyframe_source_index": len(keyframe_rows),
                    "camera_curve_source_index": curve_index,
                    "cmad_record_source_index": int_value(curve_row.get("cmad_record_source_index")),
                    "camera_blob_segment_source_index": int_value(curve_row.get("camera_blob_segment_source_index")),
                    "camera_blob_source_index": int_value(curve_row.get("camera_blob_source_index")),
                    "native_command_source_index": int_value(curve_row.get("native_command_source_index")),
                    "cutscene_source_index": int_value(curve_row.get("cutscene_source_index")),
                    "scene_path": scene_path,
                    "setup_index": int_value(curve_row.get("setup_index")),
                    "local_command_index": int_value(curve_row.get("local_command_index")),
                    "segment_index": int_value(curve_row.get("segment_index")),
                    "cmad_record_index": int_value(curve_row.get("cmad_record_index")),
                    "channel_type": int_value(curve_row.get("channel_type")),
                    "curve_slot_index": int_value(curve_row.get("curve_slot_index")),
                    "curve_role": str(curve_row.get("curve_role", "")),
                    "point_index": point_index,
                    "keyframe_absolute_offset": point_offset,
                    "keyframe_absolute_offset_hex": hex_u32(point_offset),
                    "frame": frame,
                    "value_bits": value_bits,
                    "value_bits_hex": hex_u32(value_bits),
                    "value_float": read_f32(data, point_offset + 4),
                    "tangent_in_bits": tangent_in_bits,
                    "tangent_in_bits_hex": hex_u32(tangent_in_bits),
                    "tangent_in_float": read_f32(data, point_offset + 8),
                    "tangent_out_bits": tangent_out_bits,
                    "tangent_out_bits_hex": hex_u32(tangent_out_bits),
                    "tangent_out_float": read_f32(data, point_offset + 0x0C),
                    **source_meta(curve_row),
                }
            )
            keyframe_role_counts[str(curve_row.get("curve_role", ""))] += 1
        curve_row["keyframe_ref_start"] = keyframe_ref_start
        curve_row["keyframe_ref_count"] = point_count
        curve_keyframe_ref_rows.append(
            {
                "camera_curve_source_index": curve_index,
                "keyframe_ref_start": keyframe_ref_start,
                "keyframe_ref_count": point_count,
                **source_meta(curve_row),
            }
        )

        strt_offset = curve_offset + CURVE_HEADER_SIZE + point_count * KEYFRAME_RECORD_SIZE
        cmad_row = cmad_rows[int_value(curve_row.get("cmad_record_source_index"))]
        strt_row, label_rows = decode_strt(
            data,
            strt_offset,
            curve_row,
            cmad_row,
            len(strt_rows),
            len(strt_label_rows),
        )
        if strt_row is not None:
            strt_rows.append(strt_row)
            strt_label_rows.extend(label_rows)
            strt_label_count_counts[str(len(label_rows))] += 1

    intro_scene_names = {"link_info.zsi", "spot04_info.zsi"}
    intro_keyframes = [row for row in keyframe_rows if row.get("scene_path") in intro_scene_names]
    intro_strt_rows = [row for row in strt_rows if row.get("scene_path") in intro_scene_names]
    summary_out = {
        "format": "oot3d_scene_cutscene_camera_keyframe_table_v1",
        "source_cmad_table": str(cmad_table_path),
        "scene_root": str(scene_root),
        "romfs_root": str(romfs_root),
        "camera_curve_row_count": len(curve_rows),
        "keyframe_row_count": len(keyframe_rows),
        "strt_row_count": len(strt_rows),
        "strt_label_row_count": len(strt_label_rows),
        "title_intro_spot00_keyframe_count": sum(
            1 for row in keyframe_rows if row.get("source_kind") == "zar_embedded_qdb"
        ),
        "title_intro_spot00_strt_count": sum(
            1 for row in strt_rows if row.get("source_kind") == "zar_embedded_qdb"
        ),
        "intro_target_keyframe_count": len(intro_keyframes),
        "intro_target_strt_count": len(intro_strt_rows),
        "min_frame": min_frame,
        "max_frame": max_frame,
        "max_point_count": max_point_count,
        "curve_point_count_counts": dict(sorted(point_count_counts.items(), key=lambda item: int(item[0]))),
        "keyframe_role_counts": dict(sorted(keyframe_role_counts.items())),
        "strt_label_count_counts": dict(sorted(strt_label_count_counts.items(), key=lambda item: int(item[0]))),
        "strt_extends_past_curve_size_count": sum(1 for row in strt_rows if row.get("extends_past_curve_size")),
        "strt_extends_past_cmad_size_count": sum(1 for row in strt_rows if row.get("extends_past_cmad_size")),
        "runtime_reference": {
            "sampler": "FUN_003087A4 at 0x003087A4",
            "type2_point_stride": "0x10 bytes",
            "point_frame": "s32 at point + 0x00, compared against input frame",
            "point_value": "float32 bits at point + 0x04",
            "point_tangent_in": "float32 bits at point + 0x08, used as next-point tangent in cubic branch",
            "point_tangent_out": "float32 bits at point + 0x0C, used as previous-point tangent in cubic branch",
            "literal_pool": {
                "code_bin_base": "VA = file offset + 0x00100000",
                "0x00308A68": "0.0",
                "0x00308A6C": "1.0",
                "0x00308A70": "2.0",
                "0x00308A74": "3.0",
            },
            "native_formula_notes": [
                "single-point curves return the first point value",
                "frames after the final point return the final point value",
                "interior samples use native cubic interpolation over adjacent 0x10-byte point records",
                "the loop/wrap branch is controlled by the sampler state byte at param + 0x04, not by the asset table itself",
            ],
        },
    }
    return {
        "summary": summary_out,
        "source_policy": {
            "primary_source": "OOT3D command 0x97 camera blobs decoded from native ZSI files",
            "runtime_reference": "code.bin FUN_003087A4",
            "strict_n64_policy": "not used for this table",
        },
        "keyframe_rows": keyframe_rows,
        "curve_keyframe_ref_rows": curve_keyframe_ref_rows,
        "strt_rows": strt_rows,
        "strt_label_rows": strt_label_rows,
    }


KEYFRAME_CSV_COLUMNS = [
    "keyframe_source_index",
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
    "point_index",
    "keyframe_absolute_offset_hex",
    "frame",
    "value_bits_hex",
    "value_float",
    "tangent_in_bits_hex",
    "tangent_in_float",
    "tangent_out_bits_hex",
    "tangent_out_float",
]

STRT_CSV_COLUMNS = [
    "strt_source_index",
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
    "strt_absolute_offset_hex",
    "label_count",
    "label_ref_start",
    "label_ref_count",
    "strt_size",
    "curve_declared_end_offset_hex",
    "cmad_declared_end_offset_hex",
    "extends_past_curve_size",
    "extends_past_cmad_size",
    "raw_strt_hex",
]

STRT_LABEL_CSV_COLUMNS = [
    "strt_label_source_index",
    "strt_source_index",
    "camera_curve_source_index",
    "label_index",
    "label_start",
    "label_end",
    "label",
    "parsed_label",
    "parsed_camera_index",
    "parsed_start_frame",
    "parsed_end_frame",
]


def write_markdown(path: Path, payload: dict[str, Any]) -> None:
    summary = as_dict(payload.get("summary"))
    lines = [
        "# Scene Cutscene Camera Keyframe Table",
        "",
        "Generated source table for interpolation type `2` camera curves inside OOT3D-native cutscene command `0x97` camera blobs.",
        "",
        "## Summary",
        "",
        f"- Camera curve rows: {summary.get('camera_curve_row_count')}",
        f"- Keyframe rows: {summary.get('keyframe_row_count')}",
        f"- Intro target keyframes (`link_info.zsi`, `spot04_info.zsi`): {summary.get('intro_target_keyframe_count')}",
        f"- Native `strt` rows after keyframe data: {summary.get('strt_row_count')}",
        f"- Native `strt` labels: {summary.get('strt_label_row_count')}",
        f"- Intro target `strt` rows: {summary.get('intro_target_strt_count')}",
        f"- Frame range: `{summary.get('min_frame')}` to `{summary.get('max_frame')}`",
        f"- Max points in one curve: {summary.get('max_point_count')}",
        f"- `strt` extending past declared curve size: {summary.get('strt_extends_past_curve_size_count')}",
        f"- `strt` extending past declared cmad size: {summary.get('strt_extends_past_cmad_size_count')}",
        "",
        "## Runtime Mapping",
        "",
        "- `FUN_003087A4` samples interpolation type `2` curves from 0x10-byte point records.",
        "- Point `+0x00` is the signed frame key used by the runtime search.",
        "- Point `+0x04` is the float32 value bits returned directly for one-point/final-point cases.",
        "- Point `+0x08` and `+0x0C` are float32 cubic tangent terms used as next-point and previous-point tangents in the native interior branch.",
        "- The sampler literal pool at `0x00308A68..0x00308A74` is `0.0, 1.0, 2.0, 3.0` with `code.bin` base `0x00100000`.",
        f"- Some curves are followed by native `strt` string tables. These are decoded as independent native records because {summary.get('strt_extends_past_cmad_size_count')} of them extend past the curve/cmad size inferred from `cmad` offsets.",
        "",
        "## Outputs",
        "",
        "- `include/oot3d/scene_cutscene_camera_keyframe_table.h`",
        "- `src/code/z_scene_cutscene_camera_keyframe_table.c`",
        "- `analysis/scene_cutscene_camera_keyframe_table.json`",
        "- `analysis/scene_cutscene_camera_keyframe_table.csv`",
        "- `analysis/scene_cutscene_camera_strt_table.csv`",
        "- `analysis/scene_cutscene_camera_strt_label_table.csv`",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_header(path: Path, payload: dict[str, Any]) -> None:
    keyframe_count = len(as_list(payload.get("keyframe_rows")))
    strt_count = len(as_list(payload.get("strt_rows")))
    label_count = len(as_list(payload.get("strt_label_rows")))
    lines = [
        "#ifndef OOT3D_SCENE_CUTSCENE_CAMERA_KEYFRAME_TABLE_H",
        "#define OOT3D_SCENE_CUTSCENE_CAMERA_KEYFRAME_TABLE_H",
        "",
        '#include "oot3d/scene_cutscene_camera_cmad_table.h"',
        "",
        "enum {",
        f"    OOT3D_SCENE_CUTSCENE_CAMERA_KEYFRAME_ROW_COUNT = {keyframe_count},",
        "    OOT3D_SCENE_CUTSCENE_CAMERA_CURVE_KEYFRAME_REF_ROW_COUNT = OOT3D_SCENE_CUTSCENE_CAMERA_CURVE_ROW_COUNT,",
        f"    OOT3D_SCENE_CUTSCENE_CAMERA_STRT_ROW_COUNT = {strt_count},",
        f"    OOT3D_SCENE_CUTSCENE_CAMERA_STRT_LABEL_ROW_COUNT = {label_count},",
        "    OOT3D_SCENE_CUTSCENE_CAMERA_KEYFRAME_RECORD_SIZE = 0x10,",
        "    OOT3D_SCENE_CUTSCENE_CAMERA_CURVE_TYPE_HERMITE = 2,",
        "    OOT3D_SCENE_CUTSCENE_CAMERA_STRT_MAGIC = 0x74727473,",
        "};",
        "",
        "typedef struct {",
        "    u16 keyframeSourceIndex;",
        "    u16 cameraCurveSourceIndex;",
        "    u16 pointIndex;",
        "    u32 keyframeAbsoluteOffset;",
        "    s32 frame;",
        "    u32 valueBits;",
        "    u32 tangentInBits;",
        "    u32 tangentOutBits;",
        "} Oot3dSceneCutsceneCameraKeyframeRow;",
        "",
        "typedef struct {",
        "    u16 strtSourceIndex;",
        "    u16 cameraCurveSourceIndex;",
        "    u16 cmadRecordSourceIndex;",
        "    u16 labelRefStart;",
        "    u16 labelRefCount;",
        "    u32 strtAbsoluteOffset;",
        "    u32 strtSize;",
        "    u32 curveDeclaredEndOffset;",
        "    u32 cmadDeclaredEndOffset;",
        "    u8 extendsPastCurveSize;",
        "    u8 extendsPastCmadSize;",
        "    const char* scenePath;",
        "    const char* curveRole;",
        "    const char* rawStrtHex;",
        "} Oot3dSceneCutsceneCameraStrtRow;",
        "",
        "typedef struct {",
        "    u16 strtLabelSourceIndex;",
        "    u16 strtSourceIndex;",
        "    u16 cameraCurveSourceIndex;",
        "    u16 labelIndex;",
        "    u32 labelStart;",
        "    u32 labelEnd;",
        "    u8 parsedLabel;",
        "    u16 parsedCameraIndex;",
        "    s32 parsedStartFrame;",
        "    s32 parsedEndFrame;",
        "    const char* label;",
        "} Oot3dSceneCutsceneCameraStrtLabelRow;",
        "",
        "extern const Oot3dSceneCutsceneCameraKeyframeRow oot3d_scene_cutscene_camera_keyframe_rows[];",
        "extern const u32 oot3d_scene_cutscene_camera_curve_keyframe_ref_start[];",
        "extern const u16 oot3d_scene_cutscene_camera_curve_keyframe_ref_count[];",
        "extern const Oot3dSceneCutsceneCameraStrtRow oot3d_scene_cutscene_camera_strt_rows[];",
        "extern const Oot3dSceneCutsceneCameraStrtLabelRow oot3d_scene_cutscene_camera_strt_label_rows[];",
        "extern const u32 oot3d_scene_cutscene_camera_keyframe_row_count;",
        "extern const u32 oot3d_scene_cutscene_camera_strt_row_count;",
        "extern const u32 oot3d_scene_cutscene_camera_strt_label_row_count;",
        "",
        "#endif",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_source(path: Path, payload: dict[str, Any]) -> None:
    keyframe_rows = [as_dict(row) for row in as_list(payload.get("keyframe_rows"))]
    curve_keyframe_ref_rows = [as_dict(row) for row in as_list(payload.get("curve_keyframe_ref_rows"))]
    strt_rows = [as_dict(row) for row in as_list(payload.get("strt_rows"))]
    label_rows = [as_dict(row) for row in as_list(payload.get("strt_label_rows"))]
    lines = [
        "/* Generated by build_scene_cutscene_camera_keyframe_table.py. */",
        "",
        '#include "oot3d/scene_cutscene_camera_keyframe_table.h"',
        "",
        "const Oot3dSceneCutsceneCameraKeyframeRow oot3d_scene_cutscene_camera_keyframe_rows[] = {",
    ]
    for row in keyframe_rows:
        lines.append(
            "    { "
            f"{c_u16(row.get('keyframe_source_index'))}, "
            f"{c_u16(row.get('camera_curve_source_index'))}, "
            f"{c_u16(row.get('point_index'))}, "
            f"{c_u32(row.get('keyframe_absolute_offset'))}, "
            f"{c_s32(row.get('frame'))}, "
            f"{c_u32(row.get('value_bits'))}, "
            f"{c_u32(row.get('tangent_in_bits'))}, "
            f"{c_u32(row.get('tangent_out_bits'))}"
            " },"
        )
    lines.extend(
        [
            "};",
            "",
            "const u32 oot3d_scene_cutscene_camera_curve_keyframe_ref_start[] = {",
        ]
    )
    for row in curve_keyframe_ref_rows:
        lines.append(f"    {c_u32(row.get('keyframe_ref_start'))},")
    lines.extend(
        [
            "};",
            "",
            "const u16 oot3d_scene_cutscene_camera_curve_keyframe_ref_count[] = {",
        ]
    )
    for row in curve_keyframe_ref_rows:
        lines.append(f"    {c_u16(row.get('keyframe_ref_count'))},")
    lines.extend(
        [
            "};",
            "",
            "const Oot3dSceneCutsceneCameraStrtRow oot3d_scene_cutscene_camera_strt_rows[] = {",
        ]
    )
    for row in strt_rows:
        lines.append(
            "    { "
            f"{c_u16(row.get('strt_source_index'))}, "
            f"{c_u16(row.get('camera_curve_source_index'))}, "
            f"{c_u16(row.get('cmad_record_source_index'))}, "
            f"{c_u16(row.get('label_ref_start'))}, "
            f"{c_u16(row.get('label_ref_count'))}, "
            f"{c_u32(row.get('strt_absolute_offset'))}, "
            f"{c_u32(row.get('strt_size'))}, "
            f"{c_u32(row.get('curve_declared_end_offset'))}, "
            f"{c_u32(row.get('cmad_declared_end_offset'))}, "
            f"{c_u8(1 if row.get('extends_past_curve_size') else 0)}, "
            f"{c_u8(1 if row.get('extends_past_cmad_size') else 0)}, "
            f"{c_string(row.get('scene_path'))}, "
            f"{c_string(row.get('curve_role'))}, "
            f"{c_string(row.get('raw_strt_hex'))}"
            " },"
        )
    lines.extend(
        [
            "};",
            "",
            "const Oot3dSceneCutsceneCameraStrtLabelRow oot3d_scene_cutscene_camera_strt_label_rows[] = {",
        ]
    )
    for row in label_rows:
        lines.append(
            "    { "
            f"{c_u16(row.get('strt_label_source_index'))}, "
            f"{c_u16(row.get('strt_source_index'))}, "
            f"{c_u16(row.get('camera_curve_source_index'))}, "
            f"{c_u16(row.get('label_index'))}, "
            f"{c_u32(row.get('label_start'))}, "
            f"{c_u32(row.get('label_end'))}, "
            f"{c_u8(1 if row.get('parsed_label') else 0)}, "
            f"{c_u16(row.get('parsed_camera_index'))}, "
            f"{c_s32(row.get('parsed_start_frame'))}, "
            f"{c_s32(row.get('parsed_end_frame'))}, "
            f"{c_string(row.get('label'))}"
            " },"
        )
    lines.extend(
        [
            "};",
            "",
            "const u32 oot3d_scene_cutscene_camera_keyframe_row_count = OOT3D_SCENE_CUTSCENE_CAMERA_KEYFRAME_ROW_COUNT;",
            "const u32 oot3d_scene_cutscene_camera_strt_row_count = OOT3D_SCENE_CUTSCENE_CAMERA_STRT_ROW_COUNT;",
            "const u32 oot3d_scene_cutscene_camera_strt_label_row_count = OOT3D_SCENE_CUTSCENE_CAMERA_STRT_LABEL_ROW_COUNT;",
            "",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_outputs(payload: dict[str, Any]) -> None:
    write_json(DEFAULT_OUT_JSON, payload)
    write_markdown(DEFAULT_OUT_MD, payload)
    write_csv(DEFAULT_OUT_KEYFRAME_CSV, as_list(payload.get("keyframe_rows")), KEYFRAME_CSV_COLUMNS)
    write_csv(DEFAULT_OUT_STRT_CSV, as_list(payload.get("strt_rows")), STRT_CSV_COLUMNS)
    write_csv(DEFAULT_OUT_STRT_LABEL_CSV, as_list(payload.get("strt_label_rows")), STRT_LABEL_CSV_COLUMNS)
    write_header(DEFAULT_OUT_HEADER, payload)
    write_source(DEFAULT_OUT_SOURCE, payload)


def validate(payload: dict[str, Any]) -> list[str]:
    summary = as_dict(payload.get("summary"))
    keyframe_rows = [as_dict(row) for row in as_list(payload.get("keyframe_rows"))]
    curve_keyframe_ref_rows = [as_dict(row) for row in as_list(payload.get("curve_keyframe_ref_rows"))]
    strt_rows = [as_dict(row) for row in as_list(payload.get("strt_rows"))]
    label_rows = [as_dict(row) for row in as_list(payload.get("strt_label_rows"))]
    errors: list[str] = []
    if int_value(summary.get("camera_curve_row_count")) < 3457:
        errors.append("camera curve row count dropped below baseline of 3457")
    if int_value(summary.get("keyframe_row_count")) < 25420:
        errors.append("keyframe row count dropped below baseline of 25420")
    if int_value(summary.get("strt_row_count")) < 102:
        errors.append("strt row count dropped below baseline of 102")
    if int_value(summary.get("strt_label_row_count")) < 547:
        errors.append("strt label row count dropped below baseline of 547")
    if int_value(summary.get("title_intro_spot00_keyframe_count")) <= 0:
        errors.append("title intro spot00 keyframes were not decoded")
    if int_value(summary.get("title_intro_spot00_strt_count")) <= 0:
        errors.append("title intro spot00 strt rows were not decoded")
    if len(curve_keyframe_ref_rows) != int_value(summary.get("camera_curve_row_count")):
        errors.append("curve keyframe ref count does not match curve row count")
    for expected_index, row in enumerate(curve_keyframe_ref_rows):
        if int_value(row.get("camera_curve_source_index")) != expected_index:
            errors.append(f"curve keyframe ref row {expected_index}: curve index mismatch")
        start = int_value(row.get("keyframe_ref_start"))
        count = int_value(row.get("keyframe_ref_count"))
        if start + count > len(keyframe_rows):
            errors.append(f"curve keyframe ref row {expected_index}: keyframe slice outside table")
        elif any(
            int_value(keyframe_rows[index].get("camera_curve_source_index")) != expected_index
            for index in range(start, start + count)
        ):
            errors.append(f"curve keyframe ref row {expected_index}: keyframe slice owner mismatch")
    for row in strt_rows:
        start = int_value(row.get("label_ref_start"))
        count = int_value(row.get("label_ref_count"))
        if start + count > len(label_rows):
            errors.append(f"strt row {row.get('strt_source_index')}: label slice outside table")
        elif any(
            int_value(label_rows[index].get("strt_source_index")) != int_value(row.get("strt_source_index"))
            for index in range(start, start + count)
        ):
            errors.append(f"strt row {row.get('strt_source_index')}: label slice owner mismatch")
    for row in keyframe_rows:
        if int_value(row.get("camera_curve_source_index")) >= int_value(summary.get("camera_curve_row_count")):
            errors.append(f"keyframe row {row.get('keyframe_source_index')}: curve index outside table")
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
        "wrote scene cutscene camera keyframe table: "
        f"{summary.get('keyframe_row_count')} keyframes, "
        f"{summary.get('strt_row_count')} strt rows, "
        f"{summary.get('strt_label_row_count')} strt labels"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
