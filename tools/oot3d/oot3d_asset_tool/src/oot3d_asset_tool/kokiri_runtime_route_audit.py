from __future__ import annotations

import json
import re
from collections import Counter
from pathlib import Path

from .audio_asset_audit import audit_audio_assets
from .binary import ParseError
from .kankyo_environment_audit import audit_kankyo_environment_assets
from .romfs_inventory import sorted_counter
from .zsi import ZsiFile, ZsiSceneCommand
from .zsi_scene_audit import zsi_scene_role, zsi_scene_stem

DEFAULT_ROUTE_STEMS = ("link", "spot04")
ROOM_REF_RE = re.compile(rb"rom:/scene/([A-Za-z0-9_]+\.zsi)")
ROOM_INFO_RE = re.compile(r"^(?P<stem>[A-Za-z0-9_]+)_(?P<room_index>\d+)_info\.zsi$")
AUDIT_FORMAT = "oot3d_kokiri_runtime_route_audit_v4"

COMMAND_NAMES = {
    0x00: "spawn_list",
    0x01: "actor_list",
    0x03: "collision_header",
    0x04: "room_list",
    0x05: "wind_settings",
    0x06: "entrance_list",
    0x07: "special_files",
    0x08: "room_behavior",
    0x0A: "mesh_header",
    0x0B: "object_list",
    0x0C: "light_list",
    0x0D: "path_list",
    0x0E: "transition_actor_list",
    0x0F: "light_settings_list",
    0x10: "time_settings",
    0x11: "skybox_settings",
    0x12: "skybox_disables",
    0x13: "exit_list",
    0x14: "end",
    0x15: "sound_settings",
    0x16: "echo_settings",
    0x17: "cutscene_data",
    0x18: "alternate_header_list",
    0x19: "misc_settings",
}

DIRECT_FILE_OFFSET_COMMANDS = {
    0x00,
    0x01,
    0x03,
    0x04,
    0x06,
    0x0A,
    0x0B,
    0x0C,
    0x0D,
    0x0E,
    0x0F,
    0x13,
    0x17,
    0x18,
}

OOT3D_BLOCK_LAYOUT_VALIDATION_GAPS = {
    0x00: "needs_oot3d_spawn_list_layout_validation",
    0x06: "needs_oot3d_entrance_list_layout_validation",
    0x0D: "needs_oot3d_path_list_layout_validation",
    0x13: "needs_oot3d_exit_list_layout_validation",
}
ACTOR_ENTRY_SIZE = 0x10
TRANSITION_ACTOR_ENTRY_SIZE = 0x10
EXIT_VALUE_SAMPLE_LIMIT = 16
ROOM_ACTOR_TRAILING_SCAN_WINDOW = 0x4000
ROOM_ACTOR_MAX_ENTRY_COUNT = 256
ROOM_ACTOR_MIN_ENTRY_COUNT = 2
ROOM_ACTOR_MIN_OBJECT_PREFIX_COUNT = 3
ROOM_ACTOR_POSITION_LIMIT = 12000
ROOM_ACTOR_OBJECT_PREFIX_SCAN_LIMIT = 40
ROOM_ACTOR_UNKNOWN_OBJECT_TAIL_LIMIT = 2
ROOM_ACTOR_ZERO_PADDING_TAIL_LIMIT = 2
OOT3D_POSSIBLE_OBJECT_ID_MAX = 0x03FF
SKYBOX_ID_NAMES = {
    0x00: "SKYBOX_NONE",
    0x01: "SKYBOX_NORMAL_SKY",
    0x02: "SKYBOX_BAZAAR",
    0x03: "SKYBOX_OVERCAST_SUNSET",
    0x04: "SKYBOX_MARKET_ADULT",
    0x05: "SKYBOX_CUTSCENE_MAP",
    0x07: "SKYBOX_HOUSE_LINK",
    0x09: "SKYBOX_MARKET_CHILD_DAY",
    0x0A: "SKYBOX_MARKET_CHILD_NIGHT",
    0x0B: "SKYBOX_HAPPY_MASK_SHOP",
    0x0C: "SKYBOX_HOUSE_KNOW_IT_ALL_BROTHERS",
    0x0E: "SKYBOX_HOUSE_OF_TWINS",
    0x0F: "SKYBOX_STABLES",
    0x10: "SKYBOX_HOUSE_KAKARIKO",
    0x11: "SKYBOX_KOKIRI_SHOP",
    0x13: "SKYBOX_GORON_SHOP",
    0x14: "SKYBOX_ZORA_SHOP",
    0x16: "SKYBOX_POTION_SHOP_KAKARIKO",
    0x17: "SKYBOX_POTION_SHOP_MARKET",
    0x18: "SKYBOX_BOMBCHU_SHOP",
    0x1A: "SKYBOX_HOUSE_RICHARD",
    0x1B: "SKYBOX_HOUSE_IMPA",
    0x1C: "SKYBOX_TENT",
    0x1D: "SKYBOX_UNSET_1D",
    0x20: "SKYBOX_HOUSE_MIDO",
    0x21: "SKYBOX_HOUSE_SARIA",
    0x22: "SKYBOX_HOUSE_ALLEY",
    0x27: "SKYBOX_UNSET_27",
}
NORMAL_SKY_KANKYO_ARCHIVE = "BlueSky.zar"
ROUTE_ACTOR_OBJECT_REQUIREMENTS = {
    "ACTOR_EN_HOLL": {
        "required_object_name": "OBJECT_GAMEPLAY_KEEP",
        "source": "N64 En_Holl_InitVars.objectId",
        "reference": "soh/src/overlays/actors/ovl_En_Holl/z_en_holl.c",
    },
}
ROUTE_AUDIO_MAPPING_EVIDENCE = [
    "OOT3D scene-command handler 0x00273108 stores command[4..7] as one BCSAR sound id and command[2] as the nature ambience id.",
    "NativeAudioService::ApplySceneAudio consumes the sound spec, nature profile, and full BCSAR sound id without N64 sequence remapping.",
]


def audit_kokiri_runtime_route(
    romfs_root: Path,
    output_path: Path | None = None,
    *,
    route_stems: tuple[str, ...] = DEFAULT_ROUTE_STEMS,
    actor_object_semantics: Path | None = None,
    sample_limit: int = 100,
    include_records: bool = True,
) -> dict[str, object]:
    if not romfs_root.is_dir():
        raise ParseError(f"{romfs_root}: expected an extracted OOT3D RomFS directory")
    scene_root = romfs_root / "scene"
    if not scene_root.is_dir():
        raise ParseError(f"{scene_root}: expected an extracted OOT3D scene directory")

    semantic_names = load_semantic_names(actor_object_semantics)
    scene_files, missing_files = route_zsi_files(scene_root, route_stems)
    room_indices_by_stem = route_room_indices(scene_files)

    records: list[dict[str, object]] = []
    parse_errors: list[dict[str, object]] = []
    room_references: list[str] = []
    sound_settings: list[dict[str, object]] = []
    skybox_settings: list[dict[str, object]] = []
    misc_settings: list[dict[str, object]] = []
    cutscene_references: list[dict[str, object]] = []
    special_file_records: list[dict[str, object]] = []
    transition_actor_records: list[dict[str, object]] = []

    role_counts: Counter[str] = Counter()
    stem_counts: Counter[str] = Counter()
    command_id_counts: Counter[str] = Counter()
    decoded_status_counts: Counter[str] = Counter()
    runtime_gap_counts: Counter[str] = Counter()
    layout_validation_counts: Counter[str] = Counter()
    spawn_payload_profile_counts: Counter[str] = Counter()
    support_status_counts: Counter[str] = Counter()
    room_actor_payload_status_counts: Counter[str] = Counter()
    selected_room_actor_ids: Counter[str] = Counter()
    selected_room_actor_object_ids: Counter[str] = Counter()

    setup_total = 0
    command_total = 0
    embedded_cmb_total = 0
    collision_candidate_total = 0
    collision_vertex_total = 0
    collision_effective_polygon_total = 0
    collision_surface_type_total = 0
    bgcam_total = 0
    water_box_total = 0
    standard_actor_list_command_count = 0
    object_list_command_count = 0
    spawn_list_command_count = 0
    spawn_candidate_command_count = 0
    spawn_entry_candidate_total = 0
    spawn_player_candidate_total = 0
    entrance_entry_candidate_total = 0
    path_block_candidate_count = 0
    path_file_offset_candidate_total = 0
    exit_value_candidate_total = 0
    transition_actor_command_count = 0
    light_setting_total = 0
    room_actor_list_candidate_count = 0
    selected_room_actor_list_count = 0
    selected_room_actor_entry_total = 0

    for zsi_path in scene_files:
        rel = zsi_path.relative_to(scene_root).as_posix()
        role = zsi_scene_role(zsi_path)
        stem = zsi_scene_stem(zsi_path)
        role_counts[role] += 1
        stem_counts[stem] += 1

        try:
            zsi = ZsiFile.from_path(zsi_path)
            setups = zsi.scene_setups()
            embedded_cmbs = zsi.embedded_cmbs()
            collision_candidates = zsi.collision_header_candidates()
        except Exception as exc:
            parse_errors.append({"path": rel, "stage": "zsi_route_parse", "error": str(exc)})
            continue

        setup_total += len(setups)
        embedded_cmb_total += len(embedded_cmbs)
        collision_candidate_total += len(collision_candidates)
        for candidate in collision_candidates:
            collision_vertex_total += candidate.vertex_count
            collision_effective_polygon_total += candidate.effective_polygon_count
            collision_surface_type_total += candidate.surface_type_count
            bgcam_total += candidate.bgcam_count
            water_box_total += candidate.water_box_count

        room_actor_payload: dict[str, object] | None = None
        if role == "room":
            room_actor_payload = room_actor_list_candidates_from_room_payload(
                zsi.data,
                semantic_names,
                sample_limit=sample_limit,
            )
            room_actor_list_candidate_count += int(room_actor_payload["candidate_count"])
            room_actor_payload_status_counts[str(room_actor_payload["status"])] += 1
            selected_room_actor = room_actor_payload.get("selected_candidate")
            if isinstance(selected_room_actor, dict):
                selected_room_actor_list_count += 1
                selected_room_actor_entry_total += int(selected_room_actor["entry_count"])
                selected_room_actor_ids.update(
                    {
                        str(name): int(count)
                        for name, count in selected_room_actor.get(
                            "actor_name_counts", {}
                        ).items()
                    }
                )
                selected_room_actor_object_ids.update(
                    {
                        str(name): int(count)
                        for name, count in selected_room_actor.get(
                            "object_name_counts", {}
                        ).items()
                    }
                )

        setup_records: list[dict[str, object]] = []
        for setup in setups:
            command_total += len(setup.commands)
            command_records: list[dict[str, object]] = []
            for command in setup.commands:
                command_key = f"0x{command.command_id:02x}"
                command_id_counts[command_key] += 1
                decoded = decode_route_command(
                    zsi.data,
                    command,
                    semantic_names,
                    setup.commands,
                    valid_room_indices=room_indices_by_stem.get(stem, set()),
                )
                decoded_status_counts[str(decoded["decode_status"])] += 1
                support_status_counts[str(decoded["runtime_support_status"])] += 1
                if layout_validation := decoded.get("layout_validation"):
                    layout_validation_counts[str(layout_validation["status"])] += 1
                if gap := runtime_gap_for_decoded_layout(command.command_id, decoded):
                    runtime_gap_counts[gap] += 1
                if command.command_id == 0x00:
                    spawn_list_command_count += 1
                    if decoded.get("selected_spawn_list_candidate"):
                        spawn_candidate_command_count += 1
                    if spawn_profile := decoded.get("spawn_payload_profile"):
                        spawn_payload_profile_counts[str(spawn_profile["status"])] += 1
                    spawn_entry_candidate_total += int(
                        decoded.get("spawn_entry_candidate_count", 0)
                    )
                    spawn_player_candidate_total += int(
                        decoded.get("spawn_player_candidate_count", 0)
                    )
                elif command.command_id == 0x01:
                    standard_actor_list_command_count += 1
                elif command.command_id == 0x06:
                    entrance_entry_candidate_total += int(
                        decoded.get("entrance_entry_candidate_count", 0)
                    )
                elif command.command_id == 0x07:
                    if special_files := decoded.get("special_files"):
                        special_file_records.append(
                            {"path": rel, "setup_index": setup.index, **special_files}
                        )
                elif command.command_id == 0x0B:
                    object_list_command_count += 1
                elif command.command_id == 0x0D:
                    if decoded.get("path_block_candidate"):
                        path_block_candidate_count += 1
                    path_file_offset_candidate_total += int(
                        decoded.get("path_file_offset_candidate_count", 0)
                    )
                elif command.command_id == 0x0E:
                    transition_actor_command_count += 1
                    transition_actor_records.extend(
                        {"path": rel, "setup_index": setup.index, **record}
                        for record in decoded.get("transition_actors", [])
                    )
                elif command.command_id == 0x13:
                    exit_value_candidate_total += int(
                        decoded.get("exit_value_candidate_count", 0)
                    )
                elif command.command_id == 0x0F:
                    light_setting_total += command.parameter
                elif command.command_id == 0x11:
                    skybox_settings.append(
                        {"path": rel, "setup_index": setup.index, **decoded["skybox_settings"]}
                    )
                elif command.command_id == 0x15:
                    sound_settings.append(
                        {
                            "path": rel,
                            "scene_stem": stem,
                            "setup_index": setup.index,
                            **decoded["sound_settings"],
                        }
                    )
                elif command.command_id == 0x17:
                    cutscene_references.append(
                        {"path": rel, "setup_index": setup.index, **decoded["cutscene_reference"]}
                    )
                elif command.command_id == 0x19:
                    misc_settings.append(
                        {"path": rel, "setup_index": setup.index, **decoded["misc_settings"]}
                    )

                for room_ref in decoded.get("room_references", []):
                    room_references.append(str(room_ref))
                command_records.append(decoded)

            setup_records.append(
                {
                    "index": setup.index,
                    "offset": setup.offset,
                    "end_offset": setup.end_offset,
                    "command_count": len(setup.commands),
                    "commands": command_records,
                }
            )

        file_record = {
            "path": rel,
            "role": role,
            "scene_stem": stem,
            "size": zsi_path.stat().st_size,
            "setup_count": len(setups),
            "embedded_cmb_count": len(embedded_cmbs),
            "embedded_cmbs": [
                {
                    "index": cmb.index,
                    "offset": cmb.offset,
                    "size": cmb.size,
                    "model_name": cmb.model.name,
                    "mesh_count": len(cmb.model.meshes),
                    "material_count": len(cmb.model.materials),
                    "texture_count": len(cmb.model.textures),
                    "bone_count": len(cmb.model.skeleton.bones),
                }
                for cmb in embedded_cmbs
            ],
            "collision_candidate_count": len(collision_candidates),
            "collision_header_candidates": [
                candidate.summary()
                for candidate in collision_candidates[:sample_limit]
            ],
            "room_actor_payload": room_actor_payload,
            "room_actor_list_candidate_count": (
                room_actor_payload["candidate_count"] if room_actor_payload else 0
            ),
            "selected_room_actor_list_candidate": (
                room_actor_payload.get("selected_candidate") if room_actor_payload else None
            ),
            "scene_setups": setup_records,
        }
        if include_records:
            records.append(file_record)

    audio_summary = route_audio_summary(romfs_root)
    audio_runtime_mapping = route_audio_runtime_mapping(
        sound_settings,
        audio_summary,
        sample_limit=sample_limit,
    )
    kankyo_summary = route_kankyo_summary(romfs_root)
    actor_object_binding_summary = route_actor_object_binding_summary(
        transition_actor_records,
        special_file_records,
        layout_validation_counts,
        semantic_names,
        object_list_command_count=object_list_command_count,
    )
    object_list_gap_resolved = (
        actor_object_binding_summary["object_list_dependency_status"]
        == "covered_without_scene_object_list"
    )
    room_actor_lists_identified = (
        role_counts["room"] > 0
        and selected_room_actor_list_count >= role_counts["room"]
    )
    if standard_actor_list_command_count == 0:
        if not room_actor_lists_identified:
            runtime_gap_counts["missing_standard_actor_list_commands"] = 1
    runtime_gap_counts["missing_object_list_commands"] = (
        1 if object_list_command_count == 0 and not object_list_gap_resolved else 0
    )
    runtime_gap_counts["needs_audio_entry_mapping"] = int(
        audio_runtime_mapping["unresolved_profile_count"]
    )
    kankyo_runtime_selection = route_kankyo_runtime_selection(
        skybox_settings,
        kankyo_summary,
    )
    runtime_gap_counts["needs_kankyo_runtime_selection"] = int(
        kankyo_runtime_selection["unresolved_selection_count"]
    )

    transition_actor_ids = Counter(
        record["actor_name"]
        for record in transition_actor_records
        if record.get("actor_id") not in (0, None)
    )

    audit: dict[str, object] = {
        "format": AUDIT_FORMAT,
        "romfs_root": str(romfs_root),
        "scene_root": str(scene_root),
        "route_stems": list(route_stems),
        "required_scene_files": [f"{stem}_info.zsi" for stem in route_stems],
        "missing_required_file_count": len(missing_files),
        "missing_required_files": missing_files,
        "zsi_file_count": len(scene_files),
        "role_counts": sorted_counter(role_counts),
        "scene_stem_counts": sorted_counter(stem_counts),
        "scene_file_count": role_counts["scene"],
        "room_file_count": role_counts["room"],
        "setup_total": setup_total,
        "command_total": command_total,
        "command_id_counts": sorted_counter(command_id_counts),
        "decoded_status_counts": sorted_counter(decoded_status_counts),
        "runtime_support_status_counts": sorted_counter(support_status_counts),
        "layout_validation_counts": sorted_counter(layout_validation_counts),
        "spawn_payload_profile_counts": sorted_counter(spawn_payload_profile_counts),
        "runtime_gap_counts": sorted_positive_counter(runtime_gap_counts),
        "embedded_cmb_total": embedded_cmb_total,
        "collision_candidate_total": collision_candidate_total,
        "collision_vertex_total": collision_vertex_total,
        "collision_effective_polygon_total": collision_effective_polygon_total,
        "collision_surface_type_total": collision_surface_type_total,
        "bgcam_total": bgcam_total,
        "water_box_total": water_box_total,
        "room_reference_count": len(room_references),
        "unique_room_references": sorted(set(room_references)),
        "spawn_list_command_count": spawn_list_command_count,
        "spawn_candidate_command_count": spawn_candidate_command_count,
        "spawn_entry_candidate_total": spawn_entry_candidate_total,
        "spawn_player_candidate_total": spawn_player_candidate_total,
        "entrance_entry_candidate_total": entrance_entry_candidate_total,
        "path_block_candidate_count": path_block_candidate_count,
        "path_file_offset_candidate_total": path_file_offset_candidate_total,
        "exit_value_candidate_total": exit_value_candidate_total,
        "standard_actor_list_command_count": standard_actor_list_command_count,
        "object_list_command_count": object_list_command_count,
        "room_actor_payload_status_counts": sorted_counter(room_actor_payload_status_counts),
        "room_actor_list_candidate_count": room_actor_list_candidate_count,
        "selected_room_actor_list_count": selected_room_actor_list_count,
        "selected_room_actor_entry_total": selected_room_actor_entry_total,
        "selected_room_actor_name_counts": sorted_counter(selected_room_actor_ids),
        "selected_room_actor_object_name_counts": sorted_counter(
            selected_room_actor_object_ids
        ),
        "special_file_command_count": len(special_file_records),
        "special_file_keep_object_counts": sorted_counter(
            Counter(str(record["keep_object_name"]) for record in special_file_records)
        ),
        "special_file_records": special_file_records[:sample_limit],
        "transition_actor_command_count": transition_actor_command_count,
        "transition_actor_count": len(transition_actor_records),
        "transition_actor_name_counts": sorted_counter(transition_actor_ids),
        "actor_object_binding_summary": actor_object_binding_summary,
        "light_setting_total": light_setting_total,
        "sound_setting_count": len(sound_settings),
        "sound_setting_records": sound_settings[:sample_limit],
        "audio_runtime_mapping": audio_runtime_mapping,
        "skybox_setting_count": len(skybox_settings),
        "skybox_setting_records": skybox_settings[:sample_limit],
        "kankyo_runtime_selection": kankyo_runtime_selection,
        "misc_setting_records": misc_settings[:sample_limit],
        "cutscene_reference_count": len(cutscene_references),
        "cutscene_references": cutscene_references[:sample_limit],
        "audio_asset_summary": audio_summary,
        "kankyo_asset_summary": kankyo_summary,
        "parse_error_count": len(parse_errors),
        "parse_errors": parse_errors,
        "records": records if include_records else [],
        "fallback_policy": [
            "This audit is an offline route inventory for future runtime work.",
            "It does not route Shipwright scenes, actors, audio, or environment resources by itself.",
            "N64 scene resources remain fallback until explicit Kokiri runtime routing is implemented.",
        ],
    }

    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(
            json.dumps(audit, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    return audit


def route_zsi_files(scene_root: Path, route_stems: tuple[str, ...]) -> tuple[list[Path], list[str]]:
    files: list[Path] = []
    missing: list[str] = []
    seen: set[Path] = set()
    for stem in route_stems:
        scene_file = scene_root / f"{stem}_info.zsi"
        if scene_file.is_file():
            files.append(scene_file)
            seen.add(scene_file)
        else:
            missing.append(scene_file.name)
        for room_file in sorted(scene_root.glob(f"{stem}_[0-9]*_info.zsi")):
            if room_file not in seen:
                files.append(room_file)
                seen.add(room_file)
    return files, missing


def route_room_indices(scene_files: list[Path]) -> dict[str, set[int]]:
    room_indices: dict[str, set[int]] = {}
    for path in scene_files:
        match = ROOM_INFO_RE.match(path.name)
        if match is None:
            continue
        room_indices.setdefault(match.group("stem"), set()).add(int(match.group("room_index")))
    return room_indices


def runtime_gap_for_decoded_layout(command_id: int, decoded: dict[str, object]) -> str | None:
    gap = OOT3D_BLOCK_LAYOUT_VALIDATION_GAPS.get(command_id)
    if gap is None:
        return None
    validation = decoded.get("layout_validation")
    if isinstance(validation, dict) and str(validation.get("status", "")).startswith("validated_"):
        return None
    return gap


def decode_route_command(
    data: bytes,
    command: ZsiSceneCommand,
    semantic_names: dict[str, dict[int, str]],
    setup_commands: tuple[ZsiSceneCommand, ...] = (),
    *,
    valid_room_indices: set[int] | None = None,
) -> dict[str, object]:
    command_name = COMMAND_NAMES.get(command.command_id, "unknown")
    result: dict[str, object] = {
        "offset": command.offset,
        "command_id": f"0x{command.command_id:02x}",
        "command_name": command_name,
        "parameter": command.parameter,
        "command_word": command.command_word,
        "argument": command.argument,
        "argument_hex": f"0x{command.argument:08x}",
        "argument_in_file": 0 <= command.argument < len(data),
        "payload_sample_hex": payload_sample_hex(data, command),
        "decode_status": "decoded_command_fields",
        "runtime_support_status": "metadata_only",
    }

    if validation_gap := OOT3D_BLOCK_LAYOUT_VALIDATION_GAPS.get(command.command_id):
        result["runtime_gap_key"] = validation_gap

    if command.command_id == 0x00:
        candidates = spawn_list_candidates_from_payload(
            data,
            command.argument,
            command.parameter,
            semantic_names,
            payload_end=payload_end_hint(data, command, setup_commands),
        )
        selected = candidates[0] if candidates else None
        result["spawn_list_candidates"] = candidates
        result["selected_spawn_list_candidate"] = selected
        result["spawn_entry_candidate_count"] = int(selected["entry_count"]) if selected else 0
        result["spawn_player_candidate_count"] = (
            int(selected["player_actor_count"]) if selected else 0
        )
        result["layout_validation"] = validate_spawn_layout(
            data,
            command,
            setup_commands,
            selected,
            valid_room_indices=valid_room_indices or set(),
        )
        result["spawn_payload_profile"] = classify_spawn_payload_profile(
            data,
            command,
            setup_commands,
            selected,
            result["layout_validation"],
            valid_room_indices=valid_room_indices or set(),
        )
        result["decode_status"] = (
            "decoded_oot3d_spawn_candidates" if selected else "oot3d_block_fingerprint_only"
        )
        result["runtime_support_status"] = (
            "oot3d_layout_validated_for_route"
            if str(result["layout_validation"]["status"]).startswith("validated_")
            else "oot3d_layout_candidate_needs_validation"
        )
        result["oot3d_block_u32_prefix"] = u32_prefix(data, command.argument)
    elif command.command_id == 0x04:
        refs = room_references_from_payload(data, command.argument)
        result["room_references"] = refs
        result["room_reference_count"] = len(refs)
        result["decode_status"] = "decoded_room_reference_strings"
        result["runtime_support_status"] = "route_dependency_identified"
    elif command.command_id == 0x06:
        payload_end = payload_end_hint(data, command, setup_commands)
        entrance_candidates = entrance_list_candidates_from_payload(
            data,
            command.argument,
            command.parameter,
            payload_end=payload_end,
        )
        prefixed_entrance = prefixed_entrance_list_candidate_from_payload(
            data,
            command,
            setup_commands,
            payload_end=payload_end,
        )
        selected = select_entrance_list_candidate(
            entrance_candidates,
            prefixed_entrance,
            valid_room_indices=valid_room_indices or set(),
        )
        if prefixed_entrance is not None:
            entrance_candidates = [
                prefixed_entrance,
                *[
                    candidate
                    for candidate in entrance_candidates
                    if int(candidate.get("start_offset", -1))
                    != int(prefixed_entrance["start_offset"])
                ],
            ]
        result["entrance_list_candidates"] = entrance_candidates
        result["selected_entrance_list_candidate"] = selected
        result["entrance_entries_candidate"] = selected["entries"] if selected else []
        result["entrance_entry_candidate_count"] = int(selected["entry_count"]) if selected else 0
        result["layout_validation"] = validate_entrance_layout(
            selected,
            valid_room_indices=valid_room_indices or set(),
        )
        result["decode_status"] = (
            "decoded_oot3d_entrance_candidates"
            if selected
            else "oot3d_block_fingerprint_only"
        )
        result["runtime_support_status"] = (
            "oot3d_layout_validated_for_route"
            if str(result["layout_validation"]["status"]).startswith("validated_")
            else "oot3d_layout_candidate_needs_validation"
        )
        result["oot3d_block_u32_prefix"] = u32_prefix(data, command.argument)
    elif command.command_id == 0x07:
        object_id = command.argument & 0xFFFF
        result["special_files"] = {
            "c_up_elf_message_file": command.parameter,
            "keep_object_id": object_id,
            "keep_object_name": semantic_names["object"].get(object_id, f"OBJECT_0x{object_id:04x}"),
        }
        result["runtime_support_status"] = "n64_semantics_reference"
    elif command.command_id == 0x0D:
        path_candidate = path_block_candidate_from_payload(
            data,
            command.argument,
            command.parameter,
        )
        result["path_block_candidate"] = path_candidate
        result["path_file_offset_candidate_count"] = (
            len(path_candidate["valid_file_offset_u32s"]) if path_candidate else 0
        )
        result["layout_validation"] = validate_path_layout(
            data,
            command,
            setup_commands,
            path_candidate,
        )
        result["decode_status"] = (
            "decoded_oot3d_path_block_candidate"
            if path_candidate
            else "oot3d_block_fingerprint_only"
        )
        result["runtime_support_status"] = (
            "oot3d_layout_validated_for_route"
            if str(result["layout_validation"]["status"]).startswith("validated_")
            else "oot3d_layout_candidate_needs_validation"
        )
        result["oot3d_block_u32_prefix"] = u32_prefix(data, command.argument)
    elif command.command_id == 0x0E:
        actors = transition_actors_from_payload(
            data,
            command.argument,
            command.parameter,
            semantic_names,
        )
        result["transition_actors"] = actors
        result["transition_actor_count"] = len(actors)
        result["runtime_support_status"] = "n64_layout_decoded_for_audit"
    elif command.command_id == 0x0F:
        result["light_settings"] = {
            "count": command.parameter,
            "entry_size": 0x16,
            "payload_bytes_required": command.parameter * 0x16,
        }
        result["runtime_support_status"] = "route_dependency_identified"
    elif command.command_id == 0x11:
        skybox_id = command.argument & 0xFF
        result["skybox_settings"] = {
            "skybox_id": skybox_id,
            "skybox_name": skybox_name(skybox_id),
            "weather_or_unk_05": (command.argument >> 8) & 0xFF,
            "indoors": (command.argument >> 16) & 0xFF,
        }
        result["runtime_support_status"] = "route_dependency_identified"
    elif command.command_id == 0x15:
        result["sound_settings"] = {
            "spec_id": command.parameter,
            "nature_ambience_id": (command.command_word >> 16) & 0xFF,
            "data3": (command.command_word >> 24) & 0xFF,
            "bgm_sound_id": command.argument,
            "bgm_sound_id_hex": f"0x{command.argument:08x}",
        }
        result["runtime_support_status"] = "route_dependency_identified"
    elif command.command_id == 0x17:
        result["cutscene_reference"] = {
            "offset": command.argument,
            "offset_hex": f"0x{command.argument:x}",
            "in_file": 0 <= command.argument < len(data),
        }
        result["runtime_support_status"] = "route_dependency_identified"
    elif command.command_id == 0x13:
        exit_start = exit_payload_start_hint(data, command, setup_commands)
        exit_values = exit_values_from_payload(
            data,
            command,
            setup_commands,
            payload_start=exit_start,
        )
        result["exit_values_candidate"] = exit_values
        result["exit_value_candidate_count"] = len(exit_values)
        result["layout_validation"] = validate_exit_layout(
            data,
            command,
            setup_commands,
            exit_values,
            payload_start=exit_start,
        )
        result["decode_status"] = (
            "decoded_oot3d_exit_candidates"
            if exit_values
            else "oot3d_block_fingerprint_only"
        )
        result["runtime_support_status"] = (
            "oot3d_layout_validated_for_route"
            if str(result["layout_validation"]["status"]).startswith("validated_")
            else "oot3d_layout_candidate_needs_validation"
        )
        result["oot3d_block_u32_prefix"] = u32_prefix(data, command.argument)
    elif command.command_id == 0x19:
        result["misc_settings"] = {
            "camera_movement": command.parameter,
            "world_map_area": command.argument,
        }
        result["runtime_support_status"] = "n64_semantics_reference"
    elif command.command_id == 0x14:
        result["runtime_support_status"] = "terminator"
    elif (
        command.command_id in DIRECT_FILE_OFFSET_COMMANDS
        and command.command_id not in OOT3D_BLOCK_LAYOUT_VALIDATION_GAPS
    ):
        result["runtime_support_status"] = "route_dependency_identified"

    return result


def room_actor_list_candidates_from_room_payload(
    data: bytes,
    semantic_names: dict[str, dict[int, str]],
    *,
    sample_limit: int = 20,
) -> dict[str, object]:
    scan_start = max(0, len(data) - ROOM_ACTOR_TRAILING_SCAN_WINDOW)
    if scan_start % 2:
        scan_start += 1
    candidates = []
    for start in range(scan_start, max(scan_start, len(data) - ACTOR_ENTRY_SIZE + 1), 2):
        object_prefix = room_object_prefix_before_actor_start(
            data,
            start,
            semantic_names,
        )
        if object_prefix is None or not room_actor_entry_is_plausible(
            data,
            start,
            semantic_names,
        ):
            continue

        entries = room_actor_entries_from_payload(data, start, semantic_names)
        if len(entries) < ROOM_ACTOR_MIN_ENTRY_COUNT:
            continue

        actor_counts = Counter(str(entry["actor_name"]) for entry in entries)
        object_counts = Counter(
            str(record["object_name"])
            for record in object_prefix["object_ids"]
            if int(record["object_id"]) != 0
        )
        unknown_object_count = len(object_prefix["unknown_object_ids"])
        score = score_room_actor_list_candidate(
            entries,
            object_prefix,
            semantic_names,
        )
        confidence = (
            "strong_object_prefixed_actor_list"
            if len(object_prefix["object_ids"]) >= ROOM_ACTOR_MIN_OBJECT_PREFIX_COUNT
            and len(entries) >= ROOM_ACTOR_MIN_ENTRY_COUNT
            and score >= 150
            else "object_prefixed_actor_list_candidate"
        )
        candidates.append(
            {
                "status": "object_prefixed_room_actor_list_candidate",
                "start_offset": start,
                "start_offset_hex": f"0x{start:x}",
                "end_offset": start + len(entries) * ACTOR_ENTRY_SIZE,
                "entry_size": ACTOR_ENTRY_SIZE,
                "entry_count": len(entries),
                "score": score,
                "confidence": confidence,
                "scan_window_start": scan_start,
                "scan_window_size": len(data) - scan_start,
                "object_prefix": object_prefix,
                "object_id_count": len(object_prefix["object_ids"]),
                "unknown_object_id_count": unknown_object_count,
                "object_name_counts": sorted_counter(object_counts),
                "actor_name_counts": sorted_counter(actor_counts),
                "entries": entries[:sample_limit],
            }
        )

    candidates.sort(
        key=lambda candidate: (
            int(candidate["score"]),
            int(candidate["object_id_count"]),
            int(candidate["entry_count"]),
            -int(candidate["start_offset"]),
        ),
        reverse=True,
    )
    selected = candidates[0] if candidates else None
    return {
        "status": (
            "object_prefixed_room_actor_list_identified"
            if selected
            else "room_actor_list_candidate_missing"
        ),
        "candidate_count": len(candidates),
        "selected_candidate": selected,
        "candidates": candidates[:sample_limit],
        "scan_window_start": scan_start,
        "scan_window_size": len(data) - scan_start,
        "evidence": [
            "OOT3D Kokiri room files do not expose standard N64 0x01 actor-list scene commands.",
            "Room actor candidates are little-endian 0x10-byte ActorEntry-shaped runs near EOF.",
            "Selected runs must begin immediately after an object-id prefix for the same room payload.",
        ],
    }


def room_object_prefix_before_actor_start(
    data: bytes,
    actor_start: int,
    semantic_names: dict[str, dict[int, str]],
) -> dict[str, object] | None:
    if not (0 < actor_start <= len(data)):
        return None
    object_names = semantic_names["object"]
    cursor = actor_start - 2
    zero_padding = []
    while cursor >= 0 and len(zero_padding) < ROOM_ACTOR_ZERO_PADDING_TAIL_LIMIT:
        object_id = read_s16(data, cursor)
        if object_id != 0:
            break
        zero_padding.append(
            {
                "offset": cursor,
                "offset_hex": f"0x{cursor:x}",
                "object_id": object_id,
                "object_name": "OBJECT_NONE",
            }
        )
        cursor -= 2

    unknown_tail = []
    while cursor >= 0 and len(unknown_tail) < ROOM_ACTOR_UNKNOWN_OBJECT_TAIL_LIMIT:
        object_id = read_s16(data, cursor)
        if object_id in object_names and object_id != 0:
            break
        if not is_possible_oot3d_object_id(object_id):
            break
        unknown_tail.append(
            {
                "offset": cursor,
                "offset_hex": f"0x{cursor:x}",
                "object_id": object_id,
                "object_name": f"OBJECT_0x{object_id & 0xFFFF:04x}",
                "semantic_status": "unknown_oot3d_object_id",
            }
        )
        cursor -= 2

    object_ids = []
    while cursor >= 0 and len(object_ids) < ROOM_ACTOR_OBJECT_PREFIX_SCAN_LIMIT:
        object_id = read_s16(data, cursor)
        if object_id not in object_names or object_id == 0:
            break
        object_ids.append(
            {
                "offset": cursor,
                "offset_hex": f"0x{cursor:x}",
                "object_id": object_id,
                "object_name": object_names[object_id],
                "semantic_status": "known_object_id",
            }
        )
        cursor -= 2

    if len(object_ids) < ROOM_ACTOR_MIN_OBJECT_PREFIX_COUNT:
        return None

    object_ids.reverse()
    unknown_tail.reverse()
    zero_padding.reverse()
    prefix_start = int(object_ids[0]["offset"])
    header_hints = room_object_prefix_header_hints(data, prefix_start)
    return {
        "start_offset": prefix_start,
        "start_offset_hex": f"0x{prefix_start:x}",
        "end_offset": actor_start,
        "byte_count": actor_start - prefix_start,
        "object_ids": object_ids,
        "unknown_object_ids": unknown_tail,
        "zero_padding": zero_padding,
        "header_hints": header_hints,
    }


def room_object_prefix_header_hints(data: bytes, prefix_start: int) -> list[dict[str, object]]:
    hints = []
    for delta in (2, 4, 6, 8):
        offset = prefix_start - delta
        if offset < 0 or offset + 2 > len(data):
            continue
        value = read_s16(data, offset)
        hints.append(
            {
                "delta_before_prefix": delta,
                "offset": offset,
                "offset_hex": f"0x{offset:x}",
                "s16": value,
                "u16": value & 0xFFFF,
            }
        )
    return hints


def room_actor_entries_from_payload(
    data: bytes,
    start: int,
    semantic_names: dict[str, dict[int, str]],
) -> list[dict[str, object]]:
    entries = []
    cursor = start
    while (
        len(entries) < ROOM_ACTOR_MAX_ENTRY_COUNT
        and room_actor_entry_is_plausible(data, cursor, semantic_names)
    ):
        entries.append(actor_entry_from_payload(data, cursor, len(entries), semantic_names))
        cursor += ACTOR_ENTRY_SIZE
    return entries


def room_actor_entry_is_plausible(
    data: bytes,
    offset: int,
    semantic_names: dict[str, dict[int, str]],
) -> bool:
    if offset < 0 or offset + ACTOR_ENTRY_SIZE > len(data):
        return False
    actor_id = read_s16(data, offset)
    if actor_id == 0 or actor_id not in semantic_names["actor"]:
        return False
    pos = (
        read_s16(data, offset + 2),
        read_s16(data, offset + 4),
        read_s16(data, offset + 6),
    )
    rot_x = read_s16(data, offset + 8)
    if rot_x != 0:
        return False
    return all(-ROOM_ACTOR_POSITION_LIMIT <= value <= ROOM_ACTOR_POSITION_LIMIT for value in pos)


def is_possible_oot3d_object_id(object_id: int) -> bool:
    return 0 < object_id <= OOT3D_POSSIBLE_OBJECT_ID_MAX


def score_room_actor_list_candidate(
    entries: list[dict[str, object]],
    object_prefix: dict[str, object],
    semantic_names: dict[str, dict[int, str]],
) -> int:
    score = int(len(object_prefix["object_ids"])) * 50
    score += int(len(object_prefix["unknown_object_ids"])) * 25
    score += min(len(entries), 80) * 2
    for entry in entries[:8]:
        score += score_room_actor_entry(entry, semantic_names)
    return score


def score_room_actor_entry(
    entry: dict[str, object],
    semantic_names: dict[str, dict[int, str]],
) -> int:
    actor_id = int(entry["actor_id"])
    actor_name = str(entry["actor_name"])
    pos = [int(value) for value in entry["pos"]]
    rot = [int(value) for value in entry["rot"]]
    score = 0
    if actor_id in semantic_names["actor"] and actor_id != 0:
        score += 3
    if "UNSET" not in actor_name and actor_id != 0:
        score += 2
    if all(-8000 <= value <= 8000 for value in pos):
        score += 2
    if -1000 <= pos[1] <= 1000:
        score += 1
    if rot[0] == 0:
        score += 1
    if rot[2] == 0:
        score += 1
    return score


def validate_entrance_layout(
    selected: dict[str, object] | None,
    *,
    valid_room_indices: set[int],
) -> dict[str, object]:
    if not selected:
        return {
            "status": "candidate_missing",
            "reason": "no selected entrance candidate",
            "valid_room_indices": sorted(valid_room_indices),
        }

    entries = selected.get("entries", [])
    expected_count = int(selected.get("expected_entry_count", 0))
    entry_count = int(selected.get("entry_count", 0))
    if str(selected.get("confidence")) != "strong":
        return {
            "status": "candidate_needs_validation",
            "reason": "selected entrance candidate is not strong",
            "valid_room_indices": sorted(valid_room_indices),
        }
    if expected_count <= 0 or entry_count != expected_count:
        return {
            "status": "candidate_needs_validation",
            "reason": "selected entrance candidate is not complete",
            "valid_room_indices": sorted(valid_room_indices),
        }
    if len(entries) != entry_count:
        return {
            "status": "candidate_needs_validation",
            "reason": "selected entrance entries are sampled, not complete",
            "valid_room_indices": sorted(valid_room_indices),
        }

    invalid_entries = []
    for entry in entries:
        room = int(entry.get("room", 0))
        room_u8 = int(entry.get("room_u8", room & 0xFF))
        if room_u8 == 0xFF:
            continue
        if room in valid_room_indices:
            continue
        invalid_entries.append(
            {
                "index": entry.get("index"),
                "spawn": entry.get("spawn"),
                "room": room,
                "room_u8": room_u8,
            }
        )

    if invalid_entries:
        return {
            "status": "candidate_needs_validation",
            "reason": "entrance room index is outside route room indices",
            "valid_room_indices": sorted(valid_room_indices),
            "invalid_entries": invalid_entries,
        }

    return {
        "status": (
            "validated_prefixed_route_room_indices"
            if selected.get("layout_variant") == "oot3d_prefixed_spawn_list"
            else "validated_route_room_indices"
        ),
        "entry_count": entry_count,
        "start_offset": selected.get("start_offset"),
        "start_delta": selected.get("start_delta"),
        "valid_room_indices": sorted(valid_room_indices),
        "allows_room_none": True,
    }


def prefixed_entrance_list_candidate_from_payload(
    data: bytes,
    command: ZsiSceneCommand,
    setup_commands: tuple[ZsiSceneCommand, ...],
    *,
    payload_end: int | None = None,
) -> dict[str, object] | None:
    if command.parameter <= 0 or not (0 <= command.argument < len(data)):
        return None
    if payload_end is None:
        payload_end = payload_end_hint(data, command, ())
    start_delta = ACTOR_ENTRY_SIZE
    start = command.argument + start_delta
    player_command = first_setup_command(setup_commands, 0x00)
    if player_command is not None and start == player_command.argument:
        return None
    required_end = start + command.parameter * 2
    if required_end > len(data):
        return None

    entries = [
        entrance_entry_from_payload(data, start + index * 2, index)
        for index in range(command.parameter)
    ]
    score = score_entrance_candidate(entries)
    return {
        "start_offset": start,
        "start_delta": start_delta,
        "payload_end_hint": payload_end,
        "extended_payload_end": required_end,
        "extends_beyond_payload_end_hint": required_end > payload_end,
        "fits_payload_window": required_end <= payload_end,
        "expected_entry_count": command.parameter,
        "entry_count": len(entries),
        "score": score,
        "confidence": entrance_candidate_confidence(entries, score, command.parameter),
        "layout_variant": "oot3d_prefixed_spawn_list",
        "prefix_size": start_delta,
        "entries": entries,
    }


def select_entrance_list_candidate(
    candidates: list[dict[str, object]],
    prefixed_candidate: dict[str, object] | None,
    *,
    valid_room_indices: set[int],
) -> dict[str, object] | None:
    if prefixed_candidate is not None:
        validation = validate_entrance_layout(
            prefixed_candidate,
            valid_room_indices=valid_room_indices,
        )
        if str(validation.get("status", "")).startswith("validated_"):
            return prefixed_candidate
    return candidates[0] if candidates else None


def select_validated_entrance_candidate(
    data: bytes,
    command: ZsiSceneCommand,
    setup_commands: tuple[ZsiSceneCommand, ...],
    *,
    valid_room_indices: set[int],
) -> dict[str, object] | None:
    payload_end = payload_end_hint(data, command, setup_commands)
    candidates = entrance_list_candidates_from_payload(
        data,
        command.argument,
        command.parameter,
        payload_end=payload_end,
    )
    prefixed_candidate = prefixed_entrance_list_candidate_from_payload(
        data,
        command,
        setup_commands,
        payload_end=payload_end,
    )
    return select_entrance_list_candidate(
        candidates,
        prefixed_candidate,
        valid_room_indices=valid_room_indices,
    )


def validate_spawn_layout(
    data: bytes,
    command: ZsiSceneCommand,
    setup_commands: tuple[ZsiSceneCommand, ...],
    selected: dict[str, object] | None,
    *,
    valid_room_indices: set[int],
) -> dict[str, object]:
    prefixed_validation = validate_prefixed_player_spawn_layout(
        data,
        command,
        setup_commands,
        selected,
        valid_room_indices=valid_room_indices,
    )
    if prefixed_validation is not None:
        return prefixed_validation

    if not selected:
        return {
            "status": "candidate_missing",
            "reason": "no selected spawn candidate",
            "n64_reference": "Scene_CommandSpawnList indexes spawn list through setupEntranceList[curSpawn].spawn",
        }

    entries = selected.get("entries", [])
    expected_count = int(selected.get("expected_entry_count", 0))
    entry_count = int(selected.get("entry_count", 0))
    if str(selected.get("confidence")) != "strong":
        return {
            "status": "candidate_needs_validation",
            "reason": "selected spawn candidate is not strong",
            "confidence": selected.get("confidence"),
        }
    if expected_count <= 0 or entry_count != expected_count:
        return {
            "status": "candidate_needs_validation",
            "reason": "selected spawn candidate is not complete",
            "expected_entry_count": expected_count,
            "entry_count": entry_count,
        }
    if len(entries) != entry_count:
        return {
            "status": "candidate_needs_validation",
            "reason": "selected spawn entries are sampled, not complete",
            "expected_entry_count": entry_count,
            "sampled_entry_count": len(entries),
        }

    entrance_command = first_setup_command(setup_commands, 0x06)
    if entrance_command is None:
        return {
            "status": "candidate_needs_validation",
            "reason": "matching entrance list command is missing",
        }

    entrance_candidates = entrance_list_candidates_from_payload(
        data,
        entrance_command.argument,
        entrance_command.parameter,
        payload_end=payload_end_hint(data, entrance_command, setup_commands),
    )
    selected_entrance = entrance_candidates[0] if entrance_candidates else None
    entrance_validation = validate_entrance_layout(
        selected_entrance,
        valid_room_indices=valid_room_indices,
    )
    if not str(entrance_validation.get("status", "")).startswith("validated_"):
        return {
            "status": "candidate_needs_validation",
            "reason": "matching entrance list is not validated",
            "entrance_validation": entrance_validation,
        }

    entrance_entries = selected_entrance.get("entries", []) if selected_entrance else []
    used_spawn_indices = sorted({int(entry["spawn"]) for entry in entrance_entries})
    invalid_indices = [
        index
        for index in used_spawn_indices
        if index < 0 or index >= entry_count
    ]
    if invalid_indices:
        return {
            "status": "candidate_needs_validation",
            "reason": "entrance references spawn indices outside the spawn list",
            "entry_count": entry_count,
            "invalid_spawn_indices": invalid_indices,
        }

    non_player_entries = []
    for index in used_spawn_indices:
        entry = entries[index]
        if int(entry.get("actor_id", -1)) != 0:
            non_player_entries.append(
                {
                    "spawn": index,
                    "actor_id": entry.get("actor_id"),
                    "actor_name": entry.get("actor_name"),
                }
            )
    if non_player_entries:
        return {
            "status": "candidate_needs_validation",
            "reason": "entrance-indexed spawn entries are not ACTOR_PLAYER",
            "non_player_entries": non_player_entries,
        }

    return {
        "status": "validated_player_spawn_indices",
        "entry_count": entry_count,
        "used_spawn_indices": used_spawn_indices,
        "entrance_entry_count": len(entrance_entries),
        "n64_reference": "Scene_CommandSpawnList uses setupEntranceList[curSpawn].spawn to select Link's ActorEntry",
    }


def validate_prefixed_player_spawn_layout(
    data: bytes,
    command: ZsiSceneCommand,
    setup_commands: tuple[ZsiSceneCommand, ...],
    selected: dict[str, object] | None,
    *,
    valid_room_indices: set[int],
) -> dict[str, object] | None:
    start_delta = ACTOR_ENTRY_SIZE
    expected_count = command.parameter
    entry_count = int(selected.get("entry_count", 0)) if selected else 0
    start = command.argument + start_delta
    if expected_count <= 0:
        return None
    required_end = start + expected_count * ACTOR_ENTRY_SIZE
    if required_end > len(data):
        return None

    entries = [
        actor_entry_from_payload(data, start + index * ACTOR_ENTRY_SIZE, index, {"actor": {}})
        for index in range(expected_count)
    ]
    non_player_entries = [
        {
            "spawn": int(entry["index"]),
            "actor_id": entry.get("actor_id"),
            "actor_name": entry.get("actor_name"),
        }
        for entry in entries
        if int(entry.get("actor_id", -1)) != 0
    ]
    if non_player_entries:
        return None

    entrance_command = first_setup_command(setup_commands, 0x06)
    if entrance_command is None:
        return None
    selected_entrance = select_validated_entrance_candidate(
        data,
        entrance_command,
        setup_commands,
        valid_room_indices=valid_room_indices,
    )
    entrance_validation = validate_entrance_layout(
        selected_entrance,
        valid_room_indices=valid_room_indices,
    )
    if not str(entrance_validation.get("status", "")).startswith("validated_"):
        return None

    entrance_entries = selected_entrance.get("entries", []) if selected_entrance else []
    used_spawn_indices = sorted({int(entry["spawn"]) for entry in entrance_entries})
    invalid_indices = [
        index
        for index in used_spawn_indices
        if index < 0 or index >= expected_count
    ]
    if invalid_indices:
        return None

    payload_end = (
        int(selected.get("payload_end_hint"))
        if selected and selected.get("payload_end_hint") is not None
        else payload_end_hint(data, command, setup_commands)
    )
    overlap = command_payload_overlap(
        start,
        required_end,
        command,
        setup_commands,
    )
    return {
        "status": "validated_prefixed_player_spawn_indices",
        "entry_count": expected_count,
        "selected_entry_count": entry_count,
        "start_offset": start,
        "start_delta": start_delta,
        "used_spawn_indices": used_spawn_indices,
        "entrance_entry_count": len(entrance_entries),
        "entrance_layout_status": entrance_validation.get("status"),
        "entrance_start_delta": selected_entrance.get("start_delta") if selected_entrance else None,
        "payload_end_hint": payload_end,
        "extended_payload_end": required_end,
        "overlaps_following_command_payload": required_end > payload_end,
        "command_payload_overlap": overlap,
        "n64_reference": "Scene_CommandSpawnList uses setupEntranceList[curSpawn].spawn to select Link's ActorEntry",
    }


def command_payload_overlap(
    start: int,
    end: int,
    command: ZsiSceneCommand,
    setup_commands: tuple[ZsiSceneCommand, ...],
) -> list[dict[str, object]]:
    overlaps = []
    for other in setup_commands:
        if other.offset == command.offset:
            continue
        if other.command_id not in DIRECT_FILE_OFFSET_COMMANDS:
            continue
        if start <= other.argument < end:
            overlaps.append(
                {
                    "command_id": f"0x{other.command_id:02x}",
                    "command_name": COMMAND_NAMES.get(other.command_id, "unknown"),
                    "argument": other.argument,
                    "argument_hex": f"0x{other.argument:08x}",
                    "delta_from_window_start": other.argument - start,
                }
            )
    return overlaps


def classify_spawn_payload_profile(
    data: bytes,
    command: ZsiSceneCommand,
    setup_commands: tuple[ZsiSceneCommand, ...],
    selected: dict[str, object] | None,
    layout_validation: dict[str, object],
    *,
    valid_room_indices: set[int],
) -> dict[str, object]:
    if layout_validation.get("status") == "validated_prefixed_player_spawn_indices":
        return {
            "status": "validated_prefixed_player_start_list",
            "layout_validation_status": layout_validation.get("status"),
            "entry_count": layout_validation.get("entry_count"),
            "selected_entry_count": layout_validation.get("selected_entry_count"),
            "start_delta": layout_validation.get("start_delta"),
            "used_spawn_indices": layout_validation.get("used_spawn_indices", []),
            "overlaps_following_command_payload": layout_validation.get(
                "overlaps_following_command_payload"
            ),
            "command_payload_overlap": layout_validation.get("command_payload_overlap", []),
            "runtime_mapping": "Scene_CommandSpawnList compatible after OOT3D prefix skip",
        }

    if str(layout_validation.get("status", "")).startswith("validated_"):
        return {
            "status": "validated_n64_player_start_list",
            "layout_validation_status": layout_validation.get("status"),
            "entry_count": layout_validation.get("entry_count"),
            "used_spawn_indices": layout_validation.get("used_spawn_indices", []),
            "runtime_mapping": "Scene_CommandSpawnList compatible",
        }

    if not selected:
        return {
            "status": "unresolved_spawn_payload_candidate_missing",
            "layout_validation_status": layout_validation.get("status"),
        }

    actor_name_counts = {
        str(name): int(count)
        for name, count in dict(selected.get("actor_name_counts", {})).items()
    }
    player_count = int(selected.get("player_actor_count", 0))
    entry_count = int(selected.get("entry_count", 0))
    expected_count = int(selected.get("expected_entry_count", 0))
    non_player_names = sorted(
        name
        for name in actor_name_counts
        if name != "ACTOR_PLAYER"
    )
    used_spawn_indices = entrance_spawn_indices(
        data,
        setup_commands,
        valid_room_indices=valid_room_indices,
    )
    path_overlap = path_payload_overlap(command, setup_commands)
    entrance_overlap = entrance_payload_overlap(command, setup_commands)

    base: dict[str, object] = {
        "layout_validation_status": layout_validation.get("status"),
        "layout_validation_reason": layout_validation.get("reason"),
        "start_delta": selected.get("start_delta"),
        "entry_count": entry_count,
        "expected_entry_count": expected_count,
        "confidence": selected.get("confidence"),
        "player_actor_count": player_count,
        "actor_name_counts": actor_name_counts,
        "used_spawn_indices": used_spawn_indices,
        "max_used_spawn_index": max(used_spawn_indices) if used_spawn_indices else None,
    }

    if player_count == entry_count and entry_count < expected_count:
        missing = [
            index
            for index in used_spawn_indices
            if index < 0 or index >= entry_count
        ]
        return {
            **base,
            "status": "prefixed_player_start_window_missing_entrance_indices",
            "missing_spawn_indices": missing,
            "payload_profile_reason": (
                "selected OoT3D player-start window begins after a prefix but does not "
                "cover every entrance-indexed spawn"
            ),
        }

    if path_overlap is not None:
        return {
            **base,
            "status": "path_block_overlap_actor_like_candidate",
            "path_overlap": path_overlap,
            "payload_profile_reason": (
                "the actor-like candidate starts inside the decoded OoT3D path block"
            ),
        }

    if entrance_overlap is not None and player_count and non_player_names:
        return {
            **base,
            "status": "entrance_overlap_mixed_actor_entry_candidate",
            "entrance_overlap": entrance_overlap,
            "non_player_actor_names": non_player_names,
            "payload_profile_reason": (
                "the selected window overlaps the entrance block and mixes player and "
                "non-player ActorEntry-shaped data"
            ),
        }

    if entrance_overlap is not None and non_player_names:
        return {
            **base,
            "status": "entrance_overlap_actor_entry_candidate",
            "entrance_overlap": entrance_overlap,
            "non_player_actor_names": non_player_names,
            "payload_profile_reason": (
                "the selected actor-like window starts inside the OoT3D entrance block"
            ),
        }

    if non_player_names:
        return {
            **base,
            "status": "non_player_actor_entry_candidate",
            "non_player_actor_names": non_player_names,
            "payload_profile_reason": (
                "the selected window is ActorEntry-shaped but not a Link start list"
            ),
        }

    return {
        **base,
        "status": "unresolved_spawn_payload_profile",
        "payload_profile_reason": "no validated player-start or actor-entry profile matched",
    }


def entrance_spawn_indices(
    data: bytes,
    setup_commands: tuple[ZsiSceneCommand, ...],
    *,
    valid_room_indices: set[int],
) -> list[int]:
    entrance_command = first_setup_command(setup_commands, 0x06)
    if entrance_command is None:
        return []
    selected = select_validated_entrance_candidate(
        data,
        entrance_command,
        setup_commands,
        valid_room_indices=valid_room_indices,
    )
    if not selected:
        return []
    validation = validate_entrance_layout(
        selected,
        valid_room_indices=valid_room_indices,
    )
    if validation.get("status") == "candidate_missing":
        return []
    return sorted({int(entry["spawn"]) for entry in selected.get("entries", [])})


def path_payload_overlap(
    command: ZsiSceneCommand,
    setup_commands: tuple[ZsiSceneCommand, ...],
) -> dict[str, object] | None:
    for other in setup_commands:
        if other.command_id != 0x0D:
            continue
        delta = command.argument - other.argument
        if 0 <= delta <= 0x20:
            return {
                "command_id": "0x0d",
                "path_argument": other.argument,
                "path_argument_hex": f"0x{other.argument:08x}",
                "delta": delta,
            }
    return None


def entrance_payload_overlap(
    command: ZsiSceneCommand,
    setup_commands: tuple[ZsiSceneCommand, ...],
) -> dict[str, object] | None:
    entrance_command = first_setup_command(setup_commands, 0x06)
    if entrance_command is None:
        return None
    delta = command.argument - entrance_command.argument
    if 0 <= delta <= 0x10:
        return {
            "command_id": "0x06",
            "entrance_argument": entrance_command.argument,
            "entrance_argument_hex": f"0x{entrance_command.argument:08x}",
            "delta": delta,
        }
    return None


def validate_exit_layout(
    data: bytes,
    command: ZsiSceneCommand,
    setup_commands: tuple[ZsiSceneCommand, ...],
    values: list[dict[str, object]],
    *,
    payload_start: int | None = None,
) -> dict[str, object]:
    if not (0 <= command.argument < len(data)):
        return {
            "status": "candidate_missing",
            "reason": "exit list argument is outside file",
        }

    if payload_start is None:
        payload_start = command.argument
    if not (0 <= payload_start <= len(data)):
        return {
            "status": "candidate_missing",
            "reason": "exit list payload start is outside file",
            "payload_start": payload_start,
        }

    payload_end = payload_end_hint_for_payload_start(
        data,
        command,
        setup_commands,
        payload_start=payload_start,
    )
    payload_size = payload_end - payload_start
    if payload_size <= 0:
        return {
            "status": "candidate_needs_validation",
            "reason": "exit list payload window is empty",
            "payload_start": payload_start,
            "payload_end_hint": payload_end,
        }
    if payload_size % 2:
        return {
            "status": "candidate_needs_validation",
            "reason": "exit list payload window is not s16 aligned",
            "payload_start": payload_start,
            "payload_end_hint": payload_end,
            "payload_size": payload_size,
        }

    entry_count = payload_size // 2
    if len(values) != entry_count:
        return {
            "status": "candidate_needs_validation",
            "reason": "exit value sample does not cover the payload window",
            "payload_start": payload_start,
            "payload_end_hint": payload_end,
            "payload_size": payload_size,
            "expected_entry_count": entry_count,
            "sampled_entry_count": len(values),
        }

    return {
        "status": (
            "validated_prefixed_s16_exit_payload_window"
            if payload_start != command.argument
            else "validated_s16_exit_payload_window"
        ),
        "entry_count": entry_count,
        "payload_start": payload_start,
        "start_delta": payload_start - command.argument,
        "payload_end_hint": payload_end,
        "payload_size": payload_size,
        "semantic_mapping": "code_bin_transition_mapping_confirmed",
    }


def validate_path_layout(
    data: bytes,
    command: ZsiSceneCommand,
    setup_commands: tuple[ZsiSceneCommand, ...],
    candidate: dict[str, object] | None,
    *,
    payload_start: int | None = None,
    records: list[dict[str, object]] | None = None,
) -> dict[str, object]:
    if not candidate:
        return {
            "status": "candidate_missing",
            "reason": "no selected path candidate",
        }
    if command.parameter <= 0:
        return {
            "status": "candidate_needs_validation",
            "reason": "path command has no path records",
        }
    if not (0 <= command.argument < len(data)):
        return {
            "status": "candidate_missing",
            "reason": "path list argument is outside file",
        }
    if payload_start is None:
        payload_start = command.argument
    if not (0 <= payload_start < len(data)):
        return {
            "status": "candidate_missing",
            "reason": "path list payload start is outside file",
            "payload_start": payload_start,
        }

    payload_end = payload_end_hint(data, command, setup_commands)
    expected_size = command.parameter * 8
    table_end = payload_start + expected_size
    status_counts: Counter[str] = Counter()
    if records is not None:
        status_counts.update(str(record.get("points_status", "unknown")) for record in records)
    if records is not None and status_counts.get("invalid_points_offset", 0):
        return {
            "status": "candidate_needs_validation",
            "reason": "path records include point offsets that do not resolve inside the containing ZSI",
            "payload_start": payload_start,
            "start_delta": payload_start - command.argument,
            "payload_end_hint": payload_end,
            "table_end": table_end,
            "path_record_count": command.parameter,
            "record_status_counts": dict(sorted(status_counts.items())),
        }
    if table_end > len(data):
        return {
            "status": "candidate_needs_validation",
            "reason": "path record table extends outside file",
            "payload_start": payload_start,
            "start_delta": payload_start - command.argument,
            "payload_end_hint": payload_end,
            "table_end": table_end,
            "expected_payload_size": expected_size,
            "path_record_count": command.parameter,
            "record_status_counts": dict(sorted(status_counts.items())),
        }

    return {
        "status": (
            "validated_prefixed_path_record_window"
            if payload_start != command.argument
            else "validated_path_record_window"
        ),
        "path_record_count": command.parameter,
        "payload_start": payload_start,
        "start_delta": payload_start - command.argument,
        "payload_end_hint": payload_end,
        "table_end": table_end,
        "table_size": expected_size,
        "path_record_size": 8,
        "record_status_counts": dict(sorted(status_counts.items())),
        "semantic_mapping": "code_bin_path_record_mapping_confirmed",
    }


def first_setup_command(
    setup_commands: tuple[ZsiSceneCommand, ...],
    command_id: int,
) -> ZsiSceneCommand | None:
    for command in setup_commands:
        if command.command_id == command_id:
            return command
    return None


def payload_sample_hex(data: bytes, command: ZsiSceneCommand, size: int = 32) -> str:
    if command.command_id not in DIRECT_FILE_OFFSET_COMMANDS:
        return ""
    if not (0 <= command.argument < len(data)):
        return ""
    return data[command.argument : min(len(data), command.argument + size)].hex()


def u32_prefix(data: bytes, offset: int, count: int = 4) -> list[str]:
    if not (0 <= offset < len(data)):
        return []
    values = []
    for index in range(count):
        cursor = offset + index * 4
        if cursor + 4 > len(data):
            break
        value = int.from_bytes(data[cursor : cursor + 4], "little")
        values.append(f"0x{value:08x}")
    return values


def spawn_list_candidates_from_payload(
    data: bytes,
    offset: int,
    count: int,
    semantic_names: dict[str, dict[int, str]],
    *,
    payload_end: int | None = None,
    sample_limit: int = 5,
) -> list[dict[str, object]]:
    if count <= 0 or not (0 <= offset < len(data)):
        return []
    if payload_end is None:
        payload_end = len(data)
    candidates: list[dict[str, object]] = []
    for start_delta in (0, 4, 8, 12, 16):
        start = offset + start_delta
        available_entry_count = max(0, min(count, (payload_end - start) // ACTOR_ENTRY_SIZE))
        if available_entry_count <= 0:
            continue
        fits_payload_window = available_entry_count == count
        entries: list[dict[str, object]] = []
        for index in range(available_entry_count):
            cursor = start + index * ACTOR_ENTRY_SIZE
            if cursor + ACTOR_ENTRY_SIZE > len(data):
                break
            entries.append(actor_entry_from_payload(data, cursor, index, semantic_names))
        if not entries:
            continue
        score = score_spawn_candidate(entries, semantic_names)
        player_actor_count = sum(1 for entry in entries if entry["actor_id"] == 0)
        actor_name_counts = Counter(
            str(entry["actor_name"])
            for entry in entries
            if entry["actor_id"] not in (-1, None)
        )
        candidates.append(
            {
                "start_offset": start,
                "start_delta": start_delta,
                "payload_end_hint": payload_end,
                "fits_payload_window": fits_payload_window,
                "expected_entry_count": count,
                "available_entry_count": available_entry_count,
                "entry_count": len(entries),
                "score": score,
                "confidence": spawn_candidate_confidence(entries, score, count),
                "player_actor_count": player_actor_count,
                "actor_name_counts": sorted_counter(actor_name_counts),
                "entries": entries[:sample_limit],
            }
        )
    candidates.sort(
        key=lambda candidate: (
            int(candidate["score"]),
            int(candidate["entry_count"]),
            -int(candidate["start_delta"]),
        ),
        reverse=True,
    )
    return candidates


def actor_entry_from_payload(
    data: bytes,
    offset: int,
    index: int,
    semantic_names: dict[str, dict[int, str]],
) -> dict[str, object]:
    actor_id = read_s16(data, offset)
    return {
        "index": index,
        "offset": offset,
        "actor_id": actor_id,
        "actor_name": semantic_names["actor"].get(actor_id, f"ACTOR_0x{actor_id & 0xFFFF:04x}"),
        "pos": [
            read_s16(data, offset + 2),
            read_s16(data, offset + 4),
            read_s16(data, offset + 6),
        ],
        "rot": [
            read_s16(data, offset + 8),
            read_s16(data, offset + 10),
            read_s16(data, offset + 12),
        ],
        "params": read_s16(data, offset + 14),
    }


def score_spawn_candidate(
    entries: list[dict[str, object]],
    semantic_names: dict[str, dict[int, str]],
) -> int:
    score = 0
    for entry in entries[:4]:
        actor_id = int(entry["actor_id"])
        pos = [int(value) for value in entry["pos"]]
        entry_score = 0
        if -1 <= actor_id <= 0x03FF:
            entry_score += 2
        if actor_id in semantic_names["actor"]:
            entry_score += 3
        if actor_id == 0:
            entry_score += 4
        elif actor_id > 0:
            entry_score += 1
        if all(-20000 <= value <= 20000 for value in pos):
            entry_score += 2
        if any(value != 0 for value in pos):
            entry_score += 1
        score += entry_score
    if entries and entries[0]["actor_id"] == 0:
        score += 4
    return score


def spawn_candidate_confidence(
    entries: list[dict[str, object]],
    score: int,
    expected_count: int,
) -> str:
    if not entries:
        return "none"
    complete = len(entries) == expected_count
    first_is_player = entries[0]["actor_id"] == 0
    if complete and first_is_player and score >= 12:
        return "strong"
    if complete and score >= 8:
        return "candidate"
    if first_is_player and score >= 12:
        return "partial_strong"
    if score >= 8:
        return "partial_candidate"
    return "weak"


def entrance_list_candidates_from_payload(
    data: bytes,
    offset: int,
    count: int,
    *,
    payload_end: int | None = None,
    sample_limit: int = 16,
) -> list[dict[str, object]]:
    if count <= 0 or not (0 <= offset < len(data)):
        return []
    if payload_end is None:
        payload_end = len(data)
    candidates = []
    for start_delta in (0, 4, 8, 12, 16):
        start = offset + start_delta
        if start + count * 2 > payload_end:
            continue
        entries = []
        for index in range(count):
            cursor = start + index * 2
            if cursor + 2 > len(data):
                break
            entries.append(entrance_entry_from_payload(data, cursor, index))
        if not entries:
            continue
        score = score_entrance_candidate(entries)
        candidates.append(
            {
                "start_offset": start,
                "start_delta": start_delta,
                "payload_end_hint": payload_end,
                "fits_payload_window": True,
                "expected_entry_count": count,
                "entry_count": len(entries),
                "score": score,
                "confidence": entrance_candidate_confidence(entries, score, count),
                "entries": entries[:sample_limit],
            }
        )
    candidates.sort(
        key=lambda candidate: (
            int(candidate["score"]),
            int(candidate["entry_count"]),
            -int(candidate["start_delta"]),
        ),
        reverse=True,
    )
    return candidates


def entrance_entry_from_payload(data: bytes, offset: int, index: int) -> dict[str, object]:
    return {
        "index": index,
        "offset": offset,
        "spawn": data[offset],
        "room": read_s8(data, offset + 1),
        "room_u8": data[offset + 1],
    }


def score_entrance_candidate(entries: list[dict[str, object]]) -> int:
    score = 0
    previous_spawn = -1
    for entry in entries[:16]:
        spawn = int(entry["spawn"])
        room = int(entry["room"])
        if 0 <= spawn <= 31:
            score += 3
        if -1 <= room <= 15:
            score += 3
        if previous_spawn >= 0 and spawn == previous_spawn + 1:
            score += 1
        previous_spawn = spawn
    return score


def entrance_candidate_confidence(
    entries: list[dict[str, object]],
    score: int,
    expected_count: int,
) -> str:
    if not entries:
        return "none"
    complete = len(entries) == expected_count
    if complete and score >= expected_count * 5:
        return "strong"
    if complete and score >= expected_count * 3:
        return "candidate"
    return "weak"


def path_block_candidate_from_payload(
    data: bytes,
    offset: int,
    count_hint: int,
) -> dict[str, object] | None:
    if not (0 <= offset < len(data)):
        return None
    u32_values = []
    valid_offsets = []
    for index in range(8):
        cursor = offset + index * 4
        if cursor + 4 > len(data):
            break
        value = read_u32(data, cursor)
        file_offset_like = 0x18 <= value < len(data) and value % 4 == 0
        record = {
            "index": index,
            "offset": cursor,
            "value": value,
            "value_hex": f"0x{value:08x}",
            "valid_file_offset": file_offset_like,
        }
        u32_values.append(record)
        if record["valid_file_offset"]:
            valid_offsets.append(record)

    s16_values = []
    for index in range(12):
        cursor = offset + index * 2
        if cursor + 2 > len(data):
            break
        s16_values.append(read_s16(data, cursor))

    return {
        "offset": offset,
        "count_hint": count_hint,
        "u32_prefix": u32_values,
        "valid_file_offset_u32s": valid_offsets,
        "s16_prefix": s16_values,
    }


def exit_values_from_payload(
    data: bytes,
    command: ZsiSceneCommand,
    setup_commands: tuple[ZsiSceneCommand, ...],
    *,
    payload_start: int | None = None,
) -> list[dict[str, object]]:
    if not (0 <= command.argument < len(data)):
        return []
    if payload_start is None:
        payload_start = command.argument
    if not (0 <= payload_start < len(data)):
        return []
    end = payload_end_hint_for_payload_start(
        data,
        command,
        setup_commands,
        payload_start=payload_start,
    )
    sample_end = min(end, payload_start + EXIT_VALUE_SAMPLE_LIMIT * 2)
    values = []
    for index, cursor in enumerate(range(payload_start, sample_end, 2)):
        if cursor + 2 > len(data):
            break
        value = read_s16(data, cursor)
        values.append(
            {
                "index": index,
                "offset": cursor,
                "value": value,
                "value_hex": f"0x{value & 0xFFFF:04x}",
            }
        )
    return values


def exit_payload_start_hint(
    data: bytes,
    command: ZsiSceneCommand,
    setup_commands: tuple[ZsiSceneCommand, ...],
) -> int:
    player_command = first_setup_command(setup_commands, 0x00)
    if player_command is None:
        return command.argument
    player_start = player_command.argument + ACTOR_ENTRY_SIZE
    player_end = player_start + player_command.parameter * ACTOR_ENTRY_SIZE
    if (
        player_command.parameter > 0
        and player_end <= len(data)
        and all(
            read_s16(data, player_start + index * ACTOR_ENTRY_SIZE) == 0
            for index in range(player_command.parameter)
        )
        and player_start <= command.argument < player_end
    ):
        return command.argument + ACTOR_ENTRY_SIZE
    return command.argument


def payload_end_hint(
    data: bytes,
    command: ZsiSceneCommand,
    setup_commands: tuple[ZsiSceneCommand, ...],
    *,
    default_window: int = 1024,
) -> int:
    if not (0 <= command.argument < len(data)):
        return command.argument
    following_offsets = [
        other.argument
        for other in setup_commands
        if other.offset != command.offset
        and other.command_id in DIRECT_FILE_OFFSET_COMMANDS
        and command.argument < other.argument <= len(data)
    ]
    if following_offsets:
        return min(min(following_offsets), command.argument + default_window)
    return min(len(data), command.argument + default_window)


def payload_end_hint_for_payload_start(
    data: bytes,
    command: ZsiSceneCommand,
    setup_commands: tuple[ZsiSceneCommand, ...],
    *,
    payload_start: int,
    default_window: int = 1024,
) -> int:
    if payload_start == command.argument:
        return payload_end_hint(
            data,
            command,
            setup_commands,
            default_window=default_window,
        )
    following_offsets = []
    for other in setup_commands:
        if other.offset == command.offset:
            continue
        if other.command_id not in DIRECT_FILE_OFFSET_COMMANDS:
            continue
        effective_argument = other.argument
        if other.argument <= payload_start:
            shifted_argument = other.argument + ACTOR_ENTRY_SIZE
            if shifted_argument > payload_start:
                effective_argument = shifted_argument
        if payload_start < effective_argument <= len(data):
            following_offsets.append(effective_argument)
    if following_offsets:
        return min(min(following_offsets), payload_start + default_window)
    return min(len(data), payload_start + default_window)


def room_references_from_payload(data: bytes, offset: int, window: int = 512) -> list[str]:
    if not (0 <= offset < len(data)):
        return []
    chunk = data[offset : min(len(data), offset + window)]
    return [
        match.group(1).decode("ascii", errors="replace")
        for match in ROOM_REF_RE.finditer(chunk)
    ]


def transition_actors_from_payload(
    data: bytes,
    offset: int,
    count: int,
    semantic_names: dict[str, dict[int, str]],
) -> list[dict[str, object]]:
    if count <= 0 or not (0 <= offset <= len(data)):
        return []
    actors = []
    for index in range(count):
        cursor = offset + index * TRANSITION_ACTOR_ENTRY_SIZE
        if cursor + TRANSITION_ACTOR_ENTRY_SIZE > len(data):
            break
        actor_id = read_s16(data, cursor + 4)
        actors.append(
            {
                "index": index,
                "front_room": read_s8(data, cursor),
                "front_effect": read_s8(data, cursor + 1),
                "back_room": read_s8(data, cursor + 2),
                "back_effect": read_s8(data, cursor + 3),
                "actor_id": actor_id,
                "actor_name": semantic_names["actor"].get(actor_id, f"ACTOR_0x{actor_id:04x}"),
                "pos": [
                    read_s16(data, cursor + 6),
                    read_s16(data, cursor + 8),
                    read_s16(data, cursor + 10),
                ],
                "rot_y": read_s16(data, cursor + 12),
                "params": read_s16(data, cursor + 14),
            }
        )
    return actors


def route_actor_object_binding_summary(
    transition_actor_records: list[dict[str, object]],
    special_file_records: list[dict[str, object]],
    layout_validation_counts: Counter[str],
    semantic_names: dict[str, dict[int, str]],
    *,
    object_list_command_count: int,
) -> dict[str, object]:
    transition_actor_counts: Counter[str] = Counter()
    transition_actor_ids: dict[str, int] = {}
    transition_actor_binding_counts: Counter[str] = Counter()
    required_object_counts: Counter[str] = Counter()
    unresolved_actor_counts: Counter[str] = Counter()
    empty_transition_actor_slot_count = 0

    for record in transition_actor_records:
        actor_id = record.get("actor_id")
        if actor_id in (0, None):
            empty_transition_actor_slot_count += 1
            continue
        actor_name = str(record.get("actor_name", f"ACTOR_0x{int(actor_id):04x}"))
        transition_actor_counts[actor_name] += 1
        transition_actor_ids.setdefault(actor_name, int(actor_id))
        binding = ROUTE_ACTOR_OBJECT_REQUIREMENTS.get(actor_name)
        if binding is None:
            unresolved_actor_counts[actor_name] += 1
            continue
        object_name = str(binding["required_object_name"])
        transition_actor_binding_counts[f"{actor_name}->{object_name}"] += 1
        required_object_counts[object_name] += 1

    binding_records = []
    for actor_name in sorted(transition_actor_counts):
        binding = ROUTE_ACTOR_OBJECT_REQUIREMENTS.get(actor_name)
        if binding is None:
            continue
        object_name = str(binding["required_object_name"])
        binding_records.append(
            {
                "actor_name": actor_name,
                "actor_id": transition_actor_ids.get(actor_name),
                "count": transition_actor_counts[actor_name],
                "required_object_name": object_name,
                "required_object_id": semantic_id_by_name(
                    semantic_names,
                    "object",
                    object_name,
                ),
                "source": binding["source"],
                "reference": binding["reference"],
            }
        )

    special_keep_counts = Counter(
        str(record["keep_object_name"])
        for record in special_file_records
        if "keep_object_name" in record
    )
    special_keep_ids: dict[str, int] = {}
    for record in special_file_records:
        name = record.get("keep_object_name")
        object_id = record.get("keep_object_id")
        if name is not None and object_id is not None:
            special_keep_ids.setdefault(str(name), int(object_id))

    validated_player_spawns = int(
        layout_validation_counts.get("validated_player_spawn_indices", 0)
        + layout_validation_counts.get("validated_prefixed_player_spawn_indices", 0)
    )
    player_runtime_object_names = ["OBJECT_LINK_BOY", "OBJECT_LINK_CHILD"]
    unresolved_count = sum(unresolved_actor_counts.values())
    if unresolved_count:
        object_list_dependency_status = "needs_actor_object_binding"
        status = "route_actor_object_bindings_need_mapping"
    elif object_list_command_count == 0:
        object_list_dependency_status = "covered_without_scene_object_list"
        status = "route_actor_object_bindings_resolved"
    else:
        object_list_dependency_status = "scene_object_list_present"
        status = "route_actor_object_bindings_resolved"

    return {
        "status": status,
        "object_list_dependency_status": object_list_dependency_status,
        "object_list_command_count": object_list_command_count,
        "requires_scene_object_list_for_known_route_actors": bool(unresolved_count),
        "transition_actor_binding_counts": sorted_positive_counter(
            transition_actor_binding_counts
        ),
        "transition_actor_bindings": binding_records,
        "transition_actor_required_object_counts": sorted_positive_counter(
            required_object_counts
        ),
        "unresolved_transition_actor_binding_count": unresolved_count,
        "unresolved_transition_actor_name_counts": sorted_positive_counter(
            unresolved_actor_counts
        ),
        "empty_transition_actor_slot_count": empty_transition_actor_slot_count,
        "special_keep_object_counts": sorted_counter(special_keep_counts),
        "special_keep_objects": [
            {
                "object_name": name,
                "object_id": special_keep_ids.get(name),
                "setup_count": special_keep_counts[name],
            }
            for name in sorted(special_keep_counts)
        ],
        "player_object_binding": {
            "status": (
                "runtime_binding_identified"
                if validated_player_spawns
                else "needs_spawn_layout_validation"
            ),
            "validated_player_spawn_layout_count": validated_player_spawns,
            "runtime_object_source": "gLinkObjectIds[gSaveContext.linkAge]",
            "reference": "soh/src/code/z_scene.c Scene_CommandSpawnList",
            "runtime_object_names": player_runtime_object_names,
            "runtime_object_ids": {
                name: semantic_id_by_name(semantic_names, "object", name)
                for name in player_runtime_object_names
            },
        },
        "evidence": [
            "OBJECT_GAMEPLAY_KEEP is loaded as the global main keep before scene commands run.",
            "Scene_CommandSpawnList assigns ACTOR_PLAYER to gLinkObjectIds[gSaveContext.linkAge] and spawns that object.",
            "Scene command 0x07 loads the route-local dungeon/field keep object as the sub keep.",
        ],
    }


def route_audio_summary(romfs_root: Path) -> dict[str, object]:
    try:
        audio = audit_audio_assets(romfs_root, include_records=True, sample_limit=20)
    except Exception as exc:
        return {"file_count": 0, "issue_count": 1, "error": str(exc)}
    return {
        "file_count": audio["file_count"],
        "total_size": audio["total_size"],
        "extension_counts": audio["extension_counts"],
        "issue_count": audio["issue_count"],
        "records": [
            {
                "path": record["path"],
                "extension": record["extension"],
                "magic": record["magic"],
                "section_count": record["section_count"],
                "sections": [
                    {
                        "type": section["type"],
                        "magic": section["magic"],
                        "size": section["size"],
                    }
                    for section in record["sections"]
                ],
            }
            for record in audio.get("records", [])
        ],
    }


def route_audio_runtime_mapping(
    sound_settings: list[dict[str, object]],
    audio_summary: dict[str, object],
    *,
    sample_limit: int,
) -> dict[str, object]:
    records = []
    profile_counts: Counter[tuple[str, int, int, int]] = Counter()
    profile_records: dict[tuple[str, int, int, int], dict[str, object]] = {}
    mapped_record_count = 0
    unresolved_record_count = 0

    for record in sound_settings:
        mapped = map_route_sound_setting(record)
        key = (
            str(record.get("scene_stem", "")),
            int(record.get("spec_id", 0)),
            int(record.get("nature_ambience_id", 0)),
            int(record.get("bgm_sound_id", 0)),
        )
        profile_counts[key] += 1
        profile_records.setdefault(key, mapped)
        if mapped["status"] == "mapped":
            mapped_record_count += 1
        else:
            unresolved_record_count += 1
        if len(records) < sample_limit:
            records.append(mapped)

    profiles = []
    unresolved_profile_count = 0
    for key, count in sorted(profile_counts.items()):
        profile = dict(profile_records[key])
        profile["record_count"] = count
        profiles.append(profile)
        if profile["status"] != "mapped":
            unresolved_profile_count += 1

    return {
        "status": "mapped" if unresolved_record_count == 0 else "unresolved",
        "sound_setting_count": len(sound_settings),
        "mapped_record_count": mapped_record_count,
        "unresolved_record_count": unresolved_record_count,
        "profile_count": len(profiles),
        "mapped_profile_count": len(profiles) - unresolved_profile_count,
        "unresolved_profile_count": unresolved_profile_count,
        "profiles": profiles[:sample_limit],
        "records": records,
        "oot3d_audio_asset_file_count": int(audio_summary.get("file_count", 0)),
        "oot3d_audio_asset_status": (
            "audited_not_decoded"
            if int(audio_summary.get("file_count", 0)) > 0
            else "missing"
        ),
        "evidence": ROUTE_AUDIO_MAPPING_EVIDENCE,
    }


def map_route_sound_setting(record: dict[str, object]) -> dict[str, object]:
    scene_stem = str(record.get("scene_stem", ""))
    spec_id = int(record.get("spec_id", 0))
    nature_ambience_id = int(record.get("nature_ambience_id", 0))
    data3 = int(record.get("data3", 0))
    bgm_sound_id = int(record.get("bgm_sound_id", 0))
    valid_sound_id = (
        bgm_sound_id in {0, 0x7F}
        or (bgm_sound_id & 0xFF000000) == 0x01000000
    )

    mapped = {
        "path": record.get("path"),
        "scene_stem": scene_stem,
        "setup_index": record.get("setup_index"),
        "oot3d_sound_settings": {
            "spec_id": spec_id,
            "nature_ambience_id": nature_ambience_id,
            "data3": data3,
            "bgm_sound_id": bgm_sound_id,
            "bgm_sound_id_hex": f"0x{bgm_sound_id:08x}",
        },
        "mapping_reason": (
            "native_zsi_sound_settings"
            if valid_sound_id
            else "invalid_native_bcsar_sound_id"
        ),
    }
    if not valid_sound_id:
        mapped.update(
            {
                "status": "unresolved",
                "native_audio_settings": None,
            }
        )
    else:
        mapped.update(
            {
                "status": "mapped",
                "native_audio_settings": {
                    "sound_spec_id": spec_id,
                    "nature_ambience_id": nature_ambience_id,
                    "bgm_sound_id": bgm_sound_id,
                    "consumer": "NativeAudioService::ApplySceneAudio",
                },
            }
        )
    return mapped


def route_kankyo_runtime_selection(
    skybox_settings: list[dict[str, object]],
    kankyo_summary: dict[str, object],
) -> dict[str, object]:
    archive_names = {
        str(name)
        for name in kankyo_summary.get("archive_names", [])
    }
    profile_counts: Counter[tuple[int, int, int]] = Counter()
    profile_paths: dict[tuple[int, int, int], list[dict[str, object]]] = {}
    for record in skybox_settings:
        profile = (
            int(record.get("skybox_id", 0)),
            int(record.get("weather_or_unk_05", 0)),
            int(record.get("indoors", 0)),
        )
        profile_counts[profile] += 1
        profile_paths.setdefault(profile, []).append(
            {
                "path": record.get("path"),
                "setup_index": record.get("setup_index"),
            }
        )

    records: list[dict[str, object]] = []
    status_counts: Counter[str] = Counter()
    unresolved_selection_count = 0
    for profile, count in sorted(profile_counts.items()):
        skybox_id, weather_or_unk_05, indoors = profile
        record = skybox_runtime_selection_record(
            skybox_id,
            weather_or_unk_05,
            indoors,
            count=count,
            locations=profile_paths.get(profile, []),
            archive_names=archive_names,
            kankyo_summary=kankyo_summary,
        )
        records.append(record)
        status = str(record["status"])
        status_counts[status] += 1
        if status.startswith("unresolved_"):
            unresolved_selection_count += 1

    return {
        "status": (
            "selected"
            if records and unresolved_selection_count == 0
            else "unresolved"
            if unresolved_selection_count
            else "no_skybox_settings"
        ),
        "profile_count": len(records),
        "status_counts": sorted_counter(status_counts),
        "unresolved_selection_count": unresolved_selection_count,
        "records": records,
    }


def skybox_runtime_selection_record(
    skybox_id: int,
    weather_or_unk_05: int,
    indoors: int,
    *,
    count: int,
    locations: list[dict[str, object]],
    archive_names: set[str],
    kankyo_summary: dict[str, object],
) -> dict[str, object]:
    base: dict[str, object] = {
        "skybox_id": skybox_id,
        "skybox_name": skybox_name(skybox_id),
        "weather_or_unk_05": weather_or_unk_05,
        "indoors": indoors,
        "setup_count": count,
        "locations": locations,
    }

    if skybox_id == 0x00:
        return {
            **base,
            "status": "n64_no_skybox_no_kankyo_resource_needed",
            "runtime_selection": {
                "kind": "none",
                "fallback": "shipwright_n64_skybox_none",
            },
            "evidence": [
                "SKYBOX_NONE does not request a sky texture in the N64 skybox path.",
            ],
        }

    if skybox_id == 0x1D:
        return {
            **base,
            "status": "n64_filter_only_no_kankyo_resource_needed",
            "runtime_selection": {
                "kind": "n64_filter_only",
                "fallback": "shipwright_n64_fog_color_filter",
            },
            "evidence": [
                "N64 SKYBOX_UNSET_1D is documented as unused by the original game.",
                "N64 Environment_DrawSkyboxFilters treats SKYBOX_UNSET_1D as a fog-color full-screen filter.",
            ],
        }

    if skybox_id == 0x01:
        missing_archives = [
            archive
            for archive in [NORMAL_SKY_KANKYO_ARCHIVE]
            if archive not in archive_names
        ]
        if missing_archives:
            return {
                **base,
                "status": "unresolved_missing_kankyo_archive_candidate",
                "missing_archive_candidates": missing_archives,
                "runtime_selection": {
                    "kind": "oot3d_kankyo_archive_candidate",
                    "archive_candidates": [NORMAL_SKY_KANKYO_ARCHIVE],
                },
            }
        return {
            **base,
            "status": "selected_kankyo_archive_candidate",
            "runtime_selection": {
                "kind": "oot3d_kankyo_archive_candidate",
                "archive_candidates": [NORMAL_SKY_KANKYO_ARCHIVE],
                "environment_groups": [
                    "tenkyu_sky_dome",
                    "kumo_cloud",
                    "sun",
                    "star",
                ],
            },
            "evidence": [
                "N64 SKYBOX_NORMAL_SKY uses time-based fine/cloud skybox file indices.",
                "OOT3D kankyo BlueSky.zar contains fine/cloud tenkyu, cloud, sun, and star CMB candidates.",
            ],
        }

    return {
        **base,
        "status": "unresolved_oot3d_kankyo_mapping",
        "runtime_selection": {
            "kind": "unknown",
            "available_kankyo_archive_count": kankyo_summary.get("archive_count", 0),
            "available_kankyo_archives": sorted(archive_names),
        },
        "evidence": [
            "This skybox id is not yet mapped to an OOT3D kankyo archive for Kokiri runtime routing.",
        ],
    }


def skybox_name(skybox_id: int) -> str:
    return SKYBOX_ID_NAMES.get(skybox_id, f"SKYBOX_0x{skybox_id:02x}")


def semantic_id_by_name(
    semantic_names: dict[str, dict[int, str]],
    kind: str,
    name: str,
) -> int | None:
    for semantic_id, semantic_name in semantic_names.get(kind, {}).items():
        if semantic_name == name:
            return semantic_id
    return None


def sorted_positive_counter(counter: Counter[str]) -> dict[str, int]:
    return {
        key: counter[key]
        for key in sorted(counter)
        if counter[key] > 0
    }


def route_kankyo_summary(romfs_root: Path) -> dict[str, object]:
    kankyo_root = romfs_root / "kankyo"
    try:
        kankyo = audit_kankyo_environment_assets(kankyo_root, include_records=False, sample_limit=20)
    except Exception as exc:
        return {"archive_count": 0, "issue_count": 1, "error": str(exc)}
    return {
        "archive_count": kankyo["archive_count"],
        "archive_names": sorted(path.name for path in kankyo_root.glob("*.zar")),
        "archive_file_count_total": kankyo["archive_file_count_total"],
        "embedded_type_counts": kankyo["embedded_type_counts"],
        "environment_model_group_counts": kankyo["environment_model_group_counts"],
        "cmab_count": kankyo["cmab_count"],
        "ctxb_count": kankyo["ctxb_count"],
        "tbd_count": kankyo["tbd_count"],
        "archive_parse_error_count": kankyo["archive_parse_error_count"],
        "cmb_parse_error_count": len(kankyo["cmb_parse_errors"]),
    }


def load_semantic_names(path: Path | None) -> dict[str, dict[int, str]]:
    names: dict[str, dict[int, str]] = {"actor": {}, "object": {}}
    if path is None or not path.is_file():
        return names
    current: str | None = None
    for line in path.read_text(encoding="utf-8").splitlines():
        if "typedef enum Oot3dActorId" in line:
            current = "actor"
            continue
        if "typedef enum Oot3dObjectId" in line:
            current = "object"
            continue
        if current is not None and line.strip().startswith("}"):
            current = None
            continue
        if current is None:
            continue
        match = re.match(r"\s*([A-Z0-9_]+)\s*=\s*(0x[0-9A-Fa-f]+|\d+),", line)
        if match is None:
            continue
        names[current][int(match.group(2), 0)] = match.group(1)
    return names


def read_s8(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 1], "little", signed=True)


def read_s16(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 2], "little", signed=True)


def read_u32(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 4], "little")
