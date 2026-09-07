#!/usr/bin/env python3
"""Build an OOT3D-native table for fixed command 0x3E8 player dispatch events."""

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
DEFAULT_PLAYER_EVENT_HELPER_DECOMPILE = (
    ROOT
    / "analysis"
    / "scene_cutscene_context_ghidra_export"
    / "decompiled"
    / "99154_00491384_FUN_00491384.c"
)
DEFAULT_CODE_BIN = Path(r"E:\ppssppvr\oot3d_decomp\work\extract\exefs\code.bin")
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_cutscene_direct_player_event_table.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "scene_cutscene_direct_player_event_table.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_cutscene_direct_player_event_table.md"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "scene_cutscene_direct_player_event_table.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_scene_cutscene_direct_player_event_table.c"

CODE_BIN_VA_BASE = 0x00100000
CUTSCENE_PROCESS_COMMANDS = 0x002C5BA0
PLAYER_EVENT_HELPER = 0x00491384
CS_CMD_DIRECT_PLAYER_EVENT = 0x000003E8
DIRECT_PLAYER_EVENT_COMMAND_SIZE = 0x10


PLAYER_EVENT_NAMES = {
    0x0005: "OOT3D_DIRECT_PLAYER_EVENT_0005",
    0x001C: "OOT3D_DIRECT_PLAYER_EVENT_001C",
    0x001D: "OOT3D_DIRECT_PLAYER_EVENT_001D",
    0x001E: "OOT3D_DIRECT_PLAYER_EVENT_001E",
    0x001F: "OOT3D_DIRECT_PLAYER_EVENT_001F",
    0x0023: "OOT3D_DIRECT_PLAYER_EVENT_0023",
    0x0068: "OOT3D_DIRECT_PLAYER_EVENT_0068",
}

PLAYER_EVENT_DISPATCH = {
    0x0005: {
        "transition_request_index": 0x00EE,
        "transition_request_trigger": 0x14,
        "transition_request_effect": 10,
        "player_runtime_field8_expression": "DAT_00492670",
        "player_runtime_field8_literal_va": 0x00492670,
        "dispatch_evidence": "FUN_00491384 case 5: FUN_003716F0(play,0xEE,0x14,10); *(DAT_00491768+8)=DAT_00492670",
    },
    0x001C: {
        "transition_request_index": 0x0053,
        "transition_request_trigger": 0x14,
        "transition_request_effect": 3,
        "player_runtime_field8_expression": "DAT_00492698",
        "player_runtime_field8_literal_va": 0x00492698,
        "dispatch_evidence": "FUN_00491384 case 0x11/case 0x1C: FUN_003716F0(play,0x53,0x14,3); *(DAT_00491768+8)=DAT_00492698",
    },
    0x001D: {
        "transition_request_index": 0x006B,
        "transition_request_trigger": 0x14,
        "transition_request_effect": 3,
        "player_byte_write_offset": 0x05A3,
        "player_byte_write_value": 0,
        "dispatch_evidence": "FUN_00491384 case 0x1D: FUN_003716F0(play,0x6B,0x14,3); *(playerCtx+0x5A3)=0",
    },
    0x001E: {
        "transition_request_index": 0x006B,
        "transition_request_trigger": 0x14,
        "transition_request_effect": 3,
        "item_give_id": 0x0067,
        "player_byte_write_offset": 0x05A3,
        "player_byte_write_value": 1,
        "dispatch_evidence": "FUN_00491384 case 0x1E: FUN_003716F0(play,0x6B,0x14,3); Item_Give(play,0x67); *(playerCtx+0x5A3)=1",
    },
    0x001F: {
        "transition_request_index": 0x006B,
        "transition_request_trigger": 0x14,
        "transition_request_effect": 3,
        "player_byte_write_offset": 0x05A3,
        "player_byte_write_value": 2,
        "dispatch_evidence": "FUN_00491384 case 0x1F: FUN_003716F0(play,0x6B,0x14,3); *(playerCtx+0x5A3)=2",
    },
    0x0023: {
        "transition_request_index": 0x00CD,
        "transition_request_trigger": 0x14,
        "transition_request_effect": 4,
        "player_runtime_field8_expression": "DAT_00492670",
        "player_runtime_field8_literal_va": 0x00492670,
        "dispatch_evidence": "FUN_00491384 case 0x23: FUN_003716F0(play,0xCD,0x14,4); *(DAT_00491768+8)=DAT_00492670",
    },
}

PLAYER_EVENT_DYNAMIC_DISPATCH = {
    0x0068: {
        "dynamic_dispatch_state_expression": "DAT_004926F0 -> runtime selector byte",
        "dynamic_dispatch_state_literal_va": 0x004926F0,
        "dynamic_dispatch_initial_state": 0,
        "runtime_semantic": (
            "FUN_00491384 case 0x68: branch on *DAT_004926F0 and request transition "
            "0x8D/DAT_004926DC/0xA0 with trigger 0x14 effect 2"
        ),
        "dispatch_evidence": (
            "FUN_00491384 case 0x68: state 0 -> FUN_003716F0(play,0x8D,0x14,2), "
            "state 1 -> FUN_003716F0(play,DAT_004926DC,0x14,2), "
            "state 2 -> FUN_003716F0(play,0xA0,0x14,2)"
        ),
        "variants": [
            {
                "selector_value": 0,
                "selector_incremented": 1,
                "selector_reset_to_zero": 0,
                "transition_request_index": 0x008D,
                "transition_request_trigger": 0x14,
                "transition_request_effect": 2,
                "player_runtime_field8_expression": "DAT_0049267C",
                "player_runtime_field8_literal_va": 0x0049267C,
                "dispatch_evidence": (
                    "FUN_00491384 case 0x68 state 0: FUN_003716F0(play,0x8D,0x14,2); "
                    "*(DAT_00491768+8)=DAT_0049267C; (*DAT_004926F0)++"
                ),
            },
            {
                "selector_value": 1,
                "selector_incremented": 1,
                "selector_reset_to_zero": 0,
                "transition_request_index_literal_va": 0x004926DC,
                "transition_request_trigger": 0x14,
                "transition_request_effect": 2,
                "player_runtime_field8_expression": "DAT_0049266C",
                "player_runtime_field8_literal_va": 0x0049266C,
                "dispatch_evidence": (
                    "FUN_00491384 case 0x68 state 1: FUN_003716F0(play,DAT_004926DC,0x14,2); "
                    "*(DAT_00491768+8)=DAT_0049266C; (*DAT_004926F0)++"
                ),
            },
            {
                "selector_value": 2,
                "selector_incremented": 0,
                "selector_reset_to_zero": 1,
                "transition_request_index": 0x00A0,
                "transition_request_trigger": 0x14,
                "transition_request_effect": 2,
                "player_runtime_field8_expression": "DAT_00492698",
                "player_runtime_field8_literal_va": 0x00492698,
                "dispatch_evidence": (
                    "FUN_00491384 case 0x68 state 2: FUN_003716F0(play,0xA0,0x14,2); "
                    "*(DAT_00491768+8)=DAT_00492698; *DAT_004926F0=0"
                ),
            },
        ],
    },
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


def read_u32_at_va(data: bytes, va: int) -> int | None:
    offset = va - CODE_BIN_VA_BASE
    if offset < 0 or offset + 4 > len(data):
        return None
    return struct.unpack_from("<I", data, offset)[0]


def decode_event_name(action_id: int) -> str:
    return PLAYER_EVENT_NAMES.get(action_id, f"OOT3D_DIRECT_PLAYER_EVENT_{action_id:04X}")


def decode_runtime_semantic(action_id: int) -> str:
    dynamic = PLAYER_EVENT_DYNAMIC_DISPATCH.get(action_id)
    if dynamic is not None:
        return str(dynamic["runtime_semantic"])
    dispatch = PLAYER_EVENT_DISPATCH.get(action_id)
    if dispatch is None:
        return (
            "native_direct_player_event: Cutscene_ProcessCommands command 0x3E8 calls "
            "FUN_00491384(play, csCtx, payload) with payload[0]=event id and "
            "payload[1]=start frame; exact case semantics pending promotion"
        )
    return str(dispatch["dispatch_evidence"])


def load_code_bin(path: Path, errors: list[str]) -> bytes:
    if not path.exists():
        errors.append(f"missing code.bin: {path}")
        return b""
    return path.read_bytes()


def resolve_dispatch(action_id: int, code_bin: bytes, errors: list[str]) -> dict[str, Any]:
    dispatch = PLAYER_EVENT_DISPATCH.get(action_id)
    if dispatch is None:
        return {
            "dispatch_resolved": 0,
            "transition_request_index": 0,
            "transition_request_trigger": 0,
            "transition_request_effect": 0,
            "player_runtime_field8_value": 0,
            "player_runtime_field8_value_resolved": 0,
            "player_runtime_field8_expression": "",
            "player_byte_write_resolved": 0,
            "player_byte_write_offset": 0,
            "player_byte_write_value": 0,
            "item_give_resolved": 0,
            "item_give_id": 0,
            "transition_request_role": "",
            "dispatch_evidence": "",
        }

    field8_va = int_value(dispatch.get("player_runtime_field8_literal_va"))
    field8_value = 0
    field8_resolved = 0
    if field8_va != 0:
        maybe_value = read_u32_at_va(code_bin, field8_va)
        if maybe_value is None:
            errors.append(f"could not resolve {dispatch.get('player_runtime_field8_expression')} at 0x{field8_va:08X}")
        else:
            field8_value = maybe_value
            field8_resolved = 1

    return {
        "dispatch_resolved": 1,
        "transition_request_index": int_value(dispatch.get("transition_request_index")),
        "transition_request_trigger": int_value(dispatch.get("transition_request_trigger")),
        "transition_request_effect": int_value(dispatch.get("transition_request_effect")),
        "player_runtime_field8_value": field8_value,
        "player_runtime_field8_value_hex": f"0x{field8_value:08X}",
        "player_runtime_field8_value_resolved": field8_resolved,
        "player_runtime_field8_expression": str(dispatch.get("player_runtime_field8_expression", "")),
        "player_byte_write_resolved": 1 if "player_byte_write_offset" in dispatch else 0,
        "player_byte_write_offset": int_value(dispatch.get("player_byte_write_offset")),
        "player_byte_write_value": int_value(dispatch.get("player_byte_write_value")),
        "item_give_resolved": 1 if "item_give_id" in dispatch else 0,
        "item_give_id": int_value(dispatch.get("item_give_id")),
        "transition_request_role": "FUN_003716F0 request_transition_with_effect writes play+0x5C32/+0x5C2D/+0x5C76 if no pending transition is active",
        "dispatch_evidence": str(dispatch.get("dispatch_evidence", "")),
    }


def resolve_dynamic_dispatch(action_id: int, code_bin: bytes, errors: list[str]) -> dict[str, Any]:
    dynamic = PLAYER_EVENT_DYNAMIC_DISPATCH.get(action_id)
    if dynamic is None:
        return {
            "dynamic_dispatch_resolved": 0,
            "dynamic_dispatch_variant_ref_start": 0,
            "dynamic_dispatch_variant_ref_count": 0,
            "dynamic_dispatch_state_address": 0,
            "dynamic_dispatch_initial_state": 0,
            "dynamic_dispatch_state_expression": "",
        }

    state_va = int_value(dynamic.get("dynamic_dispatch_state_literal_va"))
    state_address = 0
    if state_va != 0:
        maybe_value = read_u32_at_va(code_bin, state_va)
        if maybe_value is None:
            errors.append(
                "could not resolve "
                f"{dynamic.get('dynamic_dispatch_state_expression')} at 0x{state_va:08X}"
            )
        else:
            state_address = maybe_value

    return {
        "dynamic_dispatch_resolved": 1,
        "dynamic_dispatch_variant_ref_start": 0,
        "dynamic_dispatch_variant_ref_count": 0,
        "dynamic_dispatch_state_address": state_address,
        "dynamic_dispatch_state_expression": (
            f"{dynamic.get('dynamic_dispatch_state_expression')} -> 0x{state_address:08X}"
            if state_address != 0
            else str(dynamic.get("dynamic_dispatch_state_expression", ""))
        ),
        "dynamic_dispatch_initial_state": int_value(dynamic.get("dynamic_dispatch_initial_state")),
    }


def build_dynamic_variant_rows(
    rows: list[dict[str, Any]],
    code_bin: bytes,
    errors: list[str],
) -> list[dict[str, Any]]:
    variant_rows: list[dict[str, Any]] = []
    for row in rows:
        dynamic = PLAYER_EVENT_DYNAMIC_DISPATCH.get(int_value(row.get("action_id")))
        if dynamic is None:
            continue
        start = len(variant_rows)
        for variant in dynamic.get("variants", []):
            transition_index = int_value(variant.get("transition_request_index"))
            transition_index_va = int_value(variant.get("transition_request_index_literal_va"))
            if transition_index_va != 0:
                maybe_value = read_u32_at_va(code_bin, transition_index_va)
                if maybe_value is None:
                    errors.append(
                        "could not resolve dynamic transition request literal at "
                        f"0x{transition_index_va:08X}"
                    )
                else:
                    transition_index = maybe_value & 0xFFFF

            field8_va = int_value(variant.get("player_runtime_field8_literal_va"))
            field8_value = 0
            field8_resolved = 0
            if field8_va != 0:
                maybe_value = read_u32_at_va(code_bin, field8_va)
                if maybe_value is None:
                    errors.append(
                        "could not resolve "
                        f"{variant.get('player_runtime_field8_expression')} at 0x{field8_va:08X}"
                    )
                else:
                    field8_value = maybe_value
                    field8_resolved = 1

            variant_rows.append(
                {
                    "dynamic_dispatch_variant_ref_index": len(variant_rows),
                    "direct_player_event_source_index": row["direct_player_event_source_index"],
                    "selector_value": int_value(variant.get("selector_value")),
                    "selector_incremented": int_value(variant.get("selector_incremented")),
                    "selector_reset_to_zero": int_value(variant.get("selector_reset_to_zero")),
                    "transition_request_index": transition_index,
                    "transition_request_trigger": int_value(variant.get("transition_request_trigger")),
                    "transition_request_effect": int_value(variant.get("transition_request_effect")),
                    "player_runtime_field8_value": field8_value,
                    "player_runtime_field8_value_hex": f"0x{field8_value:08X}",
                    "player_runtime_field8_value_resolved": field8_resolved,
                    "player_runtime_field8_expression": str(
                        variant.get("player_runtime_field8_expression", "")
                    ),
                    "dispatch_evidence": str(variant.get("dispatch_evidence", "")),
                }
            )
        row["dynamic_dispatch_variant_ref_start"] = start
        row["dynamic_dispatch_variant_ref_count"] = len(variant_rows) - start
    return variant_rows


def decode_direct_player_events(
    command_rows: list[dict[str, str]],
    scene_root: Path,
    code_bin: bytes,
    errors: list[str],
) -> tuple[list[dict[str, Any]], list[str]]:
    rows: list[dict[str, Any]] = []
    decode_errors: list[str] = []
    scene_cache: dict[str, bytes] = {}

    for command in command_rows:
        command_id = int_value(command.get("command_id_hex"))
        category = command.get("category", "")
        if command_id != CS_CMD_DIRECT_PLAYER_EVENT:
            continue
        if category != "fixed16":
            decode_errors.append(
                f"command {command.get('native_command_source_index')}: "
                f"CS_CMD_DIRECT_PLAYER_EVENT category is {category}"
            )
            continue

        scene_path = str(command.get("scene_path", ""))
        command_offset = int_value(command.get("command_offset_hex"))
        total_size = int_value(command.get("total_size"))
        try:
            data = load_scene_bytes(scene_root, scene_path, scene_cache)
        except FileNotFoundError:
            decode_errors.append(f"missing scene file for {scene_path}")
            continue
        if total_size != DIRECT_PLAYER_EVENT_COMMAND_SIZE:
            decode_errors.append(
                f"command {command.get('native_command_source_index')}: total size {total_size}"
            )
        if command_offset < 0 or command_offset + DIRECT_PLAYER_EVENT_COMMAND_SIZE > len(data):
            decode_errors.append(
                f"command {command.get('native_command_source_index')}: command outside {scene_path}"
            )
            continue

        raw = data[command_offset : command_offset + DIRECT_PLAYER_EVENT_COMMAND_SIZE]
        native_command_id = u32(raw, 0)
        if native_command_id != CS_CMD_DIRECT_PLAYER_EVENT:
            decode_errors.append(
                f"command {command.get('native_command_source_index')}: native id 0x{native_command_id:08X}"
            )

        action_id = u16(raw, 8)
        start_frame = u16(raw, 10)
        unk_0c = u16(raw, 12)
        unk_0e = u16(raw, 14)
        raw_words = [u32(raw, word_index * 4) for word_index in range(4)]
        dispatch = resolve_dispatch(action_id, code_bin, errors)
        dynamic_dispatch = resolve_dynamic_dispatch(action_id, code_bin, errors)

        rows.append(
            {
                "direct_player_event_source_index": len(rows),
                "native_command_source_index": int_value(command.get("native_command_source_index")),
                "cutscene_source_index": int_value(command.get("cutscene_source_index")),
                "scene_path": scene_path,
                "setup_index": int_value(command.get("setup_index")),
                "local_command_index": int_value(command.get("local_command_index")),
                "command_offset": command_offset,
                "command_offset_hex": f"0x{command_offset:08X}",
                "payload_offset": command_offset + 8,
                "payload_offset_hex": f"0x{command_offset + 8:08X}",
                "action_id": action_id,
                "action_id_hex": f"0x{action_id:04X}",
                "action_name": decode_event_name(action_id),
                "runtime_semantic": decode_runtime_semantic(action_id),
                "start_frame": start_frame,
                "unk_0c": unk_0c,
                "unk_0e": unk_0e,
                "normal_start_frame_gate_confirmed": 1,
                **dispatch,
                **dynamic_dispatch,
                "raw_words": [f"0x{word:08X}" for word in raw_words],
                "raw_word_values": raw_words,
                "raw_words_text": ";".join(f"0x{word:08X}" for word in raw_words),
                "raw_hex": raw.hex(),
            }
        )

    return rows, decode_errors


def verify_process_patterns(path: Path) -> list[str]:
    if not path.exists():
        return [f"missing Cutscene_ProcessCommands decompile: {path}"]
    text = path.read_text(encoding="utf-8")
    patterns = [
        "local_50 != 1000",
        "FUN_00491384(param_1,param_2,puVar15 + 2);",
        "puVar18 = puVar15 + 4;",
    ]
    return [f"Cutscene_ProcessCommands missing pattern `{pattern}`" for pattern in patterns if pattern not in text]


def verify_helper_patterns(path: Path) -> list[str]:
    if not path.exists():
        return [f"missing direct player event helper decompile: {path}"]
    text = path.read_text(encoding="utf-8")
    patterns = [
        "void FUN_00491384",
        "if ((*(short *)(param_2 + 0x20) != param_3[1]) && (!bVar16))",
        "switch(*param_3)",
        "case 0x23:",
        "FUN_003716f0(param_1,0xcd,0x14,4);",
        "*(undefined4 *)(DAT_00491768 + 8) = DAT_00492670;",
    ]
    return [f"FUN_00491384 missing pattern `{pattern}`" for pattern in patterns if pattern not in text]


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
        "direct_player_event_source_index",
        "native_command_source_index",
        "cutscene_source_index",
        "scene_path",
        "setup_index",
        "local_command_index",
        "command_offset_hex",
        "payload_offset_hex",
        "action_id_hex",
        "action_name",
        "runtime_semantic",
        "start_frame",
        "unk_0c",
        "unk_0e",
        "normal_start_frame_gate_confirmed",
        "dispatch_resolved",
        "transition_request_index",
        "transition_request_trigger",
        "transition_request_effect",
        "player_runtime_field8_value_hex",
        "player_runtime_field8_value_resolved",
        "player_runtime_field8_expression",
        "player_byte_write_offset",
        "player_byte_write_value",
        "player_byte_write_resolved",
        "item_give_id",
        "item_give_resolved",
        "dynamic_dispatch_resolved",
        "dynamic_dispatch_variant_ref_start",
        "dynamic_dispatch_variant_ref_count",
        "dynamic_dispatch_state_address",
        "dynamic_dispatch_initial_state",
        "dynamic_dispatch_state_expression",
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
        for row in payload.get("direct_player_event_rows", [])
        if int_value(as_dict(row).get("cutscene_source_index")) in {27, 28, 29}
    ]
    lines = [
        "# Scene Cutscene Direct Player Event Table",
        "",
        "Native decode of OOT3D fixed cutscene command `0x3E8`, which calls `FUN_00491384(play, csCtx, payload)`.",
        "",
        "## Summary",
        "",
        f"- Scene root: `{summary.get('scene_root')}`",
        f"- Direct player event commands: {summary.get('direct_player_event_count')}",
        f"- Distinct event ids: {summary.get('distinct_action_id_count')}",
        f"- Intro direct player events (`cutscene_source_index` 27/28/29): {summary.get('intro_direct_player_event_count')}",
        f"- Resolved dispatch event ids: {', '.join(summary.get('dispatch_resolved_action_ids', []))}",
        f"- Dynamic dispatch variants: {summary.get('dynamic_dispatch_variant_count')}",
        f"- Status: `{'pass' if not errors else 'fail'}`",
        "",
        "## Native Runtime Evidence",
        "",
        f"- `Cutscene_ProcessCommands` at `0x{CUTSCENE_PROCESS_COMMANDS:08X}` recognizes fixed command `1000/0x3E8` and passes `puVar15 + 2` to `FUN_00491384`.",
        f"- `FUN_00491384` at `0x{PLAYER_EVENT_HELPER:08X}` dispatches on payload word `+0x00` and uses payload word `+0x02` as the normal start-frame gate.",
        "- The helper has a native override path through cutscene context flags; the table marks only the normal `csFrame == startFrame` gate as confirmed.",
        "- Unresolved action ids keep their native 16-byte command payload intact for later code.bin promotion.",
        "",
        "## Intro Rows",
        "",
        "| cutscene | setup | command | action | start | request | field8 | byte/item | raw words |",
        "| ---: | ---: | ---: | ---: | ---: | --- | --- | --- | --- |",
    ]
    for row in intro_rows:
        request_text = ""
        if int_value(row.get("dispatch_resolved")) != 0:
            request_text = (
                f"0x{int_value(row.get('transition_request_index')):04X}/"
                f"0x{int_value(row.get('transition_request_trigger')):02X}/"
                f"0x{int_value(row.get('transition_request_effect')):02X}"
            )
        field8_text = ""
        if int_value(row.get("player_runtime_field8_value_resolved")) != 0:
            field8_text = (
                f"{row.get('player_runtime_field8_expression')}="
                f"{row.get('player_runtime_field8_value_hex')}"
            )
        byte_item_parts: list[str] = []
        if int_value(row.get("player_byte_write_resolved")) != 0:
            byte_item_parts.append(
                f"+0x{int_value(row.get('player_byte_write_offset')):04X}={int_value(row.get('player_byte_write_value'))}"
            )
        if int_value(row.get("item_give_resolved")) != 0:
            byte_item_parts.append(f"Item_Give(0x{int_value(row.get('item_give_id')):04X})")
        lines.append(
            f"| {row.get('cutscene_source_index')} | {row.get('setup_index')} | "
            f"{row.get('native_command_source_index')} | `{row.get('action_id_hex')}` | "
            f"{row.get('start_frame')} | `{request_text}` | `{field8_text}` | "
            f"`{'; '.join(byte_item_parts)}` | `{row.get('raw_words_text')}` |"
        )
    dynamic_rows = [
        as_dict(row)
        for row in payload.get("direct_player_event_dynamic_variant_rows", [])
        if int_value(row.get("direct_player_event_source_index")) in {
            int_value(intro_row.get("direct_player_event_source_index"))
            for intro_row in intro_rows
        }
    ]
    if dynamic_rows:
        lines.extend(
            [
                "",
                "## Intro Dynamic Dispatch Variants",
                "",
                "| event row | selector | next selector | request | field8 | evidence |",
                "| ---: | ---: | --- | --- | --- | --- |",
            ]
        )
        for row in dynamic_rows:
            next_selector = "reset 0" if int_value(row.get("selector_reset_to_zero")) else "increment"
            lines.append(
                f"| {row.get('direct_player_event_source_index')} | {row.get('selector_value')} | "
                f"`{next_selector}` | "
                f"`0x{int_value(row.get('transition_request_index')):04X}/"
                f"0x{int_value(row.get('transition_request_trigger')):02X}/"
                f"0x{int_value(row.get('transition_request_effect')):02X}` | "
                f"`{row.get('player_runtime_field8_expression')}={row.get('player_runtime_field8_value_hex')}` | "
                f"`{row.get('dispatch_evidence')}` |"
            )
    lines.extend(
        [
            "",
            "## Outputs",
            "",
            "- `analysis/scene_cutscene_direct_player_event_table.json`",
            "- `analysis/scene_cutscene_direct_player_event_table.csv`",
            "- `include/oot3d/scene_cutscene_direct_player_event_table.h`",
            "- `src/code/z_scene_cutscene_direct_player_event_table.c`",
            "",
        ]
    )
    if errors:
        lines.extend(["## Errors", ""])
        lines.extend(f"- {error}" for error in errors)
        lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_header(path: Path, row_count: int, dynamic_variant_count: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "#ifndef OOT3D_SCENE_CUTSCENE_DIRECT_PLAYER_EVENT_TABLE_H",
        "#define OOT3D_SCENE_CUTSCENE_DIRECT_PLAYER_EVENT_TABLE_H",
        "",
        '#include "oot3d/scene.h"',
        "",
        "enum {",
        f"    OOT3D_SCENE_CUTSCENE_DIRECT_PLAYER_EVENT_ROW_COUNT = {row_count},",
        f"    OOT3D_SCENE_CUTSCENE_DIRECT_PLAYER_EVENT_DYNAMIC_VARIANT_ROW_COUNT = {dynamic_variant_count},",
        "};",
        "",
        "typedef enum {",
        "    OOT3D_CUTSCENE_DIRECT_PLAYER_EVENT_ID_0005 = 0x0005,",
        "    OOT3D_CUTSCENE_DIRECT_PLAYER_EVENT_ID_001C = 0x001C,",
        "    OOT3D_CUTSCENE_DIRECT_PLAYER_EVENT_ID_001D = 0x001D,",
        "    OOT3D_CUTSCENE_DIRECT_PLAYER_EVENT_ID_001E = 0x001E,",
        "    OOT3D_CUTSCENE_DIRECT_PLAYER_EVENT_ID_001F = 0x001F,",
        "    OOT3D_CUTSCENE_DIRECT_PLAYER_EVENT_ID_0023 = 0x0023,",
        "    OOT3D_CUTSCENE_DIRECT_PLAYER_EVENT_ID_0068 = 0x0068,",
        "} Oot3dCutsceneDirectPlayerEventId;",
        "",
        "typedef struct {",
        "    u16 directPlayerEventSourceIndex;",
        "    u16 nativeCommandSourceIndex;",
        "    u16 cutsceneSourceIndex;",
        "    u16 setupIndex;",
        "    u16 localCommandIndex;",
        "    u32 commandOffset;",
        "    u32 payloadOffset;",
        "    u16 actionId;",
        "    u16 startFrame;",
        "    u16 unk0C;",
        "    u16 unk0E;",
        "    u8 normalStartFrameGateConfirmed;",
        "    u8 dispatchResolved;",
        "    u16 transitionRequestIndex;",
        "    u8 transitionRequestTrigger;",
        "    u8 transitionRequestEffect;",
        "    u32 playerRuntimeField8Value;",
        "    u8 playerRuntimeField8ValueResolved;",
        "    u16 playerByteWriteOffset;",
        "    u8 playerByteWriteValue;",
        "    u8 playerByteWriteResolved;",
        "    u16 itemGiveId;",
        "    u8 itemGiveResolved;",
        "    u32 rawWords[4];",
        "    const char* scenePath;",
        "    const char* actionName;",
        "    const char* runtimeSemantic;",
        "    const char* transitionRequestRole;",
        "    const char* playerRuntimeField8Expression;",
        "    const char* dispatchEvidence;",
        "    const char* rawWordsText;",
        "    u8 dynamicDispatchResolved;",
        "    u16 dynamicDispatchVariantRefStart;",
        "    u16 dynamicDispatchVariantRefCount;",
        "    u32 dynamicDispatchStateAddress;",
        "    u8 dynamicDispatchInitialState;",
        "    const char* dynamicDispatchStateExpression;",
        "} Oot3dSceneCutsceneDirectPlayerEventRow;",
        "",
        "typedef struct {",
        "    u16 dynamicDispatchVariantRefIndex;",
        "    u16 directPlayerEventSourceIndex;",
        "    u8 selectorValue;",
        "    u8 selectorIncremented;",
        "    u8 selectorResetToZero;",
        "    u16 transitionRequestIndex;",
        "    u8 transitionRequestTrigger;",
        "    u8 transitionRequestEffect;",
        "    u32 playerRuntimeField8Value;",
        "    u8 playerRuntimeField8ValueResolved;",
        "    const char* playerRuntimeField8Expression;",
        "    const char* dispatchEvidence;",
        "} Oot3dSceneCutsceneDirectPlayerEventDynamicVariantRow;",
        "",
        "extern const Oot3dSceneCutsceneDirectPlayerEventRow oot3d_scene_cutscene_direct_player_event_rows[];",
        "extern const Oot3dSceneCutsceneDirectPlayerEventDynamicVariantRow",
        "    oot3d_scene_cutscene_direct_player_event_dynamic_variant_rows[];",
        "extern const u32 oot3d_scene_cutscene_direct_player_event_row_count;",
        "extern const u32 oot3d_scene_cutscene_direct_player_event_dynamic_variant_row_count;",
        "",
        "const Oot3dSceneCutsceneDirectPlayerEventRow* Oot3d_CutsceneDirectPlayerEventGetRow(",
        "    u16 directPlayerEventSourceIndex",
        ");",
        "",
        "u32 Oot3d_CutsceneDirectPlayerEventCollectStartTriggers(",
        "    u16 cutsceneSourceIndex,",
        "    s32 frame,",
        "    const Oot3dSceneCutsceneDirectPlayerEventRow** outRows,",
        "    u32 maxRows",
        ");",
        "",
        "const Oot3dSceneCutsceneDirectPlayerEventDynamicVariantRow*",
        "Oot3d_CutsceneDirectPlayerEventFindDynamicVariant(",
        "    const Oot3dSceneCutsceneDirectPlayerEventRow* row,",
        "    u8 selectorValue",
        ");",
        "",
        "#endif",
        "",
    ]
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_source(path: Path, rows: list[dict[str, Any]], dynamic_variant_rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "/* Generated by build_scene_cutscene_direct_player_event_table.py. */",
        "",
        '#include "oot3d/scene_cutscene_direct_player_event_table.h"',
        "",
        "#include <stddef.h>",
        "",
        "const Oot3dSceneCutsceneDirectPlayerEventRow oot3d_scene_cutscene_direct_player_event_rows[] = {",
    ]
    for row in rows:
        raw_values = [int_value(value) for value in row.get("raw_word_values", [])]
        raw_initializer = "{ " + ", ".join(c_u32(value) for value in raw_values) + " }"
        lines.append(
            "    { "
            f"{c_u16(row.get('direct_player_event_source_index'))}, "
            f"{c_u16(row.get('native_command_source_index'))}, "
            f"{c_u16(row.get('cutscene_source_index'))}, "
            f"{c_u16(row.get('setup_index'))}, "
            f"{c_u16(row.get('local_command_index'))}, "
            f"{c_u32(row.get('command_offset'))}, "
            f"{c_u32(row.get('payload_offset'))}, "
            f"{c_u16(row.get('action_id'))}, "
            f"{c_u16(row.get('start_frame'))}, "
            f"{c_u16(row.get('unk_0c'))}, "
            f"{c_u16(row.get('unk_0e'))}, "
            f"{c_u8(row.get('normal_start_frame_gate_confirmed'))}, "
            f"{c_u8(row.get('dispatch_resolved'))}, "
            f"{c_u16(row.get('transition_request_index'))}, "
            f"{c_u8(row.get('transition_request_trigger'))}, "
            f"{c_u8(row.get('transition_request_effect'))}, "
            f"{c_u32(row.get('player_runtime_field8_value'))}, "
            f"{c_u8(row.get('player_runtime_field8_value_resolved'))}, "
            f"{c_u16(row.get('player_byte_write_offset'))}, "
            f"{c_u8(row.get('player_byte_write_value'))}, "
            f"{c_u8(row.get('player_byte_write_resolved'))}, "
            f"{c_u16(row.get('item_give_id'))}, "
            f"{c_u8(row.get('item_give_resolved'))}, "
            f"{raw_initializer}, "
            f"{c_string(row.get('scene_path'))}, "
            f"{c_string(row.get('action_name'))}, "
            f"{c_string(row.get('runtime_semantic'))}, "
            f"{c_string(row.get('transition_request_role'))}, "
            f"{c_string(row.get('player_runtime_field8_expression'))}, "
            f"{c_string(row.get('dispatch_evidence'))}, "
            f"{c_string(row.get('raw_words_text'))}, "
            f"{c_u8(row.get('dynamic_dispatch_resolved'))}, "
            f"{c_u16(row.get('dynamic_dispatch_variant_ref_start'))}, "
            f"{c_u16(row.get('dynamic_dispatch_variant_ref_count'))}, "
            f"{c_u32(row.get('dynamic_dispatch_state_address'))}, "
            f"{c_u8(row.get('dynamic_dispatch_initial_state'))}, "
            f"{c_string(row.get('dynamic_dispatch_state_expression'))} "
            "},"
        )
    lines.extend(
        [
            "};",
            "",
            "const Oot3dSceneCutsceneDirectPlayerEventDynamicVariantRow",
            "    oot3d_scene_cutscene_direct_player_event_dynamic_variant_rows[] = {",
        ]
    )
    for row in dynamic_variant_rows:
        lines.append(
            "    { "
            f"{c_u16(row.get('dynamic_dispatch_variant_ref_index'))}, "
            f"{c_u16(row.get('direct_player_event_source_index'))}, "
            f"{c_u8(row.get('selector_value'))}, "
            f"{c_u8(row.get('selector_incremented'))}, "
            f"{c_u8(row.get('selector_reset_to_zero'))}, "
            f"{c_u16(row.get('transition_request_index'))}, "
            f"{c_u8(row.get('transition_request_trigger'))}, "
            f"{c_u8(row.get('transition_request_effect'))}, "
            f"{c_u32(row.get('player_runtime_field8_value'))}, "
            f"{c_u8(row.get('player_runtime_field8_value_resolved'))}, "
            f"{c_string(row.get('player_runtime_field8_expression'))}, "
            f"{c_string(row.get('dispatch_evidence'))} "
            "},"
        )
    lines.extend(
        [
            "};",
            "",
            "const u32 oot3d_scene_cutscene_direct_player_event_row_count = OOT3D_SCENE_CUTSCENE_DIRECT_PLAYER_EVENT_ROW_COUNT;",
            "const u32 oot3d_scene_cutscene_direct_player_event_dynamic_variant_row_count =",
            "    OOT3D_SCENE_CUTSCENE_DIRECT_PLAYER_EVENT_DYNAMIC_VARIANT_ROW_COUNT;",
            "",
            "const Oot3dSceneCutsceneDirectPlayerEventRow* Oot3d_CutsceneDirectPlayerEventGetRow(",
            "    u16 directPlayerEventSourceIndex",
            ") {",
            "    if (directPlayerEventSourceIndex >= oot3d_scene_cutscene_direct_player_event_row_count) {",
            "        return NULL;",
            "    }",
            "    return &oot3d_scene_cutscene_direct_player_event_rows[directPlayerEventSourceIndex];",
            "}",
            "",
            "static void Oot3d_CutsceneDirectPlayerEventMaybeStoreRow(",
            "    const Oot3dSceneCutsceneDirectPlayerEventRow* row,",
            "    const Oot3dSceneCutsceneDirectPlayerEventRow** outRows,",
            "    u32 maxRows,",
            "    u32 count",
            ") {",
            "    if (outRows != NULL && count < maxRows) {",
            "        outRows[count] = row;",
            "    }",
            "}",
            "",
            "u32 Oot3d_CutsceneDirectPlayerEventCollectStartTriggers(",
            "    u16 cutsceneSourceIndex,",
            "    s32 frame,",
            "    const Oot3dSceneCutsceneDirectPlayerEventRow** outRows,",
            "    u32 maxRows",
            ") {",
            "    u32 rowIndex;",
            "    u32 count = 0;",
            "",
            "    for (rowIndex = 0; rowIndex < oot3d_scene_cutscene_direct_player_event_row_count; rowIndex++) {",
            "        const Oot3dSceneCutsceneDirectPlayerEventRow* row = &oot3d_scene_cutscene_direct_player_event_rows[rowIndex];",
            "",
            "        if (row->cutsceneSourceIndex != cutsceneSourceIndex) {",
            "            continue;",
            "        }",
            "        if (row->normalStartFrameGateConfirmed == 0) {",
            "            continue;",
            "        }",
            "        if ((s32)row->startFrame == frame) {",
            "            Oot3d_CutsceneDirectPlayerEventMaybeStoreRow(row, outRows, maxRows, count);",
            "            count++;",
            "        }",
            "    }",
            "",
            "    return count;",
            "}",
            "",
            "const Oot3dSceneCutsceneDirectPlayerEventDynamicVariantRow*",
            "Oot3d_CutsceneDirectPlayerEventFindDynamicVariant(",
            "    const Oot3dSceneCutsceneDirectPlayerEventRow* row,",
            "    u8 selectorValue",
            ") {",
            "    u32 variantIndex;",
            "    u32 variantEnd;",
            "",
            "    if (row == NULL || row->dynamicDispatchResolved == 0) {",
            "        return NULL;",
            "    }",
            "",
            "    variantEnd = (u32)row->dynamicDispatchVariantRefStart + row->dynamicDispatchVariantRefCount;",
            "    if (variantEnd > oot3d_scene_cutscene_direct_player_event_dynamic_variant_row_count) {",
            "        variantEnd = oot3d_scene_cutscene_direct_player_event_dynamic_variant_row_count;",
            "    }",
            "",
            "    for (variantIndex = row->dynamicDispatchVariantRefStart; variantIndex < variantEnd; variantIndex++) {",
            "        const Oot3dSceneCutsceneDirectPlayerEventDynamicVariantRow* variant =",
            "            &oot3d_scene_cutscene_direct_player_event_dynamic_variant_rows[variantIndex];",
            "        if (variant->directPlayerEventSourceIndex == row->directPlayerEventSourceIndex &&",
            "            variant->selectorValue == selectorValue) {",
            "            return variant;",
            "        }",
            "    }",
            "",
            "    return NULL;",
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
    code_bin = load_code_bin(DEFAULT_CODE_BIN, errors)
    direct_player_event_rows, decode_errors = decode_direct_player_events(
        command_rows,
        scene_root,
        code_bin,
        errors,
    )
    dynamic_variant_rows = build_dynamic_variant_rows(direct_player_event_rows, code_bin, errors)
    errors.extend(decode_errors)
    errors.extend(verify_process_patterns(DEFAULT_CUTSCENE_PROCESS_DECOMPILE))
    errors.extend(verify_helper_patterns(DEFAULT_PLAYER_EVENT_HELPER_DECOMPILE))

    action_counts = Counter(row["action_id_hex"] for row in direct_player_event_rows)
    scene_counts = Counter(row["scene_path"] for row in direct_player_event_rows)
    resolved_action_ids = sorted(
        {
            row["action_id"]
            for row in direct_player_event_rows
            if int_value(row.get("dispatch_resolved")) != 0
        }
    )
    summary = {
        "format": "oot3d_scene_cutscene_direct_player_event_table_v1",
        "scene_root": str(scene_root),
        "source_native_decode_table": str(DEFAULT_NATIVE_DECODE_TABLE),
        "source_native_command_table": str(DEFAULT_NATIVE_COMMAND_CSV),
        "source_code_bin": str(DEFAULT_CODE_BIN),
        "runtime_reference": f"Cutscene_ProcessCommands 0x{CUTSCENE_PROCESS_COMMANDS:08X}",
        "direct_player_event_helper_reference": f"FUN_00491384 0x{PLAYER_EVENT_HELPER:08X}",
        "direct_player_event_count": len(direct_player_event_rows),
        "distinct_action_id_count": len(action_counts),
        "action_id_counts": dict(sorted(action_counts.items())),
        "scene_counts": dict(sorted(scene_counts.items())),
        "intro_direct_player_event_count": sum(
            1 for row in direct_player_event_rows if row["cutscene_source_index"] in {27, 28, 29}
        ),
        "dynamic_dispatch_event_count": sum(
            1 for row in direct_player_event_rows if int_value(row.get("dynamic_dispatch_resolved")) != 0
        ),
        "dynamic_dispatch_variant_count": len(dynamic_variant_rows),
        "normal_start_gate": "csFrame == payload[1] unless helper override path bVar16 is active",
        "dispatch_resolved_action_ids": [f"0x{action_id:04X}" for action_id in resolved_action_ids],
        "raw_payload_policy": "preserve native fixed16 command payload until exact code.bin case semantics are promoted",
    }
    payload = {
        "summary": summary,
        "direct_player_event_rows": direct_player_event_rows,
        "direct_player_event_dynamic_variant_rows": dynamic_variant_rows,
    }
    return payload, errors


def main() -> int:
    payload, errors = build_payload()
    write_json(DEFAULT_OUT_JSON, payload)
    write_csv(DEFAULT_OUT_CSV, payload["direct_player_event_rows"])
    write_markdown(DEFAULT_OUT_MD, payload, errors)
    write_header(
        DEFAULT_OUT_HEADER,
        len(payload["direct_player_event_rows"]),
        len(payload["direct_player_event_dynamic_variant_rows"]),
    )
    write_source(
        DEFAULT_OUT_SOURCE,
        payload["direct_player_event_rows"],
        payload["direct_player_event_dynamic_variant_rows"],
    )
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1
    summary = as_dict(payload.get("summary"))
    print(
        "wrote scene cutscene direct player event table: "
        f"{summary.get('direct_player_event_count')} entries, "
        f"{summary.get('intro_direct_player_event_count')} intro entries"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
