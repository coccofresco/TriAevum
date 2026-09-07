#!/usr/bin/env python3
"""Build source-oriented OOT3D path record and path point tables.

Native command 0x0D stores OOT3D path records. The command handler at
0x002985F0 relocates each record's +4 pointer by the scene base and stores the
path table at play+0x5C20. Path_GetByIndex at 0x00348FF0 indexes 8-byte records.
This table materializes the decoded path records and their Vec3s point arrays
as source-like data bound to scene/setup/command rows.
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
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_path_source_table.json"
DEFAULT_OUT_PATH_CSV = ROOT / "analysis" / "scene_path_source_table.csv"
DEFAULT_OUT_POINT_CSV = ROOT / "analysis" / "scene_path_point_source_table.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_path_source_table.md"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "scene_path_source_table.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_scene_path_source_table.c"

UNMAPPED_INDEX = 0xFFFF
UNMAPPED_SCENE_ID = 0xFF
NO_OFFSET = 0xFFFFFFFF
COMMAND_PATH_LIST = 0x0D
PATH_RECORD_SIZE = 0x08


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


def raw_bytes(raw_hex: str) -> list[int]:
    raw = bytes.fromhex(raw_hex)
    if len(raw) != PATH_RECORD_SIZE:
        raise ValueError(f"expected 0x{PATH_RECORD_SIZE:02x} path record bytes, got {len(raw)}")
    return list(raw)


def raw_bytes_initializer(raw_hex: str) -> str:
    return "{ " + ", ".join(f"0x{byte:02x}u" for byte in raw_bytes(raw_hex)) + " }"


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
                "path_count": int_value(row.get("path_count"), 0),
                "paths_symbol": row.get("paths_symbol", ""),
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
        if scene_path and setup_index >= 0 and command_id == COMMAND_PATH_LIST:
            bindings[(scene_path, setup_index, command_id)] = {
                "command_source_index": int_value(row.get("command_source_index"), index),
                "command_index": int_value(row.get("command_index"), UNMAPPED_INDEX),
                "command_name": row.get("command_name", ""),
                "decoded_status": row.get("decoded_status", ""),
                "decoded_count": int_value(row.get("decoded_count"), 0),
                "decoded_expected_count": int_value(row.get("decoded_expected_count"), 0),
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


def binding_enum(value: str) -> str:
    return {
        "scene_row": "OOT3D_SCENE_COMMAND_BINDING_SCENE_ROW",
        "variant_row": "OOT3D_SCENE_COMMAND_BINDING_VARIANT_ROW",
        "unmapped": "OOT3D_SCENE_COMMAND_BINDING_UNMAPPED",
    }.get(value, "OOT3D_SCENE_COMMAND_BINDING_UNMAPPED")


def points_status_enum(value: str) -> str:
    return {
        "decoded_vec3s_points": "OOT3D_PATH_POINTS_DECODED_VEC3S",
        "empty": "OOT3D_PATH_POINTS_EMPTY",
        "invalid_offset": "OOT3D_PATH_POINTS_INVALID_OFFSET",
    }.get(value, "OOT3D_PATH_POINTS_INVALID_OFFSET")


def path_validation_status(record: dict[str, Any], decoded: dict[str, Any]) -> str:
    validation = as_dict(decoded.get("validation"))
    semantic_mapping = str(validation.get("semantic_mapping", ""))
    points_status = str(record.get("points_status", ""))
    if semantic_mapping == "code_bin_path_record_mapping_confirmed" and points_status == "decoded_vec3s_points":
        return "native_path_record_code_bin_mapping_confirmed_decoded_vec3s_points"
    if semantic_mapping == "code_bin_path_record_mapping_confirmed":
        return f"native_path_record_code_bin_mapping_confirmed_{points_status or 'unknown_points'}"
    return f"native_path_record_{points_status or 'unclassified'}"


def build_rows(
    full_payload: dict[str, Any],
    setup_bindings: dict[tuple[str, int], dict[str, Any]],
    command_bindings: dict[tuple[str, int, int], dict[str, Any]],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    path_rows: list[dict[str, Any]] = []
    point_rows: list[dict[str, Any]] = []
    for scene in as_list(full_payload.get("records")):
        if not isinstance(scene, dict):
            continue
        scene_path = str(scene.get("scene_path", ""))
        scene_key = scene_path.lower()
        for setup in as_list(scene.get("setups")):
            if not isinstance(setup, dict):
                continue
            setup_index = int_value(setup.get("index"), -1)
            setup_binding = setup_bindings.get((scene_key, setup_index), {})
            command_binding = command_bindings.get((scene_key, setup_index, COMMAND_PATH_LIST), {})
            for command_position, command in enumerate(as_list(setup.get("commands"))):
                if not isinstance(command, dict):
                    continue
                if int_value(command.get("command_id"), -1) != COMMAND_PATH_LIST:
                    continue
                decoded = as_dict(command.get("decoded"))
                validation = as_dict(decoded.get("validation"))
                for record_index, record in enumerate(as_list(decoded.get("raw_records"))):
                    if not isinstance(record, dict):
                        continue
                    point_ref_start = len(point_rows)
                    path_source_index = len(path_rows)
                    points = [point for point in as_list(record.get("points")) if isinstance(point, dict)]
                    for point_index, point in enumerate(points):
                        point_rows.append(
                            {
                                "point_source_index": len(point_rows),
                                "path_source_index": path_source_index,
                                "setup_source_index": int_value(
                                    setup_binding.get("setup_source_index"), UNMAPPED_INDEX
                                ),
                                "command_source_index": int_value(
                                    command_binding.get("command_source_index"), UNMAPPED_INDEX
                                ),
                                "scene_id": int_value(setup_binding.get("scene_id"), UNMAPPED_SCENE_ID),
                                "scene_id_hex": setup_binding.get("scene_id_hex", ""),
                                "scene_binding_kind": setup_binding.get("scene_binding_kind", "unmapped"),
                                "scene_binding_index": int_value(
                                    setup_binding.get("scene_binding_index"), UNMAPPED_INDEX
                                ),
                                "scene_path": scene_path,
                                "source_basename": setup_binding.get("source_basename", ""),
                                "setup_index": setup_index,
                                "setup_role": setup_binding.get("setup_role", ""),
                                "command_index": int_value(
                                    command_binding.get("command_index"), command_position
                                ),
                                "path_index": int_value(record.get("index"), record_index),
                                "point_index": int_value(point.get("index"), point_index),
                                "offset": int_value(point.get("offset"), NO_OFFSET),
                                "offset_hex": point.get("offset_hex", ""),
                                "x": int_value(point.get("x"), 0),
                                "y": int_value(point.get("y"), 0),
                                "z": int_value(point.get("z"), 0),
                            }
                        )
                    raw_hex = str(record.get("raw_hex", ""))
                    path_rows.append(
                        {
                            "path_source_index": path_source_index,
                            "setup_source_index": int_value(
                                setup_binding.get("setup_source_index"), UNMAPPED_INDEX
                            ),
                            "command_source_index": int_value(
                                command_binding.get("command_source_index"), UNMAPPED_INDEX
                            ),
                            "scene_id": int_value(setup_binding.get("scene_id"), UNMAPPED_SCENE_ID),
                            "scene_id_hex": setup_binding.get("scene_id_hex", ""),
                            "scene_binding_kind": setup_binding.get("scene_binding_kind", "unmapped"),
                            "scene_binding_index": int_value(
                                setup_binding.get("scene_binding_index"), UNMAPPED_INDEX
                            ),
                            "scene_path": scene_path,
                            "source_basename": setup_binding.get("source_basename", ""),
                            "source_c_file": setup_binding.get("source_c_file", ""),
                            "scene_index_symbol": setup_binding.get("scene_index_symbol", ""),
                            "setup_index": setup_index,
                            "setup_role": setup_binding.get("setup_role", ""),
                            "setup_symbol": setup_binding.get("setup_symbol", ""),
                            "paths_symbol": setup_binding.get("paths_symbol", ""),
                            "command_index": int_value(command_binding.get("command_index"), command_position),
                            "command_name": command_binding.get("command_name", ""),
                            "command_support_level": command_binding.get("support_level", ""),
                            "handler_support_level": command_binding.get("handler_support_level", ""),
                            "handler_binding_status": command_binding.get("handler_binding_status", ""),
                            "handler_name": command_binding.get("handler_name", ""),
                            "handler_entry": command_binding.get("handler_entry", ""),
                            "handler_runtime_store_count": int_value(
                                command_binding.get("handler_runtime_store_count"), 0
                            ),
                            "payload_symbol": command_binding.get("payload_symbol", ""),
                            "export_structs": command_binding.get("export_structs", ""),
                            "open_questions": command_binding.get("open_questions", ""),
                            "path_index": int_value(record.get("index"), record_index),
                            "offset": int_value(record.get("offset"), NO_OFFSET),
                            "offset_hex": record.get("offset_hex", ""),
                            "raw_hex": raw_hex,
                            "record_size": PATH_RECORD_SIZE,
                            "point_count": int_value(record.get("point_count"), 0),
                            "unk_01": int_value(record.get("unk_01"), 0),
                            "unk_02": int_value(record.get("unk_02"), 0),
                            "word0": int_value(record.get("word0"), 0),
                            "word0_hex": record.get("word0_hex", ""),
                            "points_offset": int_value(record.get("points_offset"), NO_OFFSET),
                            "points_offset_hex": record.get("points_offset_hex", ""),
                            "points_status": record.get("points_status", ""),
                            "point_ref_start": point_ref_start,
                            "point_ref_count": len(points),
                            "decoded_status": decoded.get("status", ""),
                            "expected_path_count": int_value(decoded.get("expected_path_count"), 0),
                            "payload_start": int_value(decoded.get("payload_start"), NO_OFFSET),
                            "payload_start_hex": decoded.get("payload_start_hex", ""),
                            "validation_status": path_validation_status(record, decoded),
                            "source_validation_status": validation.get("status", ""),
                            "source_semantic_mapping": validation.get("semantic_mapping", ""),
                            "consumer_command_handler": as_dict(record.get("consumer_evidence")).get(
                                "command_handler", ""
                            ),
                            "consumer_general_accessor": as_dict(record.get("consumer_evidence")).get(
                                "general_accessor", ""
                            ),
                            "consumer_record_layout": as_dict(record.get("consumer_evidence")).get(
                                "record_layout", ""
                            ),
                        }
                    )
    return path_rows, point_rows


def build_report() -> dict[str, Any]:
    full_payload = json.loads(DEFAULT_NATIVE_FULL_INDEX.read_text(encoding="utf-8"))
    source_payload = json.loads(DEFAULT_NATIVE_SOURCE_INDEX.read_text(encoding="utf-8"))
    setup_payload = json.loads(DEFAULT_SETUP_SOURCE_TABLE.read_text(encoding="utf-8"))
    command_payload = json.loads(DEFAULT_COMMAND_SOURCE_TABLE.read_text(encoding="utf-8"))
    setup_bindings = setup_source_bindings(setup_payload)
    command_bindings = command_source_bindings(command_payload)
    path_rows, point_rows = build_rows(full_payload, setup_bindings, command_bindings)

    expected_path_count = int_value(source_payload.get("summary", {}).get("payload_totals", {}).get("paths"))
    scene_binding_counts = Counter(str(row.get("scene_binding_kind", "")) for row in path_rows)
    support_counts = Counter(str(row.get("command_support_level", "")) for row in path_rows)
    handler_support_counts = Counter(str(row.get("handler_support_level", "")) for row in path_rows)
    validation_counts = Counter(str(row.get("validation_status", "")) for row in path_rows)
    points_status_counts = Counter(str(row.get("points_status", "")) for row in path_rows)
    decoded_status_counts = Counter(str(row.get("decoded_status", "")) for row in path_rows)
    top_scene_counts = Counter(str(row.get("scene_path", "")) for row in path_rows)

    setup_count_mismatches = []
    paths_by_setup = Counter(int_value(row.get("setup_source_index"), UNMAPPED_INDEX) for row in path_rows)
    for setup in as_list(setup_payload.get("rows")):
        if not isinstance(setup, dict):
            continue
        setup_source_index = int_value(setup.get("setup_source_index"), UNMAPPED_INDEX)
        expected = int_value(setup.get("path_count"), 0)
        actual = paths_by_setup.get(setup_source_index, 0)
        if expected != actual:
            setup_count_mismatches.append(
                {
                    "setup_source_index": setup_source_index,
                    "scene_path": setup.get("scene_path", ""),
                    "setup_index": int_value(setup.get("setup_index"), -1),
                    "expected_path_count": expected,
                    "actual_path_count": actual,
                }
            )

    command_count_mismatches = []
    paths_by_command = Counter(int_value(row.get("command_source_index"), UNMAPPED_INDEX) for row in path_rows)
    for command in as_list(command_payload.get("command_rows")):
        if not isinstance(command, dict):
            continue
        if int_value(command.get("command_id"), -1) != COMMAND_PATH_LIST:
            continue
        command_source_index = int_value(command.get("command_source_index"), UNMAPPED_INDEX)
        expected = int_value(command.get("decoded_count"), 0)
        actual = paths_by_command.get(command_source_index, 0)
        if expected != actual:
            command_count_mismatches.append(
                {
                    "command_source_index": command_source_index,
                    "scene_path": command.get("scene_path", ""),
                    "setup_index": int_value(command.get("setup_index"), -1),
                    "expected_path_count": expected,
                    "actual_path_count": actual,
                }
            )

    point_count_mismatches = [
        row
        for row in path_rows
        if int_value(row.get("point_count"), 0) != int_value(row.get("point_ref_count"), 0)
    ]
    raw_length_mismatches = [
        row for row in path_rows if len(str(row.get("raw_hex", ""))) != PATH_RECORD_SIZE * 2
    ]
    unmapped_setup_rows = [
        row for row in path_rows if int_value(row.get("setup_source_index"), UNMAPPED_INDEX) == UNMAPPED_INDEX
    ]
    unmapped_command_rows = [
        row for row in path_rows if int_value(row.get("command_source_index"), UNMAPPED_INDEX) == UNMAPPED_INDEX
    ]
    semantic_pending_rows = [row for row in path_rows if str(row.get("open_questions", ""))]

    summary = {
        "format": "oot3d_scene_path_source_table_v1",
        "path_source_row_count": len(path_rows),
        "path_point_source_row_count": len(point_rows),
        "expected_path_count": expected_path_count,
        "path_command_count": len(command_bindings),
        "scene_binding_kind_counts": dict(sorted(scene_binding_counts.items())),
        "command_support_level_counts": dict(sorted(support_counts.items())),
        "handler_support_level_counts": dict(sorted(handler_support_counts.items())),
        "decoded_status_counts": dict(sorted(decoded_status_counts.items())),
        "points_status_counts": dict(sorted(points_status_counts.items())),
        "validation_status_counts": dict(sorted(validation_counts.items())),
        "top_scene_path_counts": dict(top_scene_counts.most_common(20)),
        "setup_count_mismatch_count": len(setup_count_mismatches),
        "command_count_mismatch_count": len(command_count_mismatches),
        "point_count_mismatch_count": len(point_count_mismatches),
        "raw_length_mismatch_count": len(raw_length_mismatches),
        "unmapped_setup_count": len(unmapped_setup_rows),
        "unmapped_command_count": len(unmapped_command_rows),
        "semantic_pending_count": len(semantic_pending_rows),
    }
    return {
        "summary": summary,
        "source_policy": {
            "native_full_scene_index": str(DEFAULT_NATIVE_FULL_INDEX),
            "native_scene_source_index": str(DEFAULT_NATIVE_SOURCE_INDEX),
            "setup_source_table": str(DEFAULT_SETUP_SOURCE_TABLE),
            "command_source_table": str(DEFAULT_COMMAND_SOURCE_TABLE),
            "promoted_layout": "OOT3D command 0x0D path records: byte +0 point count, word +4 relocated Vec3s point-list pointer, 8-byte stride.",
            "n64_policy": "No N64 path tables are read by this integration table.",
        },
        "path_rows": path_rows,
        "point_rows": point_rows,
        "setup_count_mismatches": setup_count_mismatches,
        "command_count_mismatches": command_count_mismatches,
        "point_count_mismatches": point_count_mismatches,
        "raw_length_mismatches": raw_length_mismatches,
        "unmapped_setup_rows": unmapped_setup_rows,
        "unmapped_command_rows": unmapped_command_rows,
        "semantic_pending_rows": semantic_pending_rows,
    }


PATH_CSV_COLUMNS = [
    "path_source_index",
    "scene_id_hex",
    "scene_id",
    "scene_path",
    "setup_source_index",
    "setup_index",
    "setup_role",
    "command_source_index",
    "command_index",
    "path_index",
    "offset_hex",
    "point_count",
    "point_ref_start",
    "point_ref_count",
    "points_offset_hex",
    "points_status",
    "validation_status",
    "command_support_level",
    "handler_support_level",
    "payload_symbol",
    "scene_binding_kind",
    "scene_binding_index",
    "scene_index_symbol",
]

POINT_CSV_COLUMNS = [
    "point_source_index",
    "path_source_index",
    "scene_id_hex",
    "scene_id",
    "scene_path",
    "setup_source_index",
    "setup_index",
    "command_source_index",
    "path_index",
    "point_index",
    "offset_hex",
    "x",
    "y",
    "z",
    "scene_binding_kind",
    "scene_binding_index",
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
        "# Scene Path Source Table",
        "",
        "Generated from OOT3D-native command 0x0D path-list payloads.",
        "",
        "## Summary",
        "",
        f"- Path source rows: {summary['path_source_row_count']}",
        f"- Path point source rows: {summary['path_point_source_row_count']}",
        f"- Expected path count: {summary['expected_path_count']}",
        f"- Path commands: {summary['path_command_count']}",
        f"- Scene binding: `{json.dumps(summary['scene_binding_kind_counts'], sort_keys=True)}`",
        f"- Command support level: `{json.dumps(summary['command_support_level_counts'], sort_keys=True)}`",
        f"- Handler support level: `{json.dumps(summary['handler_support_level_counts'], sort_keys=True)}`",
        f"- Decoded status: `{json.dumps(summary['decoded_status_counts'], sort_keys=True)}`",
        f"- Points status: `{json.dumps(summary['points_status_counts'], sort_keys=True)}`",
        f"- Validation status: `{json.dumps(summary['validation_status_counts'], sort_keys=True)}`",
        f"- Setup count mismatches: {summary['setup_count_mismatch_count']}",
        f"- Command count mismatches: {summary['command_count_mismatch_count']}",
        f"- Point count mismatches: {summary['point_count_mismatch_count']}",
        f"- Raw length mismatches: {summary['raw_length_mismatch_count']}",
        f"- Unmapped setup rows: {summary['unmapped_setup_count']}",
        f"- Unmapped command rows: {summary['unmapped_command_count']}",
        f"- Semantic pending rows: {summary['semantic_pending_count']}",
        "",
        "## Top Scene Path Counts",
        "",
        "| Scene | Count |",
        "| --- | ---: |",
    ]
    for scene_path, count in summary["top_scene_path_counts"].items():
        lines.append(f"| `{scene_path}` | {count} |")
    lines.extend(
        [
            "",
            "## Path Rows",
            "",
            "| # | Scene | Setup | Path | Points | Point Slice | Points Offset | Status |",
            "| ---: | --- | ---: | ---: | ---: | --- | --- | --- |",
        ]
    )
    for row in payload["path_rows"]:
        point_slice = f"{row['point_ref_start']}+{row['point_ref_count']}"
        lines.append(
            f"| {row['path_source_index']} | `{row['scene_path']}` | {row['setup_index']} | "
            f"{row['path_index']} | {row['point_count']} | `{point_slice}` | "
            f"`{row['points_offset_hex']}` | `{row['validation_status']}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_header(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "#ifndef OOT3D_SCENE_PATH_SOURCE_TABLE_H",
        "#define OOT3D_SCENE_PATH_SOURCE_TABLE_H",
        "",
        '#include "oot3d/scene.h"',
        '#include "oot3d/scene_command_source_table.h"',
        "",
        "enum {",
        f"    OOT3D_SCENE_PATH_SOURCE_ROW_COUNT = {len(payload['path_rows'])},",
        f"    OOT3D_SCENE_PATH_POINT_SOURCE_ROW_COUNT = {len(payload['point_rows'])},",
        "    OOT3D_SCENE_PATH_SOURCE_UNMAPPED_INDEX = 0xFFFF,",
        "    OOT3D_SCENE_PATH_SOURCE_NO_OFFSET = 0xFFFFFFFF,",
        "};",
        "",
        "typedef struct {",
        "    u16 pointSourceIndex;",
        "    u16 pathSourceIndex;",
        "    u16 pointIndex;",
        "    u32 offset;",
        "    Oot3dVec3s pos;",
        "} Oot3dScenePathPointSourceRow;",
        "",
        "typedef struct {",
        "    u16 pathSourceIndex;",
        "    Oot3dSceneCommandBindingKind bindingKind;",
        "    u16 bindingIndex;",
        "    u8 sceneId;",
        "    u16 setupSourceIndex;",
        "    u16 commandSourceIndex;",
        "    u16 setupIndex;",
        "    u16 commandIndex;",
        "    u16 pathIndex;",
        "    u32 offset;",
        "    u8 raw[OOT3D_PATH_RECORD_SIZE];",
        "    u8 pointCount;",
        "    u8 unk01;",
        "    u16 unk02;",
        "    u32 rawPointsOffset;",
        "    Oot3dPathPointsStatus pointsStatus;",
        "    u16 pointRefStart;",
        "    u16 pointRefCount;",
        "    const char* scenePath;",
        "    const char* sourceBasename;",
        "    const char* setupRole;",
        "    const char* pathsSymbol;",
        "    const char* payloadSymbol;",
        "    const char* validationStatus;",
        "    const char* openQuestions;",
        "    const char* handlerName;",
        "    u32 handlerEntry;",
        "} Oot3dScenePathSourceRow;",
        "",
        "extern const Oot3dScenePathPointSourceRow oot3d_scene_path_point_source_rows[];",
        "extern const Oot3dScenePathSourceRow oot3d_scene_path_source_rows[];",
        "extern const u32 oot3d_scene_path_point_source_row_count;",
        "extern const u32 oot3d_scene_path_source_row_count;",
        "",
        "#endif",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def write_source(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "/* Generated by build_scene_path_source_table.py. */",
        "",
        '#include "oot3d/scene_path_source_table.h"',
        "",
        "const Oot3dScenePathPointSourceRow oot3d_scene_path_point_source_rows[] = {",
    ]
    for row in payload["point_rows"]:
        lines.append(
            "    { "
            f"{c_u16(row.get('point_source_index'))}, "
            f"{c_u16(row.get('path_source_index'))}, "
            f"{c_u16(row.get('point_index'))}, "
            f"{c_u32(row.get('offset'))}, "
            f"{{ {c_s16(row.get('x'))}, {c_s16(row.get('y'))}, {c_s16(row.get('z'))} }}"
            " },"
        )
    lines.extend(["};", "", "const Oot3dScenePathSourceRow oot3d_scene_path_source_rows[] = {"])
    for row in payload["path_rows"]:
        lines.append(
            "    { "
            f"{c_u16(row.get('path_source_index'))}, "
            f"{binding_enum(str(row.get('scene_binding_kind', '')))}, "
            f"{c_u16(row.get('scene_binding_index'))}, "
            f"{c_u8(row.get('scene_id'))}, "
            f"{c_u16(row.get('setup_source_index'))}, "
            f"{c_u16(row.get('command_source_index'))}, "
            f"{c_u16(row.get('setup_index'))}, "
            f"{c_u16(row.get('command_index'))}, "
            f"{c_u16(row.get('path_index'))}, "
            f"{c_u32(row.get('offset'))}, "
            f"{raw_bytes_initializer(str(row.get('raw_hex', '')))}, "
            f"{c_u8(row.get('point_count'))}, "
            f"{c_u8(row.get('unk_01'))}, "
            f"{c_u16(row.get('unk_02'))}, "
            f"{c_u32(row.get('points_offset'))}, "
            f"{points_status_enum(str(row.get('points_status', '')))}, "
            f"{c_u16(row.get('point_ref_start'))}, "
            f"{c_u16(row.get('point_ref_count'))}, "
            f"{c_string(str(row.get('scene_path', '')))}, "
            f"{c_string(str(row.get('source_basename', '')))}, "
            f"{c_string(str(row.get('setup_role', '')))}, "
            f"{c_string(str(row.get('paths_symbol', '')))}, "
            f"{c_string(str(row.get('payload_symbol', '')))}, "
            f"{c_string(str(row.get('validation_status', '')))}, "
            f"{c_string(str(row.get('open_questions', '')))}, "
            f"{c_string(str(row.get('handler_name', '')))}, "
            f"{c_u32(parse_handler_entry(row.get('handler_entry')))}"
            " },"
        )
    lines.extend(
        [
            "};",
            "",
            "const u32 oot3d_scene_path_point_source_row_count = OOT3D_SCENE_PATH_POINT_SOURCE_ROW_COUNT;",
            "const u32 oot3d_scene_path_source_row_count = OOT3D_SCENE_PATH_SOURCE_ROW_COUNT;",
            "",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    payload = build_report()
    write_json(DEFAULT_OUT_JSON, payload)
    write_csv(DEFAULT_OUT_PATH_CSV, PATH_CSV_COLUMNS, payload["path_rows"])
    write_csv(DEFAULT_OUT_POINT_CSV, POINT_CSV_COLUMNS, payload["point_rows"])
    write_markdown(DEFAULT_OUT_MD, payload)
    write_header(DEFAULT_OUT_HEADER, payload)
    write_source(DEFAULT_OUT_SOURCE, payload)
    for output in (
        DEFAULT_OUT_JSON,
        DEFAULT_OUT_PATH_CSV,
        DEFAULT_OUT_POINT_CSV,
        DEFAULT_OUT_MD,
        DEFAULT_OUT_HEADER,
        DEFAULT_OUT_SOURCE,
    ):
        print(output)
    print(json.dumps(payload["summary"], indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
