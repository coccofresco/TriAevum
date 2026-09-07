#!/usr/bin/env python3
"""Build a source-oriented OOT3D scene setup table.

This groups generated scene-command rows back into native scene setups. The
result is a decompilation index for source-like setup definitions: each setup
links to its generated payload symbols, its scene source binding, and the exact
command rows/handlers that interpret it.
"""

from __future__ import annotations

import csv
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SCENE_SOURCE_INDEX = ROOT / "analysis" / "oot3d_native_scene_source_index.json"
DEFAULT_SCENE_SOURCE_TABLE = ROOT / "analysis" / "scene_source_table.json"
DEFAULT_COMMAND_SOURCE_TABLE = ROOT / "analysis" / "scene_command_source_table.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_setup_source_table.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "scene_setup_source_table.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_setup_source_table.md"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "scene_setup_source_table.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_scene_setup_source_table.c"

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


def c_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


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
                "scene_id": row.get("scene_id"),
                "scene_id_hex": row.get("scene_id_hex"),
                "scene_index_symbol": row.get("scene_index_symbol", ""),
                "source_basename": row.get("source_basename", ""),
                "source_c_file": row.get("source_c_file", ""),
            }
    return bindings


def commands_by_setup(payload: dict[str, Any]) -> dict[tuple[str, int], list[dict[str, Any]]]:
    by_setup: dict[tuple[str, int], list[dict[str, Any]]] = defaultdict(list)
    for row in as_list(payload.get("command_rows")):
        if not isinstance(row, dict):
            continue
        key = (str(row.get("scene_path", "")).lower(), int_value(row.get("setup_index"), 0))
        by_setup[key].append(row)
    for rows in by_setup.values():
        rows.sort(key=lambda row: int_value(row.get("command_index"), 0))
    return by_setup


def setup_support_counts(command_rows: list[dict[str, Any]]) -> dict[str, Any]:
    support_counts = Counter(str(row.get("support_level", "")) for row in command_rows)
    handler_counts = Counter(str(row.get("handler_binding_status", "")) for row in command_rows)
    command_id_counts = Counter(str(row.get("command_id_hex", "")) for row in command_rows)
    decoded_status_counts = Counter(str(row.get("decoded_status", "")) for row in command_rows)
    payload_symbol_counts = Counter(
        "payload_symbol_present" if row.get("payload_symbol") else "payload_symbol_empty"
        for row in command_rows
    )
    return {
        "support_level_counts": dict(sorted(support_counts.items())),
        "handler_binding_status_counts": dict(sorted(handler_counts.items())),
        "command_id_counts": dict(sorted(command_id_counts.items())),
        "decoded_status_counts": dict(sorted(decoded_status_counts.items())),
        "payload_symbol_status_counts": dict(sorted(payload_symbol_counts.items())),
        "handler_resolved_count": handler_counts.get("code_bin_handler_resolved", 0),
        "handler_missing_count": handler_counts.get("code_bin_handler_missing", 0),
        "native_control_marker_count": support_counts.get("native_control_marker", 0),
        "decoded_count_mismatch_count": sum(
            1
            for row in command_rows
            if int_value(row.get("decoded_count"), 0) != int_value(row.get("decoded_expected_count"), 0)
        ),
    }


def payload_symbols(setup: dict[str, Any]) -> dict[str, Any]:
    names = [
        "standard_actors",
        "spawns",
        "entrances",
        "exits",
        "transition_actors",
        "light_settings",
        "paths",
        "special_files",
        "skybox_settings",
        "sound_settings",
        "cutscenes",
        "misc_settings",
    ]
    result: dict[str, Any] = {}
    for name in names:
        result[f"{name}_symbol"] = setup.get(f"{name}_symbol", "")
    return result


def payload_counts(setup: dict[str, Any]) -> dict[str, int]:
    return {
        "standard_actor_count": int_value(setup.get("standardActorCount"), 0),
        "spawn_count": int_value(setup.get("spawnCount"), 0),
        "entrance_count": int_value(setup.get("entranceCount"), 0),
        "exit_count": int_value(setup.get("exitCount"), 0),
        "transition_count": int_value(setup.get("transitionCount"), 0),
        "light_settings_count": int_value(setup.get("lightSettingsCount"), 0),
        "path_count": int_value(setup.get("pathCount"), 0),
        "special_file_count": int_value(setup.get("specialFileCount"), 0),
        "skybox_settings_count": int_value(setup.get("skyboxSettingsCount"), 0),
        "sound_settings_count": int_value(setup.get("soundSettingsCount"), 0),
        "cutscene_count": int_value(setup.get("cutsceneCount"), 0),
        "misc_settings_count": int_value(setup.get("miscSettingsCount"), 0),
    }


def build_setup_row(
    setup: dict[str, Any],
    setup_source_index: int,
    scene_bindings: dict[str, dict[str, Any]],
    command_rows: list[dict[str, Any]],
    command_ref_start: int,
) -> dict[str, Any]:
    scene_path = str(setup.get("scene_path", ""))
    binding = scene_bindings.get(scene_path.lower(), {})
    binding_kind = str(binding.get("scene_binding_kind", "unmapped"))
    return {
        "setup_source_index": setup_source_index,
        "scene_path": scene_path,
        "scene_id": int_value(binding.get("scene_id"), UNMAPPED_INDEX),
        "scene_id_hex": binding.get("scene_id_hex", ""),
        "scene_binding_kind": binding_kind,
        "scene_binding_index": int_value(binding.get("scene_binding_index"), UNMAPPED_INDEX),
        "source_basename": setup.get("source_basename", binding.get("source_basename", "")),
        "source_c_file": setup.get("source_c_file", binding.get("source_c_file", "")),
        "scene_index_symbol": binding.get("scene_index_symbol", ""),
        "setup_index": int_value(setup.get("setup_index"), 0),
        "setup_role": setup.get("setup_role", ""),
        "setup_symbol": setup.get("setup_symbol", ""),
        "command_ref_start": command_ref_start,
        "command_ref_count": len(command_rows),
        "command_count": int_value(setup.get("command_count"), 0),
        "command_ids": setup.get("command_ids", ""),
        "payload_counts_text": setup.get("payload_counts", ""),
        "decoded_statuses_text": setup.get("decoded_statuses", ""),
        "support_levels_text": setup.get("support_levels", ""),
        "unresolved_command_count": int_value(setup.get("unresolved_command_count"), 0),
        **payload_symbols(setup),
        **payload_counts(setup),
        **setup_support_counts(command_rows),
    }


def build_report() -> dict[str, Any]:
    source_payload = json.loads(DEFAULT_SCENE_SOURCE_INDEX.read_text(encoding="utf-8"))
    scene_source_payload = json.loads(DEFAULT_SCENE_SOURCE_TABLE.read_text(encoding="utf-8"))
    command_payload = json.loads(DEFAULT_COMMAND_SOURCE_TABLE.read_text(encoding="utf-8"))
    scene_bindings = scene_source_bindings(scene_source_payload)
    command_lookup = commands_by_setup(command_payload)

    setup_command_refs: list[dict[str, Any]] = []
    rows: list[dict[str, Any]] = []
    missing_command_setups: list[dict[str, Any]] = []
    for setup_index, setup in enumerate(as_list(source_payload.get("setups"))):
        if not isinstance(setup, dict):
            continue
        key = (str(setup.get("scene_path", "")).lower(), int_value(setup.get("setup_index"), 0))
        command_rows = command_lookup.get(key, [])
        command_ref_start = len(setup_command_refs)
        for command_row in command_rows:
            setup_command_refs.append(
                {
                    "setup_source_index": setup_index,
                    "command_source_index": int_value(command_row.get("command_source_index"), 0),
                    "command_index": int_value(command_row.get("command_index"), 0),
                    "command_id": int_value(command_row.get("command_id"), 0),
                    "command_id_hex": command_row.get("command_id_hex", ""),
                    "handler_binding_status": command_row.get("handler_binding_status", ""),
                    "support_level": command_row.get("support_level", ""),
                }
            )
        row = build_setup_row(setup, setup_index, scene_bindings, command_rows, command_ref_start)
        rows.append(row)
        if int_value(setup.get("command_count"), 0) != len(command_rows):
            missing_command_setups.append(
                {
                    "setup_source_index": setup_index,
                    "scene_path": setup.get("scene_path", ""),
                    "setup_index": setup.get("setup_index", ""),
                    "expected_command_count": int_value(setup.get("command_count"), 0),
                    "bound_command_count": len(command_rows),
                }
            )

    scene_binding_counts = Counter(str(row.get("scene_binding_kind", "")) for row in rows)
    setup_role_counts = Counter(str(row.get("setup_role", "")) for row in rows)
    handler_missing_count_distribution = Counter(int_value(row.get("handler_missing_count"), 0) for row in rows)
    decoded_mismatch_distribution = Counter(int_value(row.get("decoded_count_mismatch_count"), 0) for row in rows)
    unresolved_command_distribution = Counter(int_value(row.get("unresolved_command_count"), 0) for row in rows)
    summary = {
        "format": "oot3d_scene_setup_source_table_v1",
        "setup_row_count": len(rows),
        "setup_command_ref_count": len(setup_command_refs),
        "scene_binding_kind_counts": dict(sorted(scene_binding_counts.items())),
        "setup_role_counts": dict(sorted(setup_role_counts.items())),
        "handler_missing_count_distribution": {
            str(count): total for count, total in sorted(handler_missing_count_distribution.items())
        },
        "decoded_count_mismatch_distribution": {
            str(count): total for count, total in sorted(decoded_mismatch_distribution.items())
        },
        "unresolved_command_distribution": {
            str(count): total for count, total in sorted(unresolved_command_distribution.items())
        },
        "missing_command_setup_count": len(missing_command_setups),
        "source_setup_count": len(as_list(source_payload.get("setups"))),
        "command_source_row_count": int_value(
            command_payload.get("summary", {}).get("command_row_count"), 0
        ),
    }
    return {
        "summary": summary,
        "source_policy": {
            "native_scene_source_index": str(DEFAULT_SCENE_SOURCE_INDEX),
            "scene_source_table": str(DEFAULT_SCENE_SOURCE_TABLE),
            "scene_command_source_table": str(DEFAULT_COMMAND_SOURCE_TABLE),
            "n64_policy": "No N64 source is read by this setup source table. It groups OOT3D ZSI command rows and OOT3D code.bin handler evidence.",
        },
        "rows": rows,
        "setup_command_refs": setup_command_refs,
        "missing_command_setups": missing_command_setups,
    }


CSV_COLUMNS = [
    "setup_source_index",
    "scene_path",
    "scene_id_hex",
    "scene_binding_kind",
    "scene_binding_index",
    "setup_index",
    "setup_role",
    "setup_symbol",
    "command_ref_start",
    "command_ref_count",
    "command_count",
    "handler_resolved_count",
    "handler_missing_count",
    "native_control_marker_count",
    "decoded_count_mismatch_count",
    "spawn_count",
    "entrance_count",
    "exit_count",
    "transition_count",
    "light_settings_count",
    "path_count",
    "cutscene_count",
    "support_levels_text",
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
        "# Scene Setup Source Table",
        "",
        "Generated by grouping OOT3D-native scene-command source rows by scene setup.",
        "",
        "## Summary",
        "",
        f"- Setup rows: {summary['setup_row_count']}",
        f"- Setup command refs: {summary['setup_command_ref_count']}",
        f"- Scene binding: `{json.dumps(summary['scene_binding_kind_counts'], sort_keys=True)}`",
        f"- Setup roles: `{json.dumps(summary['setup_role_counts'], sort_keys=True)}`",
        f"- Handler-missing distribution: `{json.dumps(summary['handler_missing_count_distribution'], sort_keys=True)}`",
        f"- Decoded-count mismatch distribution: `{json.dumps(summary['decoded_count_mismatch_distribution'], sort_keys=True)}`",
        f"- Unresolved-command distribution: `{json.dumps(summary['unresolved_command_distribution'], sort_keys=True)}`",
        f"- Missing command setup rows: {summary['missing_command_setup_count']}",
        "",
        "## Setup Rows",
        "",
        "| # | Scene | Setup | Role | Commands | Handler missing | Payloads | Binding |",
        "| ---: | --- | ---: | --- | ---: | ---: | --- | --- |",
    ]
    for row in payload["rows"]:
        lines.append(
            f"| {row['setup_source_index']} | `{row['scene_path']}` | {row['setup_index']} | "
            f"`{row['setup_role']}` | {row['command_ref_count']} | {row['handler_missing_count']} | "
            f"`{row['payload_counts_text']}` | `{row['scene_binding_kind']}` |"
        )

    if payload["missing_command_setups"]:
        lines.extend(
            [
                "",
                "## Missing Command Bindings",
                "",
                "| Setup | Scene | SetupIndex | Expected | Bound |",
                "| ---: | --- | ---: | ---: | ---: |",
            ]
        )
        for row in payload["missing_command_setups"]:
            lines.append(
                f"| {row['setup_source_index']} | `{row['scene_path']}` | {row['setup_index']} | "
                f"{row['expected_command_count']} | {row['bound_command_count']} |"
            )

    lines.extend(
        [
            "",
            "## Notes",
            "",
            "- `command_ref_start` and `command_ref_count` point into the contiguous setup-command-ref array.",
            "- Handler-missing counts are expected to be one per setup for command `0x14` end markers.",
            "- Spawn-list decoded-count mismatches reflect the native handler selecting the active entrance spawn from a decoded spawn list.",
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
        "#ifndef OOT3D_SCENE_SETUP_SOURCE_TABLE_H",
        "#define OOT3D_SCENE_SETUP_SOURCE_TABLE_H",
        "",
        '#include "oot3d/scene_command_source_table.h"',
        "",
        "enum {",
        f"    OOT3D_SCENE_SETUP_SOURCE_ROW_COUNT = {len(payload['rows'])},",
        f"    OOT3D_SCENE_SETUP_COMMAND_REF_COUNT = {len(payload['setup_command_refs'])},",
        "};",
        "",
        "typedef struct {",
        "    u16 setupSourceIndex;",
        "    u16 commandSourceIndex;",
        "    u16 commandIndex;",
        "    u8 commandId;",
        "} Oot3dSceneSetupCommandRef;",
        "",
        "typedef struct {",
        "    u16 setupSourceIndex;",
        "    Oot3dSceneCommandBindingKind bindingKind;",
        "    u16 bindingIndex;",
        "    u8 sceneId;",
        "    u16 setupIndex;",
        "    const char* scenePath;",
        "    const char* sourceBasename;",
        "    const char* sceneIndexSymbol;",
        "    const char* setupRole;",
        "    const char* setupSymbol;",
        "    u16 commandRefStart;",
        "    u16 commandRefCount;",
        "    u16 handlerResolvedCount;",
        "    u16 handlerMissingCount;",
        "    u16 nativeControlMarkerCount;",
        "    u16 decodedCountMismatchCount;",
        "    const char* spawnsSymbol;",
        "    const char* entrancesSymbol;",
        "    const char* exitsSymbol;",
        "    const char* transitionActorsSymbol;",
        "    const char* lightSettingsSymbol;",
        "    const char* pathsSymbol;",
        "    const char* cutscenesSymbol;",
        "} Oot3dSceneSetupSourceRow;",
        "",
        "extern const Oot3dSceneSetupCommandRef oot3d_scene_setup_command_refs[];",
        "extern const Oot3dSceneSetupSourceRow oot3d_scene_setup_source_rows[];",
        "extern const u32 oot3d_scene_setup_command_ref_count;",
        "extern const u32 oot3d_scene_setup_source_row_count;",
        "",
        "#endif",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def write_source(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "/*",
        " * Generated source-oriented OOT3D scene-setup table.",
        " * Groups scene-command source rows by native scene setup.",
        " */",
        '#include "oot3d/scene_setup_source_table.h"',
        "",
        "const Oot3dSceneSetupCommandRef oot3d_scene_setup_command_refs[] = {",
    ]
    for row in payload["setup_command_refs"]:
        lines.append(
            "    { "
            f"{int(row['setup_source_index'])}u, "
            f"{int(row['command_source_index'])}u, "
            f"{int(row['command_index'])}u, "
            f"0x{int(row['command_id']):02X}u "
            "},"
        )
    lines.extend(
        [
            "};",
            "",
            "const Oot3dSceneSetupSourceRow oot3d_scene_setup_source_rows[] = {",
        ]
    )
    for row in payload["rows"]:
        lines.append(
            "    { "
            f"{int(row['setup_source_index'])}u, "
            f"{binding_kind_c_name(str(row.get('scene_binding_kind', '')))}, "
            f"{int(row['scene_binding_index'])}u, "
            f"0x{int(row['scene_id']) & 0xFF:02X}u, "
            f"{int(row['setup_index'])}u, "
            f"{c_string(str(row.get('scene_path', '')))}, "
            f"{c_string(str(row.get('source_basename', '')))}, "
            f"{c_string(str(row.get('scene_index_symbol', '')))}, "
            f"{c_string(str(row.get('setup_role', '')))}, "
            f"{c_string(str(row.get('setup_symbol', '')))}, "
            f"{int(row['command_ref_start'])}u, "
            f"{int(row['command_ref_count'])}u, "
            f"{int(row['handler_resolved_count'])}u, "
            f"{int(row['handler_missing_count'])}u, "
            f"{int(row['native_control_marker_count'])}u, "
            f"{int(row['decoded_count_mismatch_count'])}u, "
            f"{c_string(str(row.get('spawns_symbol', '')))}, "
            f"{c_string(str(row.get('entrances_symbol', '')))}, "
            f"{c_string(str(row.get('exits_symbol', '')))}, "
            f"{c_string(str(row.get('transition_actors_symbol', '')))}, "
            f"{c_string(str(row.get('light_settings_symbol', '')))}, "
            f"{c_string(str(row.get('paths_symbol', '')))}, "
            f"{c_string(str(row.get('cutscenes_symbol', '')))} "
            "},"
        )
    lines.extend(
        [
            "};",
            "",
            "const u32 oot3d_scene_setup_command_ref_count = OOT3D_SCENE_SETUP_COMMAND_REF_COUNT;",
            "const u32 oot3d_scene_setup_source_row_count = OOT3D_SCENE_SETUP_SOURCE_ROW_COUNT;",
            "",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    payload = build_report()
    write_json(DEFAULT_OUT_JSON, payload)
    write_csv(DEFAULT_OUT_CSV, payload["rows"])
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
