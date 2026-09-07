from __future__ import annotations

from pathlib import Path
import json
import pytest
import zipfile

from oot3d_asset_tool.playable_pack import (
    ARCHIVES, CATALOG_ARCHIVE_PATH, LIGHTING_SEMANTICS_ARCHIVE_PATH,
    LIGHT_TRANSITION_TABLE_ARCHIVE_PATH, LIGHT_TRANSITION_TABLE_PROVENANCE_ARCHIVE_PATH,
    LIGHT_TRANSITION_FALLBACK_ARCHIVE_PATH,
    NATIVE_ABI_CATALOG_ARCHIVE_PATH, PACK_ARCHIVE_PATH,
    PLAYER_MODEL_RESOURCE_PROFILE_ARCHIVE_PATH,
    ROOM_COMPILATION_UNIT_CATALOG_ARCHIVE_PATH,
    SEMANTIC_ROUTE_CATALOG_ARCHIVE_PATH,
    build_playable_pack, build_player_model_resource_profile,
    build_room_compilation_unit_catalog, write_core_archive)
from oot3d_asset_tool.room_compilation_unit import (
    COMPILER_VERSION,
    canonical_sha256,
)


def _record(asset_id: str, family: str, model_kind: str | None = None) -> dict:
    return {"asset_id": asset_id, "family": family, "metadata": {"model_kind": model_kind},
            "required_engine_capabilities": ["native_pica_material"]}


def _room_lifecycle() -> dict:
    roles = (
        "initialize",
        "request",
        "process_request",
        "destroy",
        "cleanup_room_actors",
        "spawn_transition_actors",
        "queue_resource_cleanup",
        "process_resource_cleanup",
    )
    return {
        "format": "oot3d_room_lifecycle_contract_v1",
        "status": "workflow_semantics_recovered",
        "functions": {
            role: {"address": f"0x{index + 1:08X}", "name": role}
            for index, role in enumerate(roles)
        },
        "state_model": {
            "idle": 0,
            "loading": 1,
            "terminal": 2,
            "max_resident_room_count": 2,
            "initial_buffer_count": 1,
            "buffer_count_without_transition_actors": 1,
            "buffer_count_with_transition_actors": 3,
        },
        "actor_residency": {
            "room_actor_retention_policy": "negative_room_or_current_or_previous",
            "transition_actor_residency_policy": (
                "front_or_back_matches_current_or_previous"
            ),
            "transition_actor_id_spawn_mask": 0x1FFF,
            "transition_actor_params_index_stride": 0x400,
            "transition_actor_spawn_marker": "negate_source_actor_id",
        },
        "resource_residency": {
            "previous_room_cleanup_before_next_request": True,
            "cleanup_delay_ticks": 1,
            "room_commands_install_on_request_completion": True,
        },
    }


def _room_unit(route_id: str, *, composition_complete: bool = True) -> dict:
    status = ("composition_complete_behavior_incomplete" if composition_complete
              else "composition_incomplete")
    composition_status = ("composition_complete" if composition_complete
                          else "composition_incomplete")
    unit = {
        "format": "oot3d_room_compilation_unit_v1",
        "schema_version": 1,
        "status": status,
        "identity": {
            "route_id": route_id,
            "unit_id": f"{route_id}:setup-0",
            "scene_id": 85,
            "scene_path": "spot04_info.zsi",
            "setup_index": 0,
            "native_room_indices": [0],
            "initial_room_index": 0,
        },
        "source_assets": {},
        "room_callback_contract": {},
        "entrypoint": {
            "global_entrance_index": 529,
            "local_entrance_index": 3,
            "spawn_index": 3,
            "room_index": 0,
            "entry": {
                "actor_id": 0,
                "actor_name": "ACTOR_PLAYER",
                "index": 3,
                "offset": 128,
                "params": 0x0DFF,
                "pos": [-31, 100, 1073],
                "rot": [0, -32767, 0],
            },
        },
        "rooms": [{
            "room_index": 0,
            "setup_index": 0,
            "initially_active": True,
            "commands": [{
                "command_id": 0x14,
                "parameter": 0,
                "command_word": 0x14,
                "argument": 0,
            }],
        }],
        "actor_instances": [{
            "instance_key": "player-entry",
            "profile_key": "actor:0x0000",
            "room_index": 0,
            "source_kind": "scene_spawn",
            "entry": {
                "actor_id": 0,
                "actor_name": "ACTOR_PLAYER",
                "index": 3,
                "offset": 128,
                "params": 0x0DFF,
                "pos": [-31, 100, 1073],
                "rot": [0, -32767, 0],
            },
        }],
        "actor_profiles": [{
            "profile_key": "actor:0x0000",
            "actor_id": 0,
            "actor_name": "ACTOR_PLAYER",
            "object_id": 1,
            "object_rom_path": "actor/zelda_keep.zar",
            "profile_address": "0x0053C904",
            "runtime_binding_status": "consumer_must_bind_native_profile",
            "behavior_graph": {
                "status": "workflow_graph_unavailable",
                "owner": "Player",
                "structure": "Oot3dPlayer",
                "structure_size": None,
                "function_count": 0,
                "action_transition_count": 0,
                "structure_field_count": 0,
                "initial_action_candidates": [],
                "functions": [],
                "action_transitions": [],
                "structure_fields": [],
                "runtime_binding_status": "behavior_evidence_not_available",
            },
        }],
        "object_dependencies": [{
            "object_id": 1,
            "payload_status": "native_zar_materialized",
        }],
        "closure": {
            "composition_status": composition_status,
            "behavior_evidence_status": "behavior_evidence_incomplete",
            "runtime_binding_status": "consumer_binding_required",
            "room_count": 1,
            "actor_instance_count": 1,
            "unique_actor_profile_count": 1,
            "object_dependency_count": 1,
            "behavior_graph_profile_count": 0,
            "behavior_function_count": 0,
            "behavior_action_transition_count": 0,
            "behavior_structure_field_count": 0,
            "unresolved_count": 0 if composition_complete else 1,
        },
        "unresolved": ([] if composition_complete else [{
            "severity": "composition",
            "kind": "test_gap",
            "key": route_id,
        }]),
    }
    if composition_complete:
        unit["room_lifecycle_contract"] = _room_lifecycle()
    unit["identity"]["payload_sha256"] = canonical_sha256(unit)
    return unit


def test_playable_pack_assigns_only_proven_shards(tmp_path: Path) -> None:
    for archives in ARCHIVES.values():
        for relative in archives:
            path = tmp_path / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"archive")
    catalog = {"format": "oot3d_asset_catalog_v1", "status": "complete", "records": [
        _record("cmb:rigid", "actor_model", "rigid"),
        _record("cmb:skinned", "actor_model", "skinned"),
        _record("csab:idle", "skeletal_animation"),
        _record("collision:scene", "scene_collision"),
    ]}
    result = build_playable_pack(catalog, tmp_path)
    shards = {shard["name"]: shard for shard in result["shards"]}
    assert shards["oot3d-actors-static"]["status"] == "ready"
    assert shards["oot3d-characters"]["record_count"] == 2
    assert result["unassigned_blocker_counts"] == {"scene_shard_classification_missing": 1}


def test_scene_profiles_are_core_metadata(tmp_path: Path) -> None:
    catalog = {"format": "oot3d_asset_catalog_v1", "status": "complete",
               "records": [_record("scene:test_info.zsi", "scene_profile")]}
    result = build_playable_pack(catalog, tmp_path)
    core = next(shard for shard in result["shards"] if shard["name"] == "oot3d-core")
    assert core["status"] == "ready"
    assert core["record_count"] == 1


def test_playable_pack_reports_missing_archive(tmp_path: Path) -> None:
    catalog = {"format": "oot3d_asset_catalog_v1", "status": "complete",
               "records": [_record("cmb:rigid", "actor_model", "rigid")]}
    result = build_playable_pack(catalog, tmp_path)
    shard = next(shard for shard in result["shards"] if shard["name"] == "oot3d-actors-static")
    assert shard["status"] == "blocked_missing_archive"


def test_playable_pack_assigns_qdb_timelines_to_native_cutscene_shard(tmp_path: Path) -> None:
    archive = tmp_path / ARCHIVES["oot3d-cutscenes"][0]
    archive.parent.mkdir(parents=True)
    archive.write_bytes(b"qdb archive")
    catalog = {"format": "oot3d_asset_catalog_v1", "status": "complete",
               "records": [_record("qdb:scene/test.zar!demo/test.qdb", "cutscene_timeline")]}

    result = build_playable_pack(catalog, tmp_path)

    shard = next(row for row in result["shards"] if row["name"] == "oot3d-cutscenes")
    assert shard["status"] == "ready"
    assert shard["record_count"] == 1


def test_core_archive_contains_catalog_and_pack(tmp_path: Path) -> None:
    catalog = {"format": "oot3d_asset_catalog_v1", "status": "complete", "records": []}
    pack = build_playable_pack(catalog, tmp_path)
    output = tmp_path / "oot3d-core.o2r"
    semantics = {"format": "oot3d_pica_lighting_semantics_v1"}
    transition = bytes(range(32))
    provenance = {"format": "oot3d_light_settings_transition_table_v1"}
    fallback = bytes(range(16))
    routes = {"format": "oot3d_semantic_route_catalog_v1", "status": "complete", "records": []}
    player_resources = {"format": "oot3d_player_model_resource_profile_v1"}
    native_abi = {"format": "oot3d_native_abi_catalog_v1"}
    room_unit_root = tmp_path / "room_units"
    room_unit_root.mkdir()
    (room_unit_root / "kokiri.json").write_text(
        json.dumps(_room_unit("scene_entry:kokiri")), encoding="utf-8"
    )
    room_units, room_payloads = build_room_compilation_unit_catalog(room_unit_root)
    write_core_archive(output, catalog, pack, semantics, transition, provenance, fallback,
                       semantic_routes=routes, player_model_resource_profile=player_resources,
                       native_abi_catalog=native_abi,
                       room_compilation_unit_catalog=room_units,
                       room_compilation_units=room_payloads)
    with zipfile.ZipFile(output) as archive:
        assert json.loads(archive.read(CATALOG_ARCHIVE_PATH))["format"] == "oot3d_asset_catalog_v1"
        assert json.loads(archive.read(PACK_ARCHIVE_PATH))["format"] == "oot3d_playable_pack_v1"
        assert json.loads(archive.read(LIGHTING_SEMANTICS_ARCHIVE_PATH))["format"] == semantics["format"]
        assert archive.read(LIGHT_TRANSITION_TABLE_ARCHIVE_PATH) == transition
        assert json.loads(archive.read(LIGHT_TRANSITION_TABLE_PROVENANCE_ARCHIVE_PATH))["format"] == provenance["format"]
        assert archive.read(LIGHT_TRANSITION_FALLBACK_ARCHIVE_PATH) == fallback
        assert json.loads(archive.read(SEMANTIC_ROUTE_CATALOG_ARCHIVE_PATH))["format"] == routes["format"]
        assert json.loads(archive.read(PLAYER_MODEL_RESOURCE_PROFILE_ARCHIVE_PATH))["format"] == player_resources["format"]
        assert json.loads(archive.read(NATIVE_ABI_CATALOG_ARCHIVE_PATH))["format"] == native_abi["format"]
        room_catalog = json.loads(archive.read(ROOM_COMPILATION_UNIT_CATALOG_ARCHIVE_PATH))
        assert room_catalog["unit_count"] == 1
        room_resource = room_catalog["records"][0]["resource_path"]
        assert json.loads(archive.read(room_resource))["identity"]["route_id"] == "scene_entry:kokiri"


def test_room_compilation_unit_catalog_excludes_incomplete_units(tmp_path: Path) -> None:
    complete = _room_unit("scene_entry:kokiri")
    incomplete = _room_unit("scene_entry:jabu", composition_complete=False)
    (tmp_path / "kokiri.json").write_text(json.dumps(complete), encoding="utf-8")
    (tmp_path / "jabu.json").write_text(json.dumps(incomplete), encoding="utf-8")

    catalog, payloads = build_room_compilation_unit_catalog(tmp_path)

    assert catalog["status"] == "complete"
    assert catalog["unit_count"] == 1
    assert catalog["excluded_unit_count"] == 1
    assert catalog["records"][0]["route_id"] == "scene_entry:kokiri"
    assert catalog["excluded"][0]["reason"] == "composition_incomplete"
    assert list(payloads) == [catalog["records"][0]["resource_path"]]


def test_room_compilation_unit_catalog_excludes_obsolete_contracts(
    tmp_path: Path,
) -> None:
    current = _room_unit("scene_entry:kokiri")
    legacy = _room_unit("scene:jabu")
    legacy["provenance"] = {
        "compiler": {
            "name": "oot3d_asset_tool.room_compilation_unit",
            "version": "1.1.0",
        }
    }
    for profile in legacy["actor_profiles"]:
        profile.pop("behavior_graph")
    for key in (
        "behavior_graph_profile_count",
        "behavior_function_count",
        "behavior_action_transition_count",
        "behavior_structure_field_count",
    ):
        legacy["closure"].pop(key)
    legacy["identity"].pop("payload_sha256")
    legacy["identity"]["payload_sha256"] = canonical_sha256(legacy)
    (tmp_path / "kokiri.json").write_text(json.dumps(current), encoding="utf-8")
    (tmp_path / "jabu.json").write_text(json.dumps(legacy), encoding="utf-8")

    catalog, payloads = build_room_compilation_unit_catalog(tmp_path)

    assert catalog["unit_count"] == 1
    assert catalog["excluded_unit_count"] == 1
    assert catalog["excluded"][0]["reason"] == "obsolete_room_unit_contract"
    assert catalog["excluded"][0]["compiler_version"] == "1.1.0"
    assert catalog["excluded"][0]["required_compiler_version"] == COMPILER_VERSION
    assert list(payloads) == [catalog["records"][0]["resource_path"]]


def test_room_compilation_unit_catalog_excludes_scene_without_entrypoint(
    tmp_path: Path,
) -> None:
    unit = _room_unit("scene:SCENE_JABU_JABU")
    unit["identity"]["initial_room_index"] = None
    unit["entrypoint"] = None
    unit["identity"].pop("payload_sha256")
    unit["identity"]["payload_sha256"] = canonical_sha256(unit)
    (tmp_path / "jabu.json").write_text(json.dumps(unit), encoding="utf-8")

    catalog, payloads = build_room_compilation_unit_catalog(tmp_path)

    assert catalog["unit_count"] == 0
    assert catalog["excluded_unit_count"] == 1
    assert catalog["excluded"][0]["reason"] == "native_entrypoint_missing"
    assert payloads == {}


def test_room_compilation_unit_catalog_rejects_duplicate_route_setup(tmp_path: Path) -> None:
    unit = _room_unit("scene_entry:kokiri")
    (tmp_path / "first.json").write_text(json.dumps(unit), encoding="utf-8")
    (tmp_path / "second.json").write_text(json.dumps(unit), encoding="utf-8")

    with pytest.raises(ValueError, match="duplicate room compilation unit route/setup"):
        build_room_compilation_unit_catalog(tmp_path)


def test_player_model_resource_profile_decodes_native_tables() -> None:
    code_base = 0x00100000
    code = bytearray(0x00440000)

    def put_u8(address: int, value: int) -> None:
        code[address - code_base] = value

    def put_u32(address: int, value: int) -> None:
        code[address - code_base:address - code_base + 4] = value.to_bytes(4, "little")

    model_groups = 0x0053A558
    resource_pointers = 0x0053C698
    body_resources = 0x004DC388
    put_u32(0x0032C3F8, model_groups)
    put_u32(0x0032C3FC, resource_pointers)
    put_u32(0x004C4794, body_resources)
    for group in range(16):
        row = [0, 0, 8, 18, 20] if group == 3 else [0, 0, 0, 0, 20]
        for column, value in enumerate(row):
            put_u8(model_groups + group * 5 + column, value)
    for model_type in range(21):
        table = 0x00530000 + model_type * 0x100
        put_u32(resource_pointers + model_type * 4, table)
        for shield in range(4):
            base = table + shield * 0x10
            put_u32(base, 100 + model_type + shield)
            put_u32(base + 4, model_type + shield)
            put_u32(base + 8, 100 + model_type + shield)
            put_u32(base + 12, model_type + shield)
    for slot, pair in enumerate(((45, 24), (45, 24), (46, 25), (47, 26))):
        put_u32(body_resources + slot * 8, pair[0])
        put_u32(body_resources + slot * 8 + 4, pair[1])

    profile = build_player_model_resource_profile(bytes(code))
    assert profile["format"] == "oot3d_player_model_resource_profile_v1"
    assert profile["body_resource_ids_by_age"] == [[45, 45, 46, 47], [24, 24, 25, 26]]
    assert profile["model_groups"][3]["model_types"] == [0, 8, 18, 20]
    assert profile["model_types"][18]["selection_kind"] == "shield_then_age"
    assert profile["model_types"][18]["shield_variant_resource_ids_by_age"][2] == [120, 20]
