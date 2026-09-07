from __future__ import annotations

import struct
from pathlib import Path

from oot3d_asset_tool.native_actor_contract_common import (
    ACTOR_OVERLAY_ENTRY_STRIDE,
    ACTOR_OVERLAY_PROFILE_POINTER_OFFSET,
    ACTOR_OVERLAY_TABLE_POINTER_LITERAL,
    CODE_IMAGE_BASE,
    OBJECT_TABLE,
)
from oot3d_asset_tool.objhana_native_runtime_contract import (
    FORMAT,
    OBJHANA_ACTOR_ID,
    SELECTOR_MASK_INSTRUCTION,
    VISUAL_TABLE_POINTER_LITERAL,
    build_objhana_native_runtime_contract,
)
from test_enkusa_native_runtime_contract import _write_zar


def _at(address: int) -> int:
    return address - CODE_IMAGE_BASE


def test_objhana_contract_decodes_native_visual_table(tmp_path: Path) -> None:
    field_keep = tmp_path / "zelda_field_keep.zar"
    _write_zar(field_keep, [f"Model/model_{index}.cmb" for index in range(14)])

    code = bytearray(0x450000)
    overlay_table = 0x0050CD84
    profile_address = 0x00535274
    struct.pack_into("<I", code, _at(ACTOR_OVERLAY_TABLE_POINTER_LITERAL), overlay_table)
    overlay_entry = overlay_table + OBJHANA_ACTOR_ID * ACTOR_OVERLAY_ENTRY_STRIDE
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
        OBJHANA_ACTOR_ID,
        6,
        0,
        0x80000000,
        2,
        0,
        0x200,
        0x001E1734,
        0x001E182C,
        0x002157E4,
        0x00215798,
    )
    object_path = "rom:/actor/zelda_field_keep.zar"
    object_offset = _at(OBJECT_TABLE + 2 * 0x44)
    code[object_offset : object_offset + len(object_path) + 1] = (
        object_path.encode() + b"\0"
    )

    # and r6, r0, #3
    struct.pack_into("<I", code, _at(SELECTOR_MASK_INSTRUCTION), 0xE2006003)
    visual_table = 0x005350D4
    struct.pack_into("<I", code, _at(VISUAL_TABLE_POINTER_LITERAL), visual_table)
    rows = (
        (13, 0.01, 0.0, -1, 0),
        (5, 0.1, 58.0, 10, 18),
        (10, 0.4, 0.0, 12, 44),
    )
    for index, row in enumerate(rows):
        struct.pack_into("<Iffhh", code, _at(visual_table) + index * 0x10, *row)
    code_bin = tmp_path / "code.bin"
    code_bin.write_bytes(code)

    contract = build_objhana_native_runtime_contract(code_bin, [field_keep])

    assert contract["format"] == FORMAT
    assert contract["status"] == "initial_visual_complete"
    assert contract["selector_mask"] == 3
    assert [state["cmb_type_local_index"] for state in contract["visual_states"]] == [13, 5, 10]
    assert contract["visual_states"][1]["model_scale"] == struct.unpack("<f", struct.pack("<f", 0.1))[0]
    assert contract["visual_states"][1]["collider_radius"] == 10
    assert contract["visual_states"][1]["collider_height"] == 18
    assert contract["visual_states"][2]["model_asset_id"].endswith("Model/model_10.cmb")
