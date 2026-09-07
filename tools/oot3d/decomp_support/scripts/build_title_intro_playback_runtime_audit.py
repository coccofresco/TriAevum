#!/usr/bin/env python3
"""Audit the maintained title-intro playback runtime source."""

from __future__ import annotations

import json
import re
import csv
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"

HEADER = ROOT / "include" / "oot3d" / "title_intro_playback_runtime.h"
SOURCE = ROOT / "src" / "code" / "z_title_intro_playback_runtime.c"
RECORD_APPLY = ANALYSIS / "title_intro_record_apply_audit.json"
DISPATCH_HELPER = ANALYSIS / "title_intro_dispatch_helper_audit.json"
STATE37_GATE_DATA_WORDS = ANALYSIS / "title_intro_state37_gate_data_words.csv"

OUT_JSON = ANALYSIS / "title_intro_playback_runtime_audit.json"
OUT_MD = ANALYSIS / "title_intro_playback_runtime_audit.md"


EXPECTED_DEFINES = {
    "OOT3D_TITLE_INTRO_RECORD_BYTE_TABLE_ADDR": "0x005A2E7Cu",
    "OOT3D_TITLE_INTRO_FIRST_CHANGE_LATCH_ADDR": "0x0055A21Cu",
    "OOT3D_TITLE_INTRO_FIRST_CHANGE_INIT_CONTEXT_ADDR": "0x005BE5B8u",
    "OOT3D_TITLE_INTRO_RECORD_CHANGE_NOTIFY_CONTEXT_ADDR": "0x005C1878u",
    "OOT3D_TITLE_INTRO_DISPATCH_CONTEXT_ADDR": "0x005B12E8u",
    "OOT3D_TITLE_INTRO_DISPATCH_GATE_CONTEXT_ADDR": "0x0054AC25u",
    "OOT3D_TITLE_INTRO_RECORD_APPLY_ENTRY": "0x002D038Cu",
    "OOT3D_TITLE_INTRO_DISPATCH_CODE5_ENTRY": "0x002CFCF0u",
    "OOT3D_TITLE_INTRO_DISPATCH_CODE7_SCALE_ENTRY": "0x002CFD24u",
    "OOT3D_TITLE_INTRO_DISPATCH_PAYLOAD_REBIND_ENTRY": "0x002CFD74u",
    "OOT3D_TITLE_INTRO_DISPATCH_CODE6_ENTRY": "0x002CFE00u",
    "OOT3D_TITLE_INTRO_RECORD_BYTE_SIDE_EFFECT_LIMIT": "8u",
    "OOT3D_TITLE_INTRO_DISPATCH_GATE_BYTE_OFFSET": "0x0005u",
    "OOT3D_TITLE_INTRO_DISPATCH_GATE_OPEN_VALUE": "0u",
    "OOT3D_TITLE_INTRO_DISPATCH_CODE_SLOT": "5u",
    "OOT3D_TITLE_INTRO_DISPATCH_CODE_VECTOR": "6u",
    "OOT3D_TITLE_INTRO_DISPATCH_CODE_SCALE": "7u",
    "OOT3D_TITLE_INTRO_DISPATCH_SCALE_NONZERO_BITS": "0x3F800000u",
    "OOT3D_TITLE_INTRO_DISPATCH_SCALE_ZERO_BITS": "0x3FB33333u",
    "OOT3D_TITLE_INTRO_PAYLOAD_DESCRIPTOR": "0x010004E0u",
    "OOT3D_TITLE_INTRO_RECORD_B_PAYLOAD_SOURCE_ADDR": "0x0054ACE4u",
    "OOT3D_TITLE_INTRO_RECORD_B_PAYLOAD_LITERAL_ADDR": "0x00477C8Cu",
    "OOT3D_TITLE_INTRO_RECORD_B_PAYLOAD_VALUE_BITS": "0x3F800000u",
    "OOT3D_TITLE_INTRO_DIRECT_STATE_37": "0x25u",
    "OOT3D_TITLE_INTRO_DIRECT_STATE_38": "0x26u",
    "OOT3D_TITLE_INTRO_DIRECT_STATE_39": "0x27u",
    "OOT3D_TITLE_INTRO_STATE37_PLAYER_BYTE_OFFSET": "0x0300u",
    "OOT3D_TITLE_INTRO_STATE37_PLAYER_TIMER_OFFSET": "0x02D3u",
    "OOT3D_TITLE_INTRO_STATE37_PLAYER_TIMER_VALUE": "0x1Eu",
    "OOT3D_TITLE_INTRO_STATE37_ADVANCE_MESSAGE_STATE": "2u",
    "OOT3D_TITLE_INTRO_STATE37_ADVANCE_ACTOR_GATE_KIND": "5u",
    "OOT3D_TITLE_INTRO_STATE37_ADVANCE_MASK_PTR_LITERAL_ADDR": "0x00474E24u",
    "OOT3D_TITLE_INTRO_STATE37_ADVANCE_MASK_SOURCE_ADDR": "0x004FC654u",
    "OOT3D_TITLE_INTRO_STATE37_ADVANCE_MASK_DEFAULT_VALUE": "0x00000000u",
    "OOT3D_TITLE_INTRO_STATE37_PLAYER_FLAGS_OFFSET": "0x0018u",
    "OOT3D_TITLE_INTRO_STATE37_FALLBACK_BYTE_OFFSET": "0x6016u",
}

EXPECTED_FUNCTIONS = [
    "Oot3d_TitleIntroRecordByteTableInit",
    "Oot3d_TitleIntroApplyRecordByte",
    "Oot3d_TitleIntroDispatchStateInit",
    "Oot3d_TitleIntroDispatchGateOpen",
    "Oot3d_TitleIntroDispatchCode5",
    "Oot3d_TitleIntroDispatchCode6",
    "Oot3d_TitleIntroDispatchCode7AndScale",
    "Oot3d_TitleIntroRebindPayload",
    "Oot3d_TitleIntroState37Init",
    "Oot3d_TitleIntroState37ExternalAdvanceGatePassed",
    "Oot3d_TitleIntroState37Step",
]


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def read_data_words(path: Path) -> dict[str, dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return {
            row["address"].lower(): row
            for row in csv.DictReader(handle)
        }


def rel(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def defines_from_header(text: str) -> dict[str, str]:
    result: dict[str, str] = {}
    pattern = re.compile(r"^#define\s+([A-Za-z0-9_]+)\s+([^\s]+)", re.MULTILINE)
    for match in pattern.finditer(text):
        result[match.group(1)] = match.group(2)
    return result


def ordered(text: str, first: str, second: str) -> bool:
    first_index = text.find(first)
    second_index = text.find(second)
    return first_index >= 0 and second_index >= 0 and first_index < second_index


def build_report() -> dict[str, Any]:
    header_text = HEADER.read_text(encoding="utf-8")
    source_text = SOURCE.read_text(encoding="utf-8")
    record_apply = read_json(RECORD_APPLY)
    dispatch_helper = read_json(DISPATCH_HELPER)
    state37_gate_words = read_data_words(STATE37_GATE_DATA_WORDS)
    defines = defines_from_header(header_text)
    define_checks = {
        name: defines.get(name) == value
        for name, value in EXPECTED_DEFINES.items()
    }
    function_checks = {
        function: function in header_text and function in source_text
        for function in EXPECTED_FUNCTIONS
    }
    source_checks = {
        "record_apply_audit_ok": bool(record_apply["summary"]["ok"]),
        "dispatch_helper_audit_ok": bool(dispatch_helper["summary"]["ok"]),
        "native_entrypoint_defines_promoted": all(
            name in header_text
            for name in [
                "OOT3D_TITLE_INTRO_RECORD_APPLY_ENTRY",
                "OOT3D_TITLE_INTRO_DISPATCH_CODE5_ENTRY",
                "OOT3D_TITLE_INTRO_DISPATCH_CODE7_SCALE_ENTRY",
                "OOT3D_TITLE_INTRO_DISPATCH_PAYLOAD_REBIND_ENTRY",
                "OOT3D_TITLE_INTRO_DISPATCH_CODE6_ENTRY",
            ]
        ),
        "unchanged_value_returns_before_write": ordered(
            source_text,
            "if (oldValue == value)",
            "state->bytes[index] = value;"
        ),
        "record_notify_before_store": ordered(
            source_text,
            "hooks->recordChangeNotify",
            "state->bytes[index] = value;"
        ),
        "side_effect_limit_gate_present": "index < OOT3D_TITLE_INTRO_RECORD_BYTE_SIDE_EFFECT_LIMIT" in source_text,
        "dispatch_handle_gate_present": "state->dispatchHandlePresent == 0" in source_text,
        "dispatch_gate_byte5_promoted": "state->dispatchGateByte5 = OOT3D_TITLE_INTRO_DISPATCH_GATE_OPEN_VALUE;" in source_text
        and "Oot3d_TitleIntroDispatchGateOpen" in source_text,
        "payload_allocator_gate_present": "state->payloadAllocatorAvailable == 0" in source_text,
        "scale_selector_matches_decompile": "selectorNonZero != 0 ?" in source_text
        and "OOT3D_TITLE_INTRO_DISPATCH_SCALE_NONZERO_BITS" in source_text
        and "OOT3D_TITLE_INTRO_DISPATCH_SCALE_ZERO_BITS" in source_text,
        "payload_descriptor_used": "OOT3D_TITLE_INTRO_PAYLOAD_DESCRIPTOR" in source_text,
        "payload_rebind_models_clear_and_handle_write": "outEvent->payloadClearRequested = 1u;" in source_text
        and "state->payloadHandleB8Value = *payloadValue;" in source_text
        and "outEvent->payloadHandleB8Written = 1u;" in source_text
        and "outEvent->payloadActivated = 1u;" in source_text,
        "state37_record_cursor_gate_matches_00475660": "recordC[2] != 0u" in source_text
        and "(s16)(recordC[2] - 1u) == state->cursor" in source_text,
        "state37_player_byte300_write_matches_0047567c": "state->playerByte300 = recordC[0];" in source_text,
        "state37_current_apply_before_cursor_increment": ordered(
            source_text,
            "(u32)state->cursor,\n            recordC[0],",
            "state->cursor = (s16)(state->cursor + 1);"
        ),
        "state37_cursor_increment_before_terminator_apply": ordered(
            source_text,
            "state->cursor = (s16)(state->cursor + 1);",
            "(u32)state->cursor,\n            0xFFu,"
        ),
        "state37_zero_mode_requests_state39": "recordC[1] == 0u" in source_text
        and "OOT3D_TITLE_INTRO_DIRECT_STATE_39" in source_text
        and "OOT3D_TITLE_INTRO_STATE37_PLAYER_TIMER_VALUE" in source_text,
        "state37_ff_or_external_gate_requests_state38": "recordC[1] == 0xFFu || externalAdvanceGatePassed != 0u" in source_text
        and "OOT3D_TITLE_INTRO_DIRECT_STATE_38" in source_text
        and "requestModeResetToZero" in source_text,
        "state37_external_gate_message_state_2": "input->messageState != OOT3D_TITLE_INTRO_STATE37_ADVANCE_MESSAGE_STATE" in source_text,
        "state37_external_gate_actor_gate_5": "input->actorGate5Passed == 0u" in source_text
        and "OOT3D_TITLE_INTRO_STATE37_ADVANCE_ACTOR_GATE_KIND" in header_text,
        "state37_external_gate_flag_or_fallback": "(input->playerFlags18 & input->requiredFlagMask) != 0u" in source_text
        and "input->fallbackByte6016 != 0u" in source_text,
        "state37_step_resolves_external_gate": "Oot3d_TitleIntroState37ExternalAdvanceGatePassed(" in source_text
        and "outEvent != NULL ? &outEvent->externalAdvanceGate : NULL" in source_text,
        "state37_gate_literal_points_to_mask_source": state37_gate_words.get("00474e24", {}).get("u32") == "0x004fc654",
        "state37_gate_mask_default_is_zero": state37_gate_words.get("004fc654", {}).get("u32") == "0x00000000",
    }
    checks = {
        **{f"define_{name}": value for name, value in define_checks.items()},
        **{f"function_{name}": value for name, value in function_checks.items()},
        **source_checks,
    }
    return {
        "format": "oot3d_title_intro_playback_runtime_audit_v1",
        "inputs": {
            "header": rel(HEADER),
            "source": rel(SOURCE),
            "record_apply_audit": rel(RECORD_APPLY),
            "dispatch_helper_audit": rel(DISPATCH_HELPER),
            "state37_gate_data_words": rel(STATE37_GATE_DATA_WORDS),
        },
        "summary": {
            "ok": all(checks.values()),
            "checks": checks,
            "next_gate": "Bind the state-37 playback step into the title-intro cutscene runtime.",
        },
        "defines": [
            {"name": name, "expected": expected, "actual": defines.get(name, "")}
            for name, expected in EXPECTED_DEFINES.items()
        ],
        "functions": EXPECTED_FUNCTIONS,
        "state37_gate_data_words": [
            state37_gate_words[key]
            for key in ["00474e24", "004fc654"]
            if key in state37_gate_words
        ],
    }


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def write_json(path: Path, data: Any) -> None:
    write_text(path, json.dumps(data, indent=2) + "\n")


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    lines = [
        "# OOT3D Title Intro Playback Runtime Audit",
        "",
        "This audit checks that the maintained playback runtime source preserves the OOT3D record applicator and dispatch helper evidence.",
        "",
        "## Summary",
        "",
        f"- OK: {data['summary']['ok']}",
        f"- Next gate: {data['summary']['next_gate']}",
        "",
        "## Checks",
        "",
        "| Check | Status |",
        "| --- | --- |",
    ]
    for key, value in data["summary"]["checks"].items():
        lines.append(f"| `{key}` | `{value}` |")

    lines.extend(["", "## Defines", "", "| Name | Expected | Actual |", "| --- | --- | --- |"])
    for row in data["defines"]:
        lines.append(f"| `{row['name']}` | `{row['expected']}` | `{row['actual']}` |")

    lines.extend(["", "## State 37 Gate Data", "", "| Address | U32 | Bytes |", "| --- | --- | --- |"])
    for row in data["state37_gate_data_words"]:
        lines.append(f"| `{row['address']}` | `{row['u32']}` | `{row['bytes']}` |")
    write_text(path, "\n".join(lines) + "\n")


def main() -> None:
    data = build_report()
    write_json(OUT_JSON, data)
    write_markdown(OUT_MD, data)
    if not data["summary"]["ok"]:
        raise SystemExit("title-intro playback runtime audit failed")


if __name__ == "__main__":
    main()
