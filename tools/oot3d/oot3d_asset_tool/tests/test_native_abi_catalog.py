from __future__ import annotations

import json
from pathlib import Path

import pytest

from oot3d_asset_tool.native_abi_catalog import (
    build_native_abi_catalog,
    load_native_abi_catalog,
)


def _write_catalog_pair(
    root: Path,
    relative: str,
    *,
    entry: str,
    name: str,
    family: str,
    parameter_types: str,
    parameter_names: str = "state",
) -> None:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "entry,name,family,size,confidence,notes\n"
        f'{entry},{name},{family},64,high,"fixture"\n',
        encoding="utf-8",
    )
    path.with_name(path.stem + "_signatures.csv").write_text(
        "entry,name,return_type,param_types,param_names,confidence,source,notes\n"
        f'{entry},{name},void,{parameter_types},{parameter_names},high,fixture.md,"fixture"\n',
        encoding="utf-8",
    )


def test_native_abi_catalog_unifies_reviewed_closure_kinds(tmp_path: Path) -> None:
    files = (
        "analysis/codebin_indirect_ready_small_roots_tranche_148.csv",
        "analysis/codebin_parent_owned_residual_roots_tranche_154.csv",
        "analysis/codebin_maintained_abi_microclosure_tranche_155.csv",
        "analysis/codebin_callable_identity_closure_tranche_172.csv",
        "analysis/codebin_actor_private_native_closure_tranche_181.csv",
        "analysis/codebin_actor_priority_native_closure_tranche_182.csv",
        "analysis/codebin_actor_indirect_native_roots_tranche_183.csv",
    )
    _write_catalog_pair(
        tmp_path,
        files[0],
        entry="0x00100000",
        name="PlayerRoot_Test",
        family="player_state",
        parameter_types="Oot3dPlayer*",
    )
    _write_catalog_pair(
        tmp_path,
        files[1],
        entry="0x00100040",
        name="ObjSwitch_Test",
        family="actor_state",
        parameter_types="Oot3dObjSwitch*",
    )
    _write_catalog_pair(
        tmp_path,
        files[2],
        entry="0x00100080",
        name="Gameplay_InCsMode",
        family="scene_state",
        parameter_types="Oot3dPlayState*",
    )
    _write_catalog_pair(
        tmp_path,
        files[3],
        entry="0x001000C0",
        name="CallableIdentity_Test",
        family="actor_callable_identity",
        parameter_types="Oot3dActor*",
    )
    _write_catalog_pair(
        tmp_path,
        files[4],
        entry="0x00100100",
        name="EnKo_PrivateInit",
        family="actor_private_native_closure",
        parameter_types="Oot3dEnKo*",
    )
    _write_catalog_pair(
        tmp_path,
        files[5],
        entry="0x00100140",
        name="EnKo_PriorityAction",
        family="actor_priority_native_closure",
        parameter_types="Oot3dEnKo*",
    )
    _write_catalog_pair(
        tmp_path,
        files[6],
        entry="0x00100180",
        name="EnItem00_NativeAction",
        family="actor_indirect_native_roots",
        parameter_types="Oot3dEnItem00*",
    )
    selected = list(files) + [
        relative[:-4] + "_signatures.csv" for relative in files
    ]

    catalog = build_native_abi_catalog(
        tmp_path,
        selected,
        snapshot_id="fixture-snapshot",
        source_base_revision="a" * 40,
        code_bin_sha256="b" * 64,
    )

    assert catalog["function_count"] == 7
    assert catalog["closure_kind_counts"] == {
        "actor_priority_native": 1,
        "actor_private_native": 1,
        "callable_identity": 1,
        "indirect_root": 2,
        "maintained_abi": 1,
        "parent_owned_root": 1,
    }
    assert catalog["functions"][1]["native_pointer_types"] == [
        "Oot3dObjSwitch*"
    ]
    assert catalog["subsystem_contract_count"] == 7
    assert catalog["complete_subsystem_contract_count"] == 0
    output = tmp_path / "native_abi_catalog.json"
    output.write_text(json.dumps(catalog), encoding="utf-8")
    assert load_native_abi_catalog(output)["payload_sha256"] == catalog[
        "payload_sha256"
    ]


def test_native_abi_catalog_rejects_overlapping_reviewed_entries(
    tmp_path: Path,
) -> None:
    first = "analysis/codebin_indirect_ready_small_roots_tranche_148.csv"
    second = "analysis/codebin_parent_owned_residual_roots_tranche_154.csv"
    for relative in (first, second):
        _write_catalog_pair(
            tmp_path,
            relative,
            entry="0x00100000",
            name="Duplicate",
            family="actor_state",
            parameter_types="Oot3dActor*",
        )

    with pytest.raises(ValueError, match="overlap"):
        build_native_abi_catalog(
            tmp_path,
            [
                first,
                first[:-4] + "_signatures.csv",
                second,
                second[:-4] + "_signatures.csv",
            ],
            snapshot_id="fixture-snapshot",
            source_base_revision="a" * 40,
            code_bin_sha256="b" * 64,
        )


def test_native_abi_catalog_keeps_newer_typed_actor_refinement(
    tmp_path: Path,
) -> None:
    generic = "analysis/codebin_indirect_ready_broad_roots_tranche_152.csv"
    concrete = "analysis/codebin_actor_priority_native_closure_tranche_182.csv"
    _write_catalog_pair(
        tmp_path,
        generic,
        entry="0x001E81FC",
        name="ActorRoot_Generic_001E81FC",
        family="actor_state",
        parameter_types="Oot3dActor*;s32",
        parameter_names="actor;arg1",
    )
    _write_catalog_pair(
        tmp_path,
        concrete,
        entry="0x001E81FC",
        name="BossDodongo_Action_001E81FC",
        family="actor_priority_native_closure",
        parameter_types="Oot3dBossDodongo*;Oot3dPlayState*",
        parameter_names="actor;play",
    )

    catalog = build_native_abi_catalog(
        tmp_path,
        [
            generic,
            generic[:-4] + "_signatures.csv",
            concrete,
            concrete[:-4] + "_signatures.csv",
        ],
        snapshot_id="fixture-snapshot",
        source_base_revision="a" * 40,
        code_bin_sha256="b" * 64,
    )

    assert catalog["function_count"] == 1
    assert catalog["superseded_evidence_count"] == 1
    function = catalog["functions"][0]
    assert function["name"] == "BossDodongo_Action_001E81FC"
    assert function["closure_kind"] == "actor_priority_native"
    assert function["parameter_types"] == [
        "Oot3dBossDodongo*",
        "Oot3dPlayState*",
    ]
    assert function["superseded_evidence"][0]["name"] == (
        "ActorRoot_Generic_001E81FC"
    )


def test_native_abi_catalog_adds_reviewed_embedded_helpers(
    tmp_path: Path,
) -> None:
    signatures = "analysis/codebin_embedded_helper_signatures.csv"
    inventory = "analysis/codebin_function_inventory.csv"
    signature_path = tmp_path / signatures
    signature_path.parent.mkdir(parents=True, exist_ok=True)
    signature_path.write_text(
        "entry,name,return_type,param_types,param_names,confidence,source,notes\n"
        "00320D28,SkelAnime_SetUpdate,void,Oot3dSkelAnime*,skel_anime,high,"
        'code.bin@00320D28-00320D5B,"fixture"\n',
        encoding="utf-8",
    )
    (tmp_path / inventory).write_text(
        "entry,size,module\n"
        "0x00320D28,44,game/actors\n",
        encoding="utf-8",
    )

    catalog = build_native_abi_catalog(
        tmp_path,
        [signatures, inventory],
        snapshot_id="fixture-snapshot",
        source_base_revision="a" * 40,
        code_bin_sha256="b" * 64,
    )

    assert catalog["function_count"] == 1
    assert catalog["closure_kind_counts"] == {"reviewed_signature": 1}
    assert catalog["functions"][0]["source_tranche"] == 0
    assert catalog["functions"][0]["family"] == "game_actors"
