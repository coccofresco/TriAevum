#!/usr/bin/env python3
"""Audit the title-intro source-selector feed runtime helpers."""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"

HEADER = ROOT / "include" / "oot3d" / "title_intro_source_selector_feed_runtime.h"
SOURCE = ROOT / "src" / "code" / "z_title_intro_source_selector_feed_runtime.c"
RUNTIME_TABLE_HEADER = ROOT / "include" / "oot3d" / "title_intro_runtime_tables.h"
RUNTIME_TABLE_AUDIT = ANALYSIS / "title_intro_runtime_tables.json"
OWNER_AUDIT = ANALYSIS / "title_intro_source_selector_owner_audit.json"
SOURCE_SELECTOR_AUDIT = ANALYSIS / "title_intro_source_selector_runtime_audit.json"

OUT_JSON = ANALYSIS / "title_intro_source_selector_feed_runtime_audit.json"
OUT_MD = ANALYSIS / "title_intro_source_selector_feed_runtime_audit.md"


EXPECTED_DEFINES = {
    "OOT3D_TITLE_INTRO_FEED_CAPTURE_ENTRY": "0x00422298u",
    "OOT3D_TITLE_INTRO_FEED_GATE_CHANGE_ENTRY": "0x003523DCu",
    "OOT3D_TITLE_INTRO_FEED_FRAME_ADVANCE_ENTRY": "0x00460878u",
    "OOT3D_TITLE_INTRO_FEED_MODE_CHANGE_ENTRY": "0x002D0264u",
    "OOT3D_TITLE_INTRO_FEED_SEQUENCE_BIND_ENTRY": "0x0033F248u",
    "OOT3D_TITLE_INTRO_FEED_REQUEST_INIT_ENTRY": "0x0047AFB0u",
    "OOT3D_TITLE_INTRO_FEED_REQUEST_ADVANCE_ENTRY": "0x0047BD30u",
    "OOT3D_TITLE_INTRO_FEED_SEQUENCE_ADVANCE_ENTRY": "0x00477A1Cu",
    "OOT3D_TITLE_INTRO_FEED_GATE_SAVED_WORD_OFFSET": "0x00D4u",
    "OOT3D_TITLE_INTRO_FEED_LATCHED_VECTOR_X_OFFSET": "0x004Au",
    "OOT3D_TITLE_INTRO_FEED_LATCHED_VECTOR_Y_OFFSET": "0x004Cu",
    "OOT3D_TITLE_INTRO_FEED_ACTIVE_FLAG_OFFSET": "0x0014u",
    "OOT3D_TITLE_INTRO_FEED_GLOBAL_GATE_OFFSET": "0x0015u",
    "OOT3D_TITLE_INTRO_FEED_RECORD_B_COUNTDOWN_OFFSET": "0x001Du",
    "OOT3D_TITLE_INTRO_FEED_RECORD_B_LOOKUP_SOURCE_OFFSET": "0x001Eu",
    "OOT3D_TITLE_INTRO_FEED_COUNTDOWN_OVERRIDE_OFFSET": "0x002Bu",
    "OOT3D_TITLE_INTRO_FEED_REQUEST_HISTORY_COUNT_OFFSET": "0x003Au",
    "OOT3D_TITLE_INTRO_FEED_REQUEST_WINDOW_OPEN_OFFSET": "0x003Bu",
    "OOT3D_TITLE_INTRO_FEED_REQUEST_SCAN_INDEX_OFFSET": "0x003Cu",
    "OOT3D_TITLE_INTRO_FEED_REQUEST_SCAN_LIMIT_OFFSET": "0x003Du",
    "OOT3D_TITLE_INTRO_FEED_SEQUENCE_CURSOR_OFFSET": "0x0042u",
    "OOT3D_TITLE_INTRO_FEED_SEQUENCE_SLOT_SOURCE_OFFSET": "0x0044u",
    "OOT3D_TITLE_INTRO_FEED_REQUEST_LATCHED_CODE_OFFSET": "0x0046u",
    "OOT3D_TITLE_INTRO_FEED_REQUEST_ACTIVE_MASK_OFFSET": "0x0050u",
    "OOT3D_TITLE_INTRO_FEED_TIME_COUNTER_OFFSET": "0x00BCu",
    "OOT3D_TITLE_INTRO_FEED_GATE_CURRENT_OFFSET": "0x00DCu",
    "OOT3D_TITLE_INTRO_FEED_GATE_STABLE_OFFSET": "0x00E0u",
    "OOT3D_TITLE_INTRO_FEED_GATE_PREVIOUS_OFFSET": "0x00E4u",
    "OOT3D_TITLE_INTRO_FEED_ACTIVE_SELECTOR_OFFSET": "0x00E8u",
    "OOT3D_TITLE_INTRO_FEED_VECTOR_CLAMP_MAX": "0x40",
    "OOT3D_TITLE_INTRO_FEED_VECTOR_CLAMP_MIN": "-0x40",
    "OOT3D_TITLE_INTRO_FEED_MODE_OPEN_PREVIOUS_X": "0x57u",
    "OOT3D_TITLE_INTRO_FEED_TIME_INCREMENT": "2u",
    "OOT3D_TITLE_INTRO_FEED_RUNTIME_FLAG_ALT_HELPER": "0x00004000u",
    "OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_LOW_MASK": "0x0000FFFFu",
    "OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_SELECTED_FLAG": "0x80000000u",
    "OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_ACTIVE_MASK": "0x3FFFu",
    "OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_GLOBAL_PROMOTE": "0x1000u",
    "OOT3D_TITLE_INTRO_FEED_GLOBAL_FLAG_INACTIVE": "0xFFu",
    "OOT3D_TITLE_INTRO_FEED_GLOBAL_PROMOTE_MULTIPLIER": "0x00100000u",
    "OOT3D_TITLE_INTRO_FEED_GLOBAL_PROMOTE_COMPARE": "0xFFF00000u",
    "OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_SENTINEL": "0xFFFFu",
    "OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_CFFF": "0xCFFFu",
    "OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_0FFF": "0x0FFFu",
    "OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_CFFF_OVERRIDE": "0xDFFFu",
    "OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_0FFF_OVERRIDE": "0x1FFFu",
    "OOT3D_TITLE_INTRO_FEED_RUNTIME_LANE_COUNT": "OOT3D_TITLE_INTRO_DEFERRED_SEQUENCE_LANE_COUNT",
    "OOT3D_TITLE_INTRO_FEED_REQUEST_PROGRESS_STEP": "0x12u",
    "OOT3D_TITLE_INTRO_FEED_REQUEST_COMPLETION_WINDOW": "10u",
    "OOT3D_TITLE_INTRO_FEED_SEQUENCE_INACTIVE_DELTA": "3u",
    "OOT3D_TITLE_INTRO_FEED_SCALE_BYTE_FACTOR_BITS": "0x3C010204u",
    "OOT3D_TITLE_INTRO_FEED_RECORD_A_PENDING_RUNTIME_BYTE": "0xFEu",
    "OOT3D_TITLE_INTRO_FEED_RECORD_B_DEFAULT_SEQUENCE_MODULO": "8u",
}

EXPECTED_FUNCTIONS = [
    "Oot3d_TitleIntroSourceSelectorFeedInit",
    "Oot3d_TitleIntroSourceSelectorFeedCaptureGateAndVector",
    "Oot3d_TitleIntroSourceSelectorFeedApplyGlobalGate",
    "Oot3d_TitleIntroSourceSelectorFeedSetMode",
    "Oot3d_TitleIntroSourceSelectorFeedBindSequence",
    "Oot3d_TitleIntroSourceSelectorFeedBindNativeSequence",
    "Oot3d_TitleIntroSourceSelectorFeedComposeRecordTable",
    "Oot3d_TitleIntroSourceSelectorFeedInitializeRuntimeRequest",
    "Oot3d_TitleIntroSourceSelectorFeedFrameAdvance",
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


def build_report() -> dict[str, Any]:
    header_text = HEADER.read_text(encoding="utf-8")
    source_text = SOURCE.read_text(encoding="utf-8")
    runtime_table_header_text = RUNTIME_TABLE_HEADER.read_text(encoding="utf-8")
    defines = defines_from_header(header_text)
    owner_audit = read_json(OWNER_AUDIT)
    selector_audit = read_json(SOURCE_SELECTOR_AUDIT)
    runtime_table_audit = read_json(RUNTIME_TABLE_AUDIT)
    checks = {
        **{f"define_{name}": defines.get(name) == value for name, value in EXPECTED_DEFINES.items()},
        **{f"function_{function}": function in header_text and function in source_text for function in EXPECTED_FUNCTIONS},
        "owner_audit_ok": bool(owner_audit["summary"]["ok"]),
        "source_selector_audit_ok": bool(selector_audit["summary"]["ok"]),
        "runtime_table_audit_ok": bool(runtime_table_audit["summary"]["ok"]),
        "state_carries_saved_gate_and_vectors": "u32 gateSavedWordSource;" in header_text
        and "s16 latchedVectorX;" in header_text
        and "s16 latchedVectorY;" in header_text,
        "state_carries_request_scan_fields": "u8 requestScanIndex;" in header_text
        and "u8 requestScanLimit;" in header_text
        and "u16 requestActiveMask;" in header_text
        and "u16 requestLatchedCode;" in header_text,
        "state_carries_runtime_bss_request_buffers": "requestCursorBuffer[OOT3D_TITLE_INTRO_FEED_RUNTIME_LANE_COUNT]" in header_text
        and "requestPreviousProgressBuffer[OOT3D_TITLE_INTRO_FEED_RUNTIME_LANE_COUNT]" in header_text
        and "requestDurationBuffer[OOT3D_TITLE_INTRO_FEED_RUNTIME_LANE_COUNT]" in header_text
        and "requestLookupBuffer[OOT3D_TITLE_INTRO_FEED_RUNTIME_LANE_COUNT]" in header_text,
        "state_carries_sequence_fields": "const Oot3dTitleIntroSequenceRow* sequenceRows;" in header_text
        and "u16 sequenceCursor;" in header_text
        and "u32 sequenceLastProgressTime;" in header_text
        and "u32 sequenceStateWord;" in header_text
        and "u8 recordBLookupSource;" in header_text,
        "event_separates_record_b_payload_and_scale_dispatch": "recordBPayloadDispatchEvent" in header_text
        and "recordBScaleDispatchEvent" in header_text,
        "sequence_row_is_typed_as_native_stride": "typedef struct Oot3dTitleIntroSequenceRow" in runtime_table_header_text
        and "int8_t sourceByte;" in runtime_table_header_text
        and "uint16_t duration;" in runtime_table_header_text
        and "int8_t alternateSourceByte;" in runtime_table_header_text,
        "sequence_rows_promoted_from_native_table": "gOot3dTitleIntroDeferredSequenceRows" in runtime_table_header_text
        and runtime_table_audit["summary"]["checks"].get("deferred_sequence_lane_count") is True
        and runtime_table_audit["summary"]["checks"].get("deferred_sequence_row_count") is True,
        "capture_helper_matches_00422298_fields": "state->gateSavedWordSource = gateWord;" in source_text
        and "state->latchedVectorX = vectorX;" in source_text
        and "state->latchedVectorY = vectorY;" in source_text,
        "gate_change_writes_global_gate_byte": "selector->globalGateByte = globalGateByte;" in source_text,
        "gate_open_latches_saved_gate_and_vectors": "selector->gateCurrentWord = state->gateSavedWordSource;" in source_text
        and "selector->gatePreviousWord = 0u;" in source_text
        and "selector->latchedGateFlag = 1u;" in source_text,
        "gate_open_sets_stable_snapshot": "selector->gateStableWord = selector->gateCurrentWord;" in source_text,
        "gate_close_clears_previous_active_latch": "selector->gatePreviousWord = 0u;" in source_text
        and "selector->activeSelectorWord = 0u;" in source_text
        and "selector->latchedGateFlag = 0u;" in source_text,
        "gate_close_applies_selector_before_clearing_runtime": ordered(
            source_text,
            "selectorStatus = Oot3d_TitleIntroSourceSelectorFeedApplySelector(",
            "recordContext->recordBByte1Source = 0u;"
        ),
        "mode_open_seeds_record_context": "recordContext->previousSlot = OOT3D_TITLE_INTRO_RECORD_C_EMPTY_SLOT;" in source_text
        and "recordContext->previousX = OOT3D_TITLE_INTRO_FEED_MODE_OPEN_PREVIOUS_X;" in source_text
        and "state->activeFlag = 1u;" in source_text,
        "mode_close_clears_active_and_cursor": "state->activeFlag = 0u;" in source_text
        and "recordContext->cursorCount = 0u;" in source_text,
        "sequence_bind_matches_0033f248_reset_fields": "recordContext->recordBByte1Source = recordBCountdown;" in source_text
        and "state->sequenceStateWord = 0u;" in source_text
        and "state->recordBLookupSource = OOT3D_TITLE_INTRO_FEED_RECORD_B_LOOKUP_EMPTY;" in source_text
        and "state->sequenceCursor = 0u;" in source_text,
        "sequence_bind_skips_leading_sentinel_rows": "rows[state->sequenceCursor].sourceByte == (s8)OOT3D_TITLE_INTRO_FEED_RECORD_B_LOOKUP_EMPTY" in source_text
        and "outEvent->sequenceLeadingSentinelSkipped = 1u;" in source_text,
        "native_sequence_bind_selects_promoted_lanes": "Oot3d_TitleIntroSourceSelectorFeedNativeLaneForParam" in source_text
        and "gOot3dTitleIntroDeferredSequenceLanes" in source_text
        and "gOot3dTitleIntroDeferredSequenceRows[lane->rowStart]" in source_text,
        "runtime_request_init_requires_gate_bit4_and_selector_bits": "selector->gateCurrentWord & 4u" in source_text
        and "OOT3D_TITLE_INTRO_SOURCE_SELECTOR_STABLE_BITS_MASK" in source_text,
        "runtime_request_init_derives_low16": "selector->runtimeFlagsOrRequest & OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_LOW_MASK" in source_text,
        "runtime_request_default_config_reads_record_c_global_flag": "Oot3d_TitleIntroSourceSelectorFeedBuildNativeRuntimeRequestConfig" in source_text
        and "outConfig->globalFlagValue = (s8)recordContext->globalPatternFlag;" in source_text,
        "runtime_request_default_config_matches_native_global_rewrite": "OOT3D_TITLE_INTRO_FEED_GLOBAL_PROMOTE_MULTIPLIER" in source_text
        and "OOT3D_TITLE_INTRO_FEED_GLOBAL_PROMOTE_COMPARE" in source_text
        and "OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_CFFF_OVERRIDE" in source_text
        and "OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_0FFF_OVERRIDE" in source_text,
        "runtime_request_init_uses_native_config_when_config_null": "const Oot3dTitleIntroRuntimeRequestConfig* requestConfig = config;" in source_text
        and "if (requestConfig == NULL)" in source_text
        and "requestConfig = &nativeConfig;" in source_text,
        "runtime_request_init_sets_selected_flag": "OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_SELECTED_FLAG" in source_text
        and "selector->runtimeFlagsOrRequest =" in source_text,
        "runtime_request_init_sets_scan_fields": "state->requestScanIndex = 0u;" in source_text
        and "state->requestScanLimit =" in source_text
        and "state->requestActiveMask = (u16)(selectedMask & OOT3D_TITLE_INTRO_FEED_RUNTIME_REQUEST_ACTIVE_MASK);" in source_text,
        "runtime_request_init_clears_on_sentinel": "selector->runtimeFlagsOrRequest = 0u;" in source_text
        and "outEvent->runtimeRequestCleared = 1u;" in source_text,
        "runtime_request_unresolved_pattern_branch_is_explicit": "outEvent->runtimeRequestPatternMatcherPending = 1u;" in source_text,
        "runtime_request_history_copy_unresolved_is_explicit": "outEvent->runtimeRequestHistoryCopyPending =" in source_text,
        "runtime_request_advance_implements_0047bd30": "Oot3d_TitleIntroSourceSelectorFeedAdvanceRuntimeRequest" in source_text
        and "state->requestActiveMask" in source_text
        and "Oot3d_TitleIntroSourceSelectorFeedRequestLoadLaneRow" in source_text
        and "state->countdownOverride = (u8)(laneIndex + 1u);" in source_text,
        "sequence_advance_implements_00477a1c": "Oot3d_TitleIntroSourceSelectorFeedAdvanceSequence" in source_text
        and "recordContext->recordBByte1Source" in source_text
        and "state->sequenceLastProgressTime" in source_text
        and "state->recordBLookupSource" in source_text
        and "state->recordBVectorScaleBits" in source_text,
        "sequence_advance_rebinds_record_b_payload_before_scale": ordered(
            source_text,
            "Oot3d_TitleIntroRebindPayload(",
            "Oot3d_TitleIntroDispatchCode7AndScale("
        )
        and "sOot3dTitleIntroRecordBPayloadValue" in source_text
        and "recordBPayloadDispatchEvent" in source_text
        and "recordBScaleDispatchEvent" in source_text,
        "sequence_advance_uses_native_dispatch_gate": "Oot3d_TitleIntroDispatchGateOpen(dispatchState) != 0u" in source_text,
        "record_table_composition_matches_00460878": "Oot3d_TitleIntroSourceSelectorFeedComposeRecordTable" in source_text
        and "table->recordA.byte0 = recordContext->recordSourceByte & OOT3D_TITLE_INTRO_RECORD_C_BYTE0_MASK;" in source_text
        and "table->recordA.byte1 = selector->runtimeFlagsOrRequest == 0u ?" in source_text
        and "state->countdownOverride = 0u;" in source_text
        and "gOot3dTitleIntroCompactLookup[lookupIndex]" in source_text
        and "OOT3D_TITLE_INTRO_FEED_RECORD_B_DEFAULT_SEQUENCE_MODULO" in source_text,
        "frame_advance_snapshots_progress_time": "recordContext->currentProgressTime = state->timeCounter;" in source_text,
        "frame_advance_rolls_gate_when_active_unlatched": "selector->gatePreviousWord = selector->gateCurrentWord;" in source_text
        and "outEvent->frameGateRolledFromSaved = 1u;" in source_text,
        "frame_advance_applies_selector_when_record_b_open": "recordContext->recordBByte1Source == 0u && state->activeFlag == 1u" in source_text
        and "Oot3d_TitleIntroSourceSelectorFeedApplySelector(" in source_text,
        "frame_advance_calls_native_deferred_helpers": "Oot3d_TitleIntroSourceSelectorFeedAdvanceRuntimeRequest(" in source_text
        and "Oot3d_TitleIntroSourceSelectorFeedInitializeRuntimeRequest(" in source_text
        and "Oot3d_TitleIntroSourceSelectorFeedAdvanceSequence(" in source_text
        and "state->sequenceLastProgressTime = recordContext->currentProgressTime;" in source_text,
        "frame_advance_marks_dirty_on_slot_change": "selector->dirtyCountdown = 1u;" in source_text
        and "selector->previousSlotOrId != recordContext->currentSlotOrId" in source_text,
        "frame_advance_latches_previous_slot": "selector->previousSlotOrId = recordContext->currentSlotOrId;" in source_text,
        "frame_advance_increments_time_by_two": "state->timeCounter += OOT3D_TITLE_INTRO_FEED_TIME_INCREMENT;" in source_text,
    }
    return {
        "format": "oot3d_title_intro_source_selector_feed_runtime_audit_v1",
        "inputs": {
            "header": rel(HEADER),
            "source": rel(SOURCE),
            "runtime_table_header": rel(RUNTIME_TABLE_HEADER),
            "runtime_table_audit": rel(RUNTIME_TABLE_AUDIT),
            "owner_audit": rel(OWNER_AUDIT),
            "source_selector_audit": rel(SOURCE_SELECTOR_AUDIT),
        },
        "summary": {
            "ok": all(checks.values()),
            "checks": checks,
            "next_gate": "Promote the direct-state dispatcher helper bindings that consume the completed title-intro record/feed path.",
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
        "# OOT3D Title Intro Source Selector Feed Runtime Audit",
        "",
        "This audit checks that the maintained feed helpers preserve the native owners that populate the 0x002D6798 source selector.",
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
        raise SystemExit("title-intro source-selector feed runtime audit failed")


if __name__ == "__main__":
    main()
