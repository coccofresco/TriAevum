from __future__ import annotations

import argparse
import hashlib
import json
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


FORMAT = "oot3d_semantic_route_catalog_v1"


def _scene_assets(catalog: dict[str, Any]) -> dict[int, list[dict[str, Any]]]:
    by_scene: dict[int, list[dict[str, Any]]] = {}
    for record in catalog.get("records", []):
        if not isinstance(record, dict) or record.get("family") != "scene_profile":
            continue
        ownership = record.get("ownership", {})
        if not isinstance(ownership, dict) or not isinstance(ownership.get("scene_id"), int):
            continue
        scene_id = int(ownership["scene_id"])
        if scene_id >= 0:
            by_scene.setdefault(scene_id, []).append(record)
    return by_scene


def _assets_by_id(catalog: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {
        str(record["asset_id"]): record
        for record in catalog.get("records", [])
        if isinstance(record, dict) and record.get("asset_id")
    }


def _global_entrances_by_index(table: dict[str, Any]) -> dict[int, list[dict[str, Any]]]:
    by_index: dict[int, list[dict[str, Any]]] = {}
    for row in table.get("rows", []):
        if not isinstance(row, dict) or not isinstance(row.get("entrance_index"), int):
            continue
        by_index.setdefault(int(row["entrance_index"]), []).append(row)
    return by_index


def _scene_route(source: dict[str, Any], asset: dict[str, Any]) -> dict[str, Any]:
    scene_id = int(source["scene_id"])
    semantic_key = str(source["secondary_scene_enum"])
    ownership = asset.get("ownership", {})
    return {
        "route_id": f"scene:{semantic_key}",
        "kind": "scene",
        "scaffold": {
            "system": "oot_n64_gameplay",
            "request_kind": "scene",
            "semantic_key": semantic_key,
            "scene_id": scene_id,
        },
        "native": {
            "system": "oot3d",
            "asset_id": str(asset["asset_id"]),
            "scene_id": scene_id,
            "scene_path": str(source["zsi_path"]),
            "scene_stem": str(source["scene_stem"]),
            "setup_indices": [int(index) for index in ownership.get("setup_indices", [])],
        },
        "authority": {
            "control_flow": "oot_n64_gameplay_scaffold",
            "content": "oot3d_native",
            "timing": "oot3d_native",
            "rendering": "oot3d_native",
        },
        "completion_event": "scene_exit_requested",
        "status": "resolved",
        "evidence": {
            "mapping_source": "oot3d_scene_source_table",
            "native_scene_source_status": str(source["native_scene_source_status"]),
            "native_scene_index_symbol": str(source.get("scene_index_symbol", "")),
            "native_resource_record_address": str(source.get("resource_record_address_hex", "")),
            "semantic_label_source": "secondary_scene_enum",
        },
    }


def _cutscene_route(binding: dict[str, Any], scene_asset: dict[str, Any],
                    sequence_assets: list[dict[str, Any]]) -> dict[str, Any]:
    scaffold = binding["scaffold"]
    native = binding["native"]
    sequence = []
    for asset in sequence_assets:
        metadata = asset.get("metadata", {})
        sequence.append({
            "asset_id": str(asset["asset_id"]),
            "command_count": metadata.get("command_count"),
            "end_frame": metadata.get("end_frame"),
            "canonical_resources": list(asset.get("canonical_resources", [])),
        })
    return {
        "route_id": f"cutscene:{binding['semantic_key']}",
        "kind": "cutscene",
        "scaffold": {
            "system": "oot_n64_gameplay",
            "request_kind": "cutscene",
            "semantic_key": str(binding["semantic_key"]),
            "scene_id": int(scaffold["scene_id"]),
            "game_state": str(scaffold["game_state"]),
            "scene_setup_index": int(scaffold["scene_setup_index"]),
            "cutscene_index": int(scaffold["cutscene_index"]),
        },
        "native": {
            "system": "oot3d",
            "asset_id": str(scene_asset["asset_id"]),
            "scene_id": int(native["scene_id"]),
            "scene_path": str(native["scene_path"]),
            "scene_stem": str(native["scene_stem"]),
            "setup_indices": [int(native["setup_index"])],
            "sequence_asset_ids": [str(asset["asset_id"]) for asset in sequence_assets],
            "sequence": sequence,
        },
        "authority": {
            "control_flow": "oot_n64_gameplay_scaffold",
            "content": "oot3d_native",
            "timing": "oot3d_native",
            "rendering": "oot3d_native",
        },
        "completion_event": str(binding.get("completion_event", "cutscene_finished")),
        "status": "resolved",
        "evidence": dict(binding.get("evidence", {})),
    }


_SEMANTIC_FACT_VALUES = {
    "player.age": {"child", "adult"},
    "world.day_phase": {"day", "night"},
    "gameplay.layer": {"normal", "cutscene"},
    "quest.kokiri_emerald": {"false", "true"},
    "event.zeldas_letter": {"false", "true"},
    "quest.forest_medallion": {"false", "true"},
}


def _semantic_scene_entry_variants(source: dict[str, Any]) -> dict[tuple[str, str], dict[str, Any]]:
    if source.get("format") != "oot3d_semantic_gameplay_source_v1":
        raise ValueError("unsupported offline semantic gameplay source format")
    records = source.get("records")
    if not isinstance(records, list):
        raise ValueError("offline semantic gameplay source requires records")

    result: dict[tuple[str, str], dict[str, Any]] = {}
    for record in records:
        if not isinstance(record, dict) or record.get("status") != "resolved":
            continue
        semantic_key = record.get("semantic_key")
        trigger = record.get("trigger")
        variants = record.get("variants")
        if (record.get("kind") != "scene_entry" or
                not isinstance(semantic_key, str) or not semantic_key or
                not isinstance(trigger, dict) or not isinstance(variants, list) or not variants):
            raise ValueError("invalid offline semantic scene-entry record")
        required_trigger_fields = (
            "scene_id", "entrance_index", "entrance_symbol", "origin_semantic_key"
        )
        if any(field not in trigger for field in required_trigger_fields):
            raise ValueError(f"offline semantic trigger is incomplete: {semantic_key}")
        for variant in variants:
            if not isinstance(variant, dict):
                raise ValueError(f"offline semantic variant is invalid: {semantic_key}")
            variant_key = variant.get("variant_key")
            conditions = variant.get("conditions")
            if not isinstance(variant_key, str) or not variant_key or not isinstance(conditions, list) or not conditions:
                raise ValueError(f"offline semantic variant is incomplete: {semantic_key}")
            seen_facts: set[str] = set()
            for condition in conditions:
                if not isinstance(condition, dict):
                    raise ValueError(f"offline semantic condition is invalid: {semantic_key}:{variant_key}")
                fact = condition.get("fact")
                expected = condition.get("equals")
                if (not isinstance(fact, str) or fact not in _SEMANTIC_FACT_VALUES or
                        not isinstance(expected, str) or expected not in _SEMANTIC_FACT_VALUES[fact] or
                        fact in seen_facts):
                    raise ValueError(
                        f"unsupported offline semantic condition: {semantic_key}:{variant_key}"
                    )
                seen_facts.add(fact)
            key = (semantic_key, variant_key)
            if key in result:
                raise ValueError(f"duplicate offline semantic variant: {semantic_key}:{variant_key}")
            result[key] = {
                "semantic_key": semantic_key,
                "trigger": trigger,
                "variant": variant,
                "evidence": dict(record.get("evidence", {})),
            }
    return result


def _scene_entry_route(binding: dict[str, Any], semantic_source: dict[str, Any],
                       scene_asset: dict[str, Any], global_entry: dict[str, Any]) -> dict[str, Any]:
    trigger = semantic_source["trigger"]
    variant = semantic_source["variant"]
    native = binding["native"]
    variant_key = str(variant["variant_key"])
    return {
        "route_id": f"scene_entry:{binding['semantic_key']}:{variant_key}",
        "kind": "scene_entry",
        "scaffold": {
            "system": "oot_n64_gameplay",
            "request_kind": "scene_entry",
            "semantic_key": str(binding["semantic_key"]),
            "scene_id": int(trigger["scene_id"]),
            "entrance_index": int(trigger["entrance_index"]),
            "entrance_symbol": str(trigger["entrance_symbol"]),
            "origin_semantic_key": str(trigger["origin_semantic_key"]),
            "variant_key": variant_key,
            "variant_source": "oot_n64_semantic_source_offline",
            "variant_conditions": [dict(condition) for condition in variant["conditions"]],
        },
        "native": {
            "system": "oot3d",
            "asset_id": str(scene_asset["asset_id"]),
            "scene_id": int(native["scene_id"]),
            "scene_path": str(native["scene_path"]),
            "scene_stem": str(native["scene_stem"]),
            "setup_indices": [int(index) for index in native["setup_indices"]],
            "setup_selection": str(native["setup_selection"]),
            "room_bindings": sorted(
                ({
                    "scaffold_room_index": int(room["scaffold_room_index"]),
                    "native_room_index": int(room["native_room_index"]),
                } for room in native["room_bindings"]),
                key=lambda room: room["scaffold_room_index"],
            ),
            "global_entrance_index": int(global_entry["entrance_index"]),
            "local_entrance_index": int(global_entry["local_entrance_index"]),
            "spatial_source": "oot3d_zsi_spawn_entry",
            "camera_source": "oot3d_zsi_player_params_then_collision_or_default_camera",
            "player_entry_state_source": "oot3d_player_entry_state",
            "population_source": str(native["population_source"]),
            "actor_configuration_source": str(native["actor_configuration_source"]),
        },
        "authority": {
            "control_flow": "oot_n64_gameplay_scaffold",
            "content": "oot3d_native",
            "timing": "oot3d_native",
            "rendering": "oot3d_native",
            "spatial_state": "oot3d_native",
            "player_entry_state": "oot3d_native",
            "population": "oot3d_native",
            "actor_configuration": "oot3d_native",
            "behavior_semantics": "oot_n64_semantic_scaffold_with_verified_oot3d_deltas",
        },
        "completion_event": str(binding.get("completion_event", "scene_entry_finished")),
        "status": "resolved",
        "evidence": {
            **dict(semantic_source.get("evidence", {})),
            **dict(variant.get("evidence", {})),
            **dict(binding.get("evidence", {})),
            "native_global_entry_status": str(global_entry.get("status", "")),
            "native_global_entry_validation": str(global_entry.get("native_validation_status", "")),
            "native_global_entry_table_file_offset": str(
                global_entry.get("table_file_offset_hex", "")
            ),
        },
    }


def build_semantic_route_catalog(
    asset_catalog: dict[str, Any],
    scene_source_table: dict[str, Any],
    cutscene_bindings: dict[str, Any] | None = None,
    scene_entry_bindings: dict[str, Any] | None = None,
    global_entrance_table: dict[str, Any] | None = None,
    *,
    semantic_gameplay_source: dict[str, Any] | None = None,
    now: datetime | None = None,
) -> dict[str, Any]:
    if (asset_catalog.get("format") != "oot3d_asset_catalog_v1" or
            asset_catalog.get("status") != "complete"):
        raise ValueError("semantic routes require a complete oot3d_asset_catalog_v1")
    rows = scene_source_table.get("rows")
    if not isinstance(rows, list):
        raise ValueError("semantic routes require scene_source_table rows")

    now = now or datetime.now(timezone.utc)
    assets = _scene_assets(asset_catalog)
    assets_by_id = _assets_by_id(asset_catalog)
    native_room_assets = {
        (record.get("ownership", {}).get("scene_id"),
         record.get("ownership", {}).get("room_index"))
        for record in asset_catalog.get("records", [])
        if isinstance(record, dict) and record.get("family") == "scene_room_source"
    }
    routes: list[dict[str, Any]] = []
    coverage_gaps: list[dict[str, Any]] = []
    invalid_mappings: list[dict[str, Any]] = []

    for source in sorted(rows, key=lambda row: int(row.get("scene_id", -1))):
        scene_id = source.get("scene_id")
        source_status = str(source.get("native_scene_source_status", ""))
        if not isinstance(scene_id, int) or source_status != "native_scene_source_resolved":
            coverage_gaps.append({
                "scene_id": scene_id,
                "semantic_key": source.get("secondary_scene_enum"),
                "scene_path": source.get("zsi_path"),
                "reason": source_status or "scene_id_missing",
            })
            continue
        semantic_key = source.get("secondary_scene_enum")
        if not isinstance(semantic_key, str) or not semantic_key:
            invalid_mappings.append({"scene_id": scene_id, "reason": "semantic_key_missing"})
            continue
        candidates = assets.get(scene_id, [])
        if len(candidates) != 1:
            invalid_mappings.append({
                "scene_id": scene_id,
                "semantic_key": semantic_key,
                "reason": "native_scene_asset_missing" if not candidates else "native_scene_asset_ambiguous",
                "candidate_count": len(candidates),
            })
            continue
        asset = candidates[0]
        metadata = asset.get("metadata", {})
        if str(metadata.get("scene_path", "")) != str(source.get("zsi_path", "")):
            invalid_mappings.append({
                "scene_id": scene_id,
                "semantic_key": semantic_key,
                "reason": "native_scene_path_mismatch",
                "source_path": source.get("zsi_path"),
                "asset_path": metadata.get("scene_path"),
            })
            continue
        routes.append(_scene_route(source, asset))

    scene_entry_inputs = (
        semantic_gameplay_source,
        scene_entry_bindings,
        global_entrance_table,
    )
    if any(value is not None for value in scene_entry_inputs) and not all(
        value is not None for value in scene_entry_inputs
    ):
        raise ValueError(
            "offline semantic gameplay source, scene-entry bindings, and native global entrance table are required together"
        )
    if (semantic_gameplay_source is not None and scene_entry_bindings is not None and
            global_entrance_table is not None):
        if scene_entry_bindings.get("format") != "oot3d_semantic_scene_entry_bindings_v1":
            raise ValueError("unsupported semantic scene entry binding format")
        if (global_entrance_table.get("summary", {}).get("format") !=
                "oot3d_scene_global_entrance_table_v2"):
            raise ValueError("unsupported native global entrance table format")
        binding_rows = scene_entry_bindings.get("records")
        if not isinstance(binding_rows, list):
            raise ValueError("semantic scene entry bindings require records")
        semantic_variants = _semantic_scene_entry_variants(semantic_gameplay_source)
        global_entries = _global_entrances_by_index(global_entrance_table)
        for binding in binding_rows:
            if not isinstance(binding, dict) or binding.get("status") != "resolved":
                continue
            semantic_key = str(binding.get("semantic_key", ""))
            variant_key = str(binding.get("variant_key", ""))
            native = binding.get("native", {})
            semantic_source = semantic_variants.get((semantic_key, variant_key))
            if not semantic_key or not variant_key or not isinstance(native, dict):
                invalid_mappings.append({
                    "semantic_key": semantic_key,
                    "reason": "scene_entry_binding_shape",
                })
                continue
            if semantic_source is None:
                invalid_mappings.append({
                    "semantic_key": semantic_key,
                    "variant_key": variant_key,
                    "reason": "offline_semantic_scene_entry_missing",
                })
                continue
            required_strings = {
                "native.setup_selection": native.get("setup_selection"),
                "native.population_source": native.get("population_source"),
                "native.actor_configuration_source": native.get("actor_configuration_source"),
            }
            missing_strings = [name for name, value in required_strings.items()
                               if not isinstance(value, str) or not value]
            if missing_strings:
                invalid_mappings.append({
                    "semantic_key": semantic_key,
                    "reason": "scene_entry_semantic_context_missing",
                    "missing_fields": missing_strings,
                })
                continue

            scene_asset_id = str(native.get("scene_asset_id", ""))
            scene_asset = assets_by_id.get(scene_asset_id)
            if scene_asset is None or scene_asset.get("family") != "scene_profile":
                invalid_mappings.append({
                    "semantic_key": semantic_key,
                    "reason": "native_scene_entry_asset_missing",
                    "asset_id": scene_asset_id,
                })
                continue
            ownership = scene_asset.get("ownership", {})
            metadata = scene_asset.get("metadata", {})
            setup_indices = native.get("setup_indices")
            owned_setups = ownership.get("setup_indices", [])
            if (not isinstance(setup_indices, list) or not setup_indices or
                    any(not isinstance(index, int) for index in setup_indices) or
                    ownership.get("scene_id") != native.get("scene_id") or
                    metadata.get("scene_path") != native.get("scene_path") or
                    any(index not in owned_setups for index in setup_indices)):
                invalid_mappings.append({
                    "semantic_key": semantic_key,
                    "reason": "native_scene_entry_ownership_mismatch",
                    "asset_id": scene_asset_id,
                })
                continue

            room_bindings = native.get("room_bindings")
            if not isinstance(room_bindings, list) or not room_bindings:
                invalid_mappings.append({
                    "semantic_key": semantic_key,
                    "reason": "native_scene_entry_room_bindings_missing",
                })
                continue
            scaffold_rooms: set[int] = set()
            native_rooms: set[int] = set()
            room_binding_invalid = False
            for room_binding in room_bindings:
                if not isinstance(room_binding, dict):
                    room_binding_invalid = True
                    break
                scaffold_room = room_binding.get("scaffold_room_index")
                native_room = room_binding.get("native_room_index")
                if (not isinstance(scaffold_room, int) or scaffold_room < 0 or
                        not isinstance(native_room, int) or native_room < 0 or
                        scaffold_room in scaffold_rooms or
                        (native.get("scene_id"), native_room) not in native_room_assets):
                    room_binding_invalid = True
                    break
                scaffold_rooms.add(scaffold_room)
                native_rooms.add(native_room)
            if room_binding_invalid:
                invalid_mappings.append({
                    "semantic_key": semantic_key,
                    "reason": "native_scene_entry_room_binding_invalid",
                    "room_bindings": room_bindings,
                })
                continue

            native_global_index = native.get("global_entrance_index")
            candidates = (global_entries.get(native_global_index, [])
                          if isinstance(native_global_index, int) else [])
            if len(candidates) != 1:
                invalid_mappings.append({
                    "semantic_key": semantic_key,
                    "reason": ("native_global_entrance_missing" if not candidates
                               else "native_global_entrance_ambiguous"),
                    "global_entrance_index": native_global_index,
                    "candidate_count": len(candidates),
                })
                continue
            global_entry = candidates[0]
            if (global_entry.get("status") != "decoded_from_code_bin" or
                    global_entry.get("scene_id") != native.get("scene_id") or
                    global_entry.get("local_entrance_index") !=
                    native.get("local_entrance_index") or
                    global_entry.get("native_scene_resource_zsi_path") !=
                    native.get("scene_path")):
                invalid_mappings.append({
                    "semantic_key": semantic_key,
                    "reason": "native_global_entrance_target_mismatch",
                    "global_entrance_index": native_global_index,
                })
                continue

            native_matches = global_entry.get("native_matches", [])
            resolved_matches = [
                match for match in native_matches if isinstance(match, dict) and
                match.get("scene_path") == native.get("scene_path") and
                match.get("setup_index") in setup_indices and
                match.get("local_entrance_index") == native.get("local_entrance_index") and
                match.get("spawn_resolved") is True and
                match.get("spawn_actor_name") == "ACTOR_PLAYER"
            ] if isinstance(native_matches, list) else []
            if any(
                not any(match.get("setup_index") == setup_index for match in resolved_matches)
                for setup_index in setup_indices
            ):
                invalid_mappings.append({
                    "semantic_key": semantic_key,
                    "reason": "native_scene_entry_spawn_unresolved",
                    "global_entrance_index": native_global_index,
                    "setup_indices": setup_indices,
                })
                continue
            resolved_entry_rooms = {
                match.get("entrance_room") for match in resolved_matches
                if isinstance(match.get("entrance_room"), int) and
                match.get("entrance_room") >= 0
            }
            if not resolved_entry_rooms or not resolved_entry_rooms.issubset(native_rooms):
                invalid_mappings.append({
                    "semantic_key": semantic_key,
                    "reason": "native_scene_entry_spawn_room_unmapped",
                    "entry_rooms": sorted(resolved_entry_rooms),
                    "mapped_native_rooms": sorted(native_rooms),
                })
                continue
            routes.append(_scene_entry_route(binding, semantic_source, scene_asset, global_entry))

    if cutscene_bindings is not None:
        if cutscene_bindings.get("format") != "oot3d_semantic_cutscene_bindings_v1":
            raise ValueError("unsupported semantic cutscene binding format")
        binding_rows = cutscene_bindings.get("records")
        if not isinstance(binding_rows, list):
            raise ValueError("semantic cutscene bindings require records")
        for binding in binding_rows:
            if not isinstance(binding, dict) or binding.get("status") != "resolved":
                continue
            semantic_key = str(binding.get("semantic_key", ""))
            scaffold = binding.get("scaffold", {})
            native = binding.get("native", {})
            if not semantic_key or not isinstance(scaffold, dict) or not isinstance(native, dict):
                invalid_mappings.append({"semantic_key": semantic_key, "reason": "cutscene_binding_shape"})
                continue
            scene_asset_id = str(native.get("scene_asset_id", ""))
            scene_asset = assets_by_id.get(scene_asset_id)
            if scene_asset is None or scene_asset.get("family") != "scene_profile":
                invalid_mappings.append({
                    "semantic_key": semantic_key,
                    "reason": "native_cutscene_scene_asset_missing",
                    "asset_id": scene_asset_id,
                })
                continue
            ownership = scene_asset.get("ownership", {})
            metadata = scene_asset.get("metadata", {})
            setup_index = native.get("setup_index")
            if (ownership.get("scene_id") != native.get("scene_id") or
                    metadata.get("scene_path") != native.get("scene_path") or
                    setup_index not in ownership.get("setup_indices", [])):
                invalid_mappings.append({
                    "semantic_key": semantic_key,
                    "reason": "native_cutscene_scene_ownership_mismatch",
                    "asset_id": scene_asset_id,
                })
                continue
            sequence_ids = native.get("sequence_asset_ids")
            if not isinstance(sequence_ids, list) or not sequence_ids:
                invalid_mappings.append({
                    "semantic_key": semantic_key, "reason": "native_cutscene_sequence_missing"
                })
                continue
            sequence_assets = [assets_by_id.get(str(asset_id)) for asset_id in sequence_ids]
            if any(asset is None or asset.get("family") != "cutscene_timeline"
                   for asset in sequence_assets):
                invalid_mappings.append({
                    "semantic_key": semantic_key,
                    "reason": "native_cutscene_timeline_asset_missing",
                    "sequence_asset_ids": sequence_ids,
                })
                continue
            if any(asset.get("ownership", {}).get("scene_id") != native.get("scene_id")
                   for asset in sequence_assets):
                invalid_mappings.append({
                    "semantic_key": semantic_key,
                    "reason": "native_cutscene_timeline_scene_mismatch",
                })
                continue
            routes.append(_cutscene_route(binding, scene_asset, sequence_assets))

    route_ids = [str(route["route_id"]) for route in routes]
    semantic_requests = []
    for route in routes:
        request = f"{route['kind']}:{route['scaffold']['semantic_key']}"
        if route["kind"] == "scene_entry":
            request += f":{route['scaffold']['variant_key']}"
        semantic_requests.append(request)
    duplicate_route_ids = sorted(key for key, count in Counter(route_ids).items() if count > 1)
    duplicate_semantic_requests = sorted(
        key for key, count in Counter(semantic_requests).items() if count > 1
    )
    scene_route_count = sum(route["kind"] == "scene" for route in routes)
    scene_entry_route_count = sum(route["kind"] == "scene_entry" for route in routes)
    cutscene_route_count = sum(route["kind"] == "cutscene" for route in routes)
    if invalid_mappings:
        status = "invalid_native_scene_mappings"
    elif duplicate_route_ids or duplicate_semantic_requests:
        status = "invalid_duplicate_routes"
    else:
        status = "complete"

    return {
        "format": FORMAT,
        "status": status,
        "generated_utc": now.isoformat(),
        "authority_policy": {
            "control_flow": "oot_n64_gameplay_scaffold",
            "content": "oot3d_native",
            "timing": "oot3d_native",
            "rendering": "oot3d_native",
            "emulator_role": "validation_only",
            "hud_menu_and_connected_logic": "oot_n64_ship",
        },
        "source_policy": scene_source_table.get("source_policy", {}),
        "summary": {
            "route_count": len(routes),
            "scene_route_count": scene_route_count,
            "scene_entry_route_count": scene_entry_route_count,
            "cutscene_route_count": cutscene_route_count,
            "resolved_source_scene_count": sum(
                1 for row in rows
                if row.get("native_scene_source_status") == "native_scene_source_resolved"
            ),
            "coverage_gap_count": len(coverage_gaps),
            "invalid_mapping_count": len(invalid_mappings),
        },
        "coverage_gaps": coverage_gaps,
        "invalid_mappings": invalid_mappings,
        "duplicate_route_ids": duplicate_route_ids,
        "duplicate_semantic_requests": duplicate_semantic_requests,
        "records": routes,
        "notes": [
            "N64 identities are control-plane requests only and never asset targets.",
            "Every resolved target is an OOT3D asset-catalog identity.",
            "Matching numeric scene ids remain separate scaffold and native fields.",
            "Scene-entry transforms, player parameters and camera selection are read from OOT3D; no N64 spatial values are transferred.",
            "N64 progression semantics select a verified OOT3D setup variant; native actor/object lists and params own population and configuration.",
            "Cutscene routes preserve ordered OOT3D QDB identities and native frame spans.",
            "HUD, menus and connected logic remain N64/Ship-owned for the current phase.",
        ],
    }


def render_semantic_route_markdown(catalog: dict[str, Any]) -> str:
    summary = catalog.get("summary", {})
    lines = [
        "# OOT3D Semantic Route Catalog",
        "",
        f"- Status: `{catalog.get('status', 'unknown')}`",
        f"- Resolved scene routes: `{summary.get('scene_route_count', 0)}`",
        f"- Resolved scene-entry routes: `{summary.get('scene_entry_route_count', 0)}`",
        f"- Resolved cutscene routes: `{summary.get('cutscene_route_count', 0)}`",
        f"- Source coverage gaps: `{summary.get('coverage_gap_count', 0)}`",
        f"- Invalid mappings: `{summary.get('invalid_mapping_count', 0)}`",
        "",
        "## Authority",
        "",
        "- Ship/N64 owns control-flow requests and gameplay consequences.",
        "- OOT3D owns content, timing and rendering after route resolution.",
        "- Emulator evidence validates behavior but is never packaged as runtime data.",
        "- HUD, menus and their connected logic remain on the N64/Ship path for now.",
        "",
        "## Scene Routes",
        "",
        "| Semantic request | Scaffold id | OOT3D target | Setups |",
        "| --- | ---: | --- | ---: |",
    ]
    for route in catalog.get("records", []):
        if route.get("kind") != "scene":
            continue
        native = route["native"]
        scaffold = route["scaffold"]
        lines.append(
            f"| `{scaffold['semantic_key']}` | `0x{int(scaffold['scene_id']):02X}` | "
            f"`{native['asset_id']}` | {len(native.get('setup_indices', []))} |"
        )
    scene_entry_routes = [route for route in catalog.get("records", [])
                          if route.get("kind") == "scene_entry"]
    if scene_entry_routes:
        lines.extend([
            "",
            "## Scene Entry Routes",
            "",
            "| Semantic request | Gameplay variant | Offline conditions | Ship trigger | OOT3D global/local entry | Room map | Native target |",
            "| --- | --- | --- | --- | --- | --- | --- |",
        ])
        for route in scene_entry_routes:
            scaffold = route["scaffold"]
            native = route["native"]
            conditions = ", ".join(
                f"{condition['fact']}={condition['equals']}"
                for condition in scaffold.get("variant_conditions", [])
            )
            room_map = ", ".join(
                f"{room['scaffold_room_index']}->{room['native_room_index']}"
                for room in native.get("room_bindings", [])
            )
            lines.append(
                f"| `{scaffold['semantic_key']}` | `{scaffold['variant_key']}` | `{conditions}` | "
                f"`{scaffold['entrance_symbol']}` "
                f"(`0x{int(scaffold['entrance_index']):04X}`) | "
                f"`0x{int(native['global_entrance_index']):04X}` / "
                f"`{int(native['local_entrance_index'])}` | `{room_map}` | `{native['asset_id']}` |"
            )
    cutscene_routes = [route for route in catalog.get("records", [])
                       if route.get("kind") == "cutscene"]
    if cutscene_routes:
        lines.extend([
            "",
            "## Cutscene Routes",
            "",
            "| Semantic request | Ship trigger | OOT3D setup | Native QDB sequence |",
            "| --- | --- | ---: | ---: |",
        ])
        for route in cutscene_routes:
            scaffold = route["scaffold"]
            native = route["native"]
            lines.append(
                f"| `{scaffold['semantic_key']}` | `{scaffold['game_state']}` / "
                f"`{scaffold['scene_setup_index']}` / `0x{int(scaffold['cutscene_index']):04X}` | "
                f"{native['setup_indices'][0]} | {len(native['sequence_asset_ids'])} |"
            )
    if catalog.get("coverage_gaps"):
        lines.extend([
            "",
            "## Coverage Gaps",
            "",
            "| Scene | Semantic request | Native path | Reason |",
            "| ---: | --- | --- | --- |",
        ])
        for gap in catalog["coverage_gaps"]:
            scene_id = gap.get("scene_id")
            scene_text = f"0x{scene_id:02X}" if isinstance(scene_id, int) else "unknown"
            lines.append(
                f"| `{scene_text}` | `{gap.get('semantic_key') or ''}` | "
                f"`{gap.get('scene_path') or ''}` | `{gap.get('reason') or ''}` |"
            )
    lines.append("")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Generate semantic gameplay-to-native OOT3D route bindings."
    )
    parser.add_argument("--asset-catalog", type=Path, required=True)
    parser.add_argument("--scene-source-table", type=Path, required=True)
    parser.add_argument("--cutscene-bindings", type=Path)
    parser.add_argument("--semantic-gameplay-source", type=Path)
    parser.add_argument("--scene-entry-bindings", type=Path)
    parser.add_argument("--global-entrance-table", type=Path)
    parser.add_argument("--output-json", type=Path, required=True)
    parser.add_argument("--output-md", type=Path, required=True)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args(argv)

    asset_catalog = json.loads(args.asset_catalog.read_text(encoding="utf-8-sig"))
    scene_sources = json.loads(args.scene_source_table.read_text(encoding="utf-8-sig"))
    cutscene_bindings = (json.loads(args.cutscene_bindings.read_text(encoding="utf-8-sig"))
                         if args.cutscene_bindings is not None else None)
    semantic_gameplay_source = (
        json.loads(args.semantic_gameplay_source.read_text(encoding="utf-8-sig"))
        if args.semantic_gameplay_source is not None else None
    )
    scene_entry_bindings = (
        json.loads(args.scene_entry_bindings.read_text(encoding="utf-8-sig"))
        if args.scene_entry_bindings is not None else None
    )
    global_entrance_table = (
        json.loads(args.global_entrance_table.read_text(encoding="utf-8-sig"))
        if args.global_entrance_table is not None else None
    )
    result = build_semantic_route_catalog(
        asset_catalog,
        scene_sources,
        cutscene_bindings,
        scene_entry_bindings,
        global_entrance_table,
        semantic_gameplay_source=semantic_gameplay_source,
    )
    result["manifest_inputs"] = [
        {
            "kind": "asset_catalog",
            "path": str(args.asset_catalog),
            "sha256": hashlib.sha256(args.asset_catalog.read_bytes()).hexdigest(),
        },
        {
            "kind": "scene_source_table",
            "path": str(args.scene_source_table),
            "sha256": hashlib.sha256(args.scene_source_table.read_bytes()).hexdigest(),
        },
    ]
    if args.cutscene_bindings is not None:
        result["manifest_inputs"].append({
            "kind": "cutscene_bindings",
            "path": str(args.cutscene_bindings),
            "sha256": hashlib.sha256(args.cutscene_bindings.read_bytes()).hexdigest(),
        })
    if args.scene_entry_bindings is not None:
        result["manifest_inputs"].append({
            "kind": "scene_entry_bindings",
            "path": str(args.scene_entry_bindings),
            "sha256": hashlib.sha256(args.scene_entry_bindings.read_bytes()).hexdigest(),
        })
    if args.semantic_gameplay_source is not None:
        result["manifest_inputs"].append({
            "kind": "semantic_gameplay_source",
            "path": str(args.semantic_gameplay_source),
            "sha256": hashlib.sha256(args.semantic_gameplay_source.read_bytes()).hexdigest(),
        })
    if args.global_entrance_table is not None:
        result["manifest_inputs"].append({
            "kind": "global_entrance_table",
            "path": str(args.global_entrance_table),
            "sha256": hashlib.sha256(args.global_entrance_table.read_bytes()).hexdigest(),
        })
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_md.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(
        json.dumps(result, indent=2) + "\n", encoding="utf-8", newline="\n"
    )
    args.output_md.write_text(
        render_semantic_route_markdown(result), encoding="utf-8", newline="\n"
    )
    print(args.output_json)
    print(args.output_md)
    return 1 if args.verify and result["status"] != "complete" else 0


if __name__ == "__main__":
    raise SystemExit(main())
