from __future__ import annotations

import struct
from pathlib import Path

from oot3d_asset_tool.enkusa_native_runtime_contract import (
    DRAW_CALLBACK,
    DRAW_CALLBACK_LITERAL,
    ENKUSA_ACTOR_ID,
    FORMAT,
    INIT_CHAIN_POINTER_LITERAL,
    MODEL_INDEX_MOVES,
    OBJECT_READY_CALLBACK,
    OBJECT_READY_CALLBACK_LITERAL,
    OBJECT_SELECTOR_TABLE_POINTER_LITERAL,
    DESTROYED_MODEL_INDEX_MOVE,
    build_enkusa_native_runtime_contract,
)
from oot3d_asset_tool.native_actor_contract_common import (
    ACTOR_OVERLAY_ENTRY_STRIDE,
    ACTOR_OVERLAY_PROFILE_POINTER_OFFSET,
    ACTOR_OVERLAY_TABLE_POINTER_LITERAL,
    CODE_IMAGE_BASE,
    OBJECT_TABLE,
)


def _at(address: int) -> int:
    return address - CODE_IMAGE_BASE


def _write_zar(path: Path, model_names: list[str]) -> None:
    type_section = 0x20
    meta_section = 0x30
    type_list = meta_section + len(model_names) * 8
    type_name = type_list + len(model_names) * 4
    cursor = type_name + 4
    file_name_offsets: list[int] = []
    for name in model_names:
        file_name_offsets.append(cursor)
        cursor += len(name) + 1
    data_section = (cursor + 3) & ~3
    data_offsets = [data_section + len(model_names) * 4 + index * 3
                    for index in range(len(model_names))]
    data = bytearray(data_section + len(model_names) * 7)
    data[0:4] = b"ZAR\x01"
    struct.pack_into("<IHHIII", data, 4, len(data), 1, len(model_names),
                     type_section, meta_section, data_section)
    struct.pack_into("<III", data, type_section, len(model_names), type_list, type_name)
    data[type_name:type_name + 4] = b"cmb\0"
    for index, name in enumerate(model_names):
        struct.pack_into("<I", data, type_list + index * 4, index)
        struct.pack_into("<II", data, meta_section + index * 8, 3, file_name_offsets[index])
        data[file_name_offsets[index]:file_name_offsets[index] + len(name) + 1] = name.encode() + b"\0"
        struct.pack_into("<I", data, data_section + index * 4, data_offsets[index])
        data[data_offsets[index]:data_offsets[index] + 3] = b"cmb"
    path.write_bytes(data)


def _mov_immediate(register: int, value: int) -> int:
    return 0xE3A00000 | (register << 12) | value


def test_enkusa_contract_uses_native_object_and_cmb_selectors(tmp_path: Path) -> None:
    field_keep = tmp_path / "zelda_field_keep.zar"
    kusa = tmp_path / "zelda_kusa.zar"
    keep = tmp_path / "zelda_keep.zar"
    _write_zar(field_keep, [f"Model/field_{index}.cmb" for index in range(11)])
    _write_zar(kusa, ["Model/obj_kusa01_model.cmb", "Model/obj_kusa03_model.cmb"])
    _write_zar(keep, ["Model/keep.cmb"])

    code = bytearray(0x450000)
    overlay_table = 0x0050CD84
    profile_address = 0x0052BE58
    struct.pack_into("<I", code, _at(ACTOR_OVERLAY_TABLE_POINTER_LITERAL), overlay_table)
    overlay_entry = overlay_table + ENKUSA_ACTOR_ID * ACTOR_OVERLAY_ENTRY_STRIDE
    struct.pack_into(
        "<I", code, _at(overlay_entry) + ACTOR_OVERLAY_PROFILE_POINTER_OFFSET,
        profile_address,
    )
    struct.pack_into(
        "<HBBIHHIIIII", code, _at(profile_address), ENKUSA_ACTOR_ID, 6, 0,
        0x80800010, 1, 0, 0x210, 0x001B64EC, 0x003501E8,
        0x001F75C4, 0,
    )
    paths = {
        1: "rom:/actor/zelda_keep.zar",
        2: "rom:/actor/zelda_field_keep.zar",
        0x12B: "rom:/actor/zelda_kusa.zar",
    }
    for object_id, path in paths.items():
        offset = _at(OBJECT_TABLE + object_id * 0x44)
        code[offset:offset + len(path) + 1] = path.encode() + b"\0"

    init_chain = 0x0052BE40
    struct.pack_into("<I", code, _at(INIT_CHAIN_POINTER_LITERAL), init_chain)
    scale_entry = (400 << 16) | (0x54 << 5) | (9 << 1)
    struct.pack_into("<I", code, _at(init_chain), scale_entry)
    selector_table = 0x0052BDB8
    struct.pack_into("<I", code, _at(OBJECT_SELECTOR_TABLE_POINTER_LITERAL), selector_table)
    struct.pack_into("<4H", code, _at(selector_table), 2, 0x12B, 0x12B, 0)
    struct.pack_into("<I", code, _at(OBJECT_READY_CALLBACK_LITERAL), OBJECT_READY_CALLBACK)
    struct.pack_into("<I", code, _at(DRAW_CALLBACK_LITERAL), DRAW_CALLBACK)
    struct.pack_into("<I", code, _at(MODEL_INDEX_MOVES[0]), _mov_immediate(3, 10))
    struct.pack_into("<I", code, _at(MODEL_INDEX_MOVES[1]), _mov_immediate(3, 0))
    struct.pack_into("<I", code, _at(DESTROYED_MODEL_INDEX_MOVE), _mov_immediate(3, 1))
    code_bin = tmp_path / "code.bin"
    code_bin.write_bytes(code)

    contract = build_enkusa_native_runtime_contract(
        code_bin, [keep, field_keep, kusa]
    )

    assert contract["format"] == FORMAT
    assert contract["status"] == "initial_visual_complete"
    assert contract["actor_profile"]["draw_address"] == 0
    assert contract["init_chain"]["model_scale"] == 0.4
    assert contract["object_selector_ids"] == [2, 0x12B, 0x12B, 0]
    assert contract["visual_states"][0]["intact_cmb_type_local_index"] == 10
    assert contract["visual_states"][1]["intact_cmb_member"] == "Model/obj_kusa01_model.cmb"
    assert contract["visual_states"][1]["destroyed_cmb_member"] == "Model/obj_kusa03_model.cmb"
    assert contract["unsupported_selector"]["native_result"] == "actor_kill"
