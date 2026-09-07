#!/usr/bin/env python3
"""Audit the title-intro native record-C producer runtime."""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"

HEADER = ROOT / "include" / "oot3d" / "title_intro_record_c_runtime.h"
SOURCE = ROOT / "src" / "code" / "z_title_intro_record_c_runtime.c"
RECORD_POINTER_AUDIT = ANALYSIS / "title_intro_record_pointer_audit.json"
CONTEXT_FEED_AUDIT = ANALYSIS / "title_intro_context_feed_audit.json"
PRIMARY_PRODUCER_AUDIT = ANALYSIS / "title_intro_primary_producer_audit.json"

OUT_JSON = ANALYSIS / "title_intro_record_c_runtime_audit.json"
OUT_MD = ANALYSIS / "title_intro_record_c_runtime_audit.md"


EXPECTED_DEFINES = {
    "OOT3D_TITLE_INTRO_RECORD_CONTEXT_ADDR": "0x0054AC48u",
    "OOT3D_TITLE_INTRO_RECORD_TABLE_ADDR": "0x0054AC9Du",
    "OOT3D_TITLE_INTRO_RECORD_A_ADDR": "0x0054AC9Du",
    "OOT3D_TITLE_INTRO_RECORD_B_ADDR": "0x0054ACA0u",
    "OOT3D_TITLE_INTRO_RECORD_C_ADDR": "0x0054ACA3u",
    "OOT3D_TITLE_INTRO_CONTEXT_CURRENT_SLOT_OFFSET": "0x0016u",
    "OOT3D_TITLE_INTRO_CONTEXT_RECORD_SOURCE_OFFSET": "0x0018u",
    "OOT3D_TITLE_INTRO_CONTEXT_CURRENT_Z_OFFSET": "0x001Au",
    "OOT3D_TITLE_INTRO_CONTEXT_CURRENT_X_OFFSET": "0x001Bu",
    "OOT3D_TITLE_INTRO_CONTEXT_CURRENT_Y_OFFSET": "0x001Cu",
    "OOT3D_TITLE_INTRO_CONTEXT_RECORD_B_BYTE1_SOURCE_OFFSET": "0x001Du",
    "OOT3D_TITLE_INTRO_CONTEXT_MODE_OFFSET": "0x0024u",
    "OOT3D_TITLE_INTRO_CONTEXT_PENDING_HISTORY_INDEX_OFFSET": "0x0025u",
    "OOT3D_TITLE_INTRO_CONTEXT_PREVIOUS_SLOT_OFFSET": "0x0026u",
    "OOT3D_TITLE_INTRO_CONTEXT_PREVIOUS_X_OFFSET": "0x0027u",
    "OOT3D_TITLE_INTRO_CONTEXT_PREVIOUS_Y_OFFSET": "0x0028u",
    "OOT3D_TITLE_INTRO_CONTEXT_PREVIOUS_Z_OFFSET": "0x0029u",
    "OOT3D_TITLE_INTRO_CONTEXT_PREVIOUS_SOURCE_FLAGS_OFFSET": "0x002Au",
    "OOT3D_TITLE_INTRO_CONTEXT_DIRTY_FLAG_OFFSET": "0x002Du",
    "OOT3D_TITLE_INTRO_CONTEXT_RECORD_CURSOR_OFFSET": "0x003Eu",
    "OOT3D_TITLE_INTRO_CONTEXT_LAST_PROGRESS_TIME_OFFSET": "0x00ACu",
    "OOT3D_TITLE_INTRO_CONTEXT_CURRENT_PROGRESS_TIME_OFFSET": "0x00D8u",
    "OOT3D_TITLE_INTRO_RECORD_C_PROGRESS_DELTA_MIN": "2u",
    "OOT3D_TITLE_INTRO_RECORD_C_CURSOR_LIMIT": "8u",
    "OOT3D_TITLE_INTRO_RECORD_C_MODE_2": "2u",
    "OOT3D_TITLE_INTRO_RECORD_C_MODE_MATCH": "0xFFu",
    "OOT3D_TITLE_INTRO_RECORD_C_EMPTY_SLOT": "0xFFu",
    "OOT3D_TITLE_INTRO_RECORD_C_BYTE0_MASK": "0x3Fu",
    "OOT3D_TITLE_INTRO_RECORD_C_HISTORY_CAPACITY": "0x6Bu",
    "OOT3D_TITLE_INTRO_RECORD_C_HISTORY_SCAN_LIMIT": "0x10u",
    "OOT3D_TITLE_INTRO_RECORD_C_PATTERN_WINDOW_COUNT": "8u",
    "OOT3D_TITLE_INTRO_RECORD_C_PATTERN_NONE": "0xFFu",
    "OOT3D_TITLE_INTRO_RECORD_C_SOURCE_FLAGS_HI2_MASK": "0xC0u",
    "OOT3D_TITLE_INTRO_RECORD_C_GLOBAL_PATTERN_MATCH_VALUE": "0xFFu",
}

EXPECTED_FUNCTIONS = [
    "Oot3d_TitleIntroRecordCContextInit",
    "Oot3d_TitleIntroRecordTableInit",
    "Oot3d_TitleIntroRecordCFrameUpdate",
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
    defines = defines_from_header(header_text)
    record_pointer = read_json(RECORD_POINTER_AUDIT)
    context_feed = read_json(CONTEXT_FEED_AUDIT)
    primary_producer = read_json(PRIMARY_PRODUCER_AUDIT)
    checks = {
        **{f"define_{name}": defines.get(name) == value for name, value in EXPECTED_DEFINES.items()},
        **{f"function_{function}": function in header_text and function in source_text for function in EXPECTED_FUNCTIONS},
        "record_pointer_audit_ok": bool(record_pointer["summary"]["ok"]),
        "context_feed_audit_ok": bool(context_feed["summary"]["ok"]),
        "primary_producer_audit_ok": bool(primary_producer["summary"]["ok"]),
        "record_table_init_matches_00477c90": "table->recordA.byte0 = OOT3D_TITLE_INTRO_RECORD_C_EMPTY_SLOT" in source_text
        and "table->recordC.byte1 = OOT3D_TITLE_INTRO_RECORD_C_EMPTY_SLOT" in source_text,
        "context_owns_mutable_history_buffers": "historyB0Rows[OOT3D_TITLE_INTRO_RECORD_C_HISTORY_CAPACITY]" in header_text
        and "historyB4Rows[OOT3D_TITLE_INTRO_RECORD_C_HISTORY_CAPACITY]" in header_text
        and "historyPatternWindow[OOT3D_TITLE_INTRO_RECORD_C_PATTERN_WINDOW_COUNT]" in header_text,
        "history_buffers_seed_from_native_tables": "gOot3dTitleIntroHistoryMode1Records" in source_text
        and "gOot3dTitleIntroFallbackZeroRun" in source_text
        and "context->historyB0Rows[0].previousSlot = OOT3D_TITLE_INTRO_RECORD_C_EMPTY_SLOT;" in source_text,
        "progression_gate_matches_00476d44": "context->modeByte == 0u" in source_text
        and "progressDelta <= OOT3D_TITLE_INTRO_RECORD_C_PROGRESS_DELTA_MIN" in source_text,
        "stable_sample_returns_before_commit": "outEvent->stablePreviousSample = 1;\n            }\n            return;" in source_text,
        "record_c_byte0_source_masked": "table->recordC.byte0 = context->recordSourceByte & OOT3D_TITLE_INTRO_RECORD_C_BYTE0_MASK" in source_text,
        "cursor_limit_forces_mode2_commit": "context->cursorCount > OOT3D_TITLE_INTRO_RECORD_C_CURSOR_LIMIT" in source_text
        and "context->modeByte == OOT3D_TITLE_INTRO_RECORD_C_MODE_2" in source_text,
        "history_commit_writes_native_record_layout": "historyRows[pendingIndexBefore].previousSlot = context->previousSlot;" in source_text
        and "historyRows[pendingIndexBefore].durationDelta = durationDelta;" in source_text
        and "historyRows[pendingIndexBefore].sourceFlagsHi2 =" in source_text,
        "history_compaction_matches_002cfe34_tail_gate": "Oot3d_TitleIntroRecordCCompactHistoryTail" in source_text
        and "pendingIndexAfter != OOT3D_TITLE_INTRO_RECORD_C_HISTORY_CAPACITY && forced == 0u" in source_text
        and "historyRows[scanIndex + 1u].durationDelta = 0;" in source_text,
        "mode2_pattern_scan_uses_native_tables": "Oot3d_TitleIntroRecordCScanMode2Patterns" in source_text
        and "gOot3dTitleIntroCompactLookup[slot]" in source_text
        and "gOot3dTitleIntroPatternRecords[patternIndex]" in source_text,
        "pattern_match_sets_mode_ff_and_global_flag": "context->modeByte = OOT3D_TITLE_INTRO_RECORD_C_MODE_MATCH;" in source_text
        and "context->globalPatternFlag = OOT3D_TITLE_INTRO_RECORD_C_GLOBAL_PATTERN_MATCH_VALUE;" in source_text,
        "unmatched_pattern_requests_active_clear": "context->historyCompactionActiveClearRequested = 1u;" in source_text
        and "outEvent->activeClearRequested = 1u;" in source_text,
        "normal_commit_updates_last_progress_time": ordered(
            source_text,
            "Oot3d_TitleIntroRecordCHistoryCommit(context, 0u",
            "context->lastProgressTime = context->currentProgressTime;"
        ),
        "frame_update_writes_record_c_byte1_byte2": "table->recordC.byte1 = context->modeByte;" in source_text
        and "table->recordC.byte2 = context->cursorCount;" in source_text,
        "frame_update_resets_mode_ff": "context->modeByte == OOT3D_TITLE_INTRO_RECORD_C_MODE_MATCH" in source_text
        and "context->modeByte = 0u;" in source_text,
    }
    return {
        "format": "oot3d_title_intro_record_c_runtime_audit_v1",
        "inputs": {
            "header": rel(HEADER),
            "source": rel(SOURCE),
            "record_pointer_audit": rel(RECORD_POINTER_AUDIT),
            "context_feed_audit": rel(CONTEXT_FEED_AUDIT),
            "primary_producer_audit": rel(PRIMARY_PRODUCER_AUDIT),
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
        "# OOT3D Title Intro Record C Runtime Audit",
        "",
        "This audit checks that the maintained record-C producer preserves the native writer evidence for the title-intro runtime record consumed by state 37.",
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
        raise SystemExit("title-intro record-C runtime audit failed")


if __name__ == "__main__":
    main()
