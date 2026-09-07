#!/usr/bin/env python3
"""Build native cutscene transition handoff candidates.

Direct-player cutscene events can request a global entrance through the same
transition helper used by scene exits. This table keeps that route explicit:
event -> global entrance -> native scene/setup entrance candidates -> native
cutscene sources for those setups.
"""

from __future__ import annotations

import csv
import json
from collections import Counter
from pathlib import Path
from typing import Any

import build_scene_global_entrance_table as global_entrance_table


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DIRECT_PLAYER_EVENT_TABLE = ROOT / "analysis" / "scene_cutscene_direct_player_event_table.json"
DEFAULT_GLOBAL_ENTRANCE_TABLE = ROOT / "analysis" / "scene_global_entrance_table.json"
DEFAULT_NATIVE_CUTSCENE_TABLE = ROOT / "analysis" / "scene_cutscene_native_source_table.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "scene_cutscene_transition_handoff_table.json"
DEFAULT_OUT_HANDOFF_CSV = ROOT / "analysis" / "scene_cutscene_transition_handoff_table.csv"
DEFAULT_OUT_CANDIDATE_CSV = ROOT / "analysis" / "scene_cutscene_transition_handoff_candidates.csv"
DEFAULT_OUT_EFFECTIVE_ENTRANCE_CSV = ROOT / "analysis" / "scene_cutscene_transition_effective_entrances.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "scene_cutscene_transition_handoff_table.md"
DEFAULT_OUT_HEADER = ROOT / "include" / "oot3d" / "scene_cutscene_transition_handoff_table.h"
DEFAULT_OUT_SOURCE = ROOT / "src" / "code" / "z_scene_cutscene_transition_handoff_table.c"

NO_CUTSCENE_SOURCE_INDEX = 0xFFFF
GLOBAL_ENTRANCE_INDEX_SOURCE_ADDRESS = 0x00587958
SCENE_LAYER_OFFSET_CONTEXT_ADDRESS = 0x00588958
SCENE_LAYER_OFFSET_CONTEXT_FIELD_OFFSET = 0x4E8
SCENE_LAYER_OFFSET_SOURCE_ADDRESS = SCENE_LAYER_OFFSET_CONTEXT_ADDRESS + SCENE_LAYER_OFFSET_CONTEXT_FIELD_OFFSET
GLOBAL_ENTRANCE_TABLE_ADDRESS = global_entrance_table.GLOBAL_ENTRANCE_TABLE
SCENE_LAYER_OFFSET_CANDIDATE_MAX = 0x13
SCENE_LAYER_OFFSET_EXPRESSION = (
    "globalEntranceTable[transitionRequestIndex + *(s32*)(0x00588958 + 0x4E8)]"
)
SCENE_LAYER_OFFSET_EVIDENCE = [
    "FUN_004490F8 writes play+0x5C02 from globalEntranceTable[*0x00587958 + *(s32*)(0x00588958 + 0x4E8)]",
    "FUN_002E2E60 copies play+0x5C32 into *0x00587958 when the transition completes",
    "FUN_0044F38C resolves the initial room from play+0x5C18 and play+0x5C02 after scene commands install the selected entrance list",
]


def as_list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def int_value(value: Any, default: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def str_value(value: Any) -> str:
    return "" if value is None else str(value)


def c_string(value: Any) -> str:
    escaped = str_value(value).replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def c_u8(value: Any) -> str:
    return f"{int_value(value) & 0xFF}u"


def c_u16(value: Any) -> str:
    return f"{int_value(value) & 0xFFFF}u"


def c_s16(value: Any) -> str:
    value = int_value(value)
    if value < 0:
        return str(value)
    return f"{value}u"


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def build_cutscene_lookup(cutscene_payload: dict[str, Any]) -> dict[tuple[str, int], dict[str, Any]]:
    lookup: dict[tuple[str, int], dict[str, Any]] = {}
    for row in as_list(cutscene_payload.get("cutscene_rows")):
        if not isinstance(row, dict):
            continue
        scene_path = str_value(row.get("scene_path"))
        setup_index = int_value(row.get("setup_index"), -1)
        if scene_path and setup_index >= 0:
            lookup[(scene_path, setup_index)] = row
    return lookup


def build_global_lookup(global_payload: dict[str, Any]) -> dict[int, dict[str, Any]]:
    lookup: dict[int, dict[str, Any]] = {}
    for row in as_list(global_payload.get("rows")):
        if not isinstance(row, dict):
            continue
        entrance_index = int_value(row.get("entrance_index"), -1)
        if entrance_index >= 0:
            lookup[entrance_index] = row
    return lookup


def build_native_scene_records_by_id(
    scene_index_payload: dict[str, Any],
    scene_resource_payload: dict[int, dict[str, Any]],
) -> dict[int, dict[str, Any]]:
    by_path = global_entrance_table.scene_lookup_by_path(scene_index_payload)
    records: dict[int, dict[str, Any]] = {}
    for scene_id, resource in scene_resource_payload.items():
        scene_path = str_value(resource.get("zsi_path"))
        record = by_path.get(scene_path.lower()) if scene_path else None
        records[scene_id] = {
            "scene_path": scene_path,
            "scene_index_symbol": str_value(resource.get("native_scene_index_symbol")),
            "record": record,
        }
    return records


def native_match_cutscene_count(
    matches: list[dict[str, Any]],
    cutscene_lookup: dict[tuple[str, int], dict[str, Any]],
) -> int:
    count = 0
    for match in matches:
        scene_path = str_value(match.get("scene_path"))
        setup_index = int_value(match.get("setup_index"), -1)
        if cutscene_lookup.get((scene_path, setup_index)) is not None:
            count += 1
    return count


def decode_effective_entrance_rows(
    handoff_source_index: int,
    event: dict[str, Any],
    transition_request_index: int,
    code: bytes,
    table_entry_count: int,
    native_scene_records_by_id: dict[int, dict[str, Any]],
    cutscene_lookup: dict[tuple[str, int], dict[str, Any]],
    first_source_index: int,
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for scene_layer_offset in range(SCENE_LAYER_OFFSET_CANDIDATE_MAX + 1):
        effective_index = transition_request_index + scene_layer_offset
        decoded = global_entrance_table.decode_global_entry(code, effective_index, table_entry_count)
        scene_id = int_value(decoded.get("scene_id"), -1)
        scene_record = native_scene_records_by_id.get(scene_id, {})
        native_scene_path = str_value(scene_record.get("scene_path"))
        native_scene_index_symbol = str_value(scene_record.get("scene_index_symbol"))
        record = scene_record.get("record")
        native_matches = (
            global_entrance_table.native_entrance_matches(decoded, [record])
            if isinstance(record, dict)
            else []
        )
        cutscene_candidate_count = native_match_cutscene_count(native_matches, cutscene_lookup)
        status = str_value(decoded.get("status"))
        rows.append(
            {
                "effective_entrance_source_index": first_source_index + len(rows),
                "handoff_source_index": handoff_source_index,
                "direct_player_event_source_index": int_value(event.get("direct_player_event_source_index")),
                "transition_request_index": transition_request_index,
                "scene_layer_offset": scene_layer_offset,
                "effective_entrance_index": effective_index,
                "decoded_from_code_bin": 1 if status == "decoded_from_code_bin" else 0,
                "scene_id": scene_id if scene_id >= 0 else 0,
                "local_entrance_index": int_value(decoded.get("local_entrance_index")),
                "field": int_value(decoded.get("field")),
                "raw_hex": str_value(decoded.get("raw_hex")),
                "native_scene_path_candidate": native_scene_path,
                "native_scene_index_symbol": native_scene_index_symbol,
                "native_match_count": len(native_matches),
                "cutscene_candidate_count": cutscene_candidate_count,
                "decode_status": status,
            }
        )
    return rows


def vec3(values: Any) -> tuple[int, int, int]:
    vector = [int_value(value) for value in as_list(values)]
    while len(vector) < 3:
        vector.append(0)
    return vector[0], vector[1], vector[2]


def global_native_scene_path(global_row: dict[str, Any] | None) -> str:
    if global_row is None:
        return ""
    resource_path = str_value(global_row.get("native_scene_resource_zsi_path"))
    if resource_path:
        return resource_path
    candidates = as_list(global_row.get("native_scene_path_candidates"))
    if candidates:
        return str_value(candidates[0])
    return str_value(global_row.get("native_scene_path_candidate"))


def handoff_status(
    global_row: dict[str, Any] | None,
    native_match_count: int,
    cutscene_candidate_count: int,
    requires_scene_layer_offset: bool,
) -> str:
    if global_row is None:
        return "unresolved_global_entrance"
    if native_match_count == 0:
        return "no_native_scene_matches"
    if cutscene_candidate_count == 0:
        return "no_native_cutscene_candidates"
    if cutscene_candidate_count == 1:
        return "unique_native_cutscene_candidate"
    if requires_scene_layer_offset:
        return "requires_scene_layer_offset"
    return "ambiguous_native_cutscene_candidates"


def build_rows() -> dict[str, Any]:
    direct_payload = load_json(DEFAULT_DIRECT_PLAYER_EVENT_TABLE)
    global_payload = load_json(DEFAULT_GLOBAL_ENTRANCE_TABLE)
    cutscene_payload = load_json(DEFAULT_NATIVE_CUTSCENE_TABLE)
    scene_index_payload = load_json(global_entrance_table.DEFAULT_SCENE_INDEX)
    scene_resource_payload = global_entrance_table.load_native_scene_resources(
        global_entrance_table.DEFAULT_SCENE_RESOURCE_TABLE
    )
    code = global_entrance_table.DEFAULT_CODE_BIN.read_bytes()
    table_entry_count, table_extent_evidence = global_entrance_table.infer_global_entrance_table_extent(code)
    global_lookup = build_global_lookup(global_payload)
    cutscene_lookup = build_cutscene_lookup(cutscene_payload)
    native_scene_records_by_id = build_native_scene_records_by_id(scene_index_payload, scene_resource_payload)

    handoff_rows: list[dict[str, Any]] = []
    candidate_rows: list[dict[str, Any]] = []
    effective_entrance_rows: list[dict[str, Any]] = []

    for event in as_list(direct_payload.get("direct_player_event_rows")):
        if not isinstance(event, dict):
            continue
        if int_value(event.get("dispatch_resolved")) == 0:
            continue
        entrance_index = int_value(event.get("transition_request_index"), -1)
        if entrance_index < 0:
            continue

        global_row = global_lookup.get(entrance_index)
        first_candidate_index = len(candidate_rows)
        first_effective_entrance_index = len(effective_entrance_rows)
        effective_rows_for_handoff = decode_effective_entrance_rows(
            len(handoff_rows),
            event,
            entrance_index,
            code,
            table_entry_count,
            native_scene_records_by_id,
            cutscene_lookup,
            first_effective_entrance_index,
        )
        effective_entrance_rows.extend(effective_rows_for_handoff)
        native_matches = as_list(global_row.get("native_matches")) if global_row is not None else []
        cutscene_candidate_count = 0
        for native_match in native_matches:
            if not isinstance(native_match, dict):
                continue
            scene_path = str_value(native_match.get("scene_path"))
            setup_index = int_value(native_match.get("setup_index"), -1)
            cutscene = cutscene_lookup.get((scene_path, setup_index))
            cutscene_source_index = (
                int_value(cutscene.get("cutscene_source_index"), NO_CUTSCENE_SOURCE_INDEX)
                if cutscene is not None
                else NO_CUTSCENE_SOURCE_INDEX
            )
            if cutscene_source_index != NO_CUTSCENE_SOURCE_INDEX:
                cutscene_candidate_count += 1
            spawn_pos = vec3(native_match.get("spawn_pos"))
            spawn_rot = vec3(native_match.get("spawn_rot"))
            candidate_rows.append(
                {
                    "candidate_source_index": len(candidate_rows),
                    "handoff_source_index": len(handoff_rows),
                    "direct_player_event_source_index": int_value(event.get("direct_player_event_source_index")),
                    "transition_request_index": entrance_index,
                    "scene_id": int_value(global_row.get("scene_id")) if global_row is not None else 0,
                    "local_entrance_index": int_value(global_row.get("local_entrance_index")) if global_row is not None else 0,
                    "field": int_value(global_row.get("field")) if global_row is not None else 0,
                    "scene_path": scene_path,
                    "setup_index": setup_index,
                    "cutscene_source_index": cutscene_source_index,
                    "native_end_frame": int_value(cutscene.get("native_end_frame")) if cutscene is not None else 0,
                    "native_command_count": int_value(cutscene.get("native_command_count")) if cutscene is not None else 0,
                    "spawn_resolved": 1 if native_match.get("spawn_resolved") else 0,
                    "entrance_spawn": int_value(native_match.get("entrance_spawn"), 0),
                    "entrance_room": int_value(native_match.get("entrance_room"), 0),
                    "spawn_actor_name": str_value(native_match.get("spawn_actor_name")),
                    "spawn_pos_x": spawn_pos[0],
                    "spawn_pos_y": spawn_pos[1],
                    "spawn_pos_z": spawn_pos[2],
                    "spawn_rot_x": spawn_rot[0],
                    "spawn_rot_y": spawn_rot[1],
                    "spawn_rot_z": spawn_rot[2],
                    "spawn_params": int_value(native_match.get("spawn_params"), 0),
                }
            )

        native_match_count = len([match for match in native_matches if isinstance(match, dict)])
        requires_scene_layer_offset = len(effective_rows_for_handoff) > 0 and cutscene_candidate_count > 1
        status = handoff_status(
            global_row,
            native_match_count,
            cutscene_candidate_count,
            requires_scene_layer_offset,
        )
        handoff_rows.append(
            {
                "handoff_source_index": len(handoff_rows),
                "direct_player_event_source_index": int_value(event.get("direct_player_event_source_index")),
                "source_cutscene_source_index": int_value(event.get("cutscene_source_index")),
                "source_scene_path": str_value(event.get("scene_path")),
                "source_setup_index": int_value(event.get("setup_index")),
                "action_id": int_value(event.get("action_id")),
                "start_frame": int_value(event.get("start_frame")),
                "transition_request_index": entrance_index,
                "transition_request_trigger": int_value(event.get("transition_request_trigger")),
                "transition_request_effect": int_value(event.get("transition_request_effect")),
                "global_entrance_resolved": 1 if global_row is not None else 0,
                "scene_id": int_value(global_row.get("scene_id")) if global_row is not None else 0,
                "local_entrance_index": int_value(global_row.get("local_entrance_index")) if global_row is not None else 0,
                "field": int_value(global_row.get("field")) if global_row is not None else 0,
                "native_scene_path_candidate": global_native_scene_path(global_row),
                "native_scene_index_symbol": str_value(global_row.get("native_scene_index_symbol")) if global_row is not None else "",
                "native_match_count": native_match_count,
                "candidate_first_index": first_candidate_index,
                "candidate_count": len(candidate_rows) - first_candidate_index,
                "cutscene_candidate_count": cutscene_candidate_count,
                "requires_scene_layer_offset": 1 if requires_scene_layer_offset else 0,
                "effective_entrance_first_index": first_effective_entrance_index,
                "effective_entrance_count": len(effective_rows_for_handoff),
                "status": status,
            }
        )

    summary = {
        "format": "oot3d_scene_cutscene_transition_handoff_table_v2",
        "handoff_row_count": len(handoff_rows),
        "candidate_row_count": len(candidate_rows),
        "effective_entrance_row_count": len(effective_entrance_rows),
        "status_counts": dict(sorted(Counter(row["status"] for row in handoff_rows).items())),
        "transition_request_indices": [
            f"0x{index:04x}"
            for index in sorted({int_value(row.get("transition_request_index")) for row in handoff_rows})
        ],
        "scene_layer_offset_source": {
            "global_entrance_index_source_address": f"0x{GLOBAL_ENTRANCE_INDEX_SOURCE_ADDRESS:08x}",
            "global_entrance_table_address": f"0x{GLOBAL_ENTRANCE_TABLE_ADDRESS:08x}",
            "scene_layer_offset_context_address": f"0x{SCENE_LAYER_OFFSET_CONTEXT_ADDRESS:08x}",
            "scene_layer_offset_context_field_offset": f"0x{SCENE_LAYER_OFFSET_CONTEXT_FIELD_OFFSET:04x}",
            "scene_layer_offset_source_address": f"0x{SCENE_LAYER_OFFSET_SOURCE_ADDRESS:08x}",
            "effective_entrance_expression": SCENE_LAYER_OFFSET_EXPRESSION,
            "candidate_offset_range": f"0..0x{SCENE_LAYER_OFFSET_CANDIDATE_MAX:x}",
            "table_entry_count": table_entry_count,
            "table_extent_evidence": table_extent_evidence,
            "code_bin": str(global_entrance_table.DEFAULT_CODE_BIN),
        },
        "basis": [
            "direct player events decoded from native OOT3D cutscene command 0x3E8",
            "global entrance rows decoded from OOT3D code.bin",
            "scene-layer effective entrance calculation decoded from OOT3D scene-load/transition code",
            "scene/setup/entrance/cutscene candidates decoded from native OOT3D ZSI scene commands",
        ],
        "scene_layer_offset_evidence": SCENE_LAYER_OFFSET_EVIDENCE,
    }
    return {
        "summary": summary,
        "handoff_rows": handoff_rows,
        "candidate_rows": candidate_rows,
        "effective_entrance_rows": effective_entrance_rows,
    }


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = sorted({key for row in rows for key in row})
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def status_enum_name(status: str) -> str:
    names = {
        "unresolved_global_entrance": "OOT3D_CUTSCENE_TRANSITION_HANDOFF_UNRESOLVED_GLOBAL_ENTRANCE",
        "no_native_scene_matches": "OOT3D_CUTSCENE_TRANSITION_HANDOFF_NO_NATIVE_SCENE_MATCHES",
        "no_native_cutscene_candidates": "OOT3D_CUTSCENE_TRANSITION_HANDOFF_NO_NATIVE_CUTSCENE_CANDIDATES",
        "unique_native_cutscene_candidate": "OOT3D_CUTSCENE_TRANSITION_HANDOFF_UNIQUE_NATIVE_CUTSCENE_CANDIDATE",
        "requires_scene_layer_offset": "OOT3D_CUTSCENE_TRANSITION_HANDOFF_REQUIRES_SCENE_LAYER_OFFSET",
        "ambiguous_native_cutscene_candidates": "OOT3D_CUTSCENE_TRANSITION_HANDOFF_AMBIGUOUS_NATIVE_CUTSCENE_CANDIDATES",
    }
    return names[status]


def write_header(path: Path, payload: dict[str, Any]) -> None:
    summary = payload["summary"]
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "#ifndef OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_TABLE_H",
        "#define OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_TABLE_H",
        "",
        '#include "oot3d/types.h"',
        "",
        "enum {",
        f"    OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_ROW_COUNT = {summary['handoff_row_count']},",
        f"    OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_CANDIDATE_ROW_COUNT = {summary['candidate_row_count']},",
        f"    OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_EFFECTIVE_ENTRANCE_ROW_COUNT = {summary['effective_entrance_row_count']},",
        f"    OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_NO_CUTSCENE_SOURCE_INDEX = {NO_CUTSCENE_SOURCE_INDEX},",
        f"    OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_GLOBAL_ENTRANCE_INDEX_SOURCE_ADDRESS = 0x{GLOBAL_ENTRANCE_INDEX_SOURCE_ADDRESS:08X},",
        f"    OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_GLOBAL_ENTRANCE_TABLE_ADDRESS = 0x{GLOBAL_ENTRANCE_TABLE_ADDRESS:08X},",
        f"    OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_SCENE_LAYER_OFFSET_CONTEXT_ADDRESS = 0x{SCENE_LAYER_OFFSET_CONTEXT_ADDRESS:08X},",
        f"    OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_SCENE_LAYER_OFFSET_CONTEXT_FIELD_OFFSET = 0x{SCENE_LAYER_OFFSET_CONTEXT_FIELD_OFFSET:04X},",
        f"    OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_SCENE_LAYER_OFFSET_SOURCE_ADDRESS = 0x{SCENE_LAYER_OFFSET_SOURCE_ADDRESS:08X},",
        "};",
        "",
        "typedef enum {",
        "    OOT3D_CUTSCENE_TRANSITION_HANDOFF_UNRESOLVED_GLOBAL_ENTRANCE,",
        "    OOT3D_CUTSCENE_TRANSITION_HANDOFF_NO_NATIVE_SCENE_MATCHES,",
        "    OOT3D_CUTSCENE_TRANSITION_HANDOFF_NO_NATIVE_CUTSCENE_CANDIDATES,",
        "    OOT3D_CUTSCENE_TRANSITION_HANDOFF_UNIQUE_NATIVE_CUTSCENE_CANDIDATE,",
        "    OOT3D_CUTSCENE_TRANSITION_HANDOFF_REQUIRES_SCENE_LAYER_OFFSET,",
        "    OOT3D_CUTSCENE_TRANSITION_HANDOFF_AMBIGUOUS_NATIVE_CUTSCENE_CANDIDATES,",
        "} Oot3dCutsceneTransitionHandoffStatus;",
        "",
        "typedef struct {",
        "    u16 effectiveEntranceSourceIndex;",
        "    u16 handoffSourceIndex;",
        "    u16 directPlayerEventSourceIndex;",
        "    u16 transitionRequestIndex;",
        "    u8 sceneLayerOffset;",
        "    u16 effectiveEntranceIndex;",
        "    u8 decodedFromCodeBin;",
        "    u8 sceneId;",
        "    u8 localEntranceIndex;",
        "    u16 field;",
        "    const char* rawHex;",
        "    const char* nativeScenePathCandidate;",
        "    const char* nativeSceneIndexSymbol;",
        "    u16 nativeMatchCount;",
        "    u16 cutsceneCandidateCount;",
        "    const char* decodeStatus;",
        "} Oot3dSceneCutsceneTransitionEffectiveEntranceRow;",
        "",
        "typedef struct {",
        "    u16 candidateSourceIndex;",
        "    u16 handoffSourceIndex;",
        "    u16 directPlayerEventSourceIndex;",
        "    u16 transitionRequestIndex;",
        "    u8 sceneId;",
        "    u8 localEntranceIndex;",
        "    u16 field;",
        "    const char* scenePath;",
        "    u16 setupIndex;",
        "    u16 cutsceneSourceIndex;",
        "    s32 nativeEndFrame;",
        "    u16 nativeCommandCount;",
        "    u8 spawnResolved;",
        "    u8 entranceSpawn;",
        "    s8 entranceRoom;",
        "    const char* spawnActorName;",
        "    s16 spawnPos[3];",
        "    s16 spawnRot[3];",
        "    u16 spawnParams;",
        "} Oot3dSceneCutsceneTransitionHandoffCandidateRow;",
        "",
        "typedef struct {",
        "    u16 handoffSourceIndex;",
        "    u16 directPlayerEventSourceIndex;",
        "    u16 sourceCutsceneSourceIndex;",
        "    const char* sourceScenePath;",
        "    u16 sourceSetupIndex;",
        "    u16 actionId;",
        "    u16 startFrame;",
        "    u16 transitionRequestIndex;",
        "    u8 transitionRequestTrigger;",
        "    u8 transitionRequestEffect;",
        "    u8 globalEntranceResolved;",
        "    u8 sceneId;",
        "    u8 localEntranceIndex;",
        "    u16 field;",
        "    const char* nativeScenePathCandidate;",
        "    const char* nativeSceneIndexSymbol;",
        "    u16 nativeMatchCount;",
        "    u16 candidateFirstIndex;",
        "    u16 candidateCount;",
        "    u16 cutsceneCandidateCount;",
        "    u8 requiresSceneLayerOffset;",
        "    u16 effectiveEntranceFirstIndex;",
        "    u16 effectiveEntranceCount;",
        "    Oot3dCutsceneTransitionHandoffStatus status;",
        "} Oot3dSceneCutsceneTransitionHandoffRow;",
        "",
        "extern const Oot3dSceneCutsceneTransitionHandoffRow oot3d_scene_cutscene_transition_handoff_rows[];",
        "extern const Oot3dSceneCutsceneTransitionHandoffCandidateRow oot3d_scene_cutscene_transition_handoff_candidate_rows[];",
        "extern const Oot3dSceneCutsceneTransitionEffectiveEntranceRow oot3d_scene_cutscene_transition_effective_entrance_rows[];",
        "extern const u32 oot3d_scene_cutscene_transition_handoff_row_count;",
        "extern const u32 oot3d_scene_cutscene_transition_handoff_candidate_row_count;",
        "extern const u32 oot3d_scene_cutscene_transition_effective_entrance_row_count;",
        "",
        "const char* Oot3d_CutsceneTransitionHandoffEffectiveEntranceExpression(void);",
        "",
        "const Oot3dSceneCutsceneTransitionHandoffRow* Oot3d_CutsceneTransitionHandoffFindByDirectPlayerEvent(",
        "    u16 directPlayerEventSourceIndex",
        ");",
        "",
        "#endif",
        "",
    ]
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_source(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "/* Generated by build_scene_cutscene_transition_handoff_table.py. */",
        "",
        '#include "oot3d/scene_cutscene_transition_handoff_table.h"',
        "",
        "#include <stddef.h>",
        "",
        "const Oot3dSceneCutsceneTransitionHandoffRow oot3d_scene_cutscene_transition_handoff_rows[] = {",
    ]
    for row in payload["handoff_rows"]:
        lines.append(
            "    { "
            f"{c_u16(row['handoff_source_index'])}, "
            f"{c_u16(row['direct_player_event_source_index'])}, "
            f"{c_u16(row['source_cutscene_source_index'])}, "
            f"{c_string(row['source_scene_path'])}, "
            f"{c_u16(row['source_setup_index'])}, "
            f"{c_u16(row['action_id'])}, "
            f"{c_u16(row['start_frame'])}, "
            f"{c_u16(row['transition_request_index'])}, "
            f"{c_u8(row['transition_request_trigger'])}, "
            f"{c_u8(row['transition_request_effect'])}, "
            f"{c_u8(row['global_entrance_resolved'])}, "
            f"{c_u8(row['scene_id'])}, "
            f"{c_u8(row['local_entrance_index'])}, "
            f"{c_u16(row['field'])}, "
            f"{c_string(row['native_scene_path_candidate'])}, "
            f"{c_string(row['native_scene_index_symbol'])}, "
            f"{c_u16(row['native_match_count'])}, "
            f"{c_u16(row['candidate_first_index'])}, "
            f"{c_u16(row['candidate_count'])}, "
            f"{c_u16(row['cutscene_candidate_count'])}, "
            f"{c_u8(row['requires_scene_layer_offset'])}, "
            f"{c_u16(row['effective_entrance_first_index'])}, "
            f"{c_u16(row['effective_entrance_count'])}, "
            f"{status_enum_name(row['status'])} "
            "},"
        )
    lines.extend(
        [
            "};",
            "",
            "const Oot3dSceneCutsceneTransitionEffectiveEntranceRow oot3d_scene_cutscene_transition_effective_entrance_rows[] = {",
        ]
    )
    for row in payload["effective_entrance_rows"]:
        lines.append(
            "    { "
            f"{c_u16(row['effective_entrance_source_index'])}, "
            f"{c_u16(row['handoff_source_index'])}, "
            f"{c_u16(row['direct_player_event_source_index'])}, "
            f"{c_u16(row['transition_request_index'])}, "
            f"{c_u8(row['scene_layer_offset'])}, "
            f"{c_u16(row['effective_entrance_index'])}, "
            f"{c_u8(row['decoded_from_code_bin'])}, "
            f"{c_u8(row['scene_id'])}, "
            f"{c_u8(row['local_entrance_index'])}, "
            f"{c_u16(row['field'])}, "
            f"{c_string(row['raw_hex'])}, "
            f"{c_string(row['native_scene_path_candidate'])}, "
            f"{c_string(row['native_scene_index_symbol'])}, "
            f"{c_u16(row['native_match_count'])}, "
            f"{c_u16(row['cutscene_candidate_count'])}, "
            f"{c_string(row['decode_status'])} "
            "},"
        )
    lines.extend(
        [
            "};",
            "",
            "const Oot3dSceneCutsceneTransitionHandoffCandidateRow oot3d_scene_cutscene_transition_handoff_candidate_rows[] = {",
        ]
    )
    for row in payload["candidate_rows"]:
        lines.append(
            "    { "
            f"{c_u16(row['candidate_source_index'])}, "
            f"{c_u16(row['handoff_source_index'])}, "
            f"{c_u16(row['direct_player_event_source_index'])}, "
            f"{c_u16(row['transition_request_index'])}, "
            f"{c_u8(row['scene_id'])}, "
            f"{c_u8(row['local_entrance_index'])}, "
            f"{c_u16(row['field'])}, "
            f"{c_string(row['scene_path'])}, "
            f"{c_u16(row['setup_index'])}, "
            f"{c_u16(row['cutscene_source_index'])}, "
            f"{int_value(row['native_end_frame'])}, "
            f"{c_u16(row['native_command_count'])}, "
            f"{c_u8(row['spawn_resolved'])}, "
            f"{c_u8(row['entrance_spawn'])}, "
            f"{c_s16(row['entrance_room'])}, "
            f"{c_string(row['spawn_actor_name'])}, "
            f"{{ {c_s16(row['spawn_pos_x'])}, {c_s16(row['spawn_pos_y'])}, {c_s16(row['spawn_pos_z'])} }}, "
            f"{{ {c_s16(row['spawn_rot_x'])}, {c_s16(row['spawn_rot_y'])}, {c_s16(row['spawn_rot_z'])} }}, "
            f"{c_u16(row['spawn_params'])} "
            "},"
        )
    lines.extend(
        [
            "};",
            "",
            "const u32 oot3d_scene_cutscene_transition_handoff_row_count = OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_ROW_COUNT;",
            "const u32 oot3d_scene_cutscene_transition_handoff_candidate_row_count = OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_CANDIDATE_ROW_COUNT;",
            "const u32 oot3d_scene_cutscene_transition_effective_entrance_row_count = OOT3D_SCENE_CUTSCENE_TRANSITION_HANDOFF_EFFECTIVE_ENTRANCE_ROW_COUNT;",
            "",
            "const char* Oot3d_CutsceneTransitionHandoffEffectiveEntranceExpression(void) {",
            f"    return {c_string(SCENE_LAYER_OFFSET_EXPRESSION)};",
            "}",
            "",
            "const Oot3dSceneCutsceneTransitionHandoffRow* Oot3d_CutsceneTransitionHandoffFindByDirectPlayerEvent(",
            "    u16 directPlayerEventSourceIndex",
            ") {",
            "    u32 rowIndex;",
            "",
            "    for (rowIndex = 0; rowIndex < oot3d_scene_cutscene_transition_handoff_row_count; rowIndex++) {",
            "        const Oot3dSceneCutsceneTransitionHandoffRow* row =",
            "            &oot3d_scene_cutscene_transition_handoff_rows[rowIndex];",
            "",
            "        if (row->directPlayerEventSourceIndex == directPlayerEventSourceIndex) {",
            "            return row;",
            "        }",
            "    }",
            "",
            "    return NULL;",
            "}",
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_markdown(path: Path, payload: dict[str, Any]) -> None:
    summary = payload["summary"]
    lines = [
        "# OOT3D Cutscene Transition Handoff Table",
        "",
        "Generated from native OOT3D direct-player cutscene events, code.bin global entrances, and decoded ZSI scene cutscene sources.",
        "",
        f"- Handoff rows: {summary['handoff_row_count']}",
        f"- Candidate rows: {summary['candidate_row_count']}",
        f"- Effective entrance rows: {summary['effective_entrance_row_count']}",
        f"- Status counts: {summary['status_counts']}",
        f"- Transition request indices: {', '.join(summary['transition_request_indices'])}",
        f"- Effective entrance expression: `{summary['scene_layer_offset_source']['effective_entrance_expression']}`",
        f"- Scene-layer offset source: `{summary['scene_layer_offset_source']['scene_layer_offset_source_address']}`",
        "",
        "## Rows",
        "",
        "| Handoff | Event | Source cutscene | Request | Scene | Local | Field | Candidates | Effective entrances | Cutscene candidates | Status |",
        "| ---: | ---: | ---: | ---: | --- | ---: | --- | ---: | ---: | ---: | --- |",
    ]
    for row in payload["handoff_rows"]:
        lines.append(
            f"| {row['handoff_source_index']} | {row['direct_player_event_source_index']} | "
            f"{row['source_cutscene_source_index']} | `0x{row['transition_request_index']:04X}` | "
            f"{row['native_scene_path_candidate']} | {row['local_entrance_index']} | "
            f"`0x{row['field']:04X}` | {row['candidate_count']} | "
            f"{row['effective_entrance_count']} | "
            f"{row['cutscene_candidate_count']} | {row['status']} |"
        )
    lines.extend(
        [
            "",
            "## Effective Entrance Candidates",
            "",
            "| Handoff | Offset | Effective index | Raw | Scene | Local | Field | Native scene | Matches | Cutscene matches | Status |",
            "| ---: | ---: | ---: | --- | ---: | ---: | --- | --- | ---: | ---: | --- |",
        ]
    )
    for row in payload["effective_entrance_rows"]:
        lines.append(
            f"| {row['handoff_source_index']} | {row['scene_layer_offset']} | "
            f"`0x{row['effective_entrance_index']:04X}` | `{row['raw_hex']}` | "
            f"`0x{row['scene_id']:02X}` | {row['local_entrance_index']} | "
            f"`0x{row['field']:04X}` | `{row['native_scene_path_candidate']}` | "
            f"{row['native_match_count']} | {row['cutscene_candidate_count']} | "
            f"`{row['decode_status']}` |"
        )
    lines.extend(
        [
            "",
            "## Notes",
            "",
            "- Multi-candidate rows are intentionally not resolved here; the missing discriminator is the native scene-layer/setup value consumed by OOT3D scene-load code.",
            "- `FUN_004490F8` computes the active local entrance index from `globalEntranceTable[*0x00587958 + *(s32*)(0x00588958 + 0x4E8)]`, so `transition_request_index` alone is incomplete.",
            "- `FUN_002E2E60` copies `play+0x5C32` into the global entrance index before the next load, preserving the route from direct-player cutscene events to the normal scene loader.",
            "- For the intro transition `0x00CD`, the table exposes both all native `spot00_info.zsi` setup candidates and the effective global-entrance variants read directly from `code.bin`.",
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main() -> int:
    payload = build_rows()
    DEFAULT_OUT_JSON.parent.mkdir(parents=True, exist_ok=True)
    DEFAULT_OUT_JSON.write_text(json.dumps(payload, indent=2, sort_keys=True), encoding="utf-8", newline="\n")
    write_csv(DEFAULT_OUT_HANDOFF_CSV, payload["handoff_rows"])
    write_csv(DEFAULT_OUT_CANDIDATE_CSV, payload["candidate_rows"])
    write_csv(DEFAULT_OUT_EFFECTIVE_ENTRANCE_CSV, payload["effective_entrance_rows"])
    write_markdown(DEFAULT_OUT_MD, payload)
    write_header(DEFAULT_OUT_HEADER, payload)
    write_source(DEFAULT_OUT_SOURCE, payload)
    print(
        "wrote "
        f"{len(payload['handoff_rows'])} handoff rows and "
        f"{len(payload['candidate_rows'])} candidates and "
        f"{len(payload['effective_entrance_rows'])} effective entrances"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
