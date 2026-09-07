#!/usr/bin/env python3
"""Build an audit for the OOT3D title-intro Link boy player-action consumer.

This report is intentionally narrow: it ties native spot00 QDB
CS_CMD_SET_PLAYER_ACTION rows for the opening Link boy actor to the OOT3D
cutscene/player consumer path and to the 36/37/38 direct-state switch evidence.
It does not promote legacy/manual symbol names to semantics.
"""

from __future__ import annotations

import csv
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
EXPORT = ANALYSIS / "title_intro_player_action_consumer_ghidra_export"
DECOMPILED = EXPORT / "decompiled"

PLAYER_ACTION_ROWS = ANALYSIS / "title_intro_link_boy_player_action_table.csv"
SWITCH_MAP_JSON = ANALYSIS / "direct_state_mode_switch_map.json"
HANDLER_CONTEXT_JSON = ANALYSIS / "direct_state_mode_handler_context.json"
FUNCTIONS_CSV = EXPORT / "functions_selected.csv"
DISASSEMBLY = EXPORT / "disassembly_selected.txt"

OUT_JSON = ANALYSIS / "title_intro_player_action_consumer_audit.json"
OUT_MD = ANALYSIS / "title_intro_player_action_consumer_audit.md"

REQUIRED_FUNCTIONS = {
    "001e1b54": "Player_Update",
    "002c5ba0": "Cutscene_ProcessCommands",
    "002e2e60": "player_actor_update_route",
    "00340bdc": "player_direct_state_setter",
    "00458460": "player_direct_state_switch_wrapper",
    "00473ef8": "player_direct_state_switch",
}

DECOMPILED_FILES = {
    "player_update": DECOMPILED / "99002_001e1b54_Player_Update.c",
    "cutscene_process": DECOMPILED / "99011_002c5ba0_Cutscene_ProcessCommands.c",
    "state_setter": DECOMPILED / "99039_00340bdc_FUN_00340bdc.c",
    "state_switch": DECOMPILED / "99086_00473ef8_oot3d_player_action_swing_bottle.c",
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


def read_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    return json.loads(path.read_text(encoding="utf-8-sig"))


def read_csv_rows(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")


def find_line(path: Path, needle: str) -> dict[str, Any]:
    lines = read_text(path).splitlines()
    for index, line in enumerate(lines, start=1):
        if needle in line:
            return {
                "file": rel(path),
                "line": index,
                "text": line.strip(),
                "found": True,
            }
    return {"file": rel(path), "line": 0, "text": "", "found": False}


def find_nearest_previous(path: Path, line_number: int, needle: str, limit: int = 32) -> dict[str, Any]:
    if line_number <= 0:
        return {"file": rel(path), "line": 0, "text": "", "found": False}
    lines = read_text(path).splitlines()
    start = max(1, line_number - limit)
    for index in range(min(line_number - 1, len(lines)), start - 1, -1):
        if needle in lines[index - 1]:
            return {
                "file": rel(path),
                "line": index,
                "text": lines[index - 1].strip(),
                "found": True,
            }
    return {"file": rel(path), "line": 0, "text": "", "found": False}


def find_line_after(path: Path, anchor: str, needle: str, limit: int = 96) -> dict[str, Any]:
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
            return {
                "file": rel(path),
                "line": index,
                "text": lines[index - 1].strip(),
                "found": True,
            }
    return {"file": rel(path), "line": 0, "text": "", "found": False}


def read_functions() -> dict[str, dict[str, str]]:
    rows = read_csv_rows(FUNCTIONS_CSV)
    result: dict[str, dict[str, str]] = {}
    for row in rows:
        entry = str(row.get("entry", "")).lower()
        if entry:
            result[entry] = row
    return result


def summarize_qdb_rows(rows: list[dict[str, str]]) -> dict[str, Any]:
    cue_ids = sorted({int(row["cue_id"]) for row in rows if row.get("cue_id")})
    qdbs = sorted({row.get("qdb_embedded_name", "") for row in rows if row.get("qdb_embedded_name")})
    roles = sorted({row.get("actor_role", "") for row in rows if row.get("actor_role")})
    archives = sorted({row.get("archive_path", "") for row in rows if row.get("archive_path")})
    cmbs = sorted({row.get("cmb_name", "") for row in rows if row.get("cmb_name")})
    csabs = sorted({row.get("title_visual_csab_name", "") for row in rows if row.get("title_visual_csab_name")})
    by_cue: dict[str, int] = {}
    for cue_id in cue_ids:
        by_cue[str(cue_id)] = sum(1 for row in rows if int(row.get("cue_id", -1)) == cue_id)
    return {
        "row_count": len(rows),
        "cue_ids": cue_ids,
        "rows_by_cue": by_cue,
        "qdb_payloads": qdbs,
        "actor_roles": roles,
        "archives": archives,
        "cmbs": cmbs,
        "title_visual_csabs": csabs,
        "opening_link_boy_only": roles == ["opening_link_boy"],
        "has_required_cues_36_37_38": cue_ids == [36, 37, 38],
        "has_link_opening_asset": "actor/zelda_link_opening.zar" in archives
        and "boy/model/link_opening.cmb" in cmbs,
        "has_title_fastrun_clip": "boy/anim/uma_anim_fastrun.csab" in csabs,
    }


def state_rows() -> list[dict[str, Any]]:
    switch_map = read_json(SWITCH_MAP_JSON, {})
    context = read_json(HANDLER_CONTEXT_JSON, {})
    context_by_handler = {
        str(row.get("handler", "")).lower(): row for row in context.get("rows", []) if isinstance(row, dict)
    }
    rows: list[dict[str, Any]] = []
    for row in switch_map.get("rows", []):
        if not isinstance(row, dict):
            continue
        state = int(row.get("state", -1))
        if state not in (36, 37, 38):
            continue
        handler = str(row.get("handler_target", "")).lower()
        ctx = context_by_handler.get(handler, {})
        rows.append(
            {
                "state": state,
                "state_hex": f"0x{state:02x}",
                "handler": handler,
                "flags": row.get("flag_pattern", ""),
                "player_setters": row.get("player_setter_callsite_count", 0),
                "setter_callsites": row.get("player_setter_callsites", ""),
                "features": ctx.get("feature_tags", ""),
                "calls": ctx.get("calls", ""),
                "offsets": ctx.get("offsets", ""),
                "outgoing_targets": ctx.get("outgoing_targets", ""),
            }
        )
    return sorted(rows, key=lambda item: item["state"])


def evidence_signals() -> list[dict[str, Any]]:
    cutscene = DECOMPILED_FILES["cutscene_process"]
    player_update = DECOMPILED_FILES["player_update"]
    state_setter = DECOMPILED_FILES["state_setter"]
    state_switch = DECOMPILED_FILES["state_switch"]

    cutscene_ptr = find_line(cutscene, "*(uint **)(param_2 + 0x40) = puVar18;")
    cutscene_case = find_nearest_previous(cutscene, int(cutscene_ptr["line"]), "case 10:")
    signals = [
        {
            "id": "qdb_command_10_sets_cutscene_player_action_pointer",
            "status": "ok" if cutscene_ptr["found"] and cutscene_case["found"] else "missing",
            "entry": "002c5ba0",
            "function": "Cutscene_ProcessCommands",
            "evidence": [cutscene_case, cutscene_ptr],
            "meaning": "Command 10 iterates 12-word cue records and stores the active record pointer at csCtx+0x40.",
        },
        {
            "id": "player_update_copies_cutscene_player_action_pointer",
            "status": "ok"
            if find_line(player_update, "uStack_24 = *(undefined4 *)(param_2 + 0x40);")["found"]
            else "missing",
            "entry": "001e1b54",
            "function": "Player_Update",
            "evidence": [find_line(player_update, "uStack_24 = *(undefined4 *)(param_2 + 0x40);")],
            "meaning": "Player_Update copies csCtx+0x40 into the local cutscene command block passed to player action logic.",
        },
        {
            "id": "player_update_enters_player_action_logic",
            "status": "ok" if find_line(player_update, "oot3d_player_action_turn_in_place")["found"] else "missing",
            "entry": "001e1b54",
            "function": "Player_Update",
            "evidence": [find_line(player_update, "oot3d_player_action_turn_in_place")],
            "meaning": "The copied cutscene command block reaches the native player action logic path.",
        },
        {
            "id": "runtime_route_calls_direct_state_switch_wrapper",
            "status": "ok" if find_line(DISASSEMBLY, "002e42e0: bl 0x00458460")["found"] else "missing",
            "entry": "002e2e60",
            "function": "player_actor_update_route",
            "evidence": [find_line(DISASSEMBLY, "002e42e0: bl 0x00458460")],
            "meaning": "The exported actor/player update route calls the direct-state switch wrapper.",
        },
        {
            "id": "direct_state_switch_wrapper_calls_switch",
            "status": "ok" if find_line(DISASSEMBLY, "00458464: bl 0x00473ef8")["found"] else "missing",
            "entry": "00458460",
            "function": "player_direct_state_switch_wrapper",
            "evidence": [find_line(DISASSEMBLY, "00458464: bl 0x00473ef8")],
            "meaning": "Wrapper 0x00458460 immediately dispatches into the direct-state switch at 0x00473EF8.",
        },
        {
            "id": "direct_state_setter_writes_player_state_byte",
            "status": "ok"
            if find_line(state_setter, "*(char *)(param_1 + 0x2a90) = (char)param_2;")["found"]
            else "missing",
            "entry": "00340bdc",
            "function": "player_direct_state_setter",
            "evidence": [find_line(state_setter, "*(char *)(param_1 + 0x2a90) = (char)param_2;")],
            "meaning": "The native setter updates player+0x2A90 and applies per-state side-effect flags.",
        },
        {
            "id": "state_36_initializes_and_enters_state_37",
            "status": "ok"
            if find_line(state_switch, "case 0x24:")["found"]
            and find_line_after(state_switch, "case 0x24:", "FUN_00340bdc(param_1,0x25);")["found"]
            else "missing",
            "entry": "00473ef8",
            "function": "player_direct_state_switch",
            "evidence": [
                find_line(state_switch, "case 0x24:"),
                find_line_after(state_switch, "case 0x24:", "FUN_00340bdc(param_1,0x25);"),
            ],
            "meaning": "State 36 begins the title cue route and transitions to state 37.",
        },
        {
            "id": "state_37_consumes_record_and_can_enter_state_38",
            "status": "ok"
            if find_line(state_switch, "case 0x25:")["found"]
            and find_line_after(state_switch, "case 0x25:", "FUN_002d0258();")["found"]
            and find_line_after(state_switch, "case 0x25:", "FUN_00340bdc(param_1,0x26);")["found"]
            else "missing",
            "entry": "00473ef8",
            "function": "player_direct_state_switch",
            "evidence": [
                find_line(state_switch, "case 0x25:"),
                find_line_after(state_switch, "case 0x25:", "FUN_002d0258();"),
                find_line_after(state_switch, "case 0x25:", "FUN_00340bdc(param_1,0x26);"),
            ],
            "meaning": "State 37 fetches a native record/helper payload and can transition to state 38.",
        },
        {
            "id": "state_38_finalizes_and_resets",
            "status": "ok"
            if find_line(state_switch, "case 0x26:")["found"]
            and find_line_after(state_switch, "case 0x26:", "FUN_00367c7c(param_1,DAT_00475c94,0);")["found"]
            and find_line_after(state_switch, "case 0x26:", "FUN_00340bdc(param_1,0);")["found"]
            else "missing",
            "entry": "00473ef8",
            "function": "player_direct_state_switch",
            "evidence": [
                find_line(state_switch, "case 0x26:"),
                find_line_after(state_switch, "case 0x26:", "FUN_00367c7c(param_1,DAT_00475c94,0);"),
                find_line_after(state_switch, "case 0x26:", "FUN_00340bdc(param_1,0);"),
            ],
            "meaning": "State 38 finishes the title cue route and resets the direct player state.",
        },
    ]
    return signals


def build_report() -> dict[str, Any]:
    qdb_rows = read_csv_rows(PLAYER_ACTION_ROWS)
    qdb_summary = summarize_qdb_rows(qdb_rows)
    functions = read_functions()
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
    signals = evidence_signals()
    states = state_rows()
    checks = {
        "qdb_rows_are_opening_link_boy": qdb_summary["opening_link_boy_only"],
        "qdb_rows_have_cues_36_37_38": qdb_summary["has_required_cues_36_37_38"],
        "qdb_rows_bind_link_opening_asset": qdb_summary["has_link_opening_asset"],
        "qdb_rows_bind_title_fastrun_clip": qdb_summary["has_title_fastrun_clip"],
        "required_functions_exported": all(item["present"] for item in required_functions.values()),
        "consumer_signals_found": all(signal["status"] == "ok" for signal in signals),
        "states_36_37_38_mapped": [row["state"] for row in states] == [36, 37, 38],
    }
    return {
        "format": "oot3d_title_intro_player_action_consumer_audit_v1",
        "inputs": {
            "player_action_rows": rel(PLAYER_ACTION_ROWS),
            "functions": rel(FUNCTIONS_CSV),
            "disassembly": rel(DISASSEMBLY),
            "switch_map": rel(SWITCH_MAP_JSON),
            "handler_context": rel(HANDLER_CONTEXT_JSON),
        },
        "identity": {
            "target_role": "opening_link_boy",
            "asset_archive": "actor/zelda_link_opening.zar",
            "model": "boy/model/link_opening.cmb",
            "title_visual_clip": "boy/anim/uma_anim_fastrun.csab",
            "note": "ACTOR_EN_HORSE_LINK_CHILD is retained only as the native actor/source-symbol route name that loads zelda_link_opening.zar; it is not treated as child-Link identity for this intro.",
        },
        "summary": {
            "checks": checks,
            "qdb_row_count": qdb_summary["row_count"],
            "cue_ids": qdb_summary["cue_ids"],
            "state_count": len(states),
            "ok": all(checks.values()),
            "next_gate": "Name and type FUN_002d0258/FUN_002d0264/FUN_002d038c/FUN_003523dc/FUN_00367c7c from OOT3D evidence, then materialize the title cue playback data structure without using the legacy function name as semantics.",
        },
        "qdb_summary": qdb_summary,
        "required_functions": required_functions,
        "consumer_signals": signals,
        "state_rows": states,
        "unresolved": [
            "The exact OOT3D record layout returned by FUN_002d0258 and consumed by FUN_002d038c still needs typed decompilation.",
            "The spot00_demo_epona external QDB route must remain separate from link_info.zsi scene cutscene rows until native scene/demo slot binding is proven.",
            "The current symbol name for 0x00473EF8 is legacy/manual and misleading; use the address or neutral player_direct_state_switch until semantics are proven.",
        ],
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    qdb = data["qdb_summary"]
    lines = [
        "# OOT3D Title Intro Player-Action Consumer Audit",
        "",
        "This audit ties the native `spot00_demo_epona_*` QDB `CS_CMD_SET_PLAYER_ACTION` rows to the OOT3D player-action consumer path for the first title intro. The target actor role is `opening_link_boy`; `ACTOR_EN_HORSE_LINK_CHILD` remains only a native actor/source-symbol name for the archive route and is not treated as child-Link identity.",
        "",
        "## Summary",
        "",
        f"- OK: {summary['ok']}",
        f"- QDB rows: {summary['qdb_row_count']}",
        f"- Cue IDs: {', '.join(str(cue) for cue in summary['cue_ids'])}",
        f"- Direct-state rows: {summary['state_count']}",
        f"- Next gate: {summary['next_gate']}",
        "",
        "## Checks",
        "",
        "| Check | Status |",
        "| --- | --- |",
    ]
    for key, value in summary["checks"].items():
        lines.append(f"| `{key}` | `{value}` |")
    lines.extend(
        [
            "",
            "## Identity",
            "",
            f"- Target role: `{data['identity']['target_role']}`",
            f"- Archive: `{data['identity']['asset_archive']}`",
            f"- Model: `{data['identity']['model']}`",
            f"- Title visual clip: `{data['identity']['title_visual_clip']}`",
            f"- Note: {data['identity']['note']}",
            "",
            "## QDB Timeline",
            "",
            f"- Rows by cue: {', '.join(f'{cue}={count}' for cue, count in qdb['rows_by_cue'].items())}",
            f"- Payloads: {', '.join(f'`{name}`' for name in qdb['qdb_payloads'])}",
            "",
            "## Consumer Signals",
            "",
            "| Signal | Entry | Status | Evidence | Meaning |",
            "| --- | --- | --- | --- | --- |",
        ]
    )
    for signal in data["consumer_signals"]:
        evidence_bits = []
        for item in signal["evidence"]:
            if not item.get("found"):
                continue
            evidence_bits.append(f"`{item['file']}:{item['line']}`")
        lines.append(
            f"| `{signal['id']}` | `{signal['entry']}` | `{signal['status']}` | "
            f"{'; '.join(evidence_bits)} | {signal['meaning']} |"
        )
    lines.extend(
        [
            "",
            "## Direct State 36/37/38",
            "",
            "| State | Handler | Flags | Features | Outgoing | Calls |",
            "| ---: | --- | --- | --- | --- | --- |",
        ]
    )
    for row in data["state_rows"]:
        lines.append(
            f"| {row['state']} `{row['state_hex']}` | `{row['handler']}` | `{row['flags']}` | "
            f"`{row['features']}` | `{row['outgoing_targets']}` | `{row['calls']}` |"
        )
    lines.extend(
        [
            "",
            "## Required Functions",
            "",
            "| Entry | Role | Present | Exported name | Body |",
            "| --- | --- | --- | --- | --- |",
        ]
    )
    for entry, row in data["required_functions"].items():
        lines.append(
            f"| `{entry}` | `{row['role']}` | `{row['present']}` | "
            f"`{row['exported_name']}` | `{row['body_min']}..{row['body_max']}` |"
        )
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
        raise SystemExit("title intro player-action consumer audit failed")


if __name__ == "__main__":
    main()
