#!/usr/bin/env python3
"""Build the OOT3D open-title player-action motion consumer audit.

This report promotes the native 0x001A35CC consumer that reads the active
CS_CMD_SET_PLAYER_ACTION record from csCtx+0x40 through the player update path.
The important distinction for the opening intro is that action ids 0x40/0x41
are not direct player states: the record pointer is still consumed for its
motion-vector fields at offsets +0x28/+0x2C.
"""

from __future__ import annotations

import csv
import json
import math
import struct
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"

ORCHESTRATION = ANALYSIS / "title_intro_opening_orchestration.json"
EXPORT = ANALYSIS / "title_intro_player_action_motion_consumer_ghidra_export"
DECOMPILED = EXPORT / "decompiled"
FUNCTIONS_CSV = EXPORT / "functions_selected.csv"
DISASSEMBLY = EXPORT / "disassembly_selected.txt"
MOTION_CONSUMER_C = DECOMPILED / "99000_001a35cc_FUN_001a35cc.c"
PLAYER_ACTION_C = DECOMPILED / "99001_00250ad0_oot3d_player_action_turn_in_place.c"
ATAN2_C = DECOMPILED / "99002_003758b0_Math_Atan2S.c"
DIRECT_STATE_SWITCH_C = (
    ANALYSIS
    / "title_intro_player_action_consumer_ghidra_export"
    / "decompiled"
    / "99086_00473ef8_oot3d_player_action_swing_bottle.c"
)
CODE_BIN = Path(r"E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin")

OUT_JSON = ANALYSIS / "title_intro_opening_player_motion_audit.json"
OUT_MD = ANALYSIS / "title_intro_opening_player_motion_audit.md"
OUT_CSV = ANALYSIS / "title_intro_opening_player_motion_vectors.csv"
OUT_HEADER = ROOT / "include" / "oot3d" / "title_intro_opening_player_motion.h"
OUT_SOURCE = ROOT / "src" / "code" / "z_title_intro_opening_player_motion.c"
MATH_HEADER = ROOT / "include" / "oot3d" / "math.h"
MATH_SOURCE = ROOT / "src" / "code" / "z_oot3d_math.c"
MATH_TABLE_CSV = ANALYSIS / "oot3d_math_atan2_table.csv"

CODE_BASE = 0x00100000
MOTION_CONSUMER_ENTRY = 0x001A35CC
MOTION_CONSUMER_CLAMP_BITS_ADDR = 0x001A3630
MOTION_CONSUMER_CLAMP_VALUE_ADDR = 0x001A3634
MOTION_CONSUMER_VECTOR_X_OFFSET = 0x28
MOTION_CONSUMER_VECTOR_Z_OFFSET = 0x2C
MOTION_CONSUMER_VECTOR_X_WORD_INDEX = MOTION_CONSUMER_VECTOR_X_OFFSET // 4
MOTION_CONSUMER_VECTOR_Z_WORD_INDEX = MOTION_CONSUMER_VECTOR_Z_OFFSET // 4
PLAYER_ACTION_RECORD_WORD_COUNT = 12

REQUIRED_FUNCTIONS = {
    "001a35cc": "player_action_motion_consumer",
    "00250ad0": "oot3d_player_action_turn_in_place",
    "003758b0": "Math_Atan2S",
}


def rel(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def read_text(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


def read_math_table() -> list[int]:
    rows = read_csv(MATH_TABLE_CSV)
    if not rows:
        return []
    return [int(row["value"]) for row in rows]


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def write_json(path: Path, data: Any) -> None:
    write_text(path, json.dumps(data, indent=2) + "\n")


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        if not rows:
            return
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def find_line(path: Path, needle: str) -> dict[str, Any]:
    for line_number, line in enumerate(read_text(path).splitlines(), start=1):
        if needle in line:
            return {
                "file": rel(path),
                "line": line_number,
                "text": line.strip(),
                "found": True,
            }
    return {"file": rel(path), "line": 0, "text": "", "found": False}


def functions_by_entry() -> dict[str, dict[str, str]]:
    return {row.get("entry", "").lower(): row for row in read_csv(FUNCTIONS_CSV) if row.get("entry")}


def read_literal(addr: int) -> dict[str, Any]:
    code = CODE_BIN.read_bytes()
    offset = addr - CODE_BASE
    data = code[offset : offset + 4]
    if len(data) != 4:
        raise SystemExit(f"could not read code.bin literal at 0x{addr:08X}")
    return {
        "addr": f"0x{addr:08X}",
        "file_offset": f"0x{offset:08X}",
        "bits": f"0x{struct.unpack('<I', data)[0]:08X}",
        "s32": struct.unpack("<i", data)[0],
        "f32": struct.unpack("<f", data)[0],
    }


def parse_raw_words(text: str) -> list[int]:
    values = [int(part, 16) for part in text.split(";") if part]
    if len(values) != PLAYER_ACTION_RECORD_WORD_COUNT:
        raise SystemExit(f"expected {PLAYER_ACTION_RECORD_WORD_COUNT} raw words, found {len(values)}")
    return values


def f32_from_bits(bits: int) -> float:
    return struct.unpack("<f", struct.pack("<I", bits & 0xFFFFFFFF))[0]


def vcvt_s32_f32_toward_zero(value: float) -> int:
    if not math.isfinite(value):
        raise SystemExit(f"non-finite motion vector component {value!r}")
    return int(value)


def to_s16(value: int) -> int:
    value &= 0xFFFF
    if value >= 0x8000:
        value -= 0x10000
    return value


def math_get_atan2_tbl(table: list[int], arg0: float, arg1: float) -> int:
    if arg1 == 0.0:
        return table[0]
    index = int(((arg0 / arg1) * 1024.0) + 0.5)
    if 0 <= index <= 0x400:
        return table[index]
    return table[0]


def math_atan2s(table: list[int], arg0: float, arg1: float) -> int:
    zero = 0.0
    neg_arg0 = -arg0
    if arg1 < zero:
        arg1 = -arg1
        if arg0 >= zero:
            if arg0 < arg1:
                angle = math_get_atan2_tbl(table, arg0, arg1) + 0xC000
            else:
                angle = -math_get_atan2_tbl(table, arg1, arg0)
        elif neg_arg0 < arg1:
            angle = 0xC000 - math_get_atan2_tbl(table, neg_arg0, arg1)
        else:
            angle = math_get_atan2_tbl(table, arg1, neg_arg0) + 0x8000
    elif arg0 < zero:
        if neg_arg0 >= arg1:
            angle = 0x8000 - math_get_atan2_tbl(table, arg1, neg_arg0)
        else:
            angle = math_get_atan2_tbl(table, neg_arg0, arg1) + 0x4000
    elif arg0 < arg1:
        angle = 0x4000 - math_get_atan2_tbl(table, arg0, arg1)
    else:
        angle = math_get_atan2_tbl(table, arg1, arg0)
    return to_s16(angle)


def direct_state_cases() -> set[int]:
    cases: set[int] = set()
    for line in read_text(DIRECT_STATE_SWITCH_C).splitlines():
        stripped = line.strip()
        if not stripped.startswith("case "):
            continue
        token = stripped.split()[1].rstrip(":")
        try:
            cases.add(int(token, 16 if token.lower().startswith("0x") else 10))
        except ValueError:
            continue
    return cases


def c_string(value: Any) -> str:
    text = "" if value is None else str(value)
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def c_u8(value: Any) -> str:
    return f"{int(value) & 0xFF}u"


def c_u16(value: Any) -> str:
    return f"{int(value) & 0xFFFF}u"


def c_u32(value: Any) -> str:
    return f"0x{int(value) & 0xFFFFFFFF:08X}u"


def c_s32(value: Any) -> str:
    return str(int(value))


def c_float(value: Any) -> str:
    text = f"{float(value):.9g}"
    if "e" not in text.lower() and "." not in text:
        text += ".0"
    return text + "f"


def collect_motion_rows(
    player_refs: list[dict[str, Any]],
    state_cases: set[int],
    clamp: float,
    math_table: list[int],
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for motion_index, ref in enumerate(player_refs):
        raw_words = parse_raw_words(ref["raw_words_text"])
        vector_x_bits = raw_words[MOTION_CONSUMER_VECTOR_X_WORD_INDEX]
        vector_z_bits = raw_words[MOTION_CONSUMER_VECTOR_Z_WORD_INDEX]
        vector_x = f32_from_bits(vector_x_bits)
        vector_z = f32_from_bits(vector_z_bits)
        vector_x_s32 = vcvt_s32_f32_toward_zero(vector_x)
        vector_z_s32 = vcvt_s32_f32_toward_zero(vector_z)
        vector_x_quantized = float(vector_x_s32)
        vector_z_quantized = float(vector_z_s32)
        unclamped_speed = math.sqrt(vector_x_quantized * vector_x_quantized + vector_z_quantized * vector_z_quantized)
        clamped = unclamped_speed > clamp
        action_id = int(ref["action_id"])
        atan2_arg0_z = vector_z_quantized
        atan2_arg1_neg_x = -vector_x_quantized
        native_heading = math_atan2s(math_table, atan2_arg0_z, atan2_arg1_neg_x)
        rows.append(
            {
                "motion_ref_index": motion_index,
                "orchestration_index": ref["orchestration_index"],
                "player_action_ref_index": ref["player_action_ref_index"],
                "player_action_source_index": ref["player_action_source_index"],
                "native_command_source_index": ref["native_command_source_index"],
                "local_command_index": ref["local_command_index"],
                "entry_index": ref["entry_index"],
                "action_id": action_id,
                "action_id_hex": f"0x{action_id:04X}",
                "start_frame": ref["start_frame"],
                "end_frame": ref["end_frame"],
                "duration_frames": ref["duration_frames"],
                "direct_state_case_present": action_id in state_cases,
                "not_direct_state_case": action_id not in state_cases,
                "vector_x_word_index": MOTION_CONSUMER_VECTOR_X_WORD_INDEX,
                "vector_z_word_index": MOTION_CONSUMER_VECTOR_Z_WORD_INDEX,
                "vector_x_bits": f"0x{vector_x_bits:08X}",
                "vector_z_bits": f"0x{vector_z_bits:08X}",
                "vector_x_f32": vector_x,
                "vector_z_f32": vector_z,
                "vector_x_s32_vcvt": vector_x_s32,
                "vector_z_s32_vcvt": vector_z_s32,
                "vector_x_quantized_f32": vector_x_quantized,
                "vector_z_quantized_f32": vector_z_quantized,
                "unclamped_speed": unclamped_speed,
                "clamped_speed": clamp if clamped else unclamped_speed,
                "speed_clamped": clamped,
                "has_quantized_motion_vector": vector_x_s32 != 0 or vector_z_s32 != 0,
                "atan2_arg0_z": atan2_arg0_z,
                "atan2_arg1_neg_x": atan2_arg1_neg_x,
                "native_heading_s16": native_heading,
                "native_heading_u16_hex": f"0x{native_heading & 0xFFFF:04X}",
                "raw_words": [f"0x{word:08X}" for word in raw_words],
                "raw_words_text": ref["raw_words_text"],
            }
        )
    return rows


def evidence_signals() -> list[dict[str, Any]]:
    return [
        {
            "id": "consumer_reads_record_vector_x_offset_28",
            "status": "ok" if find_line(DISASSEMBLY, "001a35d0: vldr.32 s0,[r2,#0x28]")["found"] else "missing",
            "evidence": [find_line(DISASSEMBLY, "001a35d0: vldr.32 s0,[r2,#0x28]")],
            "meaning": "The motion consumer reads the active player-action record float at +0x28.",
        },
        {
            "id": "consumer_reads_record_vector_z_offset_2c",
            "status": "ok" if find_line(DISASSEMBLY, "001a35e8: vldr.32 s0,[r2,#0x2c]")["found"] else "missing",
            "evidence": [find_line(DISASSEMBLY, "001a35e8: vldr.32 s0,[r2,#0x2c]")],
            "meaning": "The motion consumer reads the active player-action record float at +0x2C.",
        },
        {
            "id": "consumer_quantizes_float_components_with_vcvt",
            "status": "ok"
            if find_line(DISASSEMBLY, "001a35d8: vcvt.s32.f32 s0,s0")["found"]
            and find_line(DISASSEMBLY, "001a35f0: vcvt.s32.f32 s0,s0")["found"]
            else "missing",
            "evidence": [
                find_line(DISASSEMBLY, "001a35d8: vcvt.s32.f32 s0,s0"),
                find_line(DISASSEMBLY, "001a35f0: vcvt.s32.f32 s0,s0"),
            ],
            "meaning": "OOT3D converts each component to s32 and back to f32 before magnitude/heading.",
        },
        {
            "id": "consumer_writes_player_speed_offset_29cc",
            "status": "ok" if find_line(PLAYER_ACTION_C, "FUN_001a35cc(param_1 + 0x29cc")["found"] else "missing",
            "evidence": [find_line(PLAYER_ACTION_C, "FUN_001a35cc(param_1 + 0x29cc")],
            "meaning": "The first output pointer is player+0x29CC, used as the cutscene-driven speed scalar.",
        },
        {
            "id": "consumer_writes_player_heading_offset_29d0",
            "status": "ok" if find_line(PLAYER_ACTION_C, "param_1 + 0x29d0,*(undefined4 *)(param_1 + 0x29c8)")["found"] else "missing",
            "evidence": [find_line(PLAYER_ACTION_C, "param_1 + 0x29d0,*(undefined4 *)(param_1 + 0x29c8)")],
            "meaning": "The second output pointer is player+0x29D0, the native heading delta used immediately afterwards.",
        },
        {
            "id": "active_record_pointer_stored_at_player_29c8",
            "status": "ok" if find_line(PLAYER_ACTION_C, "*(undefined4 *)(param_1 + 0x29c8) = param_3;")["found"] else "missing",
            "evidence": [find_line(PLAYER_ACTION_C, "*(undefined4 *)(param_1 + 0x29c8) = param_3;")],
            "meaning": "The player action routine stores the active csCtx+0x40 record pointer at player+0x29C8.",
        },
        {
            "id": "consumer_calls_native_math_atan2s",
            "status": "ok" if find_line(MOTION_CONSUMER_C, "uVar2 = Math_Atan2S();")["found"] else "missing",
            "evidence": [find_line(MOTION_CONSUMER_C, "uVar2 = Math_Atan2S();")],
            "meaning": "Heading comes from the native Math_Atan2S path after setting VFP args to z and -x.",
        },
        {
            "id": "direct_switch_has_0x24_but_not_0x40_or_0x41",
            "status": "ok"
            if find_line(DIRECT_STATE_SWITCH_C, "case 0x24:")["found"]
            and not find_line(DIRECT_STATE_SWITCH_C, "case 0x40:")["found"]
            and not find_line(DIRECT_STATE_SWITCH_C, "case 0x41:")["found"]
            else "missing",
            "evidence": [
                find_line(DIRECT_STATE_SWITCH_C, "case 0x24:"),
                find_line(DIRECT_STATE_SWITCH_C, "case 0x40:"),
                find_line(DIRECT_STATE_SWITCH_C, "case 0x41:"),
            ],
            "meaning": "Open-title action 0x24 overlaps a direct-state case; action ids 0x40/0x41 do not and must not be forced through that switch.",
        },
    ]


def build_report() -> dict[str, Any]:
    orchestration = read_json(ORCHESTRATION)
    player_refs = orchestration["player_action_refs"]
    functions = functions_by_entry()
    required_functions = {
        entry: {
            "role": role,
            "present": entry in functions,
            "exported_name": functions.get(entry, {}).get("name", ""),
            "body_min": functions.get(entry, {}).get("body_min", ""),
            "body_max": functions.get(entry, {}).get("body_max", ""),
        }
        for entry, role in REQUIRED_FUNCTIONS.items()
    }
    clamp_bits = read_literal(MOTION_CONSUMER_CLAMP_BITS_ADDR)
    clamp_value = read_literal(MOTION_CONSUMER_CLAMP_VALUE_ADDR)
    state_cases = direct_state_cases()
    math_table = read_math_table()
    motion_rows = collect_motion_rows(player_refs, state_cases, float(clamp_value["f32"]), math_table)
    action_ids = sorted({row["action_id"] for row in motion_rows})
    signals = evidence_signals()
    checks = {
        "open_title_player_refs_present": len(player_refs) == 15,
        "required_functions_exported": all(item["present"] for item in required_functions.values()),
        "motion_consumer_signals_found": all(signal["status"] == "ok" for signal in signals),
        "consumer_clamp_literals_are_60f": clamp_bits["bits"] == "0x42700000"
        and clamp_value["bits"] == "0x42700000"
        and clamp_bits["f32"] == 60.0
        and clamp_value["f32"] == 60.0,
        "vector_offsets_match_record_words_10_11": MOTION_CONSUMER_VECTOR_X_WORD_INDEX == 10
        and MOTION_CONSUMER_VECTOR_Z_WORD_INDEX == 11,
        "opening_actions_are_0x24_0x40_0x41": action_ids == [0x24, 0x40, 0x41],
        "action_0x24_direct_state_overlap": any(row["action_id"] == 0x24 and row["direct_state_case_present"] for row in motion_rows),
        "action_0x40_0x41_not_direct_states": all(
            row["not_direct_state_case"] for row in motion_rows if row["action_id"] in (0x40, 0x41)
        ),
        "nonzero_motion_vectors_present": any(row["has_quantized_motion_vector"] for row in motion_rows),
        "native_math_atan2_support_present": MATH_HEADER.is_file() and MATH_SOURCE.is_file(),
        "native_heading_values_computed": len(math_table) == 0x401
        and all(isinstance(row["native_heading_s16"], int) for row in motion_rows),
    }
    return {
        "format": "oot3d_title_intro_opening_player_motion_audit_v1",
        "inputs": {
            "orchestration": rel(ORCHESTRATION),
            "functions": rel(FUNCTIONS_CSV),
            "disassembly": rel(DISASSEMBLY),
            "motion_consumer_decompile": rel(MOTION_CONSUMER_C),
            "player_action_decompile": rel(PLAYER_ACTION_C),
            "atan2_decompile": rel(ATAN2_C),
            "direct_state_switch_decompile": rel(DIRECT_STATE_SWITCH_C),
            "math_header": rel(MATH_HEADER),
            "math_source": rel(MATH_SOURCE),
            "math_table": rel(MATH_TABLE_CSV),
            "code_bin": str(CODE_BIN),
        },
        "identity": {
            "consumer_entry": f"0x{MOTION_CONSUMER_ENTRY:08X}",
            "record_pointer_source": "Player_Update copies csCtx+0x40 into the player-action block; oot3d_player_action_turn_in_place stores that pointer at player+0x29C8.",
            "speed_output_offset": "player+0x29CC",
            "heading_output_offset": "player+0x29D0",
            "vector_x_record_offset": f"0x{MOTION_CONSUMER_VECTOR_X_OFFSET:02X}",
            "vector_z_record_offset": f"0x{MOTION_CONSUMER_VECTOR_Z_OFFSET:02X}",
            "vector_x_record_word_index": MOTION_CONSUMER_VECTOR_X_WORD_INDEX,
            "vector_z_record_word_index": MOTION_CONSUMER_VECTOR_Z_WORD_INDEX,
            "heading_args": "Math_Atan2S(record_z_vcvt_s32_f32, -record_x_vcvt_s32_f32)",
        },
        "summary": {
            "ok": all(checks.values()),
            "checks": checks,
            "motion_row_count": len(motion_rows),
            "action_ids": [f"0x{action_id:04X}" for action_id in action_ids],
            "nonzero_motion_row_count": sum(1 for row in motion_rows if row["has_quantized_motion_vector"]),
            "next_gate": "Bind Oot3d_TitleIntroPlayerActionComputeMotion to the open-title actor runtime, then advance Link boy/Epona from the cutscene rows instead of direct-state guesses.",
        },
        "required_functions": required_functions,
        "literal_pool": {
            "clamp_compare_bits": clamp_bits,
            "clamp_value": clamp_value,
        },
        "consumer_signals": signals,
        "motion_rows": motion_rows,
        "unresolved": [
            "The default heading path uses the generated native Oot3d_MathAtan2S support; the callback parameter remains for validation harnesses that need to intercept angle output.",
            "Action 0x24 still overlaps the direct-state switch and may have additional state side effects; this audit only promotes the common motion-vector consumer shared by 0x24/0x40/0x41 records.",
            "The actor runtime still needs to apply these rows to Link boy/Epona instances in the open-title scene playback.",
        ],
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    lines = [
        "# OOT3D Title Intro Opening Player Motion",
        "",
        "This audit promotes the native `0x001A35CC` player-action motion consumer for the open-title intro. It keeps the direct-state overlap separate from the motion-vector path: action `0x24` has a direct-state case, while action `0x40` and `0x41` do not.",
        "",
        "## Summary",
        "",
        f"- OK: {data['summary']['ok']}",
        f"- Motion rows: {data['summary']['motion_row_count']}",
        f"- Action ids: {', '.join(data['summary']['action_ids'])}",
        f"- Nonzero quantized motion rows: {data['summary']['nonzero_motion_row_count']}",
        f"- Consumer entry: `{data['identity']['consumer_entry']}`",
        f"- Clamp literal: `{data['literal_pool']['clamp_value']['bits']}` = `{data['literal_pool']['clamp_value']['f32']}`",
        f"- Next gate: {data['summary']['next_gate']}",
        "",
        "## Checks",
        "",
        "| Check | Status |",
        "| --- | --- |",
    ]
    for key, value in data["summary"]["checks"].items():
        lines.append(f"| `{key}` | `{value}` |")
    lines.extend(
        [
            "",
            "## Native Consumer",
            "",
            f"- Record pointer source: {data['identity']['record_pointer_source']}",
            f"- Speed output: `{data['identity']['speed_output_offset']}`",
            f"- Heading output: `{data['identity']['heading_output_offset']}`",
            f"- Vector fields: `{data['identity']['vector_x_record_offset']}` / `{data['identity']['vector_z_record_offset']}` (words {data['identity']['vector_x_record_word_index']} / {data['identity']['vector_z_record_word_index']})",
            f"- Heading args: `{data['identity']['heading_args']}`",
            "",
            "## Evidence Signals",
            "",
            "| Signal | Status | Evidence | Meaning |",
            "| --- | --- | --- | --- |",
        ]
    )
    for signal in data["consumer_signals"]:
        evidence = []
        for item in signal["evidence"]:
            if item.get("found"):
                evidence.append(f"`{item['file']}:{item['line']}`")
        lines.append(f"| `{signal['id']}` | `{signal['status']}` | {'; '.join(evidence)} | {signal['meaning']} |")
    lines.extend(
        [
            "",
            "## Motion Rows",
            "",
            "| Ref | Action | Frames | Direct case | X f32 | Z f32 | X vcvt | Z vcvt | Speed | Heading | Atan2 args |",
            "| ---: | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |",
        ]
    )
    for row in data["motion_rows"]:
        lines.append(
            f"| {row['motion_ref_index']} | `{row['action_id_hex']}` | {row['start_frame']}..{row['end_frame']} | "
            f"`{row['direct_state_case_present']}` | {row['vector_x_f32']:.6g} | {row['vector_z_f32']:.6g} | "
            f"{row['vector_x_s32_vcvt']} | {row['vector_z_s32_vcvt']} | {row['clamped_speed']:.6g} | "
            f"`{row['native_heading_u16_hex']}`/{row['native_heading_s16']} | "
            f"({row['atan2_arg0_z']:.6g}, {row['atan2_arg1_neg_x']:.6g}) |"
        )
    lines.extend(["", "## Required Functions", "", "| Entry | Role | Present | Name | Body |", "| --- | --- | --- | --- | --- |"])
    for entry, row in data["required_functions"].items():
        lines.append(
            f"| `{entry}` | `{row['role']}` | `{row['present']}` | `{row['exported_name']}` | `{row['body_min']}..{row['body_max']}` |"
        )
    lines.extend(["", "## Unresolved", ""])
    for item in data["unresolved"]:
        lines.append(f"- {item}")
    write_text(path, "\n".join(lines) + "\n")


def write_header(path: Path, data: dict[str, Any]) -> None:
    lines = [
        "#ifndef OOT3D_TITLE_INTRO_OPENING_PLAYER_MOTION_H",
        "#define OOT3D_TITLE_INTRO_OPENING_PLAYER_MOTION_H",
        "",
        "#include \"oot3d/types.h\"",
        "",
        "#define OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_CONSUMER_ENTRY 0x001A35CCu",
        "#define OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_RECORD_WORD_COUNT 12u",
        "#define OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_VECTOR_X_OFFSET 0x28u",
        "#define OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_VECTOR_Z_OFFSET 0x2Cu",
        "#define OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_VECTOR_X_WORD_INDEX 10u",
        "#define OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_VECTOR_Z_WORD_INDEX 11u",
        "#define OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_CLAMP_BITS 0x42700000u",
        "#define OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_CLAMP_VALUE 60.0f",
        "",
        "typedef enum {",
        "    OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_OK = 0,",
        "    OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_NULL_RECORD,",
        "    OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_NULL_OUTPUT,",
        "} Oot3dTitleIntroPlayerActionMotionStatus;",
        "",
        "typedef s16 (*Oot3dTitleIntroMathAtan2SFunc)(float arg0Z, float arg1X, void* user);",
        "",
        "typedef struct {",
        "    u32 rawWords[OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_RECORD_WORD_COUNT];",
        "} Oot3dTitleIntroPlayerActionMotionRecord;",
        "",
        "typedef struct {",
        "    u16 actionId;",
        "    u16 startFrame;",
        "    u16 endFrame;",
        "    u16 durationFrames;",
        "    s16 rotX;",
        "    s16 rotY;",
        "    s16 rotZ;",
        "    s32 startX;",
        "    s32 startY;",
        "    s32 startZ;",
        "    s32 endX;",
        "    s32 endY;",
        "    s32 endZ;",
        "    float tailWord9F32;",
        "    float tailWord10F32;",
        "    float tailWord11F32;",
        "} Oot3dTitleIntroPlayerActionTransform;",
        "",
        "typedef struct {",
        "    u16 motionRefIndex;",
        "    u16 orchestrationIndex;",
        "    u16 playerActionRefIndex;",
        "    u16 playerActionSourceIndex;",
        "    u16 nativeCommandSourceIndex;",
        "    u16 localCommandIndex;",
        "    u16 entryIndex;",
        "    u16 actionId;",
        "    u16 startFrame;",
        "    u16 endFrame;",
        "    u16 durationFrames;",
        "    u8 directStateCasePresent;",
        "    u8 notDirectStateCase;",
        "    u8 hasQuantizedMotionVector;",
        "    u8 speedClamped;",
        "    u32 vectorXBits;",
        "    u32 vectorZBits;",
        "    float vectorXF32;",
        "    float vectorZF32;",
        "    s32 vectorXS32Vcvt;",
        "    s32 vectorZS32Vcvt;",
        "    float vectorXQuantizedF32;",
        "    float vectorZQuantizedF32;",
        "    float unclampedSpeed;",
        "    float clampedSpeed;",
        "    float atan2Arg0Z;",
        "    float atan2Arg1NegX;",
        "    s16 nativeHeadingS16;",
        "    u16 nativeHeadingU16;",
        "    const char* actionIdHex;",
        "    const char* rawWordsText;",
        "    u32 rawWords[OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_RECORD_WORD_COUNT];",
        "} Oot3dTitleIntroOpeningPlayerMotionRow;",
        "",
        "typedef struct {",
        "    float vectorXF32;",
        "    float vectorZF32;",
        "    s32 vectorXS32Vcvt;",
        "    s32 vectorZS32Vcvt;",
        "    float vectorXQuantizedF32;",
        "    float vectorZQuantizedF32;",
        "    float unclampedSpeed;",
        "    float clampedSpeed;",
        "    u8 speedClamped;",
        "    u8 headingComputed;",
        "    float atan2Arg0Z;",
        "    float atan2Arg1NegX;",
        "    s16 heading;",
        "} Oot3dTitleIntroPlayerActionMotionResult;",
        "",
        f"extern const Oot3dTitleIntroOpeningPlayerMotionRow gOot3dTitleIntroOpeningPlayerMotionRows[{len(data['motion_rows'])}];",
        "extern const u32 gOot3dTitleIntroOpeningPlayerMotionRowCount;",
        "",
        "const Oot3dTitleIntroOpeningPlayerMotionRow* Oot3d_TitleIntroOpeningGetPlayerMotionRow(u16 motionRefIndex);",
        "Oot3dTitleIntroPlayerActionMotionStatus Oot3d_TitleIntroPlayerActionComputeMotion(",
        "    const Oot3dTitleIntroPlayerActionMotionRecord* record,",
        "    Oot3dTitleIntroMathAtan2SFunc atan2S,",
        "    void* atan2SUser,",
        "    Oot3dTitleIntroPlayerActionMotionResult* outResult",
        ");",
        "Oot3dTitleIntroPlayerActionMotionStatus Oot3d_TitleIntroPlayerActionDecodeTransform(",
        "    const Oot3dTitleIntroPlayerActionMotionRecord* record,",
        "    Oot3dTitleIntroPlayerActionTransform* outTransform",
        ");",
        "Oot3dTitleIntroPlayerActionMotionStatus Oot3d_TitleIntroOpeningComputePlayerMotionRow(",
        "    const Oot3dTitleIntroOpeningPlayerMotionRow* row,",
        "    Oot3dTitleIntroMathAtan2SFunc atan2S,",
        "    void* atan2SUser,",
        "    Oot3dTitleIntroPlayerActionMotionResult* outResult",
        ");",
        "Oot3dTitleIntroPlayerActionMotionStatus Oot3d_TitleIntroOpeningDecodePlayerMotionTransform(",
        "    const Oot3dTitleIntroOpeningPlayerMotionRow* row,",
        "    Oot3dTitleIntroPlayerActionTransform* outTransform",
        ");",
        "",
        "#endif",
        "",
    ]
    write_text(path, "\n".join(lines))


def write_source(path: Path, data: dict[str, Any]) -> None:
    lines = [
        "#include \"oot3d/title_intro_opening_player_motion.h\"",
        "#include \"oot3d/math.h\"",
        "",
        "#include <math.h>",
        "#include <string.h>",
        "",
        "const Oot3dTitleIntroOpeningPlayerMotionRow gOot3dTitleIntroOpeningPlayerMotionRows[] = {",
    ]
    for row in data["motion_rows"]:
        raw = ", ".join(c_u32(int(word, 16)) for word in row["raw_words"])
        fields = [
            c_u16(row["motion_ref_index"]),
            c_u16(row["orchestration_index"]),
            c_u16(row["player_action_ref_index"]),
            c_u16(row["player_action_source_index"]),
            c_u16(row["native_command_source_index"]),
            c_u16(row["local_command_index"]),
            c_u16(row["entry_index"]),
            c_u16(row["action_id"]),
            c_u16(row["start_frame"]),
            c_u16(row["end_frame"]),
            c_u16(row["duration_frames"]),
            c_u8(row["direct_state_case_present"]),
            c_u8(row["not_direct_state_case"]),
            c_u8(row["has_quantized_motion_vector"]),
            c_u8(row["speed_clamped"]),
            c_u32(int(row["vector_x_bits"], 16)),
            c_u32(int(row["vector_z_bits"], 16)),
            c_float(row["vector_x_f32"]),
            c_float(row["vector_z_f32"]),
            c_s32(row["vector_x_s32_vcvt"]),
            c_s32(row["vector_z_s32_vcvt"]),
            c_float(row["vector_x_quantized_f32"]),
            c_float(row["vector_z_quantized_f32"]),
            c_float(row["unclamped_speed"]),
            c_float(row["clamped_speed"]),
            c_float(row["atan2_arg0_z"]),
            c_float(row["atan2_arg1_neg_x"]),
            c_s32(row["native_heading_s16"]),
            c_u16(row["native_heading_s16"] & 0xFFFF),
            c_string(row["action_id_hex"]),
            c_string(row["raw_words_text"]),
            "{ " + raw + " }",
        ]
        lines.append("    { " + ", ".join(fields) + " },")
    lines.extend(
        [
            "};",
            "const u32 gOot3dTitleIntroOpeningPlayerMotionRowCount = sizeof(gOot3dTitleIntroOpeningPlayerMotionRows) / sizeof(gOot3dTitleIntroOpeningPlayerMotionRows[0]);",
            "",
            "static float Oot3d_TitleIntroF32FromBits(u32 bits) {",
            "    float value;",
            "    memcpy(&value, &bits, sizeof(value));",
            "    return value;",
            "}",
            "",
            "static s16 Oot3d_TitleIntroS16FromU32High(u32 value) {",
            "    return (s16)((value >> 16) & 0xFFFFu);",
            "}",
            "",
            "static s16 Oot3d_TitleIntroS16FromU32Low(u32 value) {",
            "    return (s16)(value & 0xFFFFu);",
            "}",
            "",
            "const Oot3dTitleIntroOpeningPlayerMotionRow* Oot3d_TitleIntroOpeningGetPlayerMotionRow(u16 motionRefIndex) {",
            "    if (motionRefIndex >= gOot3dTitleIntroOpeningPlayerMotionRowCount) {",
            "        return 0;",
            "    }",
            "    return &gOot3dTitleIntroOpeningPlayerMotionRows[motionRefIndex];",
            "}",
            "",
            "Oot3dTitleIntroPlayerActionMotionStatus Oot3d_TitleIntroPlayerActionComputeMotion(",
            "    const Oot3dTitleIntroPlayerActionMotionRecord* record,",
            "    Oot3dTitleIntroMathAtan2SFunc atan2S,",
            "    void* atan2SUser,",
            "    Oot3dTitleIntroPlayerActionMotionResult* outResult",
            ") {",
            "    float vectorX;",
            "    float vectorZ;",
            "    float quantizedX;",
            "    float quantizedZ;",
            "    float speed;",
            "",
            "    if (record == 0) {",
            "        return OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_NULL_RECORD;",
            "    }",
            "    if (outResult == 0) {",
            "        return OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_NULL_OUTPUT;",
            "    }",
            "",
            "    memset(outResult, 0, sizeof(*outResult));",
            "    vectorX = Oot3d_TitleIntroF32FromBits(record->rawWords[OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_VECTOR_X_WORD_INDEX]);",
            "    vectorZ = Oot3d_TitleIntroF32FromBits(record->rawWords[OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_VECTOR_Z_WORD_INDEX]);",
            "    outResult->vectorXF32 = vectorX;",
            "    outResult->vectorZF32 = vectorZ;",
            "",
            "    /* Native VCVT.S32.F32 quantizes both components before magnitude and heading. */",
            "    outResult->vectorXS32Vcvt = (s32)vectorX;",
            "    outResult->vectorZS32Vcvt = (s32)vectorZ;",
            "    quantizedX = (float)outResult->vectorXS32Vcvt;",
            "    quantizedZ = (float)outResult->vectorZS32Vcvt;",
            "    outResult->vectorXQuantizedF32 = quantizedX;",
            "    outResult->vectorZQuantizedF32 = quantizedZ;",
            "",
            "    speed = sqrtf((quantizedX * quantizedX) + (quantizedZ * quantizedZ));",
            "    outResult->unclampedSpeed = speed;",
            "    if (speed > OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_CLAMP_VALUE) {",
            "        speed = OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_CLAMP_VALUE;",
            "        outResult->speedClamped = 1u;",
            "    }",
            "    outResult->clampedSpeed = speed;",
            "    outResult->atan2Arg0Z = quantizedZ;",
            "    outResult->atan2Arg1NegX = -quantizedX;",
            "    if (atan2S != 0) {",
            "        outResult->heading = atan2S(outResult->atan2Arg0Z, outResult->atan2Arg1NegX, atan2SUser);",
            "    } else {",
            "        outResult->heading = Oot3d_MathAtan2S(outResult->atan2Arg0Z, outResult->atan2Arg1NegX);",
            "    }",
            "    outResult->headingComputed = 1u;",
            "    return OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_OK;",
            "}",
            "",
            "Oot3dTitleIntroPlayerActionMotionStatus Oot3d_TitleIntroPlayerActionDecodeTransform(",
            "    const Oot3dTitleIntroPlayerActionMotionRecord* record,",
            "    Oot3dTitleIntroPlayerActionTransform* outTransform",
            ") {",
            "    if (record == 0) {",
            "        return OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_NULL_RECORD;",
            "    }",
            "    if (outTransform == 0) {",
            "        return OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_NULL_OUTPUT;",
            "    }",
            "",
            "    memset(outTransform, 0, sizeof(*outTransform));",
            "    outTransform->actionId = (u16)(record->rawWords[0] & 0xFFFFu);",
            "    outTransform->startFrame = (u16)((record->rawWords[0] >> 16) & 0xFFFFu);",
            "    outTransform->endFrame = (u16)(record->rawWords[1] & 0xFFFFu);",
            "    outTransform->durationFrames =",
            "        outTransform->endFrame >= outTransform->startFrame",
            "            ? (u16)(outTransform->endFrame - outTransform->startFrame)",
            "            : 0u;",
            "    outTransform->rotX = Oot3d_TitleIntroS16FromU32High(record->rawWords[1]);",
            "    outTransform->rotY = Oot3d_TitleIntroS16FromU32Low(record->rawWords[2]);",
            "    outTransform->rotZ = Oot3d_TitleIntroS16FromU32High(record->rawWords[2]);",
            "    outTransform->startX = (s32)record->rawWords[3];",
            "    outTransform->startY = (s32)record->rawWords[4];",
            "    outTransform->startZ = (s32)record->rawWords[5];",
            "    outTransform->endX = (s32)record->rawWords[6];",
            "    outTransform->endY = (s32)record->rawWords[7];",
            "    outTransform->endZ = (s32)record->rawWords[8];",
            "    outTransform->tailWord9F32 = Oot3d_TitleIntroF32FromBits(record->rawWords[9]);",
            "    outTransform->tailWord10F32 = Oot3d_TitleIntroF32FromBits(record->rawWords[10]);",
            "    outTransform->tailWord11F32 = Oot3d_TitleIntroF32FromBits(record->rawWords[11]);",
            "    return OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_OK;",
            "}",
            "",
            "Oot3dTitleIntroPlayerActionMotionStatus Oot3d_TitleIntroOpeningComputePlayerMotionRow(",
            "    const Oot3dTitleIntroOpeningPlayerMotionRow* row,",
            "    Oot3dTitleIntroMathAtan2SFunc atan2S,",
            "    void* atan2SUser,",
            "    Oot3dTitleIntroPlayerActionMotionResult* outResult",
            ") {",
            "    Oot3dTitleIntroPlayerActionMotionRecord record;",
            "    if (row == 0) {",
            "        return OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_NULL_RECORD;",
            "    }",
            "    memcpy(record.rawWords, row->rawWords, sizeof(record.rawWords));",
            "    return Oot3d_TitleIntroPlayerActionComputeMotion(&record, atan2S, atan2SUser, outResult);",
            "}",
            "",
            "Oot3dTitleIntroPlayerActionMotionStatus Oot3d_TitleIntroOpeningDecodePlayerMotionTransform(",
            "    const Oot3dTitleIntroOpeningPlayerMotionRow* row,",
            "    Oot3dTitleIntroPlayerActionTransform* outTransform",
            ") {",
            "    Oot3dTitleIntroPlayerActionMotionRecord record;",
            "    if (row == 0) {",
            "        return OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_NULL_RECORD;",
            "    }",
            "    memcpy(record.rawWords, row->rawWords, sizeof(record.rawWords));",
            "    return Oot3d_TitleIntroPlayerActionDecodeTransform(&record, outTransform);",
            "}",
            "",
        ]
    )
    write_text(path, "\n".join(lines))


def main() -> None:
    data = build_report()
    write_json(OUT_JSON, data)
    write_markdown(OUT_MD, data)
    write_csv(OUT_CSV, data["motion_rows"])
    write_header(OUT_HEADER, data)
    write_source(OUT_SOURCE, data)
    if not data["summary"]["ok"]:
        raise SystemExit("title-intro opening player motion audit failed")


if __name__ == "__main__":
    main()
