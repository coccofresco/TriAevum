#!/usr/bin/env python3
"""Build the OOT3D open-title actor runtime bridge audit.

This promotes the already decoded open-title orchestration, actor source
tables, and player-action motion consumer into a small runtime-facing layer.
It does not claim full visual playback: it binds native actor assets and the
active player-action record selection rule so the engine can instantiate and
sample from OOT3D-native sources without legacy QDB substitution.
"""

from __future__ import annotations

import csv
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"

ORCHESTRATION = ANALYSIS / "title_intro_opening_orchestration.json"
SOURCE_TABLE = ANALYSIS / "title_intro_source_table.json"
PLAYER_MOTION = ANALYSIS / "title_intro_opening_player_motion_audit.json"
PLAYER_ACTION_CONSUMER = ANALYSIS / "title_intro_player_action_consumer_audit.json"
PLAYER_ACTION_HELPER = ANALYSIS / "title_intro_player_action_helper_audit.json"
RECORD_POINTER = ANALYSIS / "title_intro_record_pointer_audit.json"
RECORD_APPLY = ANALYSIS / "title_intro_record_apply_audit.json"
RECORD_C_RUNTIME = ANALYSIS / "title_intro_record_c_runtime_audit.json"
PLAYBACK_RUNTIME = ANALYSIS / "title_intro_playback_runtime_audit.json"
CUTSCENE_PROCESS_COMMANDS = (
    ANALYSIS
    / "cutscene_camera_blob_helper_ghidra_export"
    / "decompiled"
    / "99003_002c5ba0_Cutscene_ProcessCommands.c"
)

OUT_JSON = ANALYSIS / "title_intro_opening_actor_runtime_audit.json"
OUT_MD = ANALYSIS / "title_intro_opening_actor_runtime_audit.md"
OUT_TIMELINE_CSV = ANALYSIS / "title_intro_opening_actor_runtime_timeline.csv"
OUT_HEADER = ROOT / "include" / "oot3d" / "title_intro_opening_actor_runtime.h"
OUT_SOURCE = ROOT / "src" / "code" / "z_title_intro_opening_actor_runtime.c"

NO_INDEX = 0xFFFF


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace") if path.is_file() else ""


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def write_json(path: Path, data: Any) -> None:
    write_text(path, json.dumps(data, indent=2) + "\n")


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        if not rows:
            return
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def rel(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def find_one(rows: list[dict[str, Any]], label: str, **criteria: Any) -> dict[str, Any]:
    matches = [row for row in rows if all(row.get(key) == value for key, value in criteria.items())]
    if len(matches) != 1:
        raise SystemExit(f"{label}: expected one row for {criteria}, found {len(matches)}")
    return matches[0]


def c_string(value: Any) -> str:
    text = "" if value is None else str(value)
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def c_u8(value: Any) -> str:
    return f"{int(value) & 0xFF}u"


def c_u16(value: Any) -> str:
    return f"{int(value) & 0xFFFF}u"


def c_u32(value: Any) -> str:
    return f"0x{int(value) & 0xFFFFFFFF:08X}u"


def c_s16(value: Any) -> str:
    return str(int(value))


def c_s32(value: Any) -> str:
    return str(int(value))


def c_float(value: Any) -> str:
    text = f"{float(value):.9g}"
    if "e" not in text.lower() and "." not in text:
        text += ".0"
    return text + "f"


def parse_hex_u32(value: Any) -> int:
    text = str(value or "").strip()
    if not text:
        return 0
    return int(text, 16)


def parse_offset(value: Any) -> int:
    text = str(value or "").strip().replace("+", "")
    if not text:
        return 0
    return int(text, 16)


def required_function_address(player_action_consumer: dict[str, Any], role: str) -> int:
    for address, row in player_action_consumer.get("required_functions", {}).items():
        if row.get("role") == role and bool(row.get("present")):
            return parse_hex_u32(address)
    return 0


def direct_state_runtime_for_action(
    action_id: int,
    direct_state_case_present: bool,
    player_action_consumer: dict[str, Any],
    direct_record_layout_index: int,
) -> dict[str, Any]:
    state_rows = {
        int(row["state"]): row
        for row in player_action_consumer.get("state_rows", [])
    }
    state_row = state_rows.get(action_id) if direct_state_case_present else None
    if state_row is None:
        return {
            "direct_record_layout_index": NO_INDEX,
            "direct_state_id": NO_INDEX,
            "direct_state_id_hex": "",
            "direct_state_switch_function": 0,
            "direct_state_switch_wrapper_function": 0,
            "direct_state_setter_function": 0,
            "direct_state_handler_function": 0,
            "direct_state_features": "",
            "direct_state_calls": "",
            "direct_state_outgoing_targets": "",
            "player_action_consumer_status": "decoded_from_oot3d_player_action_motion_consumer_no_direct_state_case",
            "direct_state_evidence": "analysis/title_intro_opening_player_motion_audit.json#action_0x40_0x41_not_direct_states",
            "unresolved": (
                "This row samples the OOT3D player-action motion consumer and is not a direct-state "
                "switch case; exact paired Epona transform application remains tied to the title cue/mount consumer."
            ),
        }

    is_title_record_entry = (
        action_id == 0x24
        and int(state_row["state"]) == 36
        and str(state_row.get("outgoing_targets", "")) == "37"
    )
    return {
        "direct_record_layout_index": direct_record_layout_index if is_title_record_entry else NO_INDEX,
        "direct_state_id": int(state_row["state"]),
        "direct_state_id_hex": str(state_row["state_hex"]),
        "direct_state_switch_function": required_function_address(player_action_consumer, "player_direct_state_switch"),
        "direct_state_switch_wrapper_function": required_function_address(player_action_consumer, "player_direct_state_switch_wrapper"),
        "direct_state_setter_function": required_function_address(player_action_consumer, "player_direct_state_setter"),
        "direct_state_handler_function": parse_hex_u32(state_row["handler"]),
        "direct_state_features": str(state_row.get("features", "")),
        "direct_state_calls": str(state_row.get("calls", "")),
        "direct_state_outgoing_targets": str(state_row.get("outgoing_targets", "")),
        "player_action_consumer_status": "decoded_from_oot3d_player_direct_state_switch_and_motion_consumer",
        "direct_state_evidence": (
            "analysis/title_intro_player_action_consumer_audit.json#state_rows[state="
            f"{int(state_row['state'])}]"
        ),
        "unresolved": (
            "This row samples the OOT3D player-action motion consumer and overlaps the native direct-state "
            "switch; the state-37 record layout is linked through direct_record_layout_index when this row enters "
            "the native title record consumer."
        ),
    }


def helper_entry(player_action_helper: dict[str, Any], role: str) -> dict[str, Any]:
    for row in player_action_helper.get("helpers", []):
        if row.get("provisional_role") == role:
            return row
    return {}


def record_pointer_getter_entry(record_pointer: dict[str, Any]) -> int:
    for row in record_pointer.get("references", []):
        if row.get("target") == "002d0260" and row.get("from_function_entry") == "002d0258":
            return parse_hex_u32(row.get("from_function_entry"))
    return 0


def record_pointer_literal_address(record_pointer: dict[str, Any]) -> int:
    for row in record_pointer.get("references", []):
        if row.get("target") == "002d0260" and row.get("from_function_entry") == "002d0258":
            return parse_hex_u32(row.get("target"))
    return 0


def literal_value_by_role(record_apply: dict[str, Any], role: str) -> int:
    for row in record_apply.get("literals", []):
        if row.get("role") == role:
            return parse_hex_u32(row.get("value"))
    return 0


def literal_address_by_role(record_apply: dict[str, Any], role: str) -> int:
    for row in record_apply.get("literals", []):
        if row.get("role") == role:
            return parse_hex_u32(row.get("literal_pool_address"))
    return 0


def state_row_by_id(player_action_consumer: dict[str, Any], state_id: int) -> dict[str, Any]:
    for row in player_action_consumer.get("state_rows", []):
        if int(row.get("state", -1)) == state_id:
            return row
    return {}


def record_table_row(record_pointer: dict[str, Any], record_name: str) -> dict[str, Any]:
    for row in record_pointer.get("record_table", []):
        if row.get("record") == record_name:
            return row
    return {}


def extract_player_offset(value: str) -> int:
    text = str(value or "").strip().lower()
    marker = "player+"
    index = text.find(marker)
    if index < 0:
        return 0
    return int(text[index + len(marker):], 16)


def build_direct_record_layouts(
    player_action_consumer: dict[str, Any],
    player_action_helper: dict[str, Any],
    record_pointer: dict[str, Any],
    record_apply: dict[str, Any],
    record_c_runtime: dict[str, Any],
    playback_runtime: dict[str, Any],
) -> list[dict[str, Any]]:
    record_c = record_table_row(record_pointer, "C")
    state37 = state_row_by_id(player_action_consumer, 37)
    layout = player_action_helper["state_37_record_layout"]
    bytes_by_offset = {
        parse_offset(row["offset"]): row
        for row in layout["record_bytes"]
    }
    local_context = layout["local_context"]
    callsites = record_apply["callsites"]["state37"]
    outgoing = [int(value) for value in str(state37.get("outgoing_targets", "")).split(";") if value]
    return [
        {
            "direct_record_layout_index": 0,
            "entry_direct_state_id": 36,
            "consumer_direct_state_id": 37,
            "completion_direct_state_id": 38,
            "branch_direct_state_id": 39 if 39 in outgoing else NO_INDEX,
            "entry_mode_change_function": 0x002D0264,
            "entry_mode_byte": 2,
            "entry_global_gate_function": 0x003523DC,
            "entry_global_gate_byte": 1,
            "state37_handler_function": parse_hex_u32(state37.get("handler")),
            "record_pointer_getter_function": record_pointer_getter_entry(record_pointer),
            "record_pointer_literal_pool_address": record_pointer_literal_address(record_pointer),
            "record_context_address": parse_hex_u32(record_pointer["resolved"]["context_base"]),
            "record_table_address": parse_hex_u32(record_pointer["resolved"]["record_table_base"]),
            "record_pointer_address": parse_hex_u32(record_pointer["resolved"]["getter_002d0258_returns"]),
            "record_context_offset": parse_offset(record_c["context_offset"]),
            "record_stride": int(record_pointer["resolved"]["record_stride"]),
            "record_byte0_offset": 0,
            "record_byte1_offset": 1,
            "record_byte2_offset": 2,
            "record_byte0_role": bytes_by_offset[0]["provisional_name"],
            "record_byte1_role": bytes_by_offset[1]["provisional_name"],
            "record_byte2_role": bytes_by_offset[2]["provisional_name"],
            "record_byte0_source": record_c["known_bytes"][0],
            "record_byte1_source": record_c["known_bytes"][1],
            "record_byte2_source": record_c["known_bytes"][2],
            "record_apply_function": parse_hex_u32(record_apply["summary"]["entry"]),
            "record_byte_table_literal_pool_address": literal_address_by_role(record_apply, "record_byte_table"),
            "record_byte_table_address": literal_value_by_role(record_apply, "record_byte_table"),
            "first_change_latch_address": literal_value_by_role(record_apply, "first_change_latch"),
            "first_change_init_context_address": literal_value_by_role(record_apply, "first_change_init_context"),
            "record_change_notify_context_address": literal_value_by_role(record_apply, "record_change_notify_context"),
            "record_apply_side_effect_index_limit": 8,
            "state37_apply_current_callsite": parse_hex_u32(callsites[0]) if callsites else 0,
            "state37_apply_terminator_callsite": parse_hex_u32(callsites[1]) if len(callsites) > 1 else 0,
            "state37_player_record_pointer_offset": extract_player_offset(local_context["record_pointer_cache"]),
            "state37_player_last_record_value_offset": extract_player_offset(local_context["last_record_value"]),
            "state37_cursor_source": local_context["cursor"],
            "record_pointer_status": "decoded_from_oot3d_title_intro_record_pointer_audit",
            "record_apply_status": "decoded_from_oot3d_title_intro_record_apply_audit",
            "record_c_runtime_status": (
                "oot3d_record_c_runtime_audit_ok" if record_c_runtime["summary"]["ok"]
                else "oot3d_record_c_runtime_audit_failed"
            ),
            "state37_playback_runtime_status": (
                "oot3d_state37_playback_runtime_audit_ok" if playback_runtime["summary"]["ok"]
                else "oot3d_state37_playback_runtime_audit_failed"
            ),
            "source_evidence": (
                "analysis/title_intro_player_action_helper_audit.json#state_37_record_layout;"
                "analysis/title_intro_record_pointer_audit.json#record_table[C];"
                "analysis/title_intro_record_apply_audit.json#semantics;"
                "analysis/title_intro_player_action_consumer_ghidra_export/decompiled/"
                "99086_00473ef8_oot3d_player_action_swing_bottle.c#state36"
            ),
            "unresolved": (
                "Upstream producer fields for the record context remain separately tracked by "
                "title_intro_record_c_runtime_audit/context-feed audits; this row binds the native "
                "state-36 entry side effects, state-37 consumer layout, and applicator."
            ),
        }
    ]


def active_motion_for_frame(rows: list[dict[str, Any]], frame: int) -> dict[str, Any] | None:
    best: dict[str, Any] | None = None
    for row in rows:
        if int(row["start_frame"]) < frame <= int(row["end_frame"]):
            if best is None:
                best = row
                continue
            row_key = (int(row["local_command_index"]), int(row["entry_index"]))
            best_key = (int(best["local_command_index"]), int(best["entry_index"]))
            if row_key >= best_key:
                best = row
    return best


def active_actor_cue_for_frame(
    rows: list[dict[str, Any]], qdb_index: int, cue_command_id: int, frame: int
) -> dict[str, Any] | None:
    first: dict[str, Any] | None = None
    held: dict[str, Any] | None = None
    active: dict[str, Any] | None = None
    for row in rows:
        if int(row["qdb_index"]) != qdb_index or int(row["cue_command_id"]) != cue_command_id:
            continue
        if first is None:
            first = row
        if frame >= int(row["start_frame"]):
            held = row
        if frame >= int(row["start_frame"]) and frame < int(row["end_frame"]):
            active = row
            break
    return active or held or first


def derive_paired_mount_cue_command_id(actor_cue_rows: list[dict[str, Any]]) -> int:
    command_ids = sorted(
        {
            int(row["cue_command_id"])
            for row in actor_cue_rows
            if int(row["cue_command_id"]) != 0x0000000A
            and "demo_epona" in str(row.get("embedded_name", "")).lower()
        }
    )
    if len(command_ids) != 1:
        raise SystemExit(f"expected one paired mount cue command id, found {command_ids}")
    return command_ids[0]


def cutscene_process_commands_evidence() -> dict[str, Any]:
    lines = read_text(CUTSCENE_PROCESS_COMMANDS).splitlines()
    case_lines: list[int] = []
    assign_line = 0
    active_rule_lines: list[int] = []
    for index, line in enumerate(lines, start=1):
        if "case 10:" in line:
            case_lines.append(index)
        if "*(uint **)(param_2 + 0x40) = puVar18;" in line:
            assign_line = index
        if "*(ushort *)((int)puVar18 + 2) < *(ushort *)(param_2 + 0x20)" in line:
            active_rule_lines.append(index)
    start_line = max((line for line in case_lines if assign_line == 0 or line < assign_line), default=0)
    active_rule_line = max((line for line in active_rule_lines if assign_line == 0 or line < assign_line), default=0)
    return {
        "file": rel(CUTSCENE_PROCESS_COMMANDS),
        "case_10_line": start_line,
        "active_rule_line": active_rule_line,
        "assign_csctx_40_line": assign_line,
        "has_case_10": start_line != 0,
        "has_start_exclusive_end_inclusive": active_rule_line != 0,
        "has_direct_csctx_40_assignment": assign_line != 0,
    }


def build_actor_bindings(orchestration: dict[str, Any], source_table: dict[str, Any]) -> list[dict[str, Any]]:
    row = orchestration["orchestration_rows"][0]
    asset_refs = orchestration["required_asset_refs"]
    asset_ref_by_role = {asset["asset_role"]: asset for asset in asset_refs}
    scale_rows = source_table["title_actor_scale_rows"]
    animation_rows = source_table["title_actor_animation_rows"]
    motion_rows = source_table["title_actor_motion_animation_rows"]
    logo_init = source_table["title_logo_actor_init"][0]
    logo_components = source_table["title_logo_component_rows"]
    logo_draws = source_table["title_logo_draw_rows"]
    logo_updates = source_table["title_logo_update_rows"]

    link_scale = find_one(scale_rows, "link scale", actor_role="opening_link_boy")
    epona_scale = find_one(scale_rows, "epona scale", actor_role="opening_epona")
    link_anim = find_one(animation_rows, "link animation", actor_role="opening_link_boy")
    epona_anim = find_one(animation_rows, "epona animation", actor_role="opening_epona")
    epona_motion = find_one(motion_rows, "epona motion animation", actor_role="opening_epona")

    bindings = [
        {
            "actor_binding_index": 0,
            "orchestration_index": row["orchestration_index"],
            "actor_kind": "LINK_BOY",
            "actor_kind_value": 1,
            "source_scale_index": link_scale["scale_index"],
            "source_animation_index": link_anim["animation_index"],
            "source_motion_animation_index": NO_INDEX,
            "source_actor_init_index": NO_INDEX,
            "paired_actor_binding_index": 1,
            "required_asset_ref_index": asset_ref_by_role["actor_link_opening"]["asset_ref_index"],
            "logo_component_ref_start": NO_INDEX,
            "logo_component_ref_count": 0,
            "logo_draw_ref_start": NO_INDEX,
            "logo_draw_ref_count": 0,
            "logo_update_ref_start": NO_INDEX,
            "logo_update_ref_count": 0,
            "actor_init_function": link_scale["init_function"],
            "actor_update_function": link_scale["update_function"],
            "actor_draw_function": link_scale["draw_function"],
            "actor_scale": link_scale["actor_scale"],
            "gravity": link_scale["gravity"],
            "shadow_scale": link_scale["shadow_scale"],
            "focus_y_offset": link_scale["focus_y_offset"],
            "actor_role": "opening_link_boy_mounted_visual",
            "actor_name": link_scale["actor_name"],
            "archive_path": link_anim["archive_path"],
            "cmb_name": link_anim["cmb_name"],
            "init_csab_name": link_anim["init_csab_name"],
            "title_visual_csab_name": link_anim["title_visual_csab_name"],
            "runtime_binding_role": "player_action_record_visual_actor",
            "native_basis": f"{link_scale['oot3d_basis']} {link_anim['oot3d_basis']}",
            "unresolved": (
                "Exact mounted transform ownership is still pending; this row binds the native "
                "Link opening asset and player-action motion consumer without claiming the final mount coupling."
            ),
        },
        {
            "actor_binding_index": 1,
            "orchestration_index": row["orchestration_index"],
            "actor_kind": "EPONA",
            "actor_kind_value": 2,
            "source_scale_index": epona_scale["scale_index"],
            "source_animation_index": epona_anim["animation_index"],
            "source_motion_animation_index": epona_motion["motion_index"],
            "source_actor_init_index": NO_INDEX,
            "paired_actor_binding_index": 0,
            "required_asset_ref_index": asset_ref_by_role["actor_epona_horse"]["asset_ref_index"],
            "logo_component_ref_start": NO_INDEX,
            "logo_component_ref_count": 0,
            "logo_draw_ref_start": NO_INDEX,
            "logo_draw_ref_count": 0,
            "logo_update_ref_start": NO_INDEX,
            "logo_update_ref_count": 0,
            "actor_init_function": epona_scale["init_function"],
            "actor_update_function": epona_scale["update_function"],
            "actor_draw_function": epona_scale["draw_function"],
            "actor_scale": epona_scale["actor_scale"],
            "gravity": epona_scale["gravity"],
            "shadow_scale": epona_scale["shadow_scale"],
            "focus_y_offset": epona_scale["focus_y_offset"],
            "actor_role": "opening_epona_mount_visual",
            "actor_name": epona_scale["actor_name"],
            "archive_path": epona_anim["archive_path"],
            "cmb_name": epona_anim["cmb_name"],
            "init_csab_name": epona_anim["init_csab_name"],
            "title_visual_csab_name": epona_anim["title_visual_csab_name"],
            "runtime_binding_role": "paired_mount_visual_native_horse_route",
            "native_basis": f"{epona_scale['oot3d_basis']} {epona_anim['oot3d_basis']} {epona_motion['oot3d_basis']}",
            "unresolved": (
                "Exact title-cutscene horse transform consumer is still pending; native horse "
                "scale, asset, and speed-to-CSAB route are bound here."
            ),
        },
        {
            "actor_binding_index": 2,
            "orchestration_index": row["orchestration_index"],
            "actor_kind": "TITLE_LOGO",
            "actor_kind_value": 3,
            "source_scale_index": NO_INDEX,
            "source_animation_index": NO_INDEX,
            "source_motion_animation_index": NO_INDEX,
            "source_actor_init_index": logo_init["actor_init_index"],
            "paired_actor_binding_index": NO_INDEX,
            "required_asset_ref_index": asset_ref_by_role["actor_title_logo"]["asset_ref_index"],
            "logo_component_ref_start": row["title_logo_component_ref_start"],
            "logo_component_ref_count": row["title_logo_component_ref_count"],
            "logo_draw_ref_start": row["title_logo_draw_ref_start"],
            "logo_draw_ref_count": row["title_logo_draw_ref_count"],
            "logo_update_ref_start": row["title_logo_update_ref_start"],
            "logo_update_ref_count": row["title_logo_update_ref_count"],
            "actor_init_function": logo_init["init_function"],
            "actor_update_function": logo_init["update_function"],
            "actor_draw_function": logo_init["draw_function"],
            "actor_scale": 0.0,
            "gravity": 0.0,
            "shadow_scale": 0.0,
            "focus_y_offset": 0.0,
            "actor_role": "opening_title_logo_actor",
            "actor_name": logo_init["actor_name"],
            "archive_path": "actor/zelda_mag.zar",
            "cmb_name": ";".join(component["cmb_name_jpeu"] for component in logo_components),
            "init_csab_name": "",
            "title_visual_csab_name": ";".join(component["csab_name_jpeu"] for component in logo_components if component["csab_name_jpeu"]),
            "runtime_binding_role": "title_logo_component_update_draw_actor",
            "native_basis": (
                f"{logo_init['basis']} components={len(logo_components)} "
                f"draw_rows={len(logo_draws)} update_rows={len(logo_updates)}"
            ),
            "unresolved": "Frame-accurate EnMag state scheduling remains a later runtime attachment step.",
        },
    ]
    return bindings


def build_motion_timeline(
    motion_rows: list[dict[str, Any]],
    player_action_consumer: dict[str, Any],
    direct_record_layout_index: int,
) -> list[dict[str, Any]]:
    timeline: list[dict[str, Any]] = []
    for row in motion_rows:
        direct_state = direct_state_runtime_for_action(
            int(row["action_id"]),
            bool(row["direct_state_case_present"]),
            player_action_consumer,
            direct_record_layout_index,
        )
        timeline.append(
            {
                "timeline_index": len(timeline),
                "orchestration_index": row["orchestration_index"],
                "actor_binding_index": 0,
                "paired_mount_actor_binding_index": 1,
                "motion_ref_index": row["motion_ref_index"],
                "player_action_ref_index": row["player_action_ref_index"],
                "player_action_source_index": row["player_action_source_index"],
                "native_command_source_index": row["native_command_source_index"],
                "local_command_index": row["local_command_index"],
                "entry_index": row["entry_index"],
                "action_id": row["action_id"],
                "action_id_hex": row["action_id_hex"],
                "start_frame": row["start_frame"],
                "end_frame": row["end_frame"],
                "duration_frames": row["duration_frames"],
                "direct_state_case_present": int(bool(row["direct_state_case_present"])),
                "not_direct_state_case": int(bool(row["not_direct_state_case"])),
                "has_quantized_motion_vector": int(bool(row["has_quantized_motion_vector"])),
                "speed_clamped": int(bool(row["speed_clamped"])),
                "start_exclusive_end_inclusive": 1,
                **direct_state,
                "native_speed": row["clamped_speed"],
                "native_heading_s16": row["native_heading_s16"],
                "native_heading_u16_hex": row["native_heading_u16_hex"],
                "source_semantic": (
                    "Cutscene_ProcessCommands command 10 assigns this active 12-word record to csCtx+0x40 "
                    "when startFrame < csFrame <= endFrame; later active command entries overwrite earlier ones."
                ),
            }
        )
    return timeline


def build_report() -> dict[str, Any]:
    orchestration = read_json(ORCHESTRATION)
    source_table = read_json(SOURCE_TABLE)
    player_motion = read_json(PLAYER_MOTION)
    player_action_consumer = read_json(PLAYER_ACTION_CONSUMER)
    player_action_helper = read_json(PLAYER_ACTION_HELPER)
    record_pointer = read_json(RECORD_POINTER)
    record_apply = read_json(RECORD_APPLY)
    record_c_runtime = read_json(RECORD_C_RUNTIME)
    playback_runtime = read_json(PLAYBACK_RUNTIME)
    evidence = cutscene_process_commands_evidence()
    actor_cue_rows = source_table["epona_actor_cue_rows"]
    link_player_action_rows = source_table["title_link_boy_player_action_rows"]
    paired_mount_cue_command_id = derive_paired_mount_cue_command_id(actor_cue_rows)
    actor_bindings = build_actor_bindings(orchestration, source_table)
    direct_record_layouts = build_direct_record_layouts(
        player_action_consumer,
        player_action_helper,
        record_pointer,
        record_apply,
        record_c_runtime,
        playback_runtime,
    )
    direct_record_layout_index = (
        int(direct_record_layouts[0]["direct_record_layout_index"])
        if direct_record_layouts
        else NO_INDEX
    )
    motion_timeline = build_motion_timeline(
        player_motion["motion_rows"],
        player_action_consumer,
        direct_record_layout_index,
    )
    sample_frames = [0, 15, 16, 925, 926, 1620, 1621]
    sample_rows: list[dict[str, Any]] = []
    for frame in sample_frames:
        active = active_motion_for_frame(motion_timeline, frame)
        player_action = (
            next(
                (
                    row
                    for row in link_player_action_rows
                    if active is not None and int(row["player_action_index"]) == int(active["player_action_ref_index"])
                ),
                None,
            )
            if active is not None
            else None
        )
        paired_mount = (
            active_actor_cue_for_frame(
                actor_cue_rows,
                int(player_action["qdb_index"]),
                paired_mount_cue_command_id,
                frame,
            )
            if player_action is not None
            else None
        )
        sample_rows.append(
            {
                "frame": frame,
                "active": active is not None,
                "timeline_index": active["timeline_index"] if active else NO_INDEX,
                "motion_ref_index": active["motion_ref_index"] if active else NO_INDEX,
                "player_action_ref_index": active["player_action_ref_index"] if active else NO_INDEX,
                "player_action_source_index": active["player_action_source_index"] if active else NO_INDEX,
                "paired_mount_actor_cue_index": paired_mount["actor_cue_index"] if paired_mount else NO_INDEX,
                "action_id_hex": active["action_id_hex"] if active else "",
                "native_speed": active["native_speed"] if active else 0.0,
                "native_heading_s16": active["native_heading_s16"] if active else 0,
                "direct_state_id": active["direct_state_id"] if active else NO_INDEX,
                "direct_record_layout_index": active["direct_record_layout_index"] if active else NO_INDEX,
                "direct_state_handler_function": active["direct_state_handler_function"] if active else 0,
                "player_action_consumer_status": active["player_action_consumer_status"] if active else "",
            }
        )

    direct_action_rows = [row for row in motion_timeline if int(row["action_id"]) == 0x24]
    non_direct_action_rows = [row for row in motion_timeline if int(row["action_id"]) in (0x40, 0x41)]
    direct_state_switch_function = required_function_address(player_action_consumer, "player_direct_state_switch")
    direct_state_switch_wrapper_function = required_function_address(player_action_consumer, "player_direct_state_switch_wrapper")
    direct_state_setter_function = required_function_address(player_action_consumer, "player_direct_state_setter")
    direct_record_layout = direct_record_layouts[0] if direct_record_layouts else {}

    checks = {
        "orchestration_audit_ok": bool(orchestration["summary"]["ok"]),
        "player_motion_audit_ok": bool(player_motion["summary"]["ok"]),
        "player_action_consumer_audit_ok": bool(player_action_consumer["summary"]["ok"]),
        "player_action_helper_audit_ok": bool(player_action_helper["summary"]["ok"]),
        "record_pointer_audit_ok": bool(record_pointer["summary"]["ok"]),
        "record_apply_audit_ok": bool(record_apply["summary"]["ok"]),
        "record_c_runtime_audit_ok": bool(record_c_runtime["summary"]["ok"]),
        "playback_runtime_audit_ok": bool(playback_runtime["summary"]["ok"]),
        "actor_binding_count_is_3": len(actor_bindings) == 3,
        "link_binding_uses_opening_asset": actor_bindings[0]["archive_path"] == "actor/zelda_link_opening.zar",
        "epona_binding_uses_native_horse_asset": actor_bindings[1]["archive_path"] == "actor/zelda_horse.zar",
        "logo_binding_uses_enmag_actor_init": actor_bindings[2]["actor_name"] == "ACTOR_EN_MAG",
        "motion_timeline_count_matches_player_motion_rows": len(motion_timeline) == len(player_motion["motion_rows"]),
        "cutscene_process_commands_case10_exported": evidence["has_case_10"],
        "native_active_rule_is_start_exclusive_end_inclusive": evidence["has_start_exclusive_end_inclusive"],
        "native_assignment_targets_csctx_40": evidence["has_direct_csctx_40_assignment"],
        "paired_mount_cue_command_id_is_0x3e": paired_mount_cue_command_id == 0x0000003E,
        "paired_mount_cue_rows_cover_all_link_title_qdbs": all(
            any(
                int(cue["qdb_index"]) == int(action["qdb_index"])
                and int(cue["cue_command_id"]) == paired_mount_cue_command_id
                for cue in actor_cue_rows
            )
            for action in link_player_action_rows
        ),
        "overlap_frame_925_selects_later_local_command": sample_rows[3]["motion_ref_index"] == 5,
        "frame_926_selects_nonzero_heading_row_6": sample_rows[4]["motion_ref_index"] == 6
        and sample_rows[4]["native_heading_s16"] == 16384,
        "frame_1620_selects_row_9": sample_rows[5]["motion_ref_index"] == 9
        and sample_rows[5]["native_heading_s16"] == -16384,
        "frame_15_resolves_initial_epona_mount_cue": sample_rows[1]["paired_mount_actor_cue_index"] == 4,
        "direct_state_switch_address_is_0x00473ef8": direct_state_switch_function == 0x00473EF8,
        "direct_state_wrapper_address_is_0x00458460": direct_state_switch_wrapper_function == 0x00458460,
        "direct_state_setter_address_is_0x00340bdc": direct_state_setter_function == 0x00340BDC,
        "direct_record_layout_count_is_1": len(direct_record_layouts) == 1,
        "direct_record_layout_getter_is_0x002d0258": direct_record_layout.get("record_pointer_getter_function") == 0x002D0258,
        "direct_record_layout_getter_literal_is_0x002d0260": direct_record_layout.get("record_pointer_literal_pool_address") == 0x002D0260,
        "direct_record_layout_record_c_is_0x0054aca3": direct_record_layout.get("record_pointer_address") == 0x0054ACA3,
        "direct_record_layout_context_offset_is_0x5b": direct_record_layout.get("record_context_offset") == 0x5B,
        "direct_record_layout_entry_mode_is_002d0264_2": (
            direct_record_layout.get("entry_mode_change_function") == 0x002D0264
            and direct_record_layout.get("entry_mode_byte") == 2
        ),
        "direct_record_layout_entry_gate_is_003523dc_1": (
            direct_record_layout.get("entry_global_gate_function") == 0x003523DC
            and direct_record_layout.get("entry_global_gate_byte") == 1
        ),
        "direct_record_layout_apply_is_0x002d038c": direct_record_layout.get("record_apply_function") == 0x002D038C,
        "direct_record_layout_byte_table_is_0x005a2e7c": direct_record_layout.get("record_byte_table_address") == 0x005A2E7C,
        "direct_record_layout_state37_handler_is_0x00475654": direct_record_layout.get("state37_handler_function") == 0x00475654,
        "direct_record_layout_uses_player_offsets": (
            direct_record_layout.get("state37_player_record_pointer_offset") == 0x2A80
            and direct_record_layout.get("state37_player_last_record_value_offset") == 0x2BA0
        ),
        "action_0x24_runtime_rows_have_state_36_handler": bool(direct_action_rows)
        and all(
            int(row["direct_state_id"]) == 36
            and int(row["direct_record_layout_index"]) == 0
            and int(row["direct_state_handler_function"]) == 0x00475628
            and str(row["direct_state_outgoing_targets"]) == "37"
            and row["player_action_consumer_status"] == "decoded_from_oot3d_player_direct_state_switch_and_motion_consumer"
            for row in direct_action_rows
        ),
        "action_0x40_0x41_runtime_rows_mark_no_direct_state": bool(non_direct_action_rows)
        and all(
            int(row["direct_state_id"]) == NO_INDEX
            and int(row["direct_record_layout_index"]) == NO_INDEX
            and int(row["direct_state_handler_function"]) == 0
            and row["player_action_consumer_status"] == "decoded_from_oot3d_player_action_motion_consumer_no_direct_state_case"
            for row in non_direct_action_rows
        ),
    }

    return {
        "format": "oot3d_title_intro_opening_actor_runtime_audit_v1",
        "inputs": {
            "orchestration": rel(ORCHESTRATION),
            "source_table": rel(SOURCE_TABLE),
            "player_motion": rel(PLAYER_MOTION),
            "player_action_consumer": rel(PLAYER_ACTION_CONSUMER),
            "player_action_helper": rel(PLAYER_ACTION_HELPER),
            "record_pointer": rel(RECORD_POINTER),
            "record_apply": rel(RECORD_APPLY),
            "record_c_runtime": rel(RECORD_C_RUNTIME),
            "playback_runtime": rel(PLAYBACK_RUNTIME),
            "cutscene_process_commands": rel(CUTSCENE_PROCESS_COMMANDS),
        },
        "summary": {
            "ok": all(checks.values()),
            "checks": checks,
            "actor_binding_count": len(actor_bindings),
            "motion_timeline_count": len(motion_timeline),
            "paired_mount_cue_command_id": paired_mount_cue_command_id,
            "direct_state_runtime_row_count": len(direct_action_rows),
            "motion_only_runtime_row_count": len(non_direct_action_rows),
            "direct_record_layout_count": len(direct_record_layouts),
            "next_gate": (
                "Bind the direct record layout to the frame runtime state-37 playback step, "
                "then drive the Link/Epona/title-logo actor updates from that native step sequence."
            ),
        },
        "native_selection_evidence": evidence,
        "actor_bindings": actor_bindings,
        "direct_record_layout_rows": direct_record_layouts,
        "motion_timeline_rows": motion_timeline,
        "sample_rows": sample_rows,
        "unresolved": [
            "The active Link-boy player-action row and paired Epona mount cue row are resolved from OOT3D QDB source rows; exact internal horse state consumer naming remains pending.",
            "The state-37 direct record layout and byte applicator are now represented as runtime rows, but the open-title frame runtime still needs to call the playback step rather than only exposing its layout.",
            "The title logo actor route is bound to native EnMag data, but frame-accurate scheduler integration is still pending.",
            "Camera curves remain in the opening orchestration table and are not sampled by this actor runtime bridge.",
        ],
    }


def write_header(path: Path, data: dict[str, Any]) -> None:
    lines = [
        "#ifndef OOT3D_TITLE_INTRO_OPENING_ACTOR_RUNTIME_H",
        "#define OOT3D_TITLE_INTRO_OPENING_ACTOR_RUNTIME_H",
        "",
        '#include "oot3d/title_intro_opening_player_motion.h"',
        '#include "oot3d/title_intro_source_table.h"',
        '#include "oot3d/types.h"',
        "",
        "#define OOT3D_TITLE_INTRO_OPENING_ACTOR_RUNTIME_NO_INDEX 0xFFFFu",
        f"#define OOT3D_TITLE_INTRO_OPENING_EPONA_MOUNT_CUE_COMMAND_ID {c_u32(data['summary']['paired_mount_cue_command_id'])}",
        "",
        "typedef enum {",
        "    OOT3D_TITLE_INTRO_OPENING_ACTOR_KIND_LINK_BOY = 1,",
        "    OOT3D_TITLE_INTRO_OPENING_ACTOR_KIND_EPONA = 2,",
        "    OOT3D_TITLE_INTRO_OPENING_ACTOR_KIND_TITLE_LOGO = 3,",
        "} Oot3dTitleIntroOpeningActorKind;",
        "",
        "typedef enum {",
        "    OOT3D_TITLE_INTRO_OPENING_ACTOR_SAMPLE_OK = 0,",
        "    OOT3D_TITLE_INTRO_OPENING_ACTOR_SAMPLE_NULL_OUTPUT,",
        "    OOT3D_TITLE_INTRO_OPENING_ACTOR_SAMPLE_NO_ACTIVE_CUE,",
        "    OOT3D_TITLE_INTRO_OPENING_ACTOR_SAMPLE_MOTION_ERROR,",
        "} Oot3dTitleIntroOpeningActorSampleStatus;",
        "",
        "typedef struct Oot3dTitleIntroOpeningActorBindingRow {",
        "    u16 actorBindingIndex;",
        "    u16 orchestrationIndex;",
        "    u16 actorKind;",
        "    u16 sourceScaleIndex;",
        "    u16 sourceAnimationIndex;",
        "    u16 sourceMotionAnimationIndex;",
        "    u16 sourceActorInitIndex;",
        "    u16 pairedActorBindingIndex;",
        "    u16 requiredAssetRefIndex;",
        "    u16 logoComponentRefStart;",
        "    u16 logoComponentRefCount;",
        "    u16 logoDrawRefStart;",
        "    u16 logoDrawRefCount;",
        "    u16 logoUpdateRefStart;",
        "    u16 logoUpdateRefCount;",
        "    u32 actorInitFunction;",
        "    u32 actorUpdateFunction;",
        "    u32 actorDrawFunction;",
        "    float actorScale;",
        "    float gravity;",
        "    float shadowScale;",
        "    float focusYOffset;",
        "    const char* actorRole;",
        "    const char* actorName;",
        "    const char* archivePath;",
        "    const char* cmbName;",
        "    const char* initCsabName;",
        "    const char* titleVisualCsabName;",
        "    const char* runtimeBindingRole;",
        "    const char* nativeBasis;",
        "    const char* unresolved;",
        "} Oot3dTitleIntroOpeningActorBindingRow;",
        "",
        "typedef struct Oot3dTitleIntroOpeningActorDirectRecordLayoutRow {",
        "    u16 directRecordLayoutIndex;",
        "    u16 entryDirectStateId;",
        "    u16 consumerDirectStateId;",
        "    u16 completionDirectStateId;",
        "    u16 branchDirectStateId;",
        "    u32 entryModeChangeFunction;",
        "    u8 entryModeByte;",
        "    u32 entryGlobalGateFunction;",
        "    u8 entryGlobalGateByte;",
        "    u32 state37HandlerFunction;",
        "    u32 recordPointerGetterFunction;",
        "    u32 recordPointerLiteralPoolAddress;",
        "    u32 recordContextAddress;",
        "    u32 recordTableAddress;",
        "    u32 recordPointerAddress;",
        "    u16 recordContextOffset;",
        "    u16 recordStride;",
        "    u8 recordByte0Offset;",
        "    u8 recordByte1Offset;",
        "    u8 recordByte2Offset;",
        "    const char* recordByte0Role;",
        "    const char* recordByte1Role;",
        "    const char* recordByte2Role;",
        "    const char* recordByte0Source;",
        "    const char* recordByte1Source;",
        "    const char* recordByte2Source;",
        "    u32 recordApplyFunction;",
        "    u32 recordByteTableLiteralPoolAddress;",
        "    u32 recordByteTableAddress;",
        "    u32 firstChangeLatchAddress;",
        "    u32 firstChangeInitContextAddress;",
        "    u32 recordChangeNotifyContextAddress;",
        "    u16 recordApplySideEffectIndexLimit;",
        "    u32 state37ApplyCurrentCallsite;",
        "    u32 state37ApplyTerminatorCallsite;",
        "    u16 state37PlayerRecordPointerOffset;",
        "    u16 state37PlayerLastRecordValueOffset;",
        "    const char* state37CursorSource;",
        "    const char* recordPointerStatus;",
        "    const char* recordApplyStatus;",
        "    const char* recordCRuntimeStatus;",
        "    const char* state37PlaybackRuntimeStatus;",
        "    const char* sourceEvidence;",
        "    const char* unresolved;",
        "} Oot3dTitleIntroOpeningActorDirectRecordLayoutRow;",
        "",
        "typedef struct Oot3dTitleIntroOpeningActorMotionCueRow {",
        "    u16 timelineIndex;",
        "    u16 orchestrationIndex;",
        "    u16 actorBindingIndex;",
        "    u16 pairedMountActorBindingIndex;",
        "    u16 motionRefIndex;",
        "    u16 playerActionRefIndex;",
        "    u16 playerActionSourceIndex;",
        "    u16 nativeCommandSourceIndex;",
        "    u16 localCommandIndex;",
        "    u16 entryIndex;",
        "    u16 actionId;",
        "    u16 startFrame;",
        "    u16 endFrame;",
        "    u16 durationFrames;",
        "    u8 directStateCasePresent;",
        "    u8 notDirectStateCase;",
        "    u8 hasQuantizedMotionVector;",
        "    u8 speedClamped;",
        "    u8 startExclusiveEndInclusive;",
        "    u16 directRecordLayoutIndex;",
        "    u16 directStateId;",
        "    u32 directStateSwitchFunction;",
        "    u32 directStateSwitchWrapperFunction;",
        "    u32 directStateSetterFunction;",
        "    u32 directStateHandlerFunction;",
        "    float nativeSpeed;",
        "    s16 nativeHeading;",
        "    const char* actionIdHex;",
        "    const char* directStateIdHex;",
        "    const char* directStateFeatures;",
        "    const char* directStateCalls;",
        "    const char* directStateOutgoingTargets;",
        "    const char* playerActionConsumerStatus;",
        "    const char* directStateEvidence;",
        "    const char* sourceSemantic;",
        "    const char* unresolved;",
        "} Oot3dTitleIntroOpeningActorMotionCueRow;",
        "",
        "typedef struct Oot3dTitleIntroOpeningActorMotionSample {",
        "    s32 frame;",
        "    u8 active;",
        "    u16 timelineIndex;",
        "    u16 actorBindingIndex;",
        "    u16 pairedMountActorBindingIndex;",
        "    u16 motionRefIndex;",
        "    u16 playerActionRefIndex;",
        "    u16 playerActionSourceIndex;",
        "    u16 actionId;",
        "    float nativeSpeed;",
        "    s16 nativeHeading;",
        "    const Oot3dTitleIntroOpeningActorBindingRow* actorBinding;",
        "    const Oot3dTitleIntroOpeningActorBindingRow* pairedMountBinding;",
        "    const Oot3dTitleIntroOpeningActorMotionCueRow* cue;",
        "    const Oot3dTitleIntroOpeningPlayerMotionRow* motionRow;",
        "    Oot3dTitleIntroPlayerActionTransform playerActionTransform;",
        "    const Oot3dTitleIntroLinkBoyPlayerActionRow* playerActionRow;",
        "    const Oot3dTitleIntroActorCueRow* pairedMountCueRow;",
        "    Oot3dTitleIntroPlayerActionMotionResult motion;",
        "} Oot3dTitleIntroOpeningActorMotionSample;",
        "",
        "extern const Oot3dTitleIntroOpeningActorBindingRow gOot3dTitleIntroOpeningActorBindingRows[];",
        "extern const u32 gOot3dTitleIntroOpeningActorBindingRowCount;",
        "extern const Oot3dTitleIntroOpeningActorDirectRecordLayoutRow gOot3dTitleIntroOpeningActorDirectRecordLayoutRows[];",
        "extern const u32 gOot3dTitleIntroOpeningActorDirectRecordLayoutRowCount;",
        "extern const Oot3dTitleIntroOpeningActorMotionCueRow gOot3dTitleIntroOpeningActorMotionCueRows[];",
        "extern const u32 gOot3dTitleIntroOpeningActorMotionCueRowCount;",
        "",
        "const Oot3dTitleIntroOpeningActorBindingRow* Oot3d_TitleIntroOpeningActorRuntimeGetBinding(u16 actorBindingIndex);",
        "const Oot3dTitleIntroOpeningActorDirectRecordLayoutRow* Oot3d_TitleIntroOpeningActorRuntimeGetDirectRecordLayout(u16 directRecordLayoutIndex);",
        "const Oot3dTitleIntroOpeningActorMotionCueRow* Oot3d_TitleIntroOpeningActorRuntimeGetMotionCue(u16 timelineIndex);",
        "const Oot3dTitleIntroOpeningActorMotionCueRow* Oot3d_TitleIntroOpeningActorRuntimeFindActivePlayerMotionCue(s32 frame);",
        "const Oot3dTitleIntroLinkBoyPlayerActionRow* Oot3d_TitleIntroOpeningActorRuntimeGetPlayerActionSourceRow(u16 playerActionRefIndex);",
        "const Oot3dTitleIntroActorCueRow* Oot3d_TitleIntroOpeningActorRuntimeFindPairedMountCueSourceRow(",
        "    const Oot3dTitleIntroLinkBoyPlayerActionRow* playerActionRow,",
        "    s32 frame",
        ");",
        "const Oot3dTitleIntroActorScaleRow* Oot3d_TitleIntroOpeningActorRuntimeGetScaleSourceRow(u16 actorBindingIndex);",
        "const Oot3dTitleIntroActorAnimationRow* Oot3d_TitleIntroOpeningActorRuntimeGetAnimationSourceRow(u16 actorBindingIndex);",
        "const Oot3dTitleIntroActorMotionAnimationRow* Oot3d_TitleIntroOpeningActorRuntimeGetMotionAnimationSourceRow(u16 actorBindingIndex);",
        "const Oot3dTitleIntroActorInitSourceRow* Oot3d_TitleIntroOpeningActorRuntimeGetActorInitSourceRow(u16 actorBindingIndex);",
        "Oot3dTitleIntroOpeningActorSampleStatus Oot3d_TitleIntroOpeningActorRuntimeSamplePlayerMotion(",
        "    s32 frame,",
        "    Oot3dTitleIntroOpeningActorMotionSample* outSample",
        ");",
        "",
        "#endif",
    ]
    write_text(path, "\n".join(lines) + "\n")


def write_source(path: Path, data: dict[str, Any]) -> None:
    lines = [
        '#include "oot3d/title_intro_opening_actor_runtime.h"',
        "",
        "#include <string.h>",
        "",
        "const Oot3dTitleIntroOpeningActorBindingRow gOot3dTitleIntroOpeningActorBindingRows[] = {",
    ]
    for row in data["actor_bindings"]:
        values = [
            c_u16(row["actor_binding_index"]),
            c_u16(row["orchestration_index"]),
            c_u16(row["actor_kind_value"]),
            c_u16(row["source_scale_index"]),
            c_u16(row["source_animation_index"]),
            c_u16(row["source_motion_animation_index"]),
            c_u16(row["source_actor_init_index"]),
            c_u16(row["paired_actor_binding_index"]),
            c_u16(row["required_asset_ref_index"]),
            c_u16(row["logo_component_ref_start"]),
            c_u16(row["logo_component_ref_count"]),
            c_u16(row["logo_draw_ref_start"]),
            c_u16(row["logo_draw_ref_count"]),
            c_u16(row["logo_update_ref_start"]),
            c_u16(row["logo_update_ref_count"]),
            c_u32(row["actor_init_function"]),
            c_u32(row["actor_update_function"]),
            c_u32(row["actor_draw_function"]),
            c_float(row["actor_scale"]),
            c_float(row["gravity"]),
            c_float(row["shadow_scale"]),
            c_float(row["focus_y_offset"]),
            c_string(row["actor_role"]),
            c_string(row["actor_name"]),
            c_string(row["archive_path"]),
            c_string(row["cmb_name"]),
            c_string(row["init_csab_name"]),
            c_string(row["title_visual_csab_name"]),
            c_string(row["runtime_binding_role"]),
            c_string(row["native_basis"]),
            c_string(row["unresolved"]),
        ]
        lines.append("    { " + ", ".join(values) + " },")
    lines.extend(
        [
            "};",
            "const u32 gOot3dTitleIntroOpeningActorBindingRowCount = sizeof(gOot3dTitleIntroOpeningActorBindingRows) / sizeof(gOot3dTitleIntroOpeningActorBindingRows[0]);",
            "",
            "const Oot3dTitleIntroOpeningActorDirectRecordLayoutRow gOot3dTitleIntroOpeningActorDirectRecordLayoutRows[] = {",
        ]
    )
    for row in data["direct_record_layout_rows"]:
        values = [
            c_u16(row["direct_record_layout_index"]),
            c_u16(row["entry_direct_state_id"]),
            c_u16(row["consumer_direct_state_id"]),
            c_u16(row["completion_direct_state_id"]),
            c_u16(row["branch_direct_state_id"]),
            c_u32(row["entry_mode_change_function"]),
            c_u8(row["entry_mode_byte"]),
            c_u32(row["entry_global_gate_function"]),
            c_u8(row["entry_global_gate_byte"]),
            c_u32(row["state37_handler_function"]),
            c_u32(row["record_pointer_getter_function"]),
            c_u32(row["record_pointer_literal_pool_address"]),
            c_u32(row["record_context_address"]),
            c_u32(row["record_table_address"]),
            c_u32(row["record_pointer_address"]),
            c_u16(row["record_context_offset"]),
            c_u16(row["record_stride"]),
            c_u8(row["record_byte0_offset"]),
            c_u8(row["record_byte1_offset"]),
            c_u8(row["record_byte2_offset"]),
            c_string(row["record_byte0_role"]),
            c_string(row["record_byte1_role"]),
            c_string(row["record_byte2_role"]),
            c_string(row["record_byte0_source"]),
            c_string(row["record_byte1_source"]),
            c_string(row["record_byte2_source"]),
            c_u32(row["record_apply_function"]),
            c_u32(row["record_byte_table_literal_pool_address"]),
            c_u32(row["record_byte_table_address"]),
            c_u32(row["first_change_latch_address"]),
            c_u32(row["first_change_init_context_address"]),
            c_u32(row["record_change_notify_context_address"]),
            c_u16(row["record_apply_side_effect_index_limit"]),
            c_u32(row["state37_apply_current_callsite"]),
            c_u32(row["state37_apply_terminator_callsite"]),
            c_u16(row["state37_player_record_pointer_offset"]),
            c_u16(row["state37_player_last_record_value_offset"]),
            c_string(row["state37_cursor_source"]),
            c_string(row["record_pointer_status"]),
            c_string(row["record_apply_status"]),
            c_string(row["record_c_runtime_status"]),
            c_string(row["state37_playback_runtime_status"]),
            c_string(row["source_evidence"]),
            c_string(row["unresolved"]),
        ]
        lines.append("    { " + ", ".join(values) + " },")
    lines.extend(
        [
            "};",
            "const u32 gOot3dTitleIntroOpeningActorDirectRecordLayoutRowCount = sizeof(gOot3dTitleIntroOpeningActorDirectRecordLayoutRows) / sizeof(gOot3dTitleIntroOpeningActorDirectRecordLayoutRows[0]);",
            "",
            "const Oot3dTitleIntroOpeningActorMotionCueRow gOot3dTitleIntroOpeningActorMotionCueRows[] = {",
        ]
    )
    for row in data["motion_timeline_rows"]:
        values = [
            c_u16(row["timeline_index"]),
            c_u16(row["orchestration_index"]),
            c_u16(row["actor_binding_index"]),
            c_u16(row["paired_mount_actor_binding_index"]),
            c_u16(row["motion_ref_index"]),
            c_u16(row["player_action_ref_index"]),
            c_u16(row["player_action_source_index"]),
            c_u16(row["native_command_source_index"]),
            c_u16(row["local_command_index"]),
            c_u16(row["entry_index"]),
            c_u16(row["action_id"]),
            c_u16(row["start_frame"]),
            c_u16(row["end_frame"]),
            c_u16(row["duration_frames"]),
            c_u8(row["direct_state_case_present"]),
            c_u8(row["not_direct_state_case"]),
            c_u8(row["has_quantized_motion_vector"]),
            c_u8(row["speed_clamped"]),
            c_u8(row["start_exclusive_end_inclusive"]),
            c_u16(row["direct_record_layout_index"]),
            c_u16(row["direct_state_id"]),
            c_u32(row["direct_state_switch_function"]),
            c_u32(row["direct_state_switch_wrapper_function"]),
            c_u32(row["direct_state_setter_function"]),
            c_u32(row["direct_state_handler_function"]),
            c_float(row["native_speed"]),
            c_s16(row["native_heading_s16"]),
            c_string(row["action_id_hex"]),
            c_string(row["direct_state_id_hex"]),
            c_string(row["direct_state_features"]),
            c_string(row["direct_state_calls"]),
            c_string(row["direct_state_outgoing_targets"]),
            c_string(row["player_action_consumer_status"]),
            c_string(row["direct_state_evidence"]),
            c_string(row["source_semantic"]),
            c_string(row["unresolved"]),
        ]
        lines.append("    { " + ", ".join(values) + " },")
    lines.extend(
        [
            "};",
            "const u32 gOot3dTitleIntroOpeningActorMotionCueRowCount = sizeof(gOot3dTitleIntroOpeningActorMotionCueRows) / sizeof(gOot3dTitleIntroOpeningActorMotionCueRows[0]);",
            "",
            "const Oot3dTitleIntroOpeningActorBindingRow* Oot3d_TitleIntroOpeningActorRuntimeGetBinding(u16 actorBindingIndex) {",
            "    if (actorBindingIndex >= gOot3dTitleIntroOpeningActorBindingRowCount) {",
            "        return 0;",
            "    }",
            "    return &gOot3dTitleIntroOpeningActorBindingRows[actorBindingIndex];",
            "}",
            "",
            "const Oot3dTitleIntroOpeningActorDirectRecordLayoutRow* Oot3d_TitleIntroOpeningActorRuntimeGetDirectRecordLayout(u16 directRecordLayoutIndex) {",
            "    if (directRecordLayoutIndex >= gOot3dTitleIntroOpeningActorDirectRecordLayoutRowCount) {",
            "        return 0;",
            "    }",
            "    return &gOot3dTitleIntroOpeningActorDirectRecordLayoutRows[directRecordLayoutIndex];",
            "}",
            "",
            "const Oot3dTitleIntroOpeningActorMotionCueRow* Oot3d_TitleIntroOpeningActorRuntimeGetMotionCue(u16 timelineIndex) {",
            "    if (timelineIndex >= gOot3dTitleIntroOpeningActorMotionCueRowCount) {",
            "        return 0;",
            "    }",
            "    return &gOot3dTitleIntroOpeningActorMotionCueRows[timelineIndex];",
            "}",
            "",
            "static u8 Oot3d_TitleIntroOpeningActorRuntimeFrameMatchesCue(",
            "    const Oot3dTitleIntroOpeningActorMotionCueRow* row,",
            "    s32 frame",
            ") {",
            "    if (row == 0 || row->startExclusiveEndInclusive == 0u) {",
            "        return 0u;",
            "    }",
            "    return ((s32)row->startFrame < frame && frame <= (s32)row->endFrame) ? 1u : 0u;",
            "}",
            "",
            "const Oot3dTitleIntroOpeningActorMotionCueRow* Oot3d_TitleIntroOpeningActorRuntimeFindActivePlayerMotionCue(s32 frame) {",
            "    const Oot3dTitleIntroOpeningActorMotionCueRow* best = 0;",
            "    u32 index;",
            "",
            "    for (index = 0; index < gOot3dTitleIntroOpeningActorMotionCueRowCount; index++) {",
            "        const Oot3dTitleIntroOpeningActorMotionCueRow* row = &gOot3dTitleIntroOpeningActorMotionCueRows[index];",
            "        if (Oot3d_TitleIntroOpeningActorRuntimeFrameMatchesCue(row, frame) == 0u) {",
            "            continue;",
            "        }",
            "        if (best == 0 || row->localCommandIndex > best->localCommandIndex ||",
            "            (row->localCommandIndex == best->localCommandIndex && row->entryIndex >= best->entryIndex)) {",
            "            best = row;",
            "        }",
            "    }",
            "    return best;",
            "}",
            "",
            "const Oot3dTitleIntroLinkBoyPlayerActionRow* Oot3d_TitleIntroOpeningActorRuntimeGetPlayerActionSourceRow(u16 playerActionRefIndex) {",
            "    u32 index;",
            "",
            "    for (index = 0; index < gOot3dTitleIntroLinkBoyPlayerActionRowCount; index++) {",
            "        const Oot3dTitleIntroLinkBoyPlayerActionRow* row = &gOot3dTitleIntroLinkBoyPlayerActionRows[index];",
            "        if (row->playerActionIndex == playerActionRefIndex) {",
            "            return row;",
            "        }",
            "    }",
            "    return 0;",
            "}",
            "",
            "const Oot3dTitleIntroActorCueRow* Oot3d_TitleIntroOpeningActorRuntimeFindPairedMountCueSourceRow(",
            "    const Oot3dTitleIntroLinkBoyPlayerActionRow* playerActionRow,",
            "    s32 frame",
            ") {",
            "    const Oot3dTitleIntroActorCueRow* first = 0;",
            "    const Oot3dTitleIntroActorCueRow* held = 0;",
            "    const Oot3dTitleIntroActorCueRow* active = 0;",
            "    u32 index;",
            "",
            "    if (playerActionRow == 0) {",
            "        return 0;",
            "    }",
            "    for (index = 0; index < gOot3dTitleIntroActorCueRowCount; index++) {",
            "        const Oot3dTitleIntroActorCueRow* row = &gOot3dTitleIntroActorCueRows[index];",
            "        if (row->qdbIndex != playerActionRow->qdbIndex ||",
            "            row->cueCommandId != OOT3D_TITLE_INTRO_OPENING_EPONA_MOUNT_CUE_COMMAND_ID) {",
            "            continue;",
            "        }",
            "        if (first == 0) {",
            "            first = row;",
            "        }",
            "        if (frame >= (s32)row->startFrame) {",
            "            held = row;",
            "        }",
            "        if (frame >= (s32)row->startFrame && frame < (s32)row->endFrame) {",
            "            active = row;",
            "            break;",
            "        }",
            "    }",
            "    return active != 0 ? active : (held != 0 ? held : first);",
            "}",
            "",
            "const Oot3dTitleIntroActorScaleRow* Oot3d_TitleIntroOpeningActorRuntimeGetScaleSourceRow(u16 actorBindingIndex) {",
            "    const Oot3dTitleIntroOpeningActorBindingRow* binding = Oot3d_TitleIntroOpeningActorRuntimeGetBinding(actorBindingIndex);",
            "    if (binding == 0 || binding->sourceScaleIndex == OOT3D_TITLE_INTRO_OPENING_ACTOR_RUNTIME_NO_INDEX ||",
            "        binding->sourceScaleIndex >= gOot3dTitleIntroActorScaleRowCount) {",
            "        return 0;",
            "    }",
            "    return &gOot3dTitleIntroActorScaleRows[binding->sourceScaleIndex];",
            "}",
            "",
            "const Oot3dTitleIntroActorAnimationRow* Oot3d_TitleIntroOpeningActorRuntimeGetAnimationSourceRow(u16 actorBindingIndex) {",
            "    const Oot3dTitleIntroOpeningActorBindingRow* binding = Oot3d_TitleIntroOpeningActorRuntimeGetBinding(actorBindingIndex);",
            "    if (binding == 0 || binding->sourceAnimationIndex == OOT3D_TITLE_INTRO_OPENING_ACTOR_RUNTIME_NO_INDEX ||",
            "        binding->sourceAnimationIndex >= gOot3dTitleIntroActorAnimationRowCount) {",
            "        return 0;",
            "    }",
            "    return &gOot3dTitleIntroActorAnimationRows[binding->sourceAnimationIndex];",
            "}",
            "",
            "const Oot3dTitleIntroActorMotionAnimationRow* Oot3d_TitleIntroOpeningActorRuntimeGetMotionAnimationSourceRow(u16 actorBindingIndex) {",
            "    const Oot3dTitleIntroOpeningActorBindingRow* binding = Oot3d_TitleIntroOpeningActorRuntimeGetBinding(actorBindingIndex);",
            "    if (binding == 0 || binding->sourceMotionAnimationIndex == OOT3D_TITLE_INTRO_OPENING_ACTOR_RUNTIME_NO_INDEX ||",
            "        binding->sourceMotionAnimationIndex >= gOot3dTitleIntroActorMotionAnimationRowCount) {",
            "        return 0;",
            "    }",
            "    return &gOot3dTitleIntroActorMotionAnimationRows[binding->sourceMotionAnimationIndex];",
            "}",
            "",
            "const Oot3dTitleIntroActorInitSourceRow* Oot3d_TitleIntroOpeningActorRuntimeGetActorInitSourceRow(u16 actorBindingIndex) {",
            "    const Oot3dTitleIntroOpeningActorBindingRow* binding = Oot3d_TitleIntroOpeningActorRuntimeGetBinding(actorBindingIndex);",
            "    if (binding == 0 || binding->sourceActorInitIndex == OOT3D_TITLE_INTRO_OPENING_ACTOR_RUNTIME_NO_INDEX ||",
            "        binding->sourceActorInitIndex >= gOot3dTitleIntroActorInitSourceRowCount) {",
            "        return 0;",
            "    }",
            "    return &gOot3dTitleIntroActorInitSourceRows[binding->sourceActorInitIndex];",
            "}",
            "",
            "Oot3dTitleIntroOpeningActorSampleStatus Oot3d_TitleIntroOpeningActorRuntimeSamplePlayerMotion(",
            "    s32 frame,",
            "    Oot3dTitleIntroOpeningActorMotionSample* outSample",
            ") {",
            "    const Oot3dTitleIntroOpeningActorMotionCueRow* cue;",
            "    const Oot3dTitleIntroOpeningPlayerMotionRow* motionRow;",
            "    Oot3dTitleIntroPlayerActionMotionStatus motionStatus;",
            "",
            "    if (outSample == 0) {",
            "        return OOT3D_TITLE_INTRO_OPENING_ACTOR_SAMPLE_NULL_OUTPUT;",
            "    }",
            "    memset(outSample, 0, sizeof(*outSample));",
            "    outSample->frame = frame;",
            "    cue = Oot3d_TitleIntroOpeningActorRuntimeFindActivePlayerMotionCue(frame);",
            "    if (cue == 0) {",
            "        return OOT3D_TITLE_INTRO_OPENING_ACTOR_SAMPLE_NO_ACTIVE_CUE;",
            "    }",
            "    motionRow = Oot3d_TitleIntroOpeningGetPlayerMotionRow(cue->motionRefIndex);",
            "    motionStatus = Oot3d_TitleIntroOpeningComputePlayerMotionRow(motionRow, 0, 0, &outSample->motion);",
            "    if (motionStatus != OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_OK) {",
            "        return OOT3D_TITLE_INTRO_OPENING_ACTOR_SAMPLE_MOTION_ERROR;",
            "    }",
            "    motionStatus = Oot3d_TitleIntroOpeningDecodePlayerMotionTransform(motionRow, &outSample->playerActionTransform);",
            "    if (motionStatus != OOT3D_TITLE_INTRO_PLAYER_ACTION_MOTION_OK) {",
            "        return OOT3D_TITLE_INTRO_OPENING_ACTOR_SAMPLE_MOTION_ERROR;",
            "    }",
            "    outSample->active = 1u;",
            "    outSample->timelineIndex = cue->timelineIndex;",
            "    outSample->actorBindingIndex = cue->actorBindingIndex;",
            "    outSample->pairedMountActorBindingIndex = cue->pairedMountActorBindingIndex;",
            "    outSample->motionRefIndex = cue->motionRefIndex;",
            "    outSample->playerActionRefIndex = cue->playerActionRefIndex;",
            "    outSample->playerActionSourceIndex = cue->playerActionSourceIndex;",
            "    outSample->actionId = cue->actionId;",
            "    outSample->nativeSpeed = outSample->motion.clampedSpeed;",
            "    outSample->nativeHeading = outSample->motion.heading;",
            "    outSample->actorBinding = Oot3d_TitleIntroOpeningActorRuntimeGetBinding(cue->actorBindingIndex);",
            "    outSample->pairedMountBinding = Oot3d_TitleIntroOpeningActorRuntimeGetBinding(cue->pairedMountActorBindingIndex);",
            "    outSample->cue = cue;",
            "    outSample->motionRow = motionRow;",
            "    outSample->playerActionRow = Oot3d_TitleIntroOpeningActorRuntimeGetPlayerActionSourceRow(cue->playerActionRefIndex);",
            "    outSample->pairedMountCueRow = Oot3d_TitleIntroOpeningActorRuntimeFindPairedMountCueSourceRow(",
            "        outSample->playerActionRow,",
            "        frame",
            "    );",
            "    return OOT3D_TITLE_INTRO_OPENING_ACTOR_SAMPLE_OK;",
            "}",
        ]
    )
    write_text(path, "\n".join(lines) + "\n")


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    lines = [
        "# OOT3D Open Title Actor Runtime Audit",
        "",
        "This audit binds the native open-title orchestration to runtime-facing actor rows and player-action motion samples.",
        "",
        "## Summary",
        "",
        f"- OK: {data['summary']['ok']}",
        f"- Actor bindings: {data['summary']['actor_binding_count']}",
        f"- Motion timeline rows: {data['summary']['motion_timeline_count']}",
        f"- Direct-state runtime rows: {data['summary']['direct_state_runtime_row_count']}",
        f"- Motion-only runtime rows: {data['summary']['motion_only_runtime_row_count']}",
        f"- Direct record layouts: {data['summary']['direct_record_layout_count']}",
        f"- Paired mount cue command: `{c_u32(data['summary']['paired_mount_cue_command_id'])}`",
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
            "## Native Selection Evidence",
            "",
            f"- File: `{data['native_selection_evidence']['file']}`",
            f"- `case 10` line: `{data['native_selection_evidence']['case_10_line']}`",
            f"- Active-window line: `{data['native_selection_evidence']['active_rule_line']}`",
            f"- `csCtx+0x40` assignment line: `{data['native_selection_evidence']['assign_csctx_40_line']}`",
            "",
            "## Actor Bindings",
            "",
            "| Binding | Kind | Actor | Archive | Model | Init CSAB | Title CSAB |",
            "| ---: | --- | --- | --- | --- | --- | --- |",
        ]
    )
    for row in data["actor_bindings"]:
        lines.append(
            f"| `{row['actor_binding_index']}` | `{row['actor_kind']}` | `{row['actor_name']}` | "
            f"`{row['archive_path']}` | `{row['cmb_name']}` | `{row['init_csab_name']}` | "
            f"`{row['title_visual_csab_name']}` |"
        )

    lines.extend(
        [
            "",
            "## Sample Frames",
            "",
            "| Frame | Active | Timeline | Motion Ref | Player Action | Mount Cue | Action | Direct State | Record Layout | Speed | Heading | Consumer |",
            "| ---: | --- | ---: | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | --- |",
        ]
    )
    for row in data["sample_rows"]:
        lines.append(
            f"| `{row['frame']}` | `{row['active']}` | `{row['timeline_index']}` | "
            f"`{row['motion_ref_index']}` | `{row['player_action_ref_index']}` | "
            f"`{row['paired_mount_actor_cue_index']}` | `{row['action_id_hex']}` | "
            f"`{row['direct_state_id']}` | `{row['direct_record_layout_index']}` | "
            f"`{row['native_speed']}` | `{row['native_heading_s16']}` | "
            f"`{row['player_action_consumer_status']}` |"
        )

    lines.extend(
        [
            "",
            "## Direct Record Layouts",
            "",
            "| Layout | Entry | Consumer | Record | Context | Getter | Apply | Player offsets | Runtime status |",
            "| ---: | ---: | ---: | --- | --- | --- | --- | --- | --- |",
        ]
    )
    for row in data["direct_record_layout_rows"]:
        lines.append(
            f"| `{row['direct_record_layout_index']}` | `{row['entry_direct_state_id']}` | "
            f"`{row['consumer_direct_state_id']}` | `{c_u32(row['record_pointer_address'])}` "
            f"`+0x{row['record_context_offset']:02X}` stride `{row['record_stride']}` | "
            f"`{c_u32(row['record_context_address'])}` | `{c_u32(row['record_pointer_getter_function'])}` | "
            f"`{c_u32(row['record_apply_function'])}` -> `{c_u32(row['record_byte_table_address'])}` | "
            f"`+0x{row['state37_player_record_pointer_offset']:04X}`/`+0x{row['state37_player_last_record_value_offset']:04X}` | "
            f"`{row['record_c_runtime_status']}`; `{row['state37_playback_runtime_status']}`; "
            f"entry `{c_u32(row['entry_mode_change_function'])}({row['entry_mode_byte']})`, "
            f"`{c_u32(row['entry_global_gate_function'])}({row['entry_global_gate_byte']})` |"
        )
    lines.extend(
        [
            "",
            "| Byte | Role | Source |",
            "| ---: | --- | --- |",
        ]
    )
    for row in data["direct_record_layout_rows"]:
        lines.append(f"| `+{row['record_byte0_offset']}` | `{row['record_byte0_role']}` | {row['record_byte0_source']} |")
        lines.append(f"| `+{row['record_byte1_offset']}` | `{row['record_byte1_role']}` | {row['record_byte1_source']} |")
        lines.append(f"| `+{row['record_byte2_offset']}` | `{row['record_byte2_role']}` | {row['record_byte2_source']} |")

    direct_actions = sorted(
        {
            (row["action_id_hex"], row["direct_state_id"], row["direct_state_handler_function"],
             row["direct_record_layout_index"], row["direct_state_outgoing_targets"],
             row["player_action_consumer_status"])
            for row in data["motion_timeline_rows"]
            if int(row["direct_state_id"]) != NO_INDEX
        }
    )
    motion_only_actions = sorted(
        {
            (row["action_id_hex"], row["player_action_consumer_status"])
            for row in data["motion_timeline_rows"]
            if int(row["direct_state_id"]) == NO_INDEX
        }
    )
    lines.extend(
        [
            "",
            "## Player-Action Consumer Split",
            "",
            "| Action | Direct State | Handler | Record Layout | Outgoing | Consumer Status |",
            "| --- | ---: | ---: | ---: | --- | --- |",
        ]
    )
    for action_hex, direct_state_id, handler, record_layout_index, outgoing, status in direct_actions:
        lines.append(
            f"| `{action_hex}` | `{direct_state_id}` | `{c_u32(handler)}` | "
            f"`{record_layout_index}` | `{outgoing}` | `{status}` |"
        )
    for action_hex, status in motion_only_actions:
        lines.append(f"| `{action_hex}` | `65535` | `0x00000000u` | `65535` | `` | `{status}` |")

    lines.extend(["", "## Unresolved", ""])
    for item in data["unresolved"]:
        lines.append(f"- {item}")
    write_text(path, "\n".join(lines) + "\n")


def main() -> None:
    data = build_report()
    write_json(OUT_JSON, data)
    write_markdown(OUT_MD, data)
    write_csv(OUT_TIMELINE_CSV, data["motion_timeline_rows"])
    write_header(OUT_HEADER, data)
    write_source(OUT_SOURCE, data)
    if not data["summary"]["ok"]:
        raise SystemExit("title-intro opening actor runtime audit failed")


if __name__ == "__main__":
    main()
