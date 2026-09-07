from __future__ import annotations

from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

from .kokiri_actor_asset_readiness import (
    ROUTE_BEHAVIOR_ONLY_ACTOR_STATUS,
    archive_from_static_source,
    read_json_file,
    write_json,
)
from .romfs_inventory import sorted_counter


READY_SINGLE_DISPLAY_LIST = "ready_single_display_list_binding"
READY_PARAM_SELECTED_DISPLAY_LIST = "ready_param_selected_display_list_binding"
READY_STATEFUL_DISPLAY_LIST = "ready_stateful_display_list_binding"

ACTOR_BINDING_RULES: dict[str, dict[str, object]] = {
    "ACTOR_BG_TREEMOUTH": {
        "binding_status": READY_SINGLE_DISPLAY_LIST,
        "object_name": "OBJECT_SPOT04_OBJECTS",
        "primary_asset_id": "zelda_spo04_objects_model_spot04_kuchi_model",
        "binding_kind": "single_opaque_display_list",
    },
    "ACTOR_EN_BOX": {
        "binding_status": READY_SINGLE_DISPLAY_LIST,
        "object_name": "OBJECT_BOX",
        "primary_asset_id": "zelda_box_model_tr_box",
        "binding_kind": "single_opaque_display_list",
    },
    "ACTOR_EN_GOROIWA": {
        "binding_status": READY_SINGLE_DISPLAY_LIST,
        "object_name": "OBJECT_GOROIWA",
        "primary_asset_id": "zelda_goroiwa_model_l_j_goroiwa_model",
        "binding_kind": "single_opaque_display_list",
    },
    "ACTOR_EN_GS": {
        "binding_status": READY_SINGLE_DISPLAY_LIST,
        "object_name": "OBJECT_GS",
        "primary_asset_id": "zelda_gs_model_gossip_stone2_model",
        "binding_kind": "single_opaque_display_list",
    },
    "ACTOR_OBJ_TSUBO": {
        "binding_status": READY_SINGLE_DISPLAY_LIST,
        "object_name": "OBJECT_TSUBO",
        "primary_asset_id": "zelda_tsubo_model_tubo2_model",
        "binding_kind": "single_opaque_display_list",
    },
    "ACTOR_DOOR_ANA": {
        "binding_status": READY_SINGLE_DISPLAY_LIST,
        "object_name": "OBJECT_GAMEPLAY_FIELD_KEEP",
        "primary_asset_id": "zelda_field_keep_model_ana01_modelt",
        "binding_kind": "single_translucent_display_list",
        "draw_layer": "translucent",
    },
    "ACTOR_EN_A_OBJ": {
        "binding_status": READY_PARAM_SELECTED_DISPLAY_LIST,
        "object_name": "OBJECT_GAMEPLAY_KEEP",
        "selector_mask": 0xFF,
        "selector_rules": (
            {
                "selector_value": 9,
                "asset_id": "zelda_keep_objects_model_kanban1_model",
                "n64_draw": "gSignRectangularDL",
                "required_object_name": "OBJECT_GAMEPLAY_KEEP",
            },
            {
                "selector_value": 10,
                "asset_id": "zelda_keep_objects_model_kanban2_model",
                "n64_draw": "gSignDirectionalDL",
                "required_object_name": "OBJECT_GAMEPLAY_KEEP",
            },
        ),
        "binding_kind": "param_selected_display_list",
    },
    "ACTOR_EN_ISHI": {
        "binding_status": READY_PARAM_SELECTED_DISPLAY_LIST,
        "object_name": "OBJECT_GAMEPLAY_FIELD_KEEP",
        "selector_mask": 1,
        "selector_rules": (
            {
                "selector_value": 0,
                "asset_id": "zelda_field_keep_model_obj_isi01_model",
                "n64_draw": "gFieldKakeraDL",
                "required_object_name": "OBJECT_GAMEPLAY_FIELD_KEEP",
            },
        ),
        "binding_kind": "param_selected_display_list",
    },
    "ACTOR_OBJ_HANA": {
        "binding_status": READY_PARAM_SELECTED_DISPLAY_LIST,
        "object_name": "OBJECT_GAMEPLAY_FIELD_KEEP",
        "selector_mask": 3,
        "selector_rules": (
            {
                "selector_value": 0,
                "asset_id": "zelda_field_keep_model_flower1_model",
                "n64_draw": "gHanaDL",
                "required_object_name": "OBJECT_GAMEPLAY_FIELD_KEEP",
            },
            {
                "selector_value": 1,
                "asset_id": "zelda_field_keep_model_obj_isi01_model",
                "n64_draw": "gFieldKakeraDL",
                "required_object_name": "OBJECT_GAMEPLAY_FIELD_KEEP",
            },
            {
                "selector_value": 2,
                "asset_id": "zelda_field_keep_model_grass05_model",
                "n64_draw": "gFieldBushDL",
                "required_object_name": "OBJECT_GAMEPLAY_FIELD_KEEP",
            },
        ),
        "binding_kind": "param_selected_display_list",
    },
    "ACTOR_EN_KUSA": {
        "binding_status": READY_PARAM_SELECTED_DISPLAY_LIST,
        "object_name": "OBJECT_KUSA",
        "selector_mask": 3,
        "selector_rules": (
            {
                "selector_value": 0,
                "asset_id": "zelda_keep_objects_model_field_kusa_model",
                "n64_draw": "gFieldBushDL",
                "required_object_name": "OBJECT_GAMEPLAY_FIELD_KEEP",
            },
            {
                "selector_value": 1,
                "asset_id": "zelda_kusa_model_obj_kusa01_model",
                "destroyed_asset_id": "zelda_kusa_model_obj_kusa03_model",
                "n64_draw": "object_kusa_DL_000140",
                "n64_destroyed_draw": "object_kusa_DL_0002E0",
                "required_object_name": "OBJECT_KUSA",
            },
            {
                "selector_value": 2,
                "asset_id": "zelda_kusa_model_obj_kusa01_model",
                "destroyed_asset_id": "zelda_kusa_model_obj_kusa03_model",
                "n64_draw": "object_kusa_DL_000140",
                "n64_destroyed_draw": "object_kusa_DL_0002E0",
                "required_object_name": "OBJECT_KUSA",
            },
        ),
        "binding_kind": "param_selected_display_list",
    },
    "ACTOR_EN_KANBAN": {
        "binding_status": READY_STATEFUL_DISPLAY_LIST,
        "object_name": "OBJECT_KANBAN",
        "binding_kind": "stateful_en_kanban_intact_sign",
        "state_rules": (
            {
                "state_name": "intact_sign",
                "state_predicate": "en_kanban_action_sign_and_all_parts",
                "asset_id": "zelda_keep_objects_model_kanban1_model",
                "n64_draw": "gSignRectangularDL",
                "required_object_name": "OBJECT_GAMEPLAY_KEEP",
                "draw_layer": "opaque",
                "transform": "en_kanban_intact_sign_offset",
            },
        ),
    },
    "ACTOR_OBJ_BEAN": {
        "binding_status": READY_STATEFUL_DISPLAY_LIST,
        "object_name": "OBJECT_MAMENOKI",
        "binding_kind": "stateful_obj_bean_draw",
        "state_rules": (
            {
                "state_name": "seedling",
                "state_flag_name": "BEAN_STATE_DRAW_SOIL",
                "state_flag_mask": 1 << 1,
                "asset_id": "zelda_mamenoki_model_c_mame_jr_model",
                "n64_draw": "gMagicBeanSeedlingDL",
                "required_object_name": "OBJECT_MAMENOKI",
                "draw_layer": "opaque",
                "transform": "actor_matrix",
            },
            {
                "state_name": "platform",
                "state_flag_name": "BEAN_STATE_DRAW_PLANT",
                "state_flag_mask": 1 << 2,
                "asset_id": "zelda_mamenoki_model_c_mame_lift_model",
                "n64_draw": "gMagicBeanPlatformDL",
                "required_object_name": "OBJECT_MAMENOKI",
                "draw_layer": "opaque",
                "transform": "actor_matrix",
            },
            {
                "state_name": "soft_soil",
                "state_flag_name": "BEAN_STATE_DRAW_LEAVES",
                "state_flag_mask": 1 << 0,
                "asset_id": "zelda_mamenoki_model_c_mame_place_model",
                "n64_draw": "gMagicBeanSoftSoilDL",
                "required_object_name": "OBJECT_MAMENOKI",
                "draw_layer": "translucent",
                "transform": "obj_bean_soft_soil_home",
            },
            {
                "state_name": "stalk",
                "state_flag_name": "BEAN_STATE_DRAW_STALK",
                "state_flag_mask": 1 << 3,
                "asset_id": "zelda_mamenoki_model_c_mame_kuki_model",
                "n64_draw": "gMagicBeanStemDL",
                "required_object_name": "OBJECT_MAMENOKI",
                "draw_layer": "opaque",
                "transform": "obj_bean_stalk_world",
            },
        ),
    },
    "ACTOR_EN_KAREBABA": {
        "binding_status": "needs_dekubaba_actor_family_mapping",
        "object_name": "OBJECT_DEKUBABA",
        "binding_kind": "mixed_static_and_skinned_family",
    },
    "ACTOR_EN_COW": {
        "binding_status": "blocked_skinned_animation_track_resolution",
        "object_name": "OBJECT_COW",
        "binding_kind": "skinned_actor",
    },
    "ACTOR_EN_SW": {
        "binding_status": "blocked_skinned_animation_track_resolution",
        "object_name": "OBJECT_ST",
        "binding_kind": "skinned_actor",
    },
    "ACTOR_EN_KO": {
        "binding_status": "needs_skinned_actor_runtime_binding",
        "object_name": "OBJECT_KW1",
        "binding_kind": "skinned_actor",
    },
    "ACTOR_EN_MD": {
        "binding_status": "needs_skinned_actor_runtime_binding",
        "object_name": "OBJECT_MD",
        "binding_kind": "skinned_actor",
    },
    "ACTOR_EN_SA": {
        "binding_status": "needs_skinned_actor_runtime_binding",
        "object_name": "OBJECT_SA",
        "binding_kind": "skinned_actor",
    },
}


def export_kokiri_static_actor_binding_plan(
    runtime_manifest_path: Path,
    actor_asset_readiness_path: Path,
    static_batch_manifest_path: Path,
    output_path: Path | None = None,
    *,
    sample_limit: int = 100,
) -> dict[str, object]:
    runtime_manifest = read_json_file(runtime_manifest_path, "Kokiri runtime manifest")
    actor_asset_readiness = read_json_file(
        actor_asset_readiness_path,
        "Kokiri actor asset readiness audit",
    )
    static_batch_manifest = read_json_file(static_batch_manifest_path, "static actor batch manifest")

    room_actor_targets = [
        target
        for target in runtime_manifest.get("resource_targets", [])
        if isinstance(target, dict) and target.get("kind") == "room_actor_list"
    ]
    actor_counts, actor_rooms, actor_ids = collect_actor_route_coverage(room_actor_targets)
    actor_param_counts = collect_actor_route_param_counts(room_actor_targets)
    object_rooms = collect_object_route_rooms(room_actor_targets)
    object_records = {
        str(record.get("object_name")): record
        for record in actor_asset_readiness.get("object_records", [])
        if isinstance(record, dict)
    }
    static_records_by_asset_id = {
        str(record.get("asset_id")): record
        for record in static_batch_manifest.get("records", [])
        if isinstance(record, dict) and record.get("status") == "converted"
    }
    static_records_by_archive = collect_static_records_by_archive(
        static_batch_manifest.get("records", [])
    )

    actor_records: list[dict[str, object]] = []
    ready_binding_records: list[dict[str, object]] = []
    deferred_binding_records: list[dict[str, object]] = []
    status_counts: Counter[str] = Counter()
    entry_status_counts: Counter[str] = Counter()
    issue_counts: Counter[str] = Counter()

    for actor_name in sorted(actor_counts):
        rule = ACTOR_BINDING_RULES.get(actor_name)
        entry_count = actor_counts[actor_name]
        rooms = sorted(actor_rooms[actor_name])
        if actor_name in ROUTE_BEHAVIOR_ONLY_ACTOR_STATUS:
            record = {
                "actor_name": actor_name,
                "actor_ids": sorted(actor_ids[actor_name]),
                "entry_count": entry_count,
                "rooms": rooms,
                "binding_status": "n64_behavior_fallback",
                "behavior_status": ROUTE_BEHAVIOR_ONLY_ACTOR_STATUS[actor_name],
            }
            actor_records.append(record)
            status_counts[str(record["binding_status"])] += 1
            entry_status_counts[str(record["binding_status"])] += entry_count
            continue

        if rule is None:
            record = {
                "actor_name": actor_name,
                "actor_ids": sorted(actor_ids[actor_name]),
                "entry_count": entry_count,
                "rooms": rooms,
                "binding_status": "needs_actor_object_semantic_mapping",
                "binding_kind": "unknown",
            }
            actor_records.append(record)
            deferred_binding_records.append(record)
            status_counts[str(record["binding_status"])] += 1
            entry_status_counts[str(record["binding_status"])] += entry_count
            continue

        record = actor_binding_record(
            actor_name,
            sorted(actor_ids[actor_name]),
            entry_count,
            rooms,
            rule,
            room_actor_targets,
            actor_param_counts[actor_name],
            object_rooms,
            object_records,
            static_records_by_asset_id,
            static_records_by_archive,
            issue_counts,
        )
        actor_records.append(record)
        status_counts[str(record["binding_status"])] += 1
        entry_status_counts[str(record["binding_status"])] += entry_count
        if is_ready_static_binding_status(str(record["binding_status"])):
            ready_binding_records.append(record)
        else:
            deferred_binding_records.append(record)

    static_route_object_records = [
        static_route_object_record(record, static_records_by_archive)
        for record in object_records.values()
        if int(record.get("static_exported_model_count", 0)) > 0
    ]
    static_route_object_records.sort(key=lambda record: str(record["object_name"]))

    ready_issue_total = sum(
        int(value)
        for key, value in issue_counts.items()
        if key.startswith("ready_binding_")
    )
    static_exported_model_total = sum(
        int(record.get("static_exported_model_count", 0))
        for record in object_records.values()
        if isinstance(record, dict)
    )
    visual_actor_entry_count = sum(
        count
        for actor_name, count in actor_counts.items()
        if actor_name not in ROUTE_BEHAVIOR_ONLY_ACTOR_STATUS
    )
    behavior_actor_entry_count = sum(
        count
        for actor_name, count in actor_counts.items()
        if actor_name in ROUTE_BEHAVIOR_ONLY_ACTOR_STATUS
    )

    plan: dict[str, object] = {
        "format": "oot3d_kokiri_static_actor_binding_plan_v1",
        "runtime_manifest": str(runtime_manifest_path),
        "actor_asset_readiness": str(actor_asset_readiness_path),
        "static_batch_manifest": str(static_batch_manifest_path),
        "route_id": runtime_manifest.get("route_id"),
        "runtime_enablement_status": runtime_manifest.get("runtime_enablement_status"),
        "route_room_actor_target_count": len(room_actor_targets),
        "route_actor_entry_count": sum(actor_counts.values()),
        "unique_route_actor_count": len(actor_counts),
        "visual_actor_entry_count": visual_actor_entry_count,
        "behavior_actor_entry_count": behavior_actor_entry_count,
        "route_object_with_static_exports_count": len(static_route_object_records),
        "route_static_exported_model_count": static_exported_model_total,
        "ready_static_binding_count": len(ready_binding_records),
        "ready_static_actor_entry_count": sum(
            int(record.get("entry_count", 0)) for record in ready_binding_records
        ),
        "ready_static_top_display_list_count": len(ready_static_top_display_lists(ready_binding_records)),
        "actor_binding_status_counts": sorted_counter(status_counts),
        "actor_entry_binding_status_counts": sorted_counter(entry_status_counts),
        "binding_issue_counts": sorted_counter(issue_counts),
        "ready_binding_issue_total": ready_issue_total,
        "ready_binding_records": ready_binding_records[:sample_limit],
        "deferred_binding_records": deferred_binding_records[:sample_limit],
        "static_route_object_records": static_route_object_records[:sample_limit],
        "actor_records": actor_records[:sample_limit],
        "runtime_policy": [
            "Only ready_single_display_list_binding and ready_param_selected_display_list_binding records are static draw hook candidates.",
            "Records with unresolved selector, multi-part, skinned, or semantic-mapping statuses remain N64 fallback.",
            "Generated actor resources stay outside git and must be packaged before any runtime hook can load them.",
        ],
    }
    if output_path is not None:
        write_json(output_path, plan)
    return plan


def actor_binding_record(
    actor_name: str,
    actor_ids: list[int],
    entry_count: int,
    rooms: list[str],
    rule: dict[str, object],
    room_actor_targets: list[dict[str, object]],
    param_counts: Counter[int],
    object_rooms: dict[str, set[str]],
    object_records: dict[str, dict[str, object]],
    static_records_by_asset_id: dict[str, dict[str, object]],
    static_records_by_archive: dict[str, list[dict[str, object]]],
    issue_counts: Counter[str],
) -> dict[str, object]:
    object_name = str(rule.get("object_name", ""))
    object_record = object_records.get(object_name, {})
    binding_status = str(rule.get("binding_status", "needs_actor_object_semantic_mapping"))
    object_room_list = sorted(object_rooms.get(object_name, set()))
    coverage_status = actor_room_object_coverage_status(
        actor_name,
        rooms,
        object_room_list,
        rule,
        room_actor_targets,
        object_rooms,
    )

    record: dict[str, object] = {
        "actor_name": actor_name,
        "actor_ids": actor_ids,
        "primary_actor_id": actor_ids[0] if len(actor_ids) == 1 else None,
        "entry_count": entry_count,
        "rooms": rooms,
        "binding_status": binding_status,
        "binding_kind": rule.get("binding_kind"),
        "object_name": object_name,
        "object_archive_path": object_record.get("archive_path"),
        "object_asset_readiness_status": object_record.get("asset_readiness_status"),
        "object_rooms": object_room_list,
        "room_object_coverage_status": coverage_status,
    }
    if coverage_status != "covered_by_room_object_prefixes":
        issue_counts["actor_room_object_prefix_gap"] += 1

    if binding_status == READY_PARAM_SELECTED_DISPLAY_LIST:
        record.update(
            param_selected_binding_record(
                rule,
                param_counts,
                static_records_by_asset_id,
                issue_counts,
            )
        )
        return record

    if binding_status == READY_STATEFUL_DISPLAY_LIST:
        record.update(
            stateful_display_list_binding_record(
                rule,
                static_records_by_asset_id,
                issue_counts,
            )
        )
        return record

    candidate_asset_ids = tuple(str(value) for value in rule.get("candidate_asset_ids", ()))
    if candidate_asset_ids:
        record["candidate_asset_ids"] = list(candidate_asset_ids)
        record["candidate_static_exports"] = [
            compact_static_binding_record(static_records_by_asset_id[asset_id])
            for asset_id in candidate_asset_ids
            if asset_id in static_records_by_asset_id
        ]
        if len(record["candidate_static_exports"]) != len(candidate_asset_ids):
            issue_counts["candidate_static_asset_missing"] += 1

    primary_asset_id = rule.get("primary_asset_id")
    if primary_asset_id is None:
        if object_name in static_records_by_archive:
            record["static_export_count"] = len(static_records_by_archive[object_name])
        return record

    asset_id = str(primary_asset_id)
    record["primary_asset_id"] = asset_id
    static_record = static_records_by_asset_id.get(asset_id)
    if static_record is None:
        record["top_display_list_resource_status"] = "missing_static_batch_record"
        issue_counts["ready_binding_missing_static_batch_record"] += 1
        return record

    top_display_list = top_display_list_path(static_record)
    top_display_list_present = display_list_resource_present(static_record, top_display_list)
    record.update(compact_static_binding_record(static_record))
    record["top_display_list"] = top_display_list
    record["draw_layer"] = str(rule.get("draw_layer", "opaque"))
    record["top_display_list_resource_status"] = (
        "present" if top_display_list_present else "missing_display_list_resource"
    )
    if binding_status == READY_SINGLE_DISPLAY_LIST and not top_display_list_present:
        issue_counts["ready_binding_missing_top_display_list_resource"] += 1
    if binding_status == READY_SINGLE_DISPLAY_LIST and coverage_status != "covered_by_room_object_prefixes":
        issue_counts["ready_binding_room_object_prefix_gap"] += 1
    return record


def param_selected_binding_record(
    rule: dict[str, object],
    param_counts: Counter[int],
    static_records_by_asset_id: dict[str, dict[str, object]],
    issue_counts: Counter[str],
) -> dict[str, object]:
    selector_mask = int(rule.get("selector_mask", 0))
    selector_counts = selector_counts_for_rule(param_counts, selector_mask)
    selector_rules = [
        selector_rule
        for selector_rule in rule.get("selector_rules", ())
        if isinstance(selector_rule, dict)
    ]
    selector_rules_by_value = {
        int(selector_rule.get("selector_value", -1)): selector_rule
        for selector_rule in selector_rules
    }
    selector_records: list[dict[str, object]] = []
    top_display_lists: set[str] = set()
    selected_asset_ids: set[str] = set()
    missing_selector_values: list[int] = []

    for selector_value in sorted(selector_counts):
        selector_rule = selector_rules_by_value.get(selector_value)
        if selector_rule is None:
            missing_selector_values.append(selector_value)
            issue_counts["ready_binding_missing_param_selector_rule"] += 1
            continue

        selector_record = compact_selector_binding_record(
            selector_rule,
            static_records_by_asset_id,
            issue_counts,
        )
        selector_record["route_entry_count"] = selector_counts[selector_value]
        selector_records.append(selector_record)

        for key in ("asset_id", "destroyed_asset_id"):
            asset_id = str(selector_record.get(key, ""))
            if asset_id:
                selected_asset_ids.add(asset_id)
        for key in ("top_display_list", "destroyed_top_display_list"):
            display_list = str(selector_record.get(key, ""))
            if display_list:
                top_display_lists.add(display_list)

    if missing_selector_values:
        selector_status = "missing_param_selector_rules"
    elif any(
        record.get("top_display_list_resource_status") != "present" or
        record.get("destroyed_top_display_list_resource_status") == "missing_display_list_resource"
        for record in selector_records
    ):
        selector_status = "missing_param_selected_display_list_resource"
    else:
        selector_status = "present"

    return {
        "selector_mask": selector_mask,
        "route_selector_value_counts": sorted_counter(selector_counts),
        "missing_selector_values": missing_selector_values,
        "selector_status": selector_status,
        "selected_asset_ids": sorted(selected_asset_ids),
        "top_display_lists": sorted(top_display_lists),
        "selector_records": selector_records,
    }


def stateful_display_list_binding_record(
    rule: dict[str, object],
    static_records_by_asset_id: dict[str, dict[str, object]],
    issue_counts: Counter[str],
) -> dict[str, object]:
    state_rules = [
        state_rule
        for state_rule in rule.get("state_rules", ())
        if isinstance(state_rule, dict)
    ]
    state_records: list[dict[str, object]] = []
    top_display_lists: set[str] = set()
    selected_asset_ids: set[str] = set()

    for state_rule in state_rules:
        state_record = compact_stateful_binding_record(
            state_rule,
            static_records_by_asset_id,
            issue_counts,
        )
        state_records.append(state_record)

        asset_id = str(state_record.get("asset_id", ""))
        if asset_id:
            selected_asset_ids.add(asset_id)
        display_list = str(state_record.get("top_display_list", ""))
        if display_list:
            top_display_lists.add(display_list)

    if not state_records:
        stateful_status = "missing_state_rules"
        issue_counts["ready_binding_missing_state_rules"] += 1
    elif any(record.get("top_display_list_resource_status") != "present" for record in state_records):
        stateful_status = "missing_stateful_display_list_resource"
    else:
        stateful_status = "present"

    return {
        "stateful_status": stateful_status,
        "selected_asset_ids": sorted(selected_asset_ids),
        "top_display_lists": sorted(top_display_lists),
        "state_records": state_records,
    }


def compact_stateful_binding_record(
    state_rule: dict[str, object],
    static_records_by_asset_id: dict[str, dict[str, object]],
    issue_counts: Counter[str],
) -> dict[str, object]:
    asset_id = str(state_rule.get("asset_id", ""))
    record: dict[str, object] = {
        "state_name": state_rule.get("state_name"),
        "state_predicate": state_rule.get("state_predicate"),
        "state_flag_name": state_rule.get("state_flag_name"),
        "state_flag_mask": state_rule.get("state_flag_mask"),
        "asset_id": asset_id,
        "n64_draw": state_rule.get("n64_draw"),
        "required_object_name": state_rule.get("required_object_name"),
        "draw_layer": state_rule.get("draw_layer", "opaque"),
        "transform": state_rule.get("transform", "actor_matrix"),
    }
    add_stateful_resource_fields(record, static_records_by_asset_id, issue_counts)
    return record


def add_stateful_resource_fields(
    record: dict[str, object],
    static_records_by_asset_id: dict[str, dict[str, object]],
    issue_counts: Counter[str],
) -> None:
    asset_id = str(record.get("asset_id", ""))
    static_record = static_records_by_asset_id.get(asset_id)
    if static_record is None:
        record["top_display_list_resource_status"] = "missing_static_batch_record"
        issue_counts["ready_binding_missing_stateful_static_batch_record"] += 1
        return

    top_display_list = top_display_list_path(static_record)
    record["top_display_list"] = top_display_list
    record["top_display_list_resource_status"] = (
        "present"
        if display_list_resource_present(static_record, top_display_list)
        else "missing_display_list_resource"
    )
    if record["top_display_list_resource_status"] != "present":
        issue_counts["ready_binding_missing_stateful_display_list_resource"] += 1


def compact_selector_binding_record(
    selector_rule: dict[str, object],
    static_records_by_asset_id: dict[str, dict[str, object]],
    issue_counts: Counter[str],
) -> dict[str, object]:
    selector_value = int(selector_rule.get("selector_value", -1))
    asset_id = str(selector_rule.get("asset_id", ""))
    record: dict[str, object] = {
        "selector_value": selector_value,
        "asset_id": asset_id,
        "n64_draw": selector_rule.get("n64_draw"),
        "required_object_name": selector_rule.get("required_object_name"),
        "draw_layer": selector_rule.get("draw_layer", "opaque"),
    }
    add_selector_resource_fields(
        record,
        static_records_by_asset_id,
        issue_counts,
        asset_id_key="asset_id",
        display_list_key="top_display_list",
        resource_status_key="top_display_list_resource_status",
    )

    destroyed_asset_id = str(selector_rule.get("destroyed_asset_id", ""))
    if destroyed_asset_id:
        record["destroyed_asset_id"] = destroyed_asset_id
        record["n64_destroyed_draw"] = selector_rule.get("n64_destroyed_draw")
        add_selector_resource_fields(
            record,
            static_records_by_asset_id,
            issue_counts,
            asset_id_key="destroyed_asset_id",
            display_list_key="destroyed_top_display_list",
            resource_status_key="destroyed_top_display_list_resource_status",
        )
    return record


def add_selector_resource_fields(
    record: dict[str, object],
    static_records_by_asset_id: dict[str, dict[str, object]],
    issue_counts: Counter[str],
    *,
    asset_id_key: str,
    display_list_key: str,
    resource_status_key: str,
) -> None:
    asset_id = str(record.get(asset_id_key, ""))
    static_record = static_records_by_asset_id.get(asset_id)
    if static_record is None:
        record[resource_status_key] = "missing_static_batch_record"
        issue_counts["ready_binding_missing_param_selected_static_batch_record"] += 1
        return

    top_display_list = top_display_list_path(static_record)
    record[display_list_key] = top_display_list
    record[resource_status_key] = (
        "present"
        if display_list_resource_present(static_record, top_display_list)
        else "missing_display_list_resource"
    )
    if record[resource_status_key] != "present":
        issue_counts["ready_binding_missing_param_selected_display_list_resource"] += 1


def collect_actor_route_coverage(
    room_actor_targets: list[dict[str, object]],
) -> tuple[Counter[str], dict[str, set[str]], dict[str, set[int]]]:
    counts: Counter[str] = Counter()
    rooms: dict[str, set[str]] = defaultdict(set)
    actor_ids: dict[str, set[int]] = defaultdict(set)
    for target in room_actor_targets:
        room = str(target.get("oot3d_source", target.get("shipwright_resource", "<unknown>")))
        for entry in target.get("actor_entries", []):
            if not isinstance(entry, dict):
                continue
            actor_id = int(entry.get("actor_id", 0))
            actor_name = str(entry.get("actor_name", f"ACTOR_0x{actor_id:04x}"))
            counts[actor_name] += 1
            rooms[actor_name].add(room)
            actor_ids[actor_name].add(actor_id)
    return counts, rooms, actor_ids


def collect_actor_route_param_counts(
    room_actor_targets: list[dict[str, object]],
) -> dict[str, Counter[int]]:
    counts: dict[str, Counter[int]] = defaultdict(Counter)
    for target in room_actor_targets:
        for entry in target.get("actor_entries", []):
            if not isinstance(entry, dict):
                continue
            actor_id = int(entry.get("actor_id", 0))
            actor_name = str(entry.get("actor_name", f"ACTOR_0x{actor_id:04x}"))
            params = int(entry.get("params", 0)) & 0xFFFF
            counts[actor_name][params] += 1
    return counts


def selector_counts_for_rule(param_counts: Counter[int], selector_mask: int) -> Counter[int]:
    selector_counts: Counter[int] = Counter()
    for params, count in param_counts.items():
        selector_counts[params & selector_mask] += count
    return selector_counts


def collect_object_route_rooms(room_actor_targets: list[dict[str, object]]) -> dict[str, set[str]]:
    rooms: dict[str, set[str]] = defaultdict(set)
    for target in room_actor_targets:
        room = str(target.get("oot3d_source", target.get("shipwright_resource", "<unknown>")))
        for record in target.get("object_ids", []):
            if isinstance(record, dict):
                object_name = str(record.get("object_name", f"OBJECT_0x{int(record.get('object_id', 0)):04x}"))
                rooms[object_name].add(room)
    return rooms


def collect_static_records_by_archive(records: object) -> dict[str, list[dict[str, object]]]:
    result: dict[str, list[dict[str, object]]] = defaultdict(list)
    if not isinstance(records, list):
        return result
    for record in records:
        if not isinstance(record, dict) or record.get("status") != "converted":
            continue
        archive = archive_from_static_source(record.get("source"))
        if archive:
            result[archive].append(record)
    return result


def static_route_object_record(
    object_record: dict[str, object],
    static_records_by_archive: dict[str, list[dict[str, object]]],
) -> dict[str, object]:
    archive_path = str(object_record.get("archive_path", ""))
    static_records = static_records_by_archive.get(archive_path, [])
    return {
        "object_name": object_record.get("object_name"),
        "archive_path": archive_path,
        "asset_readiness_status": object_record.get("asset_readiness_status"),
        "route_reference_count": object_record.get("route_reference_count"),
        "static_exported_model_count": object_record.get("static_exported_model_count"),
        "static_exports": [
            compact_static_binding_record(record) for record in static_records
        ],
    }


def actor_room_object_coverage_status(
    actor_name: str,
    actor_rooms: list[str],
    object_rooms: list[str],
    rule: dict[str, object],
    room_actor_targets: list[dict[str, object]],
    object_rooms_by_name: dict[str, set[str]],
) -> str:
    if rule.get("binding_kind") == "param_selected_display_list":
        return param_selected_room_object_coverage_status(
            actor_name,
            rule,
            room_actor_targets,
            object_rooms_by_name,
        )
    if is_route_keep_object(str(rule.get("object_name", ""))):
        return "covered_by_room_object_prefixes"
    return room_object_coverage_status(actor_rooms, object_rooms)


def param_selected_room_object_coverage_status(
    actor_name: str,
    rule: dict[str, object],
    room_actor_targets: list[dict[str, object]],
    object_rooms_by_name: dict[str, set[str]],
) -> str:
    selector_mask = int(rule.get("selector_mask", 0))
    selector_rules_by_value = {
        int(selector_rule.get("selector_value", -1)): selector_rule
        for selector_rule in rule.get("selector_rules", ())
        if isinstance(selector_rule, dict)
    }
    missing_rooms: set[str] = set()
    for target in room_actor_targets:
        room = str(target.get("oot3d_source", target.get("shipwright_resource", "<unknown>")))
        for entry in target.get("actor_entries", []):
            if not isinstance(entry, dict) or str(entry.get("actor_name")) != actor_name:
                continue
            params = int(entry.get("params", 0)) & 0xFFFF
            selector_rule = selector_rules_by_value.get(params & selector_mask)
            if selector_rule is None:
                missing_rooms.add(room)
                continue
            required_object_name = str(selector_rule.get("required_object_name", ""))
            if not required_object_name or is_route_keep_object(required_object_name):
                continue
            if room not in object_rooms_by_name.get(required_object_name, set()):
                missing_rooms.add(room)
    if missing_rooms:
        return "missing_param_selected_room_object_prefix"
    return "covered_by_room_object_prefixes"


def is_route_keep_object(object_name: str) -> bool:
    return object_name in {"OBJECT_GAMEPLAY_KEEP", "OBJECT_GAMEPLAY_FIELD_KEEP"}


def room_object_coverage_status(actor_rooms: list[str], object_rooms: list[str]) -> str:
    missing = sorted(set(actor_rooms) - set(object_rooms))
    if missing:
        return "missing_room_object_prefix"
    return "covered_by_room_object_prefixes"


def is_ready_static_binding_status(status: str) -> bool:
    return status in {
        READY_SINGLE_DISPLAY_LIST,
        READY_PARAM_SELECTED_DISPLAY_LIST,
        READY_STATEFUL_DISPLAY_LIST,
    }


def ready_static_top_display_lists(records: list[dict[str, object]]) -> set[str]:
    display_lists: set[str] = set()
    for record in records:
        if record.get("top_display_list_resource_status") == "present":
            display_list = str(record.get("top_display_list", ""))
            if display_list:
                display_lists.add(display_list)
        for display_list in record.get("top_display_lists", []):
            if str(display_list):
                display_lists.add(str(display_list))
    return display_lists


def compact_static_binding_record(record: dict[str, object]) -> dict[str, object]:
    return {
        "asset_id": record.get("asset_id"),
        "source": record.get("source"),
        "resource_root": record.get("resource_root"),
        "symbol": record.get("symbol"),
        "resource_count": record.get("resource_count"),
        "top_display_list": top_display_list_path(record),
    }


def top_display_list_path(record: dict[str, object]) -> str | None:
    resource_root = str(record.get("resource_root", "")).strip("/")
    symbol = str(record.get("symbol", "")).strip("/")
    if not resource_root or not symbol:
        return None
    return f"{resource_root}/{symbol}"


def display_list_resource_present(record: dict[str, object], path: str | None) -> bool:
    if path is None:
        return False
    resources: Any = record.get("resources", [])
    if not isinstance(resources, list):
        return False
    return any(
        isinstance(resource, dict)
        and resource.get("kind") == "DisplayList"
        and str(resource.get("path", "")).strip("/") == path
        for resource in resources
    )
