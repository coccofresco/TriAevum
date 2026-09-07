#!/usr/bin/env python3
"""Audit the title-intro cutscene runtime adapter."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"

HEADER = ROOT / "include" / "oot3d" / "title_intro_cutscene_runtime.h"
SOURCE = ROOT / "src" / "code" / "z_title_intro_cutscene_runtime.c"
OPENING_FRAME_HEADER = ROOT / "include" / "oot3d" / "title_intro_opening_frame_runtime.h"
OPENING_FRAME_SOURCE = ROOT / "src" / "code" / "z_title_intro_opening_frame_runtime.c"
PLAYBACK_AUDIT = ANALYSIS / "title_intro_playback_runtime_audit.json"
RECORD_C_AUDIT = ANALYSIS / "title_intro_record_c_runtime_audit.json"
SOURCE_SELECTOR_AUDIT = ANALYSIS / "title_intro_source_selector_runtime_audit.json"
SOURCE_SELECTOR_FEED_AUDIT = ANALYSIS / "title_intro_source_selector_feed_runtime_audit.json"
SOURCE_TABLE = ANALYSIS / "title_intro_source_table.json"

OUT_JSON = ANALYSIS / "title_intro_cutscene_runtime_audit.json"
OUT_MD = ANALYSIS / "title_intro_cutscene_runtime_audit.md"


EXPECTED_FUNCTIONS = [
    "Oot3d_TitleIntroCutsceneRuntimeInit",
    "Oot3d_TitleIntroCutsceneRuntimeCollectActiveState37Rows",
    "Oot3d_TitleIntroCutsceneRuntimeGetRecordCContext",
    "Oot3d_TitleIntroCutsceneRuntimeGetSourceSelectorFeed",
    "Oot3d_TitleIntroCutsceneRuntimeGetSourceSelector",
    "Oot3d_TitleIntroCutsceneRuntimeStepState37",
    "Oot3d_TitleIntroCutsceneRuntimeStepState37FromContext",
    "Oot3d_TitleIntroCutsceneRuntimeStepState37FromSelector",
    "Oot3d_TitleIntroCutsceneRuntimeStepState37FromFeed",
]


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def rel(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


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
    opening_frame_header_text = OPENING_FRAME_HEADER.read_text(encoding="utf-8")
    opening_frame_source_text = OPENING_FRAME_SOURCE.read_text(encoding="utf-8")
    playback_audit = read_json(PLAYBACK_AUDIT)
    record_c_audit = read_json(RECORD_C_AUDIT)
    source_selector_audit = read_json(SOURCE_SELECTOR_AUDIT)
    source_selector_feed_audit = read_json(SOURCE_SELECTOR_FEED_AUDIT)
    source_table = read_json(SOURCE_TABLE)
    link_boy_rows = source_table["title_link_boy_player_action_rows"]
    cue37_rows = [row for row in link_boy_rows if int(row["cue_id"]) == 37]
    checks = {
        "playback_runtime_audit_ok": bool(playback_audit["summary"]["ok"]),
        "record_c_runtime_audit_ok": bool(record_c_audit["summary"]["ok"]),
        "source_selector_runtime_audit_ok": bool(source_selector_audit["summary"]["ok"]),
        "source_selector_feed_runtime_audit_ok": bool(source_selector_feed_audit["summary"]["ok"]),
        "title_link_boy_rows_decoded": bool(source_table["summary"]["title_link_boy_player_action_decoded"]),
        "title_link_boy_cue37_row_count_is_5": len(cue37_rows) == 5,
        "header_includes_record_c_runtime": '#include "oot3d/title_intro_record_c_runtime.h"' in header_text,
        "header_includes_playback_runtime": '#include "oot3d/title_intro_playback_runtime.h"' in header_text,
        "header_includes_source_selector_feed_runtime": '#include "oot3d/title_intro_source_selector_feed_runtime.h"' in header_text,
        "header_includes_source_selector_runtime": '#include "oot3d/title_intro_source_selector_runtime.h"' in header_text,
        "header_includes_source_table": '#include "oot3d/title_intro_source_table.h"' in header_text,
        **{
            f"function_{function}": function in header_text and function in source_text
            for function in EXPECTED_FUNCTIONS
        },
        "collects_from_native_title_link_boy_rows": "gOot3dTitleIntroLinkBoyPlayerActionRows" in source_text,
        "defines_qdb_any_sentinel": "OOT3D_TITLE_INTRO_STATE37_QDB_ANY 0xFFFFu" in header_text,
        "exposes_qdb_filtered_collect_api": "Oot3d_TitleIntroCutsceneRuntimeCollectActiveState37RowsForQdb" in header_text
        and "Oot3d_TitleIntroCutsceneRuntimeCollectActiveState37RowsForQdb" in source_text,
        "state37_step_records_qdb_filter": "qdbIndexFiltered" in header_text
        and "outStep->qdbIndexFiltered = qdbIndex != OOT3D_TITLE_INTRO_STATE37_QDB_ANY ? 1u : 0u;" in source_text,
        "filters_state37_rows_by_qdb_when_bound": "row->qdbIndex != qdbIndex" in source_text
        and "qdbIndex != OOT3D_TITLE_INTRO_STATE37_QDB_ANY" in source_text,
        "filters_cue37_with_native_constant": "row->cueId != (s16)OOT3D_TITLE_INTRO_DIRECT_STATE_37" in source_text,
        "uses_native_qdb_row_frame_window": "(s32)row->startFrame <= frame && frame < (s32)row->endFrame" in source_text,
        "collects_rows_before_state37_step": ordered(
            source_text,
            "Oot3d_TitleIntroCutsceneRuntimeCollectActiveState37Rows(",
            "Oot3d_TitleIntroState37Step("
        ),
        "calls_state37_step_with_record_c": "Oot3d_TitleIntroState37Step(" in source_text
        and "recordC," in source_text,
        "from_context_produces_record_c_before_playback": ordered(
            source_text,
            "recordCStatus = Oot3d_TitleIntroRecordCFrameUpdate(",
            "playbackStatus = Oot3d_TitleIntroCutsceneRuntimeStepState37ForQdb("
        ),
        "from_context_uses_produced_record_c_bytes": "recordCBytes[0] = state->nativeRecordTable.recordC.byte0;" in source_text
        and "recordCBytes[2] = state->nativeRecordTable.recordC.byte2;" in source_text,
        "state_owns_source_selector": "Oot3dTitleIntroSourceSelectorState sourceSelector;" in header_text,
        "state_owns_source_selector_feed": "Oot3dTitleIntroSourceSelectorFeedState sourceSelectorFeed;" in header_text,
        "init_initializes_source_selector": "Oot3d_TitleIntroSourceSelectorInit(&state->sourceSelector);" in source_text,
        "init_initializes_source_selector_feed": "Oot3d_TitleIntroSourceSelectorFeedInit(&state->sourceSelectorFeed);" in source_text,
        "exposes_source_selector_feed_state": "return &state->sourceSelectorFeed;" in source_text,
        "exposes_source_selector_state": "return &state->sourceSelector;" in source_text,
        "from_selector_applies_selector_before_record_c": ordered(
            source_text,
            "selectorStatus = Oot3d_TitleIntroSourceSelectorApply(",
            "playbackStatus = Oot3d_TitleIntroCutsceneRuntimeProduceRecordCAndStepState37("
        ),
        "from_selector_passes_dispatch_state_to_selector": "dispatchState," in source_text
        and "Oot3d_TitleIntroSourceSelectorApply(" in source_text,
        "from_selector_records_selector_event": "outStep->sourceSelectorEvent = selectorEvent;" in source_text,
        "from_feed_advances_feed_before_record_c": ordered_after(
            source_text,
            "Oot3d_TitleIntroCutsceneRuntimeStepState37FromFeed",
            "feedStatus = Oot3d_TitleIntroSourceSelectorFeedFrameAdvance(",
            "playbackStatus = Oot3d_TitleIntroCutsceneRuntimeProduceRecordCAndStepState37("
        ),
        "from_feed_composes_record_table": "Oot3d_TitleIntroSourceSelectorFeedComposeRecordTable(" in source_text
        and "&state->nativeRecordTable" in source_text
        and "&state->sourceSelectorFeed" in source_text
        and "&state->sourceSelector" in source_text,
        "from_feed_propagates_record_c_active_clear": "recordCEvent.historyCommit.activeClearRequested != 0u" in source_text
        and "feedState->activeFlag = 0u;" in source_text,
        "from_feed_records_feed_event": "outStep->sourceSelectorFeedEvent = feedEvent;" in source_text,
        "does_not_apply_when_no_active_cue37": "if (activeCount == 0)" in source_text
        and "return OOT3D_TITLE_INTRO_PLAYBACK_OK;" in source_text,
        "opening_frame_header_includes_cutscene_runtime": '#include "oot3d/title_intro_cutscene_runtime.h"' in opening_frame_header_text,
        "opening_frame_state_owns_cutscene_runtime": "Oot3dTitleIntroCutsceneRuntimeState cutsceneRuntime;" in opening_frame_header_text,
        "opening_frame_state_owns_dispatch_state": "Oot3dTitleIntroDispatchState dispatchState;" in opening_frame_header_text,
        "opening_frame_state_owns_record_byte_table": "recordByteTableBytes[OOT3D_TITLE_INTRO_OPENING_FRAME_RUNTIME_RECORD_BYTE_TABLE_CAPACITY]" in opening_frame_header_text,
        "opening_frame_init_initializes_cutscene_runtime": "Oot3d_TitleIntroCutsceneRuntimeInit(" in opening_frame_source_text,
        "opening_frame_init_initializes_dispatch_state": "Oot3d_TitleIntroDispatchStateInit(&state->dispatchState" in opening_frame_source_text,
        "opening_frame_direct_mode_open_uses_mode2_constant": "Oot3d_TitleIntroSourceSelectorFeedSetMode(" in opening_frame_source_text
        and "OOT3D_TITLE_INTRO_RECORD_C_MODE_2" in opening_frame_source_text,
        "opening_frame_direct_mode_gate_uses_layout_entry_state": "cue->directStateId == layout->entryDirectStateId" in opening_frame_source_text,
        "opening_frame_steps_feed_with_qdb_filter": "Oot3d_TitleIntroCutsceneRuntimeStepState37FromFeedForQdb" in opening_frame_source_text
        and "outStep->cutsceneQdbIndex" in opening_frame_source_text,
        "opening_frame_resets_mode_when_state37_requests": "requestModeResetToZero" in opening_frame_source_text
        and "directModeResetStatus = Oot3d_TitleIntroSourceSelectorFeedSetMode" in opening_frame_source_text,
    }
    return {
        "format": "oot3d_title_intro_cutscene_runtime_audit_v1",
        "inputs": {
            "header": rel(HEADER),
            "source": rel(SOURCE),
            "opening_frame_header": rel(OPENING_FRAME_HEADER),
            "opening_frame_source": rel(OPENING_FRAME_SOURCE),
            "playback_runtime_audit": rel(PLAYBACK_AUDIT),
            "record_c_runtime_audit": rel(RECORD_C_AUDIT),
            "source_selector_runtime_audit": rel(SOURCE_SELECTOR_AUDIT),
            "source_selector_feed_runtime_audit": rel(SOURCE_SELECTOR_FEED_AUDIT),
            "source_table": rel(SOURCE_TABLE),
        },
        "summary": {
            "ok": all(checks.values()),
            "checks": checks,
            "title_link_boy_cue37_row_count": len(cue37_rows),
            "next_gate": "Drive title-intro direct state 38/39 side effects from the decoded state-37 playback requests.",
        },
        "cue37_rows": [
            {
                "player_action_index": row["player_action_index"],
                "qdb_index": row["qdb_index"],
                "cue_index": row["cue_index"],
                "start_frame": row["start_frame"],
                "end_frame": row["end_frame"],
                "qdb_embedded_name": row["qdb_embedded_name"],
            }
            for row in cue37_rows
        ],
    }


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def write_json(path: Path, data: Any) -> None:
    write_text(path, json.dumps(data, indent=2) + "\n")


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    lines = [
        "# OOT3D Title Intro Cutscene Runtime Audit",
        "",
        "This audit checks that the title-intro cutscene runtime adapter binds native Link-boy cue 37 rows to the decompiled state-37 playback step and that the opening frame runtime advances that path through the QDB-filtered feed bridge.",
        "",
        "## Summary",
        "",
        f"- OK: {data['summary']['ok']}",
        f"- Cue 37 rows: {data['summary']['title_link_boy_cue37_row_count']}",
        f"- Next gate: {data['summary']['next_gate']}",
        "",
        "## Checks",
        "",
        "| Check | Status |",
        "| --- | --- |",
    ]
    for key, value in data["summary"]["checks"].items():
        lines.append(f"| `{key}` | `{value}` |")

    lines.extend(["", "## Cue 37 Rows", "", "| Player Action | QDB | Cue | Frames | QDB Name |", "| ---: | ---: | ---: | --- | --- |"])
    for row in data["cue37_rows"]:
        lines.append(
            f"| `{row['player_action_index']}` | `{row['qdb_index']}` | `{row['cue_index']}` | "
            f"`{row['start_frame']}..{row['end_frame']}` | `{row['qdb_embedded_name']}` |"
        )
    write_text(path, "\n".join(lines) + "\n")


def main() -> None:
    data = build_report()
    write_json(OUT_JSON, data)
    write_markdown(OUT_MD, data)
    if not data["summary"]["ok"]:
        raise SystemExit("title-intro cutscene runtime audit failed")


if __name__ == "__main__":
    main()
