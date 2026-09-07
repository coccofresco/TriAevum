#!/usr/bin/env python3
"""Compare semantic call shape for state/mode source workorders."""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter
from pathlib import Path
from typing import Any

from probe_direct_source_subregions import rel
from sweep_direct_source_subregion_ranges import float_value, list_value, repo_path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_WORKORDERS = ROOT / "analysis" / "direct_state_mode_source_workorders.json"
DEFAULT_HANDLER_CONTEXT = ROOT / "analysis" / "direct_state_mode_handler_context.json"
DEFAULT_SEED_QUALIFICATION = ROOT / "analysis" / "direct_state_mode_source_workorder_seed_qualification.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_state_mode_source_call_shape.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_state_mode_source_call_shape.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_state_mode_source_call_shape.md"
SOURCE_CALL_RE = re.compile(r"\b(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\(")
CONTROL_WORDS = {"if", "for", "while", "switch", "return", "sizeof", "ABS", "CLAMP", "ARRAY_COUNT"}

ROLE_ALIASES = {
    "Message_StartTextbox": "message_start",
    "oot3d_message_start_textbox": "message_start",
    "Message_ContinueTextbox": "message_continue",
    "Message_GetState": "message_state",
    "Audio_PlaySoundGeneral": "audio_or_sfx",
    "Sfx_PlaySfxAtPos": "audio_or_sfx",
    "Sfx_PlaySfxCentered": "audio_or_sfx",
    "Player_PlaySfx": "audio_or_sfx",
    "Player_ProcessAnimSfxList": "anim_sfx",
    "Interface_SetSubTimerToFinalSecond": "interface_or_timer",
    "Interface_ChangeAlpha": "interface_or_timer",
    "Camera_SetFinishedFlag": "camera_finish",
    "Play_GetCamera": "camera_get",
    "func_80839FFC": "player_reset",
    "func_8083C0E8": "player_reset",
    "func_80853080": "player_reset",
    "FUN_003725e0": "player_reset",
    "FUN_00340bdc": "state_transition",
    "Player_SpawnMagicSpell": "magic_spawn",
    "Magic_Reset": "magic_reset",
    "LinkAnimation_Update": "anim_update",
    "LinkAnimation_PlayOnceSetSpeed": "anim_play",
    "LinkAnimation_PlayLoopSetSpeed": "anim_play",
    "Player_AnimPlayOnce": "anim_play",
    "Player_StartTalking": "talk_start",
    "Player_PlaySfx": "audio_or_sfx",
    "Actor_Spawn": "actor_spawn",
}

TARGET_ROLE_ALIASES = {
    "FUN_003523dc": "screen_effect_or_gate",
    "FUN_003371d8": "item_record_fetch",
    "FUN_0033f2d8": "item_record_update",
    "FUN_0033f248": "item_action_lookup",
    "FUN_0048b8f8": "timer_or_transition",
    "FUN_0048b814": "timer_or_transition",
    "FUN_0048b9a0": "timer_or_transition",
    "FUN_003679b4": "message_or_textbox",
    "FUN_0036788c": "message_or_textbox",
    "FUN_002c0ca4": "message_or_textbox",
    "FUN_00374be8": "play_or_camera",
    "FUN_0043c20c": "screen_effect_or_gate",
    "FUN_00368d94": "math_or_curve",
    "FUN_0035c528": "item_action_lookup",
    "FUN_00347cc4": "item_action_lookup",
}

SOURCE_ROLE_ALIASES = {
    "Message_StartTextbox": "message_or_textbox",
    "Message_GetState": "message_or_textbox",
    "Message_ContinueTextbox": "message_or_textbox",
    "Player_StartTalking": "message_or_textbox",
    "Player_PlaySfx": "audio_or_sfx",
    "Sfx_PlaySfxAtPos": "audio_or_sfx",
    "Interface_SetSubTimerToFinalSecond": "screen_effect_or_gate",
    "Player_SpawnMagicSpell": "magic_or_item_effect",
    "Magic_Reset": "magic_or_item_effect",
    "func_80853080": "player_reset",
    "func_8083C0E8": "player_reset",
    "func_80839FFC": "player_reset",
    "Camera_SetFinishedFlag": "camera_finish",
    "Play_GetCamera": "play_or_camera",
    "Player_ProcessAnimSfxList": "timer_or_transition",
    "LinkAnimation_PlayOnceSetSpeed": "timer_or_transition",
    "LinkAnimation_PlayLoopSetSpeed": "timer_or_transition",
}


def read_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    return value if isinstance(value, dict) else default


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def lcs_length(left: list[str], right: list[str]) -> int:
    previous = [0] * (len(right) + 1)
    for item in left:
        current = [0]
        northwest = 0
        for index, other in enumerate(right, start=1):
            north = previous[index]
            west = current[-1]
            current.append(northwest + 1 if item == other else max(north, west))
            northwest = north
        previous = current
    return previous[-1]


def normalize_target_call(name: str) -> str:
    return TARGET_ROLE_ALIASES.get(name) or ROLE_ALIASES.get(name) or name


def normalize_source_call(name: str) -> str:
    return SOURCE_ROLE_ALIASES.get(name) or ROLE_ALIASES.get(name) or name


def source_calls(source_excerpt: Path) -> list[str]:
    calls: list[str] = []
    if not source_excerpt.is_file():
        return calls
    for raw in source_excerpt.read_text(encoding="utf-8", errors="replace").splitlines():
        text = re.sub(r"^\s*\d+:\s*", "", raw)
        text = re.sub(r"//.*$", "", text)
        for match in SOURCE_CALL_RE.finditer(text):
            name = match.group("name")
            if name not in CONTROL_WORDS:
                calls.append(name)
    return calls


def handler_index(context: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {
        str(row.get("handler", "")): row
        for row in list_value(context.get("rows", []))
        if isinstance(row, dict) and row.get("handler")
    }


def qualification_index(data: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {
        str(row.get("workorder_id", "")): row
        for row in list_value(data.get("rows", []))
        if isinstance(row, dict) and row.get("workorder_id")
    }


def support_class(
    target_roles: list[str],
    source_roles: list[str],
    lcs_count: int,
    shared_roles: set[str],
    seed_class: str,
) -> str:
    if seed_class in {"short-generic-near-risk", "near-window-risk"} and lcs_count >= 2 and len(shared_roles) >= 2:
        return "semantic-callshape-risk-confirmed"
    if lcs_count >= 3 and len(shared_roles) >= 3:
        return "semantic-callshape-supported"
    if lcs_count >= 2 or len(shared_roles) >= 2:
        return "semantic-callshape-orientation"
    if target_roles and source_roles:
        return "semantic-callshape-weak"
    return "semantic-callshape-missing"


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    workorders = read_json(args.workorders, {})
    handlers = handler_index(read_json(args.handler_context, {}))
    qualifications = qualification_index(read_json(args.seed_qualification, {}))
    rows: list[dict[str, Any]] = []
    for workorder in list_value(workorders.get("rows", [])):
        if not isinstance(workorder, dict):
            continue
        workorder_id = str(workorder.get("workorder_id", ""))
        handler = handlers.get(str(workorder.get("handler", "")), {})
        target_calls = [part for part in str(handler.get("calls", "")).split(";") if part]
        target_roles = [normalize_target_call(call) for call in target_calls]
        source_path = repo_path(workorder.get("workorder_dir", "")) / "source_excerpt.c.txt"
        source_call_names = source_calls(source_path)
        source_roles = [normalize_source_call(call) for call in source_call_names]
        shared_roles = set(target_roles) & set(source_roles)
        lcs_count = lcs_length(target_roles, source_roles)
        target_den = max(1, len(target_roles))
        source_den = max(1, len(source_roles))
        seed_row = qualifications.get(workorder_id, {})
        seed_class = str(seed_row.get("seed_class", ""))
        support = support_class(target_roles, source_roles, lcs_count, shared_roles, seed_class)
        rows.append(
            {
                "workorder_id": workorder_id,
                "handler": workorder.get("handler", ""),
                "states": workorder.get("states", ""),
                "best_source_function": workorder.get("best_source_function", ""),
                "best_source_range": workorder.get("best_source_range", ""),
                "seed_class": seed_class,
                "support_class": support,
                "target_call_count": len(target_calls),
                "source_call_count": len(source_call_names),
                "target_roles": ";".join(target_roles),
                "source_roles": ";".join(source_roles),
                "shared_roles": ";".join(sorted(shared_roles)),
                "shared_role_count": len(shared_roles),
                "role_lcs_count": lcs_count,
                "role_lcs_target_ratio": round(lcs_count / target_den, 4),
                "role_lcs_source_ratio": round(lcs_count / source_den, 4),
                "target_calls": ";".join(target_calls),
                "source_calls": ";".join(source_call_names),
                "opcode_category": seed_row.get("category", ""),
                "opcode_lcs_window_ratio": seed_row.get("lcs_window_ratio", 0.0),
            }
        )
    rows.sort(
        key=lambda row: (
            support_rank(str(row.get("support_class", ""))),
            -float_value(row.get("role_lcs_source_ratio")),
            -int(row.get("role_lcs_count", 0)),
            str(row.get("workorder_id", "")),
        )
    )
    counts = Counter(str(row.get("support_class", "")) for row in rows)
    best = rows[0] if rows else {}
    summary = {
        "workorders": len(rows),
        "semantic_supported": counts.get("semantic-callshape-supported", 0),
        "semantic_risk_confirmed": counts.get("semantic-callshape-risk-confirmed", 0),
        "semantic_orientation": counts.get("semantic-callshape-orientation", 0),
        "semantic_weak": counts.get("semantic-callshape-weak", 0),
        "semantic_missing": counts.get("semantic-callshape-missing", 0),
        "support_classes": dict(sorted(counts.items())),
        "best_workorder": best.get("workorder_id", ""),
        "best_support_class": best.get("support_class", ""),
        "best_role_lcs_count": best.get("role_lcs_count", 0),
        "best_shared_roles": best.get("shared_roles", ""),
        "promotion_ready": 0,
        "next_gate": "Use supported call-shape rows only as semantic orientation; opcode-qualified seeds are still required for promotion.",
    }
    return {
        "format": "oot3d_direct_state_mode_source_call_shape_v1",
        "inputs": {
            "workorders": rel(args.workorders),
            "handler_context": rel(args.handler_context),
            "seed_qualification": rel(args.seed_qualification),
        },
        "summary": summary,
        "rows": rows,
    }


def support_rank(value: str) -> int:
    return {
        "semantic-callshape-supported": 0,
        "semantic-callshape-risk-confirmed": 1,
        "semantic-callshape-orientation": 2,
        "semantic-callshape-weak": 3,
        "semantic-callshape-missing": 4,
    }.get(value, 99)


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct State/Mode Source Call Shape",
        "",
        "This report compares semantic call roles between OOT3D handlers and N64 source workorder excerpts.",
        "It can support orientation, but it cannot make a row promotion-ready without opcode-qualified evidence.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Workorders | {summary['workorders']} |",
        f"| Semantic supported | {summary['semantic_supported']} |",
        f"| Semantic risk-confirmed | {summary['semantic_risk_confirmed']} |",
        f"| Semantic orientation | {summary['semantic_orientation']} |",
        f"| Semantic weak | {summary['semantic_weak']} |",
        f"| Promotion-ready | {summary['promotion_ready']} |",
        "",
        f"Next gate: {summary['next_gate']}",
        "",
        "## Rows",
        "",
        "| Support | Workorder | Handler | Source | Seed class | LCS | Shared roles |",
        "| --- | --- | --- | --- | --- | ---: | --- |",
    ]
    for row in data["rows"]:
        source = f"{row.get('best_source_function', '')}:{row.get('best_source_range', '')}"
        lines.append(
            f"| `{row.get('support_class', '')}` | `{row.get('workorder_id', '')}` | "
            f"`{row.get('handler', '')}` | `{source}` | `{row.get('seed_class', '')}` | "
            f"{row.get('role_lcs_count', 0)} | `{row.get('shared_roles', '')}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workorders", type=Path, default=DEFAULT_WORKORDERS)
    parser.add_argument("--handler-context", type=Path, default=DEFAULT_HANDLER_CONTEXT)
    parser.add_argument("--seed-qualification", type=Path, default=DEFAULT_SEED_QUALIFICATION)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()
    data = build_report(args)
    fields = [
        "workorder_id",
        "handler",
        "states",
        "best_source_function",
        "best_source_range",
        "seed_class",
        "support_class",
        "target_call_count",
        "source_call_count",
        "shared_role_count",
        "role_lcs_count",
        "role_lcs_target_ratio",
        "role_lcs_source_ratio",
        "shared_roles",
        "target_roles",
        "source_roles",
        "target_calls",
        "source_calls",
        "opcode_category",
        "opcode_lcs_window_ratio",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct state/mode source call shape: "
        f"{summary['workorders']} workorders, "
        f"{summary['semantic_supported']} supported, "
        f"{summary['semantic_risk_confirmed']} risk-confirmed"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
