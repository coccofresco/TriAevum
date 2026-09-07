from __future__ import annotations

from collections import Counter, defaultdict
import json
from pathlib import Path
from typing import Any

from .binary import ParseError
from .romfs_inventory import sorted_counter


ROUTE_OBJECT_ARCHIVE_OVERRIDES = {
    "OBJECT_BOX": "zelda_box.zar",
    "OBJECT_COW": "zelda_cow.zar",
    "OBJECT_DEKUBABA": "zelda_dekubaba.zar",
    "OBJECT_FA": "zelda_fa.zar",
    "OBJECT_GI_HEART": "zelda_gi_heart.zar",
    "OBJECT_GOROIWA": "zelda_goroiwa.zar",
    "OBJECT_GS": "zelda_gs.zar",
    "OBJECT_KANBAN": "zelda_kanban.zar",
    "OBJECT_KM1": "zelda_km1.zar",
    "OBJECT_KUSA": "zelda_kusa.zar",
    "OBJECT_KW1": "zelda_kw1.zar",
    "OBJECT_MAMENOKI": "zelda_mamenoki.zar",
    "OBJECT_MD": "zelda_md.zar",
    "OBJECT_NIW": "zelda_nw.zar",
    "OBJECT_OS_ANIME": "zelda_os.zar",
    "OBJECT_SA": "zelda_sa.zar",
    "OBJECT_SPOT04_OBJECTS": "zelda_spo04_objects.zar",
    "OBJECT_ST": "zelda_st.zar",
    "OBJECT_TSUBO": "zelda_tsubo.zar",
}

ROUTE_BEHAVIOR_ONLY_ACTOR_STATUS = {
    "ACTOR_EN_ITEM00": "n64_collectible_item_behavior_fallback",
    "ACTOR_EN_RIVER_SOUND": "n64_audio_trigger_behavior_fallback",
    "ACTOR_EN_WONDER_ITEM": "n64_hidden_item_trigger_behavior_fallback",
    "ACTOR_EN_WONDER_TALK2": "n64_dialog_trigger_behavior_fallback",
    "ACTOR_OBJECT_KANKYO": "n64_environment_actor_behavior_fallback",
    "ACTOR_OBJ_MAKEKINSUTA": "n64_soil_spawner_behavior_fallback",
    "ACTOR_OBJ_MURE2": "n64_spawn_controller_behavior_fallback",
}


def audit_kokiri_actor_asset_readiness(
    runtime_manifest_path: Path,
    actor_inventory_path: Path,
    static_batch_manifest_path: Path,
    skinned_binding_manifest_path: Path,
    output_path: Path | None = None,
    *,
    sample_limit: int = 100,
) -> dict[str, object]:
    runtime_manifest = read_json_file(runtime_manifest_path, "Kokiri runtime manifest")
    actor_inventory = read_json_file(actor_inventory_path, "actor inventory")
    static_batch_manifest = read_json_file(static_batch_manifest_path, "static actor batch manifest")
    skinned_binding_manifest = read_json_file(
        skinned_binding_manifest_path,
        "skinned animation binding manifest",
    )

    room_actor_targets = [
        target
        for target in runtime_manifest.get("resource_targets", [])
        if isinstance(target, dict) and target.get("kind") == "room_actor_list"
    ]
    actor_counts, actor_rooms = collect_route_actor_counts(room_actor_targets)
    object_counts, object_rooms = collect_route_object_counts(room_actor_targets)

    archive_records = {
        str(record.get("path")): record
        for record in actor_inventory.get("archive_records", [])
        if isinstance(record, dict)
    }
    model_records_by_archive = records_by_archive(
        actor_inventory.get("model_records", []),
        "container_path",
    )
    static_records_by_archive = static_batch_records_by_archive(
        static_batch_manifest.get("records", [])
    )
    skinned_targets_by_archive = records_by_archive(
        skinned_binding_manifest.get("targets", []),
        "archive_path",
    )
    unused_bind_pose_by_archive = records_by_archive(
        skinned_binding_manifest.get("unused_bind_pose_targets", []),
        "archive_path",
    )

    object_records = []
    object_status_counts: Counter[str] = Counter()
    mapping_issue_counts: Counter[str] = Counter()
    for object_name in sorted(object_counts):
        record = route_object_asset_record(
            object_name,
            object_counts[object_name],
            sorted(object_rooms[object_name]),
            archive_records,
            model_records_by_archive,
            static_records_by_archive,
            skinned_targets_by_archive,
            unused_bind_pose_by_archive,
            sample_limit=sample_limit,
        )
        object_records.append(record)
        object_status_counts[str(record["asset_readiness_status"])] += 1
        if record["archive_mapping_status"] != "mapped":
            mapping_issue_counts[str(record["archive_mapping_status"])] += 1

    actor_records = []
    actor_status_counts: Counter[str] = Counter()
    for actor_name in sorted(actor_counts):
        behavior_status = ROUTE_BEHAVIOR_ONLY_ACTOR_STATUS.get(
            actor_name,
            "n64_visual_actor_behavior_fallback",
        )
        actor_status_counts[behavior_status] += 1
        actor_records.append(
            {
                "actor_name": actor_name,
                "entry_count": actor_counts[actor_name],
                "rooms": sorted(actor_rooms[actor_name]),
                "behavior_status": behavior_status,
                "runtime_asset_binding_status": "not_routed_uses_n64_actor_draw_fallback",
            }
        )

    blocker_counts = actor_asset_blocker_counts(object_records)
    audit: dict[str, object] = {
        "format": "oot3d_kokiri_actor_asset_readiness_v1",
        "runtime_manifest": str(runtime_manifest_path),
        "actor_inventory": str(actor_inventory_path),
        "static_batch_manifest": str(static_batch_manifest_path),
        "skinned_binding_manifest": str(skinned_binding_manifest_path),
        "route_id": runtime_manifest.get("route_id"),
        "runtime_enablement_status": runtime_manifest.get("runtime_enablement_status"),
        "route_room_actor_target_count": len(room_actor_targets),
        "route_actor_entry_count": sum(actor_counts.values()),
        "unique_route_actor_count": len(actor_counts),
        "route_object_prefix_reference_count": sum(object_counts.values()),
        "unique_route_object_count": len(object_counts),
        "mapped_route_object_count": len(
            [record for record in object_records if record["archive_mapping_status"] == "mapped"]
        ),
        "object_asset_readiness_status_counts": sorted_counter(object_status_counts),
        "actor_behavior_status_counts": sorted_counter(actor_status_counts),
        "archive_mapping_issue_counts": sorted_counter(mapping_issue_counts),
        "asset_blocker_counts": sorted_counter(blocker_counts),
        "object_records": object_records,
        "actor_records": actor_records[:sample_limit],
        "fallback_policy": [
            "This audit proves route-local offline asset coverage only.",
            "Shipwright actor behavior and draw calls still use N64 fallback until explicit runtime binding is implemented.",
            "No generated OOT3D asset payloads are written to git by this audit.",
        ],
    }
    if output_path is not None:
        write_json(output_path, audit)
    return audit


def route_object_asset_record(
    object_name: str,
    route_reference_count: int,
    rooms: list[str],
    archive_records: dict[str, dict[str, object]],
    model_records_by_archive: dict[str, list[dict[str, object]]],
    static_records_by_archive: dict[str, list[dict[str, object]]],
    skinned_targets_by_archive: dict[str, list[dict[str, object]]],
    unused_bind_pose_by_archive: dict[str, list[dict[str, object]]],
    *,
    sample_limit: int,
) -> dict[str, object]:
    archive_path = ROUTE_OBJECT_ARCHIVE_OVERRIDES.get(object_name)
    if archive_path is None:
        return {
            "object_name": object_name,
            "route_reference_count": route_reference_count,
            "rooms": rooms,
            "archive_mapping_status": "missing_route_object_archive_mapping",
            "asset_readiness_status": "missing_route_object_archive_mapping",
        }

    archive_record = archive_records.get(archive_path)
    if archive_record is None:
        return {
            "object_name": object_name,
            "route_reference_count": route_reference_count,
            "rooms": rooms,
            "archive_mapping_status": "archive_not_found_in_actor_inventory",
            "archive_path": archive_path,
            "asset_readiness_status": "archive_not_found_in_actor_inventory",
        }

    static_records = static_records_by_archive.get(archive_path, [])
    static_exported_records = [
        record for record in static_records if record.get("status") == "converted"
    ]
    static_skipped_records = [
        record for record in static_records if record.get("status") == "skipped"
    ]
    skinned_targets = skinned_targets_by_archive.get(archive_path, [])
    unused_bind_pose_targets = unused_bind_pose_by_archive.get(archive_path, [])
    model_records = model_records_by_archive.get(archive_path, [])
    status = object_asset_readiness_status(
        static_exported_count=len(static_exported_records),
        skinned_target_count=len(skinned_targets),
        unused_bind_pose_count=len(unused_bind_pose_targets),
        archive_record=archive_record,
    )

    return {
        "object_name": object_name,
        "route_reference_count": route_reference_count,
        "rooms": rooms,
        "archive_mapping_status": "mapped",
        "archive_path": archive_path,
        "asset_readiness_status": status,
        "archive_support_status": archive_record.get("support_status"),
        "archive_counts": {
            "cmb_count": archive_record.get("cmb_count", 0),
            "static_cmb_count": archive_record.get("static_cmb_count", 0),
            "nonstatic_cmb_count": archive_record.get("nonstatic_cmb_count", 0),
            "known_animation_file_count": archive_record.get("known_animation_file_count", 0),
        },
        "model_count": len(model_records),
        "static_exported_model_count": len(static_exported_records),
        "static_skipped_model_count": len(static_skipped_records),
        "skinned_binding_target_count": len(skinned_targets),
        "skinned_animation_count": sum(
            int(target.get("animation_count", 0)) for target in skinned_targets
        ),
        "unused_bind_pose_target_count": len(unused_bind_pose_targets),
        "sample_static_exports": [
            compact_static_record(record) for record in static_exported_records[:sample_limit]
        ],
        "sample_skinned_targets": [
            compact_skinned_target(target) for target in skinned_targets[:sample_limit]
        ],
        "sample_unused_bind_pose_targets": [
            compact_unused_bind_pose(target)
            for target in unused_bind_pose_targets[:sample_limit]
        ],
    }


def object_asset_readiness_status(
    *,
    static_exported_count: int,
    skinned_target_count: int,
    unused_bind_pose_count: int,
    archive_record: dict[str, object],
) -> str:
    if static_exported_count and skinned_target_count:
        return "mixed_static_and_skinned_animation_bindings_ready"
    if skinned_target_count:
        return "skinned_animation_bindings_ready"
    if unused_bind_pose_count:
        return "skinned_bind_pose_ready_animation_tracks_unresolved"
    if static_exported_count:
        return "static_or_rigid_exports_ready"
    if int(archive_record.get("nonstatic_cmb_count", 0)):
        return "needs_skeleton_or_skinning_asset_support"
    if int(archive_record.get("cmb_count", 0)):
        return "model_inventory_only"
    return "no_model_assets"


def actor_asset_blocker_counts(object_records: list[dict[str, object]]) -> Counter[str]:
    counts: Counter[str] = Counter()
    for record in object_records:
        status = str(record.get("asset_readiness_status"))
        if status == "skinned_bind_pose_ready_animation_tracks_unresolved":
            counts["needs_skinned_animation_track_resolution"] += 1
        elif status in {
            "missing_route_object_archive_mapping",
            "archive_not_found_in_actor_inventory",
            "needs_skeleton_or_skinning_asset_support",
            "model_inventory_only",
            "no_model_assets",
        }:
            counts[status] += 1

        if status in {
            "mixed_static_and_skinned_animation_bindings_ready",
            "skinned_animation_bindings_ready",
            "static_or_rigid_exports_ready",
        }:
            counts["needs_shipwright_runtime_actor_asset_binding"] += 1
    return counts


def collect_route_actor_counts(
    room_actor_targets: list[dict[str, object]],
) -> tuple[Counter[str], dict[str, set[str]]]:
    counts: Counter[str] = Counter()
    rooms: dict[str, set[str]] = defaultdict(set)
    for target in room_actor_targets:
        room = str(target.get("oot3d_source", target.get("shipwright_resource", "<unknown>")))
        for entry in target.get("actor_entries", []):
            if not isinstance(entry, dict):
                continue
            actor_name = str(entry.get("actor_name", f"ACTOR_0x{int(entry.get('actor_id', 0)):04x}"))
            counts[actor_name] += 1
            rooms[actor_name].add(room)
    return counts, rooms


def collect_route_object_counts(
    room_actor_targets: list[dict[str, object]],
) -> tuple[Counter[str], dict[str, set[str]]]:
    counts: Counter[str] = Counter()
    rooms: dict[str, set[str]] = defaultdict(set)
    for target in room_actor_targets:
        room = str(target.get("oot3d_source", target.get("shipwright_resource", "<unknown>")))
        for record in target.get("object_ids", []):
            if not isinstance(record, dict):
                continue
            object_name = str(record.get("object_name", f"OBJECT_0x{int(record.get('object_id', 0)):04x}"))
            counts[object_name] += 1
            rooms[object_name].add(room)
    return counts, rooms


def records_by_archive(records: object, archive_field: str) -> dict[str, list[dict[str, object]]]:
    result: dict[str, list[dict[str, object]]] = defaultdict(list)
    if not isinstance(records, list):
        return result
    for record in records:
        if not isinstance(record, dict):
            continue
        archive = normalize_archive_path(record.get(archive_field))
        if archive:
            result[archive].append(record)
    return result


def static_batch_records_by_archive(records: object) -> dict[str, list[dict[str, object]]]:
    result: dict[str, list[dict[str, object]]] = defaultdict(list)
    if not isinstance(records, list):
        return result
    for record in records:
        if not isinstance(record, dict):
            continue
        archive = archive_from_static_source(record.get("source"))
        if archive:
            result[archive].append(record)
    return result


def archive_from_static_source(source: object) -> str:
    value = str(source or "").replace("\\", "/")
    if "!" in value:
        value = value.split("!", 1)[0]
    return value.rsplit("/", 1)[-1]


def normalize_archive_path(value: object) -> str:
    return str(value or "").replace("\\", "/").strip("/")


def compact_static_record(record: dict[str, object]) -> dict[str, object]:
    summary = record.get("summary")
    return {
        "asset_id": record.get("asset_id"),
        "resource_root": record.get("resource_root"),
        "bone_count": summary.get("bone_count") if isinstance(summary, dict) else None,
        "resource_count": record.get("resource_count"),
    }


def compact_skinned_target(target: dict[str, object]) -> dict[str, object]:
    return {
        "target_id": target.get("target_id"),
        "target_cmb_name": target.get("target_cmb_name"),
        "model_name": target.get("model_name"),
        "bone_count": target.get("bone_count"),
        "animation_count": target.get("animation_count"),
        "bind_pose_package_entry": nested_get(target, ("bind_pose", "package_entry")),
        "support_status_counts": target.get("support_status_counts", {}),
    }


def compact_unused_bind_pose(target: dict[str, object]) -> dict[str, object]:
    return {
        "target_id": target.get("target_id"),
        "target_cmb_name": target.get("target_cmb_name"),
        "model_name": target.get("model_name"),
        "bone_count": target.get("bone_count"),
        "bind_pose_package_entry": target.get("bind_pose_package_entry"),
    }


def nested_get(mapping: dict[str, object], keys: tuple[str, ...]) -> Any:
    current: Any = mapping
    for key in keys:
        if not isinstance(current, dict):
            return None
        current = current.get(key)
    return current


def read_json_file(path: Path, label: str) -> dict[str, object]:
    if not path.is_file():
        raise ParseError(f"{label} not found: {path}")
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ParseError(f"{label} must be a JSON object: {path}")
    return value


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8", newline="\n")
