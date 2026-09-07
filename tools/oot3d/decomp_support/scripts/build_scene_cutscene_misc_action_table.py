#!/usr/bin/env python3
"""Build an entry-level table for OOT3D CS_CMD_MISC native actions."""

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
DEFAULT_MISC_HELPER_EXPORT_DIR = ROOT / "analysis" / "cutscene_misc_target_helpers_ghidra_export"
DEFAULT_CODE_BIN = Path(r"E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin")
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_cutscene_misc_action_table.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "scene_cutscene_misc_action_table.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_cutscene_misc_action_table.md"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "scene_cutscene_misc_action_table.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_scene_cutscene_misc_action_table.c"

CODE_BIN_VA_BASE = 0x00100000
CUTSCENE_PROCESS_COMMANDS = 0x002C5BA0
CS_CMD_MISC = 0x00000003
MISC_ENTRY_SIZE = 0x30

EXPECTED_CODE_LITERALS = {
    0x002C655C: (0x00003271, "misc action 0x09 PlayState byte offset"),
    0x002C6560: (0x00000672, "misc action 0x0B transition counter ceiling"),
    0x002C6568: (0x452BF000, "misc action 0x0B timed-audio frame numerator 2751.0"),
    0x002C656C: (0x3F000000, "misc action 0x0B float rounding bias 0.5"),
    0x002C6964: (0x00000A64, "active camera index offset used to choose camera pointer for Quake_Add"),
    0x002C6968: (0x00007FFF, "Quake_SetSpeed argument for misc action 0x10"),
    0x00340ABC: (0x01000498, "FUN_00340A1C camera data-index sound for non-1 target"),
    0x00340AC0: (0x01000499, "FUN_00340A1C camera data-index sound for target 1"),
    0x00340AC4: (0x00000A64, "FUN_00340A1C active camera index offset"),
}

MISC_ACTION_NAMES = {
    0x0007: "CS_MISC_START_LIGHT_MODE_BLEND_0_TO_1",
    0x0008: "CS_MISC_RAMP_53F0_SMALL",
    0x0009: "CS_MISC_SET_3271_TO_10",
    0x000B: "CS_MISC_RAMP_53F0_TIMED_SFX",
    0x000C: "CS_MISC_REQUEST_CUTSCENE_END",
    0x000E: "CS_MISC_SET_CAMERA_DATA_INDEX_0",
    0x0010: "CS_MISC_START_CAMERA_QUAKE",
    0x0011: "CS_MISC_STOP_CAMERA_QUAKE",
}


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


def c_s32(value: Any) -> str:
    raw = int_value(value)
    return str(max(-2147483648, min(2147483647, raw)))


def read_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def read_u32(data: bytes, va: int) -> int | None:
    offset = va - CODE_BIN_VA_BASE
    if offset < 0 or offset + 4 > len(data):
        return None
    return struct.unpack_from("<I", data, offset)[0]


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


def decode_action_name(action_id: int) -> str:
    return MISC_ACTION_NAMES.get(action_id, f"CS_MISC_ACTION_0x{action_id:04X}")


def decode_runtime_semantic(action_id: int) -> str:
    if action_id == 0x0007:
        return "light_mode_blend_start: on start frame write PlayState+0x31B3=1, +0x31B1=0, +0x31B2=1, +0x31B6=0x003C, +0x31B4=0x003C; consumed by code.bin 0x0045DD50 runtime light transition mode blend"
    if action_id == 0x0008:
        return "transition_counter_small_ramp: while PlayState+0x53F0 < 0x80, add 4"
    if action_id == 0x0009:
        return "environment_runtime_flag: write 0x10 to PlayState+0x3271"
    if action_id == 0x000B:
        return "transition_counter_timed_sfx: while PlayState+0x53F0 < 0x0672 add 0x14; at int(2751.0 / actorScale + 0.5) play native SFX and reset PlayState+0x53F0"
    if action_id == 0x000C:
        return "cutscene_end_request: on start frame, if csCtx state != 4, increment PlayState+0x22AC, clear camera reset flag when active camera is setting 0x25, then write state 3 to PlayState+0x22A0"
    if action_id == 0x000E:
        return "camera_data_index_0: on start frame call FUN_00340A1C(play,1), which stores PlayState+0x7F24=1 and calls Camera_ChangeDataIdx(activeCamera,0) with optional native SFX"
    if action_id == 0x0010:
        return "quake_start: Quake_Add(callback=6), store csCtx+0x30, SetSpeed(0x7FFF), SetQuakeValues(2,0,100,0), SetCountdown(800)"
    if action_id == 0x0011:
        return "quake_stop: Quake_RemoveFromIdx(csCtx+0x30)"
    return "pending_misc_action_semantics"


def decode_misc_actions(
    command_rows: list[dict[str, str]],
    scene_root: Path,
) -> tuple[list[dict[str, Any]], list[str]]:
    rows: list[dict[str, Any]] = []
    errors: list[str] = []
    scene_cache: dict[str, bytes] = {}

    for command in command_rows:
        command_id = int_value(command.get("command_id_hex"))
        category = command.get("category", "")
        if command_id != CS_CMD_MISC:
            continue
        if category != "counted_12word_entries":
            errors.append(
                f"command {command.get('native_command_source_index')}: CS_CMD_MISC category is {category}"
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
        expected_size = 8 + entry_count * MISC_ENTRY_SIZE
        if native_command_id != CS_CMD_MISC:
            errors.append(
                f"command {command.get('native_command_source_index')}: native id 0x{native_command_id:08X}"
            )
        if native_entry_count != entry_count:
            errors.append(
                f"command {command.get('native_command_source_index')}: entry count {native_entry_count} != {entry_count}"
            )
        if command_offset + expected_size > len(data):
            errors.append(
                f"command {command.get('native_command_source_index')}: entries outside {scene_path}"
            )
            continue

        for entry_index in range(entry_count):
            entry_offset = command_offset + 8 + entry_index * MISC_ENTRY_SIZE
            raw = data[entry_offset : entry_offset + MISC_ENTRY_SIZE]
            action_id = u16(raw, 0)
            start_frame = u16(raw, 2)
            end_frame = u16(raw, 4)
            raw_words = [
                f"0x{struct.unpack_from('<I', raw, word_index * 4)[0]:08X}"
                for word_index in range(12)
            ]
            rows.append(
                {
                    "misc_action_source_index": len(rows),
                    "native_command_source_index": int_value(
                        command.get("native_command_source_index")
                    ),
                    "cutscene_source_index": int_value(command.get("cutscene_source_index")),
                    "scene_path": scene_path,
                    "setup_index": int_value(command.get("setup_index")),
                    "local_command_index": int_value(command.get("local_command_index")),
                    "command_offset": command_offset,
                    "command_offset_hex": f"0x{command_offset:08X}",
                    "entry_index": entry_index,
                    "entry_offset": entry_offset,
                    "entry_offset_hex": f"0x{entry_offset:08X}",
                    "action_id": action_id,
                    "action_id_hex": f"0x{action_id:04X}",
                    "action_name": decode_action_name(action_id),
                    "runtime_semantic": decode_runtime_semantic(action_id),
                    "start_frame": start_frame,
                    "end_frame": end_frame,
                    "duration_frames": max(0, end_frame - start_frame),
                    "start_frame_trigger_confirmed": 1
                    if action_id in {0x0007, 0x000C, 0x000E, 0x0010, 0x0011}
                    else 0,
                    "active_window_inclusive_start_exclusive_end": 1,
                    "raw_words": raw_words,
                    "raw_words_text": ";".join(raw_words),
                    "raw_hex": raw.hex(),
                }
            )

    return rows, errors


def verify_codebin_literals(path: Path) -> tuple[list[dict[str, Any]], list[str]]:
    rows: list[dict[str, Any]] = []
    errors: list[str] = []
    if not path.exists():
        return rows, [f"missing code.bin: {path}"]
    data = path.read_bytes()
    for va, (expected, label) in EXPECTED_CODE_LITERALS.items():
        value = read_u32(data, va)
        rows.append(
            {
                "address": va,
                "address_hex": f"0x{va:08X}",
                "expected": expected,
                "expected_hex": f"0x{expected:08X}",
                "value": value,
                "value_hex": "unavailable" if value is None else f"0x{value:08X}",
                "label": label,
            }
        )
        if value != expected:
            errors.append(f"literal 0x{va:08X}: expected 0x{expected:08X}, got {value!r}")
    return rows, errors


def verify_decompile_patterns(path: Path) -> list[str]:
    if not path.exists():
        return [f"missing Cutscene_ProcessCommands decompile: {path}"]
    text = path.read_text(encoding="utf-8")
    patterns = [
        "case 3:",
        "switch(uVar16)",
        "case 0x10:",
        "Quake_Add",
        "Quake_SetSpeed",
        "Quake_SetQuakeValues",
        "Quake_SetCountdown",
        "case 0x11:",
        "Quake_RemoveFromIdx",
        "*(short *)(param_2 + 0x30)",
        "case 8:",
        "*(short *)(param_1 + 0x53f0) + 4",
        "case 9:",
        "*(undefined1 *)(DAT_002c655c + param_1) = 0x10",
        "case 0xb:",
        "*(short *)(param_1 + 0x53f0) + 0x14",
        "case 0xc:",
        "*(int *)(local_30 + 0x14) = *(int *)(local_30 + 0x14) + 1",
        "*(undefined1 *)(local_30 + 8) = 3",
        "case 0xe:",
        "FUN_00340a1c(param_1,1)",
    ]
    return [f"Cutscene_ProcessCommands missing pattern `{pattern}`" for pattern in patterns if pattern not in text]


def verify_helper_export(path: Path) -> list[str]:
    decompiled_path = path / "decompiled" / "99001_00340a1c_FUN_00340a1c.c"
    disassembly_path = path / "disassembly_selected.txt"
    if not decompiled_path.exists() or not disassembly_path.exists():
        return [f"missing misc helper Ghidra export: {path}"]
    text = decompiled_path.read_text(encoding="utf-8")
    patterns = [
        "*(char *)(param_1 + 0x7f24) = (char)param_2",
        "Audio_PlaySoundGeneral",
        "Camera_ChangeDataIdx",
        "*(byte *)(param_1 + 0x7f24) - 1",
    ]
    return [f"FUN_00340A1C helper missing pattern `{pattern}`" for pattern in patterns if pattern not in text]


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
        "misc_action_source_index",
        "native_command_source_index",
        "cutscene_source_index",
        "scene_path",
        "setup_index",
        "local_command_index",
        "command_offset_hex",
        "entry_index",
        "entry_offset_hex",
        "action_id_hex",
        "action_name",
        "runtime_semantic",
        "start_frame",
        "end_frame",
        "duration_frames",
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
    quake_rows = [
        row
        for row in payload.get("misc_action_rows", [])
        if int_value(as_dict(row).get("action_id")) in {0x0010, 0x0011}
    ]
    intro_rows = [
        row
        for row in payload.get("misc_action_rows", [])
        if as_dict(row).get("scene_path") in {"link_info.zsi", "spot04_info.zsi"}
    ]
    lines = [
        "# Scene Cutscene Misc Action Table",
        "",
        "Entry-level decode of OOT3D-native `CS_CMD_MISC` (`command id 3`) records from original `.zsi` cutscene payloads.",
        "",
        "## Summary",
        "",
        f"- Scene root: `{summary.get('scene_root')}`",
        f"- `CS_CMD_MISC` commands: {summary.get('misc_command_count')}",
        f"- Misc action entries: {summary.get('misc_action_count')}",
        f"- Distinct action ids: {summary.get('distinct_action_id_count')}",
        f"- Quake start entries: {summary.get('quake_start_count')}",
        f"- Quake stop entries: {summary.get('quake_stop_count')}",
        f"- Intro target misc entries (`link_info.zsi`, `spot04_info.zsi`): {summary.get('intro_target_misc_action_count')}",
        f"- Status: `{'pass' if not errors else 'fail'}`",
        "",
        "## Native Runtime Evidence",
        "",
        "- `Cutscene_ProcessCommands` outer `case 3` reads counted 12-word entries and switches on entry `+0x00` as a misc action id.",
        "- Entry `+0x02` is the start frame and entry `+0x04` is the end frame; actions that check `bVar22` fire when `csCtx+0x20 == startFrame`.",
        "- Action `0x0010` starts camera quake through `Quake_Add`, stores the returned request id at `csCtx+0x30`, then calls `Quake_SetSpeed`, `Quake_SetQuakeValues`, and `Quake_SetCountdown`.",
        "- Action `0x0011` stops that quake through `Quake_RemoveFromIdx(csCtx+0x30)`.",
        "",
        "## Code Literals",
        "",
        "| address | value | meaning |",
        "| ---: | ---: | --- |",
    ]
    for row in payload.get("code_literal_rows", []):
        item = as_dict(row)
        lines.append(f"| `{item.get('address_hex')}` | `{item.get('value_hex')}` | {item.get('label')} |")
    lines.extend(
        [
            "",
            "## Quake Actions",
            "",
            "| action | scene | cutscene | setup | command | entry | start | end | semantic |",
            "| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |",
        ]
    )
    for row in quake_rows:
        item = as_dict(row)
        lines.append(
            f"| `{item.get('action_id_hex')}` | `{item.get('scene_path')}` | "
            f"{item.get('cutscene_source_index')} | {item.get('setup_index')} | "
            f"{item.get('native_command_source_index')} | {item.get('entry_index')} | "
            f"{item.get('start_frame')} | {item.get('end_frame')} | {item.get('runtime_semantic')} |"
        )
    lines.extend(
        [
            "",
            "## Intro Target Rows",
            "",
            "| scene | cutscene | setup | action | start | end | semantic |",
            "| --- | ---: | ---: | ---: | ---: | ---: | --- |",
        ]
    )
    for row in intro_rows:
        item = as_dict(row)
        lines.append(
            f"| `{item.get('scene_path')}` | {item.get('cutscene_source_index')} | "
            f"{item.get('setup_index')} | `{item.get('action_id_hex')}` | "
            f"{item.get('start_frame')} | {item.get('end_frame')} | {item.get('runtime_semantic')} |"
        )
    lines.extend(
        [
            "",
            "## Outputs",
            "",
            "- `analysis/scene_cutscene_misc_action_table.json`",
            "- `analysis/scene_cutscene_misc_action_table.csv`",
            "- `include/oot3d/scene_cutscene_misc_action_table.h`",
            "- `src/code/z_scene_cutscene_misc_action_table.c`",
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
        "#ifndef OOT3D_SCENE_CUTSCENE_MISC_ACTION_TABLE_H",
        "#define OOT3D_SCENE_CUTSCENE_MISC_ACTION_TABLE_H",
        "",
        '#include "oot3d/scene.h"',
        "",
        "enum {",
        f"    OOT3D_SCENE_CUTSCENE_MISC_ACTION_ROW_COUNT = {row_count},",
        "};",
        "",
        "typedef enum {",
        "    OOT3D_CUTSCENE_MISC_ACTION_START_LIGHT_MODE_BLEND_0_TO_1 = 0x0007,",
        "    OOT3D_CUTSCENE_MISC_ACTION_RAMP_53F0_SMALL = 0x0008,",
        "    OOT3D_CUTSCENE_MISC_ACTION_SET_3271_TO_10 = 0x0009,",
        "    OOT3D_CUTSCENE_MISC_ACTION_RAMP_53F0_TIMED_SFX = 0x000B,",
        "    OOT3D_CUTSCENE_MISC_ACTION_REQUEST_CUTSCENE_END = 0x000C,",
        "    OOT3D_CUTSCENE_MISC_ACTION_SET_CAMERA_DATA_INDEX_0 = 0x000E,",
        "    OOT3D_CUTSCENE_MISC_ACTION_START_CAMERA_QUAKE = 0x0010,",
        "    OOT3D_CUTSCENE_MISC_ACTION_STOP_CAMERA_QUAKE = 0x0011,",
        "    OOT3D_CUTSCENE_MISC_ACTION_SET_ENV_3 = 0x001E,",
        "    OOT3D_CUTSCENE_MISC_ACTION_SET_ENV_4 = 0x001F,",
        "} Oot3dCutsceneMiscActionId;",
        "",
        "typedef struct {",
        "    u16 miscActionSourceIndex;",
        "    u16 nativeCommandSourceIndex;",
        "    u16 cutsceneSourceIndex;",
        "    u16 setupIndex;",
        "    u16 localCommandIndex;",
        "    u16 entryIndex;",
        "    u32 commandOffset;",
        "    u32 entryOffset;",
        "    u16 actionId;",
        "    u16 startFrame;",
        "    u16 endFrame;",
        "    u16 durationFrames;",
        "    u8 startFrameTriggerConfirmed;",
        "    u8 activeWindowInclusiveStartExclusiveEnd;",
        "    const char* scenePath;",
        "    const char* actionName;",
        "    const char* runtimeSemantic;",
        "    const char* rawWordsText;",
        "} Oot3dSceneCutsceneMiscActionRow;",
        "",
        "extern const Oot3dSceneCutsceneMiscActionRow oot3d_scene_cutscene_misc_action_rows[];",
        "extern const u32 oot3d_scene_cutscene_misc_action_row_count;",
        "",
        "const Oot3dSceneCutsceneMiscActionRow* Oot3d_CutsceneMiscActionGetRow(u16 miscActionSourceIndex);",
        "",
        "u32 Oot3d_CutsceneMiscActionCollectActiveWindows(",
        "    u16 cutsceneSourceIndex,",
        "    s32 frame,",
        "    const Oot3dSceneCutsceneMiscActionRow** outRows,",
        "    u32 maxRows",
        ");",
        "",
        "u32 Oot3d_CutsceneMiscActionCollectStartTriggers(",
        "    u16 cutsceneSourceIndex,",
        "    s32 frame,",
        "    const Oot3dSceneCutsceneMiscActionRow** outRows,",
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
        "/* Generated by build_scene_cutscene_misc_action_table.py. */",
        "",
        '#include "oot3d/scene_cutscene_misc_action_table.h"',
        "",
        "#include <stddef.h>",
        "",
        "const Oot3dSceneCutsceneMiscActionRow oot3d_scene_cutscene_misc_action_rows[] = {",
    ]
    for row in rows:
        lines.append(
            "    { "
            f"{c_u16(row.get('misc_action_source_index'))}, "
            f"{c_u16(row.get('native_command_source_index'))}, "
            f"{c_u16(row.get('cutscene_source_index'))}, "
            f"{c_u16(row.get('setup_index'))}, "
            f"{c_u16(row.get('local_command_index'))}, "
            f"{c_u16(row.get('entry_index'))}, "
            f"{c_u32(row.get('command_offset'))}, "
            f"{c_u32(row.get('entry_offset'))}, "
            f"{c_u16(row.get('action_id'))}, "
            f"{c_u16(row.get('start_frame'))}, "
            f"{c_u16(row.get('end_frame'))}, "
            f"{c_u16(row.get('duration_frames'))}, "
            f"{c_u8(row.get('start_frame_trigger_confirmed'))}, "
            f"{c_u8(row.get('active_window_inclusive_start_exclusive_end'))}, "
            f"{c_string(row.get('scene_path'))}, "
            f"{c_string(row.get('action_name'))}, "
            f"{c_string(row.get('runtime_semantic'))}, "
            f"{c_string(row.get('raw_words_text'))} "
            "},"
        )
    lines.extend(
        [
            "};",
            "",
            "const u32 oot3d_scene_cutscene_misc_action_row_count = OOT3D_SCENE_CUTSCENE_MISC_ACTION_ROW_COUNT;",
            "",
            "const Oot3dSceneCutsceneMiscActionRow* Oot3d_CutsceneMiscActionGetRow(u16 miscActionSourceIndex) {",
            "    if (miscActionSourceIndex >= oot3d_scene_cutscene_misc_action_row_count) {",
            "        return NULL;",
            "    }",
            "    return &oot3d_scene_cutscene_misc_action_rows[miscActionSourceIndex];",
            "}",
            "",
            "static void Oot3d_CutsceneMiscActionMaybeStoreRow(",
            "    const Oot3dSceneCutsceneMiscActionRow* row,",
            "    const Oot3dSceneCutsceneMiscActionRow** outRows,",
            "    u32 maxRows,",
            "    u32 count",
            ") {",
            "    if (outRows != NULL && count < maxRows) {",
            "        outRows[count] = row;",
            "    }",
            "}",
            "",
            "u32 Oot3d_CutsceneMiscActionCollectActiveWindows(",
            "    u16 cutsceneSourceIndex,",
            "    s32 frame,",
            "    const Oot3dSceneCutsceneMiscActionRow** outRows,",
            "    u32 maxRows",
            ") {",
            "    u32 rowIndex;",
            "    u32 count = 0;",
            "",
            "    for (rowIndex = 0; rowIndex < oot3d_scene_cutscene_misc_action_row_count; rowIndex++) {",
            "        const Oot3dSceneCutsceneMiscActionRow* row = &oot3d_scene_cutscene_misc_action_rows[rowIndex];",
            "",
            "        if (row->cutsceneSourceIndex != cutsceneSourceIndex) {",
            "            continue;",
            "        }",
            "        if (row->activeWindowInclusiveStartExclusiveEnd == 0) {",
            "            continue;",
            "        }",
            "        if ((s32)row->startFrame <= frame && frame < (s32)row->endFrame) {",
            "            Oot3d_CutsceneMiscActionMaybeStoreRow(row, outRows, maxRows, count);",
            "            count++;",
            "        }",
            "    }",
            "",
            "    return count;",
            "}",
            "",
            "u32 Oot3d_CutsceneMiscActionCollectStartTriggers(",
            "    u16 cutsceneSourceIndex,",
            "    s32 frame,",
            "    const Oot3dSceneCutsceneMiscActionRow** outRows,",
            "    u32 maxRows",
            ") {",
            "    u32 rowIndex;",
            "    u32 count = 0;",
            "",
            "    for (rowIndex = 0; rowIndex < oot3d_scene_cutscene_misc_action_row_count; rowIndex++) {",
            "        const Oot3dSceneCutsceneMiscActionRow* row = &oot3d_scene_cutscene_misc_action_rows[rowIndex];",
            "",
            "        if (row->cutsceneSourceIndex != cutsceneSourceIndex) {",
            "            continue;",
            "        }",
            "        if (row->startFrameTriggerConfirmed == 0) {",
            "            continue;",
            "        }",
            "        if ((s32)row->startFrame == frame) {",
            "            Oot3d_CutsceneMiscActionMaybeStoreRow(row, outRows, maxRows, count);",
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
    misc_rows, decode_errors = decode_misc_actions(command_rows, scene_root)
    literal_rows, literal_errors = verify_codebin_literals(DEFAULT_CODE_BIN)
    errors.extend(decode_errors)
    errors.extend(literal_errors)
    errors.extend(verify_decompile_patterns(DEFAULT_CUTSCENE_PROCESS_DECOMPILE))
    errors.extend(verify_helper_export(DEFAULT_MISC_HELPER_EXPORT_DIR))

    action_counts = Counter(row["action_id_hex"] for row in misc_rows)
    scene_counts = Counter(row["scene_path"] for row in misc_rows)
    misc_command_count = sum(
        1
        for row in command_rows
        if int_value(row.get("command_id_hex")) == CS_CMD_MISC
        and row.get("category") == "counted_12word_entries"
    )
    summary = {
        "format": "oot3d_scene_cutscene_misc_action_table_v1",
        "scene_root": str(scene_root),
        "source_native_decode_table": str(DEFAULT_NATIVE_DECODE_TABLE),
        "source_native_command_table": str(DEFAULT_NATIVE_COMMAND_CSV),
        "runtime_reference": f"Cutscene_ProcessCommands 0x{CUTSCENE_PROCESS_COMMANDS:08X}",
        "misc_command_count": misc_command_count,
        "misc_action_count": len(misc_rows),
        "distinct_action_id_count": len(action_counts),
        "action_id_counts": dict(sorted(action_counts.items())),
        "scene_counts": dict(sorted(scene_counts.items())),
        "quake_start_count": action_counts.get("0x0010", 0),
        "quake_stop_count": action_counts.get("0x0011", 0),
        "intro_target_misc_action_count": sum(
            1 for row in misc_rows if row["scene_path"] in {"link_info.zsi", "spot04_info.zsi"}
        ),
    }
    payload = {
        "summary": summary,
        "code_literal_rows": literal_rows,
        "misc_action_rows": misc_rows,
    }
    return payload, errors


def main() -> int:
    payload, errors = build_payload()
    write_json(DEFAULT_OUT_JSON, payload)
    write_csv(DEFAULT_OUT_CSV, payload["misc_action_rows"])
    write_markdown(DEFAULT_OUT_MD, payload, errors)
    write_header(DEFAULT_OUT_HEADER, len(payload["misc_action_rows"]))
    write_source(DEFAULT_OUT_SOURCE, payload["misc_action_rows"])
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1
    summary = as_dict(payload.get("summary"))
    print(
        "wrote scene cutscene misc action table: "
        f"{summary.get('misc_action_count')} entries, "
        f"{summary.get('quake_start_count')} quake starts, "
        f"{summary.get('quake_stop_count')} quake stops"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
