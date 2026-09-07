from __future__ import annotations

import json
import re
from collections import Counter
from pathlib import Path
from typing import Any

from .binary import ParseError
from .romfs_inventory import sorted_counter

AUDIT_FORMAT = "oot3d_kokiri_runtime_route_audit_v4"
MANIFEST_FORMAT = "oot3d_kokiri_runtime_manifest_v1"
DEFAULT_ROUTE_ID = "link_house_to_kokiri_forest"
DEFAULT_N64_DECOMP_REFERENCE = "https://github.com/zeldaret/oot"
RUNTIME_FALLBACK_ONLY = "n64_fallback_only"
RUNTIME_ENABLED = "oot3d_runtime_enabled"
RUNTIME_ENABLEMENT_STATUSES = {
    RUNTIME_FALLBACK_ONLY,
    RUNTIME_ENABLED,
}

N64_FALLBACK_SCENES = {
    "link": {
        "n64_scene_stem": "link_home",
        "shipwright_scene_root": "scenes/indoors/link_home",
        "shipwright_scene_resource": "scenes/indoors/link_home/link_home_scene",
    },
    "spot04": {
        "n64_scene_stem": "spot04",
        "shipwright_scene_root": "scenes/overworld/spot04",
        "shipwright_scene_resource": "scenes/overworld/spot04/spot04_scene",
    },
}

ROOM_INFO_RE = re.compile(r"^(?P<stem>[A-Za-z0-9]+)_(?P<room_index>\d+)_info\.zsi$")


def export_kokiri_runtime_manifest(
    audit_path: Path,
    output_path: Path,
    *,
    route_id: str = DEFAULT_ROUTE_ID,
    n64_rom: str | None = None,
    n64_decomp_reference: str = DEFAULT_N64_DECOMP_REFERENCE,
    runtime_enablement_status: str = RUNTIME_FALLBACK_ONLY,
    sample_limit: int = 20,
) -> dict[str, object]:
    if not audit_path.is_file():
        raise ParseError(f"{audit_path}: Kokiri runtime route audit not found")
    audit = json.loads(audit_path.read_text(encoding="utf-8"))
    manifest = build_kokiri_runtime_manifest(
        audit,
        audit_path=audit_path,
        route_id=route_id,
        n64_rom=n64_rom,
        n64_decomp_reference=n64_decomp_reference,
        runtime_enablement_status=runtime_enablement_status,
        sample_limit=sample_limit,
    )
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(manifest, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    return manifest


def build_kokiri_runtime_manifest(
    audit: dict[str, Any],
    *,
    audit_path: Path | None = None,
    route_id: str = DEFAULT_ROUTE_ID,
    n64_rom: str | None = None,
    n64_decomp_reference: str = DEFAULT_N64_DECOMP_REFERENCE,
    runtime_enablement_status: str = RUNTIME_FALLBACK_ONLY,
    sample_limit: int = 20,
) -> dict[str, object]:
    audit_format = audit.get("format")
    if audit_format != AUDIT_FORMAT:
        raise ParseError(f"expected {AUDIT_FORMAT}, got {audit_format!r}")

    route_files = [route_file_record(record) for record in audit.get("records", [])]
    setup_bindings = setup_binding_records(audit, sample_limit=sample_limit)
    resource_targets = route_resource_targets(route_files)
    runtime_blocker_counts = runtime_blockers(audit)
    runtime_enablement_status = validated_runtime_enablement_status(
        runtime_enablement_status,
        runtime_blocker_counts,
    )

    manifest: dict[str, object] = {
        "format": MANIFEST_FORMAT,
        "route_id": route_id,
        "runtime_enablement_status": runtime_enablement_status,
        "source_audit": {
            "path": str(audit_path) if audit_path is not None else None,
            "format": audit_format,
            "romfs_root": audit.get("romfs_root"),
            "scene_root": audit.get("scene_root"),
        },
        "reference_policy": {
            "primary_behavior_reference": "oot_n64_decomp",
            "n64_rom": n64_rom,
            "n64_decomp_reference": n64_decomp_reference,
            "oot3d_behavior_assumption": (
                "Treat OOT3D behavior as equal to or derived from OOT N64 unless "
                "a route-local delta is proven."
            ),
        },
        "route_scope": {
            "oot3d_scene_stems": audit.get("route_stems", []),
            "required_oot3d_scene_files": audit.get("required_scene_files", []),
            "unique_oot3d_room_references": audit.get("unique_room_references", []),
            "shipwright_n64_fallbacks": [
                {"oot3d_stem": stem, **fallback}
                for stem, fallback in sorted(N64_FALLBACK_SCENES.items())
            ],
        },
        "runtime_policy": [
            "Shipwright N64 resources remain default until an explicit OOT3D route gate is enabled.",
            "The route must fall back per subsystem when a generated resource, layout validation, or mapping is missing.",
            "Generated OOT3D asset payloads stay outside git and are referenced only through manifests and local packages.",
        ],
        "readiness": readiness_summary(audit, runtime_blocker_counts),
        "runtime_blocker_counts": runtime_blocker_counts,
        "route_files": route_files,
        "resource_targets": resource_targets,
        "setup_bindings": setup_bindings,
        "audio_summary": audio_manifest_summary(audit),
        "kankyo_summary": kankyo_manifest_summary(audit),
        "fallback_policy": audit.get("fallback_policy", []),
    }
    return manifest


def readiness_summary(
    audit: dict[str, Any],
    runtime_blocker_counts: dict[str, int],
) -> dict[str, object]:
    mesh_ready = (
        int(audit.get("missing_required_file_count", 0)) == 0
        and int(audit.get("embedded_cmb_total", 0)) >= int(audit.get("room_file_count", 0))
        and int(audit.get("room_reference_count", 0)) > 0
    )
    collision_has_candidates = int(audit.get("collision_candidate_total", 0)) > 0
    transition_has_candidates = int(audit.get("transition_actor_count", 0)) > 0
    spawn_has_candidates = int(audit.get("spawn_candidate_command_count", 0)) > 0
    audio_has_candidates = int(audit.get("sound_setting_count", 0)) > 0
    audio_mapping = audit.get("audio_runtime_mapping", {})
    audio_unresolved_profile_count = int(
        audio_mapping.get("unresolved_profile_count", 0)
        if isinstance(audio_mapping, dict)
        else 0
    )
    kankyo_has_candidates = int(audit.get("skybox_setting_count", 0)) > 0
    kankyo_selection = audit.get("kankyo_runtime_selection", {})
    kankyo_unresolved_selection_count = int(
        kankyo_selection.get("unresolved_selection_count", 0)
        if isinstance(kankyo_selection, dict)
        else 0
    )
    actor_object_binding = audit.get("actor_object_binding_summary", {})
    actor_object_status = (
        actor_object_binding.get("status")
        if isinstance(actor_object_binding, dict)
        else None
    )
    layout_validation_counts = audit.get("layout_validation_counts", {})
    validated_layout_count = sum(
        int(value)
        for key, value in layout_validation_counts.items()
        if str(key).startswith("validated_")
    )
    layout_blocker_total = sum(
        int(value)
        for key, value in runtime_blocker_counts.items()
        if str(key)
        in {
            "needs_oot3d_spawn_list_layout_validation",
            "needs_oot3d_entrance_list_layout_validation",
            "needs_oot3d_path_list_layout_validation",
            "needs_oot3d_exit_list_layout_validation",
        }
    )
    if layout_blocker_total == 0:
        scene_layout_status = "layout_validated"
    elif validated_layout_count:
        scene_layout_status = "layout_candidates_partially_validated"
    else:
        scene_layout_status = "layout_candidates_need_runtime_validation"
    if kankyo_has_candidates and kankyo_unresolved_selection_count == 0:
        sky_environment_status = "skybox_runtime_selection_ready"
    elif kankyo_has_candidates:
        sky_environment_status = "skybox_settings_need_kankyo_runtime_selection"
    else:
        sky_environment_status = "route_environment_candidates_missing"
    actor_status = actor_readiness_status(
        transition_has_candidates=transition_has_candidates,
        spawn_has_candidates=spawn_has_candidates,
        actor_object_status=actor_object_status,
        standard_actor_list_command_count=int(
            audit.get("standard_actor_list_command_count", 0)
        ),
        selected_room_actor_list_count=int(
            audit.get("selected_room_actor_list_count", 0)
        ),
    )

    return {
        "mesh": {
            "status": "source_candidate_ready" if mesh_ready else "source_candidate_missing",
            "embedded_cmb_total": audit.get("embedded_cmb_total", 0),
            "room_file_count": audit.get("room_file_count", 0),
            "fallback": "shipwright_n64_scene_mesh",
        },
        "collision": {
            "status": (
                "source_candidate_needs_surface_runtime_validation"
                if collision_has_candidates
                else "source_candidate_missing"
            ),
            "candidate_total": audit.get("collision_candidate_total", 0),
            "effective_polygon_total": audit.get("collision_effective_polygon_total", 0),
            "surface_type_total": audit.get("collision_surface_type_total", 0),
            "bgcam_total": audit.get("bgcam_total", 0),
            "water_box_total": audit.get("water_box_total", 0),
            "fallback": "shipwright_n64_collision",
        },
        "scene_layout": {
            "status": scene_layout_status,
            "spawn_candidate_command_count": audit.get("spawn_candidate_command_count", 0),
            "spawn_entry_candidate_total": audit.get("spawn_entry_candidate_total", 0),
            "spawn_player_candidate_total": audit.get("spawn_player_candidate_total", 0),
            "spawn_payload_profile_counts": audit.get("spawn_payload_profile_counts", {}),
            "entrance_entry_candidate_total": audit.get("entrance_entry_candidate_total", 0),
            "layout_validation_counts": layout_validation_counts,
            "path_block_candidate_count": audit.get("path_block_candidate_count", 0),
            "exit_value_candidate_total": audit.get("exit_value_candidate_total", 0),
            "fallback": "shipwright_n64_scene_setup",
        },
        "actors": {
            "status": actor_status,
            "transition_actor_count": audit.get("transition_actor_count", 0),
            "transition_actor_name_counts": audit.get("transition_actor_name_counts", {}),
            "standard_actor_list_command_count": audit.get("standard_actor_list_command_count", 0),
            "object_list_command_count": audit.get("object_list_command_count", 0),
            "room_actor_list_candidate_count": audit.get("room_actor_list_candidate_count", 0),
            "selected_room_actor_list_count": audit.get("selected_room_actor_list_count", 0),
            "selected_room_actor_entry_total": audit.get("selected_room_actor_entry_total", 0),
            "selected_room_actor_name_counts": audit.get(
                "selected_room_actor_name_counts", {}
            ),
            "selected_room_actor_object_name_counts": audit.get(
                "selected_room_actor_object_name_counts", {}
            ),
            "actor_object_binding_summary": actor_object_binding,
            "fallback": "shipwright_n64_actor_objects_and_behavior",
        },
        "animations": {
            "status": animation_readiness_status(actor_status),
            "fallback": "shipwright_n64_actor_animation_assets",
        },
        "audio": {
            "status": audio_readiness_status(
                audio_has_candidates=audio_has_candidates,
                unresolved_profile_count=audio_unresolved_profile_count,
            ),
            "sound_setting_count": audit.get("sound_setting_count", 0),
            "mapped_profile_count": (
                audio_mapping.get("mapped_profile_count", 0)
                if isinstance(audio_mapping, dict)
                else 0
            ),
            "unresolved_profile_count": audio_unresolved_profile_count,
            "consumer": "oot3d_native_audio_service",
        },
        "sky_environment": {
            "status": sky_environment_status,
            "skybox_setting_count": audit.get("skybox_setting_count", 0),
            "kankyo_archive_count": audit.get("kankyo_asset_summary", {}).get("archive_count", 0),
            "kankyo_selection_status_counts": (
                kankyo_selection.get("status_counts", {})
                if isinstance(kankyo_selection, dict)
                else {}
            ),
            "kankyo_unresolved_selection_count": kankyo_unresolved_selection_count,
            "fallback": "shipwright_n64_sky_environment",
        },
        "runtime_enablement": {
            "status": "blocked" if sum(runtime_blocker_counts.values()) else "ready_for_opt_in",
            "blocker_total": sum(runtime_blocker_counts.values()),
        },
    }


def actor_readiness_status(
    *,
    transition_has_candidates: bool,
    spawn_has_candidates: bool,
    actor_object_status: object,
    standard_actor_list_command_count: int,
    selected_room_actor_list_count: int = 0,
) -> str:
    if (
        not transition_has_candidates
        and not spawn_has_candidates
        and not selected_room_actor_list_count
    ):
        return "route_actor_candidates_missing"
    if actor_object_status == "route_actor_object_bindings_resolved":
        if standard_actor_list_command_count == 0:
            if selected_room_actor_list_count:
                return "room_actor_list_runtime_routing_ready"
            return "transition_actor_object_bindings_ready_actor_list_pending"
        return "route_actor_object_bindings_ready"
    return "transition_actor_candidates_need_object_binding"


def animation_readiness_status(actor_status: str) -> str:
    if actor_status == "room_actor_list_runtime_routing_ready":
        return "route_actor_bindings_ready_animation_assets_pending"
    return "blocked_until_route_actor_bindings_are_selected"


def audio_readiness_status(
    *,
    audio_has_candidates: bool,
    unresolved_profile_count: int,
) -> str:
    if not audio_has_candidates:
        return "route_audio_candidates_missing"
    if unresolved_profile_count:
        return "sound_settings_need_audio_entry_mapping"
    return "native_oot3d_sound_settings_ready"


def runtime_blockers(audit: dict[str, Any]) -> dict[str, int]:
    gap_counts = audit.get("runtime_gap_counts", {})
    blockers = {
        key: int(value)
        for key, value in gap_counts.items()
        if int(value) > 0
    }
    return {key: blockers[key] for key in sorted(blockers)}


def validated_runtime_enablement_status(
    status: str,
    runtime_blocker_counts: dict[str, int],
) -> str:
    if status not in RUNTIME_ENABLEMENT_STATUSES:
        raise ParseError(f"unsupported runtime enablement status {status!r}")
    blocker_total = sum(runtime_blocker_counts.values())
    if status == RUNTIME_ENABLED and blocker_total:
        raise ParseError(
            "cannot promote Kokiri runtime manifest with "
            f"{blocker_total} blocker(s): {runtime_blocker_counts}"
        )
    return status


def route_file_record(record: dict[str, Any]) -> dict[str, object]:
    fallback = fallback_for_record(record)
    return {
        "path": record.get("path"),
        "oot3d_source": f"scene/{record.get('path')}",
        "role": record.get("role"),
        "scene_stem": record.get("scene_stem"),
        "size": record.get("size"),
        "fallback": fallback,
        "setup_count": record.get("setup_count", 0),
        "embedded_cmb_count": record.get("embedded_cmb_count", 0),
        "embedded_cmbs": [
            {
                "index": cmb.get("index"),
                "offset": cmb.get("offset"),
                "size": cmb.get("size"),
                "model_name": cmb.get("model_name"),
                "mesh_count": cmb.get("mesh_count"),
                "material_count": cmb.get("material_count"),
                "texture_count": cmb.get("texture_count"),
                "bone_count": cmb.get("bone_count"),
            }
            for cmb in record.get("embedded_cmbs", [])
        ],
        "collision_candidate_count": record.get("collision_candidate_count", 0),
        "collision_candidates": [
            compact_collision_candidate(candidate)
            for candidate in record.get("collision_header_candidates", [])
        ],
        "room_actor_list_candidate_count": record.get("room_actor_list_candidate_count", 0),
        "selected_room_actor_list_candidate": compact_room_actor_list_candidate(
            record.get("selected_room_actor_list_candidate"),
            full_entries=True,
        ),
    }


def compact_collision_candidate(candidate: dict[str, Any]) -> dict[str, object]:
    return {
        "setup_index": candidate.get("setup_index"),
        "command_argument": candidate.get("command_argument"),
        "offset": candidate.get("offset"),
        "bounds_min": candidate.get("bounds_min"),
        "bounds_max": candidate.get("bounds_max"),
        "vertex_count": candidate.get("vertex_count"),
        "effective_polygon_count": candidate.get("effective_polygon_count"),
        "surface_type_count": candidate.get("surface_type_count"),
        "bgcam_count": candidate.get("bgcam_count"),
        "water_box_count": candidate.get("water_box_count"),
        "evidence": candidate.get("evidence", []),
    }


def fallback_for_record(record: dict[str, Any]) -> dict[str, object]:
    path = str(record.get("path", ""))
    stem = str(record.get("scene_stem", ""))
    fallback = N64_FALLBACK_SCENES.get(stem)
    if fallback is None:
        return {"status": "unknown_n64_fallback_scene"}

    if record.get("role") == "scene":
        return {
            "status": "shipwright_n64_default",
            "shipwright_scene_root": fallback["shipwright_scene_root"],
            "shipwright_resource": fallback["shipwright_scene_resource"],
        }

    match = ROOM_INFO_RE.match(path)
    if match is None:
        return {
            "status": "shipwright_n64_default",
            "shipwright_scene_root": fallback["shipwright_scene_root"],
        }
    room_index = int(match.group("room_index"))
    return {
        "status": "shipwright_n64_default",
        "shipwright_scene_root": fallback["shipwright_scene_root"],
        "shipwright_resource": (
            f"{fallback['shipwright_scene_root']}/"
            f"{fallback['n64_scene_stem']}_room_{room_index}"
        ),
        "room_index": room_index,
    }


def route_resource_targets(route_files: list[dict[str, object]]) -> list[dict[str, object]]:
    targets: list[dict[str, object]] = []
    for record in route_files:
        fallback = record.get("fallback", {})
        if not isinstance(fallback, dict):
            fallback = {}
        resource = fallback.get("shipwright_resource")
        if record.get("role") == "room" and record.get("embedded_cmb_count", 0):
            targets.append(
                {
                    "kind": "room_mesh",
                    "oot3d_source": record.get("oot3d_source"),
                    "embedded_cmb_count": record.get("embedded_cmb_count"),
                    "shipwright_resource": resource,
                    "runtime_status": "source_candidate_identified",
                    "fallback": "shipwright_n64_default",
                }
            )
        selected_room_actor = record.get("selected_room_actor_list_candidate")
        if record.get("role") == "room" and selected_room_actor:
            object_prefix = selected_room_actor.get("object_prefix", {})
            if not isinstance(object_prefix, dict):
                object_prefix = {}
            targets.append(
                {
                    "kind": "room_actor_list",
                    "oot3d_source": record.get("oot3d_source"),
                    "shipwright_resource": resource,
                    "entry_count": selected_room_actor.get("entry_count"),
                    "object_id_count": selected_room_actor.get("object_id_count"),
                    "object_ids": object_prefix.get("object_ids", []),
                    "actor_entries": selected_room_actor.get("entries", []),
                    "runtime_status": "source_candidate_identified_routing_pending",
                    "fallback": "shipwright_n64_default",
                }
            )
        if record.get("role") == "scene" and record.get("collision_candidate_count", 0):
            targets.append(
                {
                    "kind": "scene_collision",
                    "oot3d_source": record.get("oot3d_source"),
                    "collision_candidate_count": record.get("collision_candidate_count"),
                    "shipwright_scene_resource": resource,
                    "runtime_status": "source_candidate_needs_surface_validation",
                    "fallback": "shipwright_n64_default",
                }
            )
    return targets


def compact_room_actor_list_candidate(
    candidate: Any,
    *,
    full_entries: bool = False,
) -> dict[str, object] | None:
    if not isinstance(candidate, dict):
        return None
    object_prefix = candidate.get("object_prefix", {})
    if not isinstance(object_prefix, dict):
        object_prefix = {}
    return {
        "status": candidate.get("status"),
        "confidence": candidate.get("confidence"),
        "start_offset": candidate.get("start_offset"),
        "entry_count": candidate.get("entry_count"),
        "score": candidate.get("score"),
        "object_id_count": candidate.get("object_id_count"),
        "unknown_object_id_count": candidate.get("unknown_object_id_count"),
        "actor_name_counts": candidate.get("actor_name_counts", {}),
        "object_name_counts": candidate.get("object_name_counts", {}),
        "object_prefix": {
            "start_offset": object_prefix.get("start_offset"),
            "byte_count": object_prefix.get("byte_count"),
            "object_ids": (
                object_prefix.get("object_ids", [])
                if full_entries
                else object_prefix.get("object_ids", [])[:8]
            ),
            "unknown_object_ids": object_prefix.get("unknown_object_ids", [])[:4],
            "zero_padding": object_prefix.get("zero_padding", [])[:4],
        },
        "entries": (
            candidate.get("entries", [])
            if full_entries
            else candidate.get("entries", [])[:3]
        ),
    }


def setup_binding_records(
    audit: dict[str, Any],
    *,
    sample_limit: int,
) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    for file_record in audit.get("records", []):
        path = file_record.get("path")
        stem = file_record.get("scene_stem")
        for setup in file_record.get("scene_setups", []):
            commands = setup.get("commands", [])
            transition_actors = [
                actor
                for command in commands
                for actor in command.get("transition_actors", [])
            ]
            transition_actor_counts = Counter(
                actor.get("actor_name", "unknown")
                for actor in transition_actors
                if actor.get("actor_id") not in (0, None)
            )
            records.append(
                {
                    "path": path,
                    "scene_stem": stem,
                    "setup_index": setup.get("index"),
                    "command_count": setup.get("command_count"),
                    "room_references": [
                        room_ref
                        for command in commands
                        for room_ref in command.get("room_references", [])
                    ],
                    "spawn": compact_spawn_binding(commands),
                    "entrance": compact_entrance_binding(commands),
                    "path_list": compact_path_binding(commands),
                    "exit_list": compact_exit_binding(commands),
                    "transition_actor_count": len(transition_actors),
                    "transition_actor_name_counts": sorted_counter(transition_actor_counts),
                    "transition_actor_samples": transition_actors[:sample_limit],
                    "sound_settings": first_payload(commands, "sound_settings"),
                    "skybox_settings": first_payload(commands, "skybox_settings"),
                    "misc_settings": first_payload(commands, "misc_settings"),
                    "cutscene_reference": first_payload(commands, "cutscene_reference"),
                    "fallback_policy": "shipwright_n64_setup_until_oot3d_layout_validated",
                }
            )
    return records


def compact_spawn_binding(commands: list[dict[str, Any]]) -> dict[str, object] | None:
    command = first_command(commands, "0x00")
    if command is None:
        return None
    selected = command.get("selected_spawn_list_candidate")
    return {
        "command_parameter": command.get("parameter"),
        "candidate_count": command.get("spawn_entry_candidate_count", 0),
        "player_candidate_count": command.get("spawn_player_candidate_count", 0),
        "selected": compact_actor_candidate(selected),
        "payload_profile": command.get("spawn_payload_profile"),
        "runtime_status": "layout_candidate_needs_validation",
    }


def compact_actor_candidate(candidate: dict[str, Any] | None) -> dict[str, object] | None:
    if not candidate:
        return None
    return {
        "start_delta": candidate.get("start_delta"),
        "confidence": candidate.get("confidence"),
        "entry_count": candidate.get("entry_count"),
        "expected_entry_count": candidate.get("expected_entry_count"),
        "fits_payload_window": candidate.get("fits_payload_window"),
        "actor_name_counts": candidate.get("actor_name_counts", {}),
        "entries": candidate.get("entries", [])[:3],
    }


def compact_entrance_binding(commands: list[dict[str, Any]]) -> dict[str, object] | None:
    command = first_command(commands, "0x06")
    if command is None:
        return None
    selected = command.get("selected_entrance_list_candidate")
    return {
        "command_parameter": command.get("parameter"),
        "candidate_count": command.get("entrance_entry_candidate_count", 0),
        "selected": compact_entrance_candidate(selected),
        "layout_validation": command.get("layout_validation", {}),
        "runtime_status": (
            "layout_validated"
            if str(command.get("layout_validation", {}).get("status", "")).startswith("validated_")
            else "layout_candidate_needs_validation"
        ),
    }


def compact_entrance_candidate(candidate: dict[str, Any] | None) -> dict[str, object] | None:
    if not candidate:
        return None
    return {
        "start_delta": candidate.get("start_delta"),
        "confidence": candidate.get("confidence"),
        "entry_count": candidate.get("entry_count"),
        "expected_entry_count": candidate.get("expected_entry_count"),
        "entries": candidate.get("entries", [])[:8],
    }


def compact_path_binding(commands: list[dict[str, Any]]) -> dict[str, object] | None:
    command = first_command(commands, "0x0d")
    if command is None:
        return None
    candidate = command.get("path_block_candidate")
    valid_offsets = candidate.get("valid_file_offset_u32s", []) if candidate else []
    return {
        "command_parameter": command.get("parameter"),
        "valid_file_offset_candidate_count": command.get("path_file_offset_candidate_count", 0),
        "valid_file_offset_candidates": valid_offsets[:8],
        "runtime_status": "layout_candidate_needs_validation",
    }


def compact_exit_binding(commands: list[dict[str, Any]]) -> dict[str, object] | None:
    command = first_command(commands, "0x13")
    if command is None:
        return None
    return {
        "candidate_count": command.get("exit_value_candidate_count", 0),
        "values": command.get("exit_values_candidate", [])[:16],
        "runtime_status": "layout_candidate_needs_validation",
    }


def first_command(commands: list[dict[str, Any]], command_id: str) -> dict[str, Any] | None:
    for command in commands:
        if command.get("command_id") == command_id:
            return command
    return None


def first_payload(commands: list[dict[str, Any]], key: str) -> object | None:
    for command in commands:
        if key in command:
            return command[key]
    return None


def audio_manifest_summary(audit: dict[str, Any]) -> dict[str, object]:
    audio = audit.get("audio_asset_summary", {})
    mapping = audit.get("audio_runtime_mapping", {})
    unresolved_profile_count = int(
        mapping.get("unresolved_profile_count", 0)
        if isinstance(mapping, dict)
        else 0
    )
    if mapping and unresolved_profile_count == 0:
        status = "native_oot3d_sound_settings_ready"
    elif audio.get("file_count", 0):
        status = "entry_mapping_pending"
    else:
        status = "missing"
    return {
        "status": status,
        "file_count": audio.get("file_count", 0),
        "extension_counts": audio.get("extension_counts", {}),
        "records": audio.get("records", []),
        "runtime_mapping": mapping,
        "consumer": "oot3d_native_audio_service",
    }


def kankyo_manifest_summary(audit: dict[str, Any]) -> dict[str, object]:
    kankyo = audit.get("kankyo_asset_summary", {})
    selection = audit.get("kankyo_runtime_selection", {})
    unresolved_selection_count = int(
        selection.get("unresolved_selection_count", 0)
        if isinstance(selection, dict)
        else 0
    )
    if selection and unresolved_selection_count == 0:
        status = "runtime_selection_ready"
    elif kankyo.get("archive_count", 0):
        status = "runtime_selection_pending"
    else:
        status = "missing"
    return {
        "status": status,
        "archive_count": kankyo.get("archive_count", 0),
        "archive_names": kankyo.get("archive_names", []),
        "archive_file_count_total": kankyo.get("archive_file_count_total", 0),
        "embedded_type_counts": kankyo.get("embedded_type_counts", {}),
        "environment_model_group_counts": kankyo.get("environment_model_group_counts", {}),
        "runtime_selection": selection,
        "fallback": "shipwright_n64_sky_environment",
    }
