from __future__ import annotations

import struct
from pathlib import Path

from oot3d_asset_tool.room_compilation_unit import room_unit
from oot3d_asset_tool.zsi import ZSI_RESOURCE_BASE_OFFSET, ZsiFile
from oot3d_asset_tool.zsi_scene_index import build_room_index_record
from oot3d_asset_tool.zsi_scene_index import export_zsi_scene_index


def _command(
    command_id: int,
    parameter: int = 0,
    argument: int = 0,
    data2: int = 0,
    data3: int = 0,
) -> bytes:
    command_word = (
        command_id | (parameter << 8) | (data2 << 16) | (data3 << 24)
    )
    return struct.pack("<II", command_word, argument)


def _actor(actor_id: int, x: int) -> bytes:
    return struct.pack("<hhhhhhhh", actor_id, x, 20, 30, 0, 0, 0, 0x1234)


def _scene_setup(first_command_id: int) -> bytes:
    command_ids = (first_command_id, 0x19, 0x03, 0x06, 0x00, 0x11, 0x0F, 0x14)
    return b"".join(_command(command_id) for command_id in command_ids)


def _room_setup(object_argument: int, actor_argument: int, actor_id: int) -> bytes:
    commands = (
        _command(0x16),
        _command(0x08),
        _command(0x12),
        _command(0x10),
        _command(0x0A, argument=0x80),
        _command(0x0B, parameter=2, argument=object_argument),
        _command(0x01, parameter=1, argument=actor_argument),
        _command(0x14),
    )
    return b"".join(commands)


def test_scene_setup_parser_accepts_native_room_list_first_header(
    tmp_path: Path,
) -> None:
    path = tmp_path / "fixture_info.zsi"
    data = bytearray(0x100)
    data[:4] = b"ZSI\x01"
    data[0x18 : 0x18 + 0x40] = _scene_setup(0x04)
    path.write_bytes(data)

    setups = ZsiFile.from_path(path).scene_setups()

    assert len(setups) == 1
    assert setups[0].commands[0].command_id == 0x04
    assert setups[0].commands[-1].command_id == 0x14


def test_scene_index_preserves_native_sound_id_and_ambience_byte(
    tmp_path: Path,
) -> None:
    path = tmp_path / "fixture_info.zsi"
    data = bytearray(0x80)
    data[:4] = b"ZSI\x01"
    data[0x18:0x28] = b"".join(
        (
            _command(
                0x15,
                parameter=1,
                data2=4,
                data3=0,
                argument=0x010005A9,
            ),
            _command(0x14),
        )
    )
    path.write_bytes(data)

    index = export_zsi_scene_index(
        tmp_path,
        scene_stems=["fixture"],
        full_entries=True,
        include_room_mesh_summaries=False,
    )
    decoded = index["records"][0]["setups"][0]["commands"][0]["decoded"]

    assert decoded == {
        "status": "decoded_sound_settings",
        "spec_id": 1,
        "nature_ambience_id": 4,
        "data3": 0,
        "bgm_sound_id": 0x010005A9,
        "bgm_sound_id_hex": "0x010005a9",
    }


def test_room_index_uses_resource_base_and_preserves_each_native_setup(
    tmp_path: Path,
) -> None:
    path = tmp_path / "fixture_0_info.zsi"
    data = bytearray(0x240)
    data[:4] = b"ZSI\x01"
    data[0x18:0x58] = _room_setup(0x100, 0x120, 21)
    data[0x58:0x98] = _room_setup(0x140, 0x160, 57)
    data[0x100 + ZSI_RESOURCE_BASE_OFFSET : 0x114] = struct.pack("<hh", 303, 392)
    data[0x120 + ZSI_RESOURCE_BASE_OFFSET : 0x140] = _actor(21, 100)
    data[0x140 + ZSI_RESOURCE_BASE_OFFSET : 0x154] = struct.pack("<hh", 286, 252)
    data[0x160 + ZSI_RESOURCE_BASE_OFFSET : 0x180] = _actor(57, 200)
    path.write_bytes(data)
    semantics = {
        "actor": {21: "ACTOR_EN_ITEM00", 57: "ACTOR_EN_A_OBJ"},
        "object": {
            252: "OBJECT_KM1",
            286: "OBJECT_MAMENOKI",
            303: "OBJECT_KANBAN",
            392: "OBJECT_GS",
        },
    }

    room = build_room_index_record(
        tmp_path,
        path,
        semantics,
        sample_limit=16,
        full_entries=True,
        include_mesh_summary=False,
    )

    assert room["setup_count"] == 2
    assert room["setups"][0]["object_list"]["source_offset"] == 0x110
    assert [
        entry["object_id"] for entry in room["setups"][0]["object_list"]["entries"]
    ] == [303, 392]
    assert (
        room["setups"][0]["actor_list"]["selected_candidate"]["start_offset"] == 0x130
    )
    assert (
        room["setups"][0]["actor_list"]["selected_candidate"]["entries"][0]["actor_id"]
        == 21
    )

    compiled = room_unit(
        room,
        path,
        initial_room_index=0,
        setup_index=1,
    )
    assert compiled["setup_index"] == 1
    assert compiled["object_list"]["entry_count"] == 2
    assert compiled["actor_list"]["entries"][0]["actor_id"] == 57


def test_scene_index_reports_exact_room_setup_alignment(tmp_path: Path) -> None:
    scene_data = bytearray(0x100)
    scene_data[:4] = b"ZSI\x01"
    scene_data[0x18:0x58] = _scene_setup(0x04)
    scene_data[0x58:0x98] = _scene_setup(0x04)
    (tmp_path / "fixture_info.zsi").write_bytes(scene_data)

    room_data = bytearray(0x240)
    room_data[:4] = b"ZSI\x01"
    room_data[0x18:0x58] = _room_setup(0x100, 0x120, 21)
    room_data[0x58:0x98] = _room_setup(0x140, 0x160, 57)
    room_data[0x110:0x114] = struct.pack("<hh", 303, 392)
    room_data[0x130:0x140] = _actor(21, 100)
    room_data[0x150:0x154] = struct.pack("<hh", 286, 252)
    room_data[0x170:0x180] = _actor(57, 200)
    (tmp_path / "fixture_0_info.zsi").write_bytes(room_data)

    semantics = tmp_path / "actor_object_semantics.h"
    semantics.write_text(
        "typedef enum Oot3dActorId { ACTOR_EN_ITEM00 = 21, ACTOR_EN_A_OBJ = 57 } Oot3dActorId;\n"
        "typedef enum Oot3dObjectId { OBJECT_KM1 = 252, OBJECT_MAMENOKI = 286, "
        "OBJECT_KANBAN = 303, OBJECT_GS = 392 } Oot3dObjectId;\n",
        encoding="utf-8",
    )

    index = export_zsi_scene_index(
        tmp_path,
        scene_stems=["fixture"],
        actor_object_semantics=semantics,
        full_entries=True,
        include_room_mesh_summaries=False,
    )

    assert index["setup_count"] == 2
    assert index["unique_room_setup_count"] == 2
    assert index["room_setup_binding_count"] == 2
    assert index["room_setup_alignment_mismatch_count"] == 0


def test_scene_index_uses_native_resource_base_for_scene_payloads(
    tmp_path: Path,
) -> None:
    scene_data = bytearray(0x220)
    scene_data[:4] = b"ZSI\x01"
    commands = (
        _command(0x04, parameter=1, argument=0x100),
        _command(0x06, parameter=1, argument=0x140),
        _command(0x0D, parameter=1, argument=0x148),
        _command(0x00, parameter=1, argument=0x160),
        _command(0x0E, parameter=1, argument=0x180),
        _command(0x13, argument=0x1A0),
        _command(0x0F, parameter=1, argument=0x1A4),
        _command(0x14),
    )
    scene_data[0x18:0x58] = b"".join(commands)
    room_ref = b"rom:/scene/fixture_0_info.zsi\0"
    scene_data[0x110 : 0x110 + len(room_ref)] = room_ref
    scene_data[0x150:0x152] = bytes((0, 0))
    scene_data[0x158:0x160] = bytes((1, 0, 0, 0)) + struct.pack("<I", 0x1D0)
    scene_data[0x170:0x180] = _actor(0, 100)
    scene_data[0x190:0x1A0] = struct.pack("<bbbbhhhhhh", 0, 0, -1, 0, 57, 1, 2, 3, 4, 5)
    scene_data[0x1B0:0x1B4] = struct.pack("<hh", 0x123, -1)
    scene_data[0x1B4:0x1D0] = bytes(range(0x1C))
    scene_data[0x1E0:0x1E6] = struct.pack("<hhh", 11, 22, 33)
    (tmp_path / "fixture_info.zsi").write_bytes(scene_data)

    room_data = bytearray(0x180)
    room_data[:4] = b"ZSI\x01"
    room_data[0x18:0x58] = _room_setup(0x100, 0x120, 21)
    room_data[0x110:0x114] = struct.pack("<hh", 303, 392)
    room_data[0x130:0x140] = _actor(21, 100)
    (tmp_path / "fixture_0_info.zsi").write_bytes(room_data)

    semantics = tmp_path / "actor_object_semantics.h"
    semantics.write_text(
        "typedef enum Oot3dActorId { ACTOR_PLAYER = 0, ACTOR_EN_ITEM00 = 21, "
        "ACTOR_EN_A_OBJ = 57 } Oot3dActorId;\n"
        "typedef enum Oot3dObjectId { OBJECT_KANBAN = 303, OBJECT_GS = 392 } "
        "Oot3dObjectId;\n",
        encoding="utf-8",
    )

    index = export_zsi_scene_index(
        tmp_path,
        scene_stems=["fixture"],
        actor_object_semantics=semantics,
        full_entries=True,
        include_room_mesh_summaries=False,
    )
    decoded = {
        command["command_id"]: command["decoded"]
        for command in index["records"][0]["setups"][0]["commands"]
    }

    assert decoded[0x04]["source_offset"] == 0x110
    assert decoded[0x04]["room_references"][0]["room_index"] == 0
    assert decoded[0x06]["selected_candidate"]["start_offset"] == 0x150
    assert decoded[0x00]["selected_candidate"]["start_offset"] == 0x170
    assert decoded[0x0D]["raw_records"][0]["points_offset"] == 0x1E0
    assert decoded[0x0D]["raw_records"][0]["points"][0]["x"] == 11
    assert [entry["value"] for entry in decoded[0x13]["values"]] == [0x123, -1]
    assert decoded[0x0F]["records"][0]["offset"] == 0x1B4
