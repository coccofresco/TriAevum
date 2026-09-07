from __future__ import annotations

from datetime import datetime, timezone

from oot3d_asset_tool.semantic_route_catalog import (
    build_semantic_route_catalog,
    render_semantic_route_markdown,
)


def _asset_catalog(*records: dict) -> dict:
    return {
        "format": "oot3d_asset_catalog_v1",
        "status": "complete",
        "records": list(records),
    }


def _scene_asset(scene_id: int, path: str) -> dict:
    return {
        "asset_id": f"scene:{path}",
        "family": "scene_profile",
        "metadata": {"scene_path": path},
        "ownership": {"scene_id": scene_id, "setup_indices": [0, 3]},
    }


def _room_asset(scene_id: int, room_index: int) -> dict:
    return {
        "asset_id": f"room:spot04_{room_index}_info.zsi",
        "family": "scene_room_source",
        "ownership": {"scene_id": scene_id, "room_index": room_index},
    }


def _qdb_asset(scene_id: int, asset_id: str, end_frame: int) -> dict:
    return {
        "asset_id": asset_id,
        "family": "cutscene_timeline",
        "canonical_resources": ["oot3d/native/qdb/test.qdb"],
        "metadata": {"command_count": 5, "end_frame": end_frame},
        "ownership": {"scene_id": scene_id},
    }


def _scene_source(scene_id: int, path: str, semantic_key: str, *, resolved: bool = True) -> dict:
    return {
        "scene_id": scene_id,
        "secondary_scene_enum": semantic_key,
        "zsi_path": path,
        "scene_stem": path.removesuffix("_info.zsi"),
        "native_scene_source_status": (
            "native_scene_source_resolved" if resolved else "native_scene_source_missing"
        ),
        "scene_index_symbol": f"oot3d_scene_index_{path.removesuffix('.zsi')}",
        "resource_record_address_hex": "0x00548000",
    }


def _scene_entry_bindings(*, native_global_index: int = 900) -> dict:
    return {
        "format": "oot3d_semantic_scene_entry_bindings_v1",
        "records": [{
            "semantic_key": "SCENE_ENTRY_KOKIRI_FOREST_FROM_LINKS_HOUSE",
            "variant_key": "KOKIRI_FOREST_INITIAL_CHILD_DAY",
            "status": "resolved",
            "native": {
                "scene_asset_id": "scene:spot04_info.zsi",
                "scene_id": 0x55,
                "scene_path": "spot04_info.zsi",
                "scene_stem": "spot04",
                "global_entrance_index": native_global_index,
                "local_entrance_index": 3,
                "setup_indices": [0],
                "setup_selection": "oot3d_new_game_child_day_scene_setup",
                "room_bindings": [
                    {"scaffold_room_index": 0, "native_room_index": 0},
                ],
                "population_source": "oot3d_zsi_actor_and_object_lists",
                "actor_configuration_source": "oot3d_zsi_actor_params",
            },
        }],
    }


def _semantic_gameplay_source() -> dict:
    return {
        "format": "oot3d_semantic_gameplay_source_v1",
        "records": [{
            "semantic_key": "SCENE_ENTRY_KOKIRI_FOREST_FROM_LINKS_HOUSE",
            "kind": "scene_entry",
            "status": "resolved",
            "trigger": {
                "scene_id": 0x55,
                "entrance_index": 0x211,
                "entrance_symbol": "ENTR_KOKIRI_FOREST_OUTSIDE_LINKS_HOUSE",
                "origin_semantic_key": "SCENE_LINKS_HOUSE_EXIT",
            },
            "variants": [{
                "variant_key": "KOKIRI_FOREST_INITIAL_CHILD_DAY",
                "conditions": [
                    {"fact": "player.age", "equals": "child"},
                    {"fact": "world.day_phase", "equals": "day"},
                    {"fact": "gameplay.layer", "equals": "normal"},
                ],
            }],
        }],
    }


def _global_entrance_table(*, entrance_index: int = 900,
                           local_entrance_index: int = 3) -> dict:
    return {
        "summary": {"format": "oot3d_scene_global_entrance_table_v2"},
        "rows": [{
            "entrance_index": entrance_index,
            "status": "decoded_from_code_bin",
            "scene_id": 0x55,
            "local_entrance_index": local_entrance_index,
            "native_scene_resource_zsi_path": "spot04_info.zsi",
            "native_matches": [{
                "scene_path": "spot04_info.zsi",
                "setup_index": 0,
                "local_entrance_index": local_entrance_index,
                "spawn_resolved": True,
                "spawn_actor_name": "ACTOR_PLAYER",
                "entrance_room": 0,
                "spawn_pos": [-31, 100, 1073],
                "spawn_rot": [0, -32767, 0],
                "spawn_params": 0x0DFF,
            }],
            "native_validation_status": "native_scene_candidate_spawn_resolved",
            "table_file_offset_hex": "0x4443fc",
        }],
    }


def test_builds_native_scene_routes_with_separate_authority_fields() -> None:
    catalog = build_semantic_route_catalog(
        _asset_catalog(_scene_asset(0x55, "spot04_info.zsi")),
        {"rows": [_scene_source(0x55, "spot04_info.zsi", "SCENE_KOKIRI_FOREST")]},
        now=datetime(2026, 7, 13, tzinfo=timezone.utc),
    )

    assert catalog["status"] == "complete"
    assert catalog["summary"]["scene_route_count"] == 1
    route = catalog["records"][0]
    assert route["scaffold"] == {
        "system": "oot_n64_gameplay",
        "request_kind": "scene",
        "semantic_key": "SCENE_KOKIRI_FOREST",
        "scene_id": 0x55,
    }
    assert route["native"]["asset_id"] == "scene:spot04_info.zsi"
    assert route["native"]["setup_indices"] == [0, 3]
    assert route["authority"]["content"] == "oot3d_native"
    assert route["authority"]["timing"] == "oot3d_native"
    assert route["completion_event"] == "scene_exit_requested"
    assert "SCENE_KOKIRI_FOREST" in render_semantic_route_markdown(catalog)


def test_keeps_unresolved_native_scenes_as_coverage_gaps() -> None:
    catalog = build_semantic_route_catalog(
        _asset_catalog(),
        {"rows": [_scene_source(0x65, "test01_info.zsi", "SCENE_TEST01", resolved=False)]},
    )

    assert catalog["status"] == "complete"
    assert catalog["records"] == []
    assert catalog["summary"]["coverage_gap_count"] == 1
    assert catalog["coverage_gaps"][0]["reason"] == "native_scene_source_missing"


def test_rejects_resolved_scene_without_matching_native_asset() -> None:
    catalog = build_semantic_route_catalog(
        _asset_catalog(),
        {"rows": [_scene_source(0x51, "spot00_info.zsi", "SCENE_HYRULE_FIELD")]},
    )

    assert catalog["status"] == "invalid_native_scene_mappings"
    assert catalog["invalid_mappings"][0]["reason"] == "native_scene_asset_missing"


def test_rejects_native_scene_path_mismatch() -> None:
    catalog = build_semantic_route_catalog(
        _asset_catalog(_scene_asset(0x51, "wrong_info.zsi")),
        {"rows": [_scene_source(0x51, "spot00_info.zsi", "SCENE_HYRULE_FIELD")]},
    )

    assert catalog["status"] == "invalid_native_scene_mappings"
    assert catalog["invalid_mappings"][0]["reason"] == "native_scene_path_mismatch"


def test_routes_n64_opening_request_to_ordered_native_qdb_sequence() -> None:
    first = "qdb:scene/spot00.zar!demo/epona_00.qdb"
    second = "qdb:scene/spot00.zar!demo/epona_01.qdb"
    bindings = {
        "format": "oot3d_semantic_cutscene_bindings_v1",
        "records": [{
            "semantic_key": "CUTSCENE_OPENING_TITLE",
            "status": "resolved",
            "scaffold": {"game_state": "Opening", "scene_id": 0x51,
                         "scene_setup_index": 7, "cutscene_index": 0xFFF3},
            "native": {"scene_asset_id": "scene:spot00_info.zsi", "scene_id": 0x51,
                       "scene_path": "spot00_info.zsi", "scene_stem": "spot00",
                       "setup_index": 6, "sequence_asset_ids": [first, second]},
            "completion_event": "cutscene_finished",
        }],
    }
    scene_asset = _scene_asset(0x51, "spot00_info.zsi")
    scene_asset["ownership"]["setup_indices"].append(6)
    catalog = build_semantic_route_catalog(
        _asset_catalog(scene_asset,
                       _qdb_asset(0x51, first, 1680), _qdb_asset(0x51, second, 2595)),
        {"rows": [_scene_source(0x51, "spot00_info.zsi", "SCENE_HYRULE_FIELD")]},
        bindings,
    )

    assert catalog["status"] == "complete"
    assert catalog["summary"]["cutscene_route_count"] == 1
    route = next(row for row in catalog["records"] if row["kind"] == "cutscene")
    assert route["scaffold"]["scene_setup_index"] == 7
    assert route["native"]["setup_indices"] == [6]
    assert route["native"]["sequence_asset_ids"] == [first, second]
    assert [row["end_frame"] for row in route["native"]["sequence"]] == [1680, 2595]
    assert route["authority"]["timing"] == "oot3d_native"
    assert route["completion_event"] == "cutscene_finished"


def test_routes_semantic_scene_entry_to_independent_native_entrance() -> None:
    catalog = build_semantic_route_catalog(
        _asset_catalog(_scene_asset(0x55, "spot04_info.zsi"), _room_asset(0x55, 0)),
        {"rows": [_scene_source(0x55, "spot04_info.zsi", "SCENE_KOKIRI_FOREST")]},
        None,
        _scene_entry_bindings(native_global_index=900),
        _global_entrance_table(entrance_index=900),
        semantic_gameplay_source=_semantic_gameplay_source(),
    )

    assert catalog["status"] == "complete"
    assert catalog["summary"]["scene_entry_route_count"] == 1
    route = next(row for row in catalog["records"] if row["kind"] == "scene_entry")
    assert route["scaffold"]["entrance_index"] == 0x211
    assert route["scaffold"]["variant_source"] == "oot_n64_semantic_source_offline"
    assert route["scaffold"]["variant_conditions"][0] == {
        "fact": "player.age", "equals": "child"
    }
    assert route["native"]["global_entrance_index"] == 900
    assert route["native"]["local_entrance_index"] == 3
    assert route["native"]["spatial_source"] == "oot3d_zsi_spawn_entry"
    assert route["native"]["room_bindings"] == [
        {"scaffold_room_index": 0, "native_room_index": 0}
    ]
    assert route["native"]["camera_source"].startswith("oot3d_zsi_player_params")
    assert route["authority"]["spatial_state"] == "oot3d_native"
    assert route["scaffold"]["variant_key"] == "KOKIRI_FOREST_INITIAL_CHILD_DAY"
    assert route["native"]["population_source"] == "oot3d_zsi_actor_and_object_lists"
    assert route["authority"]["behavior_semantics"].startswith("oot_n64_semantic_scaffold")
    assert "spawn_pos" not in route["native"]
    assert "spawn_rot" not in route["native"]
    assert "Scene Entry Routes" in render_semantic_route_markdown(catalog)


def test_rejects_scene_entry_without_matching_native_spawn() -> None:
    table = _global_entrance_table()
    table["rows"][0]["native_matches"][0]["spawn_resolved"] = False
    catalog = build_semantic_route_catalog(
        _asset_catalog(_scene_asset(0x55, "spot04_info.zsi"), _room_asset(0x55, 0)),
        {"rows": [_scene_source(0x55, "spot04_info.zsi", "SCENE_KOKIRI_FOREST")]},
        None,
        _scene_entry_bindings(),
        table,
        semantic_gameplay_source=_semantic_gameplay_source(),
    )

    assert catalog["status"] == "invalid_native_scene_mappings"
    assert catalog["summary"]["scene_entry_route_count"] == 0
    assert catalog["invalid_mappings"][0]["reason"] == "native_scene_entry_spawn_unresolved"


def test_rejects_scene_entry_room_mapping_without_native_room_asset() -> None:
    catalog = build_semantic_route_catalog(
        _asset_catalog(_scene_asset(0x55, "spot04_info.zsi")),
        {"rows": [_scene_source(0x55, "spot04_info.zsi", "SCENE_KOKIRI_FOREST")]},
        None,
        _scene_entry_bindings(),
        _global_entrance_table(),
        semantic_gameplay_source=_semantic_gameplay_source(),
    )

    assert catalog["status"] == "invalid_native_scene_mappings"
    assert catalog["invalid_mappings"][0]["reason"] == "native_scene_entry_room_binding_invalid"
