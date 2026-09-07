from __future__ import annotations

import json
import re
from collections import Counter
from pathlib import Path

from .binary import ParseError
from .kokiri_runtime_route_audit import (
    COMMAND_NAMES,
    actor_entry_from_payload,
    load_semantic_names,
    path_block_candidate_from_payload,
    room_references_from_payload,
    transition_actors_from_payload,
    validate_entrance_layout,
)
from .romfs_inventory import sorted_counter
from .zsi import ZSI_RESOURCE_BASE_OFFSET, ZsiFile
from .zsi_scene_audit import zsi_scene_role, zsi_scene_stem

SCENE_INDEX_FORMAT = "oot3d_native_scene_index_v1"
ACTOR_ENTRY_SIZE = 0x10
LIGHT_SETTINGS_RECORD_SIZE = 0x1C
ROOM_LIGHT_RECORD_SIZE = 0x18
POINTER_COMMAND_IDS = frozenset(
    (0x00, 0x01, 0x03, 0x04, 0x06, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x13, 0x17, 0x18)
)
EXIT_HIGH_REMAP_BASE = 0x7FF9
EXIT_SPECIAL_VALUE_7FFF = 0x7FFF
EXIT_CATEGORY_C_NAMES = {
    "direct_transition_value": "OOT3D_EXIT_DIRECT_TRANSITION_VALUE",
    "high_remap_0x7ff9_to_0x7ffe": "OOT3D_EXIT_HIGH_REMAP_0X7FF9_TO_0X7FFE",
    "special_0x7fff": "OOT3D_EXIT_SPECIAL_0X7FFF",
    "unhandled_signed_value": "OOT3D_EXIT_UNHANDLED_SIGNED_VALUE",
}
ROOM_OBJECT_STATUS_C_NAMES = {
    "known_object_id": "OOT3D_ROOM_OBJECT_KNOWN_ID",
    "unknown_oot3d_object_id": "OOT3D_ROOM_OBJECT_UNKNOWN_OOT3D_ID",
}
ROOM_INFO_RE = re.compile(
    r"^(?P<stem>.+)_(?P<room_index>\d+)(?:_dd)?_info\.zsi$", re.IGNORECASE
)
COMMAND_DECOMPILATION_EVIDENCE: dict[int, dict[str, object]] = {
    0x00: {
        "support_level": "code_bin_handler_confirmed",
        "export_structs": ["Oot3dActorEntry", "Oot3dSceneSetupIndex.spawns"],
        "native_asset_evidence": [
            "ZSI scene command 0x00 points to 0x10-byte actor/spawn entries."
        ],
        "code_bin_evidence": [
            "Scene command handler table 0x0053CC84 slot 0x00 points to handler 0x00256BD8.",
            "Handler 0x00256BD8 selects a 0x10-byte spawn entry from sceneBase + command[4] using the active entrance index, stores it at play+0x5C0C, then performs object-bank setup.",
        ],
        "open_questions": [
            "Name the object-bank globals used by the spawn handler before promoting every side effect to source."
        ],
    },
    0x01: {
        "support_level": "code_bin_handler_confirmed",
        "export_structs": ["Oot3dActorEntry", "Oot3dSceneSetupIndex.standardActors"],
        "native_asset_evidence": [
            "ZSI scene command 0x01 points to standard 0x10-byte actor entries."
        ],
        "code_bin_evidence": [
            "Scene command handler table 0x0053CC84 slot 0x01 points to handler 0x0023447C.",
            "Handler 0x0023447C stores command byte +1 at play+0x5C03 and sceneBase + command[4] at play+0x5C10.",
        ],
        "open_questions": [],
    },
    0x03: {
        "support_level": "code_bin_handler_confirmed",
        "export_structs": ["scene.collision_header_candidates"],
        "native_asset_evidence": [
            "ZSI scene command 0x03 points to native collision-header candidates."
        ],
        "code_bin_evidence": [
            "Scene command handler table 0x0053CC84 slot 0x03 points to handler 0x00273070.",
            "Handler 0x00273070 relocates collision-header pointer fields at offsets 0x18..0x28, relocates nested records reached through header+0x24, then initializes play+0x0A98 spatial partition state.",
        ],
        "open_questions": [
            "Promote the native collision-header subfield names from the collision audits into the exported C structs."
        ],
    },
    0x04: {
        "support_level": "code_bin_handler_confirmed",
        "export_structs": ["Oot3dRoomReference", "Oot3dSceneIndex.roomRefs"],
        "native_asset_evidence": [
            "ZSI scene command 0x04 exposes room ZSI path references in the native path block."
        ],
        "code_bin_evidence": [
            "Scene command handler table 0x0053CC84 slot 0x04 points to handler 0x0039DED0.",
            "Handler 0x0039DED0 stores command byte +1 at play+0x5C04 and sceneBase + command[4] at play+0x5C08.",
        ],
        "open_questions": [],
    },
    0x05: {
        "support_level": "code_bin_handler_confirmed",
        "export_structs": ["Oot3dRoomWindSettings"],
        "native_asset_evidence": [
            "ZSI room command 0x05 carries three signed direction bytes and one strength byte."
        ],
        "code_bin_evidence": [
            "Scene command handler table 0x0053CC84 slot 0x05 points to handler 0x00217C2C.",
            "Handler 0x00217C2C sign-extends command[4..6] into play+0x3220/+0x3222/+0x3224 and converts command[7] to float at play+0x3228.",
        ],
        "open_questions": [],
    },
    0x06: {
        "support_level": "code_bin_handler_confirmed",
        "export_structs": ["Oot3dEntranceEntry", "Oot3dSceneSetupIndex.entrances"],
        "native_asset_evidence": [
            "ZSI scene command 0x06 exposes 2-byte entrance entries at file+0x10+command[4]."
        ],
        "code_bin_evidence": [
            "Scene command handler table 0x0053CC84 slot 0x06 points to handler 0x003791A8.",
            "Handler 0x003791A8 stores sceneBase + command[4] at play+0x5C18.",
        ],
        "open_questions": [
            "Confirm downstream entrance-table consumers before naming every packed entrance bitfield."
        ],
    },
    0x07: {
        "support_level": "code_bin_handler_confirmed",
        "export_structs": ["Oot3dSpecialFiles", "Oot3dSceneSetupIndex.specialFiles"],
        "native_asset_evidence": [
            "ZSI scene command 0x07 carries C_UP_ELF message file and keep object id fields."
        ],
        "code_bin_evidence": [
            "Scene command handler table 0x0053CC84 slot 0x07 points to handler 0x00394550.",
            "Handler 0x00394550 spawns the object id in command[4] when nonzero and maps command byte +1 through a native table to a Play-state symbolic offset.",
        ],
        "open_questions": [
            "Resolve DAT_003945B8/DAT_003945BC/DAT_003945C0 to final source names."
        ],
    },
    0x08: {
        "support_level": "code_bin_handler_confirmed",
        "export_structs": ["Oot3dRoomBehavior"],
        "native_asset_evidence": [
            "ZSI room command 0x08 carries the room behavior byte and packed native behavior flags."
        ],
        "code_bin_evidence": [
            "Scene command handler table 0x0053CC84 slot 0x08 points to handler 0x002344C4.",
            "Handler 0x002344C4 stores command[1], command[4], and argument bits 8, 9, and 10 in distinct Play-state fields.",
        ],
        "open_questions": [
            "Retain packed flag names until their OoT3D consumers prove gameplay-facing semantics."
        ],
    },
    0x0A: {
        "support_level": "code_bin_handler_confirmed",
        "export_structs": ["Oot3dRoomMeshHeaderReference"],
        "native_asset_evidence": [
            "ZSI room command 0x0A points to the native room mesh header associated with the embedded CMB."
        ],
        "code_bin_evidence": [
            "Scene command handler table 0x0053CC84 slot 0x0A points to handler 0x003A590C.",
            "Handler 0x003A590C relocates the mesh header and its type-specific nested pointers before creating native room render resources.",
        ],
        "open_questions": [
            "Promote the complete type-0/type-2 mesh-header substructures after their nested records are typed."
        ],
    },
    0x0B: {
        "support_level": "code_bin_handler_confirmed",
        "export_structs": ["Oot3dRoomObjectEntry"],
        "native_asset_evidence": [
            "ZSI room command 0x0B points to an exact command[1]-count list of signed 16-bit object ids."
        ],
        "code_bin_evidence": [
            "Scene command handler table 0x0053CC84 slot 0x0B points to handler 0x003A7558.",
            "Handler 0x003A7558 iterates command[1] signed halfwords from sceneBase + command[4] and reconciles the native object context.",
        ],
        "open_questions": [],
    },
    0x0C: {
        "support_level": "code_bin_handler_confirmed",
        "export_structs": ["Oot3dRoomLightRecord"],
        "native_asset_evidence": [
            "ZSI room command 0x0C points to command[1] native light records with stride 0x18."
        ],
        "code_bin_evidence": [
            "Scene command handler table 0x0053CC84 slot 0x0C points to handler 0x002A9DDC.",
            "Handler 0x002A9DDC submits each 0x18-byte record from sceneBase + command[4] to native light insertion helper 0x0034FAA8.",
        ],
        "open_questions": [
            "Type the point/directional light union without importing the N64 layout as authority."
        ],
    },
    0x0D: {
        "support_level": "code_bin_handler_confirmed",
        "export_structs": [
            "Oot3dPathRecord",
            "Oot3dVec3s",
            "Oot3dSceneSetupIndex.paths",
        ],
        "native_asset_evidence": [
            "ZSI scene command 0x0D has validated 8-byte path-record windows and same-ZSI Vec3s point arrays."
        ],
        "code_bin_evidence": [
            "Scene command handler table 0x0053CC84 slot 0x0D points to handler 0x002985F0.",
            "Handler 0x002985F0 treats command byte +1 as the path count, relocates each 8-byte record's +4 pointer by sceneBase, then stores the path list pointer through DAT_00298680 + play.",
            "DAT_00298680 resolves to Play-state offset 0x5C20.",
            "Path_GetByIndex at 0x00348FF0 returns play+0x5C20 + index*8 unless index equals the caller-provided sentinel.",
            "Exported actor consumers read byte +0 as point count and word +4 as a relocated Vec3s point-list pointer.",
            "Native ZSI extraction applies resource base file+0x10 to both the path table and each nested Vec3s pointer and decodes all 69 path records.",
        ],
        "open_questions": [
            "Bytes +1..+3 of each native path record remain unknown/padding; keep them named conservatively until a consumer proves additional meaning.",
            "Actor-specific path-index formulas are tracked separately from the scene path-list data format.",
        ],
    },
    0x0E: {
        "support_level": "code_bin_handler_confirmed",
        "export_structs": [
            "Oot3dTransitionActorEntry",
            "Oot3dSceneSetupIndex.transitions",
        ],
        "native_asset_evidence": [
            "ZSI scene command 0x0E exposes native 0x10-byte transition actor entries."
        ],
        "code_bin_evidence": [
            "Scene command handler table 0x0053CC84 slot 0x0E points to handler 0x002985D0.",
            "Handler 0x002985D0 stores command byte +1 at play+0x5B88 and sceneBase + command[4] at play+0x5B8C.",
        ],
        "open_questions": [],
    },
    0x0F: {
        "support_level": "code_bin_confirmed",
        "export_structs": [
            "Oot3dPicaLightSettingsRecord",
            "Oot3dSceneSetupIndex.lightSettings",
        ],
        "native_asset_evidence": [
            "ZSI scene command 0x0F points to native 0x1C-stride light-setting records."
        ],
        "code_bin_evidence": [
            "Scene command parser dispatches command 0x0F through handler table 0x0053CC84 to handler 0x00379188.",
            "Handler 0x00379188 stores command byte +1 at play+0x322C and sceneBase + command[4] at play+0x3230.",
            "Runtime consumer 0x0045DD50 reads play+0x3230 and blends 0x1C-stride records into environment light state.",
            "Evidence note: tools/oot3d/decomp_support/analysis/oot3d_pica_native_origin_structures.md.",
        ],
        "open_questions": [],
    },
    0x10: {
        "support_level": "code_bin_handler_confirmed",
        "export_structs": ["Oot3dRoomTimeSettings"],
        "native_asset_evidence": [
            "ZSI room command 0x10 carries native hour, minute, and time-speed bytes."
        ],
        "code_bin_evidence": [
            "Scene command handler table 0x0053CC84 slot 0x10 points to handler 0x00217A5C.",
            "Handler 0x00217A5C converts command[4..5] to the native day-time value and stores command[6] as the room time increment, with 0xFF sentinel handling.",
        ],
        "open_questions": [],
    },
    0x11: {
        "support_level": "code_bin_handler_confirmed",
        "export_structs": [
            "Oot3dSkyboxSettings",
            "Oot3dSceneSetupIndex.skyboxSettings",
        ],
        "native_asset_evidence": [
            "ZSI scene command 0x11 carries skybox id, weather/unknown byte, and indoors byte."
        ],
        "code_bin_evidence": [
            "Scene command handler table 0x0053CC84 slot 0x11 points to handler 0x0038A164.",
            "Handler 0x0038A164 stores command[4] to a symbolic Play-state offset, mirrors command[5] to play+0x31A8/play+0x31A9, stores command[6] to play+0x31B0, and derives a float at play+0x321C from command[7].",
        ],
        "open_questions": [
            "Resolve DAT_0038A1B4 and the command[7] scale constants against the kankyo/skybox consumer."
        ],
    },
    0x12: {
        "support_level": "code_bin_handler_confirmed",
        "export_structs": ["Oot3dRoomSkyboxDisableSettings"],
        "native_asset_evidence": [
            "ZSI room command 0x12 carries three independent sky/environment control bytes."
        ],
        "code_bin_evidence": [
            "Scene command handler table 0x0053CC84 slot 0x12 points to handler 0x00381D18.",
            "Handler 0x00381D18 copies command[4], command[5], and command[6] to play+0x31A5..0x31A7.",
        ],
        "open_questions": [
            "Name the three controls only after their OoT3D kankyo readers are reconstructed."
        ],
    },
    0x13: {
        "support_level": "code_bin_handler_confirmed",
        "export_structs": ["Oot3dExitEntry", "Oot3dSceneSetupIndex.exits"],
        "native_asset_evidence": [
            "ZSI scene command 0x13 has validated s16 exit-value payload windows."
        ],
        "code_bin_evidence": [
            "Scene command handler table 0x0053CC84 slot 0x13 points to handler 0x002A9E2C.",
            "Handler 0x002A9E2C stores sceneBase + command[4] at play+0x5C1C; the command does not carry an explicit count byte in the handler.",
            "Primary consumer 0x003365B0 reads s16 exit = *(play+0x5C1C + collisionResult*2 - 2).",
            "Signed exit values below 0x7FF9 are passed directly to helper 0x003348E8, which stores play+0x5C32 and play+0x5C2D when no transition is pending.",
            "Exit value 0x7FFF follows a separate helper 0x003716F0 path that also stores play+0x5C76.",
            "Exit values 0x7FF9..0x7FFE index high-remap delta table 0x0053A1E7 and entrance table 0x0053C094 using current entrance index play+0x5C02.",
            "The generated scene-transition support table emits the complete high-remap delta range and the asset-proven entrance-table window from code.bin.",
            "Exit-list entry count is derived from the native file+0x10-relative payload window; the command handler itself stores only the payload pointer.",
        ],
        "open_questions": [
            "Promote final source names for transition request helpers only after the surrounding transition state machine is named.",
            "Do not claim the high-remap entrance table extent beyond the asset-proven code.bin window until a wider native consumer requires it.",
            "Classify high-remap values not observed in native ZSI exit lists as reserved or runtime-only before assigning per-value names.",
        ],
    },
    0x14: {
        "support_level": "native_control_marker",
        "export_structs": ["Oot3dSceneCommand"],
        "native_asset_evidence": [
            "ZSI scene command 0x14 terminates a setup command list."
        ],
        "code_bin_evidence": [],
        "open_questions": [],
    },
    0x15: {
        "support_level": "code_bin_handler_confirmed",
        "export_structs": ["Oot3dSoundSettings", "Oot3dSceneSetupIndex.soundSettings"],
        "native_asset_evidence": [
            "ZSI scene command 0x15 carries sound spec id in command[1], nature ambience id in command[2], and a full BCSAR sound id in command[4..7]."
        ],
        "code_bin_evidence": [
            "Scene command handler table 0x0053CC84 slot 0x15 points to handler 0x00273108.",
            "Handler 0x00273108 stores command[4..7] at play+0x0A68, stores command[2] at play+0x0A6C, and conditionally passes command[1] to an audio helper.",
        ],
        "open_questions": [
            "Name the audio state global and helper FUN_0032C5DC before promoting the side effect."
        ],
    },
    0x16: {
        "support_level": "code_bin_handler_confirmed",
        "export_structs": ["Oot3dRoomAudioSettings"],
        "native_asset_evidence": [
            "ZSI room command 0x16 carries the native room audio/environment halfwords and control byte."
        ],
        "code_bin_evidence": [
            "Scene command handler table 0x0053CC84 slot 0x16 points to handler 0x00256C7C.",
            "Handler 0x00256C7C stores command halfwords +2/+4 and command byte +6 in native Play/global state.",
        ],
        "open_questions": [
            "Keep the two halfwords backend-native until their downstream audio/environment consumers are named."
        ],
    },
    0x17: {
        "support_level": "code_bin_handler_confirmed",
        "export_structs": ["Oot3dCutsceneReference", "Oot3dSceneSetupIndex.cutscenes"],
        "native_asset_evidence": [
            "ZSI scene command 0x17 references in-file cutscene command data."
        ],
        "code_bin_evidence": [
            "Scene command handler table 0x0053CC84 slot 0x17 points to handler 0x0023449C.",
            "Handler 0x0023449C passes sceneBase + command[4] to oot3d_set_field_229c_clear_22ac and then calls FUN_00357EA0.",
        ],
        "open_questions": [
            "Resolve FUN_00357EA0 and promote cutscene/camera routing names from the native cutscene audits."
        ],
    },
    0x18: {
        "support_level": "code_bin_handler_confirmed",
        "export_structs": ["Oot3dAlternateHeaderList"],
        "native_asset_evidence": [
            "ZSI command 0x18 selects another native command stream from an in-file pointer list."
        ],
        "code_bin_evidence": [
            "Scene command handler table 0x0053CC84 slot 0x18 points to handler 0x00378FF8.",
            "Handler 0x00378FF8 selects the active alternate-header pointer, relocates it by scene base, and dispatches its commands through table 0x0053CC84 until 0x14.",
        ],
        "open_questions": [
            "The extracted RomFS flattens observed alternate setup lists; preserve the original selector contract for unflattened inputs."
        ],
    },
    0x19: {
        "support_level": "code_bin_handler_confirmed",
        "export_structs": ["Oot3dMiscSettings", "Oot3dSceneSetupIndex.miscSettings"],
        "native_asset_evidence": [
            "ZSI scene command 0x19 carries camera/world-map area parameter and raw argument."
        ],
        "code_bin_evidence": [
            "Scene command handler table 0x0053CC84 slot 0x19 points to handler 0x00256CA8.",
            "Handler 0x00256CA8 stores command[1] to a global field, stores command[4] as a short at DAT_00256D3C+0xAE, and applies scene-id-specific world/map side effects.",
        ],
        "open_questions": [
            "Resolve the misc-settings globals DAT_00256D38/DAT_00256D3C/DAT_00256D44 before final field naming."
        ],
    },
}


def export_zsi_scene_index(
    scene_root: Path,
    output_path: Path | None = None,
    *,
    markdown_output_path: Path | None = None,
    c_output_dir: Path | None = None,
    scene_stems: list[str] | None = None,
    actor_object_semantics: Path | None = None,
    sample_limit: int = 20,
    full_entries: bool = False,
    include_room_mesh_summaries: bool = True,
) -> dict[str, object]:
    if not scene_root.is_dir():
        raise ParseError(f"{scene_root}: expected an extracted OOT3D scene directory")
    if c_output_dir is not None and not full_entries:
        raise ParseError(
            "--c-output-dir requires --full-entries so generated C data is not sampled"
        )

    semantic_names = load_semantic_names(
        resolve_actor_object_semantics(actor_object_semantics)
    )
    selected_stems = {stem.lower() for stem in scene_stems or []}

    scene_paths: list[Path] = []
    room_paths_by_stem: dict[str, list[Path]] = {}
    unique_room_paths: set[Path] = set()
    for path in sorted(scene_root.glob("*.zsi")):
        role = zsi_scene_role(path)
        stem = zsi_scene_stem(path)
        if role == "scene":
            if not selected_stems or stem in selected_stems:
                scene_paths.append(path)
        elif role == "room":
            unique_room_paths.add(path)
            room_paths_by_stem.setdefault(stem, []).append(path)

    records: list[dict[str, object]] = []
    parse_errors: list[dict[str, object]] = []
    command_id_counts: Counter[str] = Counter()
    decoded_payload_status_counts: Counter[str] = Counter()
    decompilation_support_level_counts: Counter[str] = Counter()
    room_actor_status_counts: Counter[str] = Counter()
    light_record_total = 0
    spawn_entry_total = 0
    room_actor_entry_total = 0

    for scene_path in scene_paths:
        try:
            record = build_scene_index_record(
                scene_root,
                scene_path,
                room_paths_by_stem.get(zsi_scene_stem(scene_path), []),
                semantic_names,
                sample_limit=sample_limit,
                full_entries=full_entries,
                include_room_mesh_summaries=include_room_mesh_summaries,
            )
        except Exception as exc:
            parse_errors.append(
                {
                    "path": scene_path.relative_to(scene_root).as_posix(),
                    "stage": "scene_index",
                    "error": str(exc),
                }
            )
            continue

        records.append(record)
        for setup in record["setups"]:
            for command in setup["commands"]:
                command_id_counts[str(command["command_id_hex"])] += 1
                evidence = command.get("decompilation_evidence")
                if isinstance(evidence, dict):
                    decompilation_support_level_counts[
                        str(evidence.get("support_level", "unknown"))
                    ] += 1
                decoded = command.get("decoded")
                if isinstance(decoded, dict):
                    decoded_payload_status_counts[
                        str(decoded.get("status", "unknown"))
                    ] += 1
                    if command["command_id"] == 0x00:
                        selected = decoded.get("selected_candidate")
                        if isinstance(selected, dict):
                            spawn_entry_total += int(
                                selected.get("entry_count", 0) or 0
                            )
                    if command["command_id"] == 0x0F:
                        light_record_total += int(decoded.get("record_count", 0) or 0)
        for room in record["rooms"]:
            for room_setup in room.get("setups", []):
                room_actor_payload = room_setup.get("actor_list")
                if isinstance(room_actor_payload, dict):
                    room_actor_status_counts[
                        str(room_actor_payload.get("status", "unknown"))
                    ] += 1
                    selected = room_actor_payload.get("selected_candidate")
                    if isinstance(selected, dict):
                        room_actor_entry_total += int(
                            selected.get("entry_count", 0) or 0
                        )

    unique_room_records: dict[str, dict[str, object]] = {}
    room_setup_alignment_mismatches: list[dict[str, object]] = []
    room_setup_binding_count = 0
    for scene in records:
        expected_setup_indices = [setup["index"] for setup in scene["setups"]]
        for room in scene["rooms"]:
            room_path = str(room["room_path"])
            unique_room_records.setdefault(room_path, room)
            actual_setup_indices = [setup["index"] for setup in room["setups"]]
            room_setup_binding_count += len(actual_setup_indices)
            if actual_setup_indices != expected_setup_indices:
                room_setup_alignment_mismatches.append(
                    {
                        "scene_path": scene["scene_path"],
                        "room_path": room_path,
                        "scene_setup_indices": expected_setup_indices,
                        "room_setup_indices": actual_setup_indices,
                    }
                )

    index: dict[str, object] = {
        "format": SCENE_INDEX_FORMAT,
        "scene_root": str(scene_root),
        "scene_filter": sorted(selected_stems),
        "source_policy": {
            "native_source": "OOT3D scene and room ZSI files from extracted RomFS",
            "semantic_labels": (
                "Actor/object enum names are labels from the local OOT3D decomp support header; "
                "N64 source is not used as an asset substitute."
            ),
            "validation_sources": (
                "code.bin/Ghidra and emulator captures may validate layouts, but exported records "
                "come from native OOT3D ZSI payloads."
            ),
        },
        "scene_count": len(records),
        "room_binding_count": sum(len(record["rooms"]) for record in records),
        "unique_room_file_count": len(
            {room["room_path"] for record in records for room in record["rooms"]}
        ),
        "available_unique_room_file_count": len(unique_room_paths),
        "setup_count": sum(len(record["setups"]) for record in records),
        "room_setup_binding_count": room_setup_binding_count,
        "unique_room_setup_count": sum(
            len(room["setups"]) for room in unique_room_records.values()
        ),
        "room_setup_alignment_mismatch_count": len(room_setup_alignment_mismatches),
        "room_setup_alignment_mismatches": room_setup_alignment_mismatches,
        "command_id_counts": sorted_counter(command_id_counts),
        "decoded_payload_status_counts": sorted_counter(decoded_payload_status_counts),
        "decompilation_support_level_counts": sorted_counter(
            decompilation_support_level_counts
        ),
        "room_actor_status_counts": sorted_counter(room_actor_status_counts),
        "spawn_entry_total": spawn_entry_total,
        "room_actor_entry_total": room_actor_entry_total,
        "light_settings_record_total": light_record_total,
        "parse_error_count": len(parse_errors),
        "parse_errors": parse_errors,
        "records": records,
    }

    if c_output_dir is not None:
        c_output_files = write_scene_index_c_sources(index, c_output_dir)
        index["c_output_dir"] = str(c_output_dir)
        index["c_output_files"] = [str(path) for path in c_output_files]

    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(
            json.dumps(index, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    if markdown_output_path is not None:
        markdown_output_path.parent.mkdir(parents=True, exist_ok=True)
        markdown_output_path.write_text(
            scene_index_markdown(index),
            encoding="utf-8",
            newline="\n",
        )

    return index


def build_scene_index_record(
    scene_root: Path,
    scene_path: Path,
    room_paths: list[Path],
    semantic_names: dict[str, dict[int, str]],
    *,
    sample_limit: int,
    full_entries: bool,
    include_room_mesh_summaries: bool,
) -> dict[str, object]:
    zsi = ZsiFile.from_path(scene_path)
    data = zsi.data
    stem = zsi_scene_stem(scene_path)
    room_records = [
        build_room_index_record(
            scene_root,
            room_path,
            semantic_names,
            sample_limit=sample_limit,
            full_entries=full_entries,
            include_mesh_summary=include_room_mesh_summaries,
        )
        for room_path in sorted(room_paths, key=room_sort_key)
    ]
    valid_room_indices = {
        int(room["room_index"])
        for room in room_records
        if isinstance(room.get("room_index"), int)
    }
    room_path_by_name = {
        room_path.name.lower(): room_path.relative_to(scene_root).as_posix()
        for room_path in room_paths
    }

    setup_records = []
    for setup in zsi.scene_setups():
        setup_records.append(
            {
                "index": setup.index,
                "offset": setup.offset,
                "offset_hex": f"0x{setup.offset:x}",
                "end_offset": setup.end_offset,
                "end_offset_hex": f"0x{setup.end_offset:x}",
                "command_count": len(setup.commands),
                "command_ids": [
                    f"0x{command.command_id:02x}" for command in setup.commands
                ],
                "setup_role": "cutscene"
                if setup.first_command(0x17) is not None
                else "gameplay",
                "commands": [
                    decode_scene_index_command(
                        data,
                        command,
                        setup.commands,
                        semantic_names,
                        valid_room_indices=valid_room_indices,
                        room_path_by_name=room_path_by_name,
                        sample_limit=sample_limit,
                        full_entries=full_entries,
                    )
                    for command in setup.commands
                ],
            }
        )

    collision_candidates = [
        candidate.summary() for candidate in zsi.collision_header_candidates()
    ]
    return {
        "scene_stem": stem,
        "scene_path": scene_path.relative_to(scene_root).as_posix(),
        "scene_size": len(data),
        "setup_count": len(setup_records),
        "rooms": room_records,
        "setups": setup_records,
        "collision_header_candidates": collision_candidates,
    }


def build_room_index_record(
    scene_root: Path,
    room_path: Path,
    semantic_names: dict[str, dict[int, str]],
    *,
    sample_limit: int,
    full_entries: bool,
    include_mesh_summary: bool,
) -> dict[str, object]:
    data = room_path.read_bytes()
    match = ROOM_INFO_RE.match(room_path.name)
    room_index = int(match.group("room_index")) if match else None
    zsi = ZsiFile.from_path(room_path)
    setup_records = [
        build_room_setup_index_record(
            data,
            setup,
            semantic_names,
            sample_limit=sample_limit,
            full_entries=full_entries,
        )
        for setup in zsi.scene_setups()
    ]

    cmb_records: list[dict[str, object]] = []
    if include_mesh_summary:
        for cmb in zsi.embedded_cmbs():
            cmb_records.append(
                {
                    "index": cmb.index,
                    "offset": cmb.offset,
                    "offset_hex": f"0x{cmb.offset:x}",
                    "size": cmb.size,
                    "name": cmb.model.name,
                    "static_candidate": cmb.model.is_static_candidate(),
                    "rigid_export_candidate": cmb.model.is_rigid_export_candidate(),
                    "texture_count": len(cmb.model.textures),
                    "material_count": len(cmb.model.materials),
                    "mesh_count": len(cmb.model.meshes),
                    "shape_count": len(cmb.model.shapes),
                    "primitive_count": sum(
                        len(shape.primitives) for shape in cmb.model.shapes
                    ),
                    "vertex_count": sum(
                        len(shape.positions) for shape in cmb.model.shapes
                    ),
                    "triangle_count": sum(
                        len(primitive.indices) // 3
                        for shape in cmb.model.shapes
                        for primitive in shape.primitives
                    ),
                }
            )

    return {
        "room_path": room_path.relative_to(scene_root).as_posix(),
        "room_index": room_index,
        "room_size": len(data),
        "setup_count": len(setup_records),
        "setups": setup_records,
        "embedded_cmb_count": len(cmb_records),
        "embedded_cmbs": cmb_records,
        "room_actor_list": (
            setup_records[0]["actor_list"]
            if setup_records
            else {
                "status": "native_room_setup_missing",
                "selected_candidate": None,
            }
        ),
    }


def build_room_setup_index_record(
    data: bytes,
    setup,
    semantic_names: dict[str, dict[int, str]],
    *,
    sample_limit: int,
    full_entries: bool,
) -> dict[str, object]:
    commands = [
        decode_room_index_command(
            data,
            command,
            setup.commands,
            semantic_names,
            sample_limit=sample_limit,
            full_entries=full_entries,
        )
        for command in setup.commands
    ]
    object_command = next(
        (command for command in commands if command["command_id"] == 0x0B), None
    )
    actor_command = next(
        (command for command in commands if command["command_id"] == 0x01), None
    )
    object_list = room_object_list_from_command(object_command)
    actor_list = room_actor_list_from_command(actor_command, object_list)
    return {
        "index": setup.index,
        "offset": setup.offset,
        "offset_hex": f"0x{setup.offset:x}",
        "end_offset": setup.end_offset,
        "end_offset_hex": f"0x{setup.end_offset:x}",
        "command_count": len(commands),
        "command_ids": [command["command_id_hex"] for command in commands],
        "commands": commands,
        "object_list": object_list,
        "actor_list": actor_list,
    }


def native_command_payload_offset(command) -> int | None:
    if command.command_id not in POINTER_COMMAND_IDS:
        return None
    return command.argument + ZSI_RESOURCE_BASE_OFFSET


def native_payload_end_hint(
    data: bytes,
    command,
    setup_commands: tuple,
    *,
    default_window: int = 1024,
) -> int:
    payload_offset = native_command_payload_offset(command)
    if payload_offset is None or not (0 <= payload_offset < len(data)):
        return payload_offset if payload_offset is not None else 0
    following_offsets = [
        other_offset
        for other in setup_commands
        if other.offset != command.offset
        and (other_offset := native_command_payload_offset(other)) is not None
        and payload_offset < other_offset <= len(data)
    ]
    if following_offsets:
        return min(min(following_offsets), payload_offset + default_window)
    return min(len(data), payload_offset + default_window)


def decode_room_index_command(
    data: bytes,
    command,
    setup_commands: tuple,
    semantic_names: dict[str, dict[int, str]],
    *,
    sample_limit: int,
    full_entries: bool,
) -> dict[str, object]:
    payload_offset = native_command_payload_offset(command)
    payload_in_file = payload_offset is not None and 0 <= payload_offset < len(data)
    record: dict[str, object] = {
        "offset": command.offset,
        "offset_hex": f"0x{command.offset:x}",
        "command_id": command.command_id,
        "command_id_hex": f"0x{command.command_id:02x}",
        "command_name": COMMAND_NAMES.get(command.command_id, "unknown"),
        "parameter": command.parameter,
        "command_word": command.command_word,
        "command_word_hex": f"0x{command.command_word:08x}",
        "argument": command.argument,
        "argument_hex": f"0x{command.argument:08x}",
        "argument_in_file": payload_in_file,
        "decompilation_evidence": command_decompilation_evidence(command.command_id),
    }
    if payload_in_file:
        record["payload_end_hint"] = native_payload_end_hint(
            data, command, setup_commands
        )
        record["payload_sample_hex"] = data[
            payload_offset : min(len(data), payload_offset + 32)
        ].hex()

    decoded: dict[str, object]
    if command.command_id == 0x01:
        payload_offset = command.argument + ZSI_RESOURCE_BASE_OFFSET
        required_size = command.parameter * ACTOR_ENTRY_SIZE
        if payload_offset + required_size <= len(data):
            entries = [
                actor_entry_from_payload(
                    data,
                    payload_offset + index * ACTOR_ENTRY_SIZE,
                    index,
                    semantic_names,
                )
                for index in range(command.parameter)
            ]
            record_limit = (
                len(entries) if full_entries else min(len(entries), sample_limit)
            )
            decoded = {
                "status": "decoded_native_room_actor_list",
                "layout": "oot3d_actor_entry_0x10",
                "entry_size": ACTOR_ENTRY_SIZE,
                "entry_count": len(entries),
                "source_argument": command.argument,
                "source_offset": payload_offset,
                "resource_base_offset": ZSI_RESOURCE_BASE_OFFSET,
                "entries": entries[:record_limit],
                "entries_omitted_count": len(entries) - record_limit,
            }
        else:
            decoded = {
                "status": "native_room_actor_list_out_of_file",
                "entry_size": ACTOR_ENTRY_SIZE,
                "entry_count": command.parameter,
                "source_argument": command.argument,
                "source_offset": payload_offset,
                "resource_base_offset": ZSI_RESOURCE_BASE_OFFSET,
            }
    elif command.command_id == 0x05:
        raw = command.command_word.to_bytes(4, "little") + command.argument.to_bytes(
            4, "little"
        )
        decoded = {
            "status": "decoded_native_room_wind_settings",
            "direction_s8": [signed_u8(raw[index]) for index in (4, 5, 6)],
            "strength_u8": raw[7],
        }
    elif command.command_id == 0x08:
        decoded = {
            "status": "decoded_native_room_behavior",
            "behavior_u8": command.parameter,
            "argument_low_u8": command.argument & 0xFF,
            "argument_flag_bit_8": (command.argument >> 8) & 1,
            "argument_flag_bit_9": (command.argument >> 9) & 1,
            "argument_flag_bit_10": (command.argument >> 10) & 1,
        }
    elif command.command_id == 0x0A:
        payload_offset = command.argument + ZSI_RESOURCE_BASE_OFFSET
        decoded = {
            "status": (
                "decoded_native_room_mesh_header_reference"
                if 0 <= payload_offset < len(data)
                else "native_room_mesh_header_out_of_file"
            ),
            "mesh_header_argument": command.argument,
            "mesh_header_offset": payload_offset,
            "mesh_header_offset_hex": f"0x{payload_offset:x}",
            "resource_base_offset": ZSI_RESOURCE_BASE_OFFSET,
        }
    elif command.command_id == 0x0B:
        decoded = decode_room_object_entries(data, command, semantic_names)
    elif command.command_id == 0x0C:
        decoded = decode_room_light_records(
            data,
            command.argument + ZSI_RESOURCE_BASE_OFFSET,
            command.parameter,
            source_argument=command.argument,
            sample_limit=sample_limit,
            full_entries=full_entries,
        )
    elif command.command_id == 0x10:
        decoded = {
            "status": "decoded_native_room_time_settings",
            "hour_u8": command.argument & 0xFF,
            "minute_u8": (command.argument >> 8) & 0xFF,
            "time_speed_u8": (command.argument >> 16) & 0xFF,
            "reserved_u8": (command.argument >> 24) & 0xFF,
        }
    elif command.command_id == 0x12:
        decoded = {
            "status": "decoded_native_room_skybox_controls",
            "control_0_u8": command.argument & 0xFF,
            "control_1_u8": (command.argument >> 8) & 0xFF,
            "control_2_u8": (command.argument >> 16) & 0xFF,
            "reserved_u8": (command.argument >> 24) & 0xFF,
        }
    elif command.command_id == 0x16:
        decoded = {
            "status": "decoded_native_room_audio_environment_settings",
            "command_halfword_02": (command.command_word >> 16) & 0xFFFF,
            "argument_halfword_04": command.argument & 0xFFFF,
            "argument_control_u8": (command.argument >> 16) & 0xFF,
            "argument_reserved_u8": (command.argument >> 24) & 0xFF,
        }
    elif command.command_id == 0x14:
        decoded = {"status": "decoded_end_marker"}
    else:
        decoded = {"status": "native_room_command_payload_not_typed"}
    record["decoded"] = decoded
    return record


def decode_room_object_entries(
    data: bytes,
    command,
    semantic_names: dict[str, dict[int, str]],
) -> dict[str, object]:
    payload_offset = command.argument + ZSI_RESOURCE_BASE_OFFSET
    required_size = command.parameter * 2
    if payload_offset < 0 or payload_offset + required_size > len(data):
        return {
            "status": "native_room_object_list_out_of_file",
            "entry_size": 2,
            "entry_count": command.parameter,
            "source_argument": command.argument,
            "source_offset": payload_offset,
            "resource_base_offset": ZSI_RESOURCE_BASE_OFFSET,
        }
    object_names = semantic_names["object"]
    entries = []
    for index in range(command.parameter):
        offset = payload_offset + index * 2
        object_id = s16(data, offset)
        semantic_status = (
            "known_object_id"
            if object_id in object_names
            else "native_control_or_unknown_object_id"
        )
        entries.append(
            {
                "index": index,
                "offset": offset,
                "offset_hex": f"0x{offset:x}",
                "object_id": object_id,
                "object_id_raw_u16": u16(data, offset),
                "object_name": object_names.get(
                    object_id, f"OBJECT_0x{object_id & 0xFFFF:04x}"
                ),
                "semantic_status": semantic_status,
                "object_bank_dependency_candidate": object_id > 0,
            }
        )
    return {
        "status": "decoded_native_room_object_list",
        "layout": "oot3d_room_object_id_s16",
        "entry_size": 2,
        "entry_count": len(entries),
        "source_argument": command.argument,
        "source_offset": payload_offset,
        "resource_base_offset": ZSI_RESOURCE_BASE_OFFSET,
        "entries": entries,
    }


def decode_room_light_records(
    data: bytes,
    offset: int,
    count: int,
    *,
    source_argument: int,
    sample_limit: int,
    full_entries: bool,
) -> dict[str, object]:
    required_size = count * ROOM_LIGHT_RECORD_SIZE
    if offset < 0 or offset + required_size > len(data):
        return {
            "status": "native_room_light_list_out_of_file",
            "record_size": ROOM_LIGHT_RECORD_SIZE,
            "record_count": count,
            "source_argument": source_argument,
            "source_offset": offset,
            "resource_base_offset": ZSI_RESOURCE_BASE_OFFSET,
        }
    record_limit = count if full_entries else min(count, sample_limit)
    records = []
    for index in range(record_limit):
        cursor = offset + index * ROOM_LIGHT_RECORD_SIZE
        records.append(
            {
                "index": index,
                "offset": cursor,
                "offset_hex": f"0x{cursor:x}",
                "raw_hex": data[cursor : cursor + ROOM_LIGHT_RECORD_SIZE].hex(),
            }
        )
    return {
        "status": "decoded_native_room_light_list",
        "layout": "oot3d_room_light_record_0x18",
        "record_size": ROOM_LIGHT_RECORD_SIZE,
        "record_count": count,
        "source_argument": source_argument,
        "source_offset": offset,
        "resource_base_offset": ZSI_RESOURCE_BASE_OFFSET,
        "records": records,
        "records_omitted_count": count - record_limit,
    }


def room_object_list_from_command(
    command: dict[str, object] | None,
) -> dict[str, object]:
    if command is None:
        return {
            "status": "native_room_object_command_absence_identified",
            "source_offset": None,
            "entries": [],
            "unknown_entries": [],
        }
    decoded = command["decoded"]
    entries = list(decoded.get("entries", [])) if isinstance(decoded, dict) else []
    return {
        "status": (
            "native_room_object_command_identified"
            if decoded.get("status") == "decoded_native_room_object_list"
            else str(decoded.get("status", "native_room_object_command_invalid"))
        ),
        "source_offset": decoded.get("source_offset"),
        "entry_size": decoded.get("entry_size"),
        "entry_count": decoded.get("entry_count", 0),
        "entries": [
            entry
            for entry in entries
            if entry.get("semantic_status") == "known_object_id"
        ],
        "unknown_entries": [
            entry
            for entry in entries
            if entry.get("semantic_status") != "known_object_id"
        ],
        "raw_entries": entries,
    }


def room_actor_list_from_command(
    command: dict[str, object] | None,
    object_list: dict[str, object],
) -> dict[str, object]:
    if command is None:
        return {
            "status": "native_room_actor_command_absence_identified",
            "selected_candidate": {
                "confidence": "native_command_exact",
                "start_offset": None,
                "entry_size": ACTOR_ENTRY_SIZE,
                "entry_count": 0,
                "entries": [],
                "object_prefix": room_object_prefix_from_exact_list(object_list),
            },
            "candidate_count": 0,
            "evidence": ["Room setup has no native command 0x01."],
        }
    decoded = command["decoded"]
    valid = decoded.get("status") == "decoded_native_room_actor_list"
    return {
        "status": (
            "native_room_actor_command_identified"
            if valid
            else str(decoded.get("status", "native_room_actor_command_invalid"))
        ),
        "selected_candidate": (
            {
                "confidence": "native_command_exact",
                "start_offset": decoded.get("source_offset"),
                "entry_size": decoded.get("entry_size"),
                "entry_count": decoded.get("entry_count", 0),
                "entries": list(decoded.get("entries", [])),
                "object_prefix": room_object_prefix_from_exact_list(object_list),
            }
            if valid
            else None
        ),
        "candidate_count": 1 if valid else 0,
        "evidence": [
            "Native room command 0x01 supplies exact count, payload offset, and 0x10-byte actor stride.",
            "Handler 0x0023447C stores the same count and relocated pointer in Play state.",
        ],
    }


def room_object_prefix_from_exact_list(
    object_list: dict[str, object],
) -> dict[str, object]:
    return {
        "start_offset": object_list.get("source_offset"),
        "object_ids": list(object_list.get("entries", [])),
        "unknown_object_ids": list(object_list.get("unknown_entries", [])),
        "source": "native_room_command_0x0b",
    }


def signed_u8(value: int) -> int:
    return value - 0x100 if value & 0x80 else value


def native_actor_entry_candidate(
    data: bytes,
    command,
    semantic_names: dict[str, dict[int, str]],
    *,
    payload_end: int,
) -> dict[str, object] | None:
    payload_offset = native_command_payload_offset(command)
    required_size = command.parameter * ACTOR_ENTRY_SIZE
    if (
        payload_offset is None
        or command.parameter <= 0
        or payload_offset < 0
        or payload_offset + required_size > min(payload_end, len(data))
    ):
        return None
    entries = [
        actor_entry_from_payload(
            data,
            payload_offset + index * ACTOR_ENTRY_SIZE,
            index,
            semantic_names,
        )
        for index in range(command.parameter)
    ]
    return {
        "start_offset": payload_offset,
        "start_delta": ZSI_RESOURCE_BASE_OFFSET,
        "payload_end_hint": payload_end,
        "fits_payload_window": True,
        "expected_entry_count": command.parameter,
        "available_entry_count": command.parameter,
        "entry_count": len(entries),
        "confidence": "native_command_exact",
        "player_actor_count": sum(entry["actor_id"] == 0 for entry in entries),
        "known_actor_count": sum(
            int(entry["actor_id"]) in semantic_names["actor"] for entry in entries
        ),
        "entries": entries,
    }


def native_entrance_candidate(data: bytes, command) -> dict[str, object] | None:
    payload_offset = native_command_payload_offset(command)
    required_size = command.parameter * 2
    if (
        payload_offset is None
        or command.parameter <= 0
        or payload_offset < 0
        or payload_offset + required_size > len(data)
    ):
        return None
    entries = []
    for index in range(command.parameter):
        cursor = payload_offset + index * 2
        room_u8 = data[cursor + 1]
        entries.append(
            {
                "index": index,
                "offset": cursor,
                "spawn": data[cursor],
                "room": signed_u8(room_u8),
                "room_u8": room_u8,
            }
        )
    return {
        "start_offset": payload_offset,
        "start_delta": ZSI_RESOURCE_BASE_OFFSET,
        "payload_end_hint": payload_offset + required_size,
        "fits_payload_window": True,
        "expected_entry_count": command.parameter,
        "entry_count": len(entries),
        "confidence": "strong",
        "layout_variant": "oot3d_resource_base_relative",
        "entries": entries,
    }


def native_exit_values(
    data: bytes,
    command,
    setup_commands: tuple,
) -> tuple[list[dict[str, object]], int, int]:
    payload_offset = native_command_payload_offset(command)
    if payload_offset is None or not (0 <= payload_offset < len(data)):
        return [], payload_offset or 0, payload_offset or 0
    payload_end = native_payload_end_hint(data, command, setup_commands)
    payload_end -= (payload_end - payload_offset) % 2
    values = [
        {
            "index": index,
            "offset": cursor,
            "value": s16(data, cursor),
            "value_hex": f"0x{u16(data, cursor):04x}",
        }
        for index, cursor in enumerate(range(payload_offset, payload_end, 2))
    ]
    return values, payload_offset, payload_end


def decode_scene_index_command(
    data: bytes,
    command,
    setup_commands: tuple,
    semantic_names: dict[str, dict[int, str]],
    *,
    valid_room_indices: set[int],
    room_path_by_name: dict[str, str],
    sample_limit: int,
    full_entries: bool,
) -> dict[str, object]:
    payload_offset = native_command_payload_offset(command)
    payload_in_file = payload_offset is not None and 0 <= payload_offset < len(data)
    payload_end = native_payload_end_hint(data, command, setup_commands)
    record: dict[str, object] = {
        "offset": command.offset,
        "offset_hex": f"0x{command.offset:x}",
        "command_id": command.command_id,
        "command_id_hex": f"0x{command.command_id:02x}",
        "command_name": COMMAND_NAMES.get(command.command_id, "unknown"),
        "parameter": command.parameter,
        "command_word": command.command_word,
        "command_word_hex": f"0x{command.command_word:08x}",
        "argument": command.argument,
        "argument_hex": f"0x{command.argument:08x}",
        "argument_in_file": payload_in_file,
        "decompilation_evidence": command_decompilation_evidence(command.command_id),
    }
    if payload_in_file:
        record["payload_end_hint"] = payload_end
        record["payload_sample_hex"] = data[
            payload_offset : min(len(data), payload_offset + 32)
        ].hex()

    decoded: dict[str, object] | None = None
    if command.command_id == 0x00:
        selected = native_actor_entry_candidate(
            data,
            command,
            semantic_names,
            payload_end=payload_end,
        )
        decoded = {
            "status": "decoded_spawn_list"
            if selected
            else "spawn_list_candidate_missing",
            "layout": "oot3d_actor_entry_0x10_resource_base_relative",
            "expected_count": command.parameter,
            "selected_candidate": summarize_entry_candidate(
                selected, sample_limit, full_entries
            ),
            "candidate_count": 1 if selected else 0,
        }
    elif command.command_id == 0x01:
        selected = native_actor_entry_candidate(
            data,
            command,
            semantic_names,
            payload_end=payload_end,
        )
        decoded = {
            "status": "decoded_actor_list"
            if selected
            else "actor_list_candidate_missing",
            "layout": "oot3d_actor_entry_0x10",
            "expected_count": command.parameter,
            "selected_candidate": summarize_entry_candidate(
                selected, sample_limit, full_entries
            ),
        }
    elif command.command_id == 0x04:
        refs = room_references_from_payload(data, payload_offset or -1)
        decoded = {
            "status": "decoded_room_list",
            "layout": "oot3d_room_list_resource_base_relative",
            "expected_room_count": command.parameter,
            "source_argument": command.argument,
            "source_offset": payload_offset,
            "resource_base_offset": ZSI_RESOURCE_BASE_OFFSET,
            "room_references": [
                {
                    "index": index,
                    "path": ref,
                    "room_file": Path(ref).name,
                    "room_index": room_index_from_name(Path(ref).name),
                    "matched_local_room_path": room_path_by_name.get(
                        Path(ref).name.lower()
                    ),
                }
                for index, ref in enumerate(
                    refs[: max(command.parameter, sample_limit)]
                )
            ],
        }
    elif command.command_id == 0x03:
        decoded = {
            "status": "decoded_collision_header_reference",
            "layout": "oot3d_zsi_collision_header_candidate",
            "collision_argument": command.argument,
            "collision_argument_hex": f"0x{command.argument:08x}",
            "detail_location": "scene.collision_header_candidates",
        }
    elif command.command_id == 0x06:
        selected = native_entrance_candidate(data, command)
        decoded = {
            "status": "decoded_entrance_list"
            if selected
            else "entrance_list_candidate_missing",
            "layout": "oot3d_entrance_entry_0x02_candidates",
            "expected_count": command.parameter,
            "selected_candidate": summarize_entry_candidate(
                selected, sample_limit, full_entries
            ),
            "validation": validate_entrance_layout(
                selected,
                valid_room_indices=valid_room_indices,
            ),
            "candidate_count": 1 if selected else 0,
        }
    elif command.command_id == 0x07:
        object_id = command.argument & 0xFFFF
        decoded = {
            "status": "decoded_special_files",
            "c_up_elf_message_file": command.parameter,
            "keep_object_id": object_id,
            "keep_object_name": semantic_names["object"].get(
                object_id, f"OBJECT_0x{object_id:04x}"
            ),
        }
    elif command.command_id == 0x0D:
        payload_start = payload_offset if payload_offset is not None else -1
        candidate = path_block_candidate_from_payload(
            data,
            payload_start,
            command.parameter,
        )
        raw_records = decode_path_records_raw(
            data,
            payload_start,
            command.parameter,
            nested_resource_base_offset=ZSI_RESOURCE_BASE_OFFSET,
        )
        invalid_path_count = sum(
            record.get("points_status") == "invalid_points_offset"
            for record in raw_records
        )
        decoded = {
            "status": "decoded_path_block_candidate"
            if candidate
            else "path_block_candidate_missing",
            "expected_path_count": command.parameter,
            "payload_start": payload_start,
            "payload_start_hex": f"0x{payload_start:x}",
            "start_delta": payload_start - command.argument,
            "candidate": candidate,
            "raw_records": raw_records,
            "validation": {
                "status": (
                    "validated_native_resource_base_path_records"
                    if len(raw_records) == command.parameter and invalid_path_count == 0
                    else "native_path_records_invalid"
                ),
                "path_record_count": len(raw_records),
                "payload_start": payload_start,
                "start_delta": ZSI_RESOURCE_BASE_OFFSET,
                "table_end": payload_start + command.parameter * 8,
                "path_record_size": 8,
                "invalid_path_count": invalid_path_count,
                "semantic_mapping": "code_bin_path_record_mapping_confirmed",
            },
        }
    elif command.command_id == 0x0E:
        actors = transition_actors_from_payload(
            data,
            payload_offset if payload_offset is not None else -1,
            command.parameter,
            semantic_names,
        )
        decoded = {
            "status": "decoded_transition_actor_list",
            "layout": "oot3d_transition_actor_entry_0x10",
            "expected_count": command.parameter,
            "entry_count": len(actors),
            "entries": actors if full_entries else actors[:sample_limit],
            "entries_omitted_count": max(0, len(actors) - sample_limit)
            if not full_entries
            else 0,
        }
    elif command.command_id == 0x0F:
        decoded = decode_light_settings_records(
            data,
            payload_offset if payload_offset is not None else -1,
            command.parameter,
            sample_limit=sample_limit,
            full_entries=full_entries,
        )
    elif command.command_id == 0x11:
        decoded = {
            "status": "decoded_skybox_settings",
            "skybox_id": command.argument & 0xFF,
            "weather_or_unk_05": (command.argument >> 8) & 0xFF,
            "indoors": (command.argument >> 16) & 0xFF,
        }
    elif command.command_id == 0x13:
        values, payload_start, exit_payload_end = native_exit_values(
            data, command, setup_commands
        )
        annotated_values = annotate_exit_values(values)
        validation = {
            "status": "validated_native_resource_base_s16_exit_payload_window",
            "entry_count": len(values),
            "payload_start": payload_start,
            "start_delta": ZSI_RESOURCE_BASE_OFFSET,
            "payload_end_hint": exit_payload_end,
            "payload_size": exit_payload_end - payload_start,
            "semantic_mapping": "code_bin_transition_mapping_confirmed",
        }
        decoded = {
            "status": "decoded_exit_list",
            "payload_start": payload_start,
            "payload_start_hex": f"0x{payload_start:x}",
            "values": annotated_values
            if full_entries
            else annotated_values[:sample_limit],
            "validation": validation,
        }
    elif command.command_id == 0x15:
        decoded = {
            "status": "decoded_sound_settings",
            "spec_id": command.parameter,
            "nature_ambience_id": (command.command_word >> 16) & 0xFF,
            "data3": (command.command_word >> 24) & 0xFF,
            "bgm_sound_id": command.argument,
            "bgm_sound_id_hex": f"0x{command.argument:08x}",
        }
    elif command.command_id == 0x17:
        decoded = {
            "status": "decoded_cutscene_reference",
            "cutscene_argument": command.argument,
            "cutscene_offset": payload_offset,
            "cutscene_offset_hex": f"0x{payload_offset:x}",
            "resource_base_offset": ZSI_RESOURCE_BASE_OFFSET,
            "in_file": payload_in_file,
        }
    elif command.command_id == 0x19:
        decoded = {
            "status": "decoded_misc_settings",
            "camera_or_world_map_area": command.parameter,
            "raw_argument": command.argument,
        }
    elif command.command_id == 0x14:
        decoded = {"status": "decoded_end_marker"}

    if decoded is not None:
        record["decoded"] = decoded
    return record


def command_decompilation_evidence(command_id: int) -> dict[str, object]:
    evidence = COMMAND_DECOMPILATION_EVIDENCE.get(command_id)
    if evidence is None:
        return {
            "support_level": "unmapped_command",
            "export_structs": [],
            "native_asset_evidence": [],
            "code_bin_evidence": [],
            "open_questions": [
                f"Map scene command 0x{command_id:02x} to native asset layout and code.bin handler."
            ],
        }
    return {
        key: list(value) if isinstance(value, list) else value
        for key, value in evidence.items()
    }


def decode_path_records_raw(
    data: bytes,
    offset: int,
    count: int,
    *,
    nested_resource_base_offset: int = 0,
) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    if count <= 0 or offset < 0:
        return records
    for index in range(count):
        cursor = offset + index * 8
        if cursor + 8 > len(data):
            break
        raw = data[cursor : cursor + 8]
        point_count = raw[0]
        raw_points_offset = int.from_bytes(raw[4:8], "little")
        points_offset = raw_points_offset + nested_resource_base_offset
        points, points_status = decode_path_points(data, points_offset, point_count)
        records.append(
            {
                "index": index,
                "offset": cursor,
                "offset_hex": f"0x{cursor:x}",
                "raw_hex": raw.hex(),
                "point_count": point_count,
                "unk_01": raw[1],
                "unk_02": int.from_bytes(raw[2:4], "little"),
                "word0": int.from_bytes(raw[0:4], "little"),
                "word0_hex": f"0x{int.from_bytes(raw[0:4], 'little'):08x}",
                "points_argument": raw_points_offset,
                "points_argument_hex": f"0x{raw_points_offset:08x}",
                "points_offset": points_offset,
                "points_offset_hex": f"0x{points_offset:08x}",
                "resource_base_offset": nested_resource_base_offset,
                "points_status": points_status,
                "points": points,
                "consumer_evidence": {
                    "command_handler": "code.bin 0x002985f0 relocates each path record +4 pointer by scene base and stores the path table at play+0x5c20",
                    "general_accessor": "code.bin 0x00348ff0 Path_GetByIndex returns play+0x5c20 + index*8 unless index equals the supplied sentinel",
                    "record_layout": "exported consumers read byte +0 as point count and word +4 as Oot3dVec3s point-list pointer",
                },
            }
        )
    return records


def annotate_exit_values(values: list[dict[str, object]]) -> list[dict[str, object]]:
    annotated: list[dict[str, object]] = []
    for entry in values:
        value = int(entry.get("value", 0))
        raw = value & 0xFFFF
        if value == EXIT_SPECIAL_VALUE_7FFF:
            category = "special_0x7fff"
        elif EXIT_HIGH_REMAP_BASE <= value < EXIT_SPECIAL_VALUE_7FFF:
            category = "high_remap_0x7ff9_to_0x7ffe"
        elif value < EXIT_HIGH_REMAP_BASE:
            category = "direct_transition_value"
        else:
            category = "unhandled_signed_value"
        row = dict(entry)
        row["raw_u16"] = raw
        row["raw_u16_hex"] = f"0x{raw:04x}"
        row["oot3d_exit_category"] = category
        row["consumer_evidence"] = {
            "handler": "code.bin 0x002a9e2c stores the exit table at play+0x5c1c",
            "consumer": "code.bin 0x003365b0 reads s16 exit = *(play+0x5c1c + collisionResult*2 - 2)",
            "direct_gate": "signed exit < 0x7ff9",
            "high_remap": "0x7ff9..0x7ffe use delta table 0x0053a1e7 and entrance table 0x0053c094",
        }
        annotated.append(row)
    return annotated


def decode_path_points(
    data: bytes, offset: int, count: int
) -> tuple[list[dict[str, object]], str]:
    if count <= 0:
        return [], "empty_path"
    required_size = count * 6
    if offset < 0 or offset + required_size > len(data):
        return [], "invalid_points_offset"
    points: list[dict[str, object]] = []
    for index in range(count):
        cursor = offset + index * 6
        points.append(
            {
                "index": index,
                "offset": cursor,
                "offset_hex": f"0x{cursor:x}",
                "x": s16(data, cursor),
                "y": s16(data, cursor + 2),
                "z": s16(data, cursor + 4),
            }
        )
    return points, "decoded_vec3s_points"


def decode_light_settings_records(
    data: bytes,
    offset: int,
    count: int,
    *,
    sample_limit: int,
    full_entries: bool,
) -> dict[str, object]:
    required_size = count * LIGHT_SETTINGS_RECORD_SIZE
    if count <= 0 or offset < 0 or offset + required_size > len(data):
        return {
            "status": "light_settings_list_out_of_file",
            "layout": "oot3d_pica_light_settings_record_0x1c",
            "record_size": LIGHT_SETTINGS_RECORD_SIZE,
            "record_count": count,
            "payload_size": required_size,
        }

    records = []
    record_limit = count if full_entries else min(count, sample_limit)
    previous_raw: bytes | None = None
    for index in range(record_limit):
        cursor = offset + index * LIGHT_SETTINGS_RECORD_SIZE
        raw = data[cursor : cursor + LIGHT_SETTINGS_RECORD_SIZE]
        records.append(decode_light_settings_record(index, cursor, raw, previous_raw))
        previous_raw = raw

    return {
        "status": "decoded_light_settings_list",
        "layout": "oot3d_pica_light_settings_record_0x1c",
        "record_size": LIGHT_SETTINGS_RECORD_SIZE,
        "record_count": count,
        "payload_size": required_size,
        "consumer_evidence": {
            "command_handler": "code.bin 0x00379188 stores command count at play+0x322c and native list pointer at play+0x3230",
            "runtime_consumer": "code.bin 0x0045dd50 consumes current/previous/target/blend state and 0x1c-stride ZSI records",
            "source": "tools/oot3d/decomp_support/analysis/oot3d_pica_native_origin_structures.md",
        },
        "records": records,
        "records_omitted_count": max(0, count - record_limit),
    }


def decode_light_settings_record(
    index: int,
    offset: int,
    raw: bytes,
    previous_raw: bytes | None,
) -> dict[str, object]:
    record: dict[str, object] = {
        "index": index,
        "offset": offset,
        "offset_hex": f"0x{offset:x}",
        "raw_hex": raw.hex(),
        "legacy_env_light_settings_prefix_candidate": {
            "ambient_rgb": rgb(raw, 0x00),
            "diffuse0_dir_s8": s8_triplet(raw, 0x03),
            "diffuse0_rgb": rgb(raw, 0x06),
            "diffuse1_dir_s8": s8_triplet(raw, 0x09),
            "diffuse1_rgb": rgb(raw, 0x0C),
            "fog_rgb": rgb(raw, 0x0F),
            "fog_near": u16(raw, 0x12),
            "fog_far": u16(raw, 0x14),
            "status": "diagnostic_only_not_promoted_runtime_layout",
        },
        "native_env_consumer_0045dd50": {
            "ambient_rgb": rgb(raw, 0x0A),
            "light0_dir_s8": s8_triplet(raw, 0x0D),
            "light0_rgb": rgb(raw, 0x10),
            "light1_dir_s8": s8_triplet(raw, 0x13),
            "light1_rgb": rgb(raw, 0x16),
            "fog_or_environment_rgb": rgb(raw, 0x19),
        },
        "actor_vs_packet_candidate": {
            "ambient_rgb": (
                [previous_raw[0x1A], previous_raw[0x1B], raw[0x00]]
                if previous_raw is not None
                else None
            ),
            "diffuse0_rgb": rgb(raw, 0x04),
            "diffuse1_rgb": rgb(raw, 0x0A),
            "source": "native_zsi_light_settings_actor_vs_packet_candidate_from_existing PICA-origin audit",
        },
    }
    return record


def summarize_entry_candidate(
    candidate: dict[str, object] | None,
    sample_limit: int,
    full_entries: bool,
) -> dict[str, object] | None:
    if candidate is None:
        return None
    result = dict(candidate)
    entries = result.get("entries")
    if isinstance(entries, list) and not full_entries:
        result["entries"] = entries[:sample_limit]
        result["entries_omitted_count"] = max(0, len(entries) - sample_limit)
    return result


def scene_index_markdown(index: dict[str, object]) -> str:
    lines = [
        "# OOT3D Native Scene Index",
        "",
        f"- Format: `{index['format']}`",
        f"- Scene root: `{index['scene_root']}`",
        f"- Scenes: {index['scene_count']}",
        f"- Room bindings: {index['room_binding_count']}",
        f"- Unique room files in index: {index['unique_room_file_count']}",
        f"- Available unique room files: {index['available_unique_room_file_count']}",
        f"- Setups: {index['setup_count']}",
        f"- Unique room setups: {index['unique_room_setup_count']}",
        f"- Scene/room setup mismatches: {index['room_setup_alignment_mismatch_count']}",
        f"- Spawn entries decoded: {index['spawn_entry_total']}",
        f"- Room actor entries decoded: {index['room_actor_entry_total']}",
        f"- Light-setting records decoded: {index['light_settings_record_total']}",
        f"- Parse errors: {index['parse_error_count']}",
    ]
    c_output_files = index.get("c_output_files")
    if isinstance(c_output_files, list):
        lines.append(f"- Generated C support files: {len(c_output_files)}")
    lines.extend(
        [
            "",
            "This index is generated from native OOT3D ZSI payloads. N64 source names are used only as semantic labels when they match already known actor/object ids.",
            "",
            "## Command Coverage",
            "",
        ]
    )
    command_counts = index.get("command_id_counts", [])
    if command_counts:
        for key, count in dict(command_counts).items():
            lines.append(f"- `{key}`: {count}")
    else:
        lines.append("- none")
    lines.extend(["", "## Decompilation Evidence Coverage", ""])
    support_counts = index.get("decompilation_support_level_counts", [])
    if support_counts:
        for key, count in dict(support_counts).items():
            lines.append(f"- `{key}`: {count}")
    else:
        lines.append("- none")
    lines.extend(["", "## Scenes", ""])

    for record in index.get("records", []):
        lines.extend(scene_record_markdown(record))
        lines.append("")

    return "\n".join(lines).rstrip() + "\n"


def scene_record_markdown(record: dict[str, object]) -> list[str]:
    lines = [
        f"### {record['scene_stem']}",
        "",
        f"- Scene ZSI: `{record['scene_path']}`",
        f"- Setups: {record['setup_count']}",
        f"- Rooms: {len(record['rooms'])}",
        f"- Collision candidates: {len(record['collision_header_candidates'])}",
    ]
    for room in record.get("rooms", []):
        actor_payload = room.get("room_actor_list", {})
        selected = (
            actor_payload.get("selected_candidate")
            if isinstance(actor_payload, dict)
            else None
        )
        actor_count = selected.get("entry_count") if isinstance(selected, dict) else 0
        cmbs = room.get("embedded_cmbs", [])
        cmb_text = ", ".join(str(cmb.get("name")) for cmb in cmbs) if cmbs else "none"
        lines.append(
            f"- Room {room.get('room_index')}: `{room.get('room_path')}`, CMB: {cmb_text}, room actors: {actor_count}"
        )
    lines.extend(
        ["", "| Setup | Role | Commands | Decoded highlights |", "|---:|---|---|---|"]
    )
    for setup in record.get("setups", []):
        highlights = setup_highlights(setup)
        lines.append(
            f"| {setup['index']} | {setup['setup_role']} | {' '.join(setup['command_ids'])} | {highlights} |"
        )
    return lines


def setup_highlights(setup: dict[str, object]) -> str:
    parts: list[str] = []
    for command in setup.get("commands", []):
        decoded = command.get("decoded")
        if not isinstance(decoded, dict):
            continue
        command_id = int(command.get("command_id", -1))
        if command_id == 0x00:
            selected = decoded.get("selected_candidate")
            if isinstance(selected, dict):
                parts.append(f"spawn={selected.get('entry_count', 0)}")
        elif command_id == 0x04:
            parts.append(f"rooms={len(decoded.get('room_references', []))}")
        elif command_id == 0x06:
            selected = decoded.get("selected_candidate")
            if isinstance(selected, dict):
                parts.append(f"entrances={selected.get('entry_count', 0)}")
        elif command_id == 0x0E:
            parts.append(f"transitions={decoded.get('entry_count', 0)}")
        elif command_id == 0x0F:
            parts.append(f"lights={decoded.get('record_count', 0)}")
        elif command_id == 0x11:
            parts.append(f"skybox={decoded.get('skybox_id')}")
    return ", ".join(parts) if parts else "-"


def write_scene_index_c_sources(
    index: dict[str, object], output_dir: Path
) -> list[Path]:
    output_dir.mkdir(parents=True, exist_ok=True)
    written: list[Path] = []
    for record in index.get("records", []):
        if not isinstance(record, dict):
            continue
        path = output_dir / f"{scene_record_source_basename(record)}.c"
        path.write_text(scene_record_c_source(record), encoding="utf-8", newline="\n")
        written.append(path)
    registry_header = output_dir / "scene_index_registry.h"
    registry_source = output_dir / "scene_index_registry.c"
    registry_header.write_text(
        scene_index_registry_header(index), encoding="utf-8", newline="\n"
    )
    registry_source.write_text(
        scene_index_registry_source(index), encoding="utf-8", newline="\n"
    )
    written.extend([registry_header, registry_source])
    return written


def scene_record_c_source(record: dict[str, object]) -> str:
    base = c_identifier(Path(str(record["scene_path"])).stem)
    public_symbol = scene_record_public_symbol(record)
    lines: list[str] = [
        "/*",
        " * Generated from native OOT3D ZSI scene data.",
        f" * Source scene: {record['scene_path']}",
        " * These tables are decompilation support data, not N64 asset substitutions.",
        " */",
        '#include "oot3d/scene.h"',
        '#include "oot3d/actor_object_semantics.h"',
        "",
    ]

    setup_initializers: list[str] = []
    for setup in record.get("setups", []):
        if not isinstance(setup, dict):
            continue
        setup_index = int(setup["index"])
        prefix = f"oot3d_{base}_setup_{setup_index}"
        command_symbol, command_count = append_array(
            lines,
            "Oot3dSceneCommand",
            f"{prefix}_commands",
            [
                command
                for command in setup.get("commands", [])
                if isinstance(command, dict)
            ],
            scene_command_initializer,
        )
        special_symbol, special_count = append_array(
            lines,
            "Oot3dSpecialFiles",
            f"{prefix}_special_files",
            setup_decoded_entries(setup, 0x07),
            special_files_initializer,
        )
        path_symbol, path_count = append_path_record_arrays(
            lines,
            f"{prefix}_paths",
            setup_path_records(setup),
        )
        standard_actor_symbol, standard_actor_count = append_array(
            lines,
            "Oot3dActorEntry",
            f"{prefix}_standard_actors",
            setup_actor_entries(setup, 0x01),
            actor_entry_initializer,
        )
        spawn_symbol, spawn_count = append_array(
            lines,
            "Oot3dActorEntry",
            f"{prefix}_spawns",
            setup_actor_entries(setup, 0x00),
            actor_entry_initializer,
        )
        entrance_symbol, entrance_count = append_array(
            lines,
            "Oot3dEntranceEntry",
            f"{prefix}_entrances",
            setup_entrance_entries(setup),
            entrance_entry_initializer,
        )
        transition_symbol, transition_count = append_array(
            lines,
            "Oot3dTransitionActorEntry",
            f"{prefix}_transition_actors",
            setup_transition_entries(setup),
            transition_actor_entry_initializer,
        )
        light_symbol, light_count = append_array(
            lines,
            "Oot3dPicaLightSettingsRecord",
            f"{prefix}_light_settings",
            setup_light_records(setup),
            light_settings_initializer,
        )
        exit_symbol, exit_count = append_array(
            lines,
            "Oot3dExitEntry",
            f"{prefix}_exits",
            setup_exit_entries(setup),
            exit_entry_initializer,
        )
        skybox_symbol, skybox_count = append_array(
            lines,
            "Oot3dSkyboxSettings",
            f"{prefix}_skybox_settings",
            setup_decoded_entries(setup, 0x11),
            skybox_settings_initializer,
        )
        sound_symbol, sound_count = append_array(
            lines,
            "Oot3dSoundSettings",
            f"{prefix}_sound_settings",
            setup_decoded_entries(setup, 0x15),
            sound_settings_initializer,
        )
        cutscene_symbol, cutscene_count = append_array(
            lines,
            "Oot3dCutsceneReference",
            f"{prefix}_cutscenes",
            setup_decoded_entries(setup, 0x17),
            cutscene_reference_initializer,
        )
        misc_symbol, misc_count = append_array(
            lines,
            "Oot3dMiscSettings",
            f"{prefix}_misc_settings",
            setup_decoded_entries(setup, 0x19),
            misc_settings_initializer,
        )
        setup_initializers.append(
            "    { "
            f"{setup_index}u, {command_symbol}, {command_count}u, "
            f"{special_symbol}, {special_count}u, "
            f"{path_symbol}, {path_count}u, "
            f"{standard_actor_symbol}, {standard_actor_count}u, "
            f"{spawn_symbol}, {spawn_count}u, "
            f"{entrance_symbol}, {entrance_count}u, "
            f"{transition_symbol}, {transition_count}u, "
            f"{light_symbol}, {light_count}u, "
            f"{exit_symbol}, {exit_count}u, "
            f"{skybox_symbol}, {skybox_count}u, "
            f"{sound_symbol}, {sound_count}u, "
            f"{cutscene_symbol}, {cutscene_count}u, "
            f"{misc_symbol}, {misc_count}u "
            "},"
        )

    room_initializers: list[str] = []
    used_room_prefixes: set[str] = set()
    for room_ordinal, room in enumerate(record.get("rooms", [])):
        if not isinstance(room, dict):
            continue
        room_index = (
            int(room["room_index"]) if isinstance(room.get("room_index"), int) else -1
        )
        room_path = str(room.get("room_path", ""))
        room_file_base = (
            c_identifier(Path(room_path).stem) if room_path else f"room_{room_ordinal}"
        )
        room_prefix = f"oot3d_{base}_{room_file_base}"
        if room_prefix in used_room_prefixes:
            room_prefix = f"{room_prefix}_{room_ordinal}"
        used_room_prefixes.add(room_prefix)
        actor_symbol, actor_count = append_array(
            lines,
            "Oot3dActorEntry",
            f"{room_prefix}_actors",
            room_actor_entries(room),
            actor_entry_initializer,
        )
        object_symbol, object_count = append_array(
            lines,
            "Oot3dRoomObjectEntry",
            f"{room_prefix}_objects",
            room_object_entries(room),
            room_object_entry_initializer,
        )
        room_initializers.append(
            "    { "
            f"{c_string(str(room.get('room_path', '')))}, {room_index}, "
            f"{object_symbol}, {object_count}u, "
            f"{actor_symbol}, {actor_count}u "
            "},"
        )

    room_ref_symbol, room_ref_count = append_array(
        lines,
        "Oot3dRoomReference",
        f"oot3d_{base}_room_refs",
        scene_room_references(record),
        room_reference_initializer,
    )

    setup_symbol = f"oot3d_{base}_setups"
    append_manual_array(lines, "Oot3dSceneSetupIndex", setup_symbol, setup_initializers)
    room_symbol = f"oot3d_{base}_rooms"
    append_manual_array(lines, "Oot3dRoomIndex", room_symbol, room_initializers)

    lines.extend(
        [
            f"const Oot3dSceneIndex {public_symbol} = {{",
            f"    {c_string(str(record['scene_path']))},",
            f"    {room_ref_symbol}, {room_ref_count}u,",
            f"    {room_symbol if room_initializers else 'NULL'}, {len(room_initializers)}u,",
            f"    {setup_symbol if setup_initializers else 'NULL'}, {len(setup_initializers)}u,",
            "};",
            "",
        ]
    )
    return "\n".join(lines)


def scene_index_registry_header(index: dict[str, object]) -> str:
    lines = [
        "/*",
        " * Generated from native OOT3D ZSI scene data.",
        " * Aggregates source-like scene index tables for decompilation support.",
        " */",
        "#ifndef OOT3D_GENERATED_SCENE_INDEX_REGISTRY_H",
        "#define OOT3D_GENERATED_SCENE_INDEX_REGISTRY_H",
        "",
        '#include "oot3d/scene.h"',
        "",
    ]
    for record in scene_index_records(index):
        lines.append(
            f"extern const Oot3dSceneIndex {scene_record_public_symbol(record)};"
        )
    lines.extend(
        [
            "",
            "extern const Oot3dSceneIndexRegistryEntry oot3d_scene_index_registry[];",
            "extern const u32 oot3d_scene_index_registry_count;",
            "",
            "#endif",
            "",
        ]
    )
    return "\n".join(lines)


def scene_index_registry_source(index: dict[str, object]) -> str:
    records = scene_index_records(index)
    lines = [
        "/*",
        " * Generated from native OOT3D ZSI scene data.",
        " * Registry entries are keyed by native scene stem/path, not by N64 scene ids.",
        " */",
        '#include "scene_index_registry.h"',
        "",
        "const Oot3dSceneIndexRegistryEntry oot3d_scene_index_registry[] = {",
    ]
    for record in records:
        lines.append(
            "    { "
            f"{c_string(str(record.get('scene_stem', '')))}, "
            f"{c_string(str(record.get('scene_path', '')))}, "
            f"{c_string(scene_record_source_basename(record))}, "
            f"&{scene_record_public_symbol(record)} "
            "},"
        )
    lines.extend(
        [
            "};",
            "",
            f"const u32 oot3d_scene_index_registry_count = {len(records)}u;",
            "",
        ]
    )
    return "\n".join(lines)


def scene_index_records(index: dict[str, object]) -> list[dict[str, object]]:
    return [record for record in index.get("records", []) if isinstance(record, dict)]


def append_array(
    lines: list[str],
    c_type: str,
    symbol: str,
    entries: list[dict[str, object]],
    initializer,
) -> tuple[str, int]:
    if not entries:
        return "NULL", 0
    lines.append(f"static const {c_type} {symbol}[] = {{")
    for entry in entries:
        lines.append(f"    {initializer(entry)},")
    lines.extend(["};", ""])
    return symbol, len(entries)


def append_manual_array(
    lines: list[str], c_type: str, symbol: str, initializers: list[str]
) -> None:
    if not initializers:
        return
    lines.append(f"static const {c_type} {symbol}[] = {{")
    lines.extend(initializers)
    lines.extend(["};", ""])


def append_path_record_arrays(
    lines: list[str],
    symbol: str,
    entries: list[dict[str, object]],
) -> tuple[str, int]:
    if not entries:
        return "NULL", 0

    initializers: list[str] = []
    for entry in entries:
        points = [point for point in entry.get("points", []) if isinstance(point, dict)]
        point_symbol = "NULL"
        if points:
            point_symbol, _ = append_array(
                lines,
                "Oot3dVec3s",
                f"{symbol}_{int(entry.get('index', len(initializers)))}_points",
                points,
                path_point_initializer,
            )
        initializers.append(f"    {path_record_initializer(entry, point_symbol)},")

    append_manual_array(lines, "Oot3dPathRecord", symbol, initializers)
    return symbol, len(entries)


def setup_decoded_entries(
    setup: dict[str, object], command_id: int
) -> list[dict[str, object]]:
    entries: list[dict[str, object]] = []
    for command in setup.get("commands", []):
        if (
            not isinstance(command, dict)
            or int(command.get("command_id", -1)) != command_id
        ):
            continue
        decoded = command.get("decoded")
        if isinstance(decoded, dict):
            entries.append(decoded)
    return entries


def setup_actor_entries(
    setup: dict[str, object], command_id: int
) -> list[dict[str, object]]:
    for command in setup.get("commands", []):
        if (
            not isinstance(command, dict)
            or int(command.get("command_id", -1)) != command_id
        ):
            continue
        decoded = command.get("decoded")
        if not isinstance(decoded, dict):
            continue
        selected = decoded.get("selected_candidate")
        if not isinstance(selected, dict):
            continue
        return [
            entry for entry in selected.get("entries", []) if isinstance(entry, dict)
        ]
    return []


def setup_path_records(setup: dict[str, object]) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    for command in setup.get("commands", []):
        if not isinstance(command, dict) or int(command.get("command_id", -1)) != 0x0D:
            continue
        decoded = command.get("decoded")
        if isinstance(decoded, dict):
            records.extend(
                entry
                for entry in decoded.get("raw_records", [])
                if isinstance(entry, dict)
            )
    return records


def setup_entrance_entries(setup: dict[str, object]) -> list[dict[str, object]]:
    for command in setup.get("commands", []):
        if not isinstance(command, dict) or int(command.get("command_id", -1)) != 0x06:
            continue
        decoded = command.get("decoded")
        if not isinstance(decoded, dict):
            continue
        selected = decoded.get("selected_candidate")
        if not isinstance(selected, dict):
            continue
        return [
            entry for entry in selected.get("entries", []) if isinstance(entry, dict)
        ]
    return []


def setup_transition_entries(setup: dict[str, object]) -> list[dict[str, object]]:
    for command in setup.get("commands", []):
        if not isinstance(command, dict) or int(command.get("command_id", -1)) != 0x0E:
            continue
        decoded = command.get("decoded")
        if isinstance(decoded, dict):
            return [
                entry for entry in decoded.get("entries", []) if isinstance(entry, dict)
            ]
    return []


def setup_light_records(setup: dict[str, object]) -> list[dict[str, object]]:
    for command in setup.get("commands", []):
        if not isinstance(command, dict) or int(command.get("command_id", -1)) != 0x0F:
            continue
        decoded = command.get("decoded")
        if isinstance(decoded, dict):
            return [
                entry for entry in decoded.get("records", []) if isinstance(entry, dict)
            ]
    return []


def setup_exit_entries(setup: dict[str, object]) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    for command in setup.get("commands", []):
        if not isinstance(command, dict) or int(command.get("command_id", -1)) != 0x13:
            continue
        decoded = command.get("decoded")
        if isinstance(decoded, dict):
            records.extend(
                entry for entry in decoded.get("values", []) if isinstance(entry, dict)
            )
    return records


def room_actor_entries(room: dict[str, object]) -> list[dict[str, object]]:
    payload = room.get("room_actor_list")
    if not isinstance(payload, dict):
        return []
    selected = payload.get("selected_candidate")
    if not isinstance(selected, dict):
        return []
    return [entry for entry in selected.get("entries", []) if isinstance(entry, dict)]


def room_object_entries(room: dict[str, object]) -> list[dict[str, object]]:
    payload = room.get("room_actor_list")
    if not isinstance(payload, dict):
        return []
    selected = payload.get("selected_candidate")
    if not isinstance(selected, dict):
        return []
    prefix = selected.get("object_prefix")
    if not isinstance(prefix, dict):
        return []
    entries: list[dict[str, object]] = []
    for key in ("object_ids", "unknown_object_ids"):
        for entry in prefix.get(key, []):
            if isinstance(entry, dict):
                entries.append(entry)
    return entries


def scene_room_references(record: dict[str, object]) -> list[dict[str, object]]:
    for setup in record.get("setups", []):
        if not isinstance(setup, dict):
            continue
        for command in setup.get("commands", []):
            if (
                not isinstance(command, dict)
                or int(command.get("command_id", -1)) != 0x04
            ):
                continue
            decoded = command.get("decoded")
            if not isinstance(decoded, dict):
                continue
            refs = [
                ref
                for ref in decoded.get("room_references", [])
                if isinstance(ref, dict)
            ]
            if refs:
                return refs
    return [
        {
            "path": room.get("room_path", ""),
            "room_index": room.get("room_index", -1),
        }
        for room in record.get("rooms", [])
        if isinstance(room, dict)
    ]


def scene_command_initializer(command: dict[str, object]) -> str:
    return (
        "{ "
        f"0x{int(command['command_word']) & 0xFFFFFFFF:08X}u, "
        f"0x{int(command['argument']) & 0xFFFFFFFF:08X}u "
        "}"
    )


def special_files_initializer(entry: dict[str, object]) -> str:
    return (
        "{ "
        f"{u8_initializer(entry.get('c_up_elf_message_file', 0))}, "
        f"{semantic_s16_initializer(entry, 'keep_object_id', 'keep_object_name', 'OBJECT_')} "
        "}"
    )


def raw8_initializer(entry: dict[str, object]) -> str:
    return raw_hex_initializer(str(entry.get("raw_hex", "")), 8)


def path_point_initializer(entry: dict[str, object]) -> str:
    return (
        "{ "
        f"{s16_initializer(entry.get('x', 0))}, "
        f"{s16_initializer(entry.get('y', 0))}, "
        f"{s16_initializer(entry.get('z', 0))} "
        "}"
    )


def path_record_initializer(entry: dict[str, object], point_symbol: str) -> str:
    return (
        "{ "
        f"{u8_initializer(entry.get('point_count', 0))}, "
        f"{u8_initializer(entry.get('unk_01', 0))}, "
        f"{u16_initializer(entry.get('unk_02', 0))}, "
        f"{u32_initializer(entry.get('points_offset', 0))}u, "
        f"{path_points_status_c_name(str(entry.get('points_status', 'invalid_points_offset')))}, "
        f"{point_symbol} "
        "}"
    )


def path_points_status_c_name(status: str) -> str:
    if status == "decoded_vec3s_points":
        return "OOT3D_PATH_POINTS_DECODED_VEC3S"
    if status == "empty_path":
        return "OOT3D_PATH_POINTS_EMPTY"
    return "OOT3D_PATH_POINTS_INVALID_OFFSET"


def actor_entry_initializer(entry: dict[str, object]) -> str:
    return (
        "{ "
        f"{semantic_s16_initializer(entry, 'actor_id', 'actor_name', 'ACTOR_')}, "
        f"{s16_vec_initializer(entry.get('pos', [0, 0, 0]))}, "
        f"{s16_vec_initializer(entry.get('rot', [0, 0, 0]))}, "
        f"{s16_initializer(entry.get('params', 0))} "
        "}"
    )


def room_object_entry_initializer(entry: dict[str, object]) -> str:
    status = ROOM_OBJECT_STATUS_C_NAMES.get(
        str(entry.get("semantic_status", "unknown_oot3d_object_id")),
        "OOT3D_ROOM_OBJECT_UNKNOWN_OOT3D_ID",
    )
    return (
        "{ "
        f"{semantic_s16_initializer(entry, 'object_id', 'object_name', 'OBJECT_')}, "
        f"{status} "
        "}"
    )


def entrance_entry_initializer(entry: dict[str, object]) -> str:
    return (
        "{ "
        f"{int(entry.get('spawn', 0)) & 0xFF}u, {s8_initializer(entry.get('room', 0))} "
        "}"
    )


def transition_actor_entry_initializer(entry: dict[str, object]) -> str:
    return (
        "{ "
        f"{{ {s8_initializer(entry.get('front_room', 0))}, {s8_initializer(entry.get('front_effect', 0))} }}, "
        f"{{ {s8_initializer(entry.get('back_room', 0))}, {s8_initializer(entry.get('back_effect', 0))} }}, "
        f"{semantic_s16_initializer(entry, 'actor_id', 'actor_name', 'ACTOR_')}, "
        f"{s16_vec_initializer(entry.get('pos', [0, 0, 0]))}, "
        f"{s16_initializer(entry.get('rot_y', 0))}, "
        f"{s16_initializer(entry.get('params', 0))} "
        "}"
    )


def light_settings_initializer(record: dict[str, object]) -> str:
    raw_hex = str(record.get("raw_hex", ""))
    return raw_hex_initializer(raw_hex, LIGHT_SETTINGS_RECORD_SIZE)


def exit_entry_initializer(entry: dict[str, object]) -> str:
    category = EXIT_CATEGORY_C_NAMES.get(
        str(entry.get("oot3d_exit_category", "unhandled_signed_value")),
        "OOT3D_EXIT_UNHANDLED_SIGNED_VALUE",
    )
    return (
        "{ "
        f"{s16_initializer(entry.get('value', 0))}, "
        f"{u16_initializer(entry.get('raw_u16', int(entry.get('value', 0)) & 0xFFFF))}, "
        f"{category} "
        "}"
    )


def skybox_settings_initializer(entry: dict[str, object]) -> str:
    return (
        "{ "
        f"{u8_initializer(entry.get('skybox_id', 0))}, "
        f"{u8_initializer(entry.get('weather_or_unk_05', 0))}, "
        f"{u8_initializer(entry.get('indoors', 0))} "
        "}"
    )


def sound_settings_initializer(entry: dict[str, object]) -> str:
    return (
        "{ "
        f"{u8_initializer(entry.get('spec_id', 0))}, "
        f"{u8_initializer(entry.get('nature_ambience_id', 0))}, "
        f"{u8_initializer(entry.get('data3', 0))}, "
        f"0x{u32_initializer(entry.get('bgm_sound_id', 0)):08X}u "
        "}"
    )


def cutscene_reference_initializer(entry: dict[str, object]) -> str:
    return (
        "{ "
        f"0x{u32_initializer(entry.get('cutscene_offset', 0)):08X}u, "
        f"{1 if entry.get('in_file') else 0}u "
        "}"
    )


def misc_settings_initializer(entry: dict[str, object]) -> str:
    return (
        "{ "
        f"{u8_initializer(entry.get('camera_or_world_map_area', 0))}, "
        f"0x{u32_initializer(entry.get('raw_argument', 0)):08X}u "
        "}"
    )


def raw_hex_initializer(raw_hex: str, expected_size: int) -> str:
    if len(raw_hex) != expected_size * 2:
        raise ParseError(f"expected {expected_size} raw bytes, got {len(raw_hex) // 2}")
    raw_values = [
        f"0x{raw_hex[index : index + 2].upper()}" for index in range(0, len(raw_hex), 2)
    ]
    return "{ { " + ", ".join(raw_values) + " } }"


def room_reference_initializer(ref: dict[str, object]) -> str:
    path = (
        ref.get("matched_local_room_path")
        or ref.get("path")
        or ref.get("room_file")
        or ""
    )
    room_index = ref.get("room_index")
    if not isinstance(room_index, int):
        room_index = -1
    return f"{{ {c_string(str(path))}, {room_index} }}"


def s16_vec_initializer(value: object) -> str:
    values = value if isinstance(value, list) else [0, 0, 0]
    ints = [
        s16_initializer(values[index] if index < len(values) else 0)
        for index in range(3)
    ]
    return "{ " + ", ".join(ints) + " }"


def s16_initializer(value: object) -> str:
    signed_value = int(value)
    if not -0x8000 <= signed_value <= 0x7FFF:
        raise ParseError(f"expected signed 16-bit value, got {signed_value}")
    return str(signed_value)


def semantic_s16_initializer(
    entry: dict[str, object],
    value_key: str,
    symbol_key: str,
    expected_prefix: str,
) -> str:
    value = entry.get(value_key, 0)
    symbol = str(entry.get(symbol_key, ""))
    fallback_prefix = f"{expected_prefix}0x"
    if (
        symbol.startswith(expected_prefix)
        and not symbol.startswith(fallback_prefix)
        and re.fullmatch(r"[A-Za-z_][0-9A-Za-z_]*", symbol)
    ):
        return symbol
    return s16_initializer(value)


def s8_initializer(value: object) -> str:
    signed_value = int(value)
    if not -0x80 <= signed_value <= 0x7F:
        raise ParseError(f"expected signed 8-bit value, got {signed_value}")
    return str(signed_value)


def u8_initializer(value: object) -> str:
    unsigned_value = int(value)
    if not 0 <= unsigned_value <= 0xFF:
        raise ParseError(f"expected unsigned 8-bit value, got {unsigned_value}")
    return f"{unsigned_value}u"


def u16_initializer(value: object) -> str:
    unsigned_value = int(value)
    if not 0 <= unsigned_value <= 0xFFFF:
        raise ParseError(f"expected unsigned 16-bit value, got {unsigned_value}")
    return f"{unsigned_value}u"


def u32_initializer(value: object) -> int:
    unsigned_value = int(value)
    if not 0 <= unsigned_value <= 0xFFFFFFFF:
        raise ParseError(f"expected unsigned 32-bit value, got {unsigned_value}")
    return unsigned_value


def c_string(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def scene_record_source_basename(record: dict[str, object]) -> str:
    return c_identifier(Path(str(record["scene_path"])).stem)


def scene_record_public_symbol(record: dict[str, object]) -> str:
    return f"oot3d_scene_index_{scene_record_source_basename(record)}"


def c_identifier(value: str) -> str:
    identifier = re.sub(r"[^0-9A-Za-z_]+", "_", value).strip("_").lower()
    if not identifier:
        return "unnamed"
    if identifier[0].isdigit():
        return f"_{identifier}"
    return identifier


def u32_table(data: bytes, offset: int, count: int) -> list[dict[str, object]]:
    records = []
    for index in range(count):
        cursor = offset + index * 4
        if cursor + 4 > len(data):
            break
        value = int.from_bytes(data[cursor : cursor + 4], "little")
        records.append(
            {
                "index": index,
                "offset": cursor,
                "offset_hex": f"0x{cursor:x}",
                "value": value,
                "value_hex": f"0x{value:08x}",
                "target_in_file": 0 <= value < len(data),
            }
        )
    return records


def room_index_from_name(name: str) -> int | None:
    match = ROOM_INFO_RE.match(name)
    return int(match.group("room_index")) if match else None


def room_sort_key(path: Path) -> tuple[int, str]:
    room_index = room_index_from_name(path.name)
    return (room_index if room_index is not None else 0xFFFF, path.name.lower())


def resolve_actor_object_semantics(path: Path | None) -> Path | None:
    if path is not None:
        return path
    candidates = [
        Path.cwd()
        / "tools/oot3d/decomp_support/include/oot3d/actor_object_semantics.h",
        Path(__file__).resolve().parents[3]
        / "decomp_support/include/oot3d/actor_object_semantics.h",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


def rgb(raw: bytes, offset: int) -> list[int]:
    return [raw[offset], raw[offset + 1], raw[offset + 2]]


def s8_triplet(raw: bytes, offset: int) -> list[int]:
    return [
        int.from_bytes(raw[offset + index : offset + index + 1], "little", signed=True)
        for index in range(3)
    ]


def s16(raw: bytes, offset: int) -> int:
    return int.from_bytes(raw[offset : offset + 2], "little", signed=True)


def u16(raw: bytes, offset: int) -> int:
    return int.from_bytes(raw[offset : offset + 2], "little")
