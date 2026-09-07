#!/usr/bin/env python3
"""Build an OOT3D-native table for CS_CMD_SET_LIGHTING cutscene records."""

from __future__ import annotations

import csv
import json
import struct
import sys
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_NATIVE_DECODE_TABLE = ROOT / "analysis" / "scene_cutscene_native_decode_table.json"
DEFAULT_NATIVE_COMMAND_CSV = ROOT / "analysis" / "scene_cutscene_native_command_table.csv"
DEFAULT_CUTSCENE_PROCESS_DECOMPILE = (
    ROOT
    / "analysis"
    / "scene_cutscene_context_ghidra_export"
    / "decompiled"
    / "99068_002c5ba0_Cutscene_ProcessCommands.c"
)
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_cutscene_lighting_table.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "scene_cutscene_lighting_table.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_cutscene_lighting_table.md"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "scene_cutscene_lighting_table.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_scene_cutscene_lighting_table.c"

CUTSCENE_PROCESS_COMMANDS = 0x002C5BA0
CS_CMD_SET_LIGHTING = 0x00000004
LIGHTING_ENTRY_SIZE = 0x30
PLAY_TARGET_LIGHT_SETTING_OFFSET = 0x3237
PLAY_BLEND_WEIGHT_OFFSET = 0x3258
TARGET_INVALID_VALUE = 0xFF


def as_dict(value: Any) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def int_value(value: Any, default: int = 0) -> int:
    try:
        if value is None:
            return default
        if isinstance(value, str) and value.lower().startswith("0x"):
            return int(value, 16)
        return int(value)
    except (TypeError, ValueError):
        return default


def c_string(value: Any) -> str:
    text = "" if value is None else str(value)
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def c_u8(value: Any) -> str:
    return f"{int_value(value) & 0xFF}u"


def c_u16(value: Any) -> str:
    return f"{int_value(value) & 0xFFFF}u"


def c_u32(value: Any) -> str:
    return f"{int_value(value) & 0xFFFFFFFF}u"


def read_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def read_native_command_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def load_scene_bytes(scene_root: Path, scene_path: str, cache: dict[str, bytes]) -> bytes:
    if scene_path not in cache:
        cache[scene_path] = (scene_root / scene_path).read_bytes()
    return cache[scene_path]


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def runtime_target_light_setting(raw_light_setting_index: int) -> int:
    return (raw_light_setting_index - 1) & 0xFF


def decode_runtime_semantic(raw_light_setting_index: int) -> str:
    target = runtime_target_light_setting(raw_light_setting_index)
    if target == TARGET_INVALID_VALUE:
        target_text = "0xFF sentinel"
    else:
        target_text = f"zero-based setting {target}"
    return (
        "Cutscene_ProcessCommands case 4: when csCtx frame equals entry +0x02, "
        f"write PlayState+0x3237 = entry +0x00 - 1 ({target_text}) and reset "
        "PlayState+0x3258 transition/blend weight; consumed by code.bin 0x0045DD50"
    )


def decode_lighting_rows(
    command_rows: list[dict[str, str]],
    scene_root: Path,
) -> tuple[list[dict[str, Any]], list[str]]:
    rows: list[dict[str, Any]] = []
    errors: list[str] = []
    scene_cache: dict[str, bytes] = {}

    for command in command_rows:
        command_id = int_value(command.get("command_id_hex"))
        category = command.get("category", "")
        if command_id != CS_CMD_SET_LIGHTING:
            continue
        if category != "counted_12word_entries":
            errors.append(
                f"command {command.get('native_command_source_index')}: "
                f"CS_CMD_SET_LIGHTING category is {category}"
            )
            continue

        scene_path = str(command.get("scene_path", ""))
        command_offset = int_value(command.get("command_offset_hex"))
        entry_count = int_value(command.get("entry_count"))
        try:
            data = load_scene_bytes(scene_root, scene_path, scene_cache)
        except FileNotFoundError:
            errors.append(f"missing scene file for {scene_path}")
            continue
        if command_offset < 0 or command_offset + 8 > len(data):
            errors.append(
                f"command {command.get('native_command_source_index')}: command header outside {scene_path}"
            )
            continue

        native_command_id = u32(data, command_offset)
        native_entry_count = u32(data, command_offset + 4)
        expected_size = 8 + entry_count * LIGHTING_ENTRY_SIZE
        if native_command_id != CS_CMD_SET_LIGHTING:
            errors.append(
                f"command {command.get('native_command_source_index')}: native id 0x{native_command_id:08X}"
            )
        if native_entry_count != entry_count:
            errors.append(
                f"command {command.get('native_command_source_index')}: "
                f"entry count {native_entry_count} != {entry_count}"
            )
        if command_offset + expected_size > len(data):
            errors.append(
                f"command {command.get('native_command_source_index')}: entries outside {scene_path}"
            )
            continue

        for entry_index in range(entry_count):
            entry_offset = command_offset + 8 + entry_index * LIGHTING_ENTRY_SIZE
            raw = data[entry_offset : entry_offset + LIGHTING_ENTRY_SIZE]
            raw_words = [u32(raw, word_index * 4) for word_index in range(12)]
            raw_light_setting_index = u16(raw, 0)
            start_frame = u16(raw, 2)
            end_frame = u16(raw, 4)
            target_light_setting = runtime_target_light_setting(raw_light_setting_index)
            rows.append(
                {
                    "lighting_source_index": len(rows),
                    "native_command_source_index": int_value(command.get("native_command_source_index")),
                    "cutscene_source_index": int_value(command.get("cutscene_source_index")),
                    "scene_path": scene_path,
                    "setup_index": int_value(command.get("setup_index")),
                    "local_command_index": int_value(command.get("local_command_index")),
                    "command_offset": command_offset,
                    "command_offset_hex": f"0x{command_offset:08X}",
                    "entry_index": entry_index,
                    "entry_offset": entry_offset,
                    "entry_offset_hex": f"0x{entry_offset:08X}",
                    "raw_light_setting_index": raw_light_setting_index,
                    "target_light_setting": target_light_setting,
                    "target_light_setting_resolved": 1,
                    "target_light_setting_is_sentinel": 1 if target_light_setting == TARGET_INVALID_VALUE else 0,
                    "play_target_light_setting_offset": PLAY_TARGET_LIGHT_SETTING_OFFSET,
                    "play_blend_weight_offset": PLAY_BLEND_WEIGHT_OFFSET,
                    "start_frame": start_frame,
                    "end_frame": end_frame,
                    "start_frame_trigger_confirmed": 1,
                    "runtime_semantic": decode_runtime_semantic(raw_light_setting_index),
                    "raw_words": [f"0x{word:08X}" for word in raw_words],
                    "raw_words_text": ";".join(f"0x{word:08X}" for word in raw_words),
                    "raw_hex": raw.hex(),
                }
            )

    return rows, errors


def verify_decompile_patterns(path: Path) -> list[str]:
    if not path.exists():
        return [f"missing Cutscene_ProcessCommands decompile: {path}"]
    text = path.read_text(encoding="utf-8")
    patterns = [
        "case 4:",
        "*(char *)(param_1 + 0x3237) = (char)(ushort)*puVar18 + -1",
        "*(float *)(param_1 + 0x3258) = fVar23",
        "*(ushort *)(param_2 + 0x20) == *(ushort *)((int)puVar18 + 2)",
    ]
    return [
        f"Cutscene_ProcessCommands missing lighting pattern `{pattern}`"
        for pattern in patterns
        if pattern not in text
    ]


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True, allow_nan=False)
        handle.write("\n")


def csv_value(value: Any) -> str:
    if isinstance(value, list):
        return ";".join(str(item) for item in value)
    return str(value)


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    fieldnames = [
        "lighting_source_index",
        "native_command_source_index",
        "cutscene_source_index",
        "scene_path",
        "setup_index",
        "local_command_index",
        "command_offset_hex",
        "entry_index",
        "entry_offset_hex",
        "raw_light_setting_index",
        "target_light_setting",
        "target_light_setting_is_sentinel",
        "start_frame",
        "end_frame",
        "runtime_semantic",
        "raw_words_text",
        "raw_hex",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            writer.writerow({key: csv_value(row.get(key, "")) for key in fieldnames})


def write_markdown(path: Path, payload: dict[str, Any], errors: list[str]) -> None:
    summary = as_dict(payload.get("summary"))
    title_rows = [
        row
        for row in payload.get("lighting_rows", [])
        if as_dict(row).get("scene_path") in {"jyasinzou_boss_info.zsi", "spot01_info.zsi"}
    ]
    lines = [
        "# Scene Cutscene Lighting Table",
        "",
        "Entry-level decode of OOT3D-native `CS_CMD_SET_LIGHTING` (`command id 4`) records from original `.zsi` cutscene payloads.",
        "",
        "## Summary",
        "",
        f"- Scene root: `{summary.get('scene_root')}`",
        f"- `CS_CMD_SET_LIGHTING` commands: {summary.get('lighting_command_count')}",
        f"- Lighting entries: {summary.get('lighting_row_count')}",
        f"- Distinct raw light-setting indices: {summary.get('distinct_raw_light_setting_count')}",
        f"- Status: `{'pass' if not errors else 'fail'}`",
        "",
        "## Native Runtime Evidence",
        "",
        f"- `Cutscene_ProcessCommands` at `0x{CUTSCENE_PROCESS_COMMANDS:08X}` handles command `4`.",
        "- Entry `+0x00` is the one-based light-setting index; native code writes `entry[0] - 1` to `PlayState+0x3237`.",
        "- Entry `+0x02` is the exact trigger frame matched against `csCtx+0x20`.",
        "- The same branch resets `PlayState+0x3258`, the environment light-setting blend/transition weight consumed by `0x0045DD50`.",
        "",
        "## Title Intro Chain Rows",
        "",
        "| scene | cutscene | setup | command | entry | raw index | target index | start | end |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for row in title_rows:
        item = as_dict(row)
        lines.append(
            f"| `{item.get('scene_path')}` | {item.get('cutscene_source_index')} | "
            f"{item.get('setup_index')} | {item.get('native_command_source_index')} | "
            f"{item.get('entry_index')} | {item.get('raw_light_setting_index')} | "
            f"{item.get('target_light_setting')} | {item.get('start_frame')} | {item.get('end_frame')} |"
        )
    lines.extend(
        [
            "",
            "## Outputs",
            "",
            "- `analysis/scene_cutscene_lighting_table.json`",
            "- `analysis/scene_cutscene_lighting_table.csv`",
            "- `include/oot3d/scene_cutscene_lighting_table.h`",
            "- `src/code/z_scene_cutscene_lighting_table.c`",
            "",
        ]
    )
    if errors:
        lines.extend(["## Errors", ""])
        lines.extend(f"- {error}" for error in errors)
        lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_header(path: Path, row_count: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "#ifndef OOT3D_SCENE_CUTSCENE_LIGHTING_TABLE_H",
        "#define OOT3D_SCENE_CUTSCENE_LIGHTING_TABLE_H",
        "",
        '#include "oot3d/scene.h"',
        "",
        "enum {",
        f"    OOT3D_SCENE_CUTSCENE_LIGHTING_ROW_COUNT = {row_count},",
        "    OOT3D_CUTSCENE_LIGHTING_PLAY_TARGET_LIGHT_SETTING_OFFSET = 0x3237,",
        "    OOT3D_CUTSCENE_LIGHTING_PLAY_BLEND_WEIGHT_OFFSET = 0x3258,",
        "    OOT3D_CUTSCENE_LIGHTING_TARGET_INVALID_VALUE = 0xFF,",
        "};",
        "",
        "typedef struct {",
        "    u16 lightingSourceIndex;",
        "    u16 nativeCommandSourceIndex;",
        "    u16 cutsceneSourceIndex;",
        "    u16 setupIndex;",
        "    u16 localCommandIndex;",
        "    u16 entryIndex;",
        "    u32 commandOffset;",
        "    u32 entryOffset;",
        "    u16 rawLightSettingIndex;",
        "    u8 targetLightSetting;",
        "    u8 targetLightSettingResolved;",
        "    u8 targetLightSettingIsSentinel;",
        "    u16 playTargetLightSettingOffset;",
        "    u16 playBlendWeightOffset;",
        "    u16 startFrame;",
        "    u16 endFrame;",
        "    u8 startFrameTriggerConfirmed;",
        "    const char* scenePath;",
        "    const char* runtimeSemantic;",
        "    const char* rawWordsText;",
        "} Oot3dSceneCutsceneLightingRow;",
        "",
        "extern const Oot3dSceneCutsceneLightingRow oot3d_scene_cutscene_lighting_rows[];",
        "extern const u32 oot3d_scene_cutscene_lighting_row_count;",
        "",
        "const Oot3dSceneCutsceneLightingRow* Oot3d_CutsceneLightingGetRow(u16 lightingSourceIndex);",
        "",
        "u32 Oot3d_CutsceneLightingCollectStartTriggers(",
        "    u16 cutsceneSourceIndex,",
        "    s32 frame,",
        "    const Oot3dSceneCutsceneLightingRow** outRows,",
        "    u32 maxRows",
        ");",
        "",
        "#endif",
        "",
    ]
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_source(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "/* Generated by build_scene_cutscene_lighting_table.py. */",
        "",
        '#include "oot3d/scene_cutscene_lighting_table.h"',
        "",
        "#include <stddef.h>",
        "",
        "const Oot3dSceneCutsceneLightingRow oot3d_scene_cutscene_lighting_rows[] = {",
    ]
    for row in rows:
        lines.append(
            "    { "
            f"{c_u16(row.get('lighting_source_index'))}, "
            f"{c_u16(row.get('native_command_source_index'))}, "
            f"{c_u16(row.get('cutscene_source_index'))}, "
            f"{c_u16(row.get('setup_index'))}, "
            f"{c_u16(row.get('local_command_index'))}, "
            f"{c_u16(row.get('entry_index'))}, "
            f"{c_u32(row.get('command_offset'))}, "
            f"{c_u32(row.get('entry_offset'))}, "
            f"{c_u16(row.get('raw_light_setting_index'))}, "
            f"{c_u8(row.get('target_light_setting'))}, "
            f"{c_u8(row.get('target_light_setting_resolved'))}, "
            f"{c_u8(row.get('target_light_setting_is_sentinel'))}, "
            f"{c_u16(row.get('play_target_light_setting_offset'))}, "
            f"{c_u16(row.get('play_blend_weight_offset'))}, "
            f"{c_u16(row.get('start_frame'))}, "
            f"{c_u16(row.get('end_frame'))}, "
            f"{c_u8(row.get('start_frame_trigger_confirmed'))}, "
            f"{c_string(row.get('scene_path'))}, "
            f"{c_string(row.get('runtime_semantic'))}, "
            f"{c_string(row.get('raw_words_text'))} "
            "},"
        )
    lines.extend(
        [
            "};",
            "",
            "const u32 oot3d_scene_cutscene_lighting_row_count = OOT3D_SCENE_CUTSCENE_LIGHTING_ROW_COUNT;",
            "",
            "const Oot3dSceneCutsceneLightingRow* Oot3d_CutsceneLightingGetRow(u16 lightingSourceIndex) {",
            "    if (lightingSourceIndex >= oot3d_scene_cutscene_lighting_row_count) {",
            "        return NULL;",
            "    }",
            "    return &oot3d_scene_cutscene_lighting_rows[lightingSourceIndex];",
            "}",
            "",
            "static void Oot3d_CutsceneLightingMaybeStoreRow(",
            "    const Oot3dSceneCutsceneLightingRow* row,",
            "    const Oot3dSceneCutsceneLightingRow** outRows,",
            "    u32 maxRows,",
            "    u32 count",
            ") {",
            "    if (outRows != NULL && count < maxRows) {",
            "        outRows[count] = row;",
            "    }",
            "}",
            "",
            "u32 Oot3d_CutsceneLightingCollectStartTriggers(",
            "    u16 cutsceneSourceIndex,",
            "    s32 frame,",
            "    const Oot3dSceneCutsceneLightingRow** outRows,",
            "    u32 maxRows",
            ") {",
            "    u32 rowIndex;",
            "    u32 count = 0;",
            "",
            "    for (rowIndex = 0; rowIndex < oot3d_scene_cutscene_lighting_row_count; rowIndex++) {",
            "        const Oot3dSceneCutsceneLightingRow* row = &oot3d_scene_cutscene_lighting_rows[rowIndex];",
            "",
            "        if (row->cutsceneSourceIndex != cutsceneSourceIndex) {",
            "            continue;",
            "        }",
            "        if (row->startFrameTriggerConfirmed == 0) {",
            "            continue;",
            "        }",
            "        if ((s32)row->startFrame == frame) {",
            "            Oot3d_CutsceneLightingMaybeStoreRow(row, outRows, maxRows, count);",
            "            count++;",
            "        }",
            "    }",
            "",
            "    return count;",
            "}",
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def build_payload() -> tuple[dict[str, Any], list[str]]:
    errors: list[str] = []
    native_decode = read_json(DEFAULT_NATIVE_DECODE_TABLE)
    scene_root = Path(str(as_dict(native_decode.get("summary")).get("scene_root", "")))
    if not scene_root.is_dir():
        errors.append(f"scene root not found: {scene_root}")
    command_rows = read_native_command_rows(DEFAULT_NATIVE_COMMAND_CSV)
    lighting_rows, decode_errors = decode_lighting_rows(command_rows, scene_root)
    errors.extend(decode_errors)
    errors.extend(verify_decompile_patterns(DEFAULT_CUTSCENE_PROCESS_DECOMPILE))

    raw_index_counts = Counter(row["raw_light_setting_index"] for row in lighting_rows)
    scene_counts = Counter(row["scene_path"] for row in lighting_rows)
    lighting_command_count = sum(
        1
        for row in command_rows
        if int_value(row.get("command_id_hex")) == CS_CMD_SET_LIGHTING
        and row.get("category") == "counted_12word_entries"
    )
    summary = {
        "format": "oot3d_scene_cutscene_lighting_table_v1",
        "scene_root": str(scene_root),
        "source_native_decode_table": str(DEFAULT_NATIVE_DECODE_TABLE),
        "source_native_command_table": str(DEFAULT_NATIVE_COMMAND_CSV),
        "runtime_reference": f"Cutscene_ProcessCommands 0x{CUTSCENE_PROCESS_COMMANDS:08X}",
        "lighting_command_count": lighting_command_count,
        "lighting_row_count": len(lighting_rows),
        "distinct_raw_light_setting_count": len(raw_index_counts),
        "raw_light_setting_counts": dict(sorted(raw_index_counts.items())),
        "scene_counts": dict(sorted(scene_counts.items())),
    }
    return {"summary": summary, "lighting_rows": lighting_rows}, errors


def main() -> int:
    payload, errors = build_payload()
    write_json(DEFAULT_OUT_JSON, payload)
    write_csv(DEFAULT_OUT_CSV, payload["lighting_rows"])
    write_markdown(DEFAULT_OUT_MD, payload, errors)
    write_header(DEFAULT_OUT_HEADER, len(payload["lighting_rows"]))
    write_source(DEFAULT_OUT_SOURCE, payload["lighting_rows"])
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1
    summary = as_dict(payload.get("summary"))
    print(
        "wrote scene cutscene lighting table: "
        f"{summary.get('lighting_row_count')} entries, "
        f"{summary.get('distinct_raw_light_setting_count')} distinct light-setting indices"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
