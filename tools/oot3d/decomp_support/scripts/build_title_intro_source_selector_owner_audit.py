#!/usr/bin/env python3
"""Audit native owners that populate the title-intro source selector state."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
DECOMPILED = ANALYSIS / "title_intro_context_feed_producer_ghidra_export" / "decompiled"

CONTEXT_FEED_AUDIT = ANALYSIS / "title_intro_context_feed_audit.json"
SOURCE_SELECTOR_AUDIT = ANALYSIS / "title_intro_source_selector_runtime_audit.json"

DECOMP_00422298 = DECOMPILED / "99007_00422298_FUN_00422298.c"
DECOMP_003523DC = DECOMPILED / "99006_003523dc_FUN_003523dc.c"
DECOMP_00460878 = DECOMPILED / "99008_00460878_FUN_00460878.c"
DECOMP_002D0264 = DECOMPILED / "99001_002d0264_FUN_002d0264.c"

OUT_JSON = ANALYSIS / "title_intro_source_selector_owner_audit.json"
OUT_MD = ANALYSIS / "title_intro_source_selector_owner_audit.md"


FIELD_ROWS = [
    {
        "field": "gate_saved_word_source",
        "offset": "+0xD4",
        "writers": ["00422298"],
        "consumer": "003523dc;00460878",
        "meaning": "Captured gate word copied into gate_current_word when the global gate opens or a new active frame latches.",
    },
    {
        "field": "latched_vector_x",
        "offset": "+0x4A",
        "writers": ["00422298"],
        "consumer": "003523dc;00460878",
        "meaning": "Signed source vector X clamped to the selector input byte before 0x002D6798 reads it.",
    },
    {
        "field": "latched_vector_y",
        "offset": "+0x4C",
        "writers": ["00422298"],
        "consumer": "003523dc;00460878",
        "meaning": "Signed source vector Y clamped to the selector input byte before 0x002D6798 reads it.",
    },
    {
        "field": "global_gate_byte",
        "offset": "+0x15",
        "writers": ["003523dc"],
        "consumer": "002d6798;00460878;00477a1c",
        "meaning": "High-level gate selector; changes initialize or clear the source-selector gate state.",
    },
    {
        "field": "latched_gate_flag",
        "offset": "+0x13",
        "writers": ["002d6798", "003523dc", "00460878"],
        "consumer": "003523dc;00460878",
        "meaning": "Prevents repeated copying of saved gate/vector into the active selector in a single gate window.",
    },
    {
        "field": "active_flag",
        "offset": "+0x14",
        "writers": ["002cfe34", "002d0264", "0033704c", "003523dc", "0047bd30"],
        "consumer": "00460878",
        "meaning": "Enables per-frame selector application and record-C production while the direct title cue is active.",
    },
    {
        "field": "gate_current_word",
        "offset": "+0xDC",
        "writers": ["003523dc", "00460878"],
        "consumer": "002d6798;003523dc;00460878;0047afb0",
        "meaning": "Primary selector bitfield masked by 0x0000FFFC in 0x002D6798.",
    },
    {
        "field": "gate_stable_word",
        "offset": "+0xE0",
        "writers": ["003523dc"],
        "consumer": "002d6798",
        "meaning": "Stable gate snapshot used by 0x002D6798 to short-circuit unchanged 0xF80 selector bits.",
    },
    {
        "field": "gate_previous_word",
        "offset": "+0xE4",
        "writers": ["003523dc", "00460878"],
        "consumer": "002d6798",
        "meaning": "Previous selector bitfield used by 0x002D6798 to derive newly active bits.",
    },
    {
        "field": "active_selector_word",
        "offset": "+0xE8",
        "writers": ["002d6798", "003523dc"],
        "consumer": "002d6798",
        "meaning": "Latched active selector bit used for priority slot/source selection.",
    },
    {
        "field": "runtime_flags_or_request",
        "offset": "+0x94",
        "writers": ["0033704c", "003371d8", "003523dc", "0047bd30"],
        "consumer": "002d6798;00460878;0047afb0;0047bd30",
        "meaning": "Request/flag word that gates dirty countdown, follow-up helpers, and slot-change dirty marking.",
    },
    {
        "field": "previous_slot_or_id",
        "offset": "+0x17",
        "writers": ["00460878"],
        "consumer": "002d6798;00460878;0047bd30",
        "meaning": "Last selector slot/id compared against the current slot for dispatch and dirty flag behavior.",
    },
    {
        "field": "dirty_flag",
        "offset": "+0x2D",
        "writers": ["002d6798", "00460878", "00477c90"],
        "consumer": "002d6798",
        "meaning": "Countdown/dirty byte decremented by 0x002D6798 when runtime flags are nonzero.",
    },
    {
        "field": "current_progress_time",
        "offset": "+0xD8",
        "writers": ["00460878"],
        "consumer": "002cfe34;002d0264;00460878;00476d44;00477a1c",
        "meaning": "Per-frame time snapshot consumed by record-C progression and history commit.",
    },
    {
        "field": "time_counter",
        "offset": "+0xBC",
        "writers": ["00460878"],
        "consumer": "00460878",
        "meaning": "Frame counter incremented by 2 after record table writes.",
    },
]


CHECKS = {
    "00422298_captures_gate_word": (DECOMP_00422298, ["+ 0xd4) = param_1"]),
    "00422298_captures_vector_pair": (DECOMP_00422298, ["+ 0x4a) = *param_2", "+ 0x4c) = param_2[1]"]),
    "003523dc_writes_global_gate_byte": (DECOMP_003523DC, ["+ 0x15) = (char)param_1"]),
    "003523dc_initializes_gate_current_from_saved": (DECOMP_003523DC, ["+ 0xdc) = *(undefined4 *)(iVar2 + 0xd4)"]),
    "003523dc_clears_previous_gate_on_open": (DECOMP_003523DC, ["+ 0xe4) = 0"]),
    "003523dc_clamps_vectors_to_input_prefix": (DECOMP_003523DC, ["*puVar4 = 0x40", "puVar4[1] = 0x40"]),
    "003523dc_stable_snapshot_for_selector": (DECOMP_003523DC, ["+ 0xe0) = *(undefined4 *)(iVar2 + 0xdc)"]),
    "003523dc_close_gate_calls_selector_and_clears_runtime": (DECOMP_003523DC, ["FUN_002d6798(0)", "+ 0x94) = 0"]),
    "00460878_snapshots_current_progress_time": (DECOMP_00460878, ["+ 0xd8) = *(undefined4 *)(DAT_00460a74 + 0xbc)"]),
    "00460878_rolls_gate_previous_to_current": (DECOMP_00460878, ["+ 0xe4) = *(undefined4 *)(iVar3 + 0xdc)", "+ 0xdc) = *(undefined4 *)(iVar3 + 0xd4)"]),
    "00460878_clamps_vectors_to_input_prefix": (DECOMP_00460878, ["*DAT_00460a78 = 0x40", "puVar4[1] = 0x40"]),
    "00460878_applies_selector_when_record_b_open": (DECOMP_00460878, ["+ 0x1d) == '\\0' && cVar1 == '\\x01'", "FUN_002d6798(0)"]),
    "00460878_marks_dirty_on_slot_change_with_runtime_flags": (DECOMP_00460878, ["+ 0x2d) = 1"]),
    "00460878_latches_previous_slot": (DECOMP_00460878, ["+ 0x17) = *(undefined1 *)(iVar3 + 0x16)"]),
    "00460878_advances_time_counter_by_two": (DECOMP_00460878, ["+ 0xbc) = *(int *)(iVar3 + 0xbc) + 2"]),
    "002d0264_opens_active_mode": (DECOMP_002D0264, ["+ 0x14) = 1", "+ 0x24) = (char)param_1"]),
    "002d0264_resets_record_history_seed": (DECOMP_002D0264, ["+ 0x26) = 0xff", "+ 0x27) = 0x57"]),
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
    context_feed = read_json(CONTEXT_FEED_AUDIT)
    source_selector = read_json(SOURCE_SELECTOR_AUDIT)
    checks: dict[str, bool] = {
        "context_feed_audit_ok": bool(context_feed["summary"]["ok"]),
        "source_selector_runtime_audit_ok": bool(source_selector["summary"]["ok"]),
    }
    evidence: dict[str, list[dict[str, Any]]] = {}

    for name, (path, fragments) in CHECKS.items():
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
        "format": "oot3d_title_intro_source_selector_owner_audit_v1",
        "inputs": {
            "context_feed_audit": rel(CONTEXT_FEED_AUDIT),
            "source_selector_runtime_audit": rel(SOURCE_SELECTOR_AUDIT),
            "decompiled": [
                rel(DECOMP_00422298),
                rel(DECOMP_003523DC),
                rel(DECOMP_00460878),
                rel(DECOMP_002D0264),
            ],
        },
        "summary": {
            "ok": all(checks.values()),
            "checks": checks,
            "next_gate": "Promote 0x00422298/0x003523DC/0x00460878/0x002D0264 into typed feed helpers that populate Oot3dTitleIntroSourceSelectorState before StepState37FromSelector.",
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
        "# OOT3D Title Intro Source Selector Owner Audit",
        "",
        "This audit identifies the native code paths that populate the gate/input fields consumed by the 0x002D6798 title-intro source selector.",
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

    lines.extend(["", "## Field Owners", "", "| Field | Offset | Writers | Consumer | Meaning |", "| --- | --- | --- | --- | --- |"])
    for row in data["field_rows"]:
        lines.append(
            f"| `{row['field']}` | `{row['offset']}` | `{';'.join(row['writers'])}` | "
            f"`{row['consumer']}` | {row['meaning']} |"
        )

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
        raise SystemExit("title-intro source-selector owner audit failed")


if __name__ == "__main__":
    main()
