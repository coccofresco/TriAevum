from __future__ import annotations

import hashlib
import json
from collections import defaultdict

import pytest

from oot3d_asset_tool.room_compilation_unit import (
    EVIDENCE_SNAPSHOT_FORMAT,
    FORMAT,
    SCHEMA_VERSION,
    EvidenceIndex,
    EvidenceSnapshot,
    canonical_sha256,
    catalog_source_container,
    load_room_callback_runtime_contracts,
    reconcile_indirect_root_evidence,
    validate_actor_behavior_graph,
    validate_room_compilation_unit,
)


def test_indirect_root_evidence_accepts_typed_actor_refinement() -> None:
    roots = [
        {"entry": "0x00100000", "name": "PlayerRoot"},
        {"entry": "0x00100040", "name": "ActorRoot_Generic"},
    ]
    catalog = {
        "functions": [
            {
                "address": "0x00100000",
                "name": "PlayerRoot",
                "closure_kind": "indirect_root",
            },
            {
                "address": "0x00100040",
                "name": "EnKo_Action",
                "closure_kind": "actor_priority_native",
                "superseded_evidence": [
                    {
                        "address": "0x00100040",
                        "name": "ActorRoot_Generic",
                        "closure_kind": "indirect_root",
                    }
                ],
            },
        ]
    }

    active = reconcile_indirect_root_evidence(catalog, roots)

    assert [row["name"] for row in active] == ["PlayerRoot"]


def test_indirect_root_evidence_rejects_unexplained_omission() -> None:
    with pytest.raises(ValueError, match="differs from source evidence"):
        reconcile_indirect_root_evidence(
            {"functions": []},
            [{"entry": "0x00100000", "name": "UnaccountedRoot"}],
        )


def test_indirect_roots_require_concrete_profile_abi_ownership() -> None:
    index = EvidenceIndex.__new__(EvidenceIndex)
    index.indirect_roots = [
        {
            "entry": "0x00001000",
            "name": "PlayerRoot_Concrete",
            "family": "player_state",
            "size": "64",
            "confidence": "high",
            "return_type": "void",
            "param_types": "Oot3dPlayer*;Oot3dPlayState*",
            "param_names": "player;play",
            "source": "fixture.md",
            "tranche": "152",
            "snapshot_file": "analysis/player.csv",
            "signature_snapshot_file": "analysis/player_signatures.csv",
        },
        {
            "entry": "0x00001040",
            "name": "ActorRoot_Generic",
            "family": "actor_state",
            "size": "64",
            "confidence": "high",
            "return_type": "void",
            "param_types": "Oot3dActor*;Oot3dPlayState*",
            "param_names": "actor;play",
            "source": "fixture.md",
            "tranche": "152",
            "snapshot_file": "analysis/actor.csv",
            "signature_snapshot_file": "analysis/actor_signatures.csv",
        },
        {
            "entry": "0x00001080",
            "name": "EnKoRoot_Concrete",
            "family": "actor_state",
            "size": "64",
            "confidence": "high",
            "return_type": "void",
            "param_types": "Oot3dEnKo*;Oot3dPlayState*",
            "param_names": "actor;play",
            "source": "fixture.md",
            "tranche": "152",
            "snapshot_file": "analysis/enko.csv",
            "signature_snapshot_file": "analysis/enko_signatures.csv",
        },
    ]

    player = index.indirect_behavior_functions("Oot3dPlayer")
    enko = index.indirect_behavior_functions("Oot3dEnKo")
    assert [row["name"] for row in player] == ["PlayerRoot_Concrete"]
    assert [row["name"] for row in enko] == ["EnKoRoot_Concrete"]
    assert player[0]["owner_resolution"] == (
        "exact_concrete_instance_pointer_in_native_abi"
    )


def test_maintained_actor_callback_requires_exact_owner_and_context_abi() -> None:
    index = EvidenceIndex.__new__(EvidenceIndex)
    index.native_functions = [
        {
            "address": "0x002335B4",
            "name": "EnKo_OverrideLimbDraw",
            "family": "actor_state",
            "byte_length": 96,
            "confidence": "high",
            "return_type": "s32",
            "parameter_types": ["Oot3dPlayState*", "s32", "Oot3dMtx3x4*", "void*"],
            "parameter_names": ["play", "limb", "mtx", "state"],
            "source": "fixture.md",
            "evidence_file": "fixture.csv",
            "signature_evidence_file": "fixture_signatures.csv",
            "closure_kind": "maintained_abi",
            "source_tranche": 156,
        },
        {
            "address": "0x00233614",
            "name": "OtherActor_OverrideLimbDraw",
            "family": "actor_state",
            "byte_length": 64,
            "confidence": "high",
            "return_type": "s32",
            "parameter_types": ["Oot3dPlayState*", "s32", "Oot3dMtx3x4*", "void*"],
            "parameter_names": ["play", "limb", "mtx", "state"],
            "source": "fixture.md",
            "evidence_file": "fixture.csv",
            "signature_evidence_file": "fixture_signatures.csv",
            "closure_kind": "maintained_abi",
            "source_tranche": 156,
        },
    ]

    functions = index.native_behavior_functions("EnKo", "Oot3dEnKo")

    assert [row["name"] for row in functions] == ["EnKo_OverrideLimbDraw"]
    assert functions[0]["owner_resolution"] == (
        "exact_maintained_actor_symbol_prefix_and_native_context_abi"
    )


def test_fixed_stride_array_replaces_overlapping_scalar_storage() -> None:
    index = EvidenceIndex.__new__(EvidenceIndex)
    index.workflow_struct_fields_by_structure = defaultdict(list)
    index.actor_struct_fields_by_structure = defaultdict(list)
    index.actor_struct_fields_by_structure["Oot3dEnFz"] = [
        {
            "structure": "Oot3dEnFz",
            "structure_size": "0x13DC",
            "offset": "0x02F0",
            "type": "Oot3dEnFzEffectRecord[60]",
            "name": "ice_effects",
            "confidence": "high",
            "source": "fixed native OoT3D callback/helper contract",
            "snapshot_file": (
                "analysis/codebin_actor_fixed_stride_array_fields.csv"
            ),
            "catalog_priority": "7",
        },
        {
            "structure": "Oot3dEnFz",
            "structure_size": "0x13DC",
            "offset": "0x02F0",
            "type": "undef4",
            "name": "tail_undef4_02f0",
            "confidence": "high",
            "source": "typed Actor callback corpus",
            "snapshot_file": "analysis/codebin_actor_tail_scalar_fields.csv",
            "catalog_priority": "8",
        },
    ]

    fields = index.actor_structure_fields("Oot3dEnFz", 0x13DC)

    assert fields == [
        {
            "offset": 0x02F0,
            "offset_hex": "0x02F0",
            "type": "Oot3dEnFzEffectRecord[60]",
            "name": "ice_effects",
            "confidence": "high",
            "source": "fixed native OoT3D callback/helper contract",
            "evidence_file": (
                "analysis/codebin_actor_fixed_stride_array_fields.csv"
            ),
            "evidence_files": [
                "analysis/codebin_actor_fixed_stride_array_fields.csv",
                "analysis/codebin_actor_tail_scalar_fields.csv",
            ],
        }
    ]


def test_concrete_native_record_refines_typed_helper_prefix() -> None:
    index = EvidenceIndex.__new__(EvidenceIndex)
    index.workflow_struct_fields_by_structure = defaultdict(list)
    index.actor_struct_fields_by_structure = defaultdict(list)
    index.actor_struct_fields_by_structure["Oot3dEnKo"] = [
        {
            "structure": "Oot3dEnKo",
            "structure_size": "0x0CA8",
            "offset": "0x0230",
            "type": "Oot3dCollider",
            "name": "typed_collider_0230",
            "confidence": "high",
            "source": "typed Actor helper parameter",
            "snapshot_file": "analysis/codebin_actor_typed_helper_fields.csv",
            "catalog_priority": "5",
        },
        {
            "structure": "Oot3dEnKo",
            "structure_size": "0x0CA8",
            "offset": "0x0230",
            "type": "Oot3dColliderCylinder",
            "name": "body_collider",
            "confidence": "high",
            "source": "original OoT3D code.bin",
            "snapshot_file": (
                "analysis/codebin_actor_residual_layout_tranche_173_fields.csv"
            ),
            "catalog_priority": "10",
        },
    ]

    fields = index.actor_structure_fields("Oot3dEnKo", 0x0CA8)

    assert fields == [
        {
            "offset": 0x0230,
            "offset_hex": "0x0230",
            "type": "Oot3dColliderCylinder",
            "name": "body_collider",
            "confidence": "high",
            "source": "original OoT3D code.bin",
            "evidence_file": (
                "analysis/codebin_actor_residual_layout_tranche_173_fields.csv"
            ),
            "evidence_files": [
                "analysis/codebin_actor_residual_layout_tranche_173_fields.csv",
                "analysis/codebin_actor_typed_helper_fields.csv",
            ],
        }
    ]


def test_actor_workflow_semantic_rejects_conflicting_typed_root() -> None:
    index = EvidenceIndex.__new__(EvidenceIndex)
    index.workflow_struct_fields_by_structure = defaultdict(list)
    index.actor_struct_fields_by_structure = defaultdict(list)
    index.workflow_struct_fields_by_structure["Oot3dPlayer"] = [
        {
            "structure": "Oot3dPlayer",
            "structure_size": "0x2A4C",
            "offset": "0x01A4",
            "type": "s8",
            "name": "current_tunic",
            "confidence": "high",
            "source": "Player_SetEquipmentData native body",
            "snapshot_file": (
                "analysis/codebin_actor_workflow_tranche_068_struct_fields.csv"
            ),
        }
    ]
    index.actor_struct_fields_by_structure["Oot3dPlayer"] = [
        {
            "structure": "Oot3dPlayer",
            "structure_size": "0x2A4C",
            "offset": "0x01A4",
            "type": "Oot3dSkelAnime",
            "name": "typed_root_01a4",
            "confidence": "high",
            "source": "cross-owner typed cast",
            "snapshot_file": (
                "analysis/codebin_actor_typed_root_contracts_tranche_188_fields.csv"
            ),
            "catalog_priority": "20",
        }
    ]

    fields = index.actor_structure_fields("Oot3dPlayer", 0x2A4C)

    assert fields == [
        {
            "offset": 0x01A4,
            "offset_hex": "0x01A4",
            "type": "s8",
            "name": "current_tunic",
            "confidence": "high",
            "source": "Player_SetEquipmentData native body",
            "evidence_file": (
                "analysis/codebin_actor_workflow_tranche_068_struct_fields.csv"
            ),
            "evidence_files": [
                "analysis/codebin_actor_workflow_tranche_068_struct_fields.csv"
            ],
            "rejected_root_contracts": [
                {
                    "type": "Oot3dSkelAnime",
                    "name": "typed_root_01a4",
                    "evidence_file": (
                        "analysis/"
                        "codebin_actor_typed_root_contracts_tranche_188_fields.csv"
                    ),
                    "reason": (
                        "conflicts_with_actor_workflow_semantic_field"
                    ),
                }
            ],
        }
    ]


def test_typed_root_covers_scalar_access_at_same_offset() -> None:
    index = EvidenceIndex.__new__(EvidenceIndex)
    index.workflow_struct_fields_by_structure = defaultdict(list)
    index.actor_struct_fields_by_structure = defaultdict(list)
    index.actor_struct_fields_by_structure["Oot3dPlayer"] = [
        {
            "structure": "Oot3dPlayer",
            "structure_size": "0x2A4C",
            "offset": "0x1234",
            "type": "s16",
            "name": "typed_s16_1234",
            "confidence": "high",
            "source": "typed dereference",
            "snapshot_file": (
                "analysis/codebin_actor_typed_corpus_tranche_186_fields.csv"
            ),
            "catalog_priority": "10",
        },
        {
            "structure": "Oot3dPlayer",
            "structure_size": "0x2A4C",
            "offset": "0x1234",
            "type": "Oot3dVec3s",
            "name": "typed_root_1234",
            "confidence": "high",
            "source": "typed pointer cast",
            "snapshot_file": (
                "analysis/codebin_actor_typed_root_contracts_tranche_188_fields.csv"
            ),
            "catalog_priority": "20",
        },
    ]

    fields = index.actor_structure_fields("Oot3dPlayer", 0x2A4C)

    assert fields == [
        {
            "offset": 0x1234,
            "offset_hex": "0x1234",
            "type": "Oot3dVec3s",
            "name": "typed_root_1234",
            "confidence": "high",
            "source": "typed pointer cast",
            "evidence_file": (
                "analysis/codebin_actor_typed_root_contracts_tranche_188_fields.csv"
            ),
            "evidence_files": [
                "analysis/codebin_actor_typed_corpus_tranche_186_fields.csv",
                "analysis/codebin_actor_typed_root_contracts_tranche_188_fields.csv",
            ],
            "covered_scalar_fields": [
                {
                    "type": "s16",
                    "name": "typed_s16_1234",
                    "evidence_file": (
                        "analysis/codebin_actor_typed_corpus_tranche_186_fields.csv"
                    ),
                    "reason": "scalar_access_is_inside_typed_root_contract",
                }
            ],
        }
    ]


def test_semantic_structure_replaces_equal_unknown_storage_span() -> None:
    index = EvidenceIndex.__new__(EvidenceIndex)
    index.workflow_struct_fields_by_structure = defaultdict(list)
    index.actor_struct_fields_by_structure = defaultdict(list)
    index.actor_struct_fields_by_structure["Oot3dEnKo"] = [
        {
            "structure": "Oot3dEnKo",
            "structure_size": "0x0CA8",
            "offset": "0x0ACC",
            "type": "Oot3dFaceAnimationSet",
            "name": "face_animations_0acc",
            "confidence": "high",
            "source": "native face-animation helpers",
            "snapshot_file": "analysis/codebin_actor_face_animation_fields.csv",
            "catalog_priority": "4",
        },
        {
            "structure": "Oot3dEnKo",
            "structure_size": "0x0CA8",
            "offset": "0x0ACC",
            "type": "undef1[460]",
            "name": "resource_group_storage",
            "confidence": "high",
            "source": "native bounded storage",
            "snapshot_file": (
                "analysis/"
                "codebin_actor_native_storage_contracts_tranche_187_fields.csv"
            ),
            "catalog_priority": "20",
        },
    ]

    fields = index.actor_structure_fields("Oot3dEnKo", 0x0CA8)

    assert fields[0]["type"] == "Oot3dFaceAnimationSet"
    assert fields[0]["evidence_files"] == [
        "analysis/codebin_actor_face_animation_fields.csv",
        (
            "analysis/"
            "codebin_actor_native_storage_contracts_tranche_187_fields.csv"
        ),
    ]


def test_actor_graph_materializes_original_consumer_roots_and_services() -> None:
    index = EvidenceIndex.__new__(EvidenceIndex)
    index.workflow_by_actor = defaultdict(list)
    index.workflow_signature_by_entry = defaultdict(list)
    index.workflow_action_targets_by_actor = defaultdict(list)
    index.workflow_struct_fields_by_structure = defaultdict(list)
    index.native_functions = []
    index.native_function_by_address = {}
    index.indirect_roots = []
    index.callback_signature_by_entry = defaultdict(list)
    index.consumer_signature_by_entry = defaultdict(list)
    index.callback_body_by_entry = defaultdict(list)
    index.actor_struct_fields_by_structure = defaultdict(list)

    def inventory(
        entry: str, name: str, callees: str = "", *, maintained: bool = True
    ) -> dict[str, str]:
        return {
            "entry": entry,
            "name": name,
            "size": "0x40",
            "module": "game/actors",
            "return_type": "void",
            "callee_count": str(len([v for v in callees.split(";") if v])),
            "callees": callees,
            "maintained_kind": "function" if maintained else "",
            "maintained_confidence": "high" if maintained else "",
            "maintained_source_file": "analysis/fixture.md",
            "confidence": "certain",
        }

    index.function_inventory_by_entry = {
        "00001000": inventory(
            "0x00001000", "EnKo_Init", "0x00001100;0x00002000"
        ),
        "00001100": inventory(
            "0x00001100", "EnKo_UpdateTracking", "0x00002100"
        ),
        "00001200": inventory("0x00001200", "EnKo_Update"),
        "00002000": inventory("0x00002000", "Object_Spawn"),
        "00002100": inventory("0x00002100", "Npc_UpdateTrackingByPreset"),
    }
    for address, name in (("00001000", "EnKo_Init"), ("00001200", "EnKo_Update")):
        index.callback_body_by_entry[address] = [
            {
                "entry": f"0x{address}",
                "name": name,
                "family": "actor_state",
                "size": "0x40",
                "confidence": "high",
            }
        ]
        index.consumer_signature_by_entry[address] = [
            {
                "entry": f"0x{address}",
                "name": name,
                "return_type": "void",
                "param_types": "Oot3dEnKo*;Oot3dPlayState*",
                "param_names": "actor;play",
                "source": "fixture.md",
                "snapshot_file": "analysis/callback_signatures.csv",
            }
        ]
    index.actor_struct_fields_by_structure["Oot3dEnKo"] = [
        {
            "structure": "Oot3dEnKo",
            "structure_size": "0x0CA8",
            "offset": "0x01A4",
            "type": "Oot3dSkelAnime",
            "name": "skel_anime_01a4",
            "confidence": "high",
            "source": "typed helper boundary",
            "snapshot_file": "analysis/actor_embedded_fields.csv",
            "catalog_priority": "0",
        },
        {
            "structure": "Oot3dEnKo",
            "structure_size": "0x0CA8",
            "offset": "0x02A0",
            "type": "undef4",
            "name": "ambiguous_02a0",
            "confidence": "high",
            "source": "typed callback corpus",
            "snapshot_file": "analysis/actor_tail_scalar_fields.csv",
            "catalog_priority": "6",
        },
    ]
    index.workflow_struct_fields_by_structure["Oot3dEnKo"] = [
        {
            "structure": "Oot3dEnKo",
            "structure_size": "0x0CA8",
            "offset": "0x02A0",
            "type": "f32",
            "name": "fade_distance",
            "confidence": "high",
            "source": "reviewed workflow",
            "snapshot_file": "analysis/workflow_fields.csv",
        }
    ]
    actor_init = {
        "owner": "EnKo",
        "structure_name": "Oot3dEnKo",
        "instance_size": "0x0CA8",
        "init_entry": "0x00001000",
        "init_name": "EnKo_Init",
        "destroy_entry": "0x00000000",
        "destroy_name": "",
        "update_entry": "0x00001200",
        "update_name": "EnKo_Update",
        "draw_entry": "0x00000000",
        "draw_name": "",
    }

    graph = index.actor_behavior_graph(actor_init)

    assert graph["status"] == "native_consumer_graph_recovered"
    assert graph["consumer_root_count"] == 2
    assert graph["consumer_function_count"] == 5
    assert graph["consumer_call_edge_count"] == 3
    assert graph["actor_local_consumer_function_count"] == 1
    assert graph["native_service_dependency_count"] == 2
    assert {row["name"] for row in graph["consumer_roots"]} == {
        "EnKo_Init",
        "EnKo_Update",
    }
    assert {row["name"] for row in graph["functions"]} == {
        "EnKo_Init",
        "EnKo_Update",
        "EnKo_UpdateTracking",
        "Object_Spawn",
        "Npc_UpdateTrackingByPreset",
    }
    assert graph["structure_fields"][0]["type"] == "Oot3dSkelAnime"
    assert graph["structure_fields"][1]["type"] == "f32"
    validate_actor_behavior_graph({"instance_size": 0x0CA8}, graph)


def _minimal_unit() -> dict:
    source = {
        "logical_path": "scene/test_info.zsi",
        "byte_length": 4,
        "sha256": hashlib.sha256(b"test").hexdigest(),
    }
    unit = {
        "format": FORMAT,
        "schema_version": SCHEMA_VERSION,
        "status": "composition_complete_behavior_incomplete",
        "identity": {
            "unit_id": "route:test:setup-0",
            "route_id": "route:test",
            "scene_id": 1,
            "scene_stem": "test",
            "scene_path": "test_info.zsi",
            "setup_index": 0,
            "native_room_indices": [0],
            "initial_room_index": 0,
        },
        "authority_policy": {},
        "provenance": {},
        "semantic_request": {},
        "semantic_variant_evidence": {},
        "native_route": {},
        "source_assets": {
            "code_bin": source,
            "scene_zsi": source,
            "room_zsi": [source],
            "scene_zar": None,
        },
        "scene_resource_record": {},
        "room_callback_contract": {},
        "room_lifecycle_contract": {
            "format": "oot3d_room_lifecycle_contract_v1",
            "status": "workflow_semantics_recovered",
            "source_snapshot_id": "fixture",
            "functions": {
                role: {"address": f"0x{index + 1:08X}", "name": role}
                for index, role in enumerate(
                    (
                        "initialize",
                        "request",
                        "process_request",
                        "destroy",
                        "cleanup_room_actors",
                        "spawn_transition_actors",
                        "queue_resource_cleanup",
                        "process_resource_cleanup",
                    )
                )
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
            "structure_evidence": [],
        },
        "object_bank_contract": {},
        "scene_setup": {},
        "collision_headers": [],
        "entrypoint": None,
        "rooms": [
            {
                "room_index": 0,
                "setup_index": 0,
                "commands": [
                    {
                        "command_id": 0x14,
                        "parameter": 0,
                        "command_word": 0x14,
                        "argument": 0,
                    }
                ],
            }
        ],
        "actor_instances": [],
        "actor_profiles": [],
        "object_dependencies": [],
        "scene_catalog_bindings": [],
        "closure": {},
        "unresolved": [],
    }
    unit["closure"].update(
        {
            "behavior_graph_profile_count": 0,
            "behavior_function_count": 0,
            "behavior_action_transition_count": 0,
            "behavior_structure_field_count": 0,
        }
    )
    unit["identity"]["payload_sha256"] = canonical_sha256(unit)
    return unit


def test_room_unit_digest_detects_payload_mutation() -> None:
    unit = _minimal_unit()
    validate_room_compilation_unit(unit)

    unit["identity"]["scene_id"] = 2
    with pytest.raises(ValueError, match="digest mismatch"):
        validate_room_compilation_unit(unit)


def test_room_unit_rejects_mismatched_or_unterminated_room_setup() -> None:
    unit = _minimal_unit()
    unit["rooms"][0]["setup_index"] = 1
    unit["identity"].pop("payload_sha256")
    unit["identity"]["payload_sha256"] = canonical_sha256(unit)
    with pytest.raises(ValueError, match="room setup does not match"):
        validate_room_compilation_unit(unit)

    unit = _minimal_unit()
    unit["rooms"][0]["commands"][0]["command_id"] = 0x08
    unit["identity"].pop("payload_sha256")
    unit["identity"]["payload_sha256"] = canonical_sha256(unit)
    with pytest.raises(ValueError, match="not bounded by end marker"):
        validate_room_compilation_unit(unit)


def test_only_incomplete_legacy_unit_may_lack_lifecycle_contract() -> None:
    unit = _minimal_unit()
    unit.pop("room_lifecycle_contract")
    unit["identity"].pop("payload_sha256")
    unit["identity"]["payload_sha256"] = canonical_sha256(unit)
    with pytest.raises(ValueError, match="lacks its room lifecycle contract"):
        validate_room_compilation_unit(unit)

    unit["status"] = "composition_incomplete"
    unit["identity"].pop("payload_sha256")
    unit["identity"]["payload_sha256"] = canonical_sha256(unit)
    validate_room_compilation_unit(unit)


def test_evidence_snapshot_verifies_every_copied_file(tmp_path) -> None:
    evidence = tmp_path / "analysis" / "sample.csv"
    evidence.parent.mkdir(parents=True)
    original = "entry,name\n1,test\n"
    evidence.write_text(original, encoding="utf-8")
    digest = hashlib.sha256(evidence.read_bytes()).hexdigest()
    manifest = {
        "format": EVIDENCE_SNAPSHOT_FORMAT,
        "snapshot_id": "fixture",
        "file_count": 1,
        "files": [
            {
                "path": "analysis/sample.csv",
                "size": evidence.stat().st_size,
                "sha256": digest,
            }
        ],
    }
    (tmp_path / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")

    snapshot = EvidenceSnapshot.load(tmp_path)
    assert snapshot.path("analysis/sample.csv") == evidence

    changed = bytearray(evidence.read_bytes())
    changed[-2] = ord("x")
    evidence.write_bytes(changed)
    with pytest.raises(ValueError, match="hash mismatch"):
        EvidenceSnapshot.load(tmp_path)

    evidence.write_text(original, encoding="utf-8")
    manifest["files"][0]["size"] += 1
    (tmp_path / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    with pytest.raises(ValueError, match="size mismatch"):
        EvidenceSnapshot.load(tmp_path)


def test_catalog_container_keeps_native_archive_identity() -> None:
    assert (
        catalog_source_container(
            {"source_identity": "cmb:actor/zelda_km1.zar!Model/km1_model.cmb"}
        )
        == "actor/zelda_km1.zar"
    )
    assert (
        catalog_source_container({"source_identity": "scene:spot04_info.zsi"})
        == "spot04_info.zsi"
    )


def test_room_callback_registry_is_bound_to_code_and_literals(tmp_path) -> None:
    code = bytearray(0x40)
    code[4:8] = b"CALL"
    code[0x10:0x14] = (0x12345678).to_bytes(4, "little")
    code[0x20:0x24] = b"RNG!"
    code_path = tmp_path / "code.bin"
    code_path.write_bytes(code)
    registry = {
        "format": "oot3d_room_scene_callback_native_contracts_v1",
        "status": "verified_partial_coverage",
        "code_bin": {
            "base_address": "0x00001000",
            "sha256": hashlib.sha256(code).hexdigest(),
        },
        "contracts": [
            {
                "callback_address": "0x00001004",
                "callback_name": "Fixture_PrepareDraw",
                "role": "prepare_draw",
                "function_size": 4,
                "function_sha256": hashlib.sha256(b"CALL").hexdigest(),
                "runtime_binding_status": "native_typed_operations_ready",
                "literal_words": [{"address": "0x00001010", "value": "0x12345678"}],
                "operations": [
                    {
                        "kind": "material_tev_constant_alpha",
                        "native_operation": 2,
                        "alpha": {
                            "random": {
                                "function_address": "0x00001020",
                                "function_size": 4,
                                "function_sha256": hashlib.sha256(b"RNG!").hexdigest(),
                                "state_address": "0x00001030",
                            }
                        },
                    }
                ],
            }
        ],
    }
    registry_path = tmp_path / "callbacks.json"
    registry_path.write_text(json.dumps(registry), encoding="utf-8")
    contracts = load_room_callback_runtime_contracts(registry_path, code_path)
    assert contracts["00001004"]["literal_verification_count"] == 1

    code[4] ^= 0xFF
    code_path.write_bytes(code)
    registry["code_bin"]["sha256"] = hashlib.sha256(code).hexdigest()
    registry_path.write_text(json.dumps(registry), encoding="utf-8")
    with pytest.raises(ValueError, match="differs from its verified function body"):
        load_room_callback_runtime_contracts(registry_path, code_path)
