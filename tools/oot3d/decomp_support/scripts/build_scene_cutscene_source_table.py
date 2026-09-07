#!/usr/bin/env python3
"""Build source-oriented OOT3D scene cutscene reference tables.

Native scene command 0x17 stores a scene-relative cutscene data pointer. The
handler at 0x0023449C relocates that pointer and stores it in play-state field
+0x229C through 0x0037573C, clearing field +0x22AC. This generator promotes
those native references into source-like rows and preserves the conservative
strict-N64-compatible subdecode as evidence, without treating undecoded OOT3D
payloads as known timeline data.
"""

from __future__ import annotations

import csv
import json
import math
import sys
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_NATIVE_FULL_INDEX = ROOT / "analysis" / "oot3d_native_scene_index_all_full.json"
DEFAULT_SETUP_SOURCE_TABLE = ROOT / "analysis" / "scene_setup_source_table.json"
DEFAULT_COMMAND_SOURCE_TABLE = ROOT / "analysis" / "scene_command_source_table.json"
DEFAULT_SETTING_SOURCE_TABLE = ROOT / "analysis" / "scene_setting_source_table.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_cutscene_source_table.json"
DEFAULT_OUT_CUTSCENE_CSV = ROOT / "analysis" / "scene_cutscene_source_table.csv"
DEFAULT_OUT_HEADER_CANDIDATE_CSV = ROOT / "analysis" / "scene_cutscene_header_candidate_table.csv"
DEFAULT_OUT_TIMELINE_COMMAND_CSV = ROOT / "analysis" / "scene_cutscene_command_table.csv"
DEFAULT_OUT_CAMERA_POINT_CSV = ROOT / "analysis" / "scene_cutscene_camera_point_table.csv"
DEFAULT_OUT_ENTRY_CSV = ROOT / "analysis" / "scene_cutscene_entry_table.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_cutscene_source_table.md"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "scene_cutscene_source_table.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_scene_cutscene_source_table.c"
ASSET_TOOL_SRC = ROOT.parent / "oot3d_asset_tool" / "src"

UNMAPPED_INDEX = 0xFFFF
UNMAPPED_SCENE_ID = 0xFF
NO_OFFSET = 0xFFFFFFFF
COMMAND_CUTSCENE_DATA = 0x17
HANDLER_CUTSCENE_DATA_ENTRY = 0x0023449C
HANDLER_SET_FIELD_229C_CLEAR_22AC_ENTRY = 0x0037573C
HANDLER_GET_FIELD_229C_ENTRY = 0x00357EA0
PLAY_CUTSCENE_PTR_OFFSET = 0x229C
PLAY_CUTSCENE_STATE_OFFSET = 0x22AC


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


def bool_value(value: Any) -> bool:
    return bool(value)


def hex_u32(value: Any) -> str:
    return f"0x{int_value(value) & 0xFFFFFFFF:08x}"


def c_string(value: Any) -> str:
    text = "" if value is None else str(value)
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def c_bool(value: Any) -> str:
    return "1u" if bool_value(value) else "0u"


def c_s8(value: Any) -> str:
    raw = int_value(value)
    return str(max(-128, min(127, raw)))


def c_u8(value: Any) -> str:
    return f"{int_value(value) & 0xFF}u"


def c_s16(value: Any) -> str:
    raw = int_value(value)
    return str(max(-32768, min(32767, raw)))


def c_u16(value: Any) -> str:
    return f"{int_value(value) & 0xFFFF}u"


def c_s32(value: Any) -> str:
    raw = int_value(value)
    return str(max(-2147483648, min(2147483647, raw)))


def c_u32(value: Any) -> str:
    return f"{int_value(value) & 0xFFFFFFFF}u"


def parse_hex_u32(value: Any) -> int:
    if isinstance(value, int):
        return value & 0xFFFFFFFF
    text = str(value or "0").strip()
    if not text:
        return 0
    return int(text, 16) & 0xFFFFFFFF


def parse_handler_entry(value: Any) -> int:
    text = str(value or "")
    if not text:
        return 0
    return int(text, 16)


def finite_float(value: Any) -> float | None:
    if not isinstance(value, (float, int)):
        return None
    result = float(value)
    return result if math.isfinite(result) else None


def binding_enum(value: str) -> str:
    return {
        "scene_row": "OOT3D_SCENE_COMMAND_BINDING_SCENE_ROW",
        "variant_row": "OOT3D_SCENE_COMMAND_BINDING_VARIANT_ROW",
        "unmapped": "OOT3D_SCENE_COMMAND_BINDING_UNMAPPED",
    }.get(value, "OOT3D_SCENE_COMMAND_BINDING_UNMAPPED")


def decode_status_enum(value: str) -> str:
    return {
        "native_cutscene_strict_n64_compatible_code_bin_handler_confirmed": (
            "OOT3D_CUTSCENE_DECODE_STRICT_N64_COMPATIBLE"
        ),
        "native_cutscene_header_candidates_semantic_pending": (
            "OOT3D_CUTSCENE_DECODE_HEADER_CANDIDATES_SEMANTIC_PENDING"
        ),
        "native_cutscene_undecoded_semantic_pending": "OOT3D_CUTSCENE_DECODE_UNDECODED_SEMANTIC_PENDING",
    }.get(value, "OOT3D_CUTSCENE_DECODE_UNDECODED_SEMANTIC_PENDING")


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True, allow_nan=False)
        handle.write("\n")


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def setup_source_bindings(payload: dict[str, Any]) -> dict[tuple[str, int], dict[str, Any]]:
    bindings: dict[tuple[str, int], dict[str, Any]] = {}
    for index, row in enumerate(as_list(payload.get("rows"))):
        if not isinstance(row, dict):
            continue
        scene_path = str(row.get("scene_path", "")).lower()
        setup_index = int_value(row.get("setup_index"), -1)
        if scene_path and setup_index >= 0:
            bindings[(scene_path, setup_index)] = {
                "setup_source_index": int_value(row.get("setup_source_index"), index),
                "scene_id": int_value(row.get("scene_id"), UNMAPPED_SCENE_ID),
                "scene_id_hex": row.get("scene_id_hex", ""),
                "scene_binding_kind": row.get("scene_binding_kind", "unmapped"),
                "scene_binding_index": int_value(row.get("scene_binding_index"), UNMAPPED_INDEX),
                "scene_index_symbol": row.get("scene_index_symbol", ""),
                "source_basename": row.get("source_basename", ""),
                "source_c_file": row.get("source_c_file", ""),
                "setup_role": row.get("setup_role", ""),
                "setup_symbol": row.get("setup_symbol", ""),
                "cutscene_count": int_value(row.get("cutscene_count"), 0),
                "cutscenes_symbol": row.get("cutscenes_symbol", ""),
            }
    return bindings


def command_source_bindings(payload: dict[str, Any]) -> dict[tuple[str, int], dict[str, Any]]:
    bindings: dict[tuple[str, int], dict[str, Any]] = {}
    for index, row in enumerate(as_list(payload.get("command_rows"))):
        if not isinstance(row, dict):
            continue
        scene_path = str(row.get("scene_path", "")).lower()
        setup_index = int_value(row.get("setup_index"), -1)
        command_id = int_value(row.get("command_id"), -1)
        if scene_path and setup_index >= 0 and command_id == COMMAND_CUTSCENE_DATA:
            bindings[(scene_path, setup_index)] = {
                "command_source_index": int_value(row.get("command_source_index"), index),
                "command_index": int_value(row.get("command_index"), UNMAPPED_INDEX),
                "offset": int_value(row.get("offset"), NO_OFFSET),
                "offset_hex": row.get("offset_hex", ""),
                "argument": int_value(row.get("argument"), NO_OFFSET),
                "argument_hex": row.get("argument_hex", ""),
                "decoded_status": row.get("decoded_status", ""),
                "support_level": row.get("support_level", ""),
                "handler_support_level": row.get("handler_support_level", ""),
                "handler_binding_status": row.get("handler_binding_status", ""),
                "handler_name": row.get("handler_name", ""),
                "handler_entry": row.get("handler_entry", ""),
                "handler_calls": as_list(row.get("handler_calls")),
                "handler_scene_relative_pointer_expressions": as_list(
                    row.get("handler_scene_relative_pointer_expressions")
                ),
                "payload_symbol": row.get("payload_symbol", ""),
                "export_structs": row.get("export_structs", ""),
                "open_questions": row.get("open_questions", ""),
            }
    return bindings


def command_source_count(payload: dict[str, Any]) -> int:
    return sum(
        1
        for row in as_list(payload.get("command_rows"))
        if isinstance(row, dict) and int_value(row.get("command_id"), -1) == COMMAND_CUTSCENE_DATA
    )


def setting_source_bindings(payload: dict[str, Any]) -> dict[tuple[str, int], dict[str, Any]]:
    bindings: dict[tuple[str, int], dict[str, Any]] = {}
    for index, row in enumerate(as_list(payload.get("rows"))):
        if not isinstance(row, dict):
            continue
        if row.get("setting_kind") != "cutscene_reference":
            continue
        scene_path = str(row.get("scene_path", "")).lower()
        setup_index = int_value(row.get("setup_index"), -1)
        if scene_path and setup_index >= 0:
            bindings[(scene_path, setup_index)] = {
                "setting_source_index": int_value(row.get("setting_source_index"), index),
                "validation_status": row.get("validation_status", ""),
                "payload_symbol": row.get("payload_symbol", ""),
                "cutscene_offset": int_value(row.get("cutscene_offset"), NO_OFFSET),
                "cutscene_offset_hex": row.get("cutscene_offset_hex", ""),
                "cutscene_in_file": bool_value(row.get("cutscene_in_file")),
                "open_questions": row.get("open_questions", ""),
            }
    return bindings


def setting_source_count(payload: dict[str, Any]) -> int:
    return sum(
        1
        for row in as_list(payload.get("rows"))
        if isinstance(row, dict) and row.get("setting_kind") == "cutscene_reference"
    )


def scene_data_loader(scene_root: Path):
    cache: dict[str, bytes] = {}

    def data_for(scene_path: str) -> bytes:
        key = scene_path.lower()
        if key not in cache:
            cache[key] = (scene_root / scene_path).read_bytes()
        return cache[key]

    return data_for


def u32_le_at(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(data):
        return 0
    return int.from_bytes(data[offset : offset + 4], "little", signed=False)


def cutscene_validation_status(decoded: bool, plausible_header_count: int) -> str:
    if decoded:
        return "native_cutscene_strict_n64_compatible_code_bin_handler_confirmed"
    if plausible_header_count:
        return "native_cutscene_header_candidates_semantic_pending"
    return "native_cutscene_undecoded_semantic_pending"


def sample_error_text(strict_decode: dict[str, Any]) -> str:
    errors: list[str] = []
    for error in as_list(strict_decode.get("sample_errors")):
        item = as_dict(error)
        if not item:
            continue
        errors.append(
            f"+0x{int_value(item.get('delta')):02x}@0x{int_value(item.get('offset')):08x}: {item.get('error', '')}"
        )
    return " | ".join(errors)


def build_tables(scene_root: Path) -> dict[str, Any]:
    sys.path.insert(0, str(ASSET_TOOL_SRC))
    from oot3d_asset_tool.zsi_cutscene_audit import audit_zsi_cutscene_metadata

    setup_bindings = setup_source_bindings(load_json(DEFAULT_SETUP_SOURCE_TABLE))
    command_payload = load_json(DEFAULT_COMMAND_SOURCE_TABLE)
    command_bindings = command_source_bindings(command_payload)
    setting_payload = load_json(DEFAULT_SETTING_SOURCE_TABLE)
    setting_bindings = setting_source_bindings(setting_payload)
    data_for_scene = scene_data_loader(scene_root)

    audit = audit_zsi_cutscene_metadata(scene_root, None, include_records=True, sample_limit=0)

    cutscene_rows: list[dict[str, Any]] = []
    header_candidate_rows: list[dict[str, Any]] = []
    timeline_command_rows: list[dict[str, Any]] = []
    camera_point_rows: list[dict[str, Any]] = []
    entry_rows: list[dict[str, Any]] = []

    for source_index, record in enumerate(as_list(audit.get("records"))):
        if not isinstance(record, dict):
            continue
        scene_path = str(record.get("path", ""))
        key = (scene_path.lower(), int_value(record.get("setup_index"), -1))
        setup = setup_bindings.get(key, {})
        command = command_bindings.get(key, {})
        setting = setting_bindings.get(key, {})
        strict_decode = as_dict(record.get("strict_n64_decode"))
        decoded = bool_value(strict_decode.get("decoded"))
        candidates = [as_dict(candidate) for candidate in as_list(record.get("header_candidates"))]
        plausible_header_count = sum(1 for candidate in candidates if bool_value(candidate.get("plausible_n64_header")))
        validation_status = cutscene_validation_status(decoded, plausible_header_count)

        header_candidate_start = len(header_candidate_rows)
        for local_index, candidate in enumerate(candidates):
            first_command_id = candidate.get("first_command_id")
            header_candidate_rows.append(
                {
                    "header_candidate_source_index": len(header_candidate_rows),
                    "cutscene_source_index": source_index,
                    "local_candidate_index": local_index,
                    "scene_path": scene_path,
                    "setup_index": int_value(record.get("setup_index"), UNMAPPED_INDEX),
                    "delta": int_value(candidate.get("delta"), NO_OFFSET),
                    "delta_hex": f"0x{int_value(candidate.get('delta'), 0):02x}",
                    "offset": int_value(candidate.get("offset"), NO_OFFSET),
                    "offset_hex": hex_u32(candidate.get("offset", NO_OFFSET)),
                    "total_entries": int_value(candidate.get("total_entries"), 0),
                    "end_frame": int_value(candidate.get("end_frame"), 0),
                    "plausible_n64_header": bool_value(candidate.get("plausible_n64_header")),
                    "first_command_id": int_value(first_command_id, -1),
                    "first_command_id_hex": "" if first_command_id is None else f"0x{int_value(first_command_id) & 0xFFFF:04x}",
                    "first_command_name": candidate.get("first_command_name") or "",
                }
            )
        header_candidate_count = len(header_candidate_rows) - header_candidate_start

        timeline_command_start = len(timeline_command_rows)
        camera_point_start = len(camera_point_rows)
        entry_start = len(entry_rows)

        if decoded:
            scene_data = data_for_scene(scene_path)
            for decoded_command in as_list(strict_decode.get("commands")):
                if not isinstance(decoded_command, dict):
                    continue
                timeline_command_index = len(timeline_command_rows)
                list_header = as_dict(decoded_command.get("list_header"))
                command_camera_start = len(camera_point_rows)
                for local_point_index, point in enumerate(as_list(decoded_command.get("camera_points"))):
                    point = as_dict(point)
                    point_offset = int_value(point.get("offset"), NO_OFFSET)
                    pos = as_dict(point.get("pos"))
                    view_angle = finite_float(point.get("view_angle"))
                    camera_point_rows.append(
                        {
                            "camera_point_source_index": len(camera_point_rows),
                            "cutscene_source_index": source_index,
                            "timeline_command_source_index": timeline_command_index,
                            "local_point_index": local_point_index,
                            "scene_path": scene_path,
                            "setup_index": int_value(record.get("setup_index"), UNMAPPED_INDEX),
                            "command_local_index": int_value(decoded_command.get("index"), UNMAPPED_INDEX),
                            "point_offset": point_offset,
                            "point_offset_hex": hex_u32(point_offset),
                            "continue_flag": int_value(point.get("continue_flag"), 0),
                            "camera_roll": int_value(point.get("camera_roll"), 0),
                            "next_point_frame": int_value(point.get("next_point_frame"), 0),
                            "view_angle": view_angle,
                            "view_angle_bits": u32_le_at(scene_data, point_offset + 4),
                            "view_angle_bits_hex": hex_u32(u32_le_at(scene_data, point_offset + 4)),
                            "pos_x": int_value(pos.get("x"), 0),
                            "pos_y": int_value(pos.get("y"), 0),
                            "pos_z": int_value(pos.get("z"), 0),
                            "unused": int_value(point.get("unused"), 0),
                        }
                    )
                command_camera_count = len(camera_point_rows) - command_camera_start

                command_entry_start = len(entry_rows)
                for local_entry_index, entry in enumerate(as_list(decoded_command.get("entries"))):
                    entry = as_dict(entry)
                    raw_words = [parse_hex_u32(word) for word in as_list(entry.get("raw_words"))[:12]]
                    raw_word_count = len(raw_words)
                    raw_words = raw_words + [0] * (12 - len(raw_words))
                    entry_rows.append(
                        {
                            "entry_source_index": len(entry_rows),
                            "cutscene_source_index": source_index,
                            "timeline_command_source_index": timeline_command_index,
                            "local_entry_index": local_entry_index,
                            "scene_path": scene_path,
                            "setup_index": int_value(record.get("setup_index"), UNMAPPED_INDEX),
                            "command_local_index": int_value(decoded_command.get("index"), UNMAPPED_INDEX),
                            "entry_offset": int_value(entry.get("offset"), NO_OFFSET),
                            "entry_offset_hex": hex_u32(entry.get("offset", NO_OFFSET)),
                            "raw_word_count": raw_word_count,
                            "raw_words": [f"0x{word:08x}" for word in raw_words],
                            "raw_words_text": ";".join(f"0x{word:08x}" for word in raw_words[:raw_word_count]),
                            "primary": int_value(entry.get("primary"), 0),
                            "start_frame": int_value(entry.get("start_frame"), 0),
                            "end_frame": int_value(entry.get("end_frame"), 0),
                        }
                    )
                command_entry_count = len(entry_rows) - command_entry_start

                timeline_command_rows.append(
                    {
                        "timeline_command_source_index": timeline_command_index,
                        "cutscene_source_index": source_index,
                        "local_command_index": int_value(decoded_command.get("index"), UNMAPPED_INDEX),
                        "scene_path": scene_path,
                        "setup_index": int_value(record.get("setup_index"), UNMAPPED_INDEX),
                        "command_offset": int_value(decoded_command.get("offset"), NO_OFFSET),
                        "command_offset_hex": hex_u32(decoded_command.get("offset", NO_OFFSET)),
                        "command_id": int_value(decoded_command.get("command_id"), -1),
                        "command_id_hex": decoded_command.get("command_id_hex", ""),
                        "command_name": decoded_command.get("name", ""),
                        "category": decoded_command.get("category", ""),
                        "entry_count": int_value(decoded_command.get("entry_count"), 0),
                        "camera_point_count": int_value(decoded_command.get("camera_point_count"), 0),
                        "payload_size": int_value(decoded_command.get("payload_size"), 0),
                        "total_size": int_value(decoded_command.get("total_size"), 0),
                        "list_header_offset": int_value(list_header.get("offset"), NO_OFFSET),
                        "list_header_offset_hex": hex_u32(list_header.get("offset", NO_OFFSET)),
                        "list_header_word0": parse_hex_u32(list_header.get("word0", "0x00000000")),
                        "list_header_word0_hex": f"0x{parse_hex_u32(list_header.get('word0', '0x00000000')):08x}",
                        "list_header_word1": parse_hex_u32(list_header.get("word1", "0x00000000")),
                        "list_header_word1_hex": f"0x{parse_hex_u32(list_header.get('word1', '0x00000000')):08x}",
                        "list_header_param": int_value(list_header.get("param"), 0),
                        "list_header_start_frame": int_value(list_header.get("start_frame"), 0),
                        "list_header_end_frame": int_value(list_header.get("end_frame"), 0),
                        "list_header_unused": int_value(list_header.get("unused"), 0),
                        "camera_point_ref_start": command_camera_start,
                        "camera_point_ref_count": command_camera_count,
                        "entry_ref_start": command_entry_start,
                        "entry_ref_count": command_entry_count,
                    }
                )

        timeline_command_count = len(timeline_command_rows) - timeline_command_start
        camera_point_count = len(camera_point_rows) - camera_point_start
        entry_count = len(entry_rows) - entry_start

        strict_error_count = int_value(strict_decode.get("error_count"), 0)
        cutscene_rows.append(
            {
                "cutscene_source_index": source_index,
                "scene_path": scene_path,
                "scene_role": record.get("role", ""),
                "scene_stem": record.get("scene_stem", ""),
                "source_basename": setup.get("source_basename", ""),
                "source_c_file": setup.get("source_c_file", ""),
                "scene_id": setup.get("scene_id", UNMAPPED_SCENE_ID),
                "scene_id_hex": setup.get("scene_id_hex", ""),
                "scene_binding_kind": setup.get("scene_binding_kind", "unmapped"),
                "scene_binding_index": setup.get("scene_binding_index", UNMAPPED_INDEX),
                "scene_index_symbol": setup.get("scene_index_symbol", ""),
                "setup_source_index": setup.get("setup_source_index", UNMAPPED_INDEX),
                "setup_index": int_value(record.get("setup_index"), UNMAPPED_INDEX),
                "setup_role": setup.get("setup_role", record.get("role", "")),
                "setup_symbol": setup.get("setup_symbol", ""),
                "cutscenes_symbol": setup.get("cutscenes_symbol", ""),
                "setting_source_index": setting.get("setting_source_index", UNMAPPED_INDEX),
                "command_source_index": command.get("command_source_index", UNMAPPED_INDEX),
                "command_index": command.get("command_index", UNMAPPED_INDEX),
                "command_offset": int_value(record.get("command_offset"), command.get("offset", NO_OFFSET)),
                "command_offset_hex": hex_u32(int_value(record.get("command_offset"), command.get("offset", NO_OFFSET))),
                "command_word": int_value(record.get("command_word"), 0),
                "command_parameter": int_value(record.get("command_parameter"), 0),
                "command_argument": int_value(record.get("argument"), NO_OFFSET),
                "command_argument_hex": hex_u32(record.get("argument", NO_OFFSET)),
                "argument_in_bounds": bool_value(record.get("argument_in_bounds")),
                "bytes_available_from_argument": int_value(record.get("bytes_available_from_argument"), 0),
                "payload_symbol": command.get("payload_symbol") or setting.get("payload_symbol", ""),
                "raw_prefix_hex": record.get("raw_prefix_hex", ""),
                "header_candidate_ref_start": header_candidate_start,
                "header_candidate_ref_count": header_candidate_count,
                "plausible_header_candidate_count": plausible_header_count,
                "timeline_command_ref_start": timeline_command_start,
                "timeline_command_ref_count": timeline_command_count,
                "camera_point_ref_start": camera_point_start,
                "camera_point_ref_count": camera_point_count,
                "entry_ref_start": entry_start,
                "entry_ref_count": entry_count,
                "strict_decoded": decoded,
                "strict_delta": int_value(strict_decode.get("delta"), NO_OFFSET),
                "strict_delta_hex": "" if not decoded else f"0x{int_value(strict_decode.get('delta'), 0):02x}",
                "strict_header_offset": int_value(strict_decode.get("offset"), NO_OFFSET),
                "strict_header_offset_hex": "" if not decoded else hex_u32(strict_decode.get("offset", NO_OFFSET)),
                "strict_total_entries": int_value(strict_decode.get("total_entries"), 0),
                "strict_end_frame": int_value(strict_decode.get("end_frame"), 0),
                "strict_command_count": int_value(strict_decode.get("command_count"), 0),
                "strict_camera_point_count": int_value(strict_decode.get("camera_point_count"), 0),
                "strict_decoded_size": int_value(strict_decode.get("decoded_size"), 0),
                "strict_terminated_by_end": bool_value(strict_decode.get("terminated_by_end")),
                "strict_alternate_decode_count": int_value(strict_decode.get("alternate_decode_count"), 0),
                "strict_decode_error_count": strict_error_count,
                "strict_decode_error_sample": sample_error_text(strict_decode),
                "validation_status": validation_status,
                "setting_validation_status": setting.get("validation_status", ""),
                "handler_name": command.get("handler_name", "oot3d_scene_cmd_17_cutscene_data"),
                "handler_entry": parse_handler_entry(command.get("handler_entry", f"{HANDLER_CUTSCENE_DATA_ENTRY:08x}")),
                "handler_setter_entry": HANDLER_SET_FIELD_229C_CLEAR_22AC_ENTRY,
                "handler_getter_entry": HANDLER_GET_FIELD_229C_ENTRY,
                "play_cutscene_ptr_offset": PLAY_CUTSCENE_PTR_OFFSET,
                "play_cutscene_state_offset": PLAY_CUTSCENE_STATE_OFFSET,
                "handler_support_level": command.get("handler_support_level", ""),
                "handler_calls_text": "; ".join(str(item) for item in command.get("handler_calls", [])),
                "handler_scene_relative_pointer_expressions_text": "; ".join(
                    str(item) for item in command.get("handler_scene_relative_pointer_expressions", [])
                ),
                "open_questions": command.get("open_questions") or setting.get("open_questions", ""),
            }
        )

    validation_status_counts = Counter(str(row["validation_status"]) for row in cutscene_rows)
    scene_binding_gap_count = sum(1 for row in cutscene_rows if row["scene_binding_kind"] == "unmapped")
    setup_binding_gap_count = sum(1 for row in cutscene_rows if row["setup_source_index"] == UNMAPPED_INDEX)
    command_binding_gap_count = sum(1 for row in cutscene_rows if row["command_source_index"] == UNMAPPED_INDEX)
    setting_binding_gap_count = sum(1 for row in cutscene_rows if row["setting_source_index"] == UNMAPPED_INDEX)
    spot04_rows = [row for row in cutscene_rows if row["scene_path"] == "spot04_info.zsi"]

    summary = {
        "format": "oot3d_scene_cutscene_source_table_v1",
        "scene_root": str(scene_root),
        "cutscene_source_row_count": len(cutscene_rows),
        "expected_cutscene_command_count": command_source_count(command_payload),
        "expected_cutscene_setting_count": setting_source_count(setting_payload),
        "audit_cutscene_command_total": int_value(audit.get("cutscene_command_total")),
        "unique_cutscene_data_offset_count": int_value(audit.get("unique_cutscene_data_offset_count")),
        "header_candidate_row_count": len(header_candidate_rows),
        "plausible_header_candidate_count": sum(1 for row in header_candidate_rows if row["plausible_n64_header"]),
        "strict_decoded_cutscene_count": int_value(audit.get("strict_n64_decoded_count")),
        "strict_decode_error_count": int_value(audit.get("strict_n64_decode_error_count")),
        "timeline_command_row_count": len(timeline_command_rows),
        "camera_point_row_count": len(camera_point_rows),
        "entry_row_count": len(entry_rows),
        "strict_command_category_counts": audit.get("strict_n64_command_category_counts", {}),
        "strict_command_id_counts": audit.get("strict_n64_command_id_counts", {}),
        "strict_decode_counts_by_delta": audit.get("strict_n64_decode_counts_by_delta", {}),
        "header_candidate_counts_by_delta": audit.get("header_candidate_counts_by_delta", {}),
        "validation_status_counts": dict(sorted(validation_status_counts.items())),
        "scene_binding_gap_count": scene_binding_gap_count,
        "setup_binding_gap_count": setup_binding_gap_count,
        "command_binding_gap_count": command_binding_gap_count,
        "setting_binding_gap_count": setting_binding_gap_count,
        "spot04_cutscene_count": len(spot04_rows),
        "spot04_strict_decoded_count": sum(1 for row in spot04_rows if row["strict_decoded"]),
        "handler_evidence": {
            "scene_command_id": COMMAND_CUTSCENE_DATA,
            "scene_command_handler_entry": f"0x{HANDLER_CUTSCENE_DATA_ENTRY:08x}",
            "scene_command_handler": "oot3d_scene_cmd_17_cutscene_data",
            "setter_entry": f"0x{HANDLER_SET_FIELD_229C_CLEAR_22AC_ENTRY:08x}",
            "setter_semantics": "store relocated cutscene pointer at play+0x229c and clear play+0x22ac",
            "getter_entry": f"0x{HANDLER_GET_FIELD_229C_ENTRY:08x}",
            "getter_semantics": "return play+0x229c",
            "play_cutscene_ptr_offset": f"0x{PLAY_CUTSCENE_PTR_OFFSET:04x}",
            "play_cutscene_state_offset": f"0x{PLAY_CUTSCENE_STATE_OFFSET:04x}",
        },
    }

    return {
        "summary": summary,
        "cutscene_rows": cutscene_rows,
        "header_candidate_rows": header_candidate_rows,
        "timeline_command_rows": timeline_command_rows,
        "camera_point_rows": camera_point_rows,
        "entry_rows": entry_rows,
    }


def write_markdown(path: Path, tables: dict[str, Any]) -> None:
    summary = as_dict(tables.get("summary"))
    cutscene_rows = as_list(tables.get("cutscene_rows"))
    spot04_rows = [row for row in cutscene_rows if as_dict(row).get("scene_path") == "spot04_info.zsi"]
    decoded_rows = [as_dict(row) for row in cutscene_rows if as_dict(row).get("strict_decoded")]

    def md_cell(value: Any) -> str:
        return str(value or "").replace("|", "\\|")

    lines = [
        "# OOT3D Scene Cutscene Source Table",
        "",
        "This table promotes native scene command `0x17` cutscene references into source-oriented rows. "
        "The native handler evidence is from OOT3D `code.bin`: `0x0023449C` relocates the scene-relative "
        "pointer, `0x0037573C` stores it at `play+0x229c` and clears `play+0x22ac`, and `0x00357EA0` "
        "returns `play+0x229c`.",
        "",
        "The strict timeline decode is intentionally conservative. Rows marked "
        "`native_cutscene_strict_n64_compatible_code_bin_handler_confirmed` expose decoded commands, "
        "camera points, and entries. Rows marked `native_cutscene_header_candidates_semantic_pending` "
        "or `native_cutscene_undecoded_semantic_pending` remain native references awaiting OOT3D-specific "
        "payload decompilation.",
        "",
        "## Summary",
        "",
        f"- Cutscene references: {summary.get('cutscene_source_row_count')}",
        f"- Expected command `0x17` rows: {summary.get('expected_cutscene_command_count')}",
        f"- Expected scalar setting rows: {summary.get('expected_cutscene_setting_count')}",
        f"- Unique native data offsets: {summary.get('unique_cutscene_data_offset_count')}",
        f"- Header candidates: {summary.get('header_candidate_row_count')}",
        f"- Plausible strict-header candidates: {summary.get('plausible_header_candidate_count')}",
        f"- Strict decoded cutscenes: {summary.get('strict_decoded_cutscene_count')}",
        f"- Strict decode errors: {summary.get('strict_decode_error_count')}",
        f"- Timeline commands: {summary.get('timeline_command_row_count')}",
        f"- Camera points: {summary.get('camera_point_row_count')}",
        f"- Timeline entries: {summary.get('entry_row_count')}",
        f"- Binding gaps: scene {summary.get('scene_binding_gap_count')}, setup {summary.get('setup_binding_gap_count')}, command {summary.get('command_binding_gap_count')}, setting {summary.get('setting_binding_gap_count')}",
        f"- `spot04_info.zsi`: {summary.get('spot04_cutscene_count')} refs, {summary.get('spot04_strict_decoded_count')} strict decoded",
        "",
        "## Validation Status Counts",
        "",
    ]

    for status, count in sorted(as_dict(summary.get("validation_status_counts")).items()):
        lines.append(f"- `{status}`: {count}")

    lines.extend(["", "## Strict Command Categories", ""])
    for category, count in sorted(as_dict(summary.get("strict_command_category_counts")).items()):
        lines.append(f"- `{category}`: {count}")

    lines.extend(
        [
            "",
            "## Decoded Cutscene Rows",
            "",
            "| index | scene | setup | offset | delta | commands | camera points | entries | end frame |",
            "| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
        ]
    )
    for row in decoded_rows:
        lines.append(
            "| {cutscene_source_index} | {scene_path} | {setup_index} | {command_argument_hex} | "
            "{strict_delta_hex} | {timeline_command_ref_count} | {camera_point_ref_count} | "
            "{entry_ref_count} | {strict_end_frame} |".format(**row)
        )

    lines.extend(
        [
            "",
            "## Kokiri / `spot04_info.zsi` Worklist",
            "",
            "| setup | offset | status | strict delta | commands | camera points | error sample |",
            "| ---: | ---: | --- | ---: | ---: | ---: | --- |",
        ]
    )
    for row in spot04_rows:
        row = as_dict(row)
        delta = row.get("strict_delta_hex") or ""
        error_sample = md_cell(row.get("strict_decode_error_sample"))
        lines.append(
            "| {setup_index} | {command_argument_hex} | `{validation_status}` | {delta} | "
            "{timeline_command_ref_count} | {camera_point_ref_count} | {error_sample} |".format(
                delta=delta,
                error_sample=error_sample,
                **row,
            )
        )

    lines.extend(
        [
            "",
            "## Outputs",
            "",
            "- `analysis/scene_cutscene_source_table.json`",
            "- `analysis/scene_cutscene_source_table.csv`",
            "- `analysis/scene_cutscene_header_candidate_table.csv`",
            "- `analysis/scene_cutscene_command_table.csv`",
            "- `analysis/scene_cutscene_camera_point_table.csv`",
            "- `analysis/scene_cutscene_entry_table.csv`",
            "- `include/oot3d/scene_cutscene_source_table.h`",
            "- `src/code/z_scene_cutscene_source_table.c`",
            "",
        ]
    )

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def write_header(path: Path, tables: dict[str, Any]) -> None:
    summary = as_dict(tables.get("summary"))
    content = f"""#ifndef OOT3D_SCENE_CUTSCENE_SOURCE_TABLE_H
#define OOT3D_SCENE_CUTSCENE_SOURCE_TABLE_H

#include "oot3d/scene.h"
#include "oot3d/scene_command_source_table.h"

enum {{
    OOT3D_SCENE_CUTSCENE_SOURCE_ROW_COUNT = {summary.get('cutscene_source_row_count')},
    OOT3D_SCENE_CUTSCENE_HEADER_CANDIDATE_ROW_COUNT = {summary.get('header_candidate_row_count')},
    OOT3D_SCENE_CUTSCENE_TIMELINE_COMMAND_ROW_COUNT = {summary.get('timeline_command_row_count')},
    OOT3D_SCENE_CUTSCENE_CAMERA_POINT_ROW_COUNT = {summary.get('camera_point_row_count')},
    OOT3D_SCENE_CUTSCENE_ENTRY_ROW_COUNT = {summary.get('entry_row_count')},
    OOT3D_SCENE_CUTSCENE_SOURCE_UNMAPPED_INDEX = 0xFFFF,
    OOT3D_SCENE_CUTSCENE_SOURCE_NO_OFFSET = 0xFFFFFFFF,
}};

typedef enum {{
    OOT3D_CUTSCENE_DECODE_STRICT_N64_COMPATIBLE,
    OOT3D_CUTSCENE_DECODE_HEADER_CANDIDATES_SEMANTIC_PENDING,
    OOT3D_CUTSCENE_DECODE_UNDECODED_SEMANTIC_PENDING,
}} Oot3dCutsceneDecodeStatus;

typedef struct {{
    u16 cutsceneSourceIndex;
    Oot3dSceneCommandBindingKind bindingKind;
    u16 bindingIndex;
    u8 sceneId;
    u16 setupSourceIndex;
    u16 commandSourceIndex;
    u16 settingSourceIndex;
    u16 setupIndex;
    u16 commandIndex;
    u32 commandOffset;
    u32 commandArgument;
    u8 argumentInBounds;
    u32 bytesAvailableFromArgument;
    u16 headerCandidateRefStart;
    u16 headerCandidateRefCount;
    u16 plausibleHeaderCandidateCount;
    u16 timelineCommandRefStart;
    u16 timelineCommandRefCount;
    u16 cameraPointRefStart;
    u16 cameraPointRefCount;
    u16 entryRefStart;
    u16 entryRefCount;
    Oot3dCutsceneDecodeStatus decodeStatus;
    u32 strictHeaderOffset;
    u32 strictDelta;
    s32 strictTotalEntries;
    s32 strictEndFrame;
    u32 strictDecodedSize;
    u8 strictTerminatedByEnd;
    u16 strictAlternateDecodeCount;
    const char* scenePath;
    const char* sourceBasename;
    const char* setupRole;
    const char* setupSymbol;
    const char* payloadSymbol;
    const char* rawPrefixHex;
    const char* validationStatus;
    const char* openQuestions;
    const char* handlerName;
    u32 handlerEntry;
    u32 handlerSetterEntry;
    u32 handlerGetterEntry;
    u32 playCutscenePtrOffset;
    u32 playCutsceneStateOffset;
}} Oot3dSceneCutsceneSourceRow;

typedef struct {{
    u16 headerCandidateSourceIndex;
    u16 cutsceneSourceIndex;
    u16 localCandidateIndex;
    u32 delta;
    u32 offset;
    s32 totalEntries;
    s32 endFrame;
    u8 plausibleN64Header;
    s32 firstCommandId;
    const char* firstCommandName;
}} Oot3dSceneCutsceneHeaderCandidateRow;

typedef struct {{
    u16 timelineCommandSourceIndex;
    u16 cutsceneSourceIndex;
    u16 localCommandIndex;
    u32 commandOffset;
    s32 commandId;
    const char* commandName;
    const char* category;
    u16 entryCount;
    u16 cameraPointCount;
    u32 payloadSize;
    u32 totalSize;
    u32 listHeaderOffset;
    u32 listHeaderWord0;
    u32 listHeaderWord1;
    s32 listHeaderParam;
    s32 listHeaderStartFrame;
    s32 listHeaderEndFrame;
    s32 listHeaderUnused;
    u16 cameraPointRefStart;
    u16 cameraPointRefCount;
    u16 entryRefStart;
    u16 entryRefCount;
}} Oot3dSceneCutsceneTimelineCommandRow;

typedef struct {{
    u16 cameraPointSourceIndex;
    u16 cutsceneSourceIndex;
    u16 timelineCommandSourceIndex;
    u16 localPointIndex;
    u32 pointOffset;
    s8 continueFlag;
    s8 cameraRoll;
    u16 nextPointFrame;
    u32 viewAngleBits;
    Oot3dVec3s pos;
    s16 unused;
}} Oot3dSceneCutsceneCameraPointRow;

typedef struct {{
    u16 entrySourceIndex;
    u16 cutsceneSourceIndex;
    u16 timelineCommandSourceIndex;
    u16 localEntryIndex;
    u32 entryOffset;
    u16 rawWordCount;
    u32 rawWords[12];
    u32 primary;
    s32 startFrame;
    s32 endFrame;
}} Oot3dSceneCutsceneEntryRow;

extern const Oot3dSceneCutsceneSourceRow oot3d_scene_cutscene_source_rows[];
extern const Oot3dSceneCutsceneHeaderCandidateRow oot3d_scene_cutscene_header_candidate_rows[];
extern const Oot3dSceneCutsceneTimelineCommandRow oot3d_scene_cutscene_timeline_command_rows[];
extern const Oot3dSceneCutsceneCameraPointRow oot3d_scene_cutscene_camera_point_rows[];
extern const Oot3dSceneCutsceneEntryRow oot3d_scene_cutscene_entry_rows[];
extern const u32 oot3d_scene_cutscene_source_row_count;
extern const u32 oot3d_scene_cutscene_header_candidate_row_count;
extern const u32 oot3d_scene_cutscene_timeline_command_row_count;
extern const u32 oot3d_scene_cutscene_camera_point_row_count;
extern const u32 oot3d_scene_cutscene_entry_row_count;

#endif
"""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def write_source(path: Path, tables: dict[str, Any]) -> None:
    cutscene_rows = [as_dict(row) for row in as_list(tables.get("cutscene_rows"))]
    header_candidate_rows = [as_dict(row) for row in as_list(tables.get("header_candidate_rows"))]
    timeline_command_rows = [as_dict(row) for row in as_list(tables.get("timeline_command_rows"))]
    camera_point_rows = [as_dict(row) for row in as_list(tables.get("camera_point_rows"))]
    entry_rows = [as_dict(row) for row in as_list(tables.get("entry_rows"))]

    lines = [
        "/* Generated by build_scene_cutscene_source_table.py. */",
        "",
        '#include "oot3d/scene_cutscene_source_table.h"',
        "",
        "const Oot3dSceneCutsceneSourceRow oot3d_scene_cutscene_source_rows[] = {",
    ]
    for row in cutscene_rows:
        lines.append(
            "    { "
            f"{c_u16(row.get('cutscene_source_index'))}, "
            f"{binding_enum(str(row.get('scene_binding_kind', 'unmapped')))}, "
            f"{c_u16(row.get('scene_binding_index'))}, "
            f"{c_u8(row.get('scene_id'))}, "
            f"{c_u16(row.get('setup_source_index'))}, "
            f"{c_u16(row.get('command_source_index'))}, "
            f"{c_u16(row.get('setting_source_index'))}, "
            f"{c_u16(row.get('setup_index'))}, "
            f"{c_u16(row.get('command_index'))}, "
            f"{c_u32(row.get('command_offset'))}, "
            f"{c_u32(row.get('command_argument'))}, "
            f"{c_bool(row.get('argument_in_bounds'))}, "
            f"{c_u32(row.get('bytes_available_from_argument'))}, "
            f"{c_u16(row.get('header_candidate_ref_start'))}, "
            f"{c_u16(row.get('header_candidate_ref_count'))}, "
            f"{c_u16(row.get('plausible_header_candidate_count'))}, "
            f"{c_u16(row.get('timeline_command_ref_start'))}, "
            f"{c_u16(row.get('timeline_command_ref_count'))}, "
            f"{c_u16(row.get('camera_point_ref_start'))}, "
            f"{c_u16(row.get('camera_point_ref_count'))}, "
            f"{c_u16(row.get('entry_ref_start'))}, "
            f"{c_u16(row.get('entry_ref_count'))}, "
            f"{decode_status_enum(str(row.get('validation_status')))}, "
            f"{c_u32(row.get('strict_header_offset'))}, "
            f"{c_u32(row.get('strict_delta'))}, "
            f"{c_s32(row.get('strict_total_entries'))}, "
            f"{c_s32(row.get('strict_end_frame'))}, "
            f"{c_u32(row.get('strict_decoded_size'))}, "
            f"{c_bool(row.get('strict_terminated_by_end'))}, "
            f"{c_u16(row.get('strict_alternate_decode_count'))}, "
            f"{c_string(row.get('scene_path'))}, "
            f"{c_string(row.get('source_basename'))}, "
            f"{c_string(row.get('setup_role'))}, "
            f"{c_string(row.get('setup_symbol'))}, "
            f"{c_string(row.get('payload_symbol'))}, "
            f"{c_string(row.get('raw_prefix_hex'))}, "
            f"{c_string(row.get('validation_status'))}, "
            f"{c_string(row.get('open_questions'))}, "
            f"{c_string(row.get('handler_name'))}, "
            f"{c_u32(row.get('handler_entry'))}, "
            f"{c_u32(row.get('handler_setter_entry'))}, "
            f"{c_u32(row.get('handler_getter_entry'))}, "
            f"{c_u32(row.get('play_cutscene_ptr_offset'))}, "
            f"{c_u32(row.get('play_cutscene_state_offset'))}"
            " },"
        )
    lines.extend(["};", ""])

    lines.append("const Oot3dSceneCutsceneHeaderCandidateRow oot3d_scene_cutscene_header_candidate_rows[] = {")
    for row in header_candidate_rows:
        lines.append(
            "    { "
            f"{c_u16(row.get('header_candidate_source_index'))}, "
            f"{c_u16(row.get('cutscene_source_index'))}, "
            f"{c_u16(row.get('local_candidate_index'))}, "
            f"{c_u32(row.get('delta'))}, "
            f"{c_u32(row.get('offset'))}, "
            f"{c_s32(row.get('total_entries'))}, "
            f"{c_s32(row.get('end_frame'))}, "
            f"{c_bool(row.get('plausible_n64_header'))}, "
            f"{c_s32(row.get('first_command_id'))}, "
            f"{c_string(row.get('first_command_name'))}"
            " },"
        )
    lines.extend(["};", ""])

    lines.append("const Oot3dSceneCutsceneTimelineCommandRow oot3d_scene_cutscene_timeline_command_rows[] = {")
    for row in timeline_command_rows:
        lines.append(
            "    { "
            f"{c_u16(row.get('timeline_command_source_index'))}, "
            f"{c_u16(row.get('cutscene_source_index'))}, "
            f"{c_u16(row.get('local_command_index'))}, "
            f"{c_u32(row.get('command_offset'))}, "
            f"{c_s32(row.get('command_id'))}, "
            f"{c_string(row.get('command_name'))}, "
            f"{c_string(row.get('category'))}, "
            f"{c_u16(row.get('entry_count'))}, "
            f"{c_u16(row.get('camera_point_count'))}, "
            f"{c_u32(row.get('payload_size'))}, "
            f"{c_u32(row.get('total_size'))}, "
            f"{c_u32(row.get('list_header_offset'))}, "
            f"{c_u32(row.get('list_header_word0'))}, "
            f"{c_u32(row.get('list_header_word1'))}, "
            f"{c_s32(row.get('list_header_param'))}, "
            f"{c_s32(row.get('list_header_start_frame'))}, "
            f"{c_s32(row.get('list_header_end_frame'))}, "
            f"{c_s32(row.get('list_header_unused'))}, "
            f"{c_u16(row.get('camera_point_ref_start'))}, "
            f"{c_u16(row.get('camera_point_ref_count'))}, "
            f"{c_u16(row.get('entry_ref_start'))}, "
            f"{c_u16(row.get('entry_ref_count'))}"
            " },"
        )
    lines.extend(["};", ""])

    lines.append("const Oot3dSceneCutsceneCameraPointRow oot3d_scene_cutscene_camera_point_rows[] = {")
    for row in camera_point_rows:
        lines.append(
            "    { "
            f"{c_u16(row.get('camera_point_source_index'))}, "
            f"{c_u16(row.get('cutscene_source_index'))}, "
            f"{c_u16(row.get('timeline_command_source_index'))}, "
            f"{c_u16(row.get('local_point_index'))}, "
            f"{c_u32(row.get('point_offset'))}, "
            f"{c_s8(row.get('continue_flag'))}, "
            f"{c_s8(row.get('camera_roll'))}, "
            f"{c_u16(row.get('next_point_frame'))}, "
            f"{c_u32(row.get('view_angle_bits'))}, "
            f"{{ {c_s16(row.get('pos_x'))}, {c_s16(row.get('pos_y'))}, {c_s16(row.get('pos_z'))} }}, "
            f"{c_s16(row.get('unused'))}"
            " },"
        )
    lines.extend(["};", ""])

    lines.append("const Oot3dSceneCutsceneEntryRow oot3d_scene_cutscene_entry_rows[] = {")
    for row in entry_rows:
        raw_words = [parse_hex_u32(word) for word in as_list(row.get("raw_words"))[:12]]
        raw_words = raw_words + [0] * (12 - len(raw_words))
        raw_words_initializer = "{ " + ", ".join(c_u32(word) for word in raw_words) + " }"
        lines.append(
            "    { "
            f"{c_u16(row.get('entry_source_index'))}, "
            f"{c_u16(row.get('cutscene_source_index'))}, "
            f"{c_u16(row.get('timeline_command_source_index'))}, "
            f"{c_u16(row.get('local_entry_index'))}, "
            f"{c_u32(row.get('entry_offset'))}, "
            f"{c_u16(row.get('raw_word_count'))}, "
            f"{raw_words_initializer}, "
            f"{c_u32(row.get('primary'))}, "
            f"{c_s32(row.get('start_frame'))}, "
            f"{c_s32(row.get('end_frame'))}"
            " },"
        )
    lines.extend(["};", ""])

    lines.extend(
        [
            f"const u32 oot3d_scene_cutscene_source_row_count = {len(cutscene_rows)}u;",
            f"const u32 oot3d_scene_cutscene_header_candidate_row_count = {len(header_candidate_rows)}u;",
            f"const u32 oot3d_scene_cutscene_timeline_command_row_count = {len(timeline_command_rows)}u;",
            f"const u32 oot3d_scene_cutscene_camera_point_row_count = {len(camera_point_rows)}u;",
            f"const u32 oot3d_scene_cutscene_entry_row_count = {len(entry_rows)}u;",
            "",
        ]
    )

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def write_outputs(tables: dict[str, Any]) -> None:
    write_json(DEFAULT_OUT_JSON, tables)
    write_csv(
        DEFAULT_OUT_CUTSCENE_CSV,
        as_list(tables.get("cutscene_rows")),
        [
            "cutscene_source_index",
            "scene_path",
            "scene_id_hex",
            "setup_index",
            "setup_role",
            "setup_source_index",
            "command_source_index",
            "setting_source_index",
            "command_offset_hex",
            "command_argument_hex",
            "argument_in_bounds",
            "bytes_available_from_argument",
            "payload_symbol",
            "plausible_header_candidate_count",
            "timeline_command_ref_count",
            "camera_point_ref_count",
            "entry_ref_count",
            "strict_decoded",
            "strict_delta_hex",
            "strict_header_offset_hex",
            "strict_total_entries",
            "strict_end_frame",
            "strict_decoded_size",
            "strict_decode_error_count",
            "validation_status",
            "strict_decode_error_sample",
        ],
    )
    write_csv(
        DEFAULT_OUT_HEADER_CANDIDATE_CSV,
        as_list(tables.get("header_candidate_rows")),
        [
            "header_candidate_source_index",
            "cutscene_source_index",
            "scene_path",
            "setup_index",
            "local_candidate_index",
            "delta_hex",
            "offset_hex",
            "total_entries",
            "end_frame",
            "plausible_n64_header",
            "first_command_id_hex",
            "first_command_name",
        ],
    )
    write_csv(
        DEFAULT_OUT_TIMELINE_COMMAND_CSV,
        as_list(tables.get("timeline_command_rows")),
        [
            "timeline_command_source_index",
            "cutscene_source_index",
            "scene_path",
            "setup_index",
            "local_command_index",
            "command_offset_hex",
            "command_id_hex",
            "command_name",
            "category",
            "entry_count",
            "camera_point_count",
            "payload_size",
            "total_size",
            "list_header_offset_hex",
            "list_header_word0_hex",
            "list_header_word1_hex",
            "list_header_param",
            "list_header_start_frame",
            "list_header_end_frame",
            "list_header_unused",
            "camera_point_ref_start",
            "camera_point_ref_count",
            "entry_ref_start",
            "entry_ref_count",
        ],
    )
    write_csv(
        DEFAULT_OUT_CAMERA_POINT_CSV,
        as_list(tables.get("camera_point_rows")),
        [
            "camera_point_source_index",
            "cutscene_source_index",
            "timeline_command_source_index",
            "scene_path",
            "setup_index",
            "command_local_index",
            "local_point_index",
            "point_offset_hex",
            "continue_flag",
            "camera_roll",
            "next_point_frame",
            "view_angle",
            "view_angle_bits_hex",
            "pos_x",
            "pos_y",
            "pos_z",
            "unused",
        ],
    )
    write_csv(
        DEFAULT_OUT_ENTRY_CSV,
        as_list(tables.get("entry_rows")),
        [
            "entry_source_index",
            "cutscene_source_index",
            "timeline_command_source_index",
            "scene_path",
            "setup_index",
            "command_local_index",
            "local_entry_index",
            "entry_offset_hex",
            "raw_word_count",
            "raw_words_text",
            "primary",
            "start_frame",
            "end_frame",
        ],
    )
    write_markdown(DEFAULT_OUT_MD, tables)
    write_header(DEFAULT_OUT_HEADER, tables)
    write_source(DEFAULT_OUT_SOURCE, tables)


def validate(tables: dict[str, Any]) -> list[str]:
    summary = as_dict(tables.get("summary"))
    cutscene_rows = [as_dict(row) for row in as_list(tables.get("cutscene_rows"))]
    header_rows = [as_dict(row) for row in as_list(tables.get("header_candidate_rows"))]
    command_rows = [as_dict(row) for row in as_list(tables.get("timeline_command_rows"))]
    point_rows = [as_dict(row) for row in as_list(tables.get("camera_point_rows"))]
    entry_rows = [as_dict(row) for row in as_list(tables.get("entry_rows"))]

    errors: list[str] = []
    expected_counts = [
        ("cutscene refs", len(cutscene_rows), int_value(summary.get("expected_cutscene_command_count"))),
        ("audit refs", len(cutscene_rows), int_value(summary.get("audit_cutscene_command_total"))),
        ("setting refs", len(cutscene_rows), int_value(summary.get("expected_cutscene_setting_count"))),
        ("timeline commands", len(command_rows), int_value(summary.get("timeline_command_row_count"))),
        ("camera points", len(point_rows), int_value(summary.get("camera_point_row_count"))),
        ("entries", len(entry_rows), int_value(summary.get("entry_row_count"))),
    ]
    for label, actual, expected in expected_counts:
        if actual != expected:
            errors.append(f"{label}: expected {expected}, got {actual}")

    if any(int_value(row.get("command_source_index"), UNMAPPED_INDEX) == UNMAPPED_INDEX for row in cutscene_rows):
        errors.append("one or more cutscene rows are missing command-source bindings")
    if any(int_value(row.get("setting_source_index"), UNMAPPED_INDEX) == UNMAPPED_INDEX for row in cutscene_rows):
        errors.append("one or more cutscene rows are missing setting-source bindings")

    for row in cutscene_rows:
        index = int_value(row.get("cutscene_source_index"))
        header_start = int_value(row.get("header_candidate_ref_start"))
        header_count = int_value(row.get("header_candidate_ref_count"))
        if header_start + header_count > len(header_rows):
            errors.append(f"cutscene {index}: header candidate slice outside table")
        elif any(int_value(header_rows[i].get("cutscene_source_index")) != index for i in range(header_start, header_start + header_count)):
            errors.append(f"cutscene {index}: header candidate slice owner mismatch")

        command_start = int_value(row.get("timeline_command_ref_start"))
        command_count = int_value(row.get("timeline_command_ref_count"))
        if command_start + command_count > len(command_rows):
            errors.append(f"cutscene {index}: timeline command slice outside table")
        elif any(int_value(command_rows[i].get("cutscene_source_index")) != index for i in range(command_start, command_start + command_count)):
            errors.append(f"cutscene {index}: timeline command slice owner mismatch")

        point_start = int_value(row.get("camera_point_ref_start"))
        point_count = int_value(row.get("camera_point_ref_count"))
        if point_start + point_count > len(point_rows):
            errors.append(f"cutscene {index}: camera point slice outside table")
        elif any(int_value(point_rows[i].get("cutscene_source_index")) != index for i in range(point_start, point_start + point_count)):
            errors.append(f"cutscene {index}: camera point slice owner mismatch")

        entry_start = int_value(row.get("entry_ref_start"))
        entry_count = int_value(row.get("entry_ref_count"))
        if entry_start + entry_count > len(entry_rows):
            errors.append(f"cutscene {index}: entry slice outside table")
        elif any(int_value(entry_rows[i].get("cutscene_source_index")) != index for i in range(entry_start, entry_start + entry_count)):
            errors.append(f"cutscene {index}: entry slice owner mismatch")

    for row in command_rows:
        command_index = int_value(row.get("timeline_command_source_index"))
        point_start = int_value(row.get("camera_point_ref_start"))
        point_count = int_value(row.get("camera_point_ref_count"))
        if point_start + point_count > len(point_rows):
            errors.append(f"timeline command {command_index}: camera point slice outside table")
        elif any(int_value(point_rows[i].get("timeline_command_source_index")) != command_index for i in range(point_start, point_start + point_count)):
            errors.append(f"timeline command {command_index}: camera point slice owner mismatch")

        entry_start = int_value(row.get("entry_ref_start"))
        entry_count = int_value(row.get("entry_ref_count"))
        if entry_start + entry_count > len(entry_rows):
            errors.append(f"timeline command {command_index}: entry slice outside table")
        elif any(int_value(entry_rows[i].get("timeline_command_source_index")) != command_index for i in range(entry_start, entry_start + entry_count)):
            errors.append(f"timeline command {command_index}: entry slice owner mismatch")

    if int_value(summary.get("strict_decoded_cutscene_count")) != sum(1 for row in cutscene_rows if row.get("strict_decoded")):
        errors.append("strict decoded count does not match cutscene rows")
    if int_value(summary.get("strict_decoded_cutscene_count")) != 9:
        errors.append("strict decoded count changed from the audited baseline of 9")

    return errors


def main() -> int:
    native_full_index = load_json(DEFAULT_NATIVE_FULL_INDEX)
    scene_root = Path(str(native_full_index["scene_root"]))
    tables = build_tables(scene_root)
    errors = validate(tables)
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1
    write_outputs(tables)
    summary = as_dict(tables.get("summary"))
    print(
        "wrote scene cutscene source table: "
        f"{summary.get('cutscene_source_row_count')} refs, "
        f"{summary.get('strict_decoded_cutscene_count')} strict decoded, "
        f"{summary.get('timeline_command_row_count')} commands, "
        f"{summary.get('camera_point_row_count')} camera points"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
