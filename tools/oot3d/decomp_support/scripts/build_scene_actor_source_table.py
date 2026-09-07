#!/usr/bin/env python3
"""Build a source-oriented OOT3D scene actor table.

This index materializes native actor placements from OOT3D source evidence:

- setup spawn lists from command 0x00
- setup standard actor lists from command 0x01
- setup transition actor lists from command 0x0E
- room actor lists decoded from native room ZSI payloads

Rows are bound back to the generated scene/setup/room/command integration
tables, so actor definitions can be reviewed as source-like data without
consulting N64 scene tables.
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
DEFAULT_SCENE_SOURCE_TABLE = ROOT / "analysis" / "scene_source_table.json"
DEFAULT_SETUP_SOURCE_TABLE = ROOT / "analysis" / "scene_setup_source_table.json"
DEFAULT_ROOM_SOURCE_TABLE = ROOT / "analysis" / "scene_room_source_table.json"
DEFAULT_COMMAND_SOURCE_TABLE = ROOT / "analysis" / "scene_command_source_table.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_actor_source_table.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "scene_actor_source_table.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_actor_source_table.md"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "scene_actor_source_table.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_scene_actor_source_table.c"

UNMAPPED_INDEX = 0xFFFF
UNMAPPED_SCENE_ID = 0xFF
NO_OFFSET = 0xFFFFFFFF

COMMAND_SPAWN_LIST = 0x00
COMMAND_STANDARD_ACTOR_LIST = 0x01
COMMAND_TRANSITION_ACTOR_LIST = 0x0E


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


def c_u16(value: Any) -> str:
    return f"{int_value(value) & 0xFFFF}u"


def c_u32(value: Any) -> str:
    return f"{int_value(value) & 0xFFFFFFFF}u"


def vec3(values: Any) -> tuple[int, int, int]:
    items = as_list(values)
    return (
        int_value(items[0]) if len(items) > 0 else 0,
        int_value(items[1]) if len(items) > 1 else 0,
        int_value(items[2]) if len(items) > 2 else 0,
    )


def scene_source_bindings(payload: dict[str, Any]) -> dict[str, dict[str, Any]]:
    bindings: dict[str, dict[str, Any]] = {}
    for key, rows in (("scene_row", as_list(payload.get("rows"))), ("variant_row", as_list(payload.get("variant_rows")))):
        for index, row in enumerate(rows):
            if not isinstance(row, dict):
                continue
            path = str(row.get("zsi_path", "")).lower()
            if not path:
                continue
            bindings[path] = {
                "scene_binding_kind": key,
                "scene_binding_index": index,
                "scene_id": int_value(row.get("scene_id"), UNMAPPED_SCENE_ID),
                "scene_id_hex": row.get("scene_id_hex", ""),
                "scene_index_symbol": row.get("scene_index_symbol", ""),
                "source_basename": row.get("source_basename", ""),
            }
    return bindings


def setup_source_bindings(payload: dict[str, Any]) -> dict[tuple[str, int], dict[str, Any]]:
    bindings: dict[tuple[str, int], dict[str, Any]] = {}
    for index, row in enumerate(as_list(payload.get("rows"))):
        if not isinstance(row, dict):
            continue
        scene_path = str(row.get("scene_path", "")).lower()
        setup_index = int_value(row.get("setup_index"), -1)
        if scene_path and setup_index >= 0:
            bindings[(scene_path, setup_index)] = {
                "setup_source_index": index,
                "setup_role": row.get("setup_role", ""),
                "setup_symbol": row.get("setup_symbol", ""),
            }
    return bindings


def room_source_bindings(payload: dict[str, Any]) -> dict[tuple[str, str], dict[str, Any]]:
    bindings: dict[tuple[str, str], dict[str, Any]] = {}
    for index, row in enumerate(as_list(payload.get("rows"))):
        if not isinstance(row, dict):
            continue
        scene_path = str(row.get("scene_path", "")).lower()
        room_path = str(row.get("room_path", "")).lower()
        if scene_path and room_path:
            bindings[(scene_path, room_path)] = {
                "room_source_index": index,
                "room_index": int_value(row.get("room_index"), -1),
                "room_actor_symbol": row.get("room_actor_symbol", ""),
                "room_object_symbol": row.get("room_object_symbol", ""),
                "room_setup_ref_count": int_value(row.get("setup_ref_count"), 0),
                "room_actor_count": int_value(row.get("actor_count"), 0),
                "room_object_count": int_value(row.get("object_count"), 0),
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
        if scene_path and setup_index >= 0 and command_id >= 0:
            bindings[(scene_path, setup_index, command_id)] = {
                "command_source_index": int_value(row.get("command_source_index"), index),
                "command_name": row.get("command_name", ""),
                "decoded_status": row.get("decoded_status", ""),
                "support_level": row.get("support_level", ""),
                "handler_name": row.get("handler_name", ""),
            }
    return bindings


def actor_kind_for_command(command_id: int) -> str:
    if command_id == COMMAND_SPAWN_LIST:
        return "setup_spawn"
    if command_id == COMMAND_STANDARD_ACTOR_LIST:
        return "setup_standard_actor"
    if command_id == COMMAND_TRANSITION_ACTOR_LIST:
        return "setup_transition_actor"
    return "unknown"


def actor_validation_status(source_kind: str, source_status: str, confidence: str) -> str:
    if source_kind == "room_actor":
        if confidence == "strong_object_prefixed_actor_list":
            return "native_room_actor_object_prefix_confirmed"
        return "native_room_actor_candidate_unqualified"
    if source_kind == "setup_transition_actor":
        return "native_transition_actor_direct_payload"
    if source_kind in {"setup_spawn", "setup_standard_actor"}:
        if confidence:
            return f"native_setup_actor_selected_candidate_{confidence}"
        return f"native_setup_actor_{source_status or 'unqualified'}"
    return "native_actor_source_unknown"


def selected_actor_entries(command: dict[str, Any]) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    command_id = int_value(command.get("command_id"), -1)
    decoded = as_dict(command.get("decoded"))
    if command_id in {COMMAND_SPAWN_LIST, COMMAND_STANDARD_ACTOR_LIST}:
        selected = as_dict(decoded.get("selected_candidate"))
        return [row for row in as_list(selected.get("entries")) if isinstance(row, dict)], {
            "source_status": decoded.get("status", ""),
            "source_layout": decoded.get("layout", ""),
            "selected_status": selected.get("status", ""),
            "confidence": selected.get("confidence", ""),
            "expected_count": int_value(decoded.get("expected_count"), 0),
            "selected_count": int_value(selected.get("entry_count"), 0),
            "source_offset": int_value(selected.get("start_offset"), NO_OFFSET),
        }
    if command_id == COMMAND_TRANSITION_ACTOR_LIST:
        return [row for row in as_list(decoded.get("entries")) if isinstance(row, dict)], {
            "source_status": decoded.get("status", ""),
            "source_layout": decoded.get("layout", ""),
            "selected_status": decoded.get("status", ""),
            "confidence": "",
            "expected_count": int_value(decoded.get("expected_count"), 0),
            "selected_count": int_value(decoded.get("entry_count"), 0),
            "source_offset": NO_OFFSET,
        }
    return [], {}


def base_actor_fields(entry: dict[str, Any]) -> dict[str, Any]:
    pos_x, pos_y, pos_z = vec3(entry.get("pos"))
    rot_x, rot_y, rot_z = vec3(entry.get("rot"))
    return {
        "source_index": int_value(entry.get("index"), -1),
        "source_offset": int_value(entry.get("offset"), NO_OFFSET),
        "actor_id": int_value(entry.get("actor_id"), 0),
        "actor_name": entry.get("actor_name", ""),
        "pos_x": pos_x,
        "pos_y": pos_y,
        "pos_z": pos_z,
        "rot_x": rot_x,
        "rot_y": rot_y,
        "rot_z": rot_z,
        "params": int_value(entry.get("params"), 0),
    }


def transition_actor_fields(entry: dict[str, Any]) -> dict[str, Any]:
    pos_x, pos_y, pos_z = vec3(entry.get("pos"))
    return {
        "source_index": int_value(entry.get("index"), -1),
        "source_offset": NO_OFFSET,
        "actor_id": int_value(entry.get("actor_id"), 0),
        "actor_name": entry.get("actor_name", ""),
        "pos_x": pos_x,
        "pos_y": pos_y,
        "pos_z": pos_z,
        "rot_x": 0,
        "rot_y": int_value(entry.get("rot_y"), 0),
        "rot_z": 0,
        "params": int_value(entry.get("params"), 0),
        "front_room": int_value(entry.get("front_room"), -1),
        "front_effect": int_value(entry.get("front_effect"), 0),
        "back_room": int_value(entry.get("back_room"), -1),
        "back_effect": int_value(entry.get("back_effect"), 0),
    }


def build_setup_actor_rows(
    record: dict[str, Any],
    scene_bindings: dict[str, dict[str, Any]],
    setup_bindings: dict[tuple[str, int], dict[str, Any]],
    command_bindings: dict[tuple[str, int, int], dict[str, Any]],
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    scene_path = str(record.get("scene_path", ""))
    scene_key = scene_path.lower()
    scene_binding = scene_bindings.get(scene_key, {})
    for setup in as_list(record.get("setups")):
        if not isinstance(setup, dict):
            continue
        setup_index = int_value(setup.get("index"), -1)
        setup_binding = setup_bindings.get((scene_key, setup_index), {})
        for command in as_list(setup.get("commands")):
            if not isinstance(command, dict):
                continue
            command_id = int_value(command.get("command_id"), -1)
            if command_id not in {COMMAND_SPAWN_LIST, COMMAND_STANDARD_ACTOR_LIST, COMMAND_TRANSITION_ACTOR_LIST}:
                continue
            entries, source_meta = selected_actor_entries(command)
            command_binding = command_bindings.get((scene_key, setup_index, command_id), {})
            for entry in entries:
                fields = (
                    transition_actor_fields(entry)
                    if command_id == COMMAND_TRANSITION_ACTOR_LIST
                    else base_actor_fields(entry)
                )
                row = {
                    "actor_source_index": len(rows),
                    "source_kind": actor_kind_for_command(command_id),
                    "scene_path": scene_path,
                    "scene_id": int_value(scene_binding.get("scene_id"), UNMAPPED_SCENE_ID),
                    "scene_id_hex": scene_binding.get("scene_id_hex", ""),
                    "scene_binding_kind": scene_binding.get("scene_binding_kind", "unmapped"),
                    "scene_binding_index": int_value(scene_binding.get("scene_binding_index"), UNMAPPED_INDEX),
                    "source_basename": scene_binding.get("source_basename", ""),
                    "scene_index_symbol": scene_binding.get("scene_index_symbol", ""),
                    "setup_source_index": int_value(setup_binding.get("setup_source_index"), UNMAPPED_INDEX),
                    "setup_index": setup_index,
                    "setup_role": setup_binding.get("setup_role", ""),
                    "setup_symbol": setup_binding.get("setup_symbol", ""),
                    "command_source_index": int_value(command_binding.get("command_source_index"), UNMAPPED_INDEX),
                    "command_id": command_id,
                    "command_name": command_binding.get("command_name", command.get("command_name", "")),
                    "command_decoded_status": command_binding.get("decoded_status", source_meta.get("source_status", "")),
                    "command_support_level": command_binding.get("support_level", ""),
                    "room_source_index": UNMAPPED_INDEX,
                    "room_index": -1,
                    "room_path": "",
                    "room_actor_symbol": "",
                    "room_object_symbol": "",
                    "room_setup_ref_count": 0,
                    "source_status": source_meta.get("source_status", ""),
                    "source_layout": source_meta.get("source_layout", ""),
                    "selected_status": source_meta.get("selected_status", ""),
                    "confidence": source_meta.get("confidence", ""),
                    "validation_status": actor_validation_status(
                        actor_kind_for_command(command_id),
                        str(source_meta.get("source_status", "")),
                        str(source_meta.get("confidence", "")),
                    ),
                    "expected_count": source_meta.get("expected_count", 0),
                    "selected_count": source_meta.get("selected_count", 0),
                    "front_room": int_value(fields.get("front_room"), -1),
                    "front_effect": int_value(fields.get("front_effect"), 0),
                    "back_room": int_value(fields.get("back_room"), -1),
                    "back_effect": int_value(fields.get("back_effect"), 0),
                    **fields,
                }
                rows.append(row)
    return rows


def build_room_actor_rows(
    record: dict[str, Any],
    scene_bindings: dict[str, dict[str, Any]],
    room_bindings: dict[tuple[str, str], dict[str, Any]],
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    scene_path = str(record.get("scene_path", ""))
    scene_key = scene_path.lower()
    scene_binding = scene_bindings.get(scene_key, {})
    for room in as_list(record.get("rooms")):
        if not isinstance(room, dict):
            continue
        room_path = str(room.get("room_path", ""))
        room_binding = room_bindings.get((scene_key, room_path.lower()), {})
        payload = as_dict(room.get("room_actor_list"))
        selected = as_dict(payload.get("selected_candidate"))
        entries = [entry for entry in as_list(selected.get("entries")) if isinstance(entry, dict)]
        for entry in entries:
            fields = base_actor_fields(entry)
            rows.append(
                {
                    "actor_source_index": len(rows),
                    "source_kind": "room_actor",
                    "scene_path": scene_path,
                    "scene_id": int_value(scene_binding.get("scene_id"), UNMAPPED_SCENE_ID),
                    "scene_id_hex": scene_binding.get("scene_id_hex", ""),
                    "scene_binding_kind": scene_binding.get("scene_binding_kind", "unmapped"),
                    "scene_binding_index": int_value(scene_binding.get("scene_binding_index"), UNMAPPED_INDEX),
                    "source_basename": scene_binding.get("source_basename", ""),
                    "scene_index_symbol": scene_binding.get("scene_index_symbol", ""),
                    "setup_source_index": UNMAPPED_INDEX,
                    "setup_index": -1,
                    "setup_role": "",
                    "setup_symbol": "",
                    "command_source_index": UNMAPPED_INDEX,
                    "command_id": UNMAPPED_INDEX,
                    "command_name": "",
                    "command_decoded_status": "",
                    "command_support_level": "",
                    "room_source_index": int_value(room_binding.get("room_source_index"), UNMAPPED_INDEX),
                    "room_index": int_value(room_binding.get("room_index"), int_value(room.get("room_index"), -1)),
                    "room_path": room_path,
                    "room_actor_symbol": room_binding.get("room_actor_symbol", ""),
                    "room_object_symbol": room_binding.get("room_object_symbol", ""),
                    "room_setup_ref_count": int_value(room_binding.get("room_setup_ref_count"), 0),
                    "source_status": payload.get("status", ""),
                    "source_layout": "object_prefixed_room_actor_list_candidate",
                    "selected_status": selected.get("status", ""),
                    "confidence": selected.get("confidence", ""),
                    "validation_status": actor_validation_status(
                        "room_actor",
                        str(payload.get("status", "")),
                        str(selected.get("confidence", "")),
                    ),
                    "expected_count": int_value(room_binding.get("room_actor_count"), len(entries)),
                    "selected_count": int_value(selected.get("entry_count"), len(entries)),
                    "front_room": -1,
                    "front_effect": 0,
                    "back_room": -1,
                    "back_effect": 0,
                    **fields,
                }
            )
    return rows


def build_report() -> dict[str, Any]:
    full_payload = json.loads(DEFAULT_NATIVE_FULL_INDEX.read_text(encoding="utf-8"))
    source_payload = json.loads(DEFAULT_NATIVE_SOURCE_INDEX.read_text(encoding="utf-8"))
    scene_source_payload = json.loads(DEFAULT_SCENE_SOURCE_TABLE.read_text(encoding="utf-8"))
    setup_source_payload = json.loads(DEFAULT_SETUP_SOURCE_TABLE.read_text(encoding="utf-8"))
    room_source_payload = json.loads(DEFAULT_ROOM_SOURCE_TABLE.read_text(encoding="utf-8"))
    command_source_payload = json.loads(DEFAULT_COMMAND_SOURCE_TABLE.read_text(encoding="utf-8"))

    scene_bindings = scene_source_bindings(scene_source_payload)
    setup_bindings = setup_source_bindings(setup_source_payload)
    room_bindings = room_source_bindings(room_source_payload)
    command_bindings = command_source_bindings(command_source_payload)

    setup_rows: list[dict[str, Any]] = []
    room_rows: list[dict[str, Any]] = []
    for record in as_list(full_payload.get("records")):
        if not isinstance(record, dict):
            continue
        setup_rows.extend(build_setup_actor_rows(record, scene_bindings, setup_bindings, command_bindings))
        room_rows.extend(build_room_actor_rows(record, scene_bindings, room_bindings))

    rows = setup_rows + room_rows
    for index, row in enumerate(rows):
        row["actor_source_index"] = index

    kind_counts = Counter(str(row.get("source_kind", "")) for row in rows)
    scene_binding_counts = Counter(str(row.get("scene_binding_kind", "")) for row in rows)
    command_support_counts = Counter(
        str(row.get("command_support_level", ""))
        for row in rows
        if str(row.get("source_kind", "")).startswith("setup_")
    )
    actor_name_counts = Counter(str(row.get("actor_name", "")) for row in rows)
    source_status_counts = Counter(str(row.get("source_status", "")) for row in rows)
    confidence_counts = Counter(str(row.get("confidence", "")) for row in rows)
    validation_counts = Counter(str(row.get("validation_status", "")) for row in rows)
    unmapped_scene_rows = [row for row in rows if row.get("scene_binding_kind") == "unmapped"]
    unmapped_setup_rows = [
        row
        for row in rows
        if str(row.get("source_kind", "")).startswith("setup_")
        and int_value(row.get("setup_source_index"), UNMAPPED_INDEX) == UNMAPPED_INDEX
    ]
    unmapped_command_rows = [
        row
        for row in rows
        if str(row.get("source_kind", "")).startswith("setup_")
        and int_value(row.get("command_source_index"), UNMAPPED_INDEX) == UNMAPPED_INDEX
    ]
    unmapped_room_rows = [
        row
        for row in rows
        if row.get("source_kind") == "room_actor"
        and int_value(row.get("room_source_index"), UNMAPPED_INDEX) == UNMAPPED_INDEX
    ]

    expected_payload_totals = as_dict(as_dict(source_payload.get("summary")).get("payload_totals"))
    summary = {
        "format": "oot3d_scene_actor_source_table_v1",
        "actor_source_row_count": len(rows),
        "source_kind_counts": dict(sorted(kind_counts.items())),
        "scene_binding_kind_counts": dict(sorted(scene_binding_counts.items())),
        "command_support_level_counts": dict(sorted(command_support_counts.items())),
        "source_status_counts": dict(sorted(source_status_counts.items())),
        "confidence_counts": dict(sorted(confidence_counts.items())),
        "validation_status_counts": dict(sorted(validation_counts.items())),
        "unique_actor_name_count": len(actor_name_counts),
        "top_actor_name_counts": dict(actor_name_counts.most_common(20)),
        "unmapped_scene_actor_count": len(unmapped_scene_rows),
        "unmapped_setup_actor_count": len(unmapped_setup_rows),
        "unmapped_command_actor_count": len(unmapped_command_rows),
        "unmapped_room_actor_count": len(unmapped_room_rows),
        "expected_spawn_actor_count": int_value(expected_payload_totals.get("spawns"), 0),
        "expected_standard_actor_count": int_value(expected_payload_totals.get("standard_actors"), 0),
        "expected_transition_actor_count": int_value(expected_payload_totals.get("transition_actors"), 0),
        "expected_room_actor_count": int_value(source_payload.get("summary", {}).get("room_actor_count"), 0),
    }
    return {
        "summary": summary,
        "source_policy": {
            "native_full_scene_index": str(DEFAULT_NATIVE_FULL_INDEX),
            "native_scene_source_index": str(DEFAULT_NATIVE_SOURCE_INDEX),
            "scene_source_table": str(DEFAULT_SCENE_SOURCE_TABLE),
            "setup_source_table": str(DEFAULT_SETUP_SOURCE_TABLE),
            "room_source_table": str(DEFAULT_ROOM_SOURCE_TABLE),
            "command_source_table": str(DEFAULT_COMMAND_SOURCE_TABLE),
            "n64_policy": "No N64 actor placement tables are read by this integration table. Rows come from OOT3D native ZSI payloads and OOT3D code.bin-backed command bindings.",
        },
        "rows": rows,
        "unmapped_scene_rows": unmapped_scene_rows,
        "unmapped_setup_rows": unmapped_setup_rows,
        "unmapped_command_rows": unmapped_command_rows,
        "unmapped_room_rows": unmapped_room_rows,
        "low_confidence_setup_actor_rows": [
            row
            for row in rows
            if str(row.get("source_kind", "")).startswith("setup_")
            and row.get("confidence") in {"weak", "candidate"}
        ],
    }


CSV_COLUMNS = [
    "actor_source_index",
    "source_kind",
    "scene_id_hex",
    "scene_id",
    "scene_path",
    "setup_index",
    "setup_source_index",
    "room_index",
    "room_source_index",
    "room_path",
    "command_id",
    "command_source_index",
    "source_index",
    "source_offset",
    "actor_id",
    "actor_name",
    "pos_x",
    "pos_y",
    "pos_z",
    "rot_x",
    "rot_y",
    "rot_z",
    "params",
    "front_room",
    "front_effect",
    "back_room",
    "back_effect",
    "source_status",
    "selected_status",
    "confidence",
    "validation_status",
    "scene_binding_kind",
    "scene_binding_index",
    "scene_index_symbol",
    "room_actor_symbol",
    "room_object_symbol",
]


def csv_value(value: Any) -> str:
    if isinstance(value, list):
        return "; ".join(str(item) for item in value)
    if isinstance(value, dict):
        return json.dumps(value, sort_keys=True)
    return str(value)


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=CSV_COLUMNS, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            writer.writerow({key: csv_value(row.get(key, "")) for key in CSV_COLUMNS})


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_markdown(path: Path, payload: dict[str, Any]) -> None:
    summary = payload["summary"]
    lines = [
        "# Scene Actor Source Table",
        "",
        "Generated from OOT3D-native setup actor payloads and room actor payloads.",
        "",
        "## Summary",
        "",
        f"- Actor source rows: {summary['actor_source_row_count']}",
        f"- Source kinds: `{json.dumps(summary['source_kind_counts'], sort_keys=True)}`",
        f"- Scene binding: `{json.dumps(summary['scene_binding_kind_counts'], sort_keys=True)}`",
        f"- Command support: `{json.dumps(summary['command_support_level_counts'], sort_keys=True)}`",
        f"- Source status: `{json.dumps(summary['source_status_counts'], sort_keys=True)}`",
        f"- Confidence: `{json.dumps(summary['confidence_counts'], sort_keys=True)}`",
        f"- Validation status: `{json.dumps(summary['validation_status_counts'], sort_keys=True)}`",
        f"- Unique actor names: {summary['unique_actor_name_count']}",
        f"- Unmapped scene actors: {summary['unmapped_scene_actor_count']}",
        f"- Unmapped setup actors: {summary['unmapped_setup_actor_count']}",
        f"- Unmapped command actors: {summary['unmapped_command_actor_count']}",
        f"- Unmapped room actors: {summary['unmapped_room_actor_count']}",
        "",
        "## Top Actor Names",
        "",
        "| Actor | Count |",
        "| --- | ---: |",
    ]
    for actor_name, count in summary["top_actor_name_counts"].items():
        lines.append(f"| `{actor_name}` | {count} |")
    lines.extend(
        [
            "",
            "## Actor Rows",
            "",
            "| # | Kind | Scene | Setup | Room | Actor | Pos | RotY | Params |",
            "| ---: | --- | --- | ---: | --- | --- | --- | ---: | ---: |",
        ]
    )
    for row in payload["rows"]:
        lines.append(
            f"| {row['actor_source_index']} | `{row['source_kind']}` | `{row['scene_path']}` | "
            f"{row['setup_index']} | `{row['room_path']}` | `{row['actor_name']}` | "
            f"{row['pos_x']},{row['pos_y']},{row['pos_z']} | {row['rot_y']} | {row['params']} |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def kind_enum(value: str) -> str:
    return {
        "setup_spawn": "OOT3D_SCENE_ACTOR_SOURCE_SETUP_SPAWN",
        "setup_standard_actor": "OOT3D_SCENE_ACTOR_SOURCE_SETUP_STANDARD_ACTOR",
        "setup_transition_actor": "OOT3D_SCENE_ACTOR_SOURCE_SETUP_TRANSITION_ACTOR",
        "room_actor": "OOT3D_SCENE_ACTOR_SOURCE_ROOM_ACTOR",
    }.get(value, "OOT3D_SCENE_ACTOR_SOURCE_UNKNOWN")


def binding_enum(value: str) -> str:
    return {
        "scene_row": "OOT3D_SCENE_COMMAND_BINDING_SCENE_ROW",
        "variant_row": "OOT3D_SCENE_COMMAND_BINDING_VARIANT_ROW",
        "unmapped": "OOT3D_SCENE_COMMAND_BINDING_UNMAPPED",
    }.get(value, "OOT3D_SCENE_COMMAND_BINDING_UNMAPPED")


def write_header(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "#ifndef OOT3D_SCENE_ACTOR_SOURCE_TABLE_H",
        "#define OOT3D_SCENE_ACTOR_SOURCE_TABLE_H",
        "",
        '#include "oot3d/scene_command_source_table.h"',
        "",
        "enum {",
        f"    OOT3D_SCENE_ACTOR_SOURCE_ROW_COUNT = {len(payload['rows'])},",
        "    OOT3D_SCENE_ACTOR_SOURCE_UNMAPPED_INDEX = 0xFFFF,",
        "    OOT3D_SCENE_ACTOR_SOURCE_NO_OFFSET = 0xFFFFFFFF,",
        "};",
        "",
        "typedef enum {",
        "    OOT3D_SCENE_ACTOR_SOURCE_SETUP_SPAWN,",
        "    OOT3D_SCENE_ACTOR_SOURCE_SETUP_STANDARD_ACTOR,",
        "    OOT3D_SCENE_ACTOR_SOURCE_SETUP_TRANSITION_ACTOR,",
        "    OOT3D_SCENE_ACTOR_SOURCE_ROOM_ACTOR,",
        "    OOT3D_SCENE_ACTOR_SOURCE_UNKNOWN,",
        "} Oot3dSceneActorSourceKind;",
        "",
        "typedef struct {",
        "    u16 actorSourceIndex;",
        "    Oot3dSceneActorSourceKind sourceKind;",
        "    Oot3dSceneCommandBindingKind bindingKind;",
        "    u16 bindingIndex;",
        "    u8 sceneId;",
        "    u16 setupSourceIndex;",
        "    u16 commandSourceIndex;",
        "    u16 roomSourceIndex;",
        "    s16 setupIndex;",
        "    s16 roomIndex;",
        "    u16 sourceIndex;",
        "    u32 sourceOffset;",
        "    s16 actorId;",
        "    const char* actorName;",
        "    s16 posX;",
        "    s16 posY;",
        "    s16 posZ;",
        "    s16 rotX;",
        "    s16 rotY;",
        "    s16 rotZ;",
        "    s16 params;",
        "    s8 frontRoom;",
        "    s8 frontEffect;",
        "    s8 backRoom;",
        "    s8 backEffect;",
        "    const char* scenePath;",
        "    const char* roomPath;",
        "    const char* setupRole;",
        "    const char* sourceStatus;",
        "    const char* selectedStatus;",
        "    const char* confidence;",
        "    const char* validationStatus;",
        "} Oot3dSceneActorSourceRow;",
        "",
        "extern const Oot3dSceneActorSourceRow oot3d_scene_actor_source_rows[];",
        "extern const u32 oot3d_scene_actor_source_row_count;",
        "",
        "#endif",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def write_source(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "/* Generated by build_scene_actor_source_table.py. */",
        "",
        '#include "oot3d/scene_actor_source_table.h"',
        "",
        "const Oot3dSceneActorSourceRow oot3d_scene_actor_source_rows[] = {",
    ]
    for row in payload["rows"]:
        lines.append(
            "    { "
            f"{c_u16(row.get('actor_source_index'))}, "
            f"{kind_enum(str(row.get('source_kind', '')))}, "
            f"{binding_enum(str(row.get('scene_binding_kind', '')))}, "
            f"{c_u16(row.get('scene_binding_index'))}, "
            f"{c_u8(row.get('scene_id'))}, "
            f"{c_u16(row.get('setup_source_index'))}, "
            f"{c_u16(row.get('command_source_index'))}, "
            f"{c_u16(row.get('room_source_index'))}, "
            f"{int_value(row.get('setup_index'), -1)}, "
            f"{int_value(row.get('room_index'), -1)}, "
            f"{c_u16(row.get('source_index'))}, "
            f"{c_u32(row.get('source_offset'))}, "
            f"{int_value(row.get('actor_id'))}, "
            f"{c_string(str(row.get('actor_name', '')))}, "
            f"{int_value(row.get('pos_x'))}, "
            f"{int_value(row.get('pos_y'))}, "
            f"{int_value(row.get('pos_z'))}, "
            f"{int_value(row.get('rot_x'))}, "
            f"{int_value(row.get('rot_y'))}, "
            f"{int_value(row.get('rot_z'))}, "
            f"{int_value(row.get('params'))}, "
            f"{int_value(row.get('front_room'), -1)}, "
            f"{int_value(row.get('front_effect'))}, "
            f"{int_value(row.get('back_room'), -1)}, "
            f"{int_value(row.get('back_effect'))}, "
            f"{c_string(str(row.get('scene_path', '')))}, "
            f"{c_string(str(row.get('room_path', '')))}, "
            f"{c_string(str(row.get('setup_role', '')))}, "
            f"{c_string(str(row.get('source_status', '')))}, "
            f"{c_string(str(row.get('selected_status', '')))}, "
            f"{c_string(str(row.get('confidence', '')))}, "
            f"{c_string(str(row.get('validation_status', '')))}"
            " },"
        )
    lines.extend(
        [
            "};",
            "",
            "const u32 oot3d_scene_actor_source_row_count = OOT3D_SCENE_ACTOR_SOURCE_ROW_COUNT;",
            "",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    payload = build_report()
    write_json(DEFAULT_OUT_JSON, payload)
    write_csv(DEFAULT_OUT_CSV, payload["rows"])
    write_markdown(DEFAULT_OUT_MD, payload)
    write_header(DEFAULT_OUT_HEADER, payload)
    write_source(DEFAULT_OUT_SOURCE, payload)
    for output in (DEFAULT_OUT_JSON, DEFAULT_OUT_CSV, DEFAULT_OUT_MD, DEFAULT_OUT_HEADER, DEFAULT_OUT_SOURCE):
        print(output)
    print(json.dumps(payload["summary"], indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
