from __future__ import annotations

import struct
from pathlib import Path

from oot3d_asset_tool.enholl_native_runtime_contract import (
    ACTION_TABLE_ENTRY_COUNT,
    ACTION_TABLE_POINTER_LITERAL,
    DEFAULT_HALF_WIDTH_LITERAL,
    ENHOLL_ACTOR_ID,
    EXPECTED_PROFILE,
    FORMAT,
    LOCAL_COORDINATE_CALLSITE,
    NARROW_HALF_WIDTH_LITERAL,
    NEXT_ACTION_POINTER_LITERAL,
    NEXT_ACTION_TABLE_POINTER_LITERAL,
    ROOM_COMMIT_CALLSITE,
    ROOM_REQUEST_CALLSITE,
    ROOM_REQUEST_HANDLER,
    VERTICAL_MIN_LITERAL,
    build_enholl_native_runtime_contract,
)
from oot3d_asset_tool.native_actor_contract_common import (
    ACTOR_OVERLAY_ENTRY_STRIDE,
    ACTOR_OVERLAY_PROFILE_POINTER_OFFSET,
    ACTOR_OVERLAY_TABLE_POINTER_LITERAL,
    CODE_IMAGE_BASE,
)


def _at(address: int) -> int:
    return address - CODE_IMAGE_BASE


def _arm_bl(address: int, target: int) -> int:
    displacement = target - (address + 8)
    assert displacement % 4 == 0
    return 0xEB000000 | ((displacement >> 2) & 0x00FFFFFF)


def test_enholl_contract_decodes_native_room_request_modes(tmp_path: Path) -> None:
    code = bytearray(0x450000)
    overlay_table = 0x0050CD84
    profile_address = 0x00526460
    struct.pack_into(
        "<I", code, _at(ACTOR_OVERLAY_TABLE_POINTER_LITERAL), overlay_table
    )
    overlay_entry = overlay_table + ENHOLL_ACTOR_ID * ACTOR_OVERLAY_ENTRY_STRIDE
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
        ENHOLL_ACTOR_ID,
        EXPECTED_PROFILE["category"],
        0,
        EXPECTED_PROFILE["flags"],
        EXPECTED_PROFILE["object_id"],
        0,
        EXPECTED_PROFILE["instance_size"],
        EXPECTED_PROFILE["init_address"],
        EXPECTED_PROFILE["destroy_address"],
        EXPECTED_PROFILE["update_address"],
        EXPECTED_PROFILE["draw_address"],
    )

    action_table = 0x00526488
    handlers = [
        0x003F1D7C,
        0x00239934,
        0x00275264,
        0x001EAAB0,
        ROOM_REQUEST_HANDLER,
        0x0021DF78,
        ROOM_REQUEST_HANDLER,
    ]
    assert len(handlers) == ACTION_TABLE_ENTRY_COUNT
    struct.pack_into("<I", code, _at(ACTION_TABLE_POINTER_LITERAL), action_table)
    struct.pack_into("<I", code, _at(NEXT_ACTION_TABLE_POINTER_LITERAL), action_table)
    struct.pack_into(f"<{len(handlers)}I", code, _at(action_table), *handlers)
    for address in (0x001B3D28, 0x001CE728):
        struct.pack_into("<I", code, _at(address), 0xE1A00B80)
    struct.pack_into("<I", code, _at(0x001B3D2C), 0xE1B01EA0)
    struct.pack_into("<I", code, _at(0x001CE72C), 0xE1B00EA0)
    struct.pack_into("<I", code, _at(0x001B3D74), 0xE1A02522)

    local_coordinate_helper = 0x0036C5D8
    room_request_function = 0x0033B6BC
    room_commit_function = 0x0036C520
    next_action = 0x001CE6B8
    struct.pack_into(
        "<I",
        code,
        _at(LOCAL_COORDINATE_CALLSITE),
        _arm_bl(LOCAL_COORDINATE_CALLSITE, local_coordinate_helper),
    )
    struct.pack_into(
        "<I",
        code,
        _at(ROOM_REQUEST_CALLSITE),
        _arm_bl(ROOM_REQUEST_CALLSITE, room_request_function),
    )
    struct.pack_into(
        "<I",
        code,
        _at(ROOM_COMMIT_CALLSITE),
        _arm_bl(ROOM_COMMIT_CALLSITE, room_commit_function),
    )
    struct.pack_into("<I", code, _at(NEXT_ACTION_POINTER_LITERAL), next_action)
    struct.pack_into("<f", code, _at(DEFAULT_HALF_WIDTH_LITERAL), 200.0)
    struct.pack_into("<f", code, _at(NARROW_HALF_WIDTH_LITERAL), 100.0)
    struct.pack_into("<f", code, _at(VERTICAL_MIN_LITERAL), -50.0)

    code_bin = tmp_path / "code.bin"
    code_bin.write_bytes(code)
    contract = build_enholl_native_runtime_contract(code_bin)

    assert contract["format"] == FORMAT
    assert contract["status"] == "room_request_modes_4_6_complete"
    assert contract["action_table"]["handlers"] == handlers
    assert contract["parameter_layout"]["action_selector_shift"] == 6
    assert contract["parameter_layout"]["transition_index_shift"] == 10
    trigger = contract["room_request_trigger"]
    assert trigger["supported_modes"] == [4, 6]
    assert trigger["local_coordinate_helper_address"] == local_coordinate_helper
    assert trigger["room_request_function_address"] == room_request_function
    assert trigger["room_commit_function_address"] == room_commit_function
    assert trigger["next_action_address"] == next_action
    assert trigger["bounds"] == {
        "vertical_min": -50.0,
        "vertical_max": 200.0,
        "absolute_depth_min": 50.0,
        "absolute_depth_max": 100.0,
        "default_half_width": 200.0,
        "narrow_half_width": 100.0,
        "strict_comparisons": True,
    }
