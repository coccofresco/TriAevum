#!/usr/bin/env python3
"""Build a source-oriented OOT3D scene light-settings table.

Light-setting rows are decoded from native OOT3D command 0x0F payloads. This
table binds each 0x1C-byte record back to generated scene/setup/command rows
and exposes the fields consumed by the OOT3D code.bin runtime consumer at
0x0045DD50.
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
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_light_source_table.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "scene_light_source_table.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_light_source_table.md"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "scene_light_source_table.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_scene_light_source_table.c"

UNMAPPED_INDEX = 0xFFFF
UNMAPPED_SCENE_ID = 0xFF
NO_OFFSET = 0xFFFFFFFF
COMMAND_LIGHT_SETTINGS_LIST = 0x0F
LIGHT_RECORD_SIZE = 0x1C
VALIDATION_STATUS = "native_env_consumer_0045dd50_code_bin_confirmed"


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


def c_u32(value: Any) -> str:
    return f"{int_value(value) & 0xFFFFFFFF}u"


def triplet(values: Any) -> tuple[int, int, int]:
    items = as_list(values)
    return (
        int_value(items[0]) if len(items) > 0 else 0,
        int_value(items[1]) if len(items) > 1 else 0,
        int_value(items[2]) if len(items) > 2 else 0,
    )


def raw_bytes(raw_hex: str) -> list[int]:
    raw = bytes.fromhex(raw_hex)
    if len(raw) != LIGHT_RECORD_SIZE:
        raise ValueError(f"expected 0x{LIGHT_RECORD_SIZE:02x} light record bytes, got {len(raw)}")
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
                "light_settings_count": int_value(row.get("light_settings_count"), 0),
                "light_settings_symbol": row.get("light_settings_symbol", ""),
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
        if scene_path and setup_index >= 0 and command_id == COMMAND_LIGHT_SETTINGS_LIST:
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


def light_record_fields(record: dict[str, Any]) -> dict[str, Any]:
    native = as_dict(record.get("native_env_consumer_0045dd50"))
    ambient_r, ambient_g, ambient_b = triplet(native.get("ambient_rgb"))
    light0_dir_x, light0_dir_y, light0_dir_z = triplet(native.get("light0_dir_s8"))
    light0_r, light0_g, light0_b = triplet(native.get("light0_rgb"))
    light1_dir_x, light1_dir_y, light1_dir_z = triplet(native.get("light1_dir_s8"))
    light1_r, light1_g, light1_b = triplet(native.get("light1_rgb"))
    fog_r, fog_g, fog_b = triplet(native.get("fog_or_environment_rgb"))
    legacy = as_dict(record.get("legacy_env_light_settings_prefix_candidate"))
    return {
        "ambient_r": ambient_r,
        "ambient_g": ambient_g,
        "ambient_b": ambient_b,
        "light0_dir_x": light0_dir_x,
        "light0_dir_y": light0_dir_y,
        "light0_dir_z": light0_dir_z,
        "light0_r": light0_r,
        "light0_g": light0_g,
        "light0_b": light0_b,
        "light1_dir_x": light1_dir_x,
        "light1_dir_y": light1_dir_y,
        "light1_dir_z": light1_dir_z,
        "light1_r": light1_r,
        "light1_g": light1_g,
        "light1_b": light1_b,
        "fog_or_environment_r": fog_r,
        "fog_or_environment_g": fog_g,
        "fog_or_environment_b": fog_b,
        "legacy_candidate_status": legacy.get("status", ""),
        "actor_vs_packet_candidate_available": bool(as_dict(record.get("actor_vs_packet_candidate"))),
    }


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
            command_binding = command_bindings.get((scene_key, setup_index, COMMAND_LIGHT_SETTINGS_LIST), {})
            for command_index, command in enumerate(as_list(setup.get("commands"))):
                if not isinstance(command, dict):
                    continue
                if int_value(command.get("command_id"), -1) != COMMAND_LIGHT_SETTINGS_LIST:
                    continue
                decoded = as_dict(command.get("decoded"))
                consumer = as_dict(decoded.get("consumer_evidence"))
                for light_index, record in enumerate(as_list(decoded.get("records"))):
                    if not isinstance(record, dict):
                        continue
                    raw_hex = str(record.get("raw_hex", ""))
                    row = {
                        "light_source_index": len(rows),
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
                        "light_settings_symbol": setup_binding.get("light_settings_symbol", ""),
                        "command_index": int_value(command_binding.get("command_index"), command_index),
                        "command_name": command_binding.get("command_name", ""),
                        "command_support_level": command_binding.get("support_level", ""),
                        "handler_binding_status": command_binding.get("handler_binding_status", ""),
                        "handler_name": command_binding.get("handler_name", ""),
                        "handler_entry": command_binding.get("handler_entry", ""),
                        "payload_symbol": command_binding.get("payload_symbol", ""),
                        "export_structs": command_binding.get("export_structs", ""),
                        "light_index": light_index,
                        "record_index": int_value(record.get("index"), light_index),
                        "offset": int_value(record.get("offset"), NO_OFFSET),
                        "offset_hex": record.get("offset_hex", ""),
                        "raw_hex": raw_hex,
                        "record_size": int_value(decoded.get("record_size"), LIGHT_RECORD_SIZE),
                        "layout": decoded.get("layout", ""),
                        "decoded_status": decoded.get("status", ""),
                        "validation_status": VALIDATION_STATUS,
                        "consumer_command_handler": consumer.get("command_handler", ""),
                        "consumer_runtime_consumer": consumer.get("runtime_consumer", ""),
                        "consumer_source": consumer.get("source", ""),
                    }
                    row.update(light_record_fields(record))
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

    expected_light_count = int_value(source_payload.get("summary", {}).get("payload_totals", {}).get("light_settings"))
    validation_counts = Counter(str(row.get("validation_status", "")) for row in rows)
    scene_binding_counts = Counter(str(row.get("scene_binding_kind", "")) for row in rows)
    support_counts = Counter(str(row.get("command_support_level", "")) for row in rows)
    layout_counts = Counter(str(row.get("layout", "")) for row in rows)
    record_size_counts = Counter(str(row.get("record_size", "")) for row in rows)
    top_scene_counts = Counter(str(row.get("scene_path", "")) for row in rows)
    unmapped_setup_rows = [
        row for row in rows if int_value(row.get("setup_source_index"), UNMAPPED_INDEX) == UNMAPPED_INDEX
    ]
    unmapped_command_rows = [
        row for row in rows if int_value(row.get("command_source_index"), UNMAPPED_INDEX) == UNMAPPED_INDEX
    ]
    unmapped_scene_rows = [
        row for row in rows if str(row.get("scene_binding_kind", "")) == "unmapped"
    ]

    per_setup_counts = Counter(int_value(row.get("setup_source_index"), UNMAPPED_INDEX) for row in rows)
    setup_light_count_mismatches = []
    for setup in as_list(setup_payload.get("rows")):
        if not isinstance(setup, dict):
            continue
        setup_source_index = int_value(setup.get("setup_source_index"), UNMAPPED_INDEX)
        expected = int_value(setup.get("light_settings_count"), 0)
        actual = per_setup_counts.get(setup_source_index, 0)
        if expected != actual:
            setup_light_count_mismatches.append(
                {
                    "setup_source_index": setup_source_index,
                    "scene_path": setup.get("scene_path", ""),
                    "setup_index": int_value(setup.get("setup_index"), -1),
                    "expected_light_settings_count": expected,
                    "actual_light_settings_count": actual,
                }
            )

    per_command_counts = Counter(int_value(row.get("command_source_index"), UNMAPPED_INDEX) for row in rows)
    command_light_count_mismatches = []
    for command in as_list(command_payload.get("command_rows")):
        if not isinstance(command, dict):
            continue
        if int_value(command.get("command_id"), -1) != COMMAND_LIGHT_SETTINGS_LIST:
            continue
        command_source_index = int_value(command.get("command_source_index"), UNMAPPED_INDEX)
        expected = int_value(command.get("decoded_count"), 0)
        actual = per_command_counts.get(command_source_index, 0)
        if expected != actual:
            command_light_count_mismatches.append(
                {
                    "command_source_index": command_source_index,
                    "scene_path": command.get("scene_path", ""),
                    "setup_index": int_value(command.get("setup_index"), -1),
                    "expected_light_settings_count": expected,
                    "actual_light_settings_count": actual,
                }
            )

    raw_length_mismatch_rows = [
        row
        for row in rows
        if len(str(row.get("raw_hex", ""))) != LIGHT_RECORD_SIZE * 2
        or int_value(row.get("record_size"), 0) != LIGHT_RECORD_SIZE
    ]
    missing_native_consumer_rows = [
        row
        for row in rows
        if not str(row.get("consumer_runtime_consumer", ""))
        or str(row.get("layout", "")) != "oot3d_pica_light_settings_record_0x1c"
    ]

    summary = {
        "format": "oot3d_scene_light_source_table_v1",
        "light_source_row_count": len(rows),
        "expected_light_settings_count": expected_light_count,
        "setup_light_command_count": len(command_bindings),
        "scene_binding_kind_counts": dict(sorted(scene_binding_counts.items())),
        "command_support_level_counts": dict(sorted(support_counts.items())),
        "validation_status_counts": dict(sorted(validation_counts.items())),
        "layout_counts": dict(sorted(layout_counts.items())),
        "record_size_counts": dict(sorted(record_size_counts.items())),
        "top_scene_light_counts": dict(top_scene_counts.most_common(20)),
        "unmapped_scene_light_count": len(unmapped_scene_rows),
        "unmapped_setup_light_count": len(unmapped_setup_rows),
        "unmapped_command_light_count": len(unmapped_command_rows),
        "setup_light_count_mismatch_count": len(setup_light_count_mismatches),
        "command_light_count_mismatch_count": len(command_light_count_mismatches),
        "raw_length_mismatch_count": len(raw_length_mismatch_rows),
        "missing_native_consumer_count": len(missing_native_consumer_rows),
    }
    return {
        "summary": summary,
        "source_policy": {
            "native_full_scene_index": str(DEFAULT_NATIVE_FULL_INDEX),
            "native_scene_source_index": str(DEFAULT_NATIVE_SOURCE_INDEX),
            "setup_source_table": str(DEFAULT_SETUP_SOURCE_TABLE),
            "command_source_table": str(DEFAULT_COMMAND_SOURCE_TABLE),
            "promoted_layout": "OOT3D code.bin 0x0045DD50 0x1C-stride native environment consumer fields",
            "diagnostic_policy": "legacy_env_light_settings_prefix_candidate and actor_vs_packet_candidate remain diagnostic-only and are not promoted as primary light semantics.",
            "n64_policy": "No N64 lighting tables are read by this integration table. Rows come from OOT3D ZSI command 0x0F payloads plus OOT3D code.bin consumer evidence.",
        },
        "rows": rows,
        "unmapped_scene_rows": unmapped_scene_rows,
        "unmapped_setup_rows": unmapped_setup_rows,
        "unmapped_command_rows": unmapped_command_rows,
        "setup_light_count_mismatches": setup_light_count_mismatches,
        "command_light_count_mismatches": command_light_count_mismatches,
        "raw_length_mismatch_rows": raw_length_mismatch_rows,
        "missing_native_consumer_rows": missing_native_consumer_rows,
    }


CSV_COLUMNS = [
    "light_source_index",
    "scene_id_hex",
    "scene_id",
    "scene_path",
    "setup_source_index",
    "setup_index",
    "setup_role",
    "command_source_index",
    "command_index",
    "light_index",
    "offset_hex",
    "ambient_r",
    "ambient_g",
    "ambient_b",
    "light0_dir_x",
    "light0_dir_y",
    "light0_dir_z",
    "light0_r",
    "light0_g",
    "light0_b",
    "light1_dir_x",
    "light1_dir_y",
    "light1_dir_z",
    "light1_r",
    "light1_g",
    "light1_b",
    "fog_or_environment_r",
    "fog_or_environment_g",
    "fog_or_environment_b",
    "layout",
    "record_size",
    "validation_status",
    "command_support_level",
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
        "# Scene Light Source Table",
        "",
        "Generated from OOT3D-native command 0x0F light-setting payloads.",
        "",
        "## Summary",
        "",
        f"- Light source rows: {summary['light_source_row_count']}",
        f"- Expected light settings: {summary['expected_light_settings_count']}",
        f"- Setup light commands: {summary['setup_light_command_count']}",
        f"- Scene binding: `{json.dumps(summary['scene_binding_kind_counts'], sort_keys=True)}`",
        f"- Command support level: `{json.dumps(summary['command_support_level_counts'], sort_keys=True)}`",
        f"- Validation status: `{json.dumps(summary['validation_status_counts'], sort_keys=True)}`",
        f"- Layouts: `{json.dumps(summary['layout_counts'], sort_keys=True)}`",
        f"- Record sizes: `{json.dumps(summary['record_size_counts'], sort_keys=True)}`",
        f"- Unmapped scene lights: {summary['unmapped_scene_light_count']}",
        f"- Unmapped setup lights: {summary['unmapped_setup_light_count']}",
        f"- Unmapped command lights: {summary['unmapped_command_light_count']}",
        f"- Setup light-count mismatches: {summary['setup_light_count_mismatch_count']}",
        f"- Command light-count mismatches: {summary['command_light_count_mismatch_count']}",
        f"- Raw length mismatches: {summary['raw_length_mismatch_count']}",
        f"- Missing native consumer evidence: {summary['missing_native_consumer_count']}",
        "",
        "## Top Scene Light Counts",
        "",
        "| Scene | Count |",
        "| --- | ---: |",
    ]
    for scene_path, count in summary["top_scene_light_counts"].items():
        lines.append(f"| `{scene_path}` | {count} |")
    lines.extend(
        [
            "",
            "## Light Rows",
            "",
            "| # | Scene | Setup | Light | Ambient | Light0 Dir/RGB | Light1 Dir/RGB | Fog/Env | Offset | Status |",
            "| ---: | --- | ---: | ---: | --- | --- | --- | --- | --- | --- |",
        ]
    )
    for row in payload["rows"]:
        ambient = f"{row['ambient_r']},{row['ambient_g']},{row['ambient_b']}"
        light0 = (
            f"{row['light0_dir_x']},{row['light0_dir_y']},{row['light0_dir_z']} / "
            f"{row['light0_r']},{row['light0_g']},{row['light0_b']}"
        )
        light1 = (
            f"{row['light1_dir_x']},{row['light1_dir_y']},{row['light1_dir_z']} / "
            f"{row['light1_r']},{row['light1_g']},{row['light1_b']}"
        )
        fog = f"{row['fog_or_environment_r']},{row['fog_or_environment_g']},{row['fog_or_environment_b']}"
        lines.append(
            f"| {row['light_source_index']} | `{row['scene_path']}` | {row['setup_index']} | "
            f"{row['light_index']} | `{ambient}` | `{light0}` | `{light1}` | `{fog}` | "
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
        "#ifndef OOT3D_SCENE_LIGHT_SOURCE_TABLE_H",
        "#define OOT3D_SCENE_LIGHT_SOURCE_TABLE_H",
        "",
        '#include "oot3d/scene.h"',
        '#include "oot3d/scene_command_source_table.h"',
        "",
        "enum {",
        f"    OOT3D_SCENE_LIGHT_SOURCE_ROW_COUNT = {len(payload['rows'])},",
        "    OOT3D_SCENE_LIGHT_SOURCE_UNMAPPED_INDEX = 0xFFFF,",
        "    OOT3D_SCENE_LIGHT_SOURCE_NO_OFFSET = 0xFFFFFFFF,",
        "};",
        "",
        "typedef struct {",
        "    u16 lightSourceIndex;",
        "    Oot3dSceneCommandBindingKind bindingKind;",
        "    u16 bindingIndex;",
        "    u8 sceneId;",
        "    u16 setupSourceIndex;",
        "    u16 commandSourceIndex;",
        "    u16 setupIndex;",
        "    u16 commandIndex;",
        "    u16 lightIndex;",
        "    u32 offset;",
        "    u8 raw[OOT3D_LIGHT_SETTINGS_RECORD_SIZE];",
        "    u8 ambientR;",
        "    u8 ambientG;",
        "    u8 ambientB;",
        "    s8 light0DirX;",
        "    s8 light0DirY;",
        "    s8 light0DirZ;",
        "    u8 light0R;",
        "    u8 light0G;",
        "    u8 light0B;",
        "    s8 light1DirX;",
        "    s8 light1DirY;",
        "    s8 light1DirZ;",
        "    u8 light1R;",
        "    u8 light1G;",
        "    u8 light1B;",
        "    u8 fogOrEnvironmentR;",
        "    u8 fogOrEnvironmentG;",
        "    u8 fogOrEnvironmentB;",
        "    const char* scenePath;",
        "    const char* sourceBasename;",
        "    const char* setupRole;",
        "    const char* lightSettingsSymbol;",
        "    const char* payloadSymbol;",
        "    const char* layout;",
        "    const char* validationStatus;",
        "    const char* handlerName;",
        "    u32 handlerEntry;",
        "} Oot3dSceneLightSourceRow;",
        "",
        "extern const Oot3dSceneLightSourceRow oot3d_scene_light_source_rows[];",
        "extern const u32 oot3d_scene_light_source_row_count;",
        "",
        "#endif",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def handler_entry_u32(value: Any) -> int:
    text = str(value or "")
    if not text:
        return 0
    return int(text, 16)


def write_source(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "/* Generated by build_scene_light_source_table.py. */",
        "",
        '#include "oot3d/scene_light_source_table.h"',
        "",
        "const Oot3dSceneLightSourceRow oot3d_scene_light_source_rows[] = {",
    ]
    for row in payload["rows"]:
        lines.append(
            "    { "
            f"{c_u16(row.get('light_source_index'))}, "
            f"{binding_enum(str(row.get('scene_binding_kind', '')))}, "
            f"{c_u16(row.get('scene_binding_index'))}, "
            f"{c_u8(row.get('scene_id'))}, "
            f"{c_u16(row.get('setup_source_index'))}, "
            f"{c_u16(row.get('command_source_index'))}, "
            f"{c_u16(row.get('setup_index'))}, "
            f"{c_u16(row.get('command_index'))}, "
            f"{c_u16(row.get('light_index'))}, "
            f"{c_u32(row.get('offset'))}, "
            f"{raw_bytes_initializer(str(row.get('raw_hex', '')))}, "
            f"{c_u8(row.get('ambient_r'))}, "
            f"{c_u8(row.get('ambient_g'))}, "
            f"{c_u8(row.get('ambient_b'))}, "
            f"{c_s8(row.get('light0_dir_x'))}, "
            f"{c_s8(row.get('light0_dir_y'))}, "
            f"{c_s8(row.get('light0_dir_z'))}, "
            f"{c_u8(row.get('light0_r'))}, "
            f"{c_u8(row.get('light0_g'))}, "
            f"{c_u8(row.get('light0_b'))}, "
            f"{c_s8(row.get('light1_dir_x'))}, "
            f"{c_s8(row.get('light1_dir_y'))}, "
            f"{c_s8(row.get('light1_dir_z'))}, "
            f"{c_u8(row.get('light1_r'))}, "
            f"{c_u8(row.get('light1_g'))}, "
            f"{c_u8(row.get('light1_b'))}, "
            f"{c_u8(row.get('fog_or_environment_r'))}, "
            f"{c_u8(row.get('fog_or_environment_g'))}, "
            f"{c_u8(row.get('fog_or_environment_b'))}, "
            f"{c_string(str(row.get('scene_path', '')))}, "
            f"{c_string(str(row.get('source_basename', '')))}, "
            f"{c_string(str(row.get('setup_role', '')))}, "
            f"{c_string(str(row.get('light_settings_symbol', '')))}, "
            f"{c_string(str(row.get('payload_symbol', '')))}, "
            f"{c_string(str(row.get('layout', '')))}, "
            f"{c_string(str(row.get('validation_status', '')))}, "
            f"{c_string(str(row.get('handler_name', '')))}, "
            f"{c_u32(handler_entry_u32(row.get('handler_entry')))}"
            " },"
        )
    lines.extend(
        [
            "};",
            "",
            "const u32 oot3d_scene_light_source_row_count = OOT3D_SCENE_LIGHT_SOURCE_ROW_COUNT;",
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
