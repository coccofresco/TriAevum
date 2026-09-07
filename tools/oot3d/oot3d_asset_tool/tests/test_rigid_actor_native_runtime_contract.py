from __future__ import annotations

import struct
from pathlib import Path

import pytest

from oot3d_asset_tool.binary import ParseError
from oot3d_asset_tool.native_actor_contract_common import (
    ACTOR_OVERLAY_ENTRY_STRIDE,
    ACTOR_OVERLAY_PROFILE_POINTER_OFFSET,
    ACTOR_OVERLAY_TABLE_POINTER_LITERAL,
    CODE_IMAGE_BASE,
    OBJECT_TABLE,
)
from oot3d_asset_tool.rigid_actor_native_runtime_contract import (
    ENAOBJ_ACTOR_ID,
    ENAOBJ_DESTROY,
    ENAOBJ_DRAW,
    ENAOBJ_INIT,
    ENAOBJ_MODEL_TABLE_ADDRESS_INSTRUCTION,
    ENAOBJ_SCALE_DEFAULT_BRANCH,
    ENAOBJ_SCALE_JUMP_TABLE,
    ENAOBJ_SELECTOR_MASK_INSTRUCTION,
    ENAOBJ_UPDATE,
    ENISHI_ACTOR_ID,
    ENISHI_ALTERNATE_MODEL_MOVE,
    ENISHI_DEFAULT_MODEL_MOVE,
    ENISHI_DESTROY,
    ENISHI_DRAW,
    ENISHI_DRAW_SELECTOR_INSTRUCTION,
    ENISHI_DRAW_TABLE_POINTER_LITERAL,
    ENISHI_INIT,
    ENISHI_INIT_SELECTOR_INSTRUCTION,
    ENISHI_SCALE_TABLE_POINTER_LITERAL,
    ENISHI_SELECTOR_ONE_COMPARE,
    ENISHI_UPDATE,
    ENKANBAN_ACTOR_ID,
    ENKANBAN_DESTROY,
    ENKANBAN_DRAW,
    ENKANBAN_DRAW_LOCAL_Z_LITERAL_LOAD,
    ENKANBAN_DRAW_ZERO_LITERAL_LOAD,
    ENKANBAN_EFFECT_MODEL_MOVE,
    ENKANBAN_INIT,
    ENKANBAN_KEEP_OBJECT_MOVE,
    ENKANBAN_PIECE_MASK_TABLE_POINTER_LITERAL,
    ENKANBAN_PIECE_MODEL_TABLE_POINTER_LITERAL,
    ENKANBAN_SCALE_CALL,
    ENKANBAN_SCALE_LOAD,
    ENKANBAN_UPDATE,
    ENKANBAN_WHOLE_MODEL_MOVE,
    ENGS_ACTOR_ID,
    ENGS_DESTROY,
    ENGS_DRAW,
    ENGS_INIT,
    ENGS_INIT_CHAIN_POINTER_LITERAL,
    ENGS_MODEL_DRAW_SEQUENCE,
    ENGS_MODEL_LOAD_SEQUENCE,
    ENGS_UPDATE,
    FORMAT,
    build_rigid_actor_native_runtime_contract,
)
from test_enkusa_native_runtime_contract import _write_zar


def _at(address: int) -> int:
    return address - CODE_IMAGE_BASE


def _branch(address: int, target: int, *, condition: int = 0xE, link: bool = False) -> int:
    relative = (target - (address + 8)) >> 2
    return (
        (condition << 28)
        | 0x0A000000
        | (0x01000000 if link else 0)
        | (relative & 0x00FFFFFF)
    )


def _write_draw_helper(code: bytearray, address: int) -> None:
    words = {
        0x00: 0xE92D4010,
        0x04: 0xE24DD030,
        0x08: 0xE1A04000,
        0x0C: 0xE2801F52,
        0x10: 0xE1A0000D,
        0x14: _branch(address + 0x14, 0x00372224, link=True),
        0x18: 0xE5940200,
        0x1C: 0xE3500000,
        0x20: _branch(address + 0x20, address + 0x44, condition=0),
        0x24: 0xE3A01001,
        0x28: 0xE5C010AC,
        0x2C: 0xE5940200,
        0x30: 0xE1A0100D,
        0x34: _branch(address + 0x34, 0x003721E0, link=True),
        0x38: 0xE5940200,
        0x3C: 0xE3A01000,
        0x40: _branch(address + 0x40, 0x00372170, link=True),
        0x44: 0xE28DD030,
        0x48: 0xE8BD8010,
    }
    for relative, word in words.items():
        struct.pack_into("<I", code, _at(address + relative), word)


def _fixture(tmp_path: Path) -> tuple[Path, list[Path]]:
    field_keep = tmp_path / "zelda_field_keep.zar"
    _write_zar(field_keep, [f"Model/model_{index}.cmb" for index in range(6)])
    keep = tmp_path / "zelda_keep.zar"
    _write_zar(keep, [f"Model/keep_{index}.cmb" for index in range(32)])
    kanban = tmp_path / "zelda_kanban.zar"
    _write_zar(kanban, [f"Model/kanban_{index}.cmb" for index in range(12)])
    gs = tmp_path / "zelda_gs.zar"
    _write_zar(gs, ["Model/gossip_stone2_model.cmb"])

    code = bytearray(0x450000)
    overlay_table = 0x0050CD84
    profile_address = 0x0052862C
    struct.pack_into(
        "<I", code, _at(ACTOR_OVERLAY_TABLE_POINTER_LITERAL), overlay_table
    )
    overlay_entry = overlay_table + ENISHI_ACTOR_ID * ACTOR_OVERLAY_ENTRY_STRIDE
    struct.pack_into(
        "<I",
        code,
        _at(overlay_entry) + ACTOR_OVERLAY_PROFILE_POINTER_OFFSET,
        profile_address,
    )
    struct.pack_into(
        "<HBBIHHIIIII",
        code,
        _at(profile_address),
        ENISHI_ACTOR_ID,
        6,
        0,
        0x00000000,
        2,
        0,
        0x204,
        ENISHI_INIT,
        ENISHI_DESTROY,
        ENISHI_UPDATE,
        ENISHI_DRAW,
    )
    object_path = "rom:/actor/zelda_field_keep.zar"
    object_offset = _at(OBJECT_TABLE + 2 * 0x44)
    code[object_offset : object_offset + len(object_path) + 1] = (
        object_path.encode() + b"\0"
    )

    enaobj_profile = 0x0052036C
    enaobj_overlay = overlay_table + ENAOBJ_ACTOR_ID * ACTOR_OVERLAY_ENTRY_STRIDE
    struct.pack_into(
        "<I",
        code,
        _at(enaobj_overlay) + ACTOR_OVERLAY_PROFILE_POINTER_OFFSET,
        enaobj_profile,
    )
    struct.pack_into(
        "<HBBIHHIIIII",
        code,
        _at(enaobj_profile),
        ENAOBJ_ACTOR_ID,
        6,
        0,
        0x10,
        1,
        0,
        0x22C,
        ENAOBJ_INIT,
        ENAOBJ_DESTROY,
        ENAOBJ_UPDATE,
        ENAOBJ_DRAW,
    )
    keep_path = "rom:/actor/zelda_keep.zar"
    keep_offset = _at(OBJECT_TABLE + 1 * 0x44)
    code[keep_offset : keep_offset + len(keep_path) + 1] = (
        keep_path.encode() + b"\0"
    )

    enkanban_profile = 0x0052B490
    enkanban_overlay = overlay_table + ENKANBAN_ACTOR_ID * ACTOR_OVERLAY_ENTRY_STRIDE
    struct.pack_into(
        "<I",
        code,
        _at(enkanban_overlay) + ACTOR_OVERLAY_PROFILE_POINTER_OFFSET,
        enkanban_profile,
    )
    struct.pack_into(
        "<HBBIHHIIIII",
        code,
        _at(enkanban_profile),
        ENKANBAN_ACTOR_ID,
        6,
        0,
        0x80000019,
        303,
        0,
        0x284,
        ENKANBAN_INIT,
        ENKANBAN_DESTROY,
        ENKANBAN_UPDATE,
        ENKANBAN_DRAW,
    )
    kanban_path = "rom:/actor/zelda_kanban.zar"
    kanban_offset = _at(OBJECT_TABLE + 303 * 0x44)
    code[kanban_offset : kanban_offset + len(kanban_path) + 1] = (
        kanban_path.encode() + b"\0"
    )

    engs_profile = 0x00526004
    engs_overlay = overlay_table + ENGS_ACTOR_ID * ACTOR_OVERLAY_ENTRY_STRIDE
    struct.pack_into(
        "<I",
        code,
        _at(engs_overlay) + ACTOR_OVERLAY_PROFILE_POINTER_OFFSET,
        engs_profile,
    )
    struct.pack_into(
        "<HBBIHHIIIII",
        code,
        _at(engs_profile),
        ENGS_ACTOR_ID,
        6,
        0,
        0x02000009,
        392,
        0,
        0x284,
        ENGS_INIT,
        ENGS_DESTROY,
        ENGS_UPDATE,
        ENGS_DRAW,
    )
    gs_path = "rom:/actor/zelda_gs.zar"
    gs_offset = _at(OBJECT_TABLE + 392 * 0x44)
    code[gs_offset : gs_offset + len(gs_path) + 1] = gs_path.encode() + b"\0"

    engs_init_chain = 0x00526024
    struct.pack_into(
        "<I", code, _at(ENGS_INIT_CHAIN_POINTER_LITERAL), engs_init_chain
    )
    struct.pack_into("<I", code, _at(engs_init_chain), 0x00640A92)
    for relative, word in {
        0x00: 0xE3A06000,
        0x2C: 0xE1A03006,
        0x30: 0xE2842E27,
        0x38: 0xE58D6000,
        0x3C: _branch(ENGS_MODEL_LOAD_SEQUENCE + 0x3C, 0x00372F38, link=True),
    }.items():
        struct.pack_into("<I", code, _at(ENGS_MODEL_LOAD_SEQUENCE + relative), word)
    for relative, word in {
        0x00: 0xE5940270,
        0x04: 0xE3500000,
        0x08: 0x0A000006,
        0x0C: 0xE5C060AC,
        0x10: 0xE5940270,
        0x14: 0xE28D1004,
        0x18: _branch(ENGS_MODEL_DRAW_SEQUENCE + 0x18, 0x003721E0, link=True),
        0x1C: 0xE5940270,
        0x20: 0xE3A01000,
        0x24: _branch(ENGS_MODEL_DRAW_SEQUENCE + 0x24, 0x00372170, link=True),
    }.items():
        struct.pack_into("<I", code, _at(ENGS_MODEL_DRAW_SEQUENCE + relative), word)

    struct.pack_into("<I", code, _at(ENISHI_DEFAULT_MODEL_MOVE), 0xE3A00005)
    struct.pack_into("<I", code, _at(ENISHI_INIT_SELECTOR_INSTRUCTION), 0xE2015003)
    struct.pack_into(
        "<III",
        code,
        _at(ENISHI_SELECTOR_ONE_COMPARE),
        0xE3550001,
        0x13550002,
        0x03A00004,
    )
    assert ENISHI_ALTERNATE_MODEL_MOVE == ENISHI_SELECTOR_ONE_COMPARE + 8
    scale_table = 0x005284C8
    struct.pack_into("<I", code, _at(ENISHI_SCALE_TABLE_POINTER_LITERAL), scale_table)
    struct.pack_into("<3f", code, _at(scale_table), 0.1, 0.4, 1.0)

    draw_table = 0x00528620
    draw_helpers = (0x003AFA7C, 0x003C4D20, 0x001048B0)
    struct.pack_into("<5I", code, _at(ENISHI_DRAW), 0xE1D021BC, 0xE59F3008,
                     0xE2022003, 0xE7932102, 0xE12FFF12)
    struct.pack_into("<I", code, _at(ENISHI_DRAW_TABLE_POINTER_LITERAL), draw_table)
    struct.pack_into("<3I", code, _at(draw_table), *draw_helpers)
    for helper in draw_helpers:
        _write_draw_helper(code, helper)

    selector_words = {
        0x001E0754: 0xE1D001BC,
        ENAOBJ_MODEL_TABLE_ADDRESS_INSTRUCTION: 0xE28F2FB2,
        0x001E0764: 0xE1A00800,
        0x001E0768: 0xE1A00C20,
        0x001E0770: 0xE1D401BC,
        ENAOBJ_SELECTOR_MASK_INSTRUCTION: 0xE20000FF,
        0x001E0778: 0xE1C401BC,
        0x001E0788: 0xE350000B,
        0x001E078C: 0xC3A0000B,
        0x001E079C: 0xE7D13000,
    }
    for address, word in selector_words.items():
        struct.pack_into("<I", code, _at(address), word)
    model_indices = bytes((24, 24, 24, 26, 26, 27, 22, 29, 28, 30, 31, 4))
    code[_at(0x001E0A2C) : _at(0x001E0A2C) + len(model_indices)] = model_indices
    scale_handlers = (
        0x001E07DC,
        0x001E07F4,
        0x001E080C,
        0x001E0824,
        0x001E083C,
        0x001E080C,
        0x001E080C,
    )
    struct.pack_into(
        "<7I", code, _at(ENAOBJ_SCALE_JUMP_TABLE), *scale_handlers
    )
    struct.pack_into(
        "<I",
        code,
        _at(ENAOBJ_SCALE_DEFAULT_BRANCH),
        _branch(ENAOBJ_SCALE_DEFAULT_BRANCH, 0x001E083C, condition=0xE),
    )
    for handler, word in {
        0x001E07DC: 0xED9F0A95,
        0x001E07F4: 0xED9F0A90,
        0x001E080C: 0xED9F0A8B,
        0x001E0824: 0xED9F0A86,
        0x001E083C: 0xED9F0A81,
    }.items():
        struct.pack_into("<I", code, _at(handler), word)
    struct.pack_into(
        "<5f", code, _at(0x001E0A38), 0.025, 0.05, 0.1, 0.005, 0.01
    )
    draw_words = {
        0x00: 0xE92D4030,
        0x08: 0xE59011AC,
        0x18: 0xE1D001BC,
        0x1C: 0xE350000B,
        0x58: 0xE59401AC,
        0x5C: 0xE2841F52,
        0x64: 0xE59401AC,
        0x68: 0xE3A01001,
        0x6C: 0xE5C010AC,
        0x70: 0xE59401AC,
        0x78: 0xE3A01000,
        0x80: _branch(ENAOBJ_DRAW + 0x80, 0x00372170, condition=0xE),
    }
    for relative, word in draw_words.items():
        struct.pack_into("<I", code, _at(ENAOBJ_DRAW + relative), word)

    scale_literal = 0x001F7444
    struct.pack_into("<f", code, _at(scale_literal), 0.01)
    struct.pack_into("<I", code, _at(ENKANBAN_SCALE_LOAD), 0xED9F0AAC)
    struct.pack_into(
        "<I",
        code,
        _at(ENKANBAN_SCALE_CALL),
        _branch(ENKANBAN_SCALE_CALL, 0x0037572C, link=True),
    )
    for address, word in {
        0x001F7210: 0xE3E01000,
        0x001F7214: 0xE1C0ACBE,
        0x001F721C: 0xE1C01ABE,
        ENKANBAN_EFFECT_MODEL_MOVE: 0xE3A0100B,
        0x001F73C4: 0xE354000B,
        0x001F73C8: 0xE5810258,
        ENKANBAN_KEEP_OBJECT_MOVE: 0xE3A01001,
        ENKANBAN_WHOLE_MODEL_MOVE: 0xE3A0101E,
    }.items():
        struct.pack_into("<I", code, _at(address), word)
    piece_model_table = 0x0052B500
    piece_model_indices = (6, 5, 3, 10, 8, 4, 9, 7, 2, 1, 0)
    struct.pack_into(
        "<I", code, _at(ENKANBAN_PIECE_MODEL_TABLE_POINTER_LITERAL),
        piece_model_table,
    )
    struct.pack_into(
        "<11I", code, _at(piece_model_table), *piece_model_indices
    )
    piece_mask_table = 0x0052B4E8
    struct.pack_into(
        "<I", code, _at(ENKANBAN_PIECE_MASK_TABLE_POINTER_LITERAL),
        piece_mask_table,
    )
    struct.pack_into(
        "<11H", code, _at(piece_mask_table), *(1 << index for index in range(11))
    )
    zero_literal = 0x0022C22C
    local_z_literal = 0x0022C23C
    struct.pack_into("<ff", code, _at(zero_literal), 0.0, 0.0)
    struct.pack_into("<f", code, _at(local_z_literal), -100.0)
    struct.pack_into("<I", code, _at(ENKANBAN_DRAW_ZERO_LITERAL_LOAD), 0xED9F8AED)
    struct.pack_into("<I", code, _at(ENKANBAN_DRAW_LOCAL_Z_LITERAL_LOAD), 0xED9F1A5F)
    enkanban_draw_words = {
        0x0022BE68: 0xE5D601AC,
        0x0022BE74: 0xE3500000,
        0x0022BE80: _branch(0x0022BE80, 0x0022C0B0, condition=0),
        0x0022C0B0: 0xEEB00A48,
        0x0022C0B8: 0xED9F1A5F,
        0x0022C0C0: 0xEEF00A40,
        0x0022C0C8: 0xE1DB0ABE,
        0x0022C0CC: 0xE2401CFF,
        0x0022C0D0: 0xE25110FF,
        0x0022C0D8: 0xE5960250,
        0x0022C0E8: 0xE5960250,
        0x0022C0EC: _branch(0x0022C0EC, 0x003721E0, link=True),
        0x0022C0F0: 0xE5940250,
        0x0022C0F4: 0xE3A01000,
        0x0022C0F8: _branch(0x0022C0F8, 0x00372170, link=True),
    }
    for address, word in enkanban_draw_words.items():
        struct.pack_into("<I", code, _at(address), word)

    code_bin = tmp_path / "code.bin"
    code_bin.write_bytes(code)
    return code_bin, [field_keep, keep, kanban, gs]


def test_rigid_actor_contract_decodes_enishi_model_scale_and_dispatch(tmp_path: Path) -> None:
    code_bin, sources = _fixture(tmp_path)

    contract = build_rigid_actor_native_runtime_contract(code_bin, sources)

    assert contract["format"] == FORMAT
    assert contract["actor_count"] == 4
    enishi = next(actor for actor in contract["actors"] if actor["actor_name"] == "EnIshi")
    assert enishi["actor_name"] == "EnIshi"
    assert enishi["selector_contract"]["mask"] == 3
    assert enishi["selector_contract"]["supported_values"] == [0, 1, 2]
    assert [state["cmb_type_local_index"] for state in enishi["visual_states"]] == [5, 4, 4]
    assert [state["model_scale"] for state in enishi["visual_states"]] == pytest.approx([0.1, 0.4, 1.0])
    assert enishi["visual_states"][0]["model_asset_id"].endswith("Model/model_5.cmb")
    assert len(enishi["code_evidence"]["draw_helpers"]) == 3

    enaobj = next(actor for actor in contract["actors"] if actor["actor_name"] == "EnAObj")
    assert enaobj["selector_contract"]["mask"] == 0xFF
    assert [state["cmb_type_local_index"] for state in enaobj["visual_states"]] == [
        24, 24, 24, 26, 26, 27, 22, 29, 28, 30, 31, 4
    ]
    assert enaobj["visual_states"][10]["model_scale"] == pytest.approx(0.01)
    assert enaobj["visual_states"][10]["model_asset_id"].endswith("Model/keep_31.cmb")

    enkanban = next(
        actor for actor in contract["actors"] if actor["actor_name"] == "EnKanban"
    )
    assert enkanban["selector_contract"] == {
        "source": "constant",
        "value": 0,
        "supported_values": [0],
        "params_semantic": "message_id_and_special_spawn_mode",
    }
    whole = enkanban["visual_states"][0]
    assert whole["cmb_type_local_index"] == 30
    assert whole["model_scale"] == pytest.approx(0.01)
    assert whole["model_local_translation"] == pytest.approx([0.0, 0.0, -100.0])
    assert whole["model_asset_id"].endswith("Model/keep_30.cmb")
    pieces = enkanban["piece_state_contract"]["pieces"]
    assert [piece["cmb_type_local_index"] for piece in pieces] == [
        6, 5, 3, 10, 8, 4, 9, 7, 2, 1, 0
    ]
    assert [piece["state_mask"] for piece in pieces] == [
        1 << index for index in range(11)
    ]
    assert enkanban["effect_model"]["cmb_type_local_index"] == 11

    engs = next(actor for actor in contract["actors"] if actor["actor_name"] == "EnGs")
    assert engs["actor_profile"]["actor_id"] == ENGS_ACTOR_ID
    assert engs["model_handle_field_offset"] == 0x270
    assert engs["init_chain"]["model_scale"] == pytest.approx(0.1)
    assert engs["visual_states"][0]["cmb_type_local_index"] == 0
    assert engs["visual_states"][0]["model_asset_id"].endswith(
        "Model/gossip_stone2_model.cmb"
    )


def test_rigid_actor_contract_rejects_changed_draw_selector(tmp_path: Path) -> None:
    code_bin, sources = _fixture(tmp_path)
    code = bytearray(code_bin.read_bytes())
    struct.pack_into("<I", code, _at(ENISHI_DRAW_SELECTOR_INSTRUCTION), 0xE2022007)
    code_bin.write_bytes(code)

    with pytest.raises(ParseError, match="selector contract changed"):
        build_rigid_actor_native_runtime_contract(code_bin, sources)


def test_rigid_actor_contract_rejects_changed_engs_model_binding(tmp_path: Path) -> None:
    code_bin, sources = _fixture(tmp_path)
    code = bytearray(code_bin.read_bytes())
    struct.pack_into("<I", code, _at(ENGS_MODEL_LOAD_SEQUENCE + 0x30), 0xE2842E26)
    code_bin.write_bytes(code)

    with pytest.raises(ParseError, match="EnGs model binding"):
        build_rigid_actor_native_runtime_contract(code_bin, sources)
