#!/usr/bin/env python3
"""Build a source-oriented OOT3D scene room table.

This table promotes native room bindings into a decompilation index. It keeps
both levels that matter for source reconstruction:

- every generated scene->room binding from native ZSI files
- every setup->room reference decoded from command 0x04 room_list payloads

No N64 room tables are read here. Scene/setup names and symbols come from the
OOT3D-native source indices and code.bin-backed integration tables.
"""

from __future__ import annotations

import csv
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_NATIVE_SOURCE_INDEX = ROOT / "analysis" / "oot3d_native_scene_source_index.json"
DEFAULT_NATIVE_FULL_INDEX = ROOT / "analysis" / "oot3d_native_scene_index_all_full.json"
DEFAULT_SCENE_SOURCE_TABLE = ROOT / "analysis" / "scene_source_table.json"
DEFAULT_SETUP_SOURCE_TABLE = ROOT / "analysis" / "scene_setup_source_table.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_room_source_table.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "scene_room_source_table.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_room_source_table.md"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "scene_room_source_table.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_scene_room_source_table.c"

UNMAPPED_INDEX = 0xFFFF
UNMAPPED_SCENE_ID = 0xFF
ROOM_REF_COMMAND_ID = 0x04


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


def c_u16(value: Any) -> str:
    return f"{int_value(value) & 0xFFFF}u"


def c_u8(value: Any) -> str:
    return f"{int_value(value) & 0xFF}u"


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
                "source_c_file": row.get("source_c_file", ""),
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


def source_rooms_by_key(rooms: list[dict[str, Any]]) -> dict[tuple[str, str], int]:
    result: dict[tuple[str, str], int] = {}
    for index, row in enumerate(rooms):
        scene_path = str(row.get("scene_path", "")).lower()
        room_path = str(row.get("room_path", "")).lower()
        if scene_path and room_path:
            result[(scene_path, room_path)] = index
    return result


def room_ref_path(reference: dict[str, Any]) -> str:
    return str(
        reference.get("matched_local_room_path")
        or reference.get("room_file")
        or reference.get("path")
        or ""
    )


def setup_room_references(
    full_index: dict[str, Any],
    setup_bindings: dict[tuple[str, int], dict[str, Any]],
    room_lookup: dict[tuple[str, str], int],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    refs: list[dict[str, Any]] = []
    missing_refs: list[dict[str, Any]] = []

    for record in as_list(full_index.get("records")):
        if not isinstance(record, dict):
            continue
        scene_path = str(record.get("scene_path", ""))
        scene_key = scene_path.lower()
        for setup in as_list(record.get("setups")):
            if not isinstance(setup, dict):
                continue
            setup_index = int_value(setup.get("index"), -1)
            setup_binding = setup_bindings.get((scene_key, setup_index), {})
            for command in as_list(setup.get("commands")):
                if not isinstance(command, dict):
                    continue
                if int_value(command.get("command_id"), -1) != ROOM_REF_COMMAND_ID:
                    continue
                decoded = as_dict(command.get("decoded"))
                for reference in as_list(decoded.get("room_references")):
                    if not isinstance(reference, dict):
                        continue
                    path = room_ref_path(reference)
                    room_source_index = room_lookup.get((scene_key, path.lower()), UNMAPPED_INDEX)
                    status = "matched_scene_room_binding" if room_source_index != UNMAPPED_INDEX else "missing_scene_room_binding"
                    row = {
                        "room_source_index": room_source_index,
                        "setup_source_index": int_value(setup_binding.get("setup_source_index"), UNMAPPED_INDEX),
                        "scene_path": scene_path,
                        "setup_index": setup_index,
                        "setup_role": setup_binding.get("setup_role", ""),
                        "setup_symbol": setup_binding.get("setup_symbol", ""),
                        "room_ref_index": int_value(reference.get("index"), 0),
                        "room_index": int_value(reference.get("room_index"), -1),
                        "room_path": path,
                        "room_file": reference.get("room_file", ""),
                        "matched_local_room_path": reference.get("matched_local_room_path", ""),
                        "command_offset_hex": command.get("offset_hex", ""),
                        "command_argument_hex": command.get("argument_hex", ""),
                        "decoded_status": decoded.get("status", ""),
                        "decoded_layout": decoded.get("layout", ""),
                        "binding_status": status,
                    }
                    refs.append(row)
                    if status != "matched_scene_room_binding":
                        missing_refs.append(row)
    return refs, missing_refs


def setup_refs_by_room(refs: list[dict[str, Any]]) -> dict[int, list[int]]:
    by_room: dict[int, list[int]] = defaultdict(list)
    for ref_index, ref in enumerate(refs):
        room_index = int_value(ref.get("room_source_index"), UNMAPPED_INDEX)
        if room_index != UNMAPPED_INDEX:
            by_room[room_index].append(ref_index)
    return by_room


def setup_ref_summary(refs: list[dict[str, Any]], all_refs: list[dict[str, Any]]) -> dict[str, Any]:
    setup_indices = {
        int_value(all_refs[index].get("setup_source_index"), UNMAPPED_INDEX)
        for index in refs
        if int_value(all_refs[index].get("setup_source_index"), UNMAPPED_INDEX) != UNMAPPED_INDEX
    }
    setup_roles = Counter(str(all_refs[index].get("setup_role", "")) for index in refs)
    statuses = Counter(str(all_refs[index].get("binding_status", "")) for index in refs)
    return {
        "setup_ref_count": len(refs),
        "unique_setup_ref_count": len(setup_indices),
        "setup_role_counts": dict(sorted(setup_roles.items())),
        "setup_ref_binding_status_counts": dict(sorted(statuses.items())),
    }


def build_room_row(
    room: dict[str, Any],
    room_source_index: int,
    scene_bindings: dict[str, dict[str, Any]],
    room_ref_indices: list[int],
    all_setup_refs: list[dict[str, Any]],
) -> dict[str, Any]:
    scene_path = str(room.get("scene_path", ""))
    binding = scene_bindings.get(scene_path.lower(), {})
    binding_kind = str(binding.get("scene_binding_kind", "unmapped"))
    ref_summary = setup_ref_summary(room_ref_indices, all_setup_refs)
    return {
        "room_source_index": room_source_index,
        "scene_path": scene_path,
        "scene_id": int_value(binding.get("scene_id"), UNMAPPED_SCENE_ID),
        "scene_id_hex": binding.get("scene_id_hex", ""),
        "scene_binding_kind": binding_kind,
        "scene_binding_index": int_value(binding.get("scene_binding_index"), UNMAPPED_INDEX),
        "source_basename": room.get("source_basename", binding.get("source_basename", "")),
        "source_c_file": room.get("source_c_file", binding.get("source_c_file", "")),
        "scene_index_symbol": binding.get("scene_index_symbol", ""),
        "scene_stem": room.get("scene_stem", ""),
        "room_path": room.get("room_path", ""),
        "room_index": int_value(room.get("room_index"), -1),
        "room_size": int_value(room.get("room_size"), 0),
        "room_actor_symbol": room.get("room_actor_symbol", ""),
        "room_object_symbol": room.get("room_object_symbol", ""),
        "actor_list_status": room.get("actor_list_status", ""),
        "actor_list_confidence": room.get("actor_list_confidence", ""),
        "actor_count": int_value(room.get("actor_count"), 0),
        "object_count": int_value(room.get("object_count"), 0),
        "unknown_object_id_count": int_value(room.get("unknown_object_id_count"), 0),
        "object_names": room.get("object_names", ""),
        "actor_names": room.get("actor_names", ""),
        "embedded_cmb_count": int_value(room.get("embedded_cmb_count"), 0),
        "setup_ref_start": min(room_ref_indices) if room_ref_indices else len(all_setup_refs),
        **ref_summary,
    }


def build_report() -> dict[str, Any]:
    source_payload = json.loads(DEFAULT_NATIVE_SOURCE_INDEX.read_text(encoding="utf-8"))
    full_payload = json.loads(DEFAULT_NATIVE_FULL_INDEX.read_text(encoding="utf-8"))
    scene_source_payload = json.loads(DEFAULT_SCENE_SOURCE_TABLE.read_text(encoding="utf-8"))
    setup_source_payload = json.loads(DEFAULT_SETUP_SOURCE_TABLE.read_text(encoding="utf-8"))

    source_rooms = [row for row in as_list(source_payload.get("rooms")) if isinstance(row, dict)]
    scene_bindings = scene_source_bindings(scene_source_payload)
    setup_bindings = setup_source_bindings(setup_source_payload)
    room_lookup = source_rooms_by_key(source_rooms)
    raw_setup_refs, missing_setup_refs = setup_room_references(full_payload, setup_bindings, room_lookup)
    raw_by_room: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for ref in raw_setup_refs:
        room_source_index = int_value(ref.get("room_source_index"), UNMAPPED_INDEX)
        if room_source_index != UNMAPPED_INDEX:
            raw_by_room[room_source_index].append(ref)

    setup_refs: list[dict[str, Any]] = []
    by_room: dict[int, list[int]] = {}
    for room_source_index in range(len(source_rooms)):
        refs = raw_by_room.get(room_source_index, [])
        start = len(setup_refs)
        setup_refs.extend(refs)
        by_room[room_source_index] = list(range(start, start + len(refs)))

    # Missing references are retained for diagnostics after all row-backed
    # slices, so every row's setupRefStart/setupRefCount span is contiguous.
    setup_refs.extend(missing_setup_refs)

    rows = [
        build_room_row(room, index, scene_bindings, by_room.get(index, []), setup_refs)
        for index, room in enumerate(source_rooms)
    ]

    scene_counts = Counter(str(row.get("scene_binding_kind", "")) for row in rows)
    actor_status_counts = Counter(str(row.get("actor_list_status", "")) for row in rows)
    actor_confidence_counts = Counter(str(row.get("actor_list_confidence", "")) for row in rows)
    setup_ref_distribution = Counter(int_value(row.get("setup_ref_count"), 0) for row in rows)
    room_index_match_mismatches = [
        ref
        for ref in setup_refs
        if int_value(ref.get("room_source_index"), UNMAPPED_INDEX) != UNMAPPED_INDEX
        and int_value(ref.get("room_index"), -1)
        != int_value(rows[int_value(ref.get("room_source_index"))].get("room_index"), -2)
    ]

    summary = {
        "format": "oot3d_scene_room_source_table_v1",
        "room_source_row_count": len(rows),
        "unique_room_path_count": len({str(row.get("room_path", "")).lower() for row in rows}),
        "setup_room_ref_count": len(setup_refs),
        "missing_setup_room_ref_count": len(missing_setup_refs),
        "scene_binding_kind_counts": dict(sorted(scene_counts.items())),
        "actor_list_status_counts": dict(sorted(actor_status_counts.items())),
        "actor_list_confidence_counts": dict(sorted(actor_confidence_counts.items())),
        "setup_ref_distribution": {str(count): total for count, total in sorted(setup_ref_distribution.items())},
        "room_actor_entry_count": sum(int_value(row.get("actor_count"), 0) for row in rows),
        "room_object_entry_count": sum(int_value(row.get("object_count"), 0) for row in rows),
        "unknown_object_id_count": sum(int_value(row.get("unknown_object_id_count"), 0) for row in rows),
        "room_index_match_mismatch_count": len(room_index_match_mismatches),
        "source_room_binding_count": int_value(source_payload.get("summary", {}).get("room_binding_count"), 0),
        "source_unique_room_file_count": int_value(source_payload.get("summary", {}).get("unique_room_file_count"), 0),
    }
    return {
        "summary": summary,
        "source_policy": {
            "native_scene_source_index": str(DEFAULT_NATIVE_SOURCE_INDEX),
            "native_full_scene_index": str(DEFAULT_NATIVE_FULL_INDEX),
            "scene_source_table": str(DEFAULT_SCENE_SOURCE_TABLE),
            "setup_source_table": str(DEFAULT_SETUP_SOURCE_TABLE),
            "n64_policy": "No N64 source is read by this integration table. Rows are derived from OOT3D native ZSI files and OOT3D code.bin-backed integration outputs.",
        },
        "rows": rows,
        "setup_room_refs": setup_refs,
        "missing_setup_room_refs": missing_setup_refs,
        "room_index_match_mismatches": room_index_match_mismatches,
    }


CSV_COLUMNS = [
    "room_source_index",
    "scene_id_hex",
    "scene_id",
    "scene_path",
    "room_index",
    "room_path",
    "source_basename",
    "scene_binding_kind",
    "scene_binding_index",
    "room_size",
    "actor_count",
    "object_count",
    "unknown_object_id_count",
    "setup_ref_count",
    "unique_setup_ref_count",
    "room_actor_symbol",
    "room_object_symbol",
    "actor_list_status",
    "actor_list_confidence",
    "object_names",
    "actor_names",
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
        "# Scene Room Source Table",
        "",
        "Generated from OOT3D-native room bindings plus decoded command `0x04` setup room lists.",
        "",
        "## Summary",
        "",
        f"- Room source rows: {summary['room_source_row_count']}",
        f"- Unique room paths: {summary['unique_room_path_count']}",
        f"- Setup room refs: {summary['setup_room_ref_count']}",
        f"- Missing setup room refs: {summary['missing_setup_room_ref_count']}",
        f"- Scene binding: `{json.dumps(summary['scene_binding_kind_counts'], sort_keys=True)}`",
        f"- Actor-list status: `{json.dumps(summary['actor_list_status_counts'], sort_keys=True)}`",
        f"- Actor-list confidence: `{json.dumps(summary['actor_list_confidence_counts'], sort_keys=True)}`",
        f"- Setup-ref distribution: `{json.dumps(summary['setup_ref_distribution'], sort_keys=True)}`",
        f"- Room actor entries: {summary['room_actor_entry_count']}",
        f"- Room object-bank entries: {summary['room_object_entry_count']}",
        f"- Unknown object IDs: {summary['unknown_object_id_count']}",
        f"- Room-index match mismatches: {summary['room_index_match_mismatch_count']}",
        "",
        "## Room Rows",
        "",
        "| # | Scene | Room | Actors | Objects | Setup refs | Actor-list status | Binding |",
        "| ---: | --- | ---: | ---: | ---: | ---: | --- | --- |",
    ]
    for row in payload["rows"]:
        lines.append(
            f"| {row['room_source_index']} | `{row['scene_path']}` | `{row['room_path']}` "
            f"({row['room_index']}) | {row['actor_count']} | {row['object_count']} | "
            f"{row['setup_ref_count']} | `{row['actor_list_status']}` | `{row['scene_binding_kind']}` |"
        )

    if payload["missing_setup_room_refs"]:
        lines.extend(
            [
                "",
                "## Missing Setup Room Refs",
                "",
                "| Scene | Setup | Room ref | Room path | Status |",
                "| --- | ---: | ---: | --- | --- |",
            ]
        )
        for ref in payload["missing_setup_room_refs"]:
            lines.append(
                f"| `{ref['scene_path']}` | {ref['setup_index']} | {ref['room_ref_index']} | "
                f"`{ref['room_path']}` | `{ref['binding_status']}` |"
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
        "#ifndef OOT3D_SCENE_ROOM_SOURCE_TABLE_H",
        "#define OOT3D_SCENE_ROOM_SOURCE_TABLE_H",
        "",
        '#include "oot3d/scene_command_source_table.h"',
        "",
        "enum {",
        f"    OOT3D_SCENE_ROOM_SOURCE_ROW_COUNT = {len(payload['rows'])},",
        f"    OOT3D_SCENE_ROOM_SETUP_REF_COUNT = {len(payload['setup_room_refs'])},",
        "};",
        "",
        "typedef struct {",
        "    u16 roomSourceIndex;",
        "    u16 setupSourceIndex;",
        "    u16 setupIndex;",
        "    u16 roomRefIndex;",
        "    s16 roomIndex;",
        "    const char* scenePath;",
        "    const char* roomPath;",
        "    const char* setupRole;",
        "    const char* setupSymbol;",
        "    const char* bindingStatus;",
        "} Oot3dSceneRoomSetupRef;",
        "",
        "typedef struct {",
        "    u16 roomSourceIndex;",
        "    Oot3dSceneCommandBindingKind bindingKind;",
        "    u16 bindingIndex;",
        "    u8 sceneId;",
        "    s16 roomIndex;",
        "    const char* scenePath;",
        "    const char* sourceBasename;",
        "    const char* sourceCFile;",
        "    const char* sceneIndexSymbol;",
        "    const char* roomPath;",
        "    u32 roomSize;",
        "    const char* roomActorSymbol;",
        "    const char* roomObjectSymbol;",
        "    const char* actorListStatus;",
        "    const char* actorListConfidence;",
        "    u16 actorCount;",
        "    u16 objectCount;",
        "    u16 unknownObjectIdCount;",
        "    u16 setupRefStart;",
        "    u16 setupRefCount;",
        "    u16 uniqueSetupRefCount;",
        "    u16 embeddedCmbCount;",
        "    const char* objectNames;",
        "    const char* actorNames;",
        "} Oot3dSceneRoomSourceRow;",
        "",
        "extern const Oot3dSceneRoomSetupRef oot3d_scene_room_setup_refs[];",
        "extern const Oot3dSceneRoomSourceRow oot3d_scene_room_source_rows[];",
        "extern const u32 oot3d_scene_room_setup_ref_count;",
        "extern const u32 oot3d_scene_room_source_row_count;",
        "",
        "#endif",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def write_source(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "/* Generated by build_scene_room_source_table.py. */",
        "",
        '#include "oot3d/scene_room_source_table.h"',
        "",
        "const Oot3dSceneRoomSetupRef oot3d_scene_room_setup_refs[] = {",
    ]
    for ref in payload["setup_room_refs"]:
        lines.append(
            "    { "
            f"{c_u16(ref.get('room_source_index'))}, "
            f"{c_u16(ref.get('setup_source_index'))}, "
            f"{c_u16(ref.get('setup_index'))}, "
            f"{c_u16(ref.get('room_ref_index'))}, "
            f"{int_value(ref.get('room_index'), -1)}, "
            f"{c_string(str(ref.get('scene_path', '')))}, "
            f"{c_string(str(ref.get('room_path', '')))}, "
            f"{c_string(str(ref.get('setup_role', '')))}, "
            f"{c_string(str(ref.get('setup_symbol', '')))}, "
            f"{c_string(str(ref.get('binding_status', '')))}"
            " },"
        )
    lines.extend(
        [
            "};",
            "",
            "const Oot3dSceneRoomSourceRow oot3d_scene_room_source_rows[] = {",
        ]
    )
    for row in payload["rows"]:
        lines.append(
            "    { "
            f"{c_u16(row.get('room_source_index'))}, "
            f"{binding_enum(str(row.get('scene_binding_kind', '')))}, "
            f"{c_u16(row.get('scene_binding_index'))}, "
            f"{c_u8(row.get('scene_id'))}, "
            f"{int_value(row.get('room_index'), -1)}, "
            f"{c_string(str(row.get('scene_path', '')))}, "
            f"{c_string(str(row.get('source_basename', '')))}, "
            f"{c_string(str(row.get('source_c_file', '')))}, "
            f"{c_string(str(row.get('scene_index_symbol', '')))}, "
            f"{c_string(str(row.get('room_path', '')))}, "
            f"{int_value(row.get('room_size'), 0)}u, "
            f"{c_string(str(row.get('room_actor_symbol', '')))}, "
            f"{c_string(str(row.get('room_object_symbol', '')))}, "
            f"{c_string(str(row.get('actor_list_status', '')))}, "
            f"{c_string(str(row.get('actor_list_confidence', '')))}, "
            f"{c_u16(row.get('actor_count'))}, "
            f"{c_u16(row.get('object_count'))}, "
            f"{c_u16(row.get('unknown_object_id_count'))}, "
            f"{c_u16(row.get('setup_ref_start'))}, "
            f"{c_u16(row.get('setup_ref_count'))}, "
            f"{c_u16(row.get('unique_setup_ref_count'))}, "
            f"{c_u16(row.get('embedded_cmb_count'))}, "
            f"{c_string(str(row.get('object_names', '')))}, "
            f"{c_string(str(row.get('actor_names', '')))}"
            " },"
        )
    lines.extend(
        [
            "};",
            "",
            "const u32 oot3d_scene_room_setup_ref_count = OOT3D_SCENE_ROOM_SETUP_REF_COUNT;",
            "const u32 oot3d_scene_room_source_row_count = OOT3D_SCENE_ROOM_SOURCE_ROW_COUNT;",
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
