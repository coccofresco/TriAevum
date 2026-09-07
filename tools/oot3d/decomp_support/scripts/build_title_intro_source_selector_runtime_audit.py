#!/usr/bin/env python3
"""Audit the title-intro native source-selector runtime."""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"

HEADER = ROOT / "include" / "oot3d" / "title_intro_source_selector_runtime.h"
SOURCE = ROOT / "src" / "code" / "z_title_intro_source_selector_runtime.c"
PRIMARY_PRODUCER_AUDIT = ANALYSIS / "title_intro_primary_producer_audit.json"
CONTEXT_FEED_DECOMPILE = (
    ANALYSIS
    / "title_intro_context_feed_producer_ghidra_export"
    / "decompiled"
    / "99002_002d6798_FUN_002d6798.c"
)

OUT_JSON = ANALYSIS / "title_intro_source_selector_runtime_audit.json"
OUT_MD = ANALYSIS / "title_intro_source_selector_runtime_audit.md"


EXPECTED_DEFINES = {
    "OOT3D_TITLE_INTRO_SOURCE_SELECTOR_ENTRY": "0x002D6798u",
    "OOT3D_TITLE_INTRO_SOURCE_SELECTOR_MASK": "0x0000FFFCu",
    "OOT3D_TITLE_INTRO_SOURCE_SELECTOR_STABLE_BITS_MASK": "0x00000F80u",
    "OOT3D_TITLE_INTRO_SOURCE_SELECTOR_BIT_SLOT2": "0x00000080u",
    "OOT3D_TITLE_INTRO_SOURCE_SELECTOR_BIT_SLOT5": "0x00000200u",
    "OOT3D_TITLE_INTRO_SOURCE_SELECTOR_BIT_SLOT9": "0x00000800u",
    "OOT3D_TITLE_INTRO_SOURCE_SELECTOR_BIT_SLOT11": "0x00000400u",
    "OOT3D_TITLE_INTRO_SOURCE_SELECTOR_BIT_SLOT14": "0x00000100u",
    "OOT3D_TITLE_INTRO_SOURCE_SELECTOR_ADJUST_HIGH": "0x00000002u",
    "OOT3D_TITLE_INTRO_SOURCE_SELECTOR_ADJUST_LOW": "0x00000001u",
    "OOT3D_TITLE_INTRO_SOURCE_SELECTOR_SOURCE_HIGH_ADJUST": "0x80u",
    "OOT3D_TITLE_INTRO_SOURCE_SELECTOR_SOURCE_LOW_ADJUST": "0x40u",
    "OOT3D_TITLE_INTRO_SOURCE_SELECTOR_MODE2_VECTOR_BITS": "0x3F800000u",
}

EXPECTED_FUNCTIONS = [
    "Oot3d_TitleIntroSourceSelectorInit",
    "Oot3d_TitleIntroSourceSelectorApply",
]


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


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


def ordered_after(text: str, anchor: str, first: str, second: str) -> bool:
    anchor_index = text.find(anchor)
    if anchor_index < 0:
        return False
    return ordered(text[anchor_index:], first, second)


def build_report() -> dict[str, Any]:
    header_text = HEADER.read_text(encoding="utf-8")
    source_text = SOURCE.read_text(encoding="utf-8")
    decompile_text = CONTEXT_FEED_DECOMPILE.read_text(encoding="utf-8")
    defines = defines_from_header(header_text)
    primary_producer = read_json(PRIMARY_PRODUCER_AUDIT)
    checks = {
        **{f"define_{name}": defines.get(name) == value for name, value in EXPECTED_DEFINES.items()},
        **{f"function_{function}": function in header_text and function in source_text for function in EXPECTED_FUNCTIONS},
        "primary_producer_audit_ok": bool(primary_producer["summary"]["ok"]),
        "native_decompile_entry_present": "void FUN_002d6798(void)" in decompile_text,
        "dirty_countdown_short_circuits": "state->runtimeFlagsOrRequest != 0u && state->dirtyCountdown != 0u" in source_text
        and "outEvent->dirtyCountdownShortCircuit = 1u" in source_text,
        "stable_gate_short_circuits": "state->gateStableWord != 0u" in source_text
        and "OOT3D_TITLE_INTRO_SOURCE_SELECTOR_STABLE_BITS_MASK" in source_text
        and "outEvent->stableGateShortCircuit = 1u" in source_text,
        "resets_current_slot_and_source": "recordContext->currentSlotOrId = OOT3D_TITLE_INTRO_RECORD_C_EMPTY_SLOT;" in source_text
        and "recordContext->recordSourceByte = OOT3D_TITLE_INTRO_RECORD_C_EMPTY_SLOT;" in source_text,
        "masks_current_gate_word": "maskedGateWord = gateCurrentWord & OOT3D_TITLE_INTRO_SOURCE_SELECTOR_MASK;" in source_text,
        "newly_active_bits_match_native_shape": "newlyActiveBits = maskedGateWord & ~state->gatePreviousWord;" in source_text
        and "state->activeSelectorWord = newlyActiveBits & maskedGateWord;" in source_text,
        "selector_priority_table_matches_audit": all(
            fragment in source_text
            for fragment in [
                "{ OOT3D_TITLE_INTRO_SOURCE_SELECTOR_BIT_SLOT2, 2u, 0u }",
                "{ OOT3D_TITLE_INTRO_SOURCE_SELECTOR_BIT_SLOT5, 5u, 1u }",
                "{ OOT3D_TITLE_INTRO_SOURCE_SELECTOR_BIT_SLOT9, 9u, 2u }",
                "{ OOT3D_TITLE_INTRO_SOURCE_SELECTOR_BIT_SLOT11, 11u, 3u }",
                "{ OOT3D_TITLE_INTRO_SOURCE_SELECTOR_BIT_SLOT14, 14u, 4u }",
            ]
        ),
        "high_adjust_matches_native": "recordContext->recordSourceByte = (u8)(recordContext->recordSourceByte -" in source_text
        and "recordContext->currentSlotOrId = (u8)(recordContext->currentSlotOrId + 1u);" in source_text,
        "low_adjust_matches_native": "recordContext->recordSourceByte = (u8)(recordContext->recordSourceByte +" in source_text
        and "recordContext->currentSlotOrId = (u8)(recordContext->currentSlotOrId - 1u);" in source_text,
        "mode2_vector_path_uses_unit_float": "recordContext->currentZ = 0u;" in source_text
        and "state->vectorScaleBits = OOT3D_TITLE_INTRO_SOURCE_SELECTOR_MODE2_VECTOR_BITS;" in source_text
        and "recordContext->currentY = 0u;" in source_text,
        "normal_vector_path_uses_native_lookup": "gOot3dTitleIntroVectorLookupBits[vectorIndex]" in source_text
        and "OOT3D_TITLE_INTRO_SOURCE_SELECTOR_VECTOR_INDEX_BIAS" in source_text,
        "normal_vector_dispatches_code6": "Oot3d_TitleIntroDispatchCode6(" in source_text
        and "outEvent->dispatchVectorEvent" in source_text,
        "slot_change_dispatch_order": ordered_after(
            source_text,
            "Oot3d_TitleIntroSourceSelectorDispatchSlotChange",
            "Oot3d_TitleIntroDispatchCode7AndScale(",
            "Oot3d_TitleIntroDispatchCode5("
        )
        and ordered_after(
            source_text,
            "Oot3d_TitleIntroSourceSelectorDispatchSlotChange",
            "Oot3d_TitleIntroDispatchCode5(",
            "Oot3d_TitleIntroDispatchCode6("
        ),
        "slot_change_dispatch_uses_native_gate": "Oot3d_TitleIntroDispatchGateOpen(dispatchState) != 0u" in source_text,
        "previous_slot_latch_owned_by_frame_advance": "state->previousSlotOrId = currentSlot;" not in source_text,
        "optional_payload_rebind_is_not_invented": "if (state->payloadValueResolved != 0u)" in source_text
        and "outEvent->payloadRebindSkipped = 1u;" in source_text,
    }
    return {
        "format": "oot3d_title_intro_source_selector_runtime_audit_v1",
        "inputs": {
            "header": rel(HEADER),
            "source": rel(SOURCE),
            "primary_producer_audit": rel(PRIMARY_PRODUCER_AUDIT),
            "context_feed_decompile": rel(CONTEXT_FEED_DECOMPILE),
        },
        "summary": {
            "ok": all(checks.values()),
            "checks": checks,
            "next_gate": "Wire the source selector into the title-intro cutscene runtime so record-C context fields are produced from native gate/input state.",
        },
        "defines": [
            {"name": name, "expected": expected, "actual": defines.get(name, "")}
            for name, expected in EXPECTED_DEFINES.items()
        ],
        "functions": EXPECTED_FUNCTIONS,
    }


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def write_json(path: Path, data: Any) -> None:
    write_text(path, json.dumps(data, indent=2) + "\n")


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    lines = [
        "# OOT3D Title Intro Source Selector Runtime Audit",
        "",
        "This audit checks that the maintained source-selector runtime preserves the native 0x002D6798 producer evidence for the title-intro record-C context.",
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
    write_text(path, "\n".join(lines) + "\n")


def main() -> None:
    data = build_report()
    write_json(OUT_JSON, data)
    write_markdown(OUT_MD, data)
    if not data["summary"]["ok"]:
        raise SystemExit("title-intro source-selector runtime audit failed")


if __name__ == "__main__":
    main()
