#!/usr/bin/env python3
"""Build an OOT3D-native table for CS_CMD_SETTIME records."""

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
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_cutscene_set_time_table.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "scene_cutscene_set_time_table.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_cutscene_set_time_table.md"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "scene_cutscene_set_time_table.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_scene_cutscene_set_time_table.c"

CS_CMD_SETTIME = 0x0000008C
SET_TIME_ENTRY_SIZE = 0x0C
CUTSCENE_PROCESS_COMMANDS = 0x002C5BA0


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


def decode_day_time(hour: int, minute: int) -> int:
    # Mirrors OOT3D Cutscene_ProcessCommands case 0x8c: hour term plus (minute + 1).
    hour_term = int(float(hour) * 60.0 * (0x4000 / 360.0))
    minute_term = int(float((minute & 0xFF) + 1) * (0x4000 / 360.0))
    return (hour_term + minute_term) & 0xFFFF


def decode_set_time_rows(
    command_rows: list[dict[str, str]],
    scene_root: Path,
) -> tuple[list[dict[str, Any]], list[str]]:
    rows: list[dict[str, Any]] = []
    errors: list[str] = []
    scene_cache: dict[str, bytes] = {}

    for command in command_rows:
        command_id = int_value(command.get("command_id_hex"))
        category = command.get("category", "")
        if command_id != CS_CMD_SETTIME:
            continue
        if category != "counted_3word_entries":
            errors.append(
                f"command {command.get('native_command_source_index')}: "
                f"CS_CMD_SETTIME category is {category}"
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
        expected_size = 8 + entry_count * SET_TIME_ENTRY_SIZE
        if native_command_id != CS_CMD_SETTIME:
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
            entry_offset = command_offset + 8 + entry_index * SET_TIME_ENTRY_SIZE
            raw = data[entry_offset : entry_offset + SET_TIME_ENTRY_SIZE]
            unk_00 = u16(raw, 0)
            start_frame = u16(raw, 2)
            end_frame = u16(raw, 4)
            hour = raw[6]
            minute = raw[7]
            unused = u32(raw, 8)
            raw_words = [u32(raw, word_index * 4) for word_index in range(3)]
            day_time = decode_day_time(hour, minute)
            rows.append(
                {
                    "set_time_source_index": len(rows),
                    "native_command_source_index": int_value(
                        command.get("native_command_source_index")
                    ),
                    "cutscene_source_index": int_value(command.get("cutscene_source_index")),
                    "scene_path": scene_path,
                    "setup_index": int_value(command.get("setup_index")),
                    "local_command_index": int_value(command.get("local_command_index")),
                    "entry_index": entry_index,
                    "command_offset": command_offset,
                    "command_offset_hex": f"0x{command_offset:08X}",
                    "entry_offset": entry_offset,
                    "entry_offset_hex": f"0x{entry_offset:08X}",
                    "unk_00": unk_00,
                    "start_frame": start_frame,
                    "end_frame": end_frame,
                    "hour": hour,
                    "minute": minute,
                    "minute_plus_one": minute + 1,
                    "unused": unused,
                    "day_time": day_time,
                    "skybox_time": day_time,
                    "raw_words": raw_words,
                    "raw_words_text": ";".join(f"0x{word:08X}" for word in raw_words),
                    "runtime_semantic": (
                        "native_set_time: Cutscene_ProcessCommands command 0x8C triggers "
                        "when csCtx frame equals entry.startFrame, converts hour and "
                        "(minute + 1) to OOT time units, then writes both dayTime and skyboxTime"
                    ),
                }
            )

    return rows, errors


def verify_decompile_patterns(path: Path) -> list[str]:
    if not path.exists():
        return [f"missing Cutscene_ProcessCommands decompile: {path}"]
    text = path.read_text(encoding="utf-8")
    patterns = [
        "case 0x8c:",
        "*(ushort *)(param_2 + 0x20) == *(ushort *)((int)puVar18 + 2)",
        "*(byte *)((int)puVar18 + 6)",
        "*(byte *)((int)puVar18 + 7) + 1",
        "*(short *)(piVar3 + 3) = sVar13",
        "*(short *)(local_34 + 0xa8) = sVar13",
    ]
    return [
        f"Cutscene_ProcessCommands set-time evidence: missing pattern `{pattern}`"
        for pattern in patterns
        if pattern not in text
    ]


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = [
        "set_time_source_index",
        "native_command_source_index",
        "cutscene_source_index",
        "scene_path",
        "setup_index",
        "local_command_index",
        "entry_index",
        "command_offset_hex",
        "entry_offset_hex",
        "unk_00",
        "start_frame",
        "end_frame",
        "hour",
        "minute",
        "minute_plus_one",
        "unused",
        "day_time",
        "skybox_time",
        "raw_words_text",
        "runtime_semantic",
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fields})


def write_markdown(path: Path, payload: dict[str, Any], errors: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    summary = as_dict(payload.get("summary"))
    lines = [
        "# Scene Cutscene Set-Time Table",
        "",
        "## Summary",
        "",
        f"- Set-time command count: `{summary.get('set_time_command_count')}`",
        f"- Set-time entry count: `{summary.get('set_time_entry_count')}`",
        f"- Distinct time values: `{summary.get('distinct_day_time_count')}`",
        f"- Runtime reference: `{summary.get('runtime_reference')}`",
        "",
        "## Rows",
        "",
        "| row | scene | cutscene | cmd | entry | start | end | time | dayTime | raw |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | --- | ---: | --- |",
    ]
    for row in payload.get("set_time_rows", []):
        lines.append(
            f"| {row['set_time_source_index']} | `{row['scene_path']}` | "
            f"{row['cutscene_source_index']} | {row['native_command_source_index']} | "
            f"{row['entry_index']} | {row['start_frame']} | {row['end_frame']} | "
            f"{row['hour']:02d}:{row['minute']:02d} | {row['day_time']} | "
            f"`{row['raw_words_text']}` |"
        )
    lines.extend(["", "## Validation", ""])
    if errors:
        lines.extend(f"- ERROR: {error}" for error in errors)
    else:
        lines.append("- OK: OOT3D native bytes and Cutscene_ProcessCommands case `0x8c` evidence matched.")
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_header(path: Path, row_count: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "#ifndef OOT3D_SCENE_CUTSCENE_SET_TIME_TABLE_H",
        "#define OOT3D_SCENE_CUTSCENE_SET_TIME_TABLE_H",
        "",
        '#include "oot3d/scene.h"',
        "",
        "enum {",
        f"    OOT3D_SCENE_CUTSCENE_SET_TIME_ROW_COUNT = {row_count},",
        "};",
        "",
        "typedef struct {",
        "    u16 setTimeSourceIndex;",
        "    u16 nativeCommandSourceIndex;",
        "    u16 cutsceneSourceIndex;",
        "    u16 setupIndex;",
        "    u16 localCommandIndex;",
        "    u16 entryIndex;",
        "    u32 commandOffset;",
        "    u32 entryOffset;",
        "    u16 unk00;",
        "    u16 startFrame;",
        "    u16 endFrame;",
        "    u8 hour;",
        "    u8 minute;",
        "    u16 minutePlusOne;",
        "    u32 unused;",
        "    u16 dayTime;",
        "    u16 skyboxTime;",
        "    u32 rawWords[3];",
        "    const char* scenePath;",
        "    const char* runtimeSemantic;",
        "    const char* rawWordsText;",
        "} Oot3dSceneCutsceneSetTimeRow;",
        "",
        "extern const Oot3dSceneCutsceneSetTimeRow oot3d_scene_cutscene_set_time_rows[];",
        "extern const u32 oot3d_scene_cutscene_set_time_row_count;",
        "",
        "u16 Oot3d_CutsceneSetTimeDecodeDayTime(u8 hour, u8 minute);",
        "",
        "const Oot3dSceneCutsceneSetTimeRow* Oot3d_CutsceneSetTimeGetRow(",
        "    u16 setTimeSourceIndex",
        ");",
        "",
        "u32 Oot3d_CutsceneSetTimeCollectStartTriggers(",
        "    u16 cutsceneSourceIndex,",
        "    s32 frame,",
        "    const Oot3dSceneCutsceneSetTimeRow** outRows,",
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
        "/* Generated by build_scene_cutscene_set_time_table.py. */",
        "",
        '#include "oot3d/scene_cutscene_set_time_table.h"',
        "",
        "#include <stddef.h>",
        "",
        "const Oot3dSceneCutsceneSetTimeRow oot3d_scene_cutscene_set_time_rows[] = {",
    ]
    for row in rows:
        raw_words = ", ".join(c_u32(word) for word in row.get("raw_words", []))
        lines.append(
            "    { "
            f"{c_u16(row.get('set_time_source_index'))}, "
            f"{c_u16(row.get('native_command_source_index'))}, "
            f"{c_u16(row.get('cutscene_source_index'))}, "
            f"{c_u16(row.get('setup_index'))}, "
            f"{c_u16(row.get('local_command_index'))}, "
            f"{c_u16(row.get('entry_index'))}, "
            f"{c_u32(row.get('command_offset'))}, "
            f"{c_u32(row.get('entry_offset'))}, "
            f"{c_u16(row.get('unk_00'))}, "
            f"{c_u16(row.get('start_frame'))}, "
            f"{c_u16(row.get('end_frame'))}, "
            f"{c_u8(row.get('hour'))}, "
            f"{c_u8(row.get('minute'))}, "
            f"{c_u16(row.get('minute_plus_one'))}, "
            f"{c_u32(row.get('unused'))}, "
            f"{c_u16(row.get('day_time'))}, "
            f"{c_u16(row.get('skybox_time'))}, "
            f"{{ {raw_words} }}, "
            f"{c_string(row.get('scene_path'))}, "
            f"{c_string(row.get('runtime_semantic'))}, "
            f"{c_string(row.get('raw_words_text'))} "
            "},"
        )
    lines.extend(
        [
            "};",
            "",
            "const u32 oot3d_scene_cutscene_set_time_row_count = OOT3D_SCENE_CUTSCENE_SET_TIME_ROW_COUNT;",
            "",
            "u16 Oot3d_CutsceneSetTimeDecodeDayTime(u8 hour, u8 minute) {",
            "    u16 hourTerm = (u16)((float)hour * 60.0f * (16384.0f / 360.0f));",
            "    u16 minuteTerm = (u16)((float)(minute + 1u) * (16384.0f / 360.0f));",
            "    return (u16)(hourTerm + minuteTerm);",
            "}",
            "",
            "const Oot3dSceneCutsceneSetTimeRow* Oot3d_CutsceneSetTimeGetRow(",
            "    u16 setTimeSourceIndex",
            ") {",
            "    if (setTimeSourceIndex >= oot3d_scene_cutscene_set_time_row_count) {",
            "        return NULL;",
            "    }",
            "    return &oot3d_scene_cutscene_set_time_rows[setTimeSourceIndex];",
            "}",
            "",
            "static void Oot3d_CutsceneSetTimeMaybeStoreRow(",
            "    const Oot3dSceneCutsceneSetTimeRow* row,",
            "    const Oot3dSceneCutsceneSetTimeRow** outRows,",
            "    u32 maxRows,",
            "    u32 count",
            ") {",
            "    if (outRows != NULL && count < maxRows) {",
            "        outRows[count] = row;",
            "    }",
            "}",
            "",
            "u32 Oot3d_CutsceneSetTimeCollectStartTriggers(",
            "    u16 cutsceneSourceIndex,",
            "    s32 frame,",
            "    const Oot3dSceneCutsceneSetTimeRow** outRows,",
            "    u32 maxRows",
            ") {",
            "    u32 rowIndex;",
            "    u32 count = 0;",
            "",
            "    for (rowIndex = 0; rowIndex < oot3d_scene_cutscene_set_time_row_count; rowIndex++) {",
            "        const Oot3dSceneCutsceneSetTimeRow* row = &oot3d_scene_cutscene_set_time_rows[rowIndex];",
            "",
            "        if (row->cutsceneSourceIndex != cutsceneSourceIndex) {",
            "            continue;",
            "        }",
            "        if ((s32)row->startFrame == frame) {",
            "            Oot3d_CutsceneSetTimeMaybeStoreRow(row, outRows, maxRows, count);",
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
    set_time_rows, decode_errors = decode_set_time_rows(command_rows, scene_root)
    errors.extend(decode_errors)
    errors.extend(verify_decompile_patterns(DEFAULT_CUTSCENE_PROCESS_DECOMPILE))
    time_counts = Counter(row["day_time"] for row in set_time_rows)
    command_count = sum(
        1
        for row in command_rows
        if int_value(row.get("command_id_hex")) == CS_CMD_SETTIME
        and row.get("category") == "counted_3word_entries"
    )
    summary = {
        "format": "oot3d_scene_cutscene_set_time_table_v1",
        "scene_root": str(scene_root),
        "source_native_decode_table": str(DEFAULT_NATIVE_DECODE_TABLE),
        "source_native_command_table": str(DEFAULT_NATIVE_COMMAND_CSV),
        "runtime_reference": f"Cutscene_ProcessCommands 0x{CUTSCENE_PROCESS_COMMANDS:08X} case 0x8C",
        "set_time_command_count": command_count,
        "set_time_entry_count": len(set_time_rows),
        "distinct_day_time_count": len(time_counts),
        "day_time_counts": dict(sorted((str(key), value) for key, value in time_counts.items())),
    }
    payload = {
        "summary": summary,
        "set_time_rows": set_time_rows,
    }
    return payload, errors


def main() -> int:
    payload, errors = build_payload()
    write_json(DEFAULT_OUT_JSON, payload)
    write_csv(DEFAULT_OUT_CSV, payload["set_time_rows"])
    write_markdown(DEFAULT_OUT_MD, payload, errors)
    write_header(DEFAULT_OUT_HEADER, len(payload["set_time_rows"]))
    write_source(DEFAULT_OUT_SOURCE, payload["set_time_rows"])
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1
    summary = as_dict(payload.get("summary"))
    print(
        "wrote scene cutscene set-time table: "
        f"{summary.get('set_time_entry_count')} entries from "
        f"{summary.get('set_time_command_count')} commands"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
