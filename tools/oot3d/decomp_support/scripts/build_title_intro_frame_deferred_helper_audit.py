#!/usr/bin/env python3
"""Audit deferred native helpers called by the title-intro frame advance path."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
DECOMPILED = ANALYSIS / "title_intro_context_feed_producer_ghidra_export" / "decompiled"

FEED_AUDIT = ANALYSIS / "title_intro_source_selector_feed_runtime_audit.json"
RUNTIME_TABLE_AUDIT = ANALYSIS / "title_intro_runtime_tables.json"
POINTER_AUDIT = ANALYSIS / "title_intro_deferred_table_pointer_audit.json"
FEED_SOURCE = ROOT / "src" / "code" / "z_title_intro_source_selector_feed_runtime.c"
DECOMP_0047AFB0 = DECOMPILED / "99012_0047afb0_FUN_0047afb0.c"
DECOMP_0047BD30 = DECOMPILED / "99013_0047bd30_FUN_0047bd30.c"
DECOMP_00477A1C = DECOMPILED / "99010_00477a1c_FUN_00477a1c.c"
DECOMP_0033F248 = DECOMPILED / "99005_0033f248_FUN_0033f248.c"

OUT_JSON = ANALYSIS / "title_intro_frame_deferred_helper_audit.json"
OUT_MD = ANALYSIS / "title_intro_frame_deferred_helper_audit.md"


FIELD_ROWS = [
    {
        "helper": "0047afb0",
        "role": "runtime_request_initializer_or_pattern_matcher",
        "fields": "+0x94;+0x3c;+0x3d;+0x50;+0x2c;+0x3b;+0x2b;+0x3e;+0x14;+0x46;+0x3a",
        "meaning": "Initializes request scan state from runtimeFlagsOrRequest, or matches recent slot history when the alternate 0x4000 route is inactive.",
    },
    {
        "helper": "0047bd30",
        "role": "runtime_request_advance",
        "fields": "+0x16;+0x17;+0x19;+0x1a;+0x2b;+0x2c;+0x3b;+0x3c;+0x3d;+0x3e;+0x50;+0x14;+0x94;+0x46",
        "meaning": "Advances active request bit masks and timing rows, can set countdownOverride and clear active/runtime flags on completion.",
    },
    {
        "helper": "0033f248",
        "role": "sequence_source_bind",
        "fields": "+0xa8;+0x1d;+0x98;+0x1e;+0x42;+0x44",
        "meaning": "Selects a sequence source pointer, seeds record-B source state, and skips leading sentinel rows.",
    },
    {
        "helper": "00477a1c",
        "role": "sequence_advance",
        "fields": "+0x1d;+0x44;+0xd8;+0xa4;+0x98;+0xa8;+0x42;+0x1f;+0xa0;+0x20;+0x21;+0x9c;+0x1e",
        "meaning": "Consumes sequence rows over frame deltas, dispatches record-B changes, updates vector scale, and resets sequence state on completion.",
    },
]

CHECKS = {
    "feed_runtime_audit_ok": (FEED_AUDIT, []),
    "runtime_tables_promote_deferred_sequence_lanes": (RUNTIME_TABLE_AUDIT, []),
    "deferred_pointer_audit_ok": (POINTER_AUDIT, []),
    "0047afb0_requires_gate_word_bit4_and_selector_bits": (
        DECOMP_0047AFB0,
        ["+ 0xdc) & 4", "+ 0xdc) & 0xf80"],
    ),
    "0047afb0_derives_runtime_request_low16": (
        DECOMP_0047AFB0,
        ["+ 0x94) & 0xffff"],
    ),
    "0047afb0_initializes_request_mask_and_bounds": (
        DECOMP_0047AFB0,
        ["+ 0x3c) = 0", "+ 0x3d) = 0xe", "+ 0x50) = (ushort)uVar9 & 0x3fff"],
    ),
    "0047afb0_sets_active_and_clears_override_fields": (
        DECOMP_0047AFB0,
        ["+ 0x2b) = 0", "+ 0x14) = 1", "+ 0x46) = 0"],
    ),
    "0047afb0_fallback_clears_runtime_flags": (
        DECOMP_0047AFB0,
        ["+ 0x94) = 0", "+ 0x14) = 0"],
    ),
    "0047afb0_pattern_history_match_sets_override": (
        DECOMP_0047AFB0,
        ["+ 0x2b) = (char)uVar8 + '\\x01'", "+ 0x14) = 0", "+ 0x94) = 0"],
    ),
    "0047bd30_starts_request_window_on_new_slot": (
        DECOMP_0047BD30,
        ["+ 0x3b) = 1", "+ 0x19) = 0xff"],
    ),
    "0047bd30_uses_previous_current_slot_delta": (
        DECOMP_0047BD30,
        ["+ 0x17) != cVar1", "+ 0x16"],
    ),
    "0047bd30_advances_active_mask_rows": (
        DECOMP_0047BD30,
        ["+ 0x50", "uVar19 * 0xa0", "*puVar14 + 1"],
    ),
    "0047bd30_completion_sets_override_and_clears_flags": (
        DECOMP_0047BD30,
        ["+ 0x2b) = (char)uVar19 + '\\x01'", "+ 0x14) = 0", "+ 0x94) = 0"],
    ),
    "0047bd30_all_masks_clear_can_latch_request_code": (
        DECOMP_0047BD30,
        ["+ 0x50) == 0", "+ 0x46) = (short)*(uint *)(iVar7 + 0x94)"],
    ),
    "0047bd30_implemented_in_feed_runtime": (
        FEED_SOURCE,
        [
            "Oot3d_TitleIntroSourceSelectorFeedAdvanceRuntimeRequest",
            "state->requestActiveMask",
            "Oot3d_TitleIntroSourceSelectorFeedRequestLoadLaneRow",
            "state->countdownOverride = (u8)(laneIndex + 1u);",
        ],
    ),
    "0033f248_binds_sequence_source_and_resets_state": (
        DECOMP_0033F248,
        ["+ 0xa8) = iVar5", "+ 0x1d) = param_2", "+ 0x98) = 0", "+ 0x1e) = 0xff"],
    ),
    "0033f248_skips_leading_sentinel_rows": (
        DECOMP_0033F248,
        ["while (cVar1 == -1)", "+ 0x42) = uVar4"],
    ),
    "00477a1c_requires_sequence_countdown": (
        DECOMP_00477A1C,
        ["+ 0x1d", "if (cVar1 != '\\0')"],
    ),
    "00477a1c_uses_frame_delta_when_record_b_slot_active": (
        DECOMP_00477A1C,
        ["+ 0xd8) - *(int *)(DAT_00477c74 + 0xa4)", "+ 0x44) != 0"],
    ),
    "00477a1c_loads_sequence_row_and_updates_timer": (
        DECOMP_00477A1C,
        ["+ 0xa8) + (uint)*(ushort *)(iVar4 + 0x42) * 8", "+ 0x98) = uVar5"],
    ),
    "00477a1c_updates_record_b_dispatch_fields": (
        DECOMP_00477A1C,
        ["+ 0x1f", "+ 0x20", "+ 0x21", "+ 0x1e"],
    ),
    "00477a1c_dispatches_record_b_changes": (
        DECOMP_00477A1C,
        ["FUN_002cfd74", "FUN_002cfd24", "FUN_002cfcf0", "FUN_002cfe00"],
    ),
    "00477a1c_resets_sequence_on_completion": (
        DECOMP_00477A1C,
        ["+ 0x42) = 0", "+ 0x44) = 0", "+ 0x1e) = 0xff"],
    ),
    "00477a1c_implemented_in_feed_runtime": (
        FEED_SOURCE,
        [
            "Oot3d_TitleIntroSourceSelectorFeedAdvanceSequence",
            "state->sequenceLastProgressTime",
            "state->recordBLookupSource",
            "state->recordBVectorScaleBits",
        ],
    ),
    "00460878_record_table_composition_implemented": (
        FEED_SOURCE,
        [
            "Oot3d_TitleIntroSourceSelectorFeedComposeRecordTable",
            "table->recordA.byte0",
            "table->recordB.byte0",
            "gOot3dTitleIntroCompactLookup",
        ],
    ),
}


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def rel(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def line_for(path: Path, fragment: str) -> int:
    for index, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        if fragment in line:
            return index
    return 0


def build_report() -> dict[str, Any]:
    checks: dict[str, bool] = {}
    evidence: dict[str, list[dict[str, Any]]] = {}
    for name, (path, fragments) in CHECKS.items():
        if path.suffix == ".json":
            json_data = read_json(path)
            checks[name] = bool(json_data["summary"]["ok"])
            if name == "runtime_tables_promote_deferred_sequence_lanes":
                checks[name] = checks[name] and bool(
                    json_data["summary"]["checks"].get("deferred_sequence_lane_count")
                ) and bool(json_data["summary"]["checks"].get("deferred_sequence_row_count"))
            evidence[name] = [{"path": rel(path), "fragment": "summary.ok", "line": 0}]
            continue
        text = path.read_text(encoding="utf-8")
        checks[name] = all(fragment in text for fragment in fragments)
        evidence[name] = [
            {
                "path": rel(path),
                "fragment": fragment,
                "line": line_for(path, fragment),
            }
            for fragment in fragments
        ]

    return {
        "format": "oot3d_title_intro_frame_deferred_helper_audit_v1",
        "inputs": {
            "feed_runtime_audit": rel(FEED_AUDIT),
            "runtime_table_audit": rel(RUNTIME_TABLE_AUDIT),
            "pointer_audit": rel(POINTER_AUDIT),
            "feed_source": rel(FEED_SOURCE),
            "decompiled": [
                rel(DECOMP_0047AFB0),
                rel(DECOMP_0047BD30),
                rel(DECOMP_00477A1C),
                rel(DECOMP_0033F248),
            ],
        },
        "summary": {
            "ok": all(checks.values()),
            "checks": checks,
            "next_gate": "Promote the direct-state dispatcher helper bindings that consume the completed title-intro record/feed path.",
        },
        "field_rows": FIELD_ROWS,
        "evidence": evidence,
    }


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def write_json(path: Path, data: Any) -> None:
    write_text(path, json.dumps(data, indent=2) + "\n")


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    lines = [
        "# OOT3D Title Intro Frame Deferred Helper Audit",
        "",
        "This audit maps the deferred native helpers reached from 0x00460878 after the source-selector feed path.",
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

    lines.extend(["", "## Helper Roles", "", "| Helper | Role | Fields | Meaning |", "| --- | --- | --- | --- |"])
    for row in data["field_rows"]:
        lines.append(f"| `{row['helper']}` | `{row['role']}` | `{row['fields']}` | {row['meaning']} |")

    lines.extend(["", "## Evidence", "", "| Check | File:Line | Fragment |", "| --- | --- | --- |"])
    for key, rows in data["evidence"].items():
        for row in rows:
            lines.append(f"| `{key}` | `{row['path']}:{row['line']}` | `{row['fragment']}` |")
    write_text(path, "\n".join(lines) + "\n")


def main() -> None:
    data = build_report()
    write_json(OUT_JSON, data)
    write_markdown(OUT_MD, data)
    if not data["summary"]["ok"]:
        raise SystemExit("title-intro frame deferred helper audit failed")


if __name__ == "__main__":
    main()
