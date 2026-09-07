#!/usr/bin/env python3
"""Build an OOT3D-native table for CS_CMD_SET_PLAYER_ACTION cue records."""

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
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_cutscene_player_action_table.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "scene_cutscene_player_action_table.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_cutscene_player_action_table.md"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "scene_cutscene_player_action_table.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_scene_cutscene_player_action_table.c"

CS_CMD_SET_PLAYER_ACTION = 0x0000000A
PLAYER_ACTION_ENTRY_SIZE = 0x30
CUTSCENE_PROCESS_COMMANDS = 0x002C5BA0


PLAYER_ACTION_NAMES = {
    0x0005: "OOT3D_PLAYER_CUTSCENE_ACTION_0005",
    0x001C: "OOT3D_PLAYER_CUTSCENE_ACTION_001C",
    0x001D: "OOT3D_PLAYER_CUTSCENE_ACTION_001D",
    0x001E: "OOT3D_PLAYER_CUTSCENE_ACTION_001E",
    0x001F: "OOT3D_PLAYER_CUTSCENE_ACTION_001F",
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


def decode_action_name(action_id: int) -> str:
    return PLAYER_ACTION_NAMES.get(action_id, f"OOT3D_PLAYER_CUTSCENE_ACTION_{action_id:04X}")


def decode_runtime_semantic(action_id: int) -> str:
    return (
        "native_player_action_cue: Cutscene_ProcessCommands command 10 writes this 12-word "
        "entry pointer to csCtx+0x40 when startFrame < csFrame <= endFrame; raw payload words "
        "are preserved pending exact consumer promotion"
    )


def decode_player_actions(
    command_rows: list[dict[str, str]],
    scene_root: Path,
) -> tuple[list[dict[str, Any]], list[str]]:
    rows: list[dict[str, Any]] = []
    decode_errors: list[str] = []
    scene_cache: dict[str, bytes] = {}

    for command in command_rows:
        command_id = int_value(command.get("command_id_hex"))
        category = command.get("category", "")
        if command_id != CS_CMD_SET_PLAYER_ACTION:
            continue
        if category != "counted_12word_entries":
            decode_errors.append(
                f"command {command.get('native_command_source_index')}: "
                f"CS_CMD_SET_PLAYER_ACTION category is {category}"
            )
            continue

        scene_path = str(command.get("scene_path", ""))
        command_offset = int_value(command.get("command_offset_hex"))
        entry_count = int_value(command.get("entry_count"))
        try:
            data = load_scene_bytes(scene_root, scene_path, scene_cache)
        except FileNotFoundError:
            decode_errors.append(f"missing scene file for {scene_path}")
            continue
        if command_offset < 0 or command_offset + 8 > len(data):
            decode_errors.append(
                f"command {command.get('native_command_source_index')}: command header outside {scene_path}"
            )
            continue

        native_command_id = u32(data, command_offset)
        native_entry_count = u32(data, command_offset + 4)
        expected_size = 8 + entry_count * PLAYER_ACTION_ENTRY_SIZE
        if native_command_id != CS_CMD_SET_PLAYER_ACTION:
            decode_errors.append(
                f"command {command.get('native_command_source_index')}: native id 0x{native_command_id:08X}"
            )
        if native_entry_count != entry_count:
            decode_errors.append(
                f"command {command.get('native_command_source_index')}: "
                f"entry count {native_entry_count} != {entry_count}"
            )
        if command_offset + expected_size > len(data):
            decode_errors.append(
                f"command {command.get('native_command_source_index')}: entries outside {scene_path}"
            )
            continue

        for entry_index in range(entry_count):
            entry_offset = command_offset + 8 + entry_index * PLAYER_ACTION_ENTRY_SIZE
            raw = data[entry_offset : entry_offset + PLAYER_ACTION_ENTRY_SIZE]
            raw_words = [u32(raw, word_index * 4) for word_index in range(12)]
            action_id = u16(raw, 0)
            start_frame = u16(raw, 2)
            end_frame = u16(raw, 4)
            unk_06 = u16(raw, 6)
            rows.append(
                {
                    "player_action_source_index": len(rows),
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
                    "unk_06": unk_06,
                    "active_window_start_exclusive_end_inclusive": 1,
                    "raw_words": [f"0x{word:08X}" for word in raw_words],
                    "raw_word_values": raw_words,
                    "raw_words_text": ";".join(f"0x{word:08X}" for word in raw_words),
                    "raw_hex": raw.hex(),
                }
            )

    return rows, decode_errors


def verify_decompile_patterns(path: Path) -> list[str]:
    if not path.exists():
        return [f"missing Cutscene_ProcessCommands decompile: {path}"]
    text = path.read_text(encoding="utf-8")
    patterns = [
        "case 10:",
        "*(uint **)(param_2 + 0x40) = puVar18;",
        "*(ushort *)((int)puVar18 + 2) < *(ushort *)(param_2 + 0x20)",
        "*(ushort *)(param_2 + 0x20) <= (ushort)puVar18[1]",
        "puVar18 = puVar18 + 0xc;",
    ]
    return [f"Cutscene_ProcessCommands missing pattern `{pattern}`" for pattern in patterns if pattern not in text]


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
        "player_action_source_index",
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
        "unk_06",
        "active_window_start_exclusive_end_inclusive",
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
    intro_rows = [
        as_dict(row)
        for row in payload.get("player_action_rows", [])
        if int_value(as_dict(row).get("cutscene_source_index")) in {27, 28, 29}
    ]
    lines = [
        "# Scene Cutscene Player Action Table",
        "",
        "Native decode of OOT3D `CS_CMD_SET_PLAYER_ACTION` (`command id 10`) records from original `.zsi` cutscene payloads.",
        "",
        "## Summary",
        "",
        f"- Scene root: `{summary.get('scene_root')}`",
        f"- `CS_CMD_SET_PLAYER_ACTION` commands: {summary.get('player_action_command_count')}",
        f"- Player action entries: {summary.get('player_action_count')}",
        f"- Distinct action ids: {summary.get('distinct_action_id_count')}",
        f"- Intro player action entries (`cutscene_source_index` 27/28/29): {summary.get('intro_player_action_count')}",
        f"- Status: `{'pass' if not errors else 'fail'}`",
        "",
        "## Native Runtime Evidence",
        "",
        f"- `Cutscene_ProcessCommands` at `0x{CUTSCENE_PROCESS_COMMANDS:08X}` outer `case 10` reads counted 12-word records.",
        "- Entry `+0x00` is the player action id, entry `+0x02` is start frame, entry `+0x04` is end frame.",
        "- The native active-pointer gate is `startFrame < csFrame <= endFrame`, storing the record pointer at `csCtx+0x40`.",
        "- Direct player/event dispatch is a separate fixed command (`0x3E8`) and is exported by `scene_cutscene_direct_player_event_table`.",
        "- The remaining words are exported as native raw payload until their exact cue-consumer semantics are promoted from code.bin.",
        "",
        "## Intro Rows",
        "",
        "| cutscene | setup | command | entry | action | start | end | raw words |",
        "| ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |",
    ]
    for row in intro_rows:
        lines.append(
            f"| {row.get('cutscene_source_index')} | {row.get('setup_index')} | "
            f"{row.get('native_command_source_index')} | {row.get('entry_index')} | "
            f"`{row.get('action_id_hex')}` | {row.get('start_frame')} | "
            f"{row.get('end_frame')} | `{row.get('raw_words_text')}` |"
        )
    lines.extend(
        [
            "",
            "## Outputs",
            "",
            "- `analysis/scene_cutscene_player_action_table.json`",
            "- `analysis/scene_cutscene_player_action_table.csv`",
            "- `include/oot3d/scene_cutscene_player_action_table.h`",
            "- `src/code/z_scene_cutscene_player_action_table.c`",
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
        "#ifndef OOT3D_SCENE_CUTSCENE_PLAYER_ACTION_TABLE_H",
        "#define OOT3D_SCENE_CUTSCENE_PLAYER_ACTION_TABLE_H",
        "",
        '#include "oot3d/scene.h"',
        "",
        "enum {",
        f"    OOT3D_SCENE_CUTSCENE_PLAYER_ACTION_ROW_COUNT = {row_count},",
        "};",
        "",
        "typedef enum {",
        "    OOT3D_CUTSCENE_PLAYER_ACTION_ID_0005 = 0x0005,",
        "    OOT3D_CUTSCENE_PLAYER_ACTION_ID_001C = 0x001C,",
        "    OOT3D_CUTSCENE_PLAYER_ACTION_ID_001D = 0x001D,",
        "    OOT3D_CUTSCENE_PLAYER_ACTION_ID_001E = 0x001E,",
        "    OOT3D_CUTSCENE_PLAYER_ACTION_ID_001F = 0x001F,",
        "} Oot3dCutscenePlayerActionId;",
        "",
        "typedef struct {",
        "    u16 playerActionSourceIndex;",
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
        "    u16 unk06;",
        "    u8 activeWindowStartExclusiveEndInclusive;",
        "    u32 rawWords[12];",
        "    const char* scenePath;",
        "    const char* actionName;",
        "    const char* runtimeSemantic;",
        "    const char* rawWordsText;",
        "} Oot3dSceneCutscenePlayerActionRow;",
        "",
        "extern const Oot3dSceneCutscenePlayerActionRow oot3d_scene_cutscene_player_action_rows[];",
        "extern const u32 oot3d_scene_cutscene_player_action_row_count;",
        "",
        "const Oot3dSceneCutscenePlayerActionRow* Oot3d_CutscenePlayerActionGetRow(",
        "    u16 playerActionSourceIndex",
        ");",
        "",
        "u32 Oot3d_CutscenePlayerActionCollectActiveWindows(",
        "    u16 cutsceneSourceIndex,",
        "    s32 frame,",
        "    const Oot3dSceneCutscenePlayerActionRow** outRows,",
        "    u32 maxRows",
        ");",
        "",
        "u32 Oot3d_CutscenePlayerActionCollectStartTriggers(",
        "    u16 cutsceneSourceIndex,",
        "    s32 frame,",
        "    const Oot3dSceneCutscenePlayerActionRow** outRows,",
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
        "/* Generated by build_scene_cutscene_player_action_table.py. */",
        "",
        '#include "oot3d/scene_cutscene_player_action_table.h"',
        "",
        "#include <stddef.h>",
        "",
        "const Oot3dSceneCutscenePlayerActionRow oot3d_scene_cutscene_player_action_rows[] = {",
    ]
    for row in rows:
        raw_values = [int_value(value) for value in row.get("raw_word_values", [])]
        raw_initializer = "{ " + ", ".join(c_u32(value) for value in raw_values) + " }"
        lines.append(
            "    { "
            f"{c_u16(row.get('player_action_source_index'))}, "
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
            f"{c_u16(row.get('unk_06'))}, "
            f"{c_u8(row.get('active_window_start_exclusive_end_inclusive'))}, "
            f"{raw_initializer}, "
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
            "const u32 oot3d_scene_cutscene_player_action_row_count = OOT3D_SCENE_CUTSCENE_PLAYER_ACTION_ROW_COUNT;",
            "",
            "const Oot3dSceneCutscenePlayerActionRow* Oot3d_CutscenePlayerActionGetRow(",
            "    u16 playerActionSourceIndex",
            ") {",
            "    if (playerActionSourceIndex >= oot3d_scene_cutscene_player_action_row_count) {",
            "        return NULL;",
            "    }",
            "    return &oot3d_scene_cutscene_player_action_rows[playerActionSourceIndex];",
            "}",
            "",
            "static void Oot3d_CutscenePlayerActionMaybeStoreRow(",
            "    const Oot3dSceneCutscenePlayerActionRow* row,",
            "    const Oot3dSceneCutscenePlayerActionRow** outRows,",
            "    u32 maxRows,",
            "    u32 count",
            ") {",
            "    if (outRows != NULL && count < maxRows) {",
            "        outRows[count] = row;",
            "    }",
            "}",
            "",
            "u32 Oot3d_CutscenePlayerActionCollectActiveWindows(",
            "    u16 cutsceneSourceIndex,",
            "    s32 frame,",
            "    const Oot3dSceneCutscenePlayerActionRow** outRows,",
            "    u32 maxRows",
            ") {",
            "    u32 rowIndex;",
            "    u32 count = 0;",
            "",
            "    for (rowIndex = 0; rowIndex < oot3d_scene_cutscene_player_action_row_count; rowIndex++) {",
            "        const Oot3dSceneCutscenePlayerActionRow* row = &oot3d_scene_cutscene_player_action_rows[rowIndex];",
            "",
            "        if (row->cutsceneSourceIndex != cutsceneSourceIndex) {",
            "            continue;",
            "        }",
            "        if (row->activeWindowStartExclusiveEndInclusive == 0) {",
            "            continue;",
            "        }",
            "        if ((s32)row->startFrame < frame && frame <= (s32)row->endFrame) {",
            "            Oot3d_CutscenePlayerActionMaybeStoreRow(row, outRows, maxRows, count);",
            "            count++;",
            "        }",
            "    }",
            "",
            "    return count;",
            "}",
            "",
            "u32 Oot3d_CutscenePlayerActionCollectStartTriggers(",
            "    u16 cutsceneSourceIndex,",
            "    s32 frame,",
            "    const Oot3dSceneCutscenePlayerActionRow** outRows,",
            "    u32 maxRows",
            ") {",
            "    u32 rowIndex;",
            "    u32 count = 0;",
            "",
            "    for (rowIndex = 0; rowIndex < oot3d_scene_cutscene_player_action_row_count; rowIndex++) {",
            "        const Oot3dSceneCutscenePlayerActionRow* row = &oot3d_scene_cutscene_player_action_rows[rowIndex];",
            "",
            "        if (row->cutsceneSourceIndex != cutsceneSourceIndex) {",
            "            continue;",
            "        }",
            "        if ((s32)row->startFrame == frame) {",
            "            Oot3d_CutscenePlayerActionMaybeStoreRow(row, outRows, maxRows, count);",
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
    player_action_rows, decode_errors = decode_player_actions(command_rows, scene_root)
    errors.extend(decode_errors)
    errors.extend(verify_decompile_patterns(DEFAULT_CUTSCENE_PROCESS_DECOMPILE))

    action_counts = Counter(row["action_id_hex"] for row in player_action_rows)
    scene_counts = Counter(row["scene_path"] for row in player_action_rows)
    player_action_command_count = sum(
        1
        for row in command_rows
        if int_value(row.get("command_id_hex")) == CS_CMD_SET_PLAYER_ACTION
        and row.get("category") == "counted_12word_entries"
    )
    summary = {
        "format": "oot3d_scene_cutscene_player_action_table_v1",
        "scene_root": str(scene_root),
        "source_native_decode_table": str(DEFAULT_NATIVE_DECODE_TABLE),
        "source_native_command_table": str(DEFAULT_NATIVE_COMMAND_CSV),
        "runtime_reference": f"Cutscene_ProcessCommands 0x{CUTSCENE_PROCESS_COMMANDS:08X}",
        "player_action_command_count": player_action_command_count,
        "player_action_count": len(player_action_rows),
        "distinct_action_id_count": len(action_counts),
        "action_id_counts": dict(sorted(action_counts.items())),
        "scene_counts": dict(sorted(scene_counts.items())),
        "intro_player_action_count": sum(
            1 for row in player_action_rows if row["cutscene_source_index"] in {27, 28, 29}
        ),
        "active_pointer_gate": "startFrame < csFrame <= endFrame",
        "direct_dispatch_channel": "CS command 0x3E8 is exported separately by scene_cutscene_direct_player_event_table",
        "raw_payload_policy": "preserve all 12 native words until exact cue-consumer field semantics are promoted",
    }
    payload = {
        "summary": summary,
        "player_action_rows": player_action_rows,
    }
    return payload, errors


def main() -> int:
    payload, errors = build_payload()
    write_json(DEFAULT_OUT_JSON, payload)
    write_csv(DEFAULT_OUT_CSV, payload["player_action_rows"])
    write_markdown(DEFAULT_OUT_MD, payload, errors)
    write_header(DEFAULT_OUT_HEADER, len(payload["player_action_rows"]))
    write_source(DEFAULT_OUT_SOURCE, payload["player_action_rows"])
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1
    summary = as_dict(payload.get("summary"))
    print(
        "wrote scene cutscene player action table: "
        f"{summary.get('player_action_count')} entries, "
        f"{summary.get('intro_player_action_count')} intro entries"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
