#!/usr/bin/env python3
"""Build source-oriented OOT3D scene collision header tables.

Native scene command 0x03 stores a scene-relative collision header pointer.
The handler at 0x00273070 relocates the collision header pointer fields at
+0x18..+0x28 and initializes the play-state collision/spatial partition
state. This generator promotes the decoded native collision header candidates
into source-like rows, while keeping every setup command as a separate
reference to the underlying header.
"""

from __future__ import annotations

import csv
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_NATIVE_FULL_INDEX = ROOT / "analysis" / "oot3d_native_scene_index_all_full.json"
DEFAULT_NATIVE_SOURCE_INDEX = ROOT / "analysis" / "oot3d_native_scene_source_index.json"
DEFAULT_SETUP_SOURCE_TABLE = ROOT / "analysis" / "scene_setup_source_table.json"
DEFAULT_COMMAND_SOURCE_TABLE = ROOT / "analysis" / "scene_command_source_table.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_collision_source_table.json"
DEFAULT_OUT_HEADER_CSV = ROOT / "analysis" / "scene_collision_header_source_table.csv"
DEFAULT_OUT_COMMAND_REF_CSV = ROOT / "analysis" / "scene_collision_command_ref_table.csv"
DEFAULT_OUT_WATER_CSV = ROOT / "analysis" / "scene_collision_water_source_table.csv"
DEFAULT_OUT_BGCAM_CSV = ROOT / "analysis" / "scene_collision_bgcam_source_table.csv"
DEFAULT_OUT_SURFACE_TYPE_CSV = ROOT / "analysis" / "scene_collision_surface_type_source_table.csv"
DEFAULT_OUT_USAGE_CSV = ROOT / "analysis" / "scene_collision_polygon_type_usage_table.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_collision_source_table.md"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "scene_collision_source_table.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_scene_collision_source_table.c"
ASSET_TOOL_SRC = ROOT.parent / "oot3d_asset_tool" / "src"

UNMAPPED_INDEX = 0xFFFF
UNMAPPED_SCENE_ID = 0xFF
NO_OFFSET = 0xFFFFFFFF
COMMAND_COLLISION_HEADER = 0x03


def as_dict(value: Any) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def as_list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def int_value(value: Any, default: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def bool_value(value: Any) -> bool:
    return bool(value)


def c_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def c_bool(value: Any) -> str:
    return "1u" if bool_value(value) else "0u"


def c_u8(value: Any) -> str:
    return f"{int_value(value) & 0xFF}u"


def c_s16(value: Any) -> str:
    return str(max(-32768, min(32767, int_value(value))))


def c_u16(value: Any) -> str:
    return f"{int_value(value) & 0xFFFF}u"


def c_u32(value: Any) -> str:
    return f"{int_value(value) & 0xFFFFFFFF}u"


def parse_handler_entry(value: Any) -> int:
    text = str(value or "")
    if not text:
        return 0
    return int(text, 16)


def vec3(value: Any) -> tuple[int, int, int]:
    values = as_list(value)
    if len(values) < 3:
        return (0, 0, 0)
    return (int_value(values[0]), int_value(values[1]), int_value(values[2]))


def hex_offset(value: Any) -> str:
    offset = int_value(value, NO_OFFSET)
    if offset == NO_OFFSET:
        return ""
    return f"0x{offset:08x}"


def payload_sample_is_zero_stub(command: dict[str, Any]) -> bool:
    raw_hex = str(command.get("payload_sample_hex", ""))
    if len(raw_hex) < 0x20 * 2:
        return False
    try:
        raw = bytes.fromhex(raw_hex[: 0x20 * 2])
    except ValueError:
        return False
    return all(byte == 0 for byte in raw)


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
        if scene_path and setup_index >= 0 and command_id == COMMAND_COLLISION_HEADER:
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
                "handler_runtime_store_count": int_value(row.get("handler_runtime_store_count"), 0),
                "payload_symbol": row.get("payload_symbol", ""),
                "export_structs": row.get("export_structs", ""),
                "open_questions": row.get("open_questions", ""),
            }
    return bindings


def command_source_rows(payload: dict[str, Any]) -> list[dict[str, Any]]:
    return [
        row
        for row in as_list(payload.get("command_rows"))
        if isinstance(row, dict) and int_value(row.get("command_id"), -1) == COMMAND_COLLISION_HEADER
    ]


def full_command_details(payload: dict[str, Any]) -> dict[tuple[str, int], dict[str, Any]]:
    details: dict[tuple[str, int], dict[str, Any]] = {}
    for scene in as_list(payload.get("records")):
        if not isinstance(scene, dict):
            continue
        scene_key = str(scene.get("scene_path", "")).lower()
        for setup in as_list(scene.get("setups")):
            if not isinstance(setup, dict):
                continue
            setup_index = int_value(setup.get("index"), -1)
            for command in as_list(setup.get("commands")):
                if not isinstance(command, dict):
                    continue
                if int_value(command.get("command_id"), -1) == COMMAND_COLLISION_HEADER:
                    details[(scene_key, setup_index)] = command
    return details


def binding_enum(value: str) -> str:
    return {
        "scene_row": "OOT3D_SCENE_COMMAND_BINDING_SCENE_ROW",
        "variant_row": "OOT3D_SCENE_COMMAND_BINDING_VARIANT_ROW",
        "unmapped": "OOT3D_SCENE_COMMAND_BINDING_UNMAPPED",
    }.get(value, "OOT3D_SCENE_COMMAND_BINDING_UNMAPPED")


def ref_status_enum(value: str) -> str:
    return {
        "native_collision_header_command_reference_resolved": "OOT3D_COLLISION_COMMAND_REF_RESOLVED",
        "native_collision_header_zero_stub_unpromoted": "OOT3D_COLLISION_COMMAND_REF_ZERO_STUB_UNPROMOTED",
        "native_collision_header_reference_missing_candidate": "OOT3D_COLLISION_COMMAND_REF_MISSING_CANDIDATE",
    }.get(value, "OOT3D_COLLISION_COMMAND_REF_MISSING_CANDIDATE")


def collision_validation_status(candidate: dict[str, Any]) -> str:
    evidence = " ".join(str(item) for item in as_list(candidate.get("evidence")))
    has_marker = "marker 0x" in evidence
    has_relocation = "pointers at +0x18/+0x1c/+0x20/+0x24/+0x28" in evidence
    has_bgcam = "bg camera count matches" in evidence
    if has_marker and has_relocation and has_bgcam:
        return "native_collision_header_code_bin_handler_confirmed_effective_tables_selected"
    if has_relocation:
        return "native_collision_header_code_bin_handler_confirmed"
    return "native_collision_header_candidate_unclassified"


def build_surface_type_rows(
    full_payload: dict[str, Any],
    header_rows: list[dict[str, Any]],
    polygon_usage_rows: list[dict[str, Any]],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    if str(ASSET_TOOL_SRC) not in sys.path:
        sys.path.insert(0, str(ASSET_TOOL_SRC))
    from oot3d_asset_tool.zsi import ZsiFile  # noqa: PLC0415

    scene_root = Path(str(full_payload.get("scene_root", "")))
    parse_errors: list[dict[str, Any]] = []
    candidates_by_key: dict[tuple[str, int], Any] = {}
    scene_paths = sorted({str(row.get("scene_path", "")) for row in header_rows if row.get("scene_path")})
    for scene_path in scene_paths:
        zsi_path = scene_root / scene_path
        try:
            zsi = ZsiFile.from_path(zsi_path)
            candidates = zsi.collision_header_candidates()
        except Exception as exc:  # pragma: no cover - diagnostic export path
            parse_errors.append({"scene_path": scene_path, "stage": "zsi_collision_surface_types", "error": str(exc)})
            continue
        for candidate in candidates:
            candidates_by_key[(scene_path.lower(), int(candidate.command_argument))] = candidate

    usage_by_header: dict[int, dict[int, int]] = defaultdict(dict)
    for row in polygon_usage_rows:
        collision_source_index = int_value(row.get("collision_source_index"), UNMAPPED_INDEX)
        surface_type = int_value(row.get("surface_type"), UNMAPPED_INDEX)
        usage_by_header[collision_source_index][surface_type] = int_value(row.get("polygon_count"))

    surface_type_rows: list[dict[str, Any]] = []
    for header in header_rows:
        collision_source_index = int_value(header.get("collision_source_index"), UNMAPPED_INDEX)
        scene_path = str(header.get("scene_path", ""))
        command_argument = int_value(header.get("command_argument"), NO_OFFSET)
        candidate = candidates_by_key.get((scene_path.lower(), command_argument))
        if candidate is None:
            parse_errors.append(
                {
                    "collision_source_index": collision_source_index,
                    "scene_path": scene_path,
                    "command_argument": command_argument,
                    "stage": "candidate_lookup",
                    "error": "decoded collision candidate not found while exporting surface types",
                }
            )
            continue
        for surface_type_index, surface_type in enumerate(candidate.effective_surface_types):
            offset = int_value(header.get("effective_surface_type_offset"), NO_OFFSET) + surface_type_index * 8
            surface_type_rows.append(
                {
                    "surface_type_source_index": len(surface_type_rows),
                    "collision_source_index": collision_source_index,
                    "surface_type_index": surface_type_index,
                    "offset": offset,
                    "offset_hex": hex_offset(offset),
                    "data1": int(surface_type.data1),
                    "data1_hex": f"0x{int(surface_type.data1) & 0xFFFFFFFF:08x}",
                    "data2": int(surface_type.data2),
                    "data2_hex": f"0x{int(surface_type.data2) & 0xFFFFFFFF:08x}",
                    "referenced_polygon_count": usage_by_header[collision_source_index].get(surface_type_index, 0),
                }
            )
    return surface_type_rows, parse_errors


def build_rows(
    full_payload: dict[str, Any],
    setup_bindings: dict[tuple[str, int], dict[str, Any]],
    command_bindings: dict[tuple[str, int], dict[str, Any]],
    command_rows: list[dict[str, Any]],
    command_details: dict[tuple[str, int], dict[str, Any]],
) -> tuple[
    list[dict[str, Any]],
    list[dict[str, Any]],
    list[dict[str, Any]],
    list[dict[str, Any]],
    list[dict[str, Any]],
]:
    header_rows: list[dict[str, Any]] = []
    command_ref_rows: list[dict[str, Any]] = []
    water_rows: list[dict[str, Any]] = []
    bgcam_rows: list[dict[str, Any]] = []
    polygon_usage_rows: list[dict[str, Any]] = []

    header_by_scene_arg: dict[tuple[str, int], int] = {}
    command_refs_by_header: dict[int, list[int]] = defaultdict(list)

    for scene in as_list(full_payload.get("records")):
        if not isinstance(scene, dict):
            continue
        scene_path = str(scene.get("scene_path", ""))
        scene_key = scene_path.lower()
        for candidate in as_list(scene.get("collision_header_candidates")):
            if not isinstance(candidate, dict):
                continue
            setup_index = int_value(candidate.get("setup_index"), -1)
            setup_binding = setup_bindings.get((scene_key, setup_index), {})
            command_binding = command_bindings.get((scene_key, setup_index), {})
            collision_source_index = len(header_rows)
            header_by_scene_arg[(scene_key, int_value(candidate.get("command_argument"), NO_OFFSET))] = (
                collision_source_index
            )

            water_ref_start = len(water_rows)
            for water_index, water_box in enumerate(as_list(candidate.get("effective_water_boxes"))):
                if not isinstance(water_box, dict):
                    continue
                water_rows.append(
                    {
                        "water_source_index": len(water_rows),
                        "collision_source_index": collision_source_index,
                        "water_index": water_index,
                        "x_min": int_value(water_box.get("x_min")),
                        "y_surface": int_value(water_box.get("y_surface")),
                        "z_min": int_value(water_box.get("z_min")),
                        "x_length": int_value(water_box.get("x_length")),
                        "z_length": int_value(water_box.get("z_length")),
                        "properties": int_value(water_box.get("properties")),
                        "plausible": bool_value(water_box.get("plausible")),
                    }
                )
            water_ref_count = len(water_rows) - water_ref_start

            bgcam_ref_start = len(bgcam_rows)
            for bgcam_index, bgcam in enumerate(as_list(candidate.get("effective_bg_cam_info"))):
                if not isinstance(bgcam, dict):
                    continue
                bgcam_rows.append(
                    {
                        "bgcam_source_index": len(bgcam_rows),
                        "collision_source_index": collision_source_index,
                        "bgcam_index": bgcam_index,
                        "setting": int_value(bgcam.get("setting")),
                        "count": int_value(bgcam.get("count")),
                        "data_offset": int_value(bgcam.get("data_offset")),
                        "data_offset_hex": hex_offset(bgcam.get("data_offset")),
                    }
                )
            bgcam_ref_count = len(bgcam_rows) - bgcam_ref_start

            polygon_usage_ref_start = len(polygon_usage_rows)
            for usage_index, usage in enumerate(as_list(candidate.get("polygon_type_usage"))):
                if not isinstance(usage, dict):
                    continue
                polygon_usage_rows.append(
                    {
                        "polygon_usage_source_index": len(polygon_usage_rows),
                        "collision_source_index": collision_source_index,
                        "usage_index": usage_index,
                        "surface_type": int_value(usage.get("type")),
                        "polygon_count": int_value(usage.get("count")),
                    }
                )
            polygon_usage_ref_count = len(polygon_usage_rows) - polygon_usage_ref_start

            bounds_min = vec3(candidate.get("bounds_min"))
            bounds_max = vec3(candidate.get("bounds_max"))
            polygons_sample = as_list(candidate.get("polygons_sample"))
            first_polygon = as_dict(polygons_sample[0]) if polygons_sample else {}
            marker = int_value(first_polygon.get("marker"), 0)
            validation_status = collision_validation_status(candidate)
            header_rows.append(
                {
                    "collision_source_index": collision_source_index,
                    "scene_binding_kind": setup_binding.get("scene_binding_kind", "unmapped"),
                    "scene_binding_index": int_value(setup_binding.get("scene_binding_index"), UNMAPPED_INDEX),
                    "scene_id": int_value(setup_binding.get("scene_id"), UNMAPPED_SCENE_ID),
                    "scene_id_hex": setup_binding.get("scene_id_hex", ""),
                    "setup_source_index": int_value(setup_binding.get("setup_source_index"), UNMAPPED_INDEX),
                    "command_source_index": int_value(command_binding.get("command_source_index"), UNMAPPED_INDEX),
                    "scene_path": scene_path,
                    "source_basename": setup_binding.get("source_basename", ""),
                    "source_c_file": setup_binding.get("source_c_file", ""),
                    "scene_index_symbol": setup_binding.get("scene_index_symbol", ""),
                    "setup_index": setup_index,
                    "setup_role": setup_binding.get("setup_role", ""),
                    "setup_symbol": setup_binding.get("setup_symbol", ""),
                    "command_index": int_value(command_binding.get("command_index"), UNMAPPED_INDEX),
                    "command_offset": int_value(candidate.get("command_offset"), NO_OFFSET),
                    "command_offset_hex": hex_offset(candidate.get("command_offset")),
                    "command_argument": int_value(candidate.get("command_argument"), NO_OFFSET),
                    "command_argument_hex": hex_offset(candidate.get("command_argument")),
                    "header_offset": int_value(candidate.get("offset"), NO_OFFSET),
                    "header_offset_hex": hex_offset(candidate.get("offset")),
                    "bounds_min_x": bounds_min[0],
                    "bounds_min_y": bounds_min[1],
                    "bounds_min_z": bounds_min[2],
                    "bounds_max_x": bounds_max[0],
                    "bounds_max_y": bounds_max[1],
                    "bounds_max_z": bounds_max[2],
                    "vertex_count": int_value(candidate.get("vertex_count")),
                    "raw_polygon_count": int_value(candidate.get("raw_polygon_count")),
                    "effective_polygon_count": int_value(candidate.get("effective_polygon_count")),
                    "surface_type_count": int_value(candidate.get("surface_type_count")),
                    "bgcam_count": int_value(candidate.get("bgcam_count")),
                    "water_box_count": int_value(candidate.get("water_box_count")),
                    "vertex_offset": int_value(candidate.get("vertex_offset"), NO_OFFSET),
                    "effective_vertex_offset": int_value(candidate.get("effective_vertex_offset"), NO_OFFSET),
                    "polygon_offset": int_value(candidate.get("polygon_offset"), NO_OFFSET),
                    "effective_polygon_offset": int_value(candidate.get("effective_polygon_offset"), NO_OFFSET),
                    "surface_type_offset": int_value(candidate.get("surface_type_offset"), NO_OFFSET),
                    "effective_surface_type_offset": int_value(
                        candidate.get("effective_surface_type_offset"), NO_OFFSET
                    ),
                    "bgcam_offset": int_value(candidate.get("bgcam_offset"), NO_OFFSET),
                    "effective_bgcam_offset": int_value(candidate.get("effective_bgcam_offset"), NO_OFFSET),
                    "camera_position_offset": int_value(candidate.get("camera_position_offset"), NO_OFFSET),
                    "camera_pointer_adjustment": int_value(candidate.get("camera_pointer_adjustment")),
                    "water_boxes_offset": int_value(candidate.get("water_boxes_offset"), NO_OFFSET),
                    "polygon_marker": marker,
                    "command_ref_start": 0,
                    "command_ref_count": 0,
                    "water_ref_start": water_ref_start,
                    "water_ref_count": water_ref_count,
                    "bgcam_ref_start": bgcam_ref_start,
                    "bgcam_ref_count": bgcam_ref_count,
                    "polygon_usage_ref_start": polygon_usage_ref_start,
                    "polygon_usage_ref_count": polygon_usage_ref_count,
                    "validation_status": validation_status,
                    "evidence_text": " | ".join(str(item) for item in as_list(candidate.get("evidence"))),
                    "open_questions": str(command_binding.get("open_questions", "")),
                    "handler_name": command_binding.get("handler_name", ""),
                    "handler_entry": command_binding.get("handler_entry", ""),
                    "handler_support_level": command_binding.get("handler_support_level", ""),
                }
            )

    for command_row in command_rows:
        scene_path = str(command_row.get("scene_path", ""))
        scene_key = scene_path.lower()
        setup_index = int_value(command_row.get("setup_index"), -1)
        setup_binding = setup_bindings.get((scene_key, setup_index), {})
        command_detail = command_details.get((scene_key, setup_index), {})
        argument = int_value(command_row.get("argument"), int_value(command_detail.get("argument"), NO_OFFSET))
        collision_source_index = header_by_scene_arg.get((scene_key, argument), UNMAPPED_INDEX)
        if collision_source_index != UNMAPPED_INDEX:
            reference_status = "native_collision_header_command_reference_resolved"
            open_questions = str(command_row.get("open_questions", ""))
            command_refs_by_header[collision_source_index].append(len(command_ref_rows))
        elif payload_sample_is_zero_stub(command_detail):
            reference_status = "native_collision_header_zero_stub_unpromoted"
            open_questions = (
                "Command points to a zero-filled native payload window; confirm whether this is a deliberate "
                "null/disabled collision header or a setup-specific header not yet decoded."
            )
        else:
            reference_status = "native_collision_header_reference_missing_candidate"
            open_questions = "Decode/promote this command argument as a collision header candidate from native ZSI."

        command_ref_rows.append(
            {
                "collision_command_ref_index": len(command_ref_rows),
                "collision_source_index": collision_source_index,
                "scene_binding_kind": setup_binding.get("scene_binding_kind", "unmapped"),
                "scene_binding_index": int_value(setup_binding.get("scene_binding_index"), UNMAPPED_INDEX),
                "scene_id": int_value(setup_binding.get("scene_id"), UNMAPPED_SCENE_ID),
                "scene_id_hex": setup_binding.get("scene_id_hex", ""),
                "setup_source_index": int_value(setup_binding.get("setup_source_index"), UNMAPPED_INDEX),
                "command_source_index": int_value(command_row.get("command_source_index"), UNMAPPED_INDEX),
                "scene_path": scene_path,
                "source_basename": str(command_row.get("source_basename", "")),
                "scene_index_symbol": str(command_row.get("scene_index_symbol", "")),
                "setup_index": setup_index,
                "setup_role": setup_binding.get("setup_role", ""),
                "command_index": int_value(command_row.get("command_index"), UNMAPPED_INDEX),
                "command_offset": int_value(command_row.get("offset"), NO_OFFSET),
                "command_offset_hex": str(command_row.get("offset_hex", "")),
                "command_argument": argument,
                "command_argument_hex": f"0x{argument:08x}" if argument != NO_OFFSET else "",
                "reference_status": reference_status,
                "decoded_status": str(command_row.get("decoded_status", "")),
                "support_level": str(command_row.get("support_level", "")),
                "handler_support_level": str(command_row.get("handler_support_level", "")),
                "handler_name": str(command_row.get("handler_name", "")),
                "handler_entry": str(command_row.get("handler_entry", "")),
                "open_questions": open_questions,
            }
        )

    for collision_source_index, ref_indices in command_refs_by_header.items():
        if not ref_indices:
            continue
        header_rows[collision_source_index]["command_ref_start"] = min(ref_indices)
        header_rows[collision_source_index]["command_ref_count"] = len(ref_indices)

    return header_rows, command_ref_rows, water_rows, bgcam_rows, polygon_usage_rows


def build_report() -> dict[str, Any]:
    full_payload = json.loads(DEFAULT_NATIVE_FULL_INDEX.read_text(encoding="utf-8"))
    setup_payload = json.loads(DEFAULT_SETUP_SOURCE_TABLE.read_text(encoding="utf-8"))
    command_payload = json.loads(DEFAULT_COMMAND_SOURCE_TABLE.read_text(encoding="utf-8"))

    setup_bindings = setup_source_bindings(setup_payload)
    command_bindings = command_source_bindings(command_payload)
    collision_command_rows = command_source_rows(command_payload)
    command_details = full_command_details(full_payload)
    header_rows, command_ref_rows, water_rows, bgcam_rows, polygon_usage_rows = build_rows(
        full_payload,
        setup_bindings,
        command_bindings,
        collision_command_rows,
        command_details,
    )
    surface_type_rows, surface_type_parse_errors = build_surface_type_rows(
        full_payload,
        header_rows,
        polygon_usage_rows,
    )
    surface_type_ref_starts: dict[int, int] = {}
    surface_type_ref_counts: Counter[int] = Counter()
    for row in surface_type_rows:
        collision_source_index = int_value(row.get("collision_source_index"), UNMAPPED_INDEX)
        surface_type_ref_starts.setdefault(collision_source_index, int_value(row.get("surface_type_source_index")))
        surface_type_ref_counts[collision_source_index] += 1
    for row in header_rows:
        collision_source_index = int_value(row.get("collision_source_index"), UNMAPPED_INDEX)
        row["surface_type_ref_start"] = surface_type_ref_starts.get(collision_source_index, 0)
        row["surface_type_ref_count"] = surface_type_ref_counts.get(collision_source_index, 0)

    scene_binding_counts = Counter(str(row.get("scene_binding_kind", "")) for row in header_rows)
    ref_status_counts = Counter(str(row.get("reference_status", "")) for row in command_ref_rows)
    validation_counts = Counter(str(row.get("validation_status", "")) for row in header_rows)
    handler_support_counts = Counter(str(row.get("handler_support_level", "")) for row in header_rows)
    command_support_counts = Counter(str(row.get("support_level", "")) for row in command_ref_rows)
    water_plausibility_counts = Counter("plausible" if row.get("plausible") else "implausible" for row in water_rows)
    header_scene_counts = Counter(str(row.get("scene_path", "")) for row in header_rows)
    ref_scene_counts = Counter(str(row.get("scene_path", "")) for row in command_ref_rows)

    unresolved_command_refs = [
        row for row in command_ref_rows if int_value(row.get("collision_source_index"), UNMAPPED_INDEX) == UNMAPPED_INDEX
    ]
    unmapped_header_setup_rows = [
        row for row in header_rows if int_value(row.get("setup_source_index"), UNMAPPED_INDEX) == UNMAPPED_INDEX
    ]
    unmapped_header_command_rows = [
        row for row in header_rows if int_value(row.get("command_source_index"), UNMAPPED_INDEX) == UNMAPPED_INDEX
    ]
    unmapped_ref_setup_rows = [
        row for row in command_ref_rows if int_value(row.get("setup_source_index"), UNMAPPED_INDEX) == UNMAPPED_INDEX
    ]
    unmapped_ref_command_rows = [
        row for row in command_ref_rows if int_value(row.get("command_source_index"), UNMAPPED_INDEX) == UNMAPPED_INDEX
    ]

    expected_collision_command_count = len(collision_command_rows)
    command_ref_count_mismatch_count = 0 if expected_collision_command_count == len(command_ref_rows) else 1
    resolved_command_ref_count = len(command_ref_rows) - len(unresolved_command_refs)
    surface_type_rows_by_header = Counter(
        int_value(row.get("collision_source_index"), UNMAPPED_INDEX) for row in surface_type_rows
    )
    surface_type_count_mismatches = [
        {
            "collision_source_index": int_value(row.get("collision_source_index"), UNMAPPED_INDEX),
            "scene_path": row.get("scene_path", ""),
            "expected_surface_type_count": int_value(row.get("surface_type_count")),
            "actual_surface_type_count": surface_type_rows_by_header.get(
                int_value(row.get("collision_source_index"), UNMAPPED_INDEX),
                0,
            ),
        }
        for row in header_rows
        if int_value(row.get("surface_type_count"))
        != surface_type_rows_by_header.get(int_value(row.get("collision_source_index"), UNMAPPED_INDEX), 0)
    ]
    spot04_headers = [row for row in header_rows if row.get("scene_path") == "spot04_info.zsi"]
    spot04_refs = [row for row in command_ref_rows if row.get("scene_path") == "spot04_info.zsi"]

    summary = {
        "format": "oot3d_scene_collision_source_table_v1",
        "collision_header_source_row_count": len(header_rows),
        "collision_command_ref_row_count": len(command_ref_rows),
        "collision_water_source_row_count": len(water_rows),
        "collision_bgcam_source_row_count": len(bgcam_rows),
        "collision_surface_type_source_row_count": len(surface_type_rows),
        "collision_polygon_type_usage_row_count": len(polygon_usage_rows),
        "expected_collision_command_count": expected_collision_command_count,
        "resolved_command_ref_count": resolved_command_ref_count,
        "unresolved_command_ref_count": len(unresolved_command_refs),
        "command_ref_count_mismatch_count": command_ref_count_mismatch_count,
        "surface_type_parse_error_count": len(surface_type_parse_errors),
        "surface_type_count_mismatch_count": len(surface_type_count_mismatches),
        "unmapped_header_setup_count": len(unmapped_header_setup_rows),
        "unmapped_header_command_count": len(unmapped_header_command_rows),
        "unmapped_ref_setup_count": len(unmapped_ref_setup_rows),
        "unmapped_ref_command_count": len(unmapped_ref_command_rows),
        "scene_binding_kind_counts": dict(sorted(scene_binding_counts.items())),
        "command_reference_status_counts": dict(sorted(ref_status_counts.items())),
        "validation_status_counts": dict(sorted(validation_counts.items())),
        "handler_support_level_counts": dict(sorted(handler_support_counts.items())),
        "command_support_level_counts": dict(sorted(command_support_counts.items())),
        "water_plausibility_counts": dict(sorted(water_plausibility_counts.items())),
        "total_vertex_count": sum(int_value(row.get("vertex_count")) for row in header_rows),
        "total_effective_polygon_count": sum(int_value(row.get("effective_polygon_count")) for row in header_rows),
        "total_surface_type_count": sum(int_value(row.get("surface_type_count")) for row in header_rows),
        "top_header_scene_counts": dict(header_scene_counts.most_common(20)),
        "top_command_ref_scene_counts": dict(ref_scene_counts.most_common(20)),
        "spot04_header_count": len(spot04_headers),
        "spot04_command_ref_count": len(spot04_refs),
    }
    return {
        "summary": summary,
        "source_policy": {
            "native_full_scene_index": str(DEFAULT_NATIVE_FULL_INDEX),
            "native_scene_source_index": str(DEFAULT_NATIVE_SOURCE_INDEX),
            "setup_source_table": str(DEFAULT_SETUP_SOURCE_TABLE),
            "command_source_table": str(DEFAULT_COMMAND_SOURCE_TABLE),
            "promoted_layout": (
                "OOT3D command 0x03 collision header references: header bounds/counts at the selected "
                "native header offset, relocated pointer fields at +0x18..+0x28, effective prefixed "
                "tables preserved as decoded offsets and child-row slices."
            ),
            "code_bin_evidence": (
                "Scene command handler 0x00273070 relocates collision-header pointer fields, relocates "
                "nested camera records reached through header+0x24, and initializes play+0x0A98 "
                "collision/spatial partition state."
            ),
            "n64_policy": "No N64 collision tables are read by this integration table.",
        },
        "header_rows": header_rows,
        "command_ref_rows": command_ref_rows,
        "water_rows": water_rows,
        "bgcam_rows": bgcam_rows,
        "surface_type_rows": surface_type_rows,
        "polygon_type_usage_rows": polygon_usage_rows,
        "unresolved_command_refs": unresolved_command_refs,
        "surface_type_parse_errors": surface_type_parse_errors,
        "surface_type_count_mismatches": surface_type_count_mismatches,
        "unmapped_header_setup_rows": unmapped_header_setup_rows,
        "unmapped_header_command_rows": unmapped_header_command_rows,
        "unmapped_ref_setup_rows": unmapped_ref_setup_rows,
        "unmapped_ref_command_rows": unmapped_ref_command_rows,
    }


HEADER_CSV_COLUMNS = [
    "collision_source_index",
    "scene_id_hex",
    "scene_id",
    "scene_path",
    "setup_source_index",
    "setup_index",
    "setup_role",
    "command_source_index",
    "command_index",
    "command_offset_hex",
    "command_argument_hex",
    "header_offset_hex",
    "bounds_min_x",
    "bounds_min_y",
    "bounds_min_z",
    "bounds_max_x",
    "bounds_max_y",
    "bounds_max_z",
    "vertex_count",
    "raw_polygon_count",
    "effective_polygon_count",
    "surface_type_count",
    "bgcam_count",
    "water_box_count",
    "vertex_offset",
    "effective_vertex_offset",
    "polygon_offset",
    "effective_polygon_offset",
    "surface_type_offset",
    "effective_surface_type_offset",
    "bgcam_offset",
    "effective_bgcam_offset",
    "camera_position_offset",
    "camera_pointer_adjustment",
    "water_boxes_offset",
    "polygon_marker",
    "command_ref_start",
    "command_ref_count",
    "water_ref_start",
    "water_ref_count",
    "bgcam_ref_start",
    "bgcam_ref_count",
    "surface_type_ref_start",
    "surface_type_ref_count",
    "polygon_usage_ref_start",
    "polygon_usage_ref_count",
    "validation_status",
    "scene_binding_kind",
    "scene_binding_index",
    "handler_name",
    "handler_support_level",
]

COMMAND_REF_CSV_COLUMNS = [
    "collision_command_ref_index",
    "collision_source_index",
    "scene_id_hex",
    "scene_id",
    "scene_path",
    "setup_source_index",
    "setup_index",
    "setup_role",
    "command_source_index",
    "command_index",
    "command_offset_hex",
    "command_argument_hex",
    "reference_status",
    "decoded_status",
    "support_level",
    "handler_support_level",
    "scene_binding_kind",
    "scene_binding_index",
    "open_questions",
]

WATER_CSV_COLUMNS = [
    "water_source_index",
    "collision_source_index",
    "water_index",
    "x_min",
    "y_surface",
    "z_min",
    "x_length",
    "z_length",
    "properties",
    "plausible",
]

BGCAM_CSV_COLUMNS = [
    "bgcam_source_index",
    "collision_source_index",
    "bgcam_index",
    "setting",
    "count",
    "data_offset_hex",
    "data_offset",
]

SURFACE_TYPE_CSV_COLUMNS = [
    "surface_type_source_index",
    "collision_source_index",
    "surface_type_index",
    "offset_hex",
    "offset",
    "data1_hex",
    "data1",
    "data2_hex",
    "data2",
    "referenced_polygon_count",
]

USAGE_CSV_COLUMNS = [
    "polygon_usage_source_index",
    "collision_source_index",
    "usage_index",
    "surface_type",
    "polygon_count",
]


def csv_value(value: Any) -> str:
    if isinstance(value, list):
        return "; ".join(str(item) for item in value)
    if isinstance(value, dict):
        return json.dumps(value, sort_keys=True)
    return str(value)


def write_csv(path: Path, columns: list[str], rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=columns, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            writer.writerow({key: csv_value(row.get(key, "")) for key in columns})


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_markdown(path: Path, payload: dict[str, Any]) -> None:
    summary = payload["summary"]
    lines = [
        "# Scene Collision Source Table",
        "",
        "Generated from OOT3D-native command 0x03 collision-header references.",
        "",
        "## Summary",
        "",
        f"- Collision header source rows: {summary['collision_header_source_row_count']}",
        f"- Collision command reference rows: {summary['collision_command_ref_row_count']}",
        f"- Expected collision commands: {summary['expected_collision_command_count']}",
        f"- Resolved command references: {summary['resolved_command_ref_count']}",
        f"- Unresolved command references: {summary['unresolved_command_ref_count']}",
        f"- Water box source rows: {summary['collision_water_source_row_count']}",
        f"- BgCam source rows: {summary['collision_bgcam_source_row_count']}",
        f"- Surface type source rows: {summary['collision_surface_type_source_row_count']}",
        f"- Polygon type usage rows: {summary['collision_polygon_type_usage_row_count']}",
        f"- Scene binding: `{json.dumps(summary['scene_binding_kind_counts'], sort_keys=True)}`",
        f"- Command reference status: `{json.dumps(summary['command_reference_status_counts'], sort_keys=True)}`",
        f"- Validation status: `{json.dumps(summary['validation_status_counts'], sort_keys=True)}`",
        f"- Handler support level: `{json.dumps(summary['handler_support_level_counts'], sort_keys=True)}`",
        f"- Water plausibility: `{json.dumps(summary['water_plausibility_counts'], sort_keys=True)}`",
        f"- Surface type parse errors: {summary['surface_type_parse_error_count']}",
        f"- Surface type count mismatches: {summary['surface_type_count_mismatch_count']}",
        f"- Total vertices: {summary['total_vertex_count']}",
        f"- Total effective polygons: {summary['total_effective_polygon_count']}",
        f"- Total surface types: {summary['total_surface_type_count']}",
        f"- `spot04_info.zsi` headers/refs: {summary['spot04_header_count']}/{summary['spot04_command_ref_count']}",
        "",
        "## Header Rows",
        "",
        "| # | Scene | Setup | Header | Bounds Min | Bounds Max | Vtx | Poly | Surf | BgCam | Water | Refs | Status |",
        "| ---: | --- | ---: | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |",
    ]
    for row in payload["header_rows"]:
        bounds_min = f"{row['bounds_min_x']},{row['bounds_min_y']},{row['bounds_min_z']}"
        bounds_max = f"{row['bounds_max_x']},{row['bounds_max_y']},{row['bounds_max_z']}"
        lines.append(
            f"| {row['collision_source_index']} | `{row['scene_path']}` | {row['setup_index']} | "
            f"`{row['header_offset_hex']}` | `{bounds_min}` | `{bounds_max}` | "
            f"{row['vertex_count']} | {row['effective_polygon_count']} | {row['surface_type_count']} | "
            f"{row['bgcam_count']} | {row['water_box_count']} | {row['command_ref_count']} | "
            f"`{row['validation_status']}` |"
        )

    lines.extend(
        [
            "",
            "## Unresolved Command References",
            "",
            "| Ref | Scene | Setup | Arg | Status | Question |",
            "| ---: | --- | ---: | --- | --- | --- |",
        ]
    )
    for row in payload["unresolved_command_refs"]:
        lines.append(
            f"| {row['collision_command_ref_index']} | `{row['scene_path']}` | {row['setup_index']} | "
            f"`{row['command_argument_hex']}` | `{row['reference_status']}` | {row['open_questions']} |"
        )

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_header(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "#ifndef OOT3D_SCENE_COLLISION_SOURCE_TABLE_H",
        "#define OOT3D_SCENE_COLLISION_SOURCE_TABLE_H",
        "",
        '#include "oot3d/scene.h"',
        '#include "oot3d/scene_command_source_table.h"',
        "",
        "enum {",
        f"    OOT3D_SCENE_COLLISION_HEADER_SOURCE_ROW_COUNT = {len(payload['header_rows'])},",
        f"    OOT3D_SCENE_COLLISION_COMMAND_REF_ROW_COUNT = {len(payload['command_ref_rows'])},",
        f"    OOT3D_SCENE_COLLISION_WATER_SOURCE_ROW_COUNT = {len(payload['water_rows'])},",
        f"    OOT3D_SCENE_COLLISION_BGCAM_SOURCE_ROW_COUNT = {len(payload['bgcam_rows'])},",
        f"    OOT3D_SCENE_COLLISION_SURFACE_TYPE_SOURCE_ROW_COUNT = {len(payload['surface_type_rows'])},",
        f"    OOT3D_SCENE_COLLISION_POLYGON_TYPE_USAGE_ROW_COUNT = {len(payload['polygon_type_usage_rows'])},",
        "    OOT3D_SCENE_COLLISION_SOURCE_UNMAPPED_INDEX = 0xFFFF,",
        "    OOT3D_SCENE_COLLISION_SOURCE_NO_OFFSET = 0xFFFFFFFF,",
        "};",
        "",
        "typedef enum {",
        "    OOT3D_COLLISION_COMMAND_REF_RESOLVED,",
        "    OOT3D_COLLISION_COMMAND_REF_ZERO_STUB_UNPROMOTED,",
        "    OOT3D_COLLISION_COMMAND_REF_MISSING_CANDIDATE,",
        "} Oot3dCollisionCommandRefStatus;",
        "",
        "typedef struct {",
        "    u16 collisionCommandRefIndex;",
        "    u16 collisionSourceIndex;",
        "    Oot3dSceneCommandBindingKind bindingKind;",
        "    u16 bindingIndex;",
        "    u8 sceneId;",
        "    u16 setupSourceIndex;",
        "    u16 commandSourceIndex;",
        "    u16 setupIndex;",
        "    u16 commandIndex;",
        "    u32 commandOffset;",
        "    u32 commandArgument;",
        "    Oot3dCollisionCommandRefStatus status;",
        "    const char* scenePath;",
        "    const char* sourceBasename;",
        "    const char* setupRole;",
        "    const char* validationStatus;",
        "    const char* openQuestions;",
        "    const char* handlerName;",
        "    u32 handlerEntry;",
        "} Oot3dSceneCollisionCommandRefRow;",
        "",
        "typedef struct {",
        "    u16 waterSourceIndex;",
        "    u16 collisionSourceIndex;",
        "    u16 waterIndex;",
        "    s16 xMin;",
        "    s16 ySurface;",
        "    s16 zMin;",
        "    s16 xLength;",
        "    s16 zLength;",
        "    u32 properties;",
        "    u8 plausible;",
        "} Oot3dSceneCollisionWaterBoxSourceRow;",
        "",
        "typedef struct {",
        "    u16 bgcamSourceIndex;",
        "    u16 collisionSourceIndex;",
        "    u16 bgcamIndex;",
        "    u16 setting;",
        "    u16 count;",
        "    u32 dataOffset;",
        "} Oot3dSceneCollisionBgCamSourceRow;",
        "",
        "typedef struct {",
        "    u16 surfaceTypeSourceIndex;",
        "    u16 collisionSourceIndex;",
        "    u16 surfaceTypeIndex;",
        "    u32 offset;",
        "    u32 data1;",
        "    u32 data2;",
        "    u16 referencedPolygonCount;",
        "} Oot3dSceneCollisionSurfaceTypeSourceRow;",
        "",
        "typedef struct {",
        "    u16 polygonUsageSourceIndex;",
        "    u16 collisionSourceIndex;",
        "    u16 usageIndex;",
        "    u16 surfaceType;",
        "    u16 polygonCount;",
        "} Oot3dSceneCollisionPolygonTypeUsageRow;",
        "",
        "typedef struct {",
        "    u16 collisionSourceIndex;",
        "    Oot3dSceneCommandBindingKind bindingKind;",
        "    u16 bindingIndex;",
        "    u8 sceneId;",
        "    u16 setupSourceIndex;",
        "    u16 commandSourceIndex;",
        "    u16 setupIndex;",
        "    u16 commandIndex;",
        "    u32 commandOffset;",
        "    u32 commandArgument;",
        "    u32 headerOffset;",
        "    Oot3dVec3s boundsMin;",
        "    Oot3dVec3s boundsMax;",
        "    u16 vertexCount;",
        "    u16 rawPolygonCount;",
        "    u16 effectivePolygonCount;",
        "    u16 surfaceTypeCount;",
        "    u16 bgcamCount;",
        "    u16 waterBoxCount;",
        "    u32 vertexOffset;",
        "    u32 effectiveVertexOffset;",
        "    u32 polygonOffset;",
        "    u32 effectivePolygonOffset;",
        "    u32 surfaceTypeOffset;",
        "    u32 effectiveSurfaceTypeOffset;",
        "    u32 bgcamOffset;",
        "    u32 effectiveBgcamOffset;",
        "    u32 cameraPositionOffset;",
        "    s16 cameraPointerAdjustment;",
        "    u32 waterBoxesOffset;",
        "    u16 polygonMarker;",
        "    u16 commandRefStart;",
        "    u16 commandRefCount;",
        "    u16 waterRefStart;",
        "    u16 waterRefCount;",
        "    u16 bgcamRefStart;",
        "    u16 bgcamRefCount;",
        "    u16 surfaceTypeRefStart;",
        "    u16 surfaceTypeRefCount;",
        "    u16 polygonUsageRefStart;",
        "    u16 polygonUsageRefCount;",
        "    const char* scenePath;",
        "    const char* sourceBasename;",
        "    const char* setupRole;",
        "    const char* validationStatus;",
        "    const char* evidenceText;",
        "    const char* openQuestions;",
        "    const char* handlerName;",
        "    u32 handlerEntry;",
        "} Oot3dSceneCollisionHeaderSourceRow;",
        "",
        "extern const Oot3dSceneCollisionCommandRefRow oot3d_scene_collision_command_ref_rows[];",
        "extern const Oot3dSceneCollisionWaterBoxSourceRow oot3d_scene_collision_water_source_rows[];",
        "extern const Oot3dSceneCollisionBgCamSourceRow oot3d_scene_collision_bgcam_source_rows[];",
        "extern const Oot3dSceneCollisionSurfaceTypeSourceRow oot3d_scene_collision_surface_type_source_rows[];",
        "extern const Oot3dSceneCollisionPolygonTypeUsageRow oot3d_scene_collision_polygon_type_usage_rows[];",
        "extern const Oot3dSceneCollisionHeaderSourceRow oot3d_scene_collision_header_source_rows[];",
        "extern const u32 oot3d_scene_collision_command_ref_row_count;",
        "extern const u32 oot3d_scene_collision_water_source_row_count;",
        "extern const u32 oot3d_scene_collision_bgcam_source_row_count;",
        "extern const u32 oot3d_scene_collision_surface_type_source_row_count;",
        "extern const u32 oot3d_scene_collision_polygon_type_usage_row_count;",
        "extern const u32 oot3d_scene_collision_header_source_row_count;",
        "",
        "#endif",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def write_source(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "/* Generated by build_scene_collision_source_table.py. */",
        "",
        '#include "oot3d/scene_collision_source_table.h"',
        "",
        "const Oot3dSceneCollisionCommandRefRow oot3d_scene_collision_command_ref_rows[] = {",
    ]
    for row in payload["command_ref_rows"]:
        lines.append(
            "    { "
            f"{c_u16(row.get('collision_command_ref_index'))}, "
            f"{c_u16(row.get('collision_source_index'))}, "
            f"{binding_enum(str(row.get('scene_binding_kind', '')))}, "
            f"{c_u16(row.get('scene_binding_index'))}, "
            f"{c_u8(row.get('scene_id'))}, "
            f"{c_u16(row.get('setup_source_index'))}, "
            f"{c_u16(row.get('command_source_index'))}, "
            f"{c_u16(row.get('setup_index'))}, "
            f"{c_u16(row.get('command_index'))}, "
            f"{c_u32(row.get('command_offset'))}, "
            f"{c_u32(row.get('command_argument'))}, "
            f"{ref_status_enum(str(row.get('reference_status', '')))}, "
            f"{c_string(str(row.get('scene_path', '')))}, "
            f"{c_string(str(row.get('source_basename', '')))}, "
            f"{c_string(str(row.get('setup_role', '')))}, "
            f"{c_string(str(row.get('reference_status', '')))}, "
            f"{c_string(str(row.get('open_questions', '')))}, "
            f"{c_string(str(row.get('handler_name', '')))}, "
            f"{c_u32(parse_handler_entry(row.get('handler_entry')))}"
            " },"
        )
    lines.extend(["};", "", "const Oot3dSceneCollisionWaterBoxSourceRow oot3d_scene_collision_water_source_rows[] = {"])
    for row in payload["water_rows"]:
        lines.append(
            "    { "
            f"{c_u16(row.get('water_source_index'))}, "
            f"{c_u16(row.get('collision_source_index'))}, "
            f"{c_u16(row.get('water_index'))}, "
            f"{c_s16(row.get('x_min'))}, "
            f"{c_s16(row.get('y_surface'))}, "
            f"{c_s16(row.get('z_min'))}, "
            f"{c_s16(row.get('x_length'))}, "
            f"{c_s16(row.get('z_length'))}, "
            f"{c_u32(row.get('properties'))}, "
            f"{c_bool(row.get('plausible'))}"
            " },"
        )
    lines.extend(["};", "", "const Oot3dSceneCollisionBgCamSourceRow oot3d_scene_collision_bgcam_source_rows[] = {"])
    for row in payload["bgcam_rows"]:
        lines.append(
            "    { "
            f"{c_u16(row.get('bgcam_source_index'))}, "
            f"{c_u16(row.get('collision_source_index'))}, "
            f"{c_u16(row.get('bgcam_index'))}, "
            f"{c_u16(row.get('setting'))}, "
            f"{c_u16(row.get('count'))}, "
            f"{c_u32(row.get('data_offset'))}"
            " },"
        )
    lines.extend(
        [
            "};",
            "",
            "const Oot3dSceneCollisionSurfaceTypeSourceRow "
            "oot3d_scene_collision_surface_type_source_rows[] = {",
        ]
    )
    for row in payload["surface_type_rows"]:
        lines.append(
            "    { "
            f"{c_u16(row.get('surface_type_source_index'))}, "
            f"{c_u16(row.get('collision_source_index'))}, "
            f"{c_u16(row.get('surface_type_index'))}, "
            f"{c_u32(row.get('offset'))}, "
            f"{c_u32(row.get('data1'))}, "
            f"{c_u32(row.get('data2'))}, "
            f"{c_u16(row.get('referenced_polygon_count'))}"
            " },"
        )
    lines.extend(
        [
            "};",
            "",
            "const Oot3dSceneCollisionPolygonTypeUsageRow "
            "oot3d_scene_collision_polygon_type_usage_rows[] = {",
        ]
    )
    for row in payload["polygon_type_usage_rows"]:
        lines.append(
            "    { "
            f"{c_u16(row.get('polygon_usage_source_index'))}, "
            f"{c_u16(row.get('collision_source_index'))}, "
            f"{c_u16(row.get('usage_index'))}, "
            f"{c_u16(row.get('surface_type'))}, "
            f"{c_u16(row.get('polygon_count'))}"
            " },"
        )
    lines.extend(["};", "", "const Oot3dSceneCollisionHeaderSourceRow oot3d_scene_collision_header_source_rows[] = {"])
    for row in payload["header_rows"]:
        lines.append(
            "    { "
            f"{c_u16(row.get('collision_source_index'))}, "
            f"{binding_enum(str(row.get('scene_binding_kind', '')))}, "
            f"{c_u16(row.get('scene_binding_index'))}, "
            f"{c_u8(row.get('scene_id'))}, "
            f"{c_u16(row.get('setup_source_index'))}, "
            f"{c_u16(row.get('command_source_index'))}, "
            f"{c_u16(row.get('setup_index'))}, "
            f"{c_u16(row.get('command_index'))}, "
            f"{c_u32(row.get('command_offset'))}, "
            f"{c_u32(row.get('command_argument'))}, "
            f"{c_u32(row.get('header_offset'))}, "
            f"{{ {c_s16(row.get('bounds_min_x'))}, {c_s16(row.get('bounds_min_y'))}, {c_s16(row.get('bounds_min_z'))} }}, "
            f"{{ {c_s16(row.get('bounds_max_x'))}, {c_s16(row.get('bounds_max_y'))}, {c_s16(row.get('bounds_max_z'))} }}, "
            f"{c_u16(row.get('vertex_count'))}, "
            f"{c_u16(row.get('raw_polygon_count'))}, "
            f"{c_u16(row.get('effective_polygon_count'))}, "
            f"{c_u16(row.get('surface_type_count'))}, "
            f"{c_u16(row.get('bgcam_count'))}, "
            f"{c_u16(row.get('water_box_count'))}, "
            f"{c_u32(row.get('vertex_offset'))}, "
            f"{c_u32(row.get('effective_vertex_offset'))}, "
            f"{c_u32(row.get('polygon_offset'))}, "
            f"{c_u32(row.get('effective_polygon_offset'))}, "
            f"{c_u32(row.get('surface_type_offset'))}, "
            f"{c_u32(row.get('effective_surface_type_offset'))}, "
            f"{c_u32(row.get('bgcam_offset'))}, "
            f"{c_u32(row.get('effective_bgcam_offset'))}, "
            f"{c_u32(row.get('camera_position_offset'))}, "
            f"{c_s16(row.get('camera_pointer_adjustment'))}, "
            f"{c_u32(row.get('water_boxes_offset'))}, "
            f"{c_u16(row.get('polygon_marker'))}, "
            f"{c_u16(row.get('command_ref_start'))}, "
            f"{c_u16(row.get('command_ref_count'))}, "
            f"{c_u16(row.get('water_ref_start'))}, "
            f"{c_u16(row.get('water_ref_count'))}, "
            f"{c_u16(row.get('bgcam_ref_start'))}, "
            f"{c_u16(row.get('bgcam_ref_count'))}, "
            f"{c_u16(row.get('surface_type_ref_start'))}, "
            f"{c_u16(row.get('surface_type_ref_count'))}, "
            f"{c_u16(row.get('polygon_usage_ref_start'))}, "
            f"{c_u16(row.get('polygon_usage_ref_count'))}, "
            f"{c_string(str(row.get('scene_path', '')))}, "
            f"{c_string(str(row.get('source_basename', '')))}, "
            f"{c_string(str(row.get('setup_role', '')))}, "
            f"{c_string(str(row.get('validation_status', '')))}, "
            f"{c_string(str(row.get('evidence_text', '')))}, "
            f"{c_string(str(row.get('open_questions', '')))}, "
            f"{c_string(str(row.get('handler_name', '')))}, "
            f"{c_u32(parse_handler_entry(row.get('handler_entry')))}"
            " },"
        )
    lines.extend(
        [
            "};",
            "",
            "const u32 oot3d_scene_collision_command_ref_row_count = "
            "OOT3D_SCENE_COLLISION_COMMAND_REF_ROW_COUNT;",
            "const u32 oot3d_scene_collision_water_source_row_count = "
            "OOT3D_SCENE_COLLISION_WATER_SOURCE_ROW_COUNT;",
            "const u32 oot3d_scene_collision_bgcam_source_row_count = "
            "OOT3D_SCENE_COLLISION_BGCAM_SOURCE_ROW_COUNT;",
            "const u32 oot3d_scene_collision_surface_type_source_row_count = "
            "OOT3D_SCENE_COLLISION_SURFACE_TYPE_SOURCE_ROW_COUNT;",
            "const u32 oot3d_scene_collision_polygon_type_usage_row_count = "
            "OOT3D_SCENE_COLLISION_POLYGON_TYPE_USAGE_ROW_COUNT;",
            "const u32 oot3d_scene_collision_header_source_row_count = "
            "OOT3D_SCENE_COLLISION_HEADER_SOURCE_ROW_COUNT;",
            "",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    payload = build_report()
    write_json(DEFAULT_OUT_JSON, payload)
    write_csv(DEFAULT_OUT_HEADER_CSV, HEADER_CSV_COLUMNS, payload["header_rows"])
    write_csv(DEFAULT_OUT_COMMAND_REF_CSV, COMMAND_REF_CSV_COLUMNS, payload["command_ref_rows"])
    write_csv(DEFAULT_OUT_WATER_CSV, WATER_CSV_COLUMNS, payload["water_rows"])
    write_csv(DEFAULT_OUT_BGCAM_CSV, BGCAM_CSV_COLUMNS, payload["bgcam_rows"])
    write_csv(DEFAULT_OUT_SURFACE_TYPE_CSV, SURFACE_TYPE_CSV_COLUMNS, payload["surface_type_rows"])
    write_csv(DEFAULT_OUT_USAGE_CSV, USAGE_CSV_COLUMNS, payload["polygon_type_usage_rows"])
    write_markdown(DEFAULT_OUT_MD, payload)
    write_header(DEFAULT_OUT_HEADER, payload)
    write_source(DEFAULT_OUT_SOURCE, payload)
    for output in (
        DEFAULT_OUT_JSON,
        DEFAULT_OUT_HEADER_CSV,
        DEFAULT_OUT_COMMAND_REF_CSV,
        DEFAULT_OUT_WATER_CSV,
        DEFAULT_OUT_BGCAM_CSV,
        DEFAULT_OUT_SURFACE_TYPE_CSV,
        DEFAULT_OUT_USAGE_CSV,
        DEFAULT_OUT_MD,
        DEFAULT_OUT_HEADER,
        DEFAULT_OUT_SOURCE,
    ):
        print(output)
    print(json.dumps(payload["summary"], indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
