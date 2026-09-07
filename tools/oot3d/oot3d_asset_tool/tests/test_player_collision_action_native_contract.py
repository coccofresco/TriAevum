from __future__ import annotations

import struct
from pathlib import Path

from oot3d_asset_tool.player_collision_action_native_contract import (
    AGE_PROPERTIES_EXPECTED_ADDRESS,
    AGE_PROPERTIES_POINTER_LITERAL,
    AGE_PROPERTIES_RECORD_STRIDE,
    CODE_IMAGE_BASE,
    EXPECTED_WALL_FLAGS,
    FLOAT_LITERALS,
    GROUND_CLIMB_ACTION_WRAPPER_FUNCTION,
    GROUND_CLIMB_ACTION_WRAPPER_POINTER_LITERAL,
    GROUND_CLIMB_FLOAT_LITERALS,
    GROUND_CLIMB_IMMEDIATE_INSTRUCTIONS,
    IMMEDIATE_INSTRUCTIONS,
    INTEGER_LITERALS,
    PLAYER_START_MODE_SELECTOR_INSTRUCTIONS,
    PLAYER_START_MODE_TABLE_COUNT,
    PLAYER_START_MODE_TABLE_EXPECTED_ADDRESS,
    PLAYER_START_MODE_TABLE_POINTER_LITERAL,
    SCENE_ENTRANCE_ACTION_FUNCTION,
    SCENE_ENTRANCE_ACTION_POINTER_LITERAL,
    SCENE_ENTRANCE_FLOAT_LITERALS,
    SCENE_ENTRANCE_IMMEDIATE_INSTRUCTIONS,
    SCENE_ENTRANCE_START_MODES,
    SURFACE_CLIMB_ACTION_FUNCTION,
    SURFACE_CLIMB_ACTION_POINTER_LITERAL,
    SURFACE_CLIMB_FLOAT_LITERALS,
    SURFACE_CLIMB_IMMEDIATE_INSTRUCTIONS,
    SURFACE_WALL_FLAGS_EXPECTED_ADDRESS,
    SURFACE_WALL_FLAGS_POINTER_LITERAL,
    build_player_collision_action_native_contract,
)


def _offset(address: int) -> int:
    return address - CODE_IMAGE_BASE


def test_player_collision_action_contract_decodes_native_tables(tmp_path: Path) -> None:
    final_address = max(
        SURFACE_WALL_FLAGS_EXPECTED_ADDRESS + len(EXPECTED_WALL_FLAGS) * 4,
        AGE_PROPERTIES_EXPECTED_ADDRESS + AGE_PROPERTIES_RECORD_STRIDE * 2,
        *(address + 4 for address, _ in FLOAT_LITERALS.values()),
        *(address + 4 for address, _ in INTEGER_LITERALS.values()),
        *(address + 4 for address, _, _ in IMMEDIATE_INSTRUCTIONS.values()),
        *(address + 4 for address, _ in SURFACE_CLIMB_FLOAT_LITERALS.values()),
        *(address + 4 for address, _, _ in SURFACE_CLIMB_IMMEDIATE_INSTRUCTIONS.values()),
        *(address + 4 for address, _ in GROUND_CLIMB_FLOAT_LITERALS.values()),
        *(address + 4 for address, _, _ in GROUND_CLIMB_IMMEDIATE_INSTRUCTIONS.values()),
        *(address + 4 for address, _, _ in PLAYER_START_MODE_SELECTOR_INSTRUCTIONS.values()),
        *(address + 4 for address, _ in SCENE_ENTRANCE_FLOAT_LITERALS.values()),
        *(address + 4 for address, _, _ in SCENE_ENTRANCE_IMMEDIATE_INSTRUCTIONS.values()),
        PLAYER_START_MODE_TABLE_EXPECTED_ADDRESS + PLAYER_START_MODE_TABLE_COUNT * 4,
        PLAYER_START_MODE_TABLE_POINTER_LITERAL + 4,
        SCENE_ENTRANCE_ACTION_POINTER_LITERAL + 4,
        SURFACE_CLIMB_ACTION_POINTER_LITERAL + 4,
        GROUND_CLIMB_ACTION_WRAPPER_POINTER_LITERAL + 4,
    )
    code = bytearray(final_address - CODE_IMAGE_BASE)
    struct.pack_into(
        "<I", code, _offset(AGE_PROPERTIES_POINTER_LITERAL), AGE_PROPERTIES_EXPECTED_ADDRESS
    )
    struct.pack_into(
        "<I",
        code,
        _offset(PLAYER_START_MODE_TABLE_POINTER_LITERAL),
        PLAYER_START_MODE_TABLE_EXPECTED_ADDRESS,
    )
    start_mode_functions = [0] * PLAYER_START_MODE_TABLE_COUNT
    for _, (index, function_address) in SCENE_ENTRANCE_START_MODES.items():
        start_mode_functions[index] = function_address
    for index, function_address in enumerate(start_mode_functions):
        struct.pack_into(
            "<I",
            code,
            _offset(PLAYER_START_MODE_TABLE_EXPECTED_ADDRESS + index * 4),
            function_address,
        )
    for address, instruction_word, _ in PLAYER_START_MODE_SELECTOR_INSTRUCTIONS.values():
        struct.pack_into("<I", code, _offset(address), instruction_word)
    struct.pack_into(
        "<I",
        code,
        _offset(SCENE_ENTRANCE_ACTION_POINTER_LITERAL),
        SCENE_ENTRANCE_ACTION_FUNCTION,
    )
    for address, value in SCENE_ENTRANCE_FLOAT_LITERALS.values():
        struct.pack_into("<f", code, _offset(address), value)
    for address, instruction_word, _ in SCENE_ENTRANCE_IMMEDIATE_INSTRUCTIONS.values():
        struct.pack_into("<I", code, _offset(address), instruction_word)
    struct.pack_into(
        "<I",
        code,
        _offset(SURFACE_WALL_FLAGS_POINTER_LITERAL),
        SURFACE_WALL_FLAGS_EXPECTED_ADDRESS,
    )
    for index, value in enumerate(EXPECTED_WALL_FLAGS):
        struct.pack_into(
            "<I", code, _offset(SURFACE_WALL_FLAGS_EXPECTED_ADDRESS + index * 4), value
        )
    for record_index, values in enumerate(
        (
            (56.0, 111.0, 79.4, 59.0, 41.0, 18.0),
            (40.0, 71.0, 47.0, 39.0, 27.0, 14.0),
        )
    ):
        record = AGE_PROPERTIES_EXPECTED_ADDRESS + record_index * AGE_PROPERTIES_RECORD_STRIDE
        for offset, value in zip((0x00, 0x0C, 0x14, 0x18, 0x1C, 0x38), values):
            struct.pack_into("<f", code, _offset(record + offset), value)
    for address, value in FLOAT_LITERALS.values():
        struct.pack_into("<f", code, _offset(address), value)
    for address, value in INTEGER_LITERALS.values():
        struct.pack_into("<I", code, _offset(address), value)
    for address, instruction_word, _ in IMMEDIATE_INSTRUCTIONS.values():
        struct.pack_into("<I", code, _offset(address), instruction_word)
    child_record = AGE_PROPERTIES_EXPECTED_ADDRESS + AGE_PROPERTIES_RECORD_STRIDE
    struct.pack_into("<f", code, _offset(child_record + 0x40), 55.0)
    struct.pack_into(
        "<I",
        code,
        _offset(SURFACE_CLIMB_ACTION_POINTER_LITERAL),
        SURFACE_CLIMB_ACTION_FUNCTION,
    )
    for address, value in SURFACE_CLIMB_FLOAT_LITERALS.values():
        struct.pack_into("<f", code, _offset(address), value)
    for address, instruction_word, _ in SURFACE_CLIMB_IMMEDIATE_INSTRUCTIONS.values():
        struct.pack_into("<I", code, _offset(address), instruction_word)
    struct.pack_into(
        "<I",
        code,
        _offset(GROUND_CLIMB_ACTION_WRAPPER_POINTER_LITERAL),
        GROUND_CLIMB_ACTION_WRAPPER_FUNCTION,
    )
    for address, value in GROUND_CLIMB_FLOAT_LITERALS.values():
        struct.pack_into("<f", code, _offset(address), value)
    for address, instruction_word, _ in GROUND_CLIMB_IMMEDIATE_INSTRUCTIONS.values():
        struct.pack_into("<I", code, _offset(address), instruction_word)

    code_bin = tmp_path / "code.bin"
    code_bin.write_bytes(code)
    contract = build_player_collision_action_native_contract(code_bin)

    assert contract["status"] == "ready"
    action_state = contract["player_action_state_machine"]
    assert action_state["start_mode_selector"]["params_mask"]["value"] == 0x0F00
    entrance = action_state["scene_entrance"]
    assert entrance["action_function_address"] == 0x00496458
    assert entrance["start_modes"]["idle"]["index"] == 13
    assert entrance["start_modes"]["idle"]["target_distance"]["value"] == 180.0
    assert entrance["start_modes"]["move_forward_slow"]["timer"]["value"] == -15
    assert entrance["start_modes"]["move_forward"]["timer_numerator"]["value"] == -80.0
    child = contract["age_properties"]["records"][1]
    assert child["fields"]["ledge_floor_probe_height"]["value"] == 71.0
    assert child["fields"]["wall_check_radius"]["value"] == 14.0
    assert contract["surface_wall_flags"]["values"][:8] == [0, 1, 3, 5, 8, 16, 32, 64]
    assert contract["ledge_detector"]["derived"]["floor_normal_y_abs_minimum_exclusive"] == 28000
    assert contract["surface_climb"]["action_function_address"] == 0x004BE20C
    assert contract["surface_climb"]["immediate_values"]["free_climb_wall_flag_mask"]["value"] == 8
    assert abs(
        contract["surface_climb"]["float_literals"]["maximum_play_speed"]["value"] - 3.35
    ) < 1e-6
    ground_entry = contract["ground_climb_entry"]
    assert ground_entry["action_wrapper_function_address"] == 0x004C11A0
    assert ground_entry["immediate_values"]["regular_ladder_wall_flag_mask"]["value"] == 2
    assert ground_entry["immediate_values"]["regular_climb_start_front_age_field_offset"]["value"] == 0x104
    assert ground_entry["float_literals"]["rung_interval"]["value"] == 15.0
    assert ground_entry["float_literals"]["wall_plane_inset"]["value"] == 1.0
