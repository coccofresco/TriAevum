#!/usr/bin/env python3
"""Build the OOT3D open-title intro orchestration source table.

This table binds the first title/opening target to the native OOT3D scene
cutscene, player-action rows, camera CMAD curves, title actors, and title-logo
routes already decoded from assets and code.bin.
"""

from __future__ import annotations

import csv
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"

TITLE_SOURCE = ANALYSIS / "title_intro_source_table.json"
NATIVE_CUTSCENE = ANALYSIS / "scene_cutscene_native_source_table.json"
PLAYER_ACTION = ANALYSIS / "scene_cutscene_player_action_table.json"
CAMERA_CMAD = ANALYSIS / "scene_cutscene_camera_cmad_table.json"
SLOT6_CAMERA_MATCH = ANALYSIS / "title_intro_slot6_camera_match.json"

OUT_JSON = ANALYSIS / "title_intro_opening_orchestration.json"
OUT_MD = ANALYSIS / "title_intro_opening_orchestration.md"
OUT_ORCHESTRATION_CSV = ANALYSIS / "title_intro_opening_orchestration.csv"
OUT_ASSET_REF_CSV = ANALYSIS / "title_intro_opening_orchestration_asset_refs.csv"
OUT_PLAYER_ACTION_REF_CSV = ANALYSIS / "title_intro_opening_orchestration_player_action_refs.csv"
OUT_CAMERA_CURVE_REF_CSV = ANALYSIS / "title_intro_opening_orchestration_camera_curve_refs.csv"
OUT_NATIVE_COMMAND_REF_CSV = ANALYSIS / "title_intro_opening_orchestration_native_command_refs.csv"
OUT_HEADER = ROOT / "include" / "oot3d" / "title_intro_opening_orchestration.h"
OUT_SOURCE = ROOT / "src" / "code" / "z_title_intro_opening_orchestration.c"

OPEN_TITLE_SCENE_PATH = "spot99_info.zsi"
OPEN_TITLE_SCENE_ID = 0x6B
OPEN_TITLE_SETUP_INDEX = 1
OPEN_TITLE_CUTSCENE_SOURCE_INDEX = 94
OPEN_TITLE_CAMERA_BLOB_SEGMENT_SOURCE_INDEX = 452
OPEN_TITLE_CAMERA_BLOB_SOURCE_INDEX = 88

REQUIRED_ASSET_ROLES = [
    "scene_main_zsi",
    "scene_room_zsi",
    "actor_link_opening",
    "actor_epona_horse",
    "actor_title_logo",
    "actor_keep_opening_common",
]


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def int_value(value: Any, default: int = 0) -> int:
    try:
        if value is None:
            return default
        if isinstance(value, str) and value.lower().startswith("0x"):
            return int(value, 16)
        return int(value)
    except (TypeError, ValueError):
        return default


def float_value(value: Any, default: float = 0.0) -> float:
    try:
        if value is None:
            return default
        return float(value)
    except (TypeError, ValueError):
        return default


def find_one(rows: list[dict[str, Any]], label: str, **criteria: Any) -> dict[str, Any]:
    matches = [
        row
        for row in rows
        if all(row.get(key) == value for key, value in criteria.items())
    ]
    if len(matches) != 1:
        raise SystemExit(f"{label}: expected one row for {criteria}, found {len(matches)}")
    return matches[0]


def c_string(value: Any) -> str:
    text = "" if value is None else str(value)
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def c_u8(value: Any) -> str:
    return f"{int_value(value) & 0xFF}u"


def c_u16(value: Any) -> str:
    return f"{int_value(value) & 0xFFFF}u"


def c_u32(value: Any) -> str:
    return f"{int_value(value) & 0xFFFFFFFF}u"


def c_s32(value: Any) -> str:
    return f"{int_value(value)}"


def c_float(value: Any) -> str:
    return f"{float_value(value):.9g}f"


def rel(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def collect_asset_refs(title_source: dict[str, Any]) -> list[dict[str, Any]]:
    refs: list[dict[str, Any]] = []
    assets = {row["role"]: row for row in title_source["assets"]}
    for ref_index, role in enumerate(REQUIRED_ASSET_ROLES):
        asset = assets[role]
        refs.append(
            {
                "asset_ref_index": ref_index,
                "orchestration_index": 0,
                "asset_index": asset["asset_index"],
                "asset_role": asset["role"],
                "romfs_path": asset["romfs_path"],
                "required": 1,
                "present": int(bool(asset["exists"])),
                "basis": "required_by_open_title_scene_actor_logo_or_camera_route",
            }
        )
    return refs


def collect_player_action_refs(player_action: dict[str, Any]) -> list[dict[str, Any]]:
    rows = [
        row
        for row in player_action["player_action_rows"]
        if row["cutscene_source_index"] == OPEN_TITLE_CUTSCENE_SOURCE_INDEX
        and row["setup_index"] == OPEN_TITLE_SETUP_INDEX
    ]
    rows.sort(key=lambda row: (row["start_frame"], row["native_command_source_index"], row["entry_index"]))
    refs: list[dict[str, Any]] = []
    for ref_index, row in enumerate(rows):
        refs.append(
            {
                "player_action_ref_index": ref_index,
                "orchestration_index": 0,
                "player_action_source_index": row["player_action_source_index"],
                "native_command_source_index": row["native_command_source_index"],
                "local_command_index": row["local_command_index"],
                "entry_index": row["entry_index"],
                "action_id": row["action_id"],
                "action_id_hex": row["action_id_hex"],
                "start_frame": row["start_frame"],
                "end_frame": row["end_frame"],
                "duration_frames": row["duration_frames"],
                "runtime_semantic": row["runtime_semantic"],
                "raw_words_text": row["raw_words_text"],
            }
        )
    return refs


def collect_camera_curve_refs(camera_cmad: dict[str, Any]) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    cmad_records = [
        row
        for row in camera_cmad["cmad_record_rows"]
        if row["cutscene_source_index"] == OPEN_TITLE_CUTSCENE_SOURCE_INDEX
        and row["setup_index"] == OPEN_TITLE_SETUP_INDEX
        and row["camera_blob_segment_source_index"] == OPEN_TITLE_CAMERA_BLOB_SEGMENT_SOURCE_INDEX
    ]
    cmad_records.sort(key=lambda row: (row["cmad_record_index"], row["channel_type"]))
    curves = [
        row
        for row in camera_cmad["camera_curve_rows"]
        if row["cutscene_source_index"] == OPEN_TITLE_CUTSCENE_SOURCE_INDEX
        and row["setup_index"] == OPEN_TITLE_SETUP_INDEX
        and row["camera_blob_segment_source_index"] == OPEN_TITLE_CAMERA_BLOB_SEGMENT_SOURCE_INDEX
    ]
    curves.sort(key=lambda row: (row["cmad_record_index"], row["curve_slot_index"]))
    refs: list[dict[str, Any]] = []
    for ref_index, row in enumerate(curves):
        refs.append(
            {
                "camera_curve_ref_index": ref_index,
                "orchestration_index": 0,
                "camera_curve_source_index": row["camera_curve_source_index"],
                "cmad_record_source_index": row["cmad_record_source_index"],
                "camera_blob_segment_source_index": row["camera_blob_segment_source_index"],
                "camera_blob_source_index": row["camera_blob_source_index"],
                "native_command_source_index": row["native_command_source_index"],
                "channel_type": row["channel_type"],
                "curve_slot_index": row["curve_slot_index"],
                "output_field_offset": row["output_field_offset"],
                "curve_role": row["curve_role"],
                "interpolation_type": row["interpolation_type"],
                "point_count": row["point_count"],
            }
        )
    return cmad_records, refs


def collect_native_command_refs(native_cutscene: dict[str, Any]) -> list[dict[str, Any]]:
    commands = [
        row
        for row in native_cutscene["native_command_rows"]
        if row["cutscene_source_index"] == OPEN_TITLE_CUTSCENE_SOURCE_INDEX
        and row["setup_index"] == OPEN_TITLE_SETUP_INDEX
    ]
    commands.sort(key=lambda row: row["local_command_index"])
    refs: list[dict[str, Any]] = []
    for ref_index, row in enumerate(commands):
        refs.append(
            {
                "native_command_ref_index": ref_index,
                "orchestration_index": 0,
                "native_command_source_index": row["native_command_source_index"],
                "local_command_index": row["local_command_index"],
                "command_id": row["command_id"],
                "command_id_hex": row["command_id_hex"],
                "command_name": row["command_name"],
                "category": row["category"],
                "semantic_kind": row["semantic_kind"],
                "entry_count": row["entry_count"],
                "blob_size": row["blob_size"],
                "command_offset": row["command_offset"],
                "command_offset_hex": row["command_offset_hex"],
            }
        )
    return refs


def build_report() -> dict[str, Any]:
    title_source = read_json(TITLE_SOURCE)
    native_cutscene = read_json(NATIVE_CUTSCENE)
    player_action = read_json(PLAYER_ACTION)
    camera_cmad = read_json(CAMERA_CMAD)
    slot6 = read_json(SLOT6_CAMERA_MATCH)

    cutscene = find_one(
        native_cutscene["cutscene_rows"],
        "open-title cutscene",
        cutscene_source_index=OPEN_TITLE_CUTSCENE_SOURCE_INDEX,
        setup_index=OPEN_TITLE_SETUP_INDEX,
    )
    slot6_summary = slot6["summary"]
    best_match = find_one(
        slot6["matches"],
        "open-title camera match",
        cutscene_source_index=OPEN_TITLE_CUTSCENE_SOURCE_INDEX,
        setup_index=OPEN_TITLE_SETUP_INDEX,
        camera_blob_segment_source_index=OPEN_TITLE_CAMERA_BLOB_SEGMENT_SOURCE_INDEX,
    )
    asset_refs = collect_asset_refs(title_source)
    player_refs = collect_player_action_refs(player_action)
    cmad_records, camera_curve_refs = collect_camera_curve_refs(camera_cmad)
    native_command_refs = collect_native_command_refs(native_cutscene)

    link_scale = find_one(title_source["title_actor_scale_rows"], "opening Link scale", actor_role="opening_link_adult")
    epona_scale = find_one(title_source["title_actor_scale_rows"], "opening Epona scale", actor_role="opening_epona")
    link_anim = find_one(title_source["title_actor_animation_rows"], "opening Link animation", actor_role="opening_link_adult")
    epona_anim = find_one(title_source["title_actor_animation_rows"], "opening Epona animation", actor_role="opening_epona")
    logo_init = title_source["title_logo_actor_init"][0]
    logo_draw_context = title_source["title_logo_draw_context_rows"][0]

    max_player_action_end = max((row["end_frame"] for row in player_refs), default=0)
    checks = {
        "native_title_cutscene_decoded": bool(cutscene["native_decoded"]),
        "native_cutscene_scene_matches_title_scene": cutscene["scene_path"] == OPEN_TITLE_SCENE_PATH,
        "native_command_count_matches_header": len(native_command_refs) == cutscene["native_command_ref_count"],
        "required_assets_present": all(row["present"] != 0 for row in asset_refs),
        "player_actions_decoded": len(player_refs) == 15,
        "camera_segment_matches_slot6_trace": slot6_summary["status"] == "pass"
        and best_match["eye_distance_to_emulator"] < 0.01,
        "camera_cmad_curves_decoded": len(cmad_records) == 2 and len(camera_curve_refs) == 6,
        "opening_link_and_epona_actor_routes_present": link_scale["scale_index"] == 0
        and epona_scale["scale_index"] == 1
        and link_anim["animation_index"] == 0
        and epona_anim["animation_index"] == 1,
        "title_logo_actor_route_present": logo_init["actor_name"] == "ACTOR_EN_MAG"
        and logo_draw_context["context_index"] == 0,
    }
    orchestration_rows = [
        {
            "orchestration_index": 0,
            "role": "open_title_initial_hyrule_field",
            "scene_id": OPEN_TITLE_SCENE_ID,
            "scene_path": OPEN_TITLE_SCENE_PATH,
            "setup_index": OPEN_TITLE_SETUP_INDEX,
            "cutscene_source_index": OPEN_TITLE_CUTSCENE_SOURCE_INDEX,
            "native_header_offset": cutscene["native_header_offset"],
            "native_header_offset_hex": f"0x{int_value(cutscene['native_header_offset']):08X}",
            "native_end_frame": cutscene["native_end_frame"],
            "native_command_ref_start": cutscene["native_command_ref_start"],
            "native_command_ref_count": cutscene["native_command_ref_count"],
            "native_command_local_ref_start": 0,
            "native_command_local_ref_count": len(native_command_refs),
            "player_action_ref_start": 0,
            "player_action_ref_count": len(player_refs),
            "player_action_max_end_frame": max_player_action_end,
            "camera_blob_source_index": OPEN_TITLE_CAMERA_BLOB_SOURCE_INDEX,
            "camera_blob_segment_source_index": OPEN_TITLE_CAMERA_BLOB_SEGMENT_SOURCE_INDEX,
            "camera_cmad_record_ref_start": 0,
            "camera_cmad_record_ref_count": len(cmad_records),
            "camera_curve_ref_start": 0,
            "camera_curve_ref_count": len(camera_curve_refs),
            "slot6_eye_distance_to_emulator": best_match["eye_distance_to_emulator"],
            "slot6_trace_path": slot6_summary["trace_path"],
            "link_actor_scale_index": link_scale["scale_index"],
            "epona_actor_scale_index": epona_scale["scale_index"],
            "link_actor_animation_index": link_anim["animation_index"],
            "epona_actor_animation_index": epona_anim["animation_index"],
            "horse_motion_animation_index": title_source["title_actor_motion_animation_rows"][0]["motion_index"],
            "title_logo_actor_init_index": logo_init["actor_init_index"],
            "title_logo_component_ref_start": 0,
            "title_logo_component_ref_count": len(title_source["title_logo_component_rows"]),
            "title_logo_draw_ref_start": 0,
            "title_logo_draw_ref_count": len(title_source["title_logo_draw_rows"]),
            "title_logo_update_ref_start": 0,
            "title_logo_update_ref_count": len(title_source["title_logo_update_rows"]),
            "basis": "native spot99_info.zsi setup-1 cutscene command 0x17, title-room PICA draw identity, slot-6 Azahar camera validation, code.bin actor/logo routes",
            "unresolved": "promote cue action ids 0x24/0x40/0x41 consumer semantics and instantiate scene actors/camera/logo in engine runtime",
        }
    ]

    return {
        "format": "oot3d_title_intro_opening_orchestration_v1",
        "inputs": {
            "title_source": rel(TITLE_SOURCE),
            "native_cutscene": rel(NATIVE_CUTSCENE),
            "player_action": rel(PLAYER_ACTION),
            "camera_cmad": rel(CAMERA_CMAD),
            "slot6_camera_match": rel(SLOT6_CAMERA_MATCH),
        },
        "summary": {
            "ok": all(checks.values()),
            "checks": checks,
            "next_gate": "Promote the open-title cue consumer for action ids 0x24/0x40/0x41 and bind this orchestration row to the engine scene/camera/logo runtime.",
            "native_cutscene_end_frame": cutscene["native_end_frame"],
            "player_action_max_end_frame": max_player_action_end,
            "player_action_end_exceeds_header": max_player_action_end > cutscene["native_end_frame"],
        },
        "orchestration_rows": orchestration_rows,
        "required_asset_refs": asset_refs,
        "native_command_refs": native_command_refs,
        "player_action_refs": player_refs,
        "camera_cmad_record_refs": cmad_records,
        "camera_curve_refs": camera_curve_refs,
        "slot6_best_match": best_match,
        "unresolved": [
            "The open-title scene/setup/camera/actor/logo route is now explicit, but cue action ids 0x24/0x40/0x41 still need exact OOT3D consumer semantics.",
            "The native cutscene header end frame is 2400 while decoded player-action rows reach frame 3036; keep both values and validate the runtime stop condition in code.bin before clamping.",
            "spot00.zar remains the native external Epona/player QDB source; the active title scene, room, and duplicate in-scene cutscene are spot99 setup 1 / cutscene 94.",
        ],
    }


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def write_json(path: Path, data: Any) -> None:
    write_text(path, json.dumps(data, indent=2) + "\n")


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        if not rows:
            handle.write("")
            return
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    row = data["orchestration_rows"][0]
    lines = [
        "# OOT3D Title Intro Opening Orchestration",
        "",
        "This table binds the first open-title intro target to the native OOT3D scene cutscene route.",
        "",
        "## Summary",
        "",
        f"- OK: {data['summary']['ok']}",
        f"- Scene: `{row['scene_path']}` scene `0x{row['scene_id']:02X}`, setup `{row['setup_index']}`, cutscene `{row['cutscene_source_index']}`",
        f"- Native commands: {row['native_command_local_ref_count']}",
        f"- Player-action refs: {row['player_action_ref_count']} (max end frame {row['player_action_max_end_frame']})",
        f"- Camera segment: blob `{row['camera_blob_source_index']}`, segment `{row['camera_blob_segment_source_index']}`, curves `{row['camera_curve_ref_count']}`",
        f"- Slot-6 eye distance to emulator: {row['slot6_eye_distance_to_emulator']:.6f}",
        f"- Next gate: {data['summary']['next_gate']}",
        "",
        "## Checks",
        "",
        "| Check | Status |",
        "| --- | --- |",
    ]
    for key, value in data["summary"]["checks"].items():
        lines.append(f"| `{key}` | `{value}` |")

    lines.extend(["", "## Required Assets", "", "| Role | Index | Path | Present |", "| --- | ---: | --- | --- |"])
    for asset in data["required_asset_refs"]:
        lines.append(
            f"| `{asset['asset_role']}` | {asset['asset_index']} | `{asset['romfs_path']}` | `{bool(asset['present'])}` |"
        )

    lines.extend(["", "## Native Commands", "", "| Local | Command | Entries | Category |", "| ---: | --- | ---: | --- |"])
    for command in data["native_command_refs"]:
        lines.append(
            f"| {command['local_command_index']} | `{command['command_name']}` `{command['command_id_hex']}` | {command['entry_count']} | `{command['category']}` |"
        )

    lines.extend(["", "## Player Actions", "", "| Ref | Action | Frames | Command |", "| ---: | --- | --- | ---: |"])
    for ref in data["player_action_refs"]:
        lines.append(
            f"| {ref['player_action_ref_index']} | `{ref['action_id_hex']}` | {ref['start_frame']}..{ref['end_frame']} | {ref['native_command_source_index']} |"
        )

    lines.extend(["", "## Camera Curves", "", "| Ref | Channel | Slot | Field | Points | Role |", "| ---: | ---: | ---: | ---: | ---: | --- |"])
    for ref in data["camera_curve_refs"]:
        lines.append(
            f"| {ref['camera_curve_ref_index']} | {ref['channel_type']} | {ref['curve_slot_index']} | `0x{ref['output_field_offset']:02X}` | {ref['point_count']} | `{ref['curve_role']}` |"
        )

    lines.extend(["", "## Unresolved", ""])
    for item in data["unresolved"]:
        lines.append(f"- {item}")
    write_text(path, "\n".join(lines) + "\n")


def write_header(path: Path, data: dict[str, Any]) -> None:
    row = data["orchestration_rows"][0]
    lines = [
        "#ifndef OOT3D_TITLE_INTRO_OPENING_ORCHESTRATION_H",
        "#define OOT3D_TITLE_INTRO_OPENING_ORCHESTRATION_H",
        "",
        "#include \"oot3d/scene.h\"",
        "",
        "enum {",
        "    OOT3D_TITLE_INTRO_OPENING_NO_ORCHESTRATION_INDEX = 0xffff,",
        f"    OOT3D_TITLE_INTRO_OPENING_ORCHESTRATION_ROW_COUNT = {len(data['orchestration_rows'])},",
        f"    OOT3D_TITLE_INTRO_OPENING_REQUIRED_ASSET_REF_COUNT = {len(data['required_asset_refs'])},",
        f"    OOT3D_TITLE_INTRO_OPENING_NATIVE_COMMAND_REF_COUNT = {len(data['native_command_refs'])},",
        f"    OOT3D_TITLE_INTRO_OPENING_PLAYER_ACTION_REF_COUNT = {len(data['player_action_refs'])},",
        f"    OOT3D_TITLE_INTRO_OPENING_CAMERA_CURVE_REF_COUNT = {len(data['camera_curve_refs'])},",
        f"    OOT3D_TITLE_INTRO_OPENING_SCENE_ID = 0x{row['scene_id']:02X},",
        f"    OOT3D_TITLE_INTRO_OPENING_SETUP_INDEX = {row['setup_index']},",
        f"    OOT3D_TITLE_INTRO_OPENING_CUTSCENE_SOURCE_INDEX = {row['cutscene_source_index']},",
        f"    OOT3D_TITLE_INTRO_OPENING_CAMERA_BLOB_SEGMENT_SOURCE_INDEX = {row['camera_blob_segment_source_index']},",
        "};",
        "",
        "typedef struct {",
        "    u16 orchestrationIndex;",
        "    u8 sceneId;",
        "    u16 setupIndex;",
        "    u16 cutsceneSourceIndex;",
        "    u32 nativeHeaderOffset;",
        "    s32 nativeEndFrame;",
        "    u16 nativeCommandRefStart;",
        "    u16 nativeCommandRefCount;",
        "    u16 nativeCommandLocalRefStart;",
        "    u16 nativeCommandLocalRefCount;",
        "    u16 playerActionRefStart;",
        "    u16 playerActionRefCount;",
        "    u16 playerActionMaxEndFrame;",
        "    u16 cameraBlobSourceIndex;",
        "    u16 cameraBlobSegmentSourceIndex;",
        "    u16 cameraCmadRecordRefStart;",
        "    u16 cameraCmadRecordRefCount;",
        "    u16 cameraCurveRefStart;",
        "    u16 cameraCurveRefCount;",
        "    float slot6EyeDistanceToEmulator;",
        "    u16 linkActorScaleIndex;",
        "    u16 eponaActorScaleIndex;",
        "    u16 linkActorAnimationIndex;",
        "    u16 eponaActorAnimationIndex;",
        "    u16 horseMotionAnimationIndex;",
        "    u16 titleLogoActorInitIndex;",
        "    u16 titleLogoComponentRefStart;",
        "    u16 titleLogoComponentRefCount;",
        "    u16 titleLogoDrawRefStart;",
        "    u16 titleLogoDrawRefCount;",
        "    u16 titleLogoUpdateRefStart;",
        "    u16 titleLogoUpdateRefCount;",
        "    const char* role;",
        "    const char* scenePath;",
        "    const char* slot6TracePath;",
        "    const char* basis;",
        "    const char* unresolved;",
        "} Oot3dTitleIntroOpeningOrchestrationRow;",
        "",
        "typedef struct {",
        "    u16 assetRefIndex;",
        "    u16 orchestrationIndex;",
        "    u16 assetIndex;",
        "    u8 required;",
        "    u8 present;",
        "    const char* assetRole;",
        "    const char* romfsPath;",
        "    const char* basis;",
        "} Oot3dTitleIntroOpeningRequiredAssetRef;",
        "",
        "typedef struct {",
        "    u16 nativeCommandRefIndex;",
        "    u16 orchestrationIndex;",
        "    u16 nativeCommandSourceIndex;",
        "    u16 localCommandIndex;",
        "    u32 commandId;",
        "    u16 entryCount;",
        "    u32 blobSize;",
        "    u32 commandOffset;",
        "    const char* commandName;",
        "    const char* category;",
        "    const char* semanticKind;",
        "} Oot3dTitleIntroOpeningNativeCommandRef;",
        "",
        "typedef struct {",
        "    u16 playerActionRefIndex;",
        "    u16 orchestrationIndex;",
        "    u16 playerActionSourceIndex;",
        "    u16 nativeCommandSourceIndex;",
        "    u16 localCommandIndex;",
        "    u16 entryIndex;",
        "    u16 actionId;",
        "    u16 startFrame;",
        "    u16 endFrame;",
        "    u16 durationFrames;",
        "    const char* runtimeSemantic;",
        "    const char* rawWordsText;",
        "} Oot3dTitleIntroOpeningPlayerActionRef;",
        "",
        "typedef struct {",
        "    u16 cameraCurveRefIndex;",
        "    u16 orchestrationIndex;",
        "    u16 cameraCurveSourceIndex;",
        "    u16 cmadRecordSourceIndex;",
        "    u16 cameraBlobSegmentSourceIndex;",
        "    u16 cameraBlobSourceIndex;",
        "    u16 nativeCommandSourceIndex;",
        "    u8 channelType;",
        "    u8 curveSlotIndex;",
        "    u32 outputFieldOffset;",
        "    u8 interpolationType;",
        "    u16 pointCount;",
        "    const char* curveRole;",
        "} Oot3dTitleIntroOpeningCameraCurveRef;",
        "",
        "extern const Oot3dTitleIntroOpeningOrchestrationRow gOot3dTitleIntroOpeningOrchestrationRows[];",
        "extern const Oot3dTitleIntroOpeningRequiredAssetRef gOot3dTitleIntroOpeningRequiredAssetRefs[];",
        "extern const Oot3dTitleIntroOpeningNativeCommandRef gOot3dTitleIntroOpeningNativeCommandRefs[];",
        "extern const Oot3dTitleIntroOpeningPlayerActionRef gOot3dTitleIntroOpeningPlayerActionRefs[];",
        "extern const Oot3dTitleIntroOpeningCameraCurveRef gOot3dTitleIntroOpeningCameraCurveRefs[];",
        "extern const u32 gOot3dTitleIntroOpeningOrchestrationRowCount;",
        "extern const u32 gOot3dTitleIntroOpeningRequiredAssetRefCount;",
        "extern const u32 gOot3dTitleIntroOpeningNativeCommandRefCount;",
        "extern const u32 gOot3dTitleIntroOpeningPlayerActionRefCount;",
        "extern const u32 gOot3dTitleIntroOpeningCameraCurveRefCount;",
        "",
        "const Oot3dTitleIntroOpeningOrchestrationRow* Oot3d_TitleIntroOpeningGetOrchestrationRow(u16 orchestrationIndex);",
        "const Oot3dTitleIntroOpeningOrchestrationRow* Oot3d_TitleIntroOpeningFindOrchestrationRowForSceneCutscene(",
        "    u8 sceneId,",
        "    u16 setupIndex,",
        "    u16 cutsceneSourceIndex",
        ");",
        "u16 Oot3d_TitleIntroOpeningFindOrchestrationIndexForSceneCutscene(",
        "    u8 sceneId,",
        "    u16 setupIndex,",
        "    u16 cutsceneSourceIndex",
        ");",
        "",
        "#endif",
        "",
    ]
    write_text(path, "\n".join(lines))


def write_source(path: Path, data: dict[str, Any]) -> None:
    row = data["orchestration_rows"][0]
    lines = [
        "#include \"oot3d/title_intro_opening_orchestration.h\"",
        "",
        "const Oot3dTitleIntroOpeningOrchestrationRow gOot3dTitleIntroOpeningOrchestrationRows[] = {",
        "    { "
        + ", ".join(
            [
                c_u16(row["orchestration_index"]),
                c_u8(row["scene_id"]),
                c_u16(row["setup_index"]),
                c_u16(row["cutscene_source_index"]),
                c_u32(row["native_header_offset"]),
                c_s32(row["native_end_frame"]),
                c_u16(row["native_command_ref_start"]),
                c_u16(row["native_command_ref_count"]),
                c_u16(row["native_command_local_ref_start"]),
                c_u16(row["native_command_local_ref_count"]),
                c_u16(row["player_action_ref_start"]),
                c_u16(row["player_action_ref_count"]),
                c_u16(row["player_action_max_end_frame"]),
                c_u16(row["camera_blob_source_index"]),
                c_u16(row["camera_blob_segment_source_index"]),
                c_u16(row["camera_cmad_record_ref_start"]),
                c_u16(row["camera_cmad_record_ref_count"]),
                c_u16(row["camera_curve_ref_start"]),
                c_u16(row["camera_curve_ref_count"]),
                c_float(row["slot6_eye_distance_to_emulator"]),
                c_u16(row["link_actor_scale_index"]),
                c_u16(row["epona_actor_scale_index"]),
                c_u16(row["link_actor_animation_index"]),
                c_u16(row["epona_actor_animation_index"]),
                c_u16(row["horse_motion_animation_index"]),
                c_u16(row["title_logo_actor_init_index"]),
                c_u16(row["title_logo_component_ref_start"]),
                c_u16(row["title_logo_component_ref_count"]),
                c_u16(row["title_logo_draw_ref_start"]),
                c_u16(row["title_logo_draw_ref_count"]),
                c_u16(row["title_logo_update_ref_start"]),
                c_u16(row["title_logo_update_ref_count"]),
                c_string(row["role"]),
                c_string(row["scene_path"]),
                c_string(row["slot6_trace_path"]),
                c_string(row["basis"]),
                c_string(row["unresolved"]),
            ]
        )
        + " },",
        "};",
        "const u32 gOot3dTitleIntroOpeningOrchestrationRowCount = sizeof(gOot3dTitleIntroOpeningOrchestrationRows) / sizeof(gOot3dTitleIntroOpeningOrchestrationRows[0]);",
        "",
        "const Oot3dTitleIntroOpeningRequiredAssetRef gOot3dTitleIntroOpeningRequiredAssetRefs[] = {",
    ]
    for asset in data["required_asset_refs"]:
        lines.append(
            "    { "
            + ", ".join(
                [
                    c_u16(asset["asset_ref_index"]),
                    c_u16(asset["orchestration_index"]),
                    c_u16(asset["asset_index"]),
                    c_u8(asset["required"]),
                    c_u8(asset["present"]),
                    c_string(asset["asset_role"]),
                    c_string(asset["romfs_path"]),
                    c_string(asset["basis"]),
                ]
            )
            + " },"
        )
    lines.extend(
        [
            "};",
            "const u32 gOot3dTitleIntroOpeningRequiredAssetRefCount = sizeof(gOot3dTitleIntroOpeningRequiredAssetRefs) / sizeof(gOot3dTitleIntroOpeningRequiredAssetRefs[0]);",
            "",
            "const Oot3dTitleIntroOpeningNativeCommandRef gOot3dTitleIntroOpeningNativeCommandRefs[] = {",
        ]
    )
    for command in data["native_command_refs"]:
        lines.append(
            "    { "
            + ", ".join(
                [
                    c_u16(command["native_command_ref_index"]),
                    c_u16(command["orchestration_index"]),
                    c_u16(command["native_command_source_index"]),
                    c_u16(command["local_command_index"]),
                    c_u32(command["command_id"]),
                    c_u16(command["entry_count"]),
                    c_u32(command["blob_size"]),
                    c_u32(command["command_offset"]),
                    c_string(command["command_name"]),
                    c_string(command["category"]),
                    c_string(command["semantic_kind"]),
                ]
            )
            + " },"
        )
    lines.extend(
        [
            "};",
            "const u32 gOot3dTitleIntroOpeningNativeCommandRefCount = sizeof(gOot3dTitleIntroOpeningNativeCommandRefs) / sizeof(gOot3dTitleIntroOpeningNativeCommandRefs[0]);",
            "",
            "const Oot3dTitleIntroOpeningPlayerActionRef gOot3dTitleIntroOpeningPlayerActionRefs[] = {",
        ]
    )
    for ref in data["player_action_refs"]:
        lines.append(
            "    { "
            + ", ".join(
                [
                    c_u16(ref["player_action_ref_index"]),
                    c_u16(ref["orchestration_index"]),
                    c_u16(ref["player_action_source_index"]),
                    c_u16(ref["native_command_source_index"]),
                    c_u16(ref["local_command_index"]),
                    c_u16(ref["entry_index"]),
                    c_u16(ref["action_id"]),
                    c_u16(ref["start_frame"]),
                    c_u16(ref["end_frame"]),
                    c_u16(ref["duration_frames"]),
                    c_string(ref["runtime_semantic"]),
                    c_string(ref["raw_words_text"]),
                ]
            )
            + " },"
        )
    lines.extend(
        [
            "};",
            "const u32 gOot3dTitleIntroOpeningPlayerActionRefCount = sizeof(gOot3dTitleIntroOpeningPlayerActionRefs) / sizeof(gOot3dTitleIntroOpeningPlayerActionRefs[0]);",
            "",
            "const Oot3dTitleIntroOpeningCameraCurveRef gOot3dTitleIntroOpeningCameraCurveRefs[] = {",
        ]
    )
    for ref in data["camera_curve_refs"]:
        lines.append(
            "    { "
            + ", ".join(
                [
                    c_u16(ref["camera_curve_ref_index"]),
                    c_u16(ref["orchestration_index"]),
                    c_u16(ref["camera_curve_source_index"]),
                    c_u16(ref["cmad_record_source_index"]),
                    c_u16(ref["camera_blob_segment_source_index"]),
                    c_u16(ref["camera_blob_source_index"]),
                    c_u16(ref["native_command_source_index"]),
                    c_u8(ref["channel_type"]),
                    c_u8(ref["curve_slot_index"]),
                    c_u32(ref["output_field_offset"]),
                    c_u8(ref["interpolation_type"]),
                    c_u16(ref["point_count"]),
                    c_string(ref["curve_role"]),
                ]
            )
            + " },"
        )
    lines.extend(
        [
            "};",
            "const u32 gOot3dTitleIntroOpeningCameraCurveRefCount = sizeof(gOot3dTitleIntroOpeningCameraCurveRefs) / sizeof(gOot3dTitleIntroOpeningCameraCurveRefs[0]);",
            "",
            "const Oot3dTitleIntroOpeningOrchestrationRow* Oot3d_TitleIntroOpeningGetOrchestrationRow(u16 orchestrationIndex) {",
            "    if (orchestrationIndex >= gOot3dTitleIntroOpeningOrchestrationRowCount) {",
            "        return 0;",
            "    }",
            "    return &gOot3dTitleIntroOpeningOrchestrationRows[orchestrationIndex];",
            "}",
            "",
            "const Oot3dTitleIntroOpeningOrchestrationRow* Oot3d_TitleIntroOpeningFindOrchestrationRowForSceneCutscene(",
            "    u8 sceneId,",
            "    u16 setupIndex,",
            "    u16 cutsceneSourceIndex",
            ") {",
            "    u32 i;",
            "",
            "    for (i = 0; i < gOot3dTitleIntroOpeningOrchestrationRowCount; ++i) {",
            "        const Oot3dTitleIntroOpeningOrchestrationRow* row = &gOot3dTitleIntroOpeningOrchestrationRows[i];",
            "        if (row->sceneId == sceneId && row->setupIndex == setupIndex &&",
            "            row->cutsceneSourceIndex == cutsceneSourceIndex) {",
            "            return row;",
            "        }",
            "    }",
            "    return 0;",
            "}",
            "",
            "u16 Oot3d_TitleIntroOpeningFindOrchestrationIndexForSceneCutscene(",
            "    u8 sceneId,",
            "    u16 setupIndex,",
            "    u16 cutsceneSourceIndex",
            ") {",
            "    const Oot3dTitleIntroOpeningOrchestrationRow* row =",
            "        Oot3d_TitleIntroOpeningFindOrchestrationRowForSceneCutscene(",
            "            sceneId,",
            "            setupIndex,",
            "            cutsceneSourceIndex",
            "        );",
            "",
            "    return row != 0 ? row->orchestrationIndex : OOT3D_TITLE_INTRO_OPENING_NO_ORCHESTRATION_INDEX;",
            "}",
            "",
        ]
    )
    write_text(path, "\n".join(lines))


def main() -> None:
    data = build_report()
    write_json(OUT_JSON, data)
    write_markdown(OUT_MD, data)
    write_csv(OUT_ORCHESTRATION_CSV, data["orchestration_rows"])
    write_csv(OUT_ASSET_REF_CSV, data["required_asset_refs"])
    write_csv(OUT_NATIVE_COMMAND_REF_CSV, data["native_command_refs"])
    write_csv(OUT_PLAYER_ACTION_REF_CSV, data["player_action_refs"])
    write_csv(OUT_CAMERA_CURVE_REF_CSV, data["camera_curve_refs"])
    write_header(OUT_HEADER, data)
    write_source(OUT_SOURCE, data)
    if not data["summary"]["ok"]:
        raise SystemExit("title-intro opening orchestration audit failed")


if __name__ == "__main__":
    main()
