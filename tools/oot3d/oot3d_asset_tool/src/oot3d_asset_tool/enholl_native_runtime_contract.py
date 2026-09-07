from __future__ import annotations

import hashlib
from pathlib import Path

from .binary import BinaryView, ParseError
from .native_actor_contract_common import actor_profile, code_offset


FORMAT = "oot3d_enholl_native_runtime_contract_v1"
RESOURCE = "oot3d/catalog/contracts/oot3d-enholl-native-runtime.json"

ENHOLL_ACTOR_ID = 0x0023
ACTION_TABLE_POINTER_LITERAL = 0x001B3D5C
NEXT_ACTION_TABLE_POINTER_LITERAL = 0x001CE764
ACTION_TABLE_ENTRY_COUNT = 7
ACTION_SELECTOR_SHIFT = 6
ACTION_SELECTOR_MASK = 0x7
TRANSITION_INDEX_SHIFT = 10

LOCAL_COORDINATE_CALLSITE = 0x001EACB8
ROOM_REQUEST_CALLSITE = 0x001EADBC
ROOM_COMMIT_CALLSITE = 0x001CE700
NEXT_ACTION_POINTER_LITERAL = 0x001EAE08
ROOM_REQUEST_HANDLER = 0x001EAC68

DEFAULT_HALF_WIDTH_LITERAL = 0x001EADE8
NARROW_HALF_WIDTH_LITERAL = 0x001EADEC
VERTICAL_MIN_LITERAL = 0x001EADF8

EXPECTED_PROFILE = {
    "category": 10,
    "flags": 0x00000010,
    "object_id": 1,
    "instance_size": 0x01B4,
    "init_address": 0x001B3CEC,
    "destroy_address": 0x001B3D60,
    "update_address": 0x001F692C,
    "draw_address": 0x001F6614,
}


def _arm_bl_target(view: BinaryView, address: int) -> int:
    word = view.u32(code_offset(address, 4, view))
    if word >> 28 != 0xE or (word >> 24) & 0xF != 0xB:
        raise ParseError(
            f"{view.source}: expected ARM BL at 0x{address:08X}"
        )
    displacement = (word & 0x00FFFFFF) << 2
    if displacement & 0x02000000:
        displacement -= 0x04000000
    return (address + 8 + displacement) & 0xFFFFFFFF


def _require_word(view: BinaryView, address: int, expected: int) -> None:
    actual = view.u32(code_offset(address, 4, view))
    if actual != expected:
        raise ParseError(
            f"{view.source}: EnHoll instruction changed at 0x{address:08X}: "
            f"0x{actual:08X} != 0x{expected:08X}"
        )


def build_enholl_native_runtime_contract(code_bin: Path) -> dict[str, object]:
    code = code_bin.read_bytes()
    view = BinaryView(code, str(code_bin))
    profile = actor_profile(view, ENHOLL_ACTOR_ID)
    for field, expected in EXPECTED_PROFILE.items():
        if profile[field] != expected:
            raise ParseError(
                f"{view.source}: EnHoll ActorInit {field} changed: "
                f"0x{profile[field]:X} != 0x{expected:X}"
            )

    # These two instruction pairs are the native `(params >> 6) & 7`
    # selector in Init and NextAction. Keeping the extraction tied to the
    # instructions prevents a plausible but unrelated table from being packed.
    for first, second, second_word in (
        (0x001B3D28, 0x001B3D2C, 0xE1B01EA0),
        (0x001CE728, 0x001CE72C, 0xE1B00EA0),
    ):
        _require_word(view, first, 0xE1A00B80)
        _require_word(view, second, second_word)
    _require_word(view, 0x001B3D74, 0xE1A02522)

    action_table = view.u32(
        code_offset(ACTION_TABLE_POINTER_LITERAL, 4, view)
    )
    next_action_table = view.u32(
        code_offset(NEXT_ACTION_TABLE_POINTER_LITERAL, 4, view)
    )
    if next_action_table != action_table:
        raise ParseError(f"{view.source}: EnHoll action tables disagree")
    action_handlers = [
        view.u32(code_offset(action_table + index * 4, 4, view))
        for index in range(ACTION_TABLE_ENTRY_COUNT)
    ]
    if action_handlers[4] != ROOM_REQUEST_HANDLER or action_handlers[6] != ROOM_REQUEST_HANDLER:
        raise ParseError(
            f"{view.source}: EnHoll room-request modes no longer share "
            f"0x{ROOM_REQUEST_HANDLER:08X}"
        )

    local_coordinate_helper = _arm_bl_target(view, LOCAL_COORDINATE_CALLSITE)
    room_request_function = _arm_bl_target(view, ROOM_REQUEST_CALLSITE)
    room_commit_function = _arm_bl_target(view, ROOM_COMMIT_CALLSITE)
    next_action = view.u32(code_offset(NEXT_ACTION_POINTER_LITERAL, 4, view))

    default_half_width = view.f32(
        code_offset(DEFAULT_HALF_WIDTH_LITERAL, 4, view)
    )
    narrow_half_width = view.f32(
        code_offset(NARROW_HALF_WIDTH_LITERAL, 4, view)
    )
    vertical_min = view.f32(code_offset(VERTICAL_MIN_LITERAL, 4, view))
    vertical_max = 200.0
    absolute_depth_min = 50.0
    absolute_depth_max = 100.0
    if (
        default_half_width != 200.0
        or narrow_half_width != 100.0
        or vertical_min != -50.0
    ):
        raise ParseError(f"{view.source}: EnHoll mode 4/6 trigger literals changed")

    return {
        "format": FORMAT,
        "status": "room_request_modes_4_6_complete",
        "source": {
            "kind": "oot3d_code_bin",
            "code_bin_path": str(code_bin),
            "code_bin_sha256": hashlib.sha256(code).hexdigest(),
        },
        "actor_profile": profile,
        "parameter_layout": {
            "action_selector_shift": ACTION_SELECTOR_SHIFT,
            "action_selector_mask": ACTION_SELECTOR_MASK,
            "transition_index_shift": TRANSITION_INDEX_SHIFT,
            "source": "EnHoll_Init@0x001B3D24 and EnHoll_Destroy@0x001B3D64",
        },
        "action_table": {
            "pointer_literal_address": ACTION_TABLE_POINTER_LITERAL,
            "address": action_table,
            "entry_count": ACTION_TABLE_ENTRY_COUNT,
            "handlers": action_handlers,
            "invalid_selector": ACTION_TABLE_ENTRY_COUNT,
        },
        "room_request_trigger": {
            "handler_address": ROOM_REQUEST_HANDLER,
            "supported_modes": [4, 6],
            "narrow_mode": 6,
            "local_coordinate_helper_address": local_coordinate_helper,
            "room_request_function_address": room_request_function,
            "room_commit_function_address": room_commit_function,
            "next_action_address": next_action,
            "focus_source": "view_eye_when_debug_camera_or_cutscene_else_player_world_position",
            "side_rule": "local_z_negative_front_else_back",
            "bounds": {
                "vertical_min": vertical_min,
                "vertical_max": vertical_max,
                "absolute_depth_min": absolute_depth_min,
                "absolute_depth_max": absolute_depth_max,
                "default_half_width": default_half_width,
                "narrow_half_width": narrow_half_width,
                "strict_comparisons": True,
            },
            "special_bypass": {
                "entrance_value": 0xEE,
                "scene_setup_index": 8,
                "status": "native_runtime_state_input_required",
            },
        },
        "behavior_coverage": {
            "modes_4_6_room_request": "complete",
            "request_completion_and_room_commit": "complete",
            "destroy_spawn_marker_restore": "complete",
            "mode_0_horizontal_fade_plane": "pending_native_actor_runtime",
            "modes_1_2_3_5_vertical_and_switch_planes": "pending_native_actor_runtime",
            "drawn_mode_0_plane": "pending_native_actor_render",
        },
    }
