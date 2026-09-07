#!/usr/bin/env python3
"""Build a source-oriented OOT3D scene setup setting table.

This table materializes the single-record setup payloads that are already
decoded from native OOT3D scene commands:

- 0x07 special files
- 0x11 skybox settings
- 0x15 sound settings
- 0x17 cutscene references
- 0x19 misc settings

Collision headers and path lists are intentionally left to dedicated tables
because they carry nested structures beyond scalar setup settings.
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
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_setting_source_table.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "scene_setting_source_table.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_setting_source_table.md"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "scene_setting_source_table.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_scene_setting_source_table.c"

UNMAPPED_INDEX = 0xFFFF
UNMAPPED_SCENE_ID = 0xFF
NO_OFFSET = 0xFFFFFFFF

COMMAND_SPECIAL_FILES = 0x07
COMMAND_SKYBOX_SETTINGS = 0x11
COMMAND_SOUND_SETTINGS = 0x15
COMMAND_CUTSCENE_DATA = 0x17
COMMAND_MISC_SETTINGS = 0x19

SETTING_COMMANDS = {
    COMMAND_SPECIAL_FILES,
    COMMAND_SKYBOX_SETTINGS,
    COMMAND_SOUND_SETTINGS,
    COMMAND_CUTSCENE_DATA,
    COMMAND_MISC_SETTINGS,
}

SETTING_KIND_BY_COMMAND = {
    COMMAND_SPECIAL_FILES: "special_files",
    COMMAND_SKYBOX_SETTINGS: "skybox_settings",
    COMMAND_SOUND_SETTINGS: "sound_settings",
    COMMAND_CUTSCENE_DATA: "cutscene_reference",
    COMMAND_MISC_SETTINGS: "misc_settings",
}

EXPECTED_PAYLOAD_KEY_BY_COMMAND = {
    COMMAND_SPECIAL_FILES: "special_files",
    COMMAND_SKYBOX_SETTINGS: "skybox_settings",
    COMMAND_SOUND_SETTINGS: "sound_settings",
    COMMAND_CUTSCENE_DATA: "cutscenes",
    COMMAND_MISC_SETTINGS: "misc_settings",
}


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


def c_u8(value: Any) -> str:
    return f"{int_value(value) & 0xFF}u"


def c_s16(value: Any) -> str:
    return str(max(-32768, min(32767, int_value(value))))


def c_u16(value: Any) -> str:
    return f"{int_value(value) & 0xFFFF}u"


def c_u32(value: Any) -> str:
    return f"{int_value(value) & 0xFFFFFFFF}u"


def c_bool(value: Any) -> str:
    return "1u" if bool_value(value) else "0u"


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
                "special_files_count": int_value(row.get("special_file_count"), 0),
                "special_files_symbol": row.get("special_files_symbol", ""),
                "skybox_settings_count": int_value(row.get("skybox_settings_count"), 0),
                "skybox_settings_symbol": row.get("skybox_settings_symbol", ""),
                "sound_settings_count": int_value(row.get("sound_settings_count"), 0),
                "sound_settings_symbol": row.get("sound_settings_symbol", ""),
                "cutscene_count": int_value(row.get("cutscene_count"), 0),
                "cutscenes_symbol": row.get("cutscenes_symbol", ""),
                "misc_settings_count": int_value(row.get("misc_settings_count"), 0),
                "misc_settings_symbol": row.get("misc_settings_symbol", ""),
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
        if scene_path and setup_index >= 0 and command_id in SETTING_COMMANDS:
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
                "handler_runtime_store_count": int_value(row.get("handler_runtime_store_count"), 0),
                "payload_symbol": row.get("payload_symbol", ""),
                "export_structs": row.get("export_structs", ""),
                "open_questions": row.get("open_questions", ""),
            }
    return bindings


def payload_symbol_for_setting(setup_binding: dict[str, Any], command_id: int) -> str:
    key = {
        COMMAND_SPECIAL_FILES: "special_files_symbol",
        COMMAND_SKYBOX_SETTINGS: "skybox_settings_symbol",
        COMMAND_SOUND_SETTINGS: "sound_settings_symbol",
        COMMAND_CUTSCENE_DATA: "cutscenes_symbol",
        COMMAND_MISC_SETTINGS: "misc_settings_symbol",
    }[command_id]
    return str(setup_binding.get(key, ""))


def setup_count_for_setting(setup_binding: dict[str, Any], command_id: int) -> int:
    key = {
        COMMAND_SPECIAL_FILES: "special_files_count",
        COMMAND_SKYBOX_SETTINGS: "skybox_settings_count",
        COMMAND_SOUND_SETTINGS: "sound_settings_count",
        COMMAND_CUTSCENE_DATA: "cutscene_count",
        COMMAND_MISC_SETTINGS: "misc_settings_count",
    }[command_id]
    return int_value(setup_binding.get(key), 0)


def setting_validation_status(command_id: int, command_binding: dict[str, Any]) -> str:
    support_level = str(command_binding.get("support_level", ""))
    has_open_questions = bool(str(command_binding.get("open_questions", "")))
    suffix = "semantic_pending" if has_open_questions else "confirmed"
    return f"native_{SETTING_KIND_BY_COMMAND[command_id]}_{support_level or 'unresolved'}_{suffix}"


def blank_setting_fields() -> dict[str, Any]:
    return {
        "c_up_elf_message_file": 0,
        "keep_object_id": 0,
        "keep_object_name": "",
        "skybox_id": 0,
        "weather_or_unk_05": 0,
        "indoors": 0,
        "sound_spec_id": 0,
        "nature_ambience_id": 0,
        "sound_data3": 0,
        "bgm_sound_id": 0,
        "cutscene_offset": NO_OFFSET,
        "cutscene_offset_hex": "",
        "cutscene_in_file": False,
        "camera_or_world_map_area": 0,
        "misc_raw_argument": 0,
    }


def decoded_setting_fields(command_id: int, decoded: dict[str, Any]) -> dict[str, Any]:
    fields = blank_setting_fields()
    if command_id == COMMAND_SPECIAL_FILES:
        fields.update(
            {
                "c_up_elf_message_file": int_value(decoded.get("c_up_elf_message_file"), 0),
                "keep_object_id": int_value(decoded.get("keep_object_id"), 0),
                "keep_object_name": decoded.get("keep_object_name", ""),
            }
        )
    elif command_id == COMMAND_SKYBOX_SETTINGS:
        fields.update(
            {
                "skybox_id": int_value(decoded.get("skybox_id"), 0),
                "weather_or_unk_05": int_value(decoded.get("weather_or_unk_05"), 0),
                "indoors": int_value(decoded.get("indoors"), 0),
            }
        )
    elif command_id == COMMAND_SOUND_SETTINGS:
        fields.update(
            {
                "sound_spec_id": int_value(decoded.get("spec_id"), 0),
                "nature_ambience_id": int_value(decoded.get("nature_ambience_id"), 0),
                "sound_data3": int_value(decoded.get("data3"), 0),
                "bgm_sound_id": int_value(decoded.get("bgm_sound_id"), 0),
            }
        )
    elif command_id == COMMAND_CUTSCENE_DATA:
        fields.update(
            {
                "cutscene_offset": int_value(decoded.get("cutscene_offset"), NO_OFFSET),
                "cutscene_offset_hex": decoded.get("cutscene_offset_hex", ""),
                "cutscene_in_file": bool_value(decoded.get("in_file")),
            }
        )
    elif command_id == COMMAND_MISC_SETTINGS:
        fields.update(
            {
                "camera_or_world_map_area": int_value(decoded.get("camera_or_world_map_area"), 0),
                "misc_raw_argument": int_value(decoded.get("raw_argument"), 0),
            }
        )
    return fields


def build_rows(
    full_payload: dict[str, Any],
    setup_bindings: dict[tuple[str, int], dict[str, Any]],
    command_bindings: dict[tuple[str, int, int], dict[str, Any]],
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
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
            for command_position, command in enumerate(as_list(setup.get("commands"))):
                if not isinstance(command, dict):
                    continue
                command_id = int_value(command.get("command_id"), -1)
                if command_id not in SETTING_COMMANDS:
                    continue
                command_binding = command_bindings.get((scene_key, setup_index, command_id), {})
                decoded = as_dict(command.get("decoded"))
                row = {
                    "setting_source_index": len(rows),
                    "setting_kind": SETTING_KIND_BY_COMMAND[command_id],
                    "setup_source_index": int_value(setup_binding.get("setup_source_index"), UNMAPPED_INDEX),
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
                    "command_index": int_value(command_binding.get("command_index"), command_position),
                    "command_id": command_id,
                    "command_id_hex": f"0x{command_id:02x}",
                    "command_name": command_binding.get("command_name", command.get("command_name", "")),
                    "command_support_level": command_binding.get("support_level", ""),
                    "handler_binding_status": command_binding.get("handler_binding_status", ""),
                    "handler_name": command_binding.get("handler_name", ""),
                    "handler_entry": command_binding.get("handler_entry", ""),
                    "handler_runtime_store_count": int_value(
                        command_binding.get("handler_runtime_store_count"), 0
                    ),
                    "offset": int_value(command.get("offset"), NO_OFFSET),
                    "offset_hex": command.get("offset_hex", ""),
                    "argument": int_value(command.get("argument"), 0),
                    "argument_hex": command.get("argument_hex", ""),
                    "payload_symbol": command_binding.get("payload_symbol", "")
                    or payload_symbol_for_setting(setup_binding, command_id),
                    "setup_payload_count": setup_count_for_setting(setup_binding, command_id),
                    "export_structs": command_binding.get("export_structs", ""),
                    "decoded_status": decoded.get("status", ""),
                    "open_questions": command_binding.get("open_questions", ""),
                    "validation_status": setting_validation_status(command_id, command_binding),
                }
                row.update(decoded_setting_fields(command_id, decoded))
                rows.append(row)
    return rows


def build_report() -> dict[str, Any]:
    full_payload = json.loads(DEFAULT_NATIVE_FULL_INDEX.read_text(encoding="utf-8"))
    source_payload = json.loads(DEFAULT_NATIVE_SOURCE_INDEX.read_text(encoding="utf-8"))
    setup_payload = json.loads(DEFAULT_SETUP_SOURCE_TABLE.read_text(encoding="utf-8"))
    command_payload = json.loads(DEFAULT_COMMAND_SOURCE_TABLE.read_text(encoding="utf-8"))
    setup_bindings = setup_source_bindings(setup_payload)
    command_bindings = command_source_bindings(command_payload)
    rows = build_rows(full_payload, setup_bindings, command_bindings)

    payload_totals = as_dict(source_payload.get("summary", {}).get("payload_totals"))
    expected_by_kind = {
        SETTING_KIND_BY_COMMAND[command_id]: int_value(payload_totals.get(payload_key), 0)
        for command_id, payload_key in EXPECTED_PAYLOAD_KEY_BY_COMMAND.items()
    }
    row_counts_by_kind = Counter(str(row.get("setting_kind", "")) for row in rows)
    support_counts = Counter(str(row.get("command_support_level", "")) for row in rows)
    validation_counts = Counter(str(row.get("validation_status", "")) for row in rows)
    scene_binding_counts = Counter(str(row.get("scene_binding_kind", "")) for row in rows)
    decoded_status_counts = Counter(str(row.get("decoded_status", "")) for row in rows)

    setup_count_mismatches = []
    rows_by_setup_kind = Counter(
        (int_value(row.get("setup_source_index"), UNMAPPED_INDEX), str(row.get("setting_kind", "")))
        for row in rows
    )
    setup_count_key_by_kind = {
        "special_files": "special_file_count",
        "skybox_settings": "skybox_settings_count",
        "sound_settings": "sound_settings_count",
        "cutscene_reference": "cutscene_count",
        "misc_settings": "misc_settings_count",
    }
    for setup in as_list(setup_payload.get("rows")):
        if not isinstance(setup, dict):
            continue
        setup_source_index = int_value(setup.get("setup_source_index"), UNMAPPED_INDEX)
        for kind, setup_key in setup_count_key_by_kind.items():
            expected = int_value(setup.get(setup_key), 0)
            actual = rows_by_setup_kind.get((setup_source_index, kind), 0)
            if expected != actual:
                setup_count_mismatches.append(
                    {
                        "setup_source_index": setup_source_index,
                        "scene_path": setup.get("scene_path", ""),
                        "setup_index": int_value(setup.get("setup_index"), -1),
                        "setting_kind": kind,
                        "expected_count": expected,
                        "actual_count": actual,
                    }
                )

    command_count_mismatches = []
    rows_by_command = Counter(int_value(row.get("command_source_index"), UNMAPPED_INDEX) for row in rows)
    for command in as_list(command_payload.get("command_rows")):
        if not isinstance(command, dict):
            continue
        command_id = int_value(command.get("command_id"), -1)
        if command_id not in SETTING_COMMANDS:
            continue
        command_source_index = int_value(command.get("command_source_index"), UNMAPPED_INDEX)
        expected = int_value(command.get("decoded_count"), 0)
        actual = rows_by_command.get(command_source_index, 0)
        if expected != actual:
            command_count_mismatches.append(
                {
                    "command_source_index": command_source_index,
                    "scene_path": command.get("scene_path", ""),
                    "setup_index": int_value(command.get("setup_index"), -1),
                    "command_id": command_id,
                    "setting_kind": SETTING_KIND_BY_COMMAND[command_id],
                    "expected_count": expected,
                    "actual_count": actual,
                }
            )

    unmapped_setup_rows = [
        row for row in rows if int_value(row.get("setup_source_index"), UNMAPPED_INDEX) == UNMAPPED_INDEX
    ]
    unmapped_command_rows = [
        row for row in rows if int_value(row.get("command_source_index"), UNMAPPED_INDEX) == UNMAPPED_INDEX
    ]
    semantic_pending_rows = [row for row in rows if str(row.get("open_questions", ""))]
    kind_count_mismatches = [
        {
            "setting_kind": kind,
            "expected_count": expected,
            "actual_count": row_counts_by_kind.get(kind, 0),
        }
        for kind, expected in sorted(expected_by_kind.items())
        if expected != row_counts_by_kind.get(kind, 0)
    ]

    summary = {
        "format": "oot3d_scene_setting_source_table_v1",
        "setting_source_row_count": len(rows),
        "expected_setting_source_row_count": sum(expected_by_kind.values()),
        "expected_counts_by_kind": dict(sorted(expected_by_kind.items())),
        "row_counts_by_kind": dict(sorted(row_counts_by_kind.items())),
        "scene_binding_kind_counts": dict(sorted(scene_binding_counts.items())),
        "command_support_level_counts": dict(sorted(support_counts.items())),
        "decoded_status_counts": dict(sorted(decoded_status_counts.items())),
        "validation_status_counts": dict(sorted(validation_counts.items())),
        "setup_count_mismatch_count": len(setup_count_mismatches),
        "command_count_mismatch_count": len(command_count_mismatches),
        "kind_count_mismatch_count": len(kind_count_mismatches),
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
            "included_commands": {
                f"0x{command_id:02x}": SETTING_KIND_BY_COMMAND[command_id]
                for command_id in sorted(SETTING_COMMANDS)
            },
            "excluded_commands": "Collision headers and path lists are intentionally kept for dedicated nested-structure tables.",
            "n64_policy": "No N64 setup-setting tables are read by this integration table.",
        },
        "rows": rows,
        "setup_count_mismatches": setup_count_mismatches,
        "command_count_mismatches": command_count_mismatches,
        "kind_count_mismatches": kind_count_mismatches,
        "unmapped_setup_rows": unmapped_setup_rows,
        "unmapped_command_rows": unmapped_command_rows,
        "semantic_pending_rows": semantic_pending_rows,
    }


CSV_COLUMNS = [
    "setting_source_index",
    "setting_kind",
    "scene_id_hex",
    "scene_id",
    "scene_path",
    "setup_source_index",
    "setup_index",
    "setup_role",
    "command_source_index",
    "command_index",
    "command_id_hex",
    "offset_hex",
    "argument_hex",
    "c_up_elf_message_file",
    "keep_object_id",
    "keep_object_name",
    "skybox_id",
    "weather_or_unk_05",
    "indoors",
    "sound_spec_id",
    "nature_ambience_id",
    "sound_data3",
    "bgm_sound_id",
    "cutscene_offset_hex",
    "cutscene_in_file",
    "camera_or_world_map_area",
    "misc_raw_argument",
    "validation_status",
    "command_support_level",
    "payload_symbol",
    "open_questions",
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
        "# Scene Setting Source Table",
        "",
        "Generated from OOT3D-native scalar setup-setting commands.",
        "",
        "## Summary",
        "",
        f"- Setting source rows: {summary['setting_source_row_count']}",
        f"- Expected setting source rows: {summary['expected_setting_source_row_count']}",
        f"- Expected by kind: `{json.dumps(summary['expected_counts_by_kind'], sort_keys=True)}`",
        f"- Rows by kind: `{json.dumps(summary['row_counts_by_kind'], sort_keys=True)}`",
        f"- Scene binding: `{json.dumps(summary['scene_binding_kind_counts'], sort_keys=True)}`",
        f"- Command support level: `{json.dumps(summary['command_support_level_counts'], sort_keys=True)}`",
        f"- Decoded status: `{json.dumps(summary['decoded_status_counts'], sort_keys=True)}`",
        f"- Validation status: `{json.dumps(summary['validation_status_counts'], sort_keys=True)}`",
        f"- Setup count mismatches: {summary['setup_count_mismatch_count']}",
        f"- Command count mismatches: {summary['command_count_mismatch_count']}",
        f"- Kind count mismatches: {summary['kind_count_mismatch_count']}",
        f"- Unmapped setup rows: {summary['unmapped_setup_count']}",
        f"- Unmapped command rows: {summary['unmapped_command_count']}",
        f"- Semantic pending rows: {summary['semantic_pending_count']}",
        "",
        "## Setting Rows",
        "",
        "| # | Kind | Scene | Setup | Command | Primary Values | Status |",
        "| ---: | --- | --- | ---: | --- | --- | --- |",
    ]
    for row in payload["rows"]:
        kind = row["setting_kind"]
        if kind == "special_files":
            values = f"cUp={row['c_up_elf_message_file']}, keep={row['keep_object_name']} ({row['keep_object_id']})"
        elif kind == "skybox_settings":
            values = f"skybox={row['skybox_id']}, weather={row['weather_or_unk_05']}, indoors={row['indoors']}"
        elif kind == "sound_settings":
            values = (
                f"spec={row['sound_spec_id']}, nature={row['nature_ambience_id']}, "
                f"data3={row['sound_data3']}, bgm={row['bgm_sound_id']:#010x}"
            )
        elif kind == "cutscene_reference":
            values = f"offset={row['cutscene_offset_hex']}, inFile={row['cutscene_in_file']}"
        else:
            values = f"cameraOrWorldMapArea={row['camera_or_world_map_area']}, raw={row['misc_raw_argument']}"
        lines.append(
            f"| {row['setting_source_index']} | `{kind}` | `{row['scene_path']}` | "
            f"{row['setup_index']} | `{row['command_id_hex']}` | `{values}` | "
            f"`{row['validation_status']}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def setting_kind_enum(value: str) -> str:
    return {
        "special_files": "OOT3D_SCENE_SETTING_SPECIAL_FILES",
        "skybox_settings": "OOT3D_SCENE_SETTING_SKYBOX_SETTINGS",
        "sound_settings": "OOT3D_SCENE_SETTING_SOUND_SETTINGS",
        "cutscene_reference": "OOT3D_SCENE_SETTING_CUTSCENE_REFERENCE",
        "misc_settings": "OOT3D_SCENE_SETTING_MISC_SETTINGS",
    }.get(value, "OOT3D_SCENE_SETTING_UNKNOWN")


def binding_enum(value: str) -> str:
    return {
        "scene_row": "OOT3D_SCENE_COMMAND_BINDING_SCENE_ROW",
        "variant_row": "OOT3D_SCENE_COMMAND_BINDING_VARIANT_ROW",
        "unmapped": "OOT3D_SCENE_COMMAND_BINDING_UNMAPPED",
    }.get(value, "OOT3D_SCENE_COMMAND_BINDING_UNMAPPED")


def write_header(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "#ifndef OOT3D_SCENE_SETTING_SOURCE_TABLE_H",
        "#define OOT3D_SCENE_SETTING_SOURCE_TABLE_H",
        "",
        '#include "oot3d/scene.h"',
        '#include "oot3d/scene_command_source_table.h"',
        "",
        "enum {",
        f"    OOT3D_SCENE_SETTING_SOURCE_ROW_COUNT = {len(payload['rows'])},",
        "    OOT3D_SCENE_SETTING_SOURCE_UNMAPPED_INDEX = 0xFFFF,",
        "    OOT3D_SCENE_SETTING_SOURCE_NO_OFFSET = 0xFFFFFFFF,",
        "};",
        "",
        "typedef enum {",
        "    OOT3D_SCENE_SETTING_UNKNOWN,",
        "    OOT3D_SCENE_SETTING_SPECIAL_FILES,",
        "    OOT3D_SCENE_SETTING_SKYBOX_SETTINGS,",
        "    OOT3D_SCENE_SETTING_SOUND_SETTINGS,",
        "    OOT3D_SCENE_SETTING_CUTSCENE_REFERENCE,",
        "    OOT3D_SCENE_SETTING_MISC_SETTINGS,",
        "} Oot3dSceneSettingKind;",
        "",
        "typedef struct {",
        "    u16 settingSourceIndex;",
        "    Oot3dSceneSettingKind settingKind;",
        "    Oot3dSceneCommandBindingKind bindingKind;",
        "    u16 bindingIndex;",
        "    u8 sceneId;",
        "    u16 setupSourceIndex;",
        "    u16 commandSourceIndex;",
        "    u16 setupIndex;",
        "    u16 commandIndex;",
        "    u8 commandId;",
        "    u32 offset;",
        "    u32 argument;",
        "    u8 cUpElfMessageFile;",
        "    s16 keepObjectId;",
        "    const char* keepObjectName;",
        "    u8 skyboxId;",
        "    u8 weatherOrUnk05;",
        "    u8 indoors;",
        "    u8 soundSpecId;",
        "    u8 natureAmbienceId;",
        "    u8 soundData3;",
        "    u32 bgmSoundId;",
        "    u32 cutsceneOffset;",
        "    u8 cutsceneInFile;",
        "    u8 cameraOrWorldMapArea;",
        "    u32 miscRawArgument;",
        "    const char* scenePath;",
        "    const char* sourceBasename;",
        "    const char* setupRole;",
        "    const char* payloadSymbol;",
        "    const char* validationStatus;",
        "    const char* openQuestions;",
        "    const char* handlerName;",
        "    u32 handlerEntry;",
        "} Oot3dSceneSettingSourceRow;",
        "",
        "extern const Oot3dSceneSettingSourceRow oot3d_scene_setting_source_rows[];",
        "extern const u32 oot3d_scene_setting_source_row_count;",
        "",
        "#endif",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def write_source(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "/* Generated by build_scene_setting_source_table.py. */",
        "",
        '#include "oot3d/scene_setting_source_table.h"',
        "",
        "const Oot3dSceneSettingSourceRow oot3d_scene_setting_source_rows[] = {",
    ]
    for row in payload["rows"]:
        lines.append(
            "    { "
            f"{c_u16(row.get('setting_source_index'))}, "
            f"{setting_kind_enum(str(row.get('setting_kind', '')))}, "
            f"{binding_enum(str(row.get('scene_binding_kind', '')))}, "
            f"{c_u16(row.get('scene_binding_index'))}, "
            f"{c_u8(row.get('scene_id'))}, "
            f"{c_u16(row.get('setup_source_index'))}, "
            f"{c_u16(row.get('command_source_index'))}, "
            f"{c_u16(row.get('setup_index'))}, "
            f"{c_u16(row.get('command_index'))}, "
            f"{c_u8(row.get('command_id'))}, "
            f"{c_u32(row.get('offset'))}, "
            f"{c_u32(row.get('argument'))}, "
            f"{c_u8(row.get('c_up_elf_message_file'))}, "
            f"{c_s16(row.get('keep_object_id'))}, "
            f"{c_string(str(row.get('keep_object_name', '')))}, "
            f"{c_u8(row.get('skybox_id'))}, "
            f"{c_u8(row.get('weather_or_unk_05'))}, "
            f"{c_u8(row.get('indoors'))}, "
            f"{c_u8(row.get('sound_spec_id'))}, "
            f"{c_u8(row.get('nature_ambience_id'))}, "
            f"{c_u8(row.get('sound_data3'))}, "
            f"{c_u32(row.get('bgm_sound_id'))}, "
            f"{c_u32(row.get('cutscene_offset'))}, "
            f"{c_bool(row.get('cutscene_in_file'))}, "
            f"{c_u8(row.get('camera_or_world_map_area'))}, "
            f"{c_u32(row.get('misc_raw_argument'))}, "
            f"{c_string(str(row.get('scene_path', '')))}, "
            f"{c_string(str(row.get('source_basename', '')))}, "
            f"{c_string(str(row.get('setup_role', '')))}, "
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
            "const u32 oot3d_scene_setting_source_row_count = OOT3D_SCENE_SETTING_SOURCE_ROW_COUNT;",
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
