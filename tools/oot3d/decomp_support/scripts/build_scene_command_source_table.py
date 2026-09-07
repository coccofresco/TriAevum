#!/usr/bin/env python3
"""Build a source-oriented OOT3D scene-command table.

This joins native ZSI command extraction with the OOT3D code.bin command handler
map and the generated scene source table. It is intended as a decompilation
index: every exported scene command can be traced to its generated source
payload and, when applicable, its native code.bin handler.
"""

from __future__ import annotations

import csv
import json
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SCENE_SOURCE_INDEX = ROOT / "analysis" / "oot3d_native_scene_source_index.json"
DEFAULT_HANDLER_MAP = ROOT / "analysis" / "scene_command_handler_runtime_map.json"
DEFAULT_SCENE_SOURCE_TABLE = ROOT / "analysis" / "scene_source_table.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_command_source_table.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "scene_command_source_table.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_command_source_table.md"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "scene_command_source_table.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_scene_command_source_table.c"

UNMAPPED_INDEX = 0xFFFF


def as_dict(value: Any) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def as_list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def int_value(value: Any, default: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def hex_value(value: Any, default: int = 0) -> int:
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        stripped = value.strip()
        try:
            if stripped.lower().startswith("0x") or (
                len(stripped) == 8 and all(char in "0123456789abcdefABCDEF" for char in stripped)
            ):
                return int(stripped, 16)
            return int(stripped, 10)
        except ValueError:
            return default
    return default


def c_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def handler_by_command(payload: dict[str, Any]) -> dict[int, dict[str, Any]]:
    handlers: dict[int, dict[str, Any]] = {}
    for row in as_list(payload.get("handlers")):
        if not isinstance(row, dict):
            continue
        handlers[int_value(row.get("command_id"), -1)] = row
    return handlers


def scene_source_bindings(payload: dict[str, Any]) -> dict[str, dict[str, Any]]:
    bindings: dict[str, dict[str, Any]] = {}
    for index, row in enumerate(as_list(payload.get("rows"))):
        if not isinstance(row, dict):
            continue
        path = str(row.get("zsi_path", "")).lower()
        if path:
            bindings[path] = {
                "scene_binding_kind": "scene_row",
                "scene_binding_index": index,
                "scene_id": row.get("scene_id"),
                "scene_id_hex": row.get("scene_id_hex"),
                "scene_index_symbol": row.get("scene_index_symbol", ""),
                "source_basename": row.get("source_basename", ""),
                "source_c_file": row.get("source_c_file", ""),
            }
    for index, row in enumerate(as_list(payload.get("variant_rows"))):
        if not isinstance(row, dict):
            continue
        path = str(row.get("zsi_path", "")).lower()
        if path:
            bindings[path] = {
                "scene_binding_kind": "variant_row",
                "scene_binding_index": index,
                "scene_id": row.get("scene_id"),
                "scene_id_hex": row.get("scene_id_hex"),
                "scene_index_symbol": row.get("scene_index_symbol", ""),
                "source_basename": row.get("source_basename", ""),
                "source_c_file": row.get("source_c_file", ""),
            }
    return bindings


def command_handler_binding(command: dict[str, Any], handlers: dict[int, dict[str, Any]]) -> dict[str, Any]:
    handler = handlers.get(int_value(command.get("command_id"), -1), {})
    return {
        "handler_binding_status": "code_bin_handler_resolved" if handler else "code_bin_handler_missing",
        "handler_entry": handler.get("handler_entry", ""),
        "handler_name": handler.get("handler_name", ""),
        "handler_support_level": handler.get("support_level", ""),
        "handler_decompiled_file": handler.get("decompiled_file", ""),
        "handler_runtime_store_count": int_value(handler.get("runtime_store_count"), 0),
        "handler_command_read_offsets": [
            item.get("offset_hex", "") for item in as_list(handler.get("command_reads")) if isinstance(item, dict)
        ],
        "handler_runtime_store_targets": [
            item.get("target", "") for item in as_list(handler.get("runtime_stores")) if isinstance(item, dict)
        ],
        "handler_calls": as_list(handler.get("calls")),
        "handler_scene_relative_pointer_expressions": as_list(handler.get("scene_relative_pointer_expressions")),
    }


def build_command_row(
    command: dict[str, Any],
    command_index: int,
    scene_bindings: dict[str, dict[str, Any]],
    handlers: dict[int, dict[str, Any]],
) -> dict[str, Any]:
    scene_path = str(command.get("scene_path", ""))
    scene_binding = scene_bindings.get(scene_path.lower(), {})
    scene_binding_kind = str(scene_binding.get("scene_binding_kind", "unmapped"))
    scene_binding_index = int_value(scene_binding.get("scene_binding_index"), UNMAPPED_INDEX)
    handler_binding = command_handler_binding(command, handlers)
    return {
        "command_source_index": command_index,
        "scene_path": scene_path,
        "scene_id": int_value(scene_binding.get("scene_id"), UNMAPPED_INDEX),
        "scene_id_hex": scene_binding.get("scene_id_hex", ""),
        "scene_binding_kind": scene_binding_kind,
        "scene_binding_index": scene_binding_index,
        "source_basename": command.get("source_basename", scene_binding.get("source_basename", "")),
        "source_c_file": scene_binding.get("source_c_file", ""),
        "scene_index_symbol": scene_binding.get("scene_index_symbol", ""),
        "setup_index": int_value(command.get("setup_index"), 0),
        "command_index": int_value(command.get("command_index"), 0),
        "offset": hex_value(command.get("offset_hex")),
        "offset_hex": command.get("offset_hex", ""),
        "command_id": int_value(command.get("command_id"), 0),
        "command_id_hex": command.get("command_id_hex", ""),
        "command_name": command.get("command_name", ""),
        "parameter": int_value(command.get("parameter"), 0),
        "argument": hex_value(command.get("argument_hex")),
        "argument_hex": command.get("argument_hex", ""),
        "decoded_status": command.get("decoded_status", ""),
        "decoded_count": int_value(command.get("decoded_count"), 0),
        "decoded_expected_count": int_value(command.get("decoded_expected_count"), 0),
        "support_level": command.get("support_level", ""),
        "payload_symbol": command.get("payload_symbol", ""),
        "export_structs": command.get("export_structs", ""),
        "open_questions": command.get("open_questions", ""),
        **handler_binding,
    }


def build_handler_row(handler: dict[str, Any]) -> dict[str, Any]:
    return {
        "command_id": int_value(handler.get("command_id"), 0),
        "command_id_hex": handler.get("command_id_hex", ""),
        "command_name": handler.get("command_name", ""),
        "handler_entry": handler.get("handler_entry", ""),
        "handler_name": handler.get("handler_name", ""),
        "support_level": handler.get("support_level", ""),
        "runtime_store_count": int_value(handler.get("runtime_store_count"), 0),
        "command_read_offsets": [
            item.get("offset_hex", "") for item in as_list(handler.get("command_reads")) if isinstance(item, dict)
        ],
        "runtime_store_targets": [
            item.get("target", "") for item in as_list(handler.get("runtime_stores")) if isinstance(item, dict)
        ],
        "calls": as_list(handler.get("calls")),
        "scene_relative_pointer_expressions": as_list(handler.get("scene_relative_pointer_expressions")),
        "decompiled_file": handler.get("decompiled_file", ""),
    }


def build_report() -> dict[str, Any]:
    source_payload = json.loads(DEFAULT_SCENE_SOURCE_INDEX.read_text(encoding="utf-8"))
    handler_payload = json.loads(DEFAULT_HANDLER_MAP.read_text(encoding="utf-8"))
    scene_source_payload = json.loads(DEFAULT_SCENE_SOURCE_TABLE.read_text(encoding="utf-8"))
    handlers = handler_by_command(handler_payload)
    scene_bindings = scene_source_bindings(scene_source_payload)

    commands = [
        build_command_row(command, index, scene_bindings, handlers)
        for index, command in enumerate(as_list(source_payload.get("commands")))
        if isinstance(command, dict)
    ]
    handler_rows = [
        build_handler_row(handler)
        for handler in sorted(
            [row for row in as_list(handler_payload.get("handlers")) if isinstance(row, dict)],
            key=lambda item: int_value(item.get("command_id"), 0),
        )
    ]

    command_id_counts = Counter(str(row.get("command_id_hex", "")) for row in commands)
    support_counts = Counter(str(row.get("support_level", "")) for row in commands)
    handler_status_counts = Counter(str(row.get("handler_binding_status", "")) for row in commands)
    scene_binding_counts = Counter(str(row.get("scene_binding_kind", "")) for row in commands)
    payload_status_counts = Counter("payload_symbol_present" if row.get("payload_symbol") else "payload_symbol_empty" for row in commands)
    unresolved_rows = [row for row in commands if int_value(row.get("decoded_count"), 0) != int_value(row.get("decoded_expected_count"), 0)]
    missing_handler_rows = [row for row in commands if row.get("handler_binding_status") == "code_bin_handler_missing"]
    unmapped_scene_rows = [row for row in commands if row.get("scene_binding_kind") == "unmapped"]
    variant_scene_paths = sorted({str(row.get("scene_path", "")) for row in commands if row.get("scene_binding_kind") == "variant_row"})

    summary = {
        "format": "oot3d_scene_command_source_table_v1",
        "command_row_count": len(commands),
        "handler_row_count": len(handler_rows),
        "unique_command_id_count": len(command_id_counts),
        "command_id_counts": dict(sorted(command_id_counts.items())),
        "support_level_counts": dict(sorted(support_counts.items())),
        "handler_binding_status_counts": dict(sorted(handler_status_counts.items())),
        "scene_binding_kind_counts": dict(sorted(scene_binding_counts.items())),
        "payload_symbol_status_counts": dict(sorted(payload_status_counts.items())),
        "decoded_count_mismatch_count": len(unresolved_rows),
        "decoded_count_mismatch_command_id_counts": dict(
            sorted(Counter(str(row.get("command_id_hex", "")) for row in unresolved_rows).items())
        ),
        "missing_handler_command_id_counts": dict(
            sorted(Counter(str(row.get("command_id_hex", "")) for row in missing_handler_rows).items())
        ),
        "missing_handler_support_level_counts": dict(
            sorted(Counter(str(row.get("support_level", "")) for row in missing_handler_rows).items())
        ),
        "unmapped_scene_binding_count": len(unmapped_scene_rows),
        "variant_scene_paths": variant_scene_paths,
        "source_scene_count": len(scene_bindings),
    }
    return {
        "summary": summary,
        "source_policy": {
            "native_scene_source_index": str(DEFAULT_SCENE_SOURCE_INDEX),
            "scene_command_handler_runtime_map": str(DEFAULT_HANDLER_MAP),
            "scene_source_table": str(DEFAULT_SCENE_SOURCE_TABLE),
            "n64_policy": "No N64 source is read by this command source table. Handler evidence comes from OOT3D code.bin exports; command payload evidence comes from OOT3D ZSI extraction.",
        },
        "handler_rows": handler_rows,
        "command_rows": commands,
    }


CSV_COLUMNS = [
    "command_source_index",
    "scene_path",
    "scene_id_hex",
    "scene_binding_kind",
    "scene_binding_index",
    "setup_index",
    "command_index",
    "offset_hex",
    "command_id_hex",
    "command_name",
    "parameter",
    "argument_hex",
    "decoded_status",
    "decoded_count",
    "decoded_expected_count",
    "support_level",
    "payload_symbol",
    "handler_binding_status",
    "handler_entry",
    "handler_name",
    "export_structs",
]


def csv_value(value: Any) -> str:
    if isinstance(value, list):
        return "; ".join(str(item) for item in value)
    if isinstance(value, dict):
        return json.dumps(value, sort_keys=True)
    return str(value)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=CSV_COLUMNS)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: csv_value(row.get(key, "")) for key in CSV_COLUMNS})


def write_markdown(path: Path, payload: dict[str, Any]) -> None:
    summary = payload["summary"]
    lines = [
        "# Scene Command Source Table",
        "",
        "Generated by joining OOT3D-native ZSI scene commands, code.bin scene-command handlers, and generated scene source rows.",
        "",
        "## Summary",
        "",
        f"- Command rows: {summary['command_row_count']}",
        f"- Handler rows: {summary['handler_row_count']}",
        f"- Unique command ids: {summary['unique_command_id_count']}",
        f"- Command id counts: `{json.dumps(summary['command_id_counts'], sort_keys=True)}`",
        f"- Support levels: `{json.dumps(summary['support_level_counts'], sort_keys=True)}`",
        f"- Handler binding: `{json.dumps(summary['handler_binding_status_counts'], sort_keys=True)}`",
        f"- Scene binding: `{json.dumps(summary['scene_binding_kind_counts'], sort_keys=True)}`",
        f"- Payload symbols: `{json.dumps(summary['payload_symbol_status_counts'], sort_keys=True)}`",
        f"- Decoded-count mismatches: {summary['decoded_count_mismatch_count']}",
        f"- Decoded-count mismatch command ids: `{json.dumps(summary['decoded_count_mismatch_command_id_counts'], sort_keys=True)}`",
        f"- Missing handler command ids: `{json.dumps(summary['missing_handler_command_id_counts'], sort_keys=True)}`",
        f"- Missing handler support levels: `{json.dumps(summary['missing_handler_support_level_counts'], sort_keys=True)}`",
        f"- Unmapped scene bindings: {summary['unmapped_scene_binding_count']}",
        f"- Variant scene paths: `{'; '.join(summary['variant_scene_paths'])}`",
        "",
        "## Handler Rows",
        "",
        "| Command | Handler | Entry | Reads | Stores | Calls |",
        "| --- | --- | --- | --- | ---: | --- |",
    ]
    for row in payload["handler_rows"]:
        lines.append(
            f"| `{row['command_id_hex']}` `{row['command_name']}` | `{row['handler_name']}` | "
            f"`{row['handler_entry']}` | `{'; '.join(row['command_read_offsets'])}` | "
            f"{row['runtime_store_count']} | `{'; '.join(str(item) for item in row['calls'])}` |"
        )

    lines.extend(
        [
            "",
            "## Command Rows",
            "",
            "| # | Scene | Setup | Cmd | Name | Payload | Handler | Support |",
            "| ---: | --- | ---: | ---: | --- | --- | --- | --- |",
        ]
    )
    for row in payload["command_rows"]:
        lines.append(
            f"| {row['command_source_index']} | `{row['scene_path']}` | {row['setup_index']} | "
            f"`{row['command_id_hex']}` | `{row['command_name']}` | `{row['payload_symbol']}` | "
            f"`{row['handler_name']}` | `{row['support_level']}` |"
        )

    lines.extend(
        [
            "",
            "## Notes",
            "",
            "- `native_control_marker` rows such as scene end markers intentionally have no code.bin handler binding.",
            "- Current missing-handler rows are all command `0x14` end markers with `native_control_marker` support.",
            "- Current decoded-count mismatches are command `0x00` spawn-list rows where the native handler selects the active entrance spawn from the decoded list.",
            "- `handler_binding_status` is derived only from `scene_command_handler_runtime_map.json`.",
            "- Scene binding uses `scene_source_table.json`, including alternate selected-record variants when the command scene path is a `_dd` scene.",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")


def binding_kind_c_name(value: str) -> str:
    mapping = {
        "scene_row": "OOT3D_SCENE_COMMAND_BINDING_SCENE_ROW",
        "variant_row": "OOT3D_SCENE_COMMAND_BINDING_VARIANT_ROW",
        "unmapped": "OOT3D_SCENE_COMMAND_BINDING_UNMAPPED",
    }
    return mapping.get(value, "OOT3D_SCENE_COMMAND_BINDING_UNMAPPED")


def write_header(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "#ifndef OOT3D_SCENE_COMMAND_SOURCE_TABLE_H",
        "#define OOT3D_SCENE_COMMAND_SOURCE_TABLE_H",
        "",
        '#include "oot3d/types.h"',
        "",
        "enum {",
        f"    OOT3D_SCENE_COMMAND_SOURCE_ROW_COUNT = {len(payload['command_rows'])},",
        f"    OOT3D_SCENE_COMMAND_HANDLER_ROW_COUNT = {len(payload['handler_rows'])},",
        f"    OOT3D_SCENE_COMMAND_SOURCE_UNMAPPED_INDEX = 0x{UNMAPPED_INDEX:04X},",
        "};",
        "",
        "typedef enum {",
        "    OOT3D_SCENE_COMMAND_BINDING_SCENE_ROW,",
        "    OOT3D_SCENE_COMMAND_BINDING_VARIANT_ROW,",
        "    OOT3D_SCENE_COMMAND_BINDING_UNMAPPED,",
        "} Oot3dSceneCommandBindingKind;",
        "",
        "typedef struct {",
        "    u8 commandId;",
        "    const char* commandName;",
        "    u32 handlerEntry;",
        "    const char* handlerName;",
        "    const char* supportLevel;",
        "    u16 runtimeStoreCount;",
        "} Oot3dSceneCommandHandlerRow;",
        "",
        "typedef struct {",
        "    u16 commandSourceIndex;",
        "    Oot3dSceneCommandBindingKind bindingKind;",
        "    u16 bindingIndex;",
        "    u8 sceneId;",
        "    u16 setupIndex;",
        "    u16 commandIndex;",
        "    u32 offset;",
        "    u8 commandId;",
        "    u8 parameter;",
        "    u32 argument;",
        "    const char* scenePath;",
        "    const char* sourceBasename;",
        "    const char* sceneIndexSymbol;",
        "    const char* commandName;",
        "    const char* decodedStatus;",
        "    const char* supportLevel;",
        "    const char* payloadSymbol;",
        "    const char* exportStructs;",
        "    const char* handlerName;",
        "    u32 handlerEntry;",
        "} Oot3dSceneCommandSourceRow;",
        "",
        "extern const Oot3dSceneCommandHandlerRow oot3d_scene_command_handler_rows[];",
        "extern const Oot3dSceneCommandSourceRow oot3d_scene_command_source_rows[];",
        "extern const u32 oot3d_scene_command_handler_row_count;",
        "extern const u32 oot3d_scene_command_source_row_count;",
        "",
        "#endif",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def write_source(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "/*",
        " * Generated source-oriented OOT3D scene-command table.",
        " * Joins native ZSI command rows with code.bin command handlers.",
        " */",
        '#include "oot3d/scene_command_source_table.h"',
        "",
        "const Oot3dSceneCommandHandlerRow oot3d_scene_command_handler_rows[] = {",
    ]
    for row in payload["handler_rows"]:
        lines.append(
            "    { "
            f"0x{int(row['command_id']):02X}u, "
            f"{c_string(str(row.get('command_name', '')))}, "
            f"0x{hex_value(row.get('handler_entry')):08X}u, "
            f"{c_string(str(row.get('handler_name', '')))}, "
            f"{c_string(str(row.get('support_level', '')))}, "
            f"{int(row['runtime_store_count'])}u "
            "},"
        )
    lines.extend(
        [
            "};",
            "",
            "const Oot3dSceneCommandSourceRow oot3d_scene_command_source_rows[] = {",
        ]
    )
    for row in payload["command_rows"]:
        lines.append(
            "    { "
            f"{int(row['command_source_index'])}u, "
            f"{binding_kind_c_name(str(row.get('scene_binding_kind', '')))}, "
            f"{int(row['scene_binding_index'])}u, "
            f"0x{int(row['scene_id']) & 0xFF:02X}u, "
            f"{int(row['setup_index'])}u, "
            f"{int(row['command_index'])}u, "
            f"0x{int(row['offset']):08X}u, "
            f"0x{int(row['command_id']):02X}u, "
            f"{int(row['parameter'])}u, "
            f"0x{int(row['argument']):08X}u, "
            f"{c_string(str(row.get('scene_path', '')))}, "
            f"{c_string(str(row.get('source_basename', '')))}, "
            f"{c_string(str(row.get('scene_index_symbol', '')))}, "
            f"{c_string(str(row.get('command_name', '')))}, "
            f"{c_string(str(row.get('decoded_status', '')))}, "
            f"{c_string(str(row.get('support_level', '')))}, "
            f"{c_string(str(row.get('payload_symbol', '')))}, "
            f"{c_string(str(row.get('export_structs', '')))}, "
            f"{c_string(str(row.get('handler_name', '')))}, "
            f"0x{hex_value(row.get('handler_entry')):08X}u "
            "},"
        )
    lines.extend(
        [
            "};",
            "",
            "const u32 oot3d_scene_command_handler_row_count = OOT3D_SCENE_COMMAND_HANDLER_ROW_COUNT;",
            "const u32 oot3d_scene_command_source_row_count = OOT3D_SCENE_COMMAND_SOURCE_ROW_COUNT;",
            "",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    payload = build_report()
    write_json(DEFAULT_OUT_JSON, payload)
    write_csv(DEFAULT_OUT_CSV, payload["command_rows"])
    write_markdown(DEFAULT_OUT_MD, payload)
    write_header(DEFAULT_OUT_HEADER, payload)
    write_source(DEFAULT_OUT_SOURCE, payload)
    print(DEFAULT_OUT_JSON)
    print(DEFAULT_OUT_CSV)
    print(DEFAULT_OUT_MD)
    print(DEFAULT_OUT_HEADER)
    print(DEFAULT_OUT_SOURCE)
    print(json.dumps(payload["summary"], indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
