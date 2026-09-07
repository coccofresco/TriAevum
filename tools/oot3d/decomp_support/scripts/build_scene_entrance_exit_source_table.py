#!/usr/bin/env python3
"""Build source-oriented OOT3D local entrance and exit tables.

Local entrances come from native scene command 0x06 payloads. Exit entries come
from command 0x13 payloads and are linked to decoded global entrance rows when
the native value is a direct non-negative transition index. High-remap exits are
kept as code.bin-confirmed delta-table cases because their final target depends
on the runtime current entrance index.
"""

from __future__ import annotations

import csv
import json
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_NATIVE_FULL_INDEX = ROOT / "analysis" / "oot3d_native_scene_index_all_full.json"
DEFAULT_NATIVE_SOURCE_INDEX = ROOT / "analysis" / "oot3d_native_scene_source_index.json"
DEFAULT_SETUP_SOURCE_TABLE = ROOT / "analysis" / "scene_setup_source_table.json"
DEFAULT_COMMAND_SOURCE_TABLE = ROOT / "analysis" / "scene_command_source_table.json"
DEFAULT_ROOM_SOURCE_TABLE = ROOT / "analysis" / "scene_room_source_table.json"
DEFAULT_GLOBAL_ENTRANCE_TABLE = ROOT / "analysis" / "scene_global_entrance_table.json"
DEFAULT_EXIT_TRANSITION_TABLES = ROOT / "analysis" / "scene_exit_transition_tables.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_entrance_exit_source_table.json"
DEFAULT_OUT_ENTRANCE_CSV = ROOT / "analysis" / "scene_entrance_source_table.csv"
DEFAULT_OUT_EXIT_CSV = ROOT / "analysis" / "scene_exit_source_table.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_entrance_exit_source_table.md"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "scene_entrance_exit_source_table.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_scene_entrance_exit_source_table.c"

UNMAPPED_INDEX = 0xFFFF
UNMAPPED_SCENE_ID = 0xFF
NO_OFFSET = 0xFFFFFFFF
NO_DELTA = -1
COMMAND_ENTRANCE_LIST = 0x06
COMMAND_EXIT_LIST = 0x13


def as_dict(value: Any) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def as_list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def int_value(value: Any, default: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def c_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def c_u8(value: Any) -> str:
    return f"{int_value(value) & 0xFF}u"


def c_s8(value: Any) -> str:
    return str(max(-128, min(127, int_value(value))))


def c_u16(value: Any) -> str:
    return f"{int_value(value) & 0xFFFF}u"


def c_s16(value: Any) -> str:
    return str(max(-32768, min(32767, int_value(value))))


def c_u32(value: Any) -> str:
    return f"{int_value(value) & 0xFFFFFFFF}u"


def parse_handler_entry(value: Any) -> int:
    text = str(value or "")
    if not text:
        return 0
    return int(text, 16)


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
                "entrance_count": int_value(row.get("entrance_count"), 0),
                "entrances_symbol": row.get("entrances_symbol", ""),
                "exit_count": int_value(row.get("exit_count"), 0),
                "exits_symbol": row.get("exits_symbol", ""),
            }
    return bindings


def command_source_bindings(payload: dict[str, Any]) -> dict[tuple[str, int, int], dict[str, Any]]:
    bindings: dict[tuple[str, int, int], dict[str, Any]] = {}
    for index, row in enumerate(as_list(payload.get("command_rows"))):
        if not isinstance(row, dict):
            continue
        scene_path = str(row.get("scene_path", "")).lower()
        setup_index = int_value(row.get("setup_index"), -1)
        command_id = int_value(row.get("command_id"), -1)
        if scene_path and setup_index >= 0 and command_id in {COMMAND_ENTRANCE_LIST, COMMAND_EXIT_LIST}:
            bindings[(scene_path, setup_index, command_id)] = {
                "command_source_index": int_value(row.get("command_source_index"), index),
                "command_index": int_value(row.get("command_index"), UNMAPPED_INDEX),
                "command_name": row.get("command_name", ""),
                "decoded_status": row.get("decoded_status", ""),
                "decoded_count": int_value(row.get("decoded_count"), 0),
                "decoded_expected_count": int_value(row.get("decoded_expected_count"), 0),
                "support_level": row.get("support_level", ""),
                "handler_binding_status": row.get("handler_binding_status", ""),
                "handler_name": row.get("handler_name", ""),
                "handler_entry": row.get("handler_entry", ""),
                "payload_symbol": row.get("payload_symbol", ""),
                "export_structs": row.get("export_structs", ""),
            }
    return bindings


def room_source_bindings(payload: dict[str, Any]) -> dict[tuple[str, int], dict[str, Any]]:
    bindings: dict[tuple[str, int], dict[str, Any]] = {}
    for index, row in enumerate(as_list(payload.get("rows"))):
        if not isinstance(row, dict):
            continue
        scene_path = str(row.get("scene_path", "")).lower()
        room_index = int_value(row.get("room_index"), -1)
        if scene_path and room_index >= 0:
            bindings[(scene_path, room_index)] = {
                "room_source_index": int_value(row.get("room_source_index"), index),
                "room_path": row.get("room_path", ""),
                "room_index": room_index,
                "room_actor_symbol": row.get("room_actor_symbol", ""),
                "room_object_symbol": row.get("room_object_symbol", ""),
            }
    return bindings


def global_entrance_bindings(payload: dict[str, Any]) -> tuple[dict[int, dict[str, Any]], dict[int, dict[str, Any]]]:
    rows: dict[int, dict[str, Any]] = {}
    for row in as_list(payload.get("rows")):
        if not isinstance(row, dict):
            continue
        rows[int_value(row.get("entrance_index"), -1)] = row
    unpromoted: dict[int, dict[str, Any]] = {}
    for row in as_list(payload.get("unpromoted_references")):
        if not isinstance(row, dict):
            continue
        unpromoted[int_value(row.get("entrance_index"), -1)] = row
    return rows, unpromoted


def high_remap_deltas(payload: dict[str, Any]) -> dict[int, int]:
    return {
        int_value(row.get("exit_value"), -1): int_value(row.get("delta"), NO_DELTA)
        for row in as_list(payload.get("delta_rows"))
        if isinstance(row, dict)
    }


def binding_enum(value: str) -> str:
    return {
        "scene_row": "OOT3D_SCENE_COMMAND_BINDING_SCENE_ROW",
        "variant_row": "OOT3D_SCENE_COMMAND_BINDING_VARIANT_ROW",
        "unmapped": "OOT3D_SCENE_COMMAND_BINDING_UNMAPPED",
    }.get(value, "OOT3D_SCENE_COMMAND_BINDING_UNMAPPED")


def exit_category_enum(value: str) -> str:
    return {
        "direct_transition_value": "OOT3D_EXIT_DIRECT_TRANSITION_VALUE",
        "high_remap_0x7ff9_to_0x7ffe": "OOT3D_EXIT_HIGH_REMAP_0X7FF9_TO_0X7FFE",
        "special_0x7fff": "OOT3D_EXIT_SPECIAL_0X7FFF",
        "unhandled_signed_value": "OOT3D_EXIT_UNHANDLED_SIGNED_VALUE",
    }.get(value, "OOT3D_EXIT_UNHANDLED_SIGNED_VALUE")


def entrance_validation_status(decoded: dict[str, Any], selected: dict[str, Any]) -> str:
    validation = as_dict(decoded.get("validation"))
    status = str(validation.get("status", ""))
    confidence = str(selected.get("confidence", ""))
    if status == "validated_prefixed_route_room_indices" and confidence == "strong":
        return "native_entrance_prefixed_route_room_indices_confirmed"
    if status == "candidate_needs_validation":
        return "native_entrance_candidate_needs_validation"
    return f"native_entrance_{status or confidence or 'unclassified'}"


def room_resolution_status(room: int, room_binding: dict[str, Any]) -> str:
    if room < 0:
        return "native_entrance_room_none_or_negative"
    if room_binding:
        return "native_entrance_room_source_resolved"
    return "native_entrance_room_source_unresolved"


def exit_validation_status(decoded: dict[str, Any]) -> str:
    validation = as_dict(decoded.get("validation"))
    semantic_mapping = str(validation.get("semantic_mapping", ""))
    status = str(validation.get("status", ""))
    if semantic_mapping == "code_bin_transition_mapping_confirmed":
        return "native_exit_s16_payload_code_bin_transition_mapping_confirmed"
    return f"native_exit_{status or 'unclassified'}"


def resolve_exit_target(
    entry: dict[str, Any],
    global_rows: dict[int, dict[str, Any]],
    unpromoted_global_rows: dict[int, dict[str, Any]],
    high_deltas: dict[int, int],
) -> dict[str, Any]:
    category = str(entry.get("oot3d_exit_category", ""))
    raw_u16 = int_value(entry.get("raw_u16", entry.get("value", 0))) & 0xFFFF
    signed_value = int_value(entry.get("value"), raw_u16 if raw_u16 < 0x8000 else raw_u16 - 0x10000)
    if category == "direct_transition_value":
        if signed_value < 0:
            return {
                "target_resolution_status": "direct_negative_signed_transition_value_preserved",
                "target_global_entrance_index": UNMAPPED_INDEX,
                "target_scene_id": UNMAPPED_SCENE_ID,
                "target_local_entrance_index": UNMAPPED_INDEX,
                "target_scene_path": "",
                "target_scene_index_symbol": "",
                "target_native_validation_status": "",
                "high_remap_delta": NO_DELTA,
            }
        target = global_rows.get(signed_value)
        if target:
            return {
                "target_resolution_status": "direct_global_entrance_code_bin_resolved",
                "target_global_entrance_index": signed_value,
                "target_scene_id": int_value(target.get("scene_id"), UNMAPPED_SCENE_ID),
                "target_local_entrance_index": int_value(target.get("local_entrance_index"), UNMAPPED_INDEX),
                "target_scene_path": target.get("native_scene_resource_zsi_path", "")
                or (as_list(target.get("native_scene_path_candidates"))[0] if as_list(target.get("native_scene_path_candidates")) else ""),
                "target_scene_index_symbol": target.get("native_scene_index_symbol", ""),
                "target_native_validation_status": target.get("native_validation_status", ""),
                "high_remap_delta": NO_DELTA,
            }
        unpromoted = unpromoted_global_rows.get(signed_value)
        if unpromoted:
            return {
                "target_resolution_status": str(unpromoted.get("reason", "direct_global_entrance_unpromoted")),
                "target_global_entrance_index": signed_value,
                "target_scene_id": UNMAPPED_SCENE_ID,
                "target_local_entrance_index": UNMAPPED_INDEX,
                "target_scene_path": "",
                "target_scene_index_symbol": "",
                "target_native_validation_status": "",
                "high_remap_delta": NO_DELTA,
            }
        return {
            "target_resolution_status": "direct_global_entrance_unresolved",
            "target_global_entrance_index": signed_value,
            "target_scene_id": UNMAPPED_SCENE_ID,
            "target_local_entrance_index": UNMAPPED_INDEX,
            "target_scene_path": "",
            "target_scene_index_symbol": "",
            "target_native_validation_status": "",
            "high_remap_delta": NO_DELTA,
        }
    if category == "high_remap_0x7ff9_to_0x7ffe":
        return {
            "target_resolution_status": "high_remap_code_bin_delta_confirmed_runtime_entrance_dependent",
            "target_global_entrance_index": UNMAPPED_INDEX,
            "target_scene_id": UNMAPPED_SCENE_ID,
            "target_local_entrance_index": UNMAPPED_INDEX,
            "target_scene_path": "",
            "target_scene_index_symbol": "",
            "target_native_validation_status": "",
            "high_remap_delta": high_deltas.get(raw_u16, NO_DELTA),
        }
    if raw_u16 == 0x7FFF:
        return {
            "target_resolution_status": "special_exit_0x7fff",
            "target_global_entrance_index": UNMAPPED_INDEX,
            "target_scene_id": UNMAPPED_SCENE_ID,
            "target_local_entrance_index": UNMAPPED_INDEX,
            "target_scene_path": "",
            "target_scene_index_symbol": "",
            "target_native_validation_status": "",
            "high_remap_delta": NO_DELTA,
        }
    return {
        "target_resolution_status": "exit_target_unclassified",
        "target_global_entrance_index": UNMAPPED_INDEX,
        "target_scene_id": UNMAPPED_SCENE_ID,
        "target_local_entrance_index": UNMAPPED_INDEX,
        "target_scene_path": "",
        "target_scene_index_symbol": "",
        "target_native_validation_status": "",
        "high_remap_delta": NO_DELTA,
    }


def common_source_fields(
    scene_path: str,
    setup_index: int,
    command_id: int,
    setup_bindings: dict[tuple[str, int], dict[str, Any]],
    command_bindings: dict[tuple[str, int, int], dict[str, Any]],
) -> dict[str, Any]:
    scene_key = scene_path.lower()
    setup_binding = setup_bindings.get((scene_key, setup_index), {})
    command_binding = command_bindings.get((scene_key, setup_index, command_id), {})
    return {
        "setup_source_index": int_value(setup_binding.get("setup_source_index"), UNMAPPED_INDEX),
        "command_source_index": int_value(command_binding.get("command_source_index"), UNMAPPED_INDEX),
        "scene_id": int_value(setup_binding.get("scene_id"), UNMAPPED_SCENE_ID),
        "scene_id_hex": setup_binding.get("scene_id_hex", ""),
        "scene_binding_kind": setup_binding.get("scene_binding_kind", "unmapped"),
        "scene_binding_index": int_value(setup_binding.get("scene_binding_index"), UNMAPPED_INDEX),
        "scene_path": scene_path,
        "source_basename": setup_binding.get("source_basename", ""),
        "source_c_file": setup_binding.get("source_c_file", ""),
        "scene_index_symbol": setup_binding.get("scene_index_symbol", ""),
        "setup_index": setup_index,
        "setup_role": setup_binding.get("setup_role", ""),
        "setup_symbol": setup_binding.get("setup_symbol", ""),
        "command_index": int_value(command_binding.get("command_index"), UNMAPPED_INDEX),
        "command_name": command_binding.get("command_name", ""),
        "command_support_level": command_binding.get("support_level", ""),
        "handler_binding_status": command_binding.get("handler_binding_status", ""),
        "handler_name": command_binding.get("handler_name", ""),
        "handler_entry": command_binding.get("handler_entry", ""),
        "payload_symbol": command_binding.get("payload_symbol", ""),
        "export_structs": command_binding.get("export_structs", ""),
    }


def build_rows(
    full_payload: dict[str, Any],
    setup_bindings: dict[tuple[str, int], dict[str, Any]],
    command_bindings: dict[tuple[str, int, int], dict[str, Any]],
    room_bindings: dict[tuple[str, int], dict[str, Any]],
    global_rows: dict[int, dict[str, Any]],
    unpromoted_global_rows: dict[int, dict[str, Any]],
    high_deltas: dict[int, int],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    entrance_rows: list[dict[str, Any]] = []
    exit_rows: list[dict[str, Any]] = []
    for scene in as_list(full_payload.get("records")):
        if not isinstance(scene, dict):
            continue
        scene_path = str(scene.get("scene_path", ""))
        scene_key = scene_path.lower()
        for setup in as_list(scene.get("setups")):
            if not isinstance(setup, dict):
                continue
            setup_index = int_value(setup.get("index"), -1)
            for command_position, command in enumerate(as_list(setup.get("commands"))):
                if not isinstance(command, dict):
                    continue
                command_id = int_value(command.get("command_id"), -1)
                if command_id not in {COMMAND_ENTRANCE_LIST, COMMAND_EXIT_LIST}:
                    continue
                decoded = as_dict(command.get("decoded"))
                common = common_source_fields(
                    scene_path, setup_index, command_id, setup_bindings, command_bindings
                )
                if int_value(common.get("command_index"), UNMAPPED_INDEX) == UNMAPPED_INDEX:
                    common["command_index"] = command_position
                if command_id == COMMAND_ENTRANCE_LIST:
                    selected = as_dict(decoded.get("selected_candidate"))
                    validation_status = entrance_validation_status(decoded, selected)
                    for entrance_index, entry in enumerate(as_list(selected.get("entries"))):
                        if not isinstance(entry, dict):
                            continue
                        room = int_value(entry.get("room"), 0)
                        room_binding = room_bindings.get((scene_key, room), {})
                        row = dict(common)
                        row.update(
                            {
                                "entrance_source_index": len(entrance_rows),
                                "entrance_index": int_value(entry.get("index"), entrance_index),
                                "offset": int_value(entry.get("offset"), NO_OFFSET),
                                "offset_hex": entry.get("offset_hex", ""),
                                "spawn": int_value(entry.get("spawn"), 0),
                                "room": room,
                                "room_u8": int_value(entry.get("room_u8"), room & 0xFF),
                                "room_source_index": int_value(
                                    room_binding.get("room_source_index"), UNMAPPED_INDEX
                                ),
                                "room_path": room_binding.get("room_path", ""),
                                "room_resolution_status": room_resolution_status(room, room_binding),
                                "source_layout": decoded.get("layout", ""),
                                "selected_layout_variant": selected.get("layout_variant", ""),
                                "selected_confidence": selected.get("confidence", ""),
                                "source_status": decoded.get("status", ""),
                                "source_validation_status": as_dict(decoded.get("validation")).get("status", ""),
                                "validation_status": validation_status,
                            }
                        )
                        entrance_rows.append(row)
                elif command_id == COMMAND_EXIT_LIST:
                    validation_status = exit_validation_status(decoded)
                    for exit_index, entry in enumerate(as_list(decoded.get("values"))):
                        if not isinstance(entry, dict):
                            continue
                        row = dict(common)
                        row.update(
                            {
                                "exit_source_index": len(exit_rows),
                                "exit_index": int_value(entry.get("index"), exit_index),
                                "offset": int_value(entry.get("offset"), NO_OFFSET),
                                "offset_hex": entry.get("offset_hex", ""),
                                "value": int_value(entry.get("value"), 0),
                                "value_hex": entry.get("value_hex", ""),
                                "raw_u16": int_value(entry.get("raw_u16"), 0),
                                "raw_u16_hex": entry.get("raw_u16_hex", ""),
                                "oot3d_exit_category": entry.get("oot3d_exit_category", ""),
                                "source_status": decoded.get("status", ""),
                                "source_validation_status": as_dict(decoded.get("validation")).get("status", ""),
                                "validation_status": validation_status,
                            }
                        )
                        row.update(resolve_exit_target(entry, global_rows, unpromoted_global_rows, high_deltas))
                        exit_rows.append(row)
    return entrance_rows, exit_rows


def build_report() -> dict[str, Any]:
    full_payload = json.loads(DEFAULT_NATIVE_FULL_INDEX.read_text(encoding="utf-8"))
    source_payload = json.loads(DEFAULT_NATIVE_SOURCE_INDEX.read_text(encoding="utf-8"))
    setup_payload = json.loads(DEFAULT_SETUP_SOURCE_TABLE.read_text(encoding="utf-8"))
    command_payload = json.loads(DEFAULT_COMMAND_SOURCE_TABLE.read_text(encoding="utf-8"))
    room_payload = json.loads(DEFAULT_ROOM_SOURCE_TABLE.read_text(encoding="utf-8"))
    global_payload = json.loads(DEFAULT_GLOBAL_ENTRANCE_TABLE.read_text(encoding="utf-8"))
    transition_payload = json.loads(DEFAULT_EXIT_TRANSITION_TABLES.read_text(encoding="utf-8"))

    setup_bindings = setup_source_bindings(setup_payload)
    command_bindings = command_source_bindings(command_payload)
    room_bindings = room_source_bindings(room_payload)
    global_rows, unpromoted_global_rows = global_entrance_bindings(global_payload)
    high_deltas = high_remap_deltas(transition_payload)
    entrance_rows, exit_rows = build_rows(
        full_payload,
        setup_bindings,
        command_bindings,
        room_bindings,
        global_rows,
        unpromoted_global_rows,
        high_deltas,
    )

    expected_entrances = int_value(source_payload.get("summary", {}).get("payload_totals", {}).get("entrances"))
    expected_exits = int_value(source_payload.get("summary", {}).get("payload_totals", {}).get("exits"))
    entrance_binding_counts = Counter(str(row.get("scene_binding_kind", "")) for row in entrance_rows)
    exit_binding_counts = Counter(str(row.get("scene_binding_kind", "")) for row in exit_rows)
    entrance_validation_counts = Counter(str(row.get("validation_status", "")) for row in entrance_rows)
    exit_validation_counts = Counter(str(row.get("validation_status", "")) for row in exit_rows)
    room_resolution_counts = Counter(str(row.get("room_resolution_status", "")) for row in entrance_rows)
    exit_category_counts = Counter(str(row.get("oot3d_exit_category", "")) for row in exit_rows)
    target_resolution_counts = Counter(str(row.get("target_resolution_status", "")) for row in exit_rows)

    entrance_count_mismatches = []
    exit_count_mismatches = []
    entrance_by_setup = Counter(int_value(row.get("setup_source_index"), UNMAPPED_INDEX) for row in entrance_rows)
    exit_by_setup = Counter(int_value(row.get("setup_source_index"), UNMAPPED_INDEX) for row in exit_rows)
    for setup in as_list(setup_payload.get("rows")):
        if not isinstance(setup, dict):
            continue
        setup_source_index = int_value(setup.get("setup_source_index"), UNMAPPED_INDEX)
        expected = int_value(setup.get("entrance_count"), 0)
        actual = entrance_by_setup.get(setup_source_index, 0)
        if expected != actual:
            entrance_count_mismatches.append(
                {
                    "setup_source_index": setup_source_index,
                    "scene_path": setup.get("scene_path", ""),
                    "setup_index": int_value(setup.get("setup_index"), -1),
                    "expected_entrance_count": expected,
                    "actual_entrance_count": actual,
                }
            )
        expected_exit = int_value(setup.get("exit_count"), 0)
        actual_exit = exit_by_setup.get(setup_source_index, 0)
        if expected_exit != actual_exit:
            exit_count_mismatches.append(
                {
                    "setup_source_index": setup_source_index,
                    "scene_path": setup.get("scene_path", ""),
                    "setup_index": int_value(setup.get("setup_index"), -1),
                    "expected_exit_count": expected_exit,
                    "actual_exit_count": actual_exit,
                }
            )

    command_count_mismatches = []
    row_lists = {
        COMMAND_ENTRANCE_LIST: entrance_rows,
        COMMAND_EXIT_LIST: exit_rows,
    }
    for command in as_list(command_payload.get("command_rows")):
        if not isinstance(command, dict):
            continue
        command_id = int_value(command.get("command_id"), -1)
        if command_id not in row_lists:
            continue
        command_source_index = int_value(command.get("command_source_index"), UNMAPPED_INDEX)
        expected = int_value(command.get("decoded_count"), 0)
        actual = sum(
            1
            for row in row_lists[command_id]
            if int_value(row.get("command_source_index"), UNMAPPED_INDEX) == command_source_index
        )
        if expected != actual:
            command_count_mismatches.append(
                {
                    "command_source_index": command_source_index,
                    "command_id": command_id,
                    "scene_path": command.get("scene_path", ""),
                    "setup_index": int_value(command.get("setup_index"), -1),
                    "expected_count": expected,
                    "actual_count": actual,
                }
            )

    unmapped_entrance_setup_rows = [
        row for row in entrance_rows if int_value(row.get("setup_source_index"), UNMAPPED_INDEX) == UNMAPPED_INDEX
    ]
    unmapped_exit_setup_rows = [
        row for row in exit_rows if int_value(row.get("setup_source_index"), UNMAPPED_INDEX) == UNMAPPED_INDEX
    ]
    unmapped_entrance_command_rows = [
        row for row in entrance_rows if int_value(row.get("command_source_index"), UNMAPPED_INDEX) == UNMAPPED_INDEX
    ]
    unmapped_exit_command_rows = [
        row for row in exit_rows if int_value(row.get("command_source_index"), UNMAPPED_INDEX) == UNMAPPED_INDEX
    ]
    entrance_low_confidence_rows = [
        row
        for row in entrance_rows
        if str(row.get("validation_status")) != "native_entrance_prefixed_route_room_indices_confirmed"
    ]
    entrance_room_unresolved_rows = [
        row
        for row in entrance_rows
        if str(row.get("room_resolution_status")) == "native_entrance_room_source_unresolved"
    ]
    exit_non_direct_resolved_target_rows = [
        row
        for row in exit_rows
        if str(row.get("target_resolution_status")) != "direct_global_entrance_code_bin_resolved"
    ]

    summary = {
        "format": "oot3d_scene_entrance_exit_source_table_v1",
        "entrance_source_row_count": len(entrance_rows),
        "exit_source_row_count": len(exit_rows),
        "expected_entrance_count": expected_entrances,
        "expected_exit_count": expected_exits,
        "entrance_command_count": sum(
            1 for key in command_bindings if key[2] == COMMAND_ENTRANCE_LIST
        ),
        "exit_command_count": sum(1 for key in command_bindings if key[2] == COMMAND_EXIT_LIST),
        "entrance_scene_binding_kind_counts": dict(sorted(entrance_binding_counts.items())),
        "exit_scene_binding_kind_counts": dict(sorted(exit_binding_counts.items())),
        "entrance_validation_status_counts": dict(sorted(entrance_validation_counts.items())),
        "exit_validation_status_counts": dict(sorted(exit_validation_counts.items())),
        "entrance_room_resolution_status_counts": dict(sorted(room_resolution_counts.items())),
        "exit_category_counts": dict(sorted(exit_category_counts.items())),
        "exit_target_resolution_status_counts": dict(sorted(target_resolution_counts.items())),
        "entrance_setup_count_mismatch_count": len(entrance_count_mismatches),
        "exit_setup_count_mismatch_count": len(exit_count_mismatches),
        "command_count_mismatch_count": len(command_count_mismatches),
        "unmapped_entrance_setup_count": len(unmapped_entrance_setup_rows),
        "unmapped_exit_setup_count": len(unmapped_exit_setup_rows),
        "unmapped_entrance_command_count": len(unmapped_entrance_command_rows),
        "unmapped_exit_command_count": len(unmapped_exit_command_rows),
        "entrance_low_confidence_count": len(entrance_low_confidence_rows),
        "entrance_room_unresolved_count": len(entrance_room_unresolved_rows),
        "exit_non_direct_resolved_target_count": len(exit_non_direct_resolved_target_rows),
    }
    return {
        "summary": summary,
        "source_policy": {
            "native_full_scene_index": str(DEFAULT_NATIVE_FULL_INDEX),
            "native_scene_source_index": str(DEFAULT_NATIVE_SOURCE_INDEX),
            "setup_source_table": str(DEFAULT_SETUP_SOURCE_TABLE),
            "command_source_table": str(DEFAULT_COMMAND_SOURCE_TABLE),
            "room_source_table": str(DEFAULT_ROOM_SOURCE_TABLE),
            "global_entrance_table": str(DEFAULT_GLOBAL_ENTRANCE_TABLE),
            "exit_transition_tables": str(DEFAULT_EXIT_TRANSITION_TABLES),
            "n64_policy": "No N64 entrance or exit tables are read by this integration table. N64 labels may appear only inside the referenced global entrance table as secondary labels.",
        },
        "entrance_rows": entrance_rows,
        "exit_rows": exit_rows,
        "entrance_count_mismatches": entrance_count_mismatches,
        "exit_count_mismatches": exit_count_mismatches,
        "command_count_mismatches": command_count_mismatches,
        "unmapped_entrance_setup_rows": unmapped_entrance_setup_rows,
        "unmapped_exit_setup_rows": unmapped_exit_setup_rows,
        "unmapped_entrance_command_rows": unmapped_entrance_command_rows,
        "unmapped_exit_command_rows": unmapped_exit_command_rows,
        "entrance_low_confidence_rows": entrance_low_confidence_rows,
        "entrance_room_unresolved_rows": entrance_room_unresolved_rows,
        "exit_non_direct_resolved_target_rows": exit_non_direct_resolved_target_rows,
    }


ENTRANCE_CSV_COLUMNS = [
    "entrance_source_index",
    "scene_id_hex",
    "scene_id",
    "scene_path",
    "setup_source_index",
    "setup_index",
    "setup_role",
    "command_source_index",
    "command_index",
    "entrance_index",
    "offset",
    "spawn",
    "room",
    "room_u8",
    "room_source_index",
    "room_path",
    "room_resolution_status",
    "validation_status",
    "selected_confidence",
    "payload_symbol",
    "scene_binding_kind",
    "scene_binding_index",
    "scene_index_symbol",
]

EXIT_CSV_COLUMNS = [
    "exit_source_index",
    "scene_id_hex",
    "scene_id",
    "scene_path",
    "setup_source_index",
    "setup_index",
    "setup_role",
    "command_source_index",
    "command_index",
    "exit_index",
    "offset",
    "value",
    "value_hex",
    "raw_u16",
    "raw_u16_hex",
    "oot3d_exit_category",
    "target_resolution_status",
    "target_global_entrance_index",
    "target_scene_id",
    "target_local_entrance_index",
    "target_scene_path",
    "target_scene_index_symbol",
    "high_remap_delta",
    "validation_status",
    "payload_symbol",
    "scene_binding_kind",
    "scene_binding_index",
    "scene_index_symbol",
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
        "# Scene Entrance/Exit Source Table",
        "",
        "Generated from OOT3D-native command 0x06 and 0x13 payloads.",
        "",
        "## Summary",
        "",
        f"- Entrance source rows: {summary['entrance_source_row_count']}",
        f"- Expected entrances: {summary['expected_entrance_count']}",
        f"- Exit source rows: {summary['exit_source_row_count']}",
        f"- Expected exits: {summary['expected_exit_count']}",
        f"- Entrance commands: {summary['entrance_command_count']}",
        f"- Exit commands: {summary['exit_command_count']}",
        f"- Entrance scene binding: `{json.dumps(summary['entrance_scene_binding_kind_counts'], sort_keys=True)}`",
        f"- Exit scene binding: `{json.dumps(summary['exit_scene_binding_kind_counts'], sort_keys=True)}`",
        f"- Entrance validation: `{json.dumps(summary['entrance_validation_status_counts'], sort_keys=True)}`",
        f"- Exit validation: `{json.dumps(summary['exit_validation_status_counts'], sort_keys=True)}`",
        f"- Entrance room resolution: `{json.dumps(summary['entrance_room_resolution_status_counts'], sort_keys=True)}`",
        f"- Exit categories: `{json.dumps(summary['exit_category_counts'], sort_keys=True)}`",
        f"- Exit target resolution: `{json.dumps(summary['exit_target_resolution_status_counts'], sort_keys=True)}`",
        f"- Entrance setup count mismatches: {summary['entrance_setup_count_mismatch_count']}",
        f"- Exit setup count mismatches: {summary['exit_setup_count_mismatch_count']}",
        f"- Command count mismatches: {summary['command_count_mismatch_count']}",
        f"- Unmapped entrance setup rows: {summary['unmapped_entrance_setup_count']}",
        f"- Unmapped exit setup rows: {summary['unmapped_exit_setup_count']}",
        f"- Unmapped entrance command rows: {summary['unmapped_entrance_command_count']}",
        f"- Unmapped exit command rows: {summary['unmapped_exit_command_count']}",
        f"- Entrance low-confidence rows: {summary['entrance_low_confidence_count']}",
        f"- Entrance unresolved-room rows: {summary['entrance_room_unresolved_count']}",
        f"- Exit non-direct-resolved target rows: {summary['exit_non_direct_resolved_target_count']}",
        "",
        "## Sample Entrance Rows",
        "",
        "| # | Scene | Setup | Entry | Spawn | Room | Room Source | Status |",
        "| ---: | --- | ---: | ---: | ---: | ---: | --- | --- |",
    ]
    for row in payload["entrance_rows"][:80]:
        room_source = row["room_path"] or row["room_resolution_status"]
        lines.append(
            f"| {row['entrance_source_index']} | `{row['scene_path']}` | {row['setup_index']} | "
            f"{row['entrance_index']} | {row['spawn']} | {row['room']} | `{room_source}` | "
            f"`{row['validation_status']}` |"
        )
    lines.extend(
        [
            "",
            "## Sample Exit Rows",
            "",
            "| # | Scene | Setup | Exit | Value | Category | Target | Status |",
            "| ---: | --- | ---: | ---: | --- | --- | --- | --- |",
        ]
    )
    for row in payload["exit_rows"][:80]:
        target = row["target_scene_path"] or row["target_resolution_status"]
        lines.append(
            f"| {row['exit_source_index']} | `{row['scene_path']}` | {row['setup_index']} | "
            f"{row['exit_index']} | `{row['raw_u16_hex']}` | `{row['oot3d_exit_category']}` | "
            f"`{target}` | `{row['validation_status']}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_header(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "#ifndef OOT3D_SCENE_ENTRANCE_EXIT_SOURCE_TABLE_H",
        "#define OOT3D_SCENE_ENTRANCE_EXIT_SOURCE_TABLE_H",
        "",
        '#include "oot3d/scene.h"',
        '#include "oot3d/scene_command_source_table.h"',
        "",
        "enum {",
        f"    OOT3D_SCENE_ENTRANCE_SOURCE_ROW_COUNT = {len(payload['entrance_rows'])},",
        f"    OOT3D_SCENE_EXIT_SOURCE_ROW_COUNT = {len(payload['exit_rows'])},",
        "    OOT3D_SCENE_ENTRANCE_EXIT_SOURCE_UNMAPPED_INDEX = 0xFFFF,",
        "    OOT3D_SCENE_ENTRANCE_EXIT_SOURCE_NO_OFFSET = 0xFFFFFFFF,",
        "};",
        "",
        "typedef struct {",
        "    u16 entranceSourceIndex;",
        "    Oot3dSceneCommandBindingKind bindingKind;",
        "    u16 bindingIndex;",
        "    u8 sceneId;",
        "    u16 setupSourceIndex;",
        "    u16 commandSourceIndex;",
        "    u16 roomSourceIndex;",
        "    u16 setupIndex;",
        "    u16 commandIndex;",
        "    u16 entranceIndex;",
        "    u32 offset;",
        "    u8 spawn;",
        "    s8 room;",
        "    u8 roomU8;",
        "    const char* scenePath;",
        "    const char* sourceBasename;",
        "    const char* setupRole;",
        "    const char* payloadSymbol;",
        "    const char* roomPath;",
        "    const char* roomResolutionStatus;",
        "    const char* validationStatus;",
        "    const char* handlerName;",
        "    u32 handlerEntry;",
        "} Oot3dSceneEntranceSourceRow;",
        "",
        "typedef struct {",
        "    u16 exitSourceIndex;",
        "    Oot3dSceneCommandBindingKind bindingKind;",
        "    u16 bindingIndex;",
        "    u8 sceneId;",
        "    u16 setupSourceIndex;",
        "    u16 commandSourceIndex;",
        "    u16 setupIndex;",
        "    u16 commandIndex;",
        "    u16 exitIndex;",
        "    u32 offset;",
        "    s16 value;",
        "    u16 rawU16;",
        "    Oot3dExitCategory category;",
        "    u16 targetGlobalEntranceIndex;",
        "    u8 targetSceneId;",
        "    u16 targetLocalEntranceIndex;",
        "    s16 highRemapDelta;",
        "    const char* scenePath;",
        "    const char* sourceBasename;",
        "    const char* setupRole;",
        "    const char* payloadSymbol;",
        "    const char* targetScenePath;",
        "    const char* targetSceneIndexSymbol;",
        "    const char* targetResolutionStatus;",
        "    const char* validationStatus;",
        "    const char* handlerName;",
        "    u32 handlerEntry;",
        "} Oot3dSceneExitSourceRow;",
        "",
        "extern const Oot3dSceneEntranceSourceRow oot3d_scene_entrance_source_rows[];",
        "extern const Oot3dSceneExitSourceRow oot3d_scene_exit_source_rows[];",
        "extern const u32 oot3d_scene_entrance_source_row_count;",
        "extern const u32 oot3d_scene_exit_source_row_count;",
        "",
        "#endif",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def write_source(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "/* Generated by build_scene_entrance_exit_source_table.py. */",
        "",
        '#include "oot3d/scene_entrance_exit_source_table.h"',
        "",
        "const Oot3dSceneEntranceSourceRow oot3d_scene_entrance_source_rows[] = {",
    ]
    for row in payload["entrance_rows"]:
        lines.append(
            "    { "
            f"{c_u16(row.get('entrance_source_index'))}, "
            f"{binding_enum(str(row.get('scene_binding_kind', '')))}, "
            f"{c_u16(row.get('scene_binding_index'))}, "
            f"{c_u8(row.get('scene_id'))}, "
            f"{c_u16(row.get('setup_source_index'))}, "
            f"{c_u16(row.get('command_source_index'))}, "
            f"{c_u16(row.get('room_source_index'))}, "
            f"{c_u16(row.get('setup_index'))}, "
            f"{c_u16(row.get('command_index'))}, "
            f"{c_u16(row.get('entrance_index'))}, "
            f"{c_u32(row.get('offset'))}, "
            f"{c_u8(row.get('spawn'))}, "
            f"{c_s8(row.get('room'))}, "
            f"{c_u8(row.get('room_u8'))}, "
            f"{c_string(str(row.get('scene_path', '')))}, "
            f"{c_string(str(row.get('source_basename', '')))}, "
            f"{c_string(str(row.get('setup_role', '')))}, "
            f"{c_string(str(row.get('payload_symbol', '')))}, "
            f"{c_string(str(row.get('room_path', '')))}, "
            f"{c_string(str(row.get('room_resolution_status', '')))}, "
            f"{c_string(str(row.get('validation_status', '')))}, "
            f"{c_string(str(row.get('handler_name', '')))}, "
            f"{c_u32(parse_handler_entry(row.get('handler_entry')))}"
            " },"
        )
    lines.extend(["};", "", "const Oot3dSceneExitSourceRow oot3d_scene_exit_source_rows[] = {"])
    for row in payload["exit_rows"]:
        lines.append(
            "    { "
            f"{c_u16(row.get('exit_source_index'))}, "
            f"{binding_enum(str(row.get('scene_binding_kind', '')))}, "
            f"{c_u16(row.get('scene_binding_index'))}, "
            f"{c_u8(row.get('scene_id'))}, "
            f"{c_u16(row.get('setup_source_index'))}, "
            f"{c_u16(row.get('command_source_index'))}, "
            f"{c_u16(row.get('setup_index'))}, "
            f"{c_u16(row.get('command_index'))}, "
            f"{c_u16(row.get('exit_index'))}, "
            f"{c_u32(row.get('offset'))}, "
            f"{c_s16(row.get('value'))}, "
            f"{c_u16(row.get('raw_u16'))}, "
            f"{exit_category_enum(str(row.get('oot3d_exit_category', '')))}, "
            f"{c_u16(row.get('target_global_entrance_index'))}, "
            f"{c_u8(row.get('target_scene_id'))}, "
            f"{c_u16(row.get('target_local_entrance_index'))}, "
            f"{c_s16(row.get('high_remap_delta'))}, "
            f"{c_string(str(row.get('scene_path', '')))}, "
            f"{c_string(str(row.get('source_basename', '')))}, "
            f"{c_string(str(row.get('setup_role', '')))}, "
            f"{c_string(str(row.get('payload_symbol', '')))}, "
            f"{c_string(str(row.get('target_scene_path', '')))}, "
            f"{c_string(str(row.get('target_scene_index_symbol', '')))}, "
            f"{c_string(str(row.get('target_resolution_status', '')))}, "
            f"{c_string(str(row.get('validation_status', '')))}, "
            f"{c_string(str(row.get('handler_name', '')))}, "
            f"{c_u32(parse_handler_entry(row.get('handler_entry')))}"
            " },"
        )
    lines.extend(
        [
            "};",
            "",
            "const u32 oot3d_scene_entrance_source_row_count = OOT3D_SCENE_ENTRANCE_SOURCE_ROW_COUNT;",
            "const u32 oot3d_scene_exit_source_row_count = OOT3D_SCENE_EXIT_SOURCE_ROW_COUNT;",
            "",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    payload = build_report()
    write_json(DEFAULT_OUT_JSON, payload)
    write_csv(DEFAULT_OUT_ENTRANCE_CSV, ENTRANCE_CSV_COLUMNS, payload["entrance_rows"])
    write_csv(DEFAULT_OUT_EXIT_CSV, EXIT_CSV_COLUMNS, payload["exit_rows"])
    write_markdown(DEFAULT_OUT_MD, payload)
    write_header(DEFAULT_OUT_HEADER, payload)
    write_source(DEFAULT_OUT_SOURCE, payload)
    for output in (
        DEFAULT_OUT_JSON,
        DEFAULT_OUT_ENTRANCE_CSV,
        DEFAULT_OUT_EXIT_CSV,
        DEFAULT_OUT_MD,
        DEFAULT_OUT_HEADER,
        DEFAULT_OUT_SOURCE,
    ):
        print(output)
    print(json.dumps(payload["summary"], indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
