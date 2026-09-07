#!/usr/bin/env python3
"""Build a source-oriented OOT3D room object-bank table.

Object-bank entries are decoded from the object prefix that precedes native
room actor lists. This table makes those entries addressable as source-like
data and binds them back to generated room/scene rows.
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
DEFAULT_ROOM_SOURCE_TABLE = ROOT / "analysis" / "scene_room_source_table.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_object_source_table.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "scene_object_source_table.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_object_source_table.md"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "scene_object_source_table.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_scene_object_source_table.c"

UNMAPPED_INDEX = 0xFFFF
UNMAPPED_SCENE_ID = 0xFF
NO_OFFSET = 0xFFFFFFFF


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


def room_source_bindings(payload: dict[str, Any]) -> dict[tuple[str, str], dict[str, Any]]:
    bindings: dict[tuple[str, str], dict[str, Any]] = {}
    for index, row in enumerate(as_list(payload.get("rows"))):
        if not isinstance(row, dict):
            continue
        scene_path = str(row.get("scene_path", "")).lower()
        room_path = str(row.get("room_path", "")).lower()
        if scene_path and room_path:
            bindings[(scene_path, room_path)] = {
                "room_source_index": int_value(row.get("room_source_index"), index),
                "scene_id": int_value(row.get("scene_id"), UNMAPPED_SCENE_ID),
                "scene_id_hex": row.get("scene_id_hex", ""),
                "scene_binding_kind": row.get("scene_binding_kind", "unmapped"),
                "scene_binding_index": int_value(row.get("scene_binding_index"), UNMAPPED_INDEX),
                "scene_index_symbol": row.get("scene_index_symbol", ""),
                "source_basename": row.get("source_basename", ""),
                "source_c_file": row.get("source_c_file", ""),
                "room_index": int_value(row.get("room_index"), -1),
                "room_object_symbol": row.get("room_object_symbol", ""),
                "room_actor_symbol": row.get("room_actor_symbol", ""),
                "actor_list_status": row.get("actor_list_status", ""),
                "actor_list_confidence": row.get("actor_list_confidence", ""),
                "object_count": int_value(row.get("object_count"), 0),
                "actor_count": int_value(row.get("actor_count"), 0),
                "setup_ref_count": int_value(row.get("setup_ref_count"), 0),
            }
    return bindings


def object_validation_status(semantic_status: str) -> str:
    if semantic_status == "known_object_id":
        return "native_room_object_known_id"
    if semantic_status == "unknown_oot3d_object_id":
        return "native_room_object_unknown_oot3d_id"
    return "native_room_object_unclassified"


def object_prefix_entries(prefix: dict[str, Any]) -> list[dict[str, Any]]:
    entries = [
        entry
        for key in ("object_ids", "unknown_object_ids")
        for entry in as_list(prefix.get(key))
        if isinstance(entry, dict)
    ]
    return sorted(entries, key=lambda entry: int_value(entry.get("offset"), NO_OFFSET))


def build_rows(full_payload: dict[str, Any], room_bindings: dict[tuple[str, str], dict[str, Any]]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for record in as_list(full_payload.get("records")):
        if not isinstance(record, dict):
            continue
        scene_path = str(record.get("scene_path", ""))
        scene_key = scene_path.lower()
        for room in as_list(record.get("rooms")):
            if not isinstance(room, dict):
                continue
            room_path = str(room.get("room_path", ""))
            binding = room_bindings.get((scene_key, room_path.lower()), {})
            payload = as_dict(room.get("room_actor_list"))
            selected = as_dict(payload.get("selected_candidate"))
            prefix = as_dict(selected.get("object_prefix"))
            entries = object_prefix_entries(prefix)
            for object_index, entry in enumerate(entries):
                semantic_status = str(entry.get("semantic_status", ""))
                rows.append(
                    {
                        "object_source_index": len(rows),
                        "room_source_index": int_value(binding.get("room_source_index"), UNMAPPED_INDEX),
                        "scene_id": int_value(binding.get("scene_id"), UNMAPPED_SCENE_ID),
                        "scene_id_hex": binding.get("scene_id_hex", ""),
                        "scene_binding_kind": binding.get("scene_binding_kind", "unmapped"),
                        "scene_binding_index": int_value(binding.get("scene_binding_index"), UNMAPPED_INDEX),
                        "scene_path": scene_path,
                        "source_basename": binding.get("source_basename", ""),
                        "source_c_file": binding.get("source_c_file", ""),
                        "scene_index_symbol": binding.get("scene_index_symbol", ""),
                        "room_path": room_path,
                        "room_index": int_value(binding.get("room_index"), int_value(room.get("room_index"), -1)),
                        "room_object_symbol": binding.get("room_object_symbol", ""),
                        "room_actor_symbol": binding.get("room_actor_symbol", ""),
                        "room_actor_count": int_value(binding.get("actor_count"), 0),
                        "room_object_count": int_value(binding.get("object_count"), len(entries)),
                        "room_setup_ref_count": int_value(binding.get("setup_ref_count"), 0),
                        "object_index": object_index,
                        "object_id": int_value(entry.get("object_id"), 0),
                        "object_name": entry.get("object_name", ""),
                        "semantic_status": semantic_status,
                        "validation_status": object_validation_status(semantic_status),
                        "offset": int_value(entry.get("offset"), NO_OFFSET),
                        "offset_hex": entry.get("offset_hex", ""),
                        "prefix_start_offset": int_value(prefix.get("start_offset"), NO_OFFSET),
                        "prefix_start_offset_hex": prefix.get("start_offset_hex", ""),
                        "prefix_end_offset": int_value(prefix.get("end_offset"), NO_OFFSET),
                        "prefix_byte_count": int_value(prefix.get("byte_count"), 0),
                        "actor_list_status": payload.get("status", ""),
                        "actor_list_confidence": selected.get("confidence", ""),
                    }
                )
    return rows


def build_report() -> dict[str, Any]:
    full_payload = json.loads(DEFAULT_NATIVE_FULL_INDEX.read_text(encoding="utf-8"))
    source_payload = json.loads(DEFAULT_NATIVE_SOURCE_INDEX.read_text(encoding="utf-8"))
    room_payload = json.loads(DEFAULT_ROOM_SOURCE_TABLE.read_text(encoding="utf-8"))
    room_bindings = room_source_bindings(room_payload)
    rows = build_rows(full_payload, room_bindings)

    semantic_counts = Counter(str(row.get("semantic_status", "")) for row in rows)
    validation_counts = Counter(str(row.get("validation_status", "")) for row in rows)
    scene_binding_counts = Counter(str(row.get("scene_binding_kind", "")) for row in rows)
    actor_list_status_counts = Counter(str(row.get("actor_list_status", "")) for row in rows)
    object_name_counts = Counter(str(row.get("object_name", "")) for row in rows)
    unmapped_room_rows = [
        row for row in rows if int_value(row.get("room_source_index"), UNMAPPED_INDEX) == UNMAPPED_INDEX
    ]
    object_count_mismatches = []
    per_room_counts = Counter(int_value(row.get("room_source_index"), UNMAPPED_INDEX) for row in rows)
    for room in as_list(room_payload.get("rows")):
        if not isinstance(room, dict):
            continue
        room_source_index = int_value(room.get("room_source_index"), UNMAPPED_INDEX)
        expected = int_value(room.get("object_count"), 0)
        actual = per_room_counts.get(room_source_index, 0)
        if expected != actual:
            object_count_mismatches.append(
                {
                    "room_source_index": room_source_index,
                    "scene_path": room.get("scene_path", ""),
                    "room_path": room.get("room_path", ""),
                    "expected_object_count": expected,
                    "actual_object_count": actual,
                }
            )

    summary = {
        "format": "oot3d_scene_object_source_table_v1",
        "object_source_row_count": len(rows),
        "room_with_object_bank_count": len({int_value(row.get("room_source_index")) for row in rows}),
        "expected_room_object_count": int_value(source_payload.get("summary", {}).get("room_object_count"), 0),
        "scene_binding_kind_counts": dict(sorted(scene_binding_counts.items())),
        "semantic_status_counts": dict(sorted(semantic_counts.items())),
        "validation_status_counts": dict(sorted(validation_counts.items())),
        "actor_list_status_counts": dict(sorted(actor_list_status_counts.items())),
        "unique_object_name_count": len(object_name_counts),
        "top_object_name_counts": dict(object_name_counts.most_common(20)),
        "unknown_object_id_count": semantic_counts.get("unknown_oot3d_object_id", 0),
        "unmapped_room_object_count": len(unmapped_room_rows),
        "room_object_count_mismatch_count": len(object_count_mismatches),
    }
    return {
        "summary": summary,
        "source_policy": {
            "native_full_scene_index": str(DEFAULT_NATIVE_FULL_INDEX),
            "native_scene_source_index": str(DEFAULT_NATIVE_SOURCE_INDEX),
            "room_source_table": str(DEFAULT_ROOM_SOURCE_TABLE),
            "n64_policy": "No N64 object-bank tables are read by this integration table. Rows come from object prefixes decoded in OOT3D native room ZSI files.",
        },
        "rows": rows,
        "unmapped_room_rows": unmapped_room_rows,
        "object_count_mismatches": object_count_mismatches,
    }


CSV_COLUMNS = [
    "object_source_index",
    "scene_id_hex",
    "scene_id",
    "scene_path",
    "room_source_index",
    "room_index",
    "room_path",
    "object_index",
    "offset_hex",
    "object_id",
    "object_name",
    "semantic_status",
    "validation_status",
    "room_object_symbol",
    "room_actor_symbol",
    "actor_list_status",
    "actor_list_confidence",
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
        "# Scene Object Source Table",
        "",
        "Generated from OOT3D-native room object prefixes.",
        "",
        "## Summary",
        "",
        f"- Object source rows: {summary['object_source_row_count']}",
        f"- Rooms with object banks: {summary['room_with_object_bank_count']}",
        f"- Expected room object count: {summary['expected_room_object_count']}",
        f"- Scene binding: `{json.dumps(summary['scene_binding_kind_counts'], sort_keys=True)}`",
        f"- Semantic status: `{json.dumps(summary['semantic_status_counts'], sort_keys=True)}`",
        f"- Validation status: `{json.dumps(summary['validation_status_counts'], sort_keys=True)}`",
        f"- Actor-list status: `{json.dumps(summary['actor_list_status_counts'], sort_keys=True)}`",
        f"- Unknown object IDs: {summary['unknown_object_id_count']}",
        f"- Unmapped room objects: {summary['unmapped_room_object_count']}",
        f"- Room object-count mismatches: {summary['room_object_count_mismatch_count']}",
        "",
        "## Top Object Names",
        "",
        "| Object | Count |",
        "| --- | ---: |",
    ]
    for name, count in summary["top_object_name_counts"].items():
        lines.append(f"| `{name}` | {count} |")
    lines.extend(
        [
            "",
            "## Object Rows",
            "",
            "| # | Scene | Room | Object | Offset | Status |",
            "| ---: | --- | --- | --- | --- | --- |",
        ]
    )
    for row in payload["rows"]:
        lines.append(
            f"| {row['object_source_index']} | `{row['scene_path']}` | `{row['room_path']}` | "
            f"`{row['object_name']}` (`0x{int_value(row['object_id']) & 0xFFFF:04x}`) | "
            f"`{row['offset_hex']}` | `{row['validation_status']}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def binding_enum(value: str) -> str:
    return {
        "scene_row": "OOT3D_SCENE_COMMAND_BINDING_SCENE_ROW",
        "variant_row": "OOT3D_SCENE_COMMAND_BINDING_VARIANT_ROW",
        "unmapped": "OOT3D_SCENE_COMMAND_BINDING_UNMAPPED",
    }.get(value, "OOT3D_SCENE_COMMAND_BINDING_UNMAPPED")


def write_header(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "#ifndef OOT3D_SCENE_OBJECT_SOURCE_TABLE_H",
        "#define OOT3D_SCENE_OBJECT_SOURCE_TABLE_H",
        "",
        '#include "oot3d/scene_command_source_table.h"',
        "",
        "enum {",
        f"    OOT3D_SCENE_OBJECT_SOURCE_ROW_COUNT = {len(payload['rows'])},",
        "    OOT3D_SCENE_OBJECT_SOURCE_UNMAPPED_INDEX = 0xFFFF,",
        "    OOT3D_SCENE_OBJECT_SOURCE_NO_OFFSET = 0xFFFFFFFF,",
        "};",
        "",
        "typedef struct {",
        "    u16 objectSourceIndex;",
        "    Oot3dSceneCommandBindingKind bindingKind;",
        "    u16 bindingIndex;",
        "    u8 sceneId;",
        "    u16 roomSourceIndex;",
        "    s16 roomIndex;",
        "    u16 objectIndex;",
        "    u16 objectId;",
        "    u32 offset;",
        "    u32 prefixStartOffset;",
        "    u32 prefixEndOffset;",
        "    u16 prefixByteCount;",
        "    const char* objectName;",
        "    const char* semanticStatus;",
        "    const char* validationStatus;",
        "    const char* scenePath;",
        "    const char* roomPath;",
        "    const char* roomObjectSymbol;",
        "    const char* actorListStatus;",
        "    const char* actorListConfidence;",
        "} Oot3dSceneObjectSourceRow;",
        "",
        "extern const Oot3dSceneObjectSourceRow oot3d_scene_object_source_rows[];",
        "extern const u32 oot3d_scene_object_source_row_count;",
        "",
        "#endif",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def write_source(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "/* Generated by build_scene_object_source_table.py. */",
        "",
        '#include "oot3d/scene_object_source_table.h"',
        "",
        "const Oot3dSceneObjectSourceRow oot3d_scene_object_source_rows[] = {",
    ]
    for row in payload["rows"]:
        lines.append(
            "    { "
            f"{c_u16(row.get('object_source_index'))}, "
            f"{binding_enum(str(row.get('scene_binding_kind', '')))}, "
            f"{c_u16(row.get('scene_binding_index'))}, "
            f"{c_u8(row.get('scene_id'))}, "
            f"{c_u16(row.get('room_source_index'))}, "
            f"{int_value(row.get('room_index'), -1)}, "
            f"{c_u16(row.get('object_index'))}, "
            f"{c_u16(row.get('object_id'))}, "
            f"{c_u32(row.get('offset'))}, "
            f"{c_u32(row.get('prefix_start_offset'))}, "
            f"{c_u32(row.get('prefix_end_offset'))}, "
            f"{c_u16(row.get('prefix_byte_count'))}, "
            f"{c_string(str(row.get('object_name', '')))}, "
            f"{c_string(str(row.get('semantic_status', '')))}, "
            f"{c_string(str(row.get('validation_status', '')))}, "
            f"{c_string(str(row.get('scene_path', '')))}, "
            f"{c_string(str(row.get('room_path', '')))}, "
            f"{c_string(str(row.get('room_object_symbol', '')))}, "
            f"{c_string(str(row.get('actor_list_status', '')))}, "
            f"{c_string(str(row.get('actor_list_confidence', '')))}"
            " },"
        )
    lines.extend(
        [
            "};",
            "",
            "const u32 oot3d_scene_object_source_row_count = OOT3D_SCENE_OBJECT_SOURCE_ROW_COUNT;",
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
