#!/usr/bin/env python3
"""Build an OOT3D-native audit for title-intro player-action helpers.

This report keeps the evidence at the level currently proven by the native
decompilation: addresses, globals, offsets, record bytes, and call shapes.
It intentionally avoids treating legacy/manual function names as semantics.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
EXPORT = ANALYSIS / "title_intro_player_action_consumer_ghidra_export"
DECOMPILED = EXPORT / "decompiled"

OUT_JSON = ANALYSIS / "title_intro_player_action_helper_audit.json"
OUT_MD = ANALYSIS / "title_intro_player_action_helper_audit.md"

HELPERS = [
    {
        "entry": "002d0258",
        "name": "FUN_002d0258",
        "file": "99013_002d0258_FUN_002d0258.c",
        "provisional_role": "direct_record_pointer_getter",
        "meaning": "Returns the current global direct-action record pointer.",
        "globals": ["DAT_002d0260"],
        "offsets": [],
        "patterns": [
            {
                "id": "returns_global_record_pointer",
                "needle": "return DAT_002d0260;",
                "meaning": "The helper is a pure getter for the current record pointer.",
            }
        ],
    },
    {
        "entry": "002d0264",
        "name": "FUN_002d0264",
        "file": "99014_002d0264_FUN_002d0264.c",
        "provisional_role": "mode_gate_setter",
        "meaning": "Updates a global mode byte and associated active/pending flags before and after the title cue route.",
        "globals": ["DAT_002d0314", "DAT_002d0318", "DAT_002d031c"],
        "offsets": ["0x14", "0x24", "0x25", "0x26", "0x27", "0x28", "0x29", "0x2a", "0x3e", "0xac", "0xd8"],
        "patterns": [
            {
                "id": "checks_mode_byte_0x24",
                "needle": "if (*(byte *)(DAT_002d0314 + 0x24) != param_1) {",
                "meaning": "Skips work unless the requested mode differs from context+0x24.",
            },
            {
                "id": "enable_path_marks_active",
                "needle": "*(undefined1 *)(iVar1 + 0x14) = 1;",
                "meaning": "Nonzero mode marks the context active.",
            },
            {
                "id": "mode_writeback",
                "needle": "*(char *)(iVar1 + 0x24) = (char)param_1;",
                "meaning": "The requested mode is stored in context+0x24.",
            },
            {
                "id": "copies_u16x4_tables",
                "needle": "oot3d_copy_u16x4(uVar3,uVar2);",
                "meaning": "Nonzero mode copies an associated 4x16-bit table pair.",
            },
        ],
    },
    {
        "entry": "002d038c",
        "name": "FUN_002d038c",
        "file": "99016_002d038c_FUN_002d038c.c",
        "provisional_role": "indexed_byte_table_setter",
        "meaning": "Updates a byte table entry and optionally notifies another subsystem for indices below 8.",
        "globals": ["DAT_002d0404", "DAT_002d0408", "DAT_002d040c", "DAT_002d0418"],
        "offsets": ["param_1"],
        "patterns": [
            {
                "id": "compares_existing_table_value",
                "needle": "if (*(byte *)(DAT_002d0404 + param_1) != param_2) {",
                "meaning": "The table write is conditional on value change.",
            },
            {
                "id": "low_index_notify_gate",
                "needle": "if (param_1 < 8) {",
                "meaning": "Only the first eight slots trigger the notification side effect.",
            },
            {
                "id": "dispatches_changed_low_index",
                "needle": "FUN_00494de0(DAT_002d0418,param_1,param_2);",
                "meaning": "Changed low-index entries are dispatched with index and value.",
            },
            {
                "id": "writes_byte_table",
                "needle": "*(char *)(iVar1 + param_1) = (char)param_2;",
                "meaning": "The final state is stored as one byte at base+index.",
            },
        ],
    },
    {
        "entry": "003523dc",
        "name": "FUN_003523dc",
        "file": "99048_003523dc_FUN_003523dc.c",
        "provisional_role": "global_gate_toggle_with_vector_latch",
        "meaning": "Toggles a global gate, latches vector-like signed shorts into two bytes, and applies enable/disable side effects.",
        "globals": ["DAT_003524d8", "DAT_003524e0", "DAT_003524e4", "DAT_003524e8"],
        "offsets": ["0x13", "0x14", "0x15", "0x1c", "0x1d", "0x44", "0x4a", "0x4c", "0x94", "0xd4", "0xdc", "0xe0", "0xe4", "0xe8"],
        "patterns": [
            {
                "id": "checks_gate_byte_0x15",
                "needle": "if (*(char *)(DAT_003524d8 + 0x15) != param_1) {",
                "meaning": "Work runs only when the gate byte changes.",
            },
            {
                "id": "latches_short_0x4a",
                "needle": "sVar1 = *(short *)(iVar2 + 0x4a);",
                "meaning": "Enable path reads a signed short source at context+0x4A.",
            },
            {
                "id": "latches_short_0x4c",
                "needle": "sVar1 = *(short *)(iVar2 + 0x4c);",
                "meaning": "Enable path reads a second signed short source at context+0x4C.",
            },
            {
                "id": "enable_side_effects",
                "needle": "FUN_00355fac(0,2,0x28,0xf);",
                "meaning": "Enable path applies a native side effect for slot 0.",
            },
            {
                "id": "disable_resets_subsystems",
                "needle": "FUN_0033c950(0xd);",
                "meaning": "Disable path resets a native subsystem state.",
            },
        ],
    },
    {
        "entry": "00367c7c",
        "name": "FUN_00367c7c",
        "file": "99061_00367c7c_FUN_00367c7c.c",
        "provisional_role": "request_dispatch_helper",
        "meaning": "Prepares actor-local request state, rewrites one special request id, dispatches it, and marks the target context active.",
        "globals": ["DAT_00367d58", "DAT_00367d5c", "DAT_00367d60", "DAT_00367d64", "DAT_00367d70"],
        "offsets": ["actor+0x224c", "target+0xd"],
        "patterns": [
            {
                "id": "prepares_actor_local_state",
                "needle": "FUN_00343f0c(param_1,param_1 + 0x224c);",
                "meaning": "The helper seeds request state from actor+0x224C.",
            },
            {
                "id": "rewrites_special_request",
                "needle": "if ((*(char *)(DAT_00367d58 + 0xe) != '\\0') && (param_2 == 0x102a)) {",
                "meaning": "Request 0x102A can be substituted by a global runtime value.",
            },
            {
                "id": "dispatches_request",
                "needle": "FUN_004c0bc8(DAT_00367d70,param_2,param_3,1);",
                "meaning": "The request id and argument are sent to a global target context.",
            },
            {
                "id": "marks_target_active",
                "needle": "*(undefined1 *)(iVar2 + 0xd) = 1;",
                "meaning": "The target context active byte is set after dispatch.",
            },
        ],
    },
    {
        "entry": "003725e0",
        "name": "FUN_003725e0",
        "file": "99073_003725e0_FUN_003725e0.c",
        "provisional_role": "direct_state_reset_wrapper",
        "meaning": "Runs a native reset side effect, then resets the direct player state through 0x00340BDC.",
        "globals": ["DAT_0037263c", "DAT_00372640", "DAT_00372648", "DAT_0037264c"],
        "offsets": ["player+0x2a90 via FUN_00340bdc"],
        "patterns": [
            {
                "id": "runs_reset_side_effect",
                "needle": "FUN_002e9a1c(DAT_0037264c,param_2);",
                "meaning": "A native side effect is applied before the state reset.",
            },
            {
                "id": "resets_direct_player_state",
                "needle": "FUN_00340bdc(param_1,0);",
                "meaning": "The wrapper returns the player to direct state 0.",
            },
        ],
    },
]

STATE_SWITCH = DECOMPILED / "99086_00473ef8_oot3d_player_action_swing_bottle.c"

STATE_37_PATTERNS = [
    {
        "id": "fetch_current_record",
        "anchor": "case 0x25:",
        "needle": "puVar35 = (undefined1 *)FUN_002d0258();",
        "meaning": "State 37 fetches the current record through the native getter.",
    },
    {
        "id": "cache_record_pointer_on_player",
        "anchor": "case 0x25:",
        "needle": "*(undefined1 **)(param_1 + 0x2a80) = puVar35;",
        "meaning": "The record pointer is cached on player+0x2A80.",
    },
    {
        "id": "byte2_threshold_matches_cursor",
        "anchor": "case 0x25:",
        "needle": "(sVar18 = *(short *)(puVar14 + 2), (byte)puVar35[2] - 1 == (int)sVar18)) {",
        "meaning": "Record byte +2 is interpreted as cursor threshold/count; cursor is DAT_00474DF8+2.",
    },
    {
        "id": "byte0_copied_to_player",
        "anchor": "case 0x25:",
        "needle": "*(undefined1 *)(param_1 + 0x2ba0) = *puVar35;",
        "meaning": "Record byte +0 is copied to player+0x2BA0.",
    },
    {
        "id": "byte0_written_to_indexed_table",
        "anchor": "case 0x25:",
        "needle": "FUN_002d038c((int)sVar18,**(undefined1 **)(param_1 + 0x2a80));",
        "meaning": "Record byte +0 is also written to the indexed byte table at the current cursor.",
    },
    {
        "id": "cursor_increment",
        "anchor": "case 0x25:",
        "needle": "*(short *)(puVar14 + 2) = sVar18 + 1;",
        "meaning": "The direct-state cursor advances by one.",
    },
    {
        "id": "next_table_slot_cleared",
        "anchor": "case 0x25:",
        "needle": "FUN_002d038c((int)(short)(sVar18 + 1),0xff);",
        "meaning": "The next indexed table slot is cleared to 0xFF.",
    },
    {
        "id": "byte1_terminal_control",
        "anchor": "case 0x25:",
        "needle": "cVar17 = *(char *)(*(int *)(param_1 + 0x2a80) + 1);",
        "meaning": "Record byte +1 controls the completion/branch path.",
    },
    {
        "id": "completion_clears_mode_gate",
        "anchor": "case 0x25:",
        "needle": "FUN_002d0264(0);",
        "meaning": "The completion path clears the mode gate.",
    },
    {
        "id": "completion_enters_state_38",
        "anchor": "case 0x25:",
        "needle": "FUN_00340bdc(param_1,0x26);",
        "meaning": "The completion path advances into state 38.",
    },
    {
        "id": "state_38_dispatches_request",
        "anchor": "case 0x26:",
        "needle": "FUN_00367c7c(param_1,DAT_00475c94,0);",
        "meaning": "State 38 dispatches the follow-up request before resetting state.",
    },
]


def rel(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def read_text(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def find_line(path: Path, needle: str) -> dict[str, Any]:
    lines = read_text(path).splitlines()
    for index, line in enumerate(lines, start=1):
        if needle in line:
            return {"file": rel(path), "line": index, "text": line.strip(), "found": True}
    return {"file": rel(path), "line": 0, "text": "", "found": False}


def find_line_after(path: Path, anchor: str, needle: str, limit: int = 128) -> dict[str, Any]:
    lines = read_text(path).splitlines()
    anchor_index = 0
    for index, line in enumerate(lines, start=1):
        if anchor in line:
            anchor_index = index
            break
    if not anchor_index:
        return {"file": rel(path), "line": 0, "text": "", "found": False}
    for index in range(anchor_index, min(len(lines), anchor_index + limit) + 1):
        if needle in lines[index - 1]:
            return {"file": rel(path), "line": index, "text": lines[index - 1].strip(), "found": True}
    return {"file": rel(path), "line": 0, "text": "", "found": False}


def evidence_for(path: Path, patterns: list[dict[str, str]]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for pattern in patterns:
        if "anchor" in pattern:
            hit = find_line_after(path, pattern["anchor"], pattern["needle"])
        else:
            hit = find_line(path, pattern["needle"])
        rows.append(
            {
                "id": pattern["id"],
                "status": "ok" if hit["found"] else "missing",
                "meaning": pattern["meaning"],
                "evidence": hit,
            }
        )
    return rows


def helper_rows() -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for helper in HELPERS:
        path = DECOMPILED / helper["file"]
        evidence = evidence_for(path, helper["patterns"])
        rows.append(
            {
                "entry": helper["entry"],
                "name": helper["name"],
                "provisional_role": helper["provisional_role"],
                "meaning": helper["meaning"],
                "file": rel(path),
                "globals": helper["globals"],
                "offsets": helper["offsets"],
                "evidence": evidence,
                "ok": all(item["status"] == "ok" for item in evidence),
            }
        )
    return rows


def state_record_layout() -> dict[str, Any]:
    evidence = evidence_for(STATE_SWITCH, STATE_37_PATTERNS)
    return {
        "switch_entry": "00473ef8",
        "switch_file": rel(STATE_SWITCH),
        "local_context": {
            "record_pointer_cache": "player+0x2A80",
            "last_record_value": "player+0x2BA0",
            "cursor": "s16 at DAT_00474DF8+2",
        },
        "record_bytes": [
            {
                "offset": "+0",
                "provisional_name": "table_value",
                "use": "Copied to player+0x2BA0 and passed to FUN_002d038c(cursor, value).",
            },
            {
                "offset": "+1",
                "provisional_name": "terminal_or_branch_control",
                "use": "Nonzero selects completion checks; zero selects the state-39 branch.",
            },
            {
                "offset": "+2",
                "provisional_name": "cursor_threshold_count",
                "use": "When nonzero, byte2 - 1 must match the cursor before byte +0 is consumed.",
            },
        ],
        "evidence": evidence,
        "ok": all(item["status"] == "ok" for item in evidence),
    }


def build_report() -> dict[str, Any]:
    helpers = helper_rows()
    layout = state_record_layout()
    checks = {
        "helper_patterns_found": all(row["ok"] for row in helpers),
        "state_37_record_layout_patterns_found": layout["ok"],
        "uses_neutral_switch_semantics": True,
    }
    return {
        "format": "oot3d_title_intro_player_action_helper_audit_v1",
        "inputs": {
            "export": rel(EXPORT),
            "state_switch": rel(STATE_SWITCH),
        },
        "summary": {
            "ok": all(checks.values()),
            "checks": checks,
            "helper_count": len(helpers),
            "next_gate": "Use title_intro_record_pointer_audit to type context 0x0054AC48 and trace upstream writes to the record C producer bytes.",
        },
        "helpers": helpers,
        "state_37_record_layout": layout,
        "unresolved": [
            "The backing storage for FUN_002D0258 is resolved by title_intro_record_pointer_audit as context+0x5B; upstream producers of that context still need typed source mapping.",
            "Type the state-39 branch and the 0x80-byte copy sourced from DAT_00475C90.",
            "Keep all names provisional until each role is supported by native callsite/write evidence.",
        ],
    }


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")


def evidence_ref(item: dict[str, Any]) -> str:
    evidence = item.get("evidence", {})
    if not evidence.get("found"):
        return "`missing`"
    return f"`{evidence['file']}:{evidence['line']}`"


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    lines = [
        "# OOT3D Title Intro Player-Action Helper Audit",
        "",
        "This audit names helper roles only where the native decompilation proves read/write behavior. Function names remain provisional; the stable identifiers are addresses, globals, offsets, and call shapes.",
        "",
        "## Summary",
        "",
        f"- OK: {data['summary']['ok']}",
        f"- Helpers: {data['summary']['helper_count']}",
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
            "## Helper Roles",
            "",
            "| Entry | Provisional role | Globals | Offsets | Evidence |",
            "| --- | --- | --- | --- | --- |",
        ]
    )
    for helper in data["helpers"]:
        refs = "; ".join(evidence_ref(item) for item in helper["evidence"])
        globals_text = ", ".join(f"`{item}`" for item in helper["globals"])
        offsets_text = ", ".join(f"`{item}`" for item in helper["offsets"])
        lines.append(
            f"| `{helper['entry']}` | `{helper['provisional_role']}` | "
            f"{globals_text} | {offsets_text} | {refs} |"
        )

    layout = data["state_37_record_layout"]
    lines.extend(
        [
            "",
            "## State 37 Record Layout",
            "",
            f"- Switch entry: `{layout['switch_entry']}`",
            f"- Record pointer cache: `{layout['local_context']['record_pointer_cache']}`",
            f"- Last record value: `{layout['local_context']['last_record_value']}`",
            f"- Cursor: `{layout['local_context']['cursor']}`",
            "",
            "| Record byte | Provisional name | Use |",
            "| --- | --- | --- |",
        ]
    )
    for row in layout["record_bytes"]:
        lines.append(f"| `{row['offset']}` | `{row['provisional_name']}` | {row['use']} |")

    lines.extend(
        [
            "",
            "## State 37 Evidence",
            "",
            "| Signal | Status | Evidence | Meaning |",
            "| --- | --- | --- | --- |",
        ]
    )
    for item in layout["evidence"]:
        lines.append(f"| `{item['id']}` | `{item['status']}` | {evidence_ref(item)} | {item['meaning']} |")

    lines.extend(["", "## Unresolved", ""])
    for item in data["unresolved"]:
        lines.append(f"- {item}")
    lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main() -> None:
    data = build_report()
    write_json(OUT_JSON, data)
    write_markdown(OUT_MD, data)
    if not data["summary"]["ok"]:
        raise SystemExit("title intro player-action helper audit failed")


if __name__ == "__main__":
    main()
