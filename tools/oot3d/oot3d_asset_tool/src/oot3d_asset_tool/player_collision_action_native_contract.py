from __future__ import annotations

import hashlib
import math
import struct
from pathlib import Path

from .binary import ParseError


FORMAT = "oot3d_player_collision_action_native_contract_v2"
CODE_IMAGE_BASE = 0x00100000
PLAYER_COLLISION_FUNCTION = 0x0032EEB4
PLAYER_INIT_FUNCTION = 0x00191844
PLAYER_START_MODE_TABLE_POINTER_LITERAL = 0x001924D0
PLAYER_START_MODE_TABLE_EXPECTED_ADDRESS = 0x0053C15C
PLAYER_START_MODE_TABLE_COUNT = 16
PLAYER_START_MODE_SELECTOR_INSTRUCTIONS = {
    "params_mask": (0x001923EC, 0xE2000C0F, 0x0F00),
    "params_shift": (0x001923F0, 0xE1A07420, 8),
    "table_index": (0x0019241C, 0xE7902107, 4),
}
SCENE_ENTRANCE_SETUP_FUNCTION = 0x0033EA74
SCENE_ENTRANCE_ACTION_FUNCTION = 0x00496458
SCENE_ENTRANCE_ACTION_POINTER_LITERAL = 0x0033EBF8
SCENE_ENTRANCE_START_MODES = {
    "idle": (13, 0x00276344),
    "move_forward_slow": (14, 0x0025D3F0),
    "move_forward": (15, 0x001D0364),
}
SCENE_ENTRANCE_FLOAT_LITERALS = {
    "idle_target_distance": (0x00276368, 180.0),
    "slow_speed": (0x0025D42C, 2.0),
    "slow_target_distance": (0x0025D434, 120.0),
    "forward_minimum_speed_compare": (0x001D03DC, 0.1),
    "forward_minimum_speed": (0x001D03E0, 0.1),
    "forward_target_distance": (0x001D03E4, 800.0),
    "forward_timer_numerator": (0x001D03E8, -80.0),
    "action_initial_linear_speed": (0x00496748, 0.1),
    "action_default_target_speed": (0x0049674C, 5.0),
    "action_floor_probe_y_offset": (0x00496770, 50.0),
}
SCENE_ENTRANCE_IMMEDIATE_INSTRUCTIONS = {
    "idle_timer": (0x0027635C, 0x13E00013, -20),
    "slow_timer": (0x0025D420, 0x13E0000E, -15),
    "forward_timer_clamp_compare": (0x001D03C4, 0xE3700014, -20),
    "forward_timer_clamp_value": (0x001D03CC, 0xB3E00013, -20),
    "action_target_capture_distance": (0x0049651C, 0xE350001E, 30),
    "action_completion_distance": (0x00496558, 0xE3A08014, 20),
}
AGE_PROPERTIES_POINTER_LITERAL = 0x00250AA8
AGE_PROPERTIES_EXPECTED_ADDRESS = 0x0053A2F0
AGE_PROPERTIES_RECORD_STRIDE = 0x134
SURFACE_WALL_FLAGS_POINTER_LITERAL = 0x00496AEC
SURFACE_WALL_FLAGS_EXPECTED_ADDRESS = 0x00514394
SURFACE_WALL_FLAGS_COUNT = 32
SURFACE_CLIMB_SELECTOR_FUNCTION = 0x001CF9AC
SURFACE_CLIMB_ACTION_FUNCTION = 0x004BE20C
SURFACE_CLIMB_ACTION_POINTER_LITERAL = 0x004C11EC
GROUND_CLIMB_ENTRY_FUNCTION = 0x0035150C
GROUND_CLIMB_ACTION_WRAPPER_FUNCTION = 0x004C11A0
GROUND_CLIMB_ACTION_WRAPPER_POINTER_LITERAL = 0x00351868

AGE_PROPERTY_FIELDS = {
    "ceiling_check_height": 0x00,
    "unk_04": 0x04,
    "model_scale": 0x08,
    "ledge_floor_probe_height": 0x0C,
    "unk_10": 0x10,
    "ledge_type_4_minimum_y": 0x14,
    "ledge_type_3_minimum_y": 0x18,
    "ledge_type_2_minimum_y": 0x1C,
    "unk_20": 0x20,
    "unk_24": 0x24,
    "unk_28": 0x28,
    "unk_2c": 0x2C,
    "unk_30": 0x30,
    "unk_34": 0x34,
    "wall_check_radius": 0x38,
    "unk_3c": 0x3C,
    "unk_40": 0x40,
}

FLOAT_LITERALS = {
    "wall_probe_forward_addend": (0x0032F1F4, 10.0),
    "standing_wall_check_height": (0x0032F208, 26.0),
    "normal_scale": (0x0032F62C, 1.0 / 32767.0),
    "ledge_wall_probe_height": (0x0032F630, 18.0),
    "wall_yaw_speed_scale": (0x0032F640, 0.00008),
    "minimum_wall_speed": (0x0032F644, 0.1),
    "minimum_ledge_y": (0x0032F650, 18.0),
    "invalid_ledge_y": (0x0032F654, 399.96002197265625),
    "ceiling_clearance_above_ledge": (0x0032F658, 20.0),
    "upper_wall_probe_above_ledge": (0x0032F65C, 5.0),
}

INTEGER_LITERALS = {
    "wall_normal_y_range_constant": (0x0032F64C, 1198),
    "floor_normal_y_range_constant": (0x0032FA20, 56000),
}

IMMEDIATE_INSTRUCTIONS = {
    "shape_yaw_to_wall_maximum_s16": (0x0032F448, 0x135A0A03, 0x3000),
    "upper_wall_yaw_difference_maximum_s16": (0x0032F5C4, 0xE3500901, 0x4000),
}

SURFACE_CLIMB_FLOAT_LITERALS = {
    "horizontal_input_play_speed_scale": (0x004BE68C, 0.0325),
    "vertical_input_play_speed_scale": (0x004BE690, 0.05),
    "minimum_play_speed": (0x004BE694, 1.0),
    "maximum_play_speed": (0x004BE698, 3.35),
    "top_floor_probe_forward_distance": (0x004BE6A8, 26.0),
    "dismount_play_speed": (0x004BE6CC, 4.0 / 3.0),
    "bottom_dismount_floor_delta": (0x004BEAE0, 15.0),
}

SURFACE_CLIMB_IMMEDIATE_INSTRUCTIONS = {
    "free_climb_wall_flag_mask": (0x001D01F0, 0xE3100008, 0x08),
    "free_climb_start_csab_type_local_index": (0x001D0200, 0xE3A030AD, 0xAD),
}

GROUND_CLIMB_FLOAT_LITERALS = {
    "minimum_y_distance_to_ledge": (0x00351850, 79.0),
    "normal_scale": (0x00351858, 1.0 / 32767.0),
    "polygon_bounds_midpoint_scale": (0x0035185C, 0.5),
    "rung_interval_reciprocal": (0x00351860, 1.0 / 15.0),
    "rung_interval": (0x00351864, 15.0),
    "wall_plane_inset": (0x0035186C, 1.0),
}

GROUND_CLIMB_IMMEDIATE_INSTRUCTIONS = {
    "free_climb_wall_flag_mask": (0x00351568, 0xE2127008, 0x08),
    "free_climb_mode_value": (0x0035156C, 0x13A07002, 0x02),
    "regular_ladder_wall_flag_mask": (0x00351570, 0xE2020002, 0x02),
    "lateral_alignment_maximum_exclusive": (0x003516F8, 0xE3500441, 0x08),
    "regular_climb_start_front_age_field_offset": (0x00351768, 0x05916104, 0x104),
    "initial_phase_magnitude": (0x00351790, 0xE3E01001, 0x02),
}

EXPECTED_WALL_FLAGS = [0, 1, 3, 5, 8, 16, 32, 64] + [0] * 24


def _code_offset(address: int, size: int, code: bytes, source: Path) -> int:
    offset = address - CODE_IMAGE_BASE
    if offset < 0 or offset + size > len(code):
        raise ParseError(
            f"{source}: runtime range 0x{address:08X}+0x{size:X} lies outside code.bin"
        )
    return offset


def _u32(code: bytes, address: int, source: Path) -> int:
    return struct.unpack_from("<I", code, _code_offset(address, 4, code, source))[0]


def _f32(code: bytes, address: int, source: Path) -> float:
    return struct.unpack_from("<f", code, _code_offset(address, 4, code, source))[0]


def _require_equal(actual: int, expected: int, label: str, source: Path) -> None:
    if actual != expected:
        raise ParseError(
            f"{source}: {label} expected 0x{expected:08X}, found 0x{actual:08X}"
        )


def _require_near(actual: float, expected: float, label: str, source: Path) -> None:
    if not math.isfinite(actual) or not math.isclose(actual, expected, rel_tol=1e-6, abs_tol=1e-7):
        raise ParseError(f"{source}: {label} expected {expected}, found {actual}")


def _age_record(code: bytes, source: Path, table_address: int, index: int, age: str) -> dict[str, object]:
    record_address = table_address + index * AGE_PROPERTIES_RECORD_STRIDE
    fields: dict[str, object] = {}
    for name, offset in AGE_PROPERTY_FIELDS.items():
        fields[name] = {
            "offset": offset,
            "runtime_address": record_address + offset,
            "value": _f32(code, record_address + offset, source),
        }
    return {
        "age": age,
        "record_index": index,
        "runtime_address": record_address,
        "fields": fields,
    }


def build_player_collision_action_native_contract(code_bin: Path) -> dict[str, object]:
    code = code_bin.read_bytes()

    start_mode_table_address = _u32(
        code, PLAYER_START_MODE_TABLE_POINTER_LITERAL, code_bin
    )
    _require_equal(
        start_mode_table_address,
        PLAYER_START_MODE_TABLE_EXPECTED_ADDRESS,
        "player start-mode table pointer",
        code_bin,
    )
    start_mode_functions = [
        _u32(code, start_mode_table_address + index * 4, code_bin)
        for index in range(PLAYER_START_MODE_TABLE_COUNT)
    ]
    for name, (index, expected_function) in SCENE_ENTRANCE_START_MODES.items():
        _require_equal(
            start_mode_functions[index],
            expected_function,
            f"scene-entrance start-mode function {name}",
            code_bin,
        )

    start_mode_selector: dict[str, object] = {}
    for name, (address, expected_word, value) in PLAYER_START_MODE_SELECTOR_INSTRUCTIONS.items():
        instruction_word = _u32(code, address, code_bin)
        _require_equal(instruction_word, expected_word, name, code_bin)
        start_mode_selector[name] = {
            "instruction_address": address,
            "instruction_word": instruction_word,
            "value": value,
        }

    scene_entrance_action_address = _u32(
        code, SCENE_ENTRANCE_ACTION_POINTER_LITERAL, code_bin
    )
    _require_equal(
        scene_entrance_action_address,
        SCENE_ENTRANCE_ACTION_FUNCTION,
        "scene-entrance action pointer",
        code_bin,
    )
    scene_entrance_float_literals: dict[str, object] = {}
    for name, (address, expected) in SCENE_ENTRANCE_FLOAT_LITERALS.items():
        value = _f32(code, address, code_bin)
        _require_near(value, expected, name, code_bin)
        scene_entrance_float_literals[name] = {
            "runtime_address": address,
            "raw_u32": _u32(code, address, code_bin),
            "value": value,
        }
    scene_entrance_immediates: dict[str, object] = {}
    for name, (address, expected_word, value) in SCENE_ENTRANCE_IMMEDIATE_INSTRUCTIONS.items():
        instruction_word = _u32(code, address, code_bin)
        _require_equal(instruction_word, expected_word, name, code_bin)
        scene_entrance_immediates[name] = {
            "instruction_address": address,
            "instruction_word": instruction_word,
            "value": value,
        }

    age_table_address = _u32(code, AGE_PROPERTIES_POINTER_LITERAL, code_bin)
    _require_equal(
        age_table_address,
        AGE_PROPERTIES_EXPECTED_ADDRESS,
        "player age-properties table pointer",
        code_bin,
    )
    _code_offset(
        age_table_address,
        AGE_PROPERTIES_RECORD_STRIDE * 2,
        code,
        code_bin,
    )

    wall_flags_address = _u32(code, SURFACE_WALL_FLAGS_POINTER_LITERAL, code_bin)
    _require_equal(
        wall_flags_address,
        SURFACE_WALL_FLAGS_EXPECTED_ADDRESS,
        "surface wall-flags table pointer",
        code_bin,
    )
    wall_flags = [
        _u32(code, wall_flags_address + index * 4, code_bin)
        for index in range(SURFACE_WALL_FLAGS_COUNT)
    ]
    if wall_flags != EXPECTED_WALL_FLAGS:
        raise ParseError(f"{code_bin}: native surface wall-flags table is not the decoded revision")

    float_literals: dict[str, object] = {}
    for name, (address, expected) in FLOAT_LITERALS.items():
        value = _f32(code, address, code_bin)
        _require_near(value, expected, name, code_bin)
        float_literals[name] = {
            "runtime_address": address,
            "raw_u32": _u32(code, address, code_bin),
            "value": value,
        }

    integer_literals: dict[str, object] = {}
    for name, (address, expected) in INTEGER_LITERALS.items():
        value = _u32(code, address, code_bin)
        _require_equal(value, expected, name, code_bin)
        integer_literals[name] = {
            "runtime_address": address,
            "value": value,
        }

    immediate_values: dict[str, object] = {}
    for name, (address, expected_word, value) in IMMEDIATE_INSTRUCTIONS.items():
        instruction_word = _u32(code, address, code_bin)
        _require_equal(instruction_word, expected_word, name, code_bin)
        immediate_values[name] = {
            "instruction_address": address,
            "instruction_word": instruction_word,
            "value": value,
        }

    surface_climb_action_address = _u32(
        code, SURFACE_CLIMB_ACTION_POINTER_LITERAL, code_bin
    )
    _require_equal(
        surface_climb_action_address,
        SURFACE_CLIMB_ACTION_FUNCTION,
        "surface-climb action pointer",
        code_bin,
    )
    surface_climb_float_literals: dict[str, object] = {}
    for name, (address, expected) in SURFACE_CLIMB_FLOAT_LITERALS.items():
        value = _f32(code, address, code_bin)
        _require_near(value, expected, name, code_bin)
        surface_climb_float_literals[name] = {
            "runtime_address": address,
            "raw_u32": _u32(code, address, code_bin),
            "value": value,
        }
    surface_climb_immediates: dict[str, object] = {}
    for name, (address, expected_word, value) in SURFACE_CLIMB_IMMEDIATE_INSTRUCTIONS.items():
        instruction_word = _u32(code, address, code_bin)
        _require_equal(instruction_word, expected_word, name, code_bin)
        surface_climb_immediates[name] = {
            "instruction_address": address,
            "instruction_word": instruction_word,
            "value": value,
        }

    ground_climb_action_wrapper = _u32(
        code, GROUND_CLIMB_ACTION_WRAPPER_POINTER_LITERAL, code_bin
    )
    _require_equal(
        ground_climb_action_wrapper,
        GROUND_CLIMB_ACTION_WRAPPER_FUNCTION,
        "ground-climb action wrapper pointer",
        code_bin,
    )
    ground_climb_float_literals: dict[str, object] = {}
    for name, (address, expected) in GROUND_CLIMB_FLOAT_LITERALS.items():
        value = _f32(code, address, code_bin)
        _require_near(value, expected, name, code_bin)
        ground_climb_float_literals[name] = {
            "runtime_address": address,
            "raw_u32": _u32(code, address, code_bin),
            "value": value,
        }
    ground_climb_immediates: dict[str, object] = {}
    for name, (address, expected_word, value) in GROUND_CLIMB_IMMEDIATE_INSTRUCTIONS.items():
        instruction_word = _u32(code, address, code_bin)
        _require_equal(instruction_word, expected_word, name, code_bin)
        ground_climb_immediates[name] = {
            "instruction_address": address,
            "instruction_word": instruction_word,
            "value": value,
        }

    return {
        "format": FORMAT,
        "status": "ready",
        "source": {
            "code_bin": str(code_bin),
            "code_sha256": hashlib.sha256(code).hexdigest(),
            "image_base": CODE_IMAGE_BASE,
        },
        "native_function": {
            "name": "oot3d_player_process_scene_collision",
            "runtime_address": PLAYER_COLLISION_FUNCTION,
        },
        "player_action_state_machine": {
            "player_init_function_address": PLAYER_INIT_FUNCTION,
            "start_mode_selector": start_mode_selector,
            "start_mode_table": {
                "pointer_literal_address": PLAYER_START_MODE_TABLE_POINTER_LITERAL,
                "runtime_address": start_mode_table_address,
                "entry_count": PLAYER_START_MODE_TABLE_COUNT,
                "function_addresses": start_mode_functions,
            },
            "scene_entrance": {
                "setup_function_address": SCENE_ENTRANCE_SETUP_FUNCTION,
                "action_pointer_literal_address": SCENE_ENTRANCE_ACTION_POINTER_LITERAL,
                "action_function_address": scene_entrance_action_address,
                "player_field_offsets": {
                    "linear_velocity": 0x221C,
                    "action_phase": 0x2237,
                    "action_timer": 0x2238,
                    "delay_timer": 0x224A,
                    "target_x": 0x12C8,
                    "target_z": 0x12D0,
                    "state_flags_1": 0x1710,
                },
                "start_modes": {
                    "idle": {
                        "index": SCENE_ENTRANCE_START_MODES["idle"][0],
                        "function_address": SCENE_ENTRANCE_START_MODES["idle"][1],
                        "initial_speed_source": "incoming_runtime_linear_velocity",
                        "target_distance": scene_entrance_float_literals["idle_target_distance"],
                        "timer": scene_entrance_immediates["idle_timer"],
                    },
                    "move_forward_slow": {
                        "index": SCENE_ENTRANCE_START_MODES["move_forward_slow"][0],
                        "function_address": SCENE_ENTRANCE_START_MODES["move_forward_slow"][1],
                        "initial_speed": scene_entrance_float_literals["slow_speed"],
                        "target_distance": scene_entrance_float_literals["slow_target_distance"],
                        "timer": scene_entrance_immediates["slow_timer"],
                    },
                    "move_forward": {
                        "index": SCENE_ENTRANCE_START_MODES["move_forward"][0],
                        "function_address": SCENE_ENTRANCE_START_MODES["move_forward"][1],
                        "minimum_initial_speed": scene_entrance_float_literals[
                            "forward_minimum_speed"
                        ],
                        "target_distance": scene_entrance_float_literals[
                            "forward_target_distance"
                        ],
                        "timer_numerator": scene_entrance_float_literals[
                            "forward_timer_numerator"
                        ],
                        "minimum_timer": scene_entrance_immediates[
                            "forward_timer_clamp_value"
                        ],
                    },
                },
                "action_float_literals": {
                    key: scene_entrance_float_literals[key]
                    for key in (
                        "action_initial_linear_speed",
                        "action_default_target_speed",
                        "action_floor_probe_y_offset",
                    )
                },
                "action_immediate_values": {
                    key: scene_entrance_immediates[key]
                    for key in (
                        "action_target_capture_distance",
                        "action_completion_distance",
                    )
                },
                "runtime_scope": (
                    "code.bin-backed grounded scene-entrance movement path; water transition, "
                    "camera handoff, respawn write, and special floor transition remain separate "
                    "native subsystems"
                ),
            },
        },
        "age_properties": {
            "pointer_literal_address": AGE_PROPERTIES_POINTER_LITERAL,
            "runtime_address": age_table_address,
            "record_stride": AGE_PROPERTIES_RECORD_STRIDE,
            "records": [
                _age_record(code, code_bin, age_table_address, 0, "adult"),
                _age_record(code, code_bin, age_table_address, 1, "child"),
            ],
        },
        "surface_wall_flags": {
            "helper_flag_0_address": 0x00496AC8,
            "helper_flag_1_address": 0x00496A9C,
            "pointer_literal_address": SURFACE_WALL_FLAGS_POINTER_LITERAL,
            "runtime_address": wall_flags_address,
            "entry_count": SURFACE_WALL_FLAGS_COUNT,
            "surface_data_word": "Data0_in_code_bin_Data1_in_native_ZSI_decoder",
            "behavior_index_shift": 21,
            "behavior_index_mask": 0x1F,
            "values": wall_flags,
        },
        "ledge_detector": {
            "float_literals": float_literals,
            "integer_literals": integer_literals,
            "immediate_values": immediate_values,
            "derived": {
                "wall_normal_y_abs_maximum_exclusive": 600,
                "floor_normal_y_abs_minimum_exclusive": 28000,
                "current_wall_reject_flag_mask": 1,
                "upper_wall_clearance_flag_mask": 2,
                "minimum_climb_type_for_falling_grab": 2,
            },
        },
        "surface_climb": {
            "selector_function_address": SURFACE_CLIMB_SELECTOR_FUNCTION,
            "action_pointer_literal_address": SURFACE_CLIMB_ACTION_POINTER_LITERAL,
            "action_function_address": surface_climb_action_address,
            "float_literals": surface_climb_float_literals,
            "immediate_values": surface_climb_immediates,
            "child_top_reach_age_field": "unk_40",
            "runtime_policy": (
                "native wall flag selects free-climb; native action scales and clamps "
                "signed stick input, alternates native CSABs, and consumes CSAB root motion"
            ),
        },
        "ground_climb_entry": {
            "function_address": GROUND_CLIMB_ENTRY_FUNCTION,
            "action_wrapper_pointer_literal_address": GROUND_CLIMB_ACTION_WRAPPER_POINTER_LITERAL,
            "action_wrapper_function_address": ground_climb_action_wrapper,
            "float_literals": ground_climb_float_literals,
            "immediate_values": ground_climb_immediates,
            "regular_ladder_mode_value": 0,
            "initial_phase": -2,
            "runtime_policy": (
                "native ZSI wall flags select free-climb or regular ladder; regular ladders "
                "derive center, vertical bounds, lateral alignment, rung phase, and wall-plane "
                "placement from the touched native collision polygon"
            ),
        },
        "action_transition_policy": {
            "collision_and_asset_authority": "OOT3D code.bin detector, native ZSI collision, and native CSABs",
            "transition_scaffold": "N64 Player falling-grab action topology pending exact OOT3D action-selector closure",
            "falling_grab_requires_descending": True,
            "falling_grab_requires_forward_speed": True,
            "falling_grab_maximum_fall_distance": 150.0,
            "falling_grab_minimum_ledge_above_floor_age_field": "unk_34",
            "hang_wall_plane_offset": 1.0,
            "climb_input_minimum_wallward_dot_exclusive": 0.0,
        },
    }
