from __future__ import annotations

import struct
from pathlib import Path

import pytest

from oot3d_asset_tool.binary import ParseError
from oot3d_asset_tool.item_actor_native_runtime_contract import (
    ENITEM00_ACTOR_ID,
    ENITEM00_DESTROY,
    ENITEM00_DRAW,
    ENITEM00_INIT,
    ENITEM00_INITIAL_ACTION_POINTER_LITERAL,
    ENITEM00_MODEL_TABLE_POINTER_LITERAL,
    ENITEM00_SCALE_JUMP_TABLE,
    ENITEM00_SELECTOR_MASK_INSTRUCTION,
    ENITEM00_UPDATE,
    FORMAT,
    MODEL_HIDE_MESH,
    MODEL_SHOW_MESH,
    MODEL_VISIBILITY_PRESET,
    build_item_actor_native_runtime_contract,
)
from oot3d_asset_tool.native_actor_contract_common import (
    ACTOR_OVERLAY_ENTRY_STRIDE,
    ACTOR_OVERLAY_PROFILE_POINTER_OFFSET,
    ACTOR_OVERLAY_TABLE_POINTER_LITERAL,
    CODE_IMAGE_BASE,
    OBJECT_TABLE,
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


def _vldr_s16(address: int, literal: int) -> int:
    displacement = literal - (address + 8)
    assert displacement >= 0 and displacement % 4 == 0
    return 0xED9F8A00 | (displacement // 4)


def _write_object_path(code: bytearray, object_id: int, path: str) -> None:
    offset = _at(OBJECT_TABLE + object_id * 0x44)
    code[offset : offset + len(path) + 1] = path.encode() + b"\0"


def _fixture(tmp_path: Path) -> tuple[Path, list[Path]]:
    keep = tmp_path / "zelda_keep.zar"
    shield1 = tmp_path / "zelda_gi_shield_1.zar"
    shield2 = tmp_path / "zelda_gi_shield_2.zar"
    clothes = tmp_path / "zelda_gi_clothes.zar"
    _write_zar(keep, [f"Model/keep_{index}.cmb" for index in range(80)])
    _write_zar(shield1, ["Model/shield1.cmb"])
    _write_zar(shield2, ["Model/shield2.cmb"])
    _write_zar(clothes, ["Model/clothes0.cmb", "Model/clothes1.cmb"])

    code = bytearray(0x450000)
    overlay_table = 0x0050CD84
    profile_address = 0x005288DC
    struct.pack_into(
        "<I", code, _at(ACTOR_OVERLAY_TABLE_POINTER_LITERAL), overlay_table
    )
    overlay_entry = overlay_table + ENITEM00_ACTOR_ID * ACTOR_OVERLAY_ENTRY_STRIDE
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
        ENITEM00_ACTOR_ID,
        8,
        0,
        0,
        1,
        0,
        0x218,
        ENITEM00_INIT,
        ENITEM00_DESTROY,
        ENITEM00_UPDATE,
        ENITEM00_DRAW,
    )
    _write_object_path(code, 1, "rom:/actor/zelda_keep.zar")
    _write_object_path(code, 203, "rom:/actor/zelda_gi_shield_1.zar")
    _write_object_path(code, 220, "rom:/actor/zelda_gi_shield_2.zar")
    _write_object_path(code, 242, "rom:/actor/zelda_gi_clothes.zar")

    struct.pack_into("<I", code, _at(ENITEM00_SELECTOR_MASK_INSTRUCTION), 0xE20000FF)
    handlers = (
        0x001F6ACC, 0x001F6ACC, 0x001F6ACC, 0x001F6B34, 0x001F6BCC,
        0x001F6B90, 0x001F6B0C, 0x001F6B68, 0x001F6BB0, 0x001F6BB0,
        0x001F6BB0, 0x001F6BCC, 0x001F6BCC, 0x001F6BCC, 0x001F6BE8,
        0x001F6BCC, 0x001F6BCC, 0x001F6AE8, 0x001F6C3C, 0x001F6C04,
        0x001F6C20, 0x001F6C5C, 0x001F6C5C, 0x001F6C5C, 0x001F6C5C,
        0x001F6BCC,
    )
    struct.pack_into("<26I", code, _at(ENITEM00_SCALE_JUMP_TABLE), *handlers)
    scale_literal = 0x001F6D1C
    y_offset_literal = 0x001F6D20
    struct.pack_into("<ff", code, _at(scale_literal), 0.015, 750.0)
    for handler in sorted(set(handlers)):
        struct.pack_into(
            "<III",
            code,
            _at(handler),
            _vldr_s16(handler, scale_literal),
            0xED848A6D,
            _vldr_s16(handler + 8, y_offset_literal),
        )

    model_table = 0x004D972E
    records = [(1, 65, 0)] * 26
    records[3] = (1, 66, 0)
    records[21] = (203, 0, 0)
    records[22] = (220, 0, 0)
    records[23] = (242, 1, 0)
    records[24] = (242, 0, 0)
    struct.pack_into("<I", code, _at(ENITEM00_MODEL_TABLE_POINTER_LITERAL), model_table)
    for index, record in enumerate(records):
        struct.pack_into("<hBB", code, _at(model_table + index * 4), *record)

    action = 0x004C02E8
    struct.pack_into("<I", code, _at(ENITEM00_INITIAL_ACTION_POINTER_LITERAL), action)
    action_words = {
        0x20: 0xE3500003,
        0x24: _branch(action + 0x24, action + 0x50, condition=0xD),
        0x28: 0xE3500014,
        0x2C: 0x12401006,
        0x30: 0x13510004,
        0x34: _branch(action + 0x34, action + 0x50, condition=0x9),
        0x38: 0xE350000D,
        0x3C: 0x12401010,
        0x40: 0x13510001,
        0x44: 0x82401015,
        0x48: 0x83510004,
        0x4C: _branch(action + 0x4C, action + 0x60, condition=0x8),
        0x50: 0xE1D40BBE,
        0x54: 0xE2800D0A,
        0x58: 0xE1C40BBE,
    }
    for relative, word in action_words.items():
        struct.pack_into("<I", code, _at(action + relative), word)

    struct.pack_into("<II", code, _at(MODEL_VISIBILITY_PRESET + 0x10),
                     0xE241006C, 0xE3500006)
    visibility_handlers = (
        0x003691B0, 0x003691FC, 0x00369248,
        MODEL_VISIBILITY_PRESET + 0x1C, 0x003692E0, 0x00369294,
    )
    struct.pack_into(
        "<6I", code, _at(MODEL_VISIBILITY_PRESET + 0x20), *visibility_handlers
    )
    for handler, mesh_index in zip(
        (0x003691B0, 0x003691FC, 0x00369248, 0x00369294, 0x003692E0),
        (0, 1, 2, 3, 4),
    ):
        struct.pack_into(
            "<III",
            code,
            _at(handler),
            0xE3A01000 | mesh_index,
            0xE1A00004,
            _branch(handler + 8, MODEL_SHOW_MESH, link=True),
        )
    preset_moves = {
        0x001F6D8C: 0x03A0106C,
        0x001F6DA0: 0xE3A0106D,
        0x001F6D74: 0x03A0106E,
        0x001F6DCC: 0xE3A01071,
        0x001F6DBC: 0xE3A01070,
    }
    for address, word in preset_moves.items():
        struct.pack_into("<I", code, _at(address), word)
    for address in (0x001F6DC0, 0x001F6DD0):
        struct.pack_into(
            "<I", code, _at(address), _branch(address, MODEL_VISIBILITY_PRESET, link=True)
        )

    draw_words = {
        0x00: 0xE92D4010, 0x04: 0xE1A04000, 0x08: 0xE2800C01,
        0x0C: 0xE1D01AFE, 0x10: 0xE1D00BF0, 0x14: 0xE1100001,
        0x1C: 0xE5940210, 0x20: 0xE2841F52, 0x24: 0xE3500000,
        0x2C: 0xE5D42214, 0x30: 0xE3520000,
        0x38: _branch(ENITEM00_DRAW + 0x38, 0x003721E0, link=True),
        0x3C: 0xE5940210, 0x40: 0xE3A01001, 0x44: 0xE5C010AC,
        0x48: 0xE5940210, 0x50: 0xE3A01000,
        0x54: _branch(ENITEM00_DRAW + 0x54, 0x00372170),
        0x58: 0xE8BD8010,
    }
    for relative, word in draw_words.items():
        struct.pack_into("<I", code, _at(ENITEM00_DRAW + relative), word)

    code_bin = tmp_path / "code.bin"
    code_bin.write_bytes(code)
    return code_bin, [keep]


def test_item_actor_contract_decodes_native_models_meshes_and_transforms(tmp_path: Path) -> None:
    code_bin, sources = _fixture(tmp_path)

    contract = build_item_actor_native_runtime_contract(code_bin, sources)

    assert contract["format"] == FORMAT
    actor = contract["actors"][0]
    assert actor["actor_name"] == "EnItem00"
    assert len(actor["visual_states"]) == 26
    states = {state["selector"]: state for state in actor["visual_states"]}
    assert states[0]["cmb_type_local_index"] == 65
    assert states[0]["visible_mesh_indices"] == [0]
    assert states[1]["visible_mesh_indices"] == [1]
    assert states[3]["cmb_type_local_index"] == 66
    assert states[0]["model_scale"] == pytest.approx(0.015)
    assert states[0]["shape_y_offset"] == pytest.approx(750.0)
    assert states[3]["shape_yaw_step_per_native_tick"] == 0x280
    assert states[21]["object_path"] == "actor/zelda_gi_shield_1.zar"
    assert actor["initial_draw_gate"]["suppressed_when_mask_nonzero"] == 0x4000


def test_item_actor_contract_rejects_changed_selector_mask(tmp_path: Path) -> None:
    code_bin, sources = _fixture(tmp_path)
    code = bytearray(code_bin.read_bytes())
    struct.pack_into("<I", code, _at(ENITEM00_SELECTOR_MASK_INSTRUCTION), 0xE200007F)
    code_bin.write_bytes(code)

    with pytest.raises(ParseError, match="selector mask changed"):
        build_item_actor_native_runtime_contract(code_bin, sources)
