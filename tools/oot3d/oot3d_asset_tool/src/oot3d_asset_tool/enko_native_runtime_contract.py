from __future__ import annotations

import hashlib
import math
from pathlib import Path
from typing import Any

from .binary import BinaryView, ParseError
from .native_actor_contract_common import (
    NATIVE_RANDOM_INCREMENT_ADDRESS,
    NATIVE_RANDOM_MULTIPLIER_ADDRESS,
    NATIVE_RANDOM_STATE_POINTER_LITERAL,
    native_random_zero_one_contract,
)
from .zar import ZarArchive, ZarFile


FORMAT = "oot3d_enko_native_runtime_contract_v1"
RESOURCE = "oot3d/catalog/contracts/oot3d-enko-native-runtime.json"

CODE_IMAGE_BASE = 0x00100000
ANIMATION_SOURCE_TABLE = 0x0052B7E4
HEAD_TABLE = 0x0052B89C
MODEL_CLASS_TABLE = 0x0052B8CC
ANIMATION_TABLE = 0x0052B8DC
QUEST_ANIMATION_LOOKUP = 0x0052BC24
MODEL_INFO_TABLE = 0x0052BC65
OBJECT_TABLE = 0x0053CCF4
MODEL_SCALE_LITERAL = 0x003ACD70
ENKO_CONSTRUCTOR = 0x0041244C
ENKO_OVERRIDE_LIMB_DRAW = 0x002335B4
ENKO_OVERRIDE_LIMB_DRAW_SIZE = 0x60
ENKO_TORSO_LIMB_COMPARE = 0x002335B8
ENKO_HEAD_LIMB_COMPARE = 0x002335E8
ENKO_TORSO_STATE_BASE_ADD = 0x002335C8
ENKO_TORSO_STATE_OFFSET_ADD = 0x002335CC
ENKO_HEAD_STATE_WORD0_LOAD = 0x002335F0
ENKO_HEAD_STATE_WORD1_LOAD = 0x002335F4
ENKO_BLINK_SEQUENCE_POINTER_LITERAL = 0x001B5F04
ENKO_BLINK_INDEX_OFFSET_LITERAL = 0x001B5F08
ENKO_BLINK_SEQUENCE_LENGTH_COMPARE = 0x001B6054
ENKO_BLINK_TIMER_BASE_MOVE = 0x001B6068
ENKO_BLINK_TIMER_RANGE_COPY = 0x001B606C
ACTOR_OVERLAY_TABLE_POINTER_LITERAL = 0x00373B64
ACTOR_OVERLAY_ENTRY_STRIDE = 0x20
ACTOR_OVERLAY_PROFILE_POINTER_OFFSET = 0x14
ENKO_ACTOR_ID = 0x0163
NPC_TRACKING_SERVICE = 0x0034C664
NPC_TRACKING_SERVICE_SIZE = 0x2C0
NPC_TRACKING_PRESET_POINTER_LITERAL = 0x0034C924
NPC_TRACKING_SIGNED_GUARD_LITERAL = 0x0034C928
NPC_TRACKING_PRESET_TABLE_END = 0x0050CCF4
NPC_TRACKING_PRESET_STRIDE = 0x18
NPC_TRACKING_SELECTOR = 0x0035CF48
NPC_TRACKING_SELECTOR_SIZE = 0x174
NPC_TRACKING_FACING_THRESHOLD_LITERAL = 0x0034C994
NPC_TRACKING_TARGET_HEIGHT_POINTER_LITERAL = 0x001B64BC
NPC_TRACKING_TARGET_HEIGHT_FALLBACK_LITERAL = 0x001B64C0
NPC_TRACKING_TARGET_HEIGHT_COPY_SIZE_INSTRUCTION = 0x001B6344
NPC_TRACKING_INTERACT_INFO_OFFSET = 0x028C
NPC_TRACKING_INTERACT_INFO_SIZE = 0x28
NATIVE_GLOBAL_CONTEXT_POINTER_ADDRESS = 0x0051B2F4
NATIVE_GLOBAL_CONTEXT_MINIMUM_SIZE = 0x112
NATIVE_UPDATE_RATE_S16_OFFSET = 0x110
NATIVE_UPDATE_RATE_INITIALIZER_ADDRESS = 0x00416FF0

NPC_TRACKING_SMOOTH_CALLS = (
    0x0034C7B8,
    0x0034C83C,
    0x0034C8A4,
    0x0034C8D8,
    0x0034C914,
)
NPC_TRACKING_SMOOTH_LANES = (
    (0x0034C7B8, 0x0034C7A0, 0x0034C7AC, 0x0034C7B0),
    (0x0034C83C, 0x0034C824, 0x0034C830, 0x0034C834),
    (0x0034C8A4, 0x0034C88C, 0x0034C898, 0x0034C89C),
    (0x0034C8D8, 0x0034C8C4, 0x0034C8CC, 0x0034C8D0),
    (0x0034C914, 0x0034C900, 0x0034C908, 0x0034C90C),
)

# These words are the reviewed native call lanes used to classify the initial
# EnKo tracking routes. Keeping the machine words beside the semantic result
# makes a changed code revision fail instead of silently reusing the mapping.
ENKO_TRACKING_ROUTE_WORDS = {
    0x00172050: 0xE3A03002,
    0x00172074: 0xE3A03001,
    0x00172078: 0xE3A02002,
    0x00172084: 0xEB076976,
    0x0017213C: 0xE3A03002,
    0x00172154: 0xE3A03001,
    0x00172158: 0xE3A02005,
    0x00172164: 0xEB07693E,
    0x00172278: 0xE3A03002,
    0x00172280: 0xE3A03001,
    0x00172284: 0xE3A02002,
    0x00172290: 0xEB0768F3,
    0x00172A20: 0xE3A03004,
    0x00172A24: 0xE3A02002,
    0x00172A30: 0xEB07670B,
    0x00172C5C: 0x13A05001,
    0x00172C60: 0x03A05002,
    0x00172C84: 0xE3A05001,
    0x00172C94: 0xE1A03005,
    0x00172C98: 0xE3A02005,
    0x00172CA4: 0xEB07666E,
}

# Npc_UpdateTrackingByPreset copies one preset locally, then masks its six
# head/torso limits and actor-rotation byte according to the selector mode.
# These words are the complete mode branch, so the semantic table below cannot
# silently survive a changed native implementation.
NPC_TRACKING_MODE_WORDS = {
    0x0034C6AC: 0xE1D400F2,
    0x0034C6B0: 0xE3500001,
    0x0034C6B4: 0x01CD11B4,
    0x0034C6B8: 0x01CD11B8,
    0x0034C6BC: 0x01CD11B6,
    0x0034C6C0: 0x0A000003,
    0x0034C6C4: 0xE3500002,
    0x0034C6C8: 0x0A000004,
    0x0034C6CC: 0xE3500003,
    0x0034C6D0: 0x1A000003,
    0x0034C6D4: 0xE1CD11BE,
    0x0034C6D8: 0xE1CD11BA,
    0x0034C6DC: 0xE1CD11BC,
    0x0034C6E0: 0xE5CD1020,
}

# The fourth ARM argument is returned unchanged when nonzero. All recovered
# initial-child EnKo routes use that native forced-mode path.
NPC_TRACKING_SELECTOR_FORCED_MODE_WORDS = {
    0x0035CF54: 0xE1B00003,
    0x0035CF60: 0x1A000039,
    0x0035D04C: 0xE8BD81F0,
}

NPC_TRACKING_LAYOUT_WORDS = {
    0x0034C698: 0xE1C400B2,
    0x0034C704: 0xED960A0B,
    0x0034C708: 0xEDD40A05,
    0x0034C70C: 0xED968A0A,
    0x0034C710: 0xEDD68A0C,
    0x0034C718: 0xED940A06,
    0x0034C71C: 0xEDD41A07,
    0x0034C728: 0xED940A08,
    0x0034C77C: 0xE1D61BBE,
    0x0034C7B4: 0xE284000A,
    0x0034C838: 0xE2840010,
    0x0034C8A0: 0xE28600BE,
    0x0034C8D4: 0xE2840008,
    0x0034C910: 0xE284000E,
    0x0035CF64: 0xE1D400B0,
    0x0035CFB0: 0xC1C470B4,
    0x0035CFB4: 0xC1C470B6,
    0x0035D028: 0xE1D400F4,
    0x0035D050: 0xE1D400F6,
    0x0035D078: 0xE1C400B4,
    0x0035D07C: 0xE1D400B6,
    0x0035D09C: 0xE1C400B4,
    0x0035D0A0: 0xE1D400B6,
    0x00375A60: 0xE5900000,
    0x00375A64: 0xE2800C01,
    0x00375A7C: 0xE1D001F0,
    0x00416FF0: 0xE3A00002,
    0x00416FEC: 0xE59F101C,
    0x00416FF8: 0xE5911000,
    0x00416FFC: 0xE2811C01,
    0x00417000: 0xE1C101B0,
}

ANIMATION_COUNT = 35
ANIMATION_SOURCE_COUNT = 29
HEAD_CLASS_COUNT = 3
MODEL_CLASS_COUNT = 2
SUBTYPE_COUNT = 13
QUEST_STATE_COUNT = 5

QUEST_STATE_SEMANTICS = (
    {
        "index": 0,
        "semantic": "child_start",
        "conditions": {"player.age": "child", "quest.kokiri_emerald": "false",
                       "event.zeldas_letter": "false"},
    },
    {
        "index": 1,
        "semantic": "child_stone",
        "conditions": {"player.age": "child", "quest.kokiri_emerald": "true",
                       "event.zeldas_letter": "false"},
    },
    {
        "index": 2,
        "semantic": "child_saria",
        "conditions": {"player.age": "child", "event.zeldas_letter": "true"},
    },
    {
        "index": 3,
        "semantic": "adult_enemy",
        "conditions": {"player.age": "adult", "quest.forest_medallion": "false"},
    },
    {
        "index": 4,
        "semantic": "adult_saved",
        "conditions": {"player.age": "adult", "quest.forest_medallion": "true"},
    },
)

# FUN_0041244C copies these source-table slots into AnimationInfo entries 0..33.
# Entry 34 retains its initialized type-local CSAB index from code.bin.
ANIMATION_SOURCE_SLOT_BY_SEMANTIC = (
    1, 0, 2, 3, 4, 5, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 0, 18, 10, 28, 7,
)


def _offset(address: int, size: int, view: BinaryView) -> int:
    offset = address - CODE_IMAGE_BASE
    view.require(offset, size)
    return offset


def _arm_cmp_r1_immediate(view: BinaryView, address: int) -> int:
    word = view.u32(_offset(address, 4, view))
    if word & 0x0FFFF000 != 0x03510000:
        raise ParseError(
            f"{view.source}: expected ARM CMP r1, immediate at 0x{address:08X}"
        )
    immediate = word & 0xFF
    rotation = ((word >> 8) & 0xF) * 2
    if rotation:
        immediate = (
            (immediate >> rotation) | (immediate << (32 - rotation))
        ) & 0xFFFFFFFF
    return immediate


def _arm_data_processing_immediate(
    view: BinaryView, address: int, opcode: int, destination_register: int
) -> int:
    word = view.u32(_offset(address, 4, view))
    if (
        word >> 28 != 0xE
        or (word >> 25) & 1 != 1
        or (word >> 21) & 0xF != opcode
        or (word >> 12) & 0xF != destination_register
    ):
        raise ParseError(
            f"{view.source}: unexpected ARM immediate instruction at 0x{address:08X}"
        )
    immediate = word & 0xFF
    rotation = ((word >> 8) & 0xF) * 2
    if rotation:
        immediate = (
            (immediate >> rotation) | (immediate << (32 - rotation))
        ) & 0xFFFFFFFF
    return immediate


def _enko_face_runtime(view: BinaryView) -> dict[str, object]:
    sequence_address = view.u32(
        _offset(ENKO_BLINK_SEQUENCE_POINTER_LITERAL, 4, view)
    )
    blink_index_offset = view.u32(
        _offset(ENKO_BLINK_INDEX_OFFSET_LITERAL, 4, view)
    )
    sequence_length = _arm_data_processing_immediate(
        view, ENKO_BLINK_SEQUENCE_LENGTH_COMPARE, 0xA, 0
    )
    timer_base = _arm_data_processing_immediate(
        view, ENKO_BLINK_TIMER_BASE_MOVE, 0xD, 1
    )
    copy_word = view.u32(_offset(ENKO_BLINK_TIMER_RANGE_COPY, 4, view))
    if copy_word != 0xE1A00001:
        raise ParseError(
            f"{view.source}: EnKo blink timer range is no longer copied from its base"
        )
    if blink_index_offset < 2 or sequence_length == 0:
        raise ParseError(f"{view.source}: invalid EnKo blink state layout")
    sequence_offset = _offset(sequence_address, sequence_length * 4, view)
    sequence = [view.u32(sequence_offset + index * 4) for index in range(sequence_length)]
    if any(value > 0xFF for value in sequence):
        raise ParseError(f"{view.source}: invalid EnKo face-animation frame")

    return {
        "face_animation_set_offset": 0x0ACC,
        "material_animation_channel": 0,
        "blink_timer_offset": blink_index_offset - 2,
        "blink_index_offset": blink_index_offset,
        "blink_sequence_address": sequence_address,
        "blink_sequence": sequence,
        "blink_timer_base": timer_base,
        "blink_timer_range": timer_base,
        "random": native_random_zero_one_contract(view),
    }


def _sha256_address_range(view: BinaryView, address: int, size: int) -> str:
    return hashlib.sha256(view.bytes(_offset(address, size, view), size)).hexdigest()


def _npc_tracking_runtime(view: BinaryView) -> dict[str, object]:
    preset_table = view.u32(
        _offset(NPC_TRACKING_PRESET_POINTER_LITERAL, 4, view)
    )
    preset_bytes = NPC_TRACKING_PRESET_TABLE_END - preset_table
    if preset_bytes <= 0 or preset_bytes % NPC_TRACKING_PRESET_STRIDE != 0:
        raise ParseError(f"{view.source}: invalid NPC tracking preset table bounds")
    preset_count = preset_bytes // NPC_TRACKING_PRESET_STRIDE
    signed_guard = view.u32(
        _offset(NPC_TRACKING_SIGNED_GUARD_LITERAL, 4, view)
    )
    if signed_guard != 0xFFFE:
        raise ParseError(f"{view.source}: NPC tracking signed-angle guard changed")

    presets: list[dict[str, object]] = []
    for index in range(preset_count):
        address = preset_table + index * NPC_TRACKING_PRESET_STRIDE
        offset = _offset(address, NPC_TRACKING_PRESET_STRIDE, view)
        distance = view.f32(offset + 0x10)
        if not math.isfinite(distance) or distance < 0.0:
            raise ParseError(f"{view.source}: invalid NPC tracking preset distance")
        presets.append({
            "index": index,
            "address": address,
            "head_yaw_limit": view.s16(offset + 0x00),
            "head_pitch_min": view.s16(offset + 0x02),
            "head_pitch_max": view.s16(offset + 0x04),
            "torso_yaw_limit": view.s16(offset + 0x06),
            "torso_pitch_min": view.s16(offset + 0x08),
            "torso_pitch_max": view.s16(offset + 0x0A),
            "rotate_actor": view.u8(offset + 0x0C) != 0,
            "auto_turn_distance": distance,
            "auto_turn_yaw_threshold": view.s16(offset + 0x14),
            "raw_sha256": hashlib.sha256(
                view.bytes(offset, NPC_TRACKING_PRESET_STRIDE)
            ).hexdigest(),
        })

    smooth_scale = None
    smooth_max_step = None
    smooth_min_step = None
    for call, min_address, max_address, scale_address in NPC_TRACKING_SMOOTH_LANES:
        min_step = _arm_data_processing_immediate(view, min_address, 0xD, 3)
        max_step = _arm_data_processing_immediate(view, max_address, 0xD, 3)
        scale = _arm_data_processing_immediate(view, scale_address, 0xD, 2)
        if smooth_scale is None:
            smooth_scale = scale
            smooth_max_step = max_step
            smooth_min_step = min_step
        elif (scale, max_step, min_step) != (
            smooth_scale,
            smooth_max_step,
            smooth_min_step,
        ):
            raise ParseError(f"{view.source}: NPC tracking smoothing lanes disagree")

    for address, expected in ENKO_TRACKING_ROUTE_WORDS.items():
        actual = view.u32(_offset(address, 4, view))
        if actual != expected:
            raise ParseError(
                f"{view.source}: EnKo tracking route changed at 0x{address:08X}"
            )
    for address, expected in {
        **NPC_TRACKING_MODE_WORDS,
        **NPC_TRACKING_SELECTOR_FORCED_MODE_WORDS,
    }.items():
        actual = view.u32(_offset(address, 4, view))
        if actual != expected:
            raise ParseError(
                f"{view.source}: NPC tracking mode decode changed at "
                f"0x{address:08X}"
            )
    for address, expected in NPC_TRACKING_LAYOUT_WORDS.items():
        actual = view.u32(_offset(address, 4, view))
        if actual != expected:
            raise ParseError(
                f"{view.source}: NPC tracking structure layout changed at "
                f"0x{address:08X}"
            )

    update_rate = _arm_data_processing_immediate(
        view, NATIVE_UPDATE_RATE_INITIALIZER_ADDRESS, 0xD, 0
    )
    if update_rate <= 0:
        raise ParseError(f"{view.source}: invalid native update rate")

    target_height_table = view.u32(
        _offset(NPC_TRACKING_TARGET_HEIGHT_POINTER_LITERAL, 4, view)
    )
    target_height_bytes = SUBTYPE_COUNT * QUEST_STATE_COUNT * 4
    copied_size = _arm_data_processing_immediate(
        view, NPC_TRACKING_TARGET_HEIGHT_COPY_SIZE_INSTRUCTION, 0xD, 2
    )
    if copied_size != target_height_bytes:
        raise ParseError(f"{view.source}: EnKo tracking-height table extent changed")
    target_height_offset = _offset(target_height_table, target_height_bytes, view)
    target_heights = [
        [
            view.f32(
                target_height_offset +
                (subtype * QUEST_STATE_COUNT + state) * 4
            )
            for state in range(QUEST_STATE_COUNT)
        ]
        for subtype in range(SUBTYPE_COUNT)
    ]
    if any(not math.isfinite(value) for row in target_heights for value in row):
        raise ParseError(f"{view.source}: EnKo tracking-height table is invalid")

    facing_threshold = view.u32(
        _offset(NPC_TRACKING_FACING_THRESHOLD_LITERAL, 4, view)
    )
    initial_routes = [
        {
            "quest_state_index": 0,
            "subtype": subtype,
            "preset_index": preset,
            "mode_policy": policy,
            "initial_forced_mode": mode,
            "facing_mode": (
                2 if policy == "facing_threshold" or
                engaged_policy == "facing_threshold" else None
            ),
            "not_facing_mode": (
                1 if policy == "facing_threshold" or
                engaged_policy == "facing_threshold" else None
            ),
            "engaged_mode_policy": (
                engaged_policy if policy == "talk_state_zero" else None
            ),
            "engaged_forced_mode": (
                engaged_mode if policy == "talk_state_zero" else None
            ),
            "evidence_call_site": call_site,
        }
        for subtype, preset, policy, mode, engaged_policy, engaged_mode, call_site in (
            (0, 2, "talk_state_zero", 1, "fixed", 2, 0x00172084),
            (1, 2, "fixed", 4, None, None, 0x00172A30),
            (2, 5, "talk_state_zero", 1, "facing_threshold", None, 0x00172CA4),
            (3, 2, "fixed", 4, None, None, 0x00172A30),
            (4, 5, "talk_state_zero", 1, "fixed", 2, 0x00172164),
            (5, 2, "fixed", 4, None, None, 0x00172A30),
            (6, 2, "facing_threshold", 1, None, None, 0x00172290),
            (12, 2, "fixed", 4, None, None, 0x00172A30),
        )
    ]
    return {
        "status": "initial_child_start_routes_recovered",
        "service": {
            "address": NPC_TRACKING_SERVICE,
            "size": NPC_TRACKING_SERVICE_SIZE,
            "sha256": _sha256_address_range(
                view, NPC_TRACKING_SERVICE, NPC_TRACKING_SERVICE_SIZE
            ),
            "parameter_types": [
                "Oot3dActor*",
                "Oot3dNpcInteractInfo*",
                "s32",
                "s32",
            ],
            "parameter_names": [
                "actor",
                "interact_info",
                "preset_index",
                "forced_tracking_mode",
            ],
            "abi_note": "fourth ARM argument r3 is consumed by the native selector",
        },
        "selector": {
            "address": NPC_TRACKING_SELECTOR,
            "size": NPC_TRACKING_SELECTOR_SIZE,
            "sha256": _sha256_address_range(
                view, NPC_TRACKING_SELECTOR, NPC_TRACKING_SELECTOR_SIZE
            ),
            "forced_mode_argument_register": "r3",
            "forced_mode_path": "nonzero_argument_returned_unchanged",
            "facing_threshold": facing_threshold,
            "auto_turn_timer_function_address": 0x003702C8,
            "auto_turn_timer_function_size": 0x80,
            "auto_turn_timer_function_sha256": _sha256_address_range(
                view, 0x003702C8, 0x80
            ),
            "random_state_address": native_random_zero_one_contract(view)[
                "state_address"
            ],
        },
        "consumer": {
            "address": 0x00171ED4,
            "size": 0x0B14,
            "sha256": _sha256_address_range(view, 0x00171ED4, 0x0B14),
        },
        "interact_info": {
            "actor_offset": NPC_TRACKING_INTERACT_INFO_OFFSET,
            "size": NPC_TRACKING_INTERACT_INFO_SIZE,
            "talk_state_s16_offset": 0x00,
            "tracking_mode_offset": 0x02,
            "auto_turn_timer_s16_offset": 0x04,
            "auto_turn_state_s16_offset": 0x06,
            "head_rotation_u16x3_offset": 0x08,
            "torso_rotation_u16x3_offset": 0x0E,
            "head_pitch_s16_offset": 0x08,
            "head_yaw_s16_offset": 0x0A,
            "torso_pitch_s16_offset": 0x0E,
            "torso_yaw_s16_offset": 0x10,
            "target_height_f32_offset": 0x14,
            "target_position_f32x3_offset": 0x18,
        },
        "actor_layout": {
            "minimum_size": 0xC0,
            "world_position_f32x3_offset": 0x28,
            "shape_yaw_s16_offset": 0xBE,
        },
        "global_context": {
            "pointer_address": NATIVE_GLOBAL_CONTEXT_POINTER_ADDRESS,
            "minimum_size": NATIVE_GLOBAL_CONTEXT_MINIMUM_SIZE,
            "update_rate_s16_offset": NATIVE_UPDATE_RATE_S16_OFFSET,
            "update_rate": update_rate,
            "initializer_address": NATIVE_UPDATE_RATE_INITIALIZER_ADDRESS,
            "source": "GameState_Init code.bin store consumed by Math_SmoothStepToS",
        },
        "smoothing": {
            "scale": smooth_scale,
            "max_step": smooth_max_step,
            "min_step": smooth_min_step,
            "call_sites": list(NPC_TRACKING_SMOOTH_CALLS),
        },
        "mode_semantics": [
            {"mode": 1, "head": False, "torso": False, "rotate_actor": False},
            {"mode": 2, "head": True, "torso": True, "rotate_actor": False},
            {"mode": 3, "head": True, "torso": False, "rotate_actor": False},
            {"mode": 4, "head": True, "torso": True, "rotate_actor": True},
        ],
        "mode_decode_instruction_sha256": hashlib.sha256(
            b"".join(
                view.bytes(_offset(address, 4, view), 4)
                for address in sorted(NPC_TRACKING_MODE_WORDS)
            )
        ).hexdigest(),
        "signed_angle_guard": signed_guard,
        "preset_table_address": preset_table,
        "preset_table_end": NPC_TRACKING_PRESET_TABLE_END,
        "preset_stride": NPC_TRACKING_PRESET_STRIDE,
        "preset_table_sha256": _sha256_address_range(
            view, preset_table, preset_bytes
        ),
        "presets": presets,
        "target_height_table_address": target_height_table,
        "target_height_table_sha256": _sha256_address_range(
            view, target_height_table, target_height_bytes
        ),
        "target_height_fallback": view.f32(
            _offset(NPC_TRACKING_TARGET_HEIGHT_FALLBACK_LITERAL, 4, view)
        ),
        "target_heights_by_subtype_and_quest_state": target_heights,
        "initial_routes": initial_routes,
    }


def _actor_profile(view: BinaryView) -> dict[str, int]:
    table_address = view.u32(_offset(ACTOR_OVERLAY_TABLE_POINTER_LITERAL, 4, view))
    overlay_entry_address = table_address + ENKO_ACTOR_ID * ACTOR_OVERLAY_ENTRY_STRIDE
    profile_address = view.u32(
        _offset(overlay_entry_address + ACTOR_OVERLAY_PROFILE_POINTER_OFFSET, 4, view)
    )
    offset = _offset(profile_address, 0x20, view)
    profile = {
        "actor_id": view.u16(offset),
        "category": view.u8(offset + 0x02),
        "flags": view.u32(offset + 0x04),
        "object_id": view.u16(offset + 0x08),
        "instance_size": view.u32(offset + 0x0C),
        "init_address": view.u32(offset + 0x10),
        "destroy_address": view.u32(offset + 0x14),
        "update_address": view.u32(offset + 0x18),
        "draw_address": view.u32(offset + 0x1C),
        "overlay_table_address": table_address,
        "overlay_entry_address": overlay_entry_address,
        "profile_address": profile_address,
    }
    if profile["actor_id"] != ENKO_ACTOR_ID:
        raise ParseError(f"{view.source}: EnKo ActorInit profile identity mismatch")
    return profile


def _object_path(view: BinaryView, object_id: int) -> str:
    offset = _offset(OBJECT_TABLE + object_id * 0x44, 0x44, view)
    raw = view.cstr(offset, 0x44).replace("\\", "/")
    if not raw.startswith("rom:/"):
        raise ParseError(f"{view.source}: object {object_id} has unsupported path {raw!r}")
    return raw[len("rom:/"):]


def _typed_files(archive: ZarArchive, type_name: str) -> list[ZarFile]:
    suffix = f".{type_name}"
    return sorted(
        (
            entry for entry in archive.files
            if entry.type_name == type_name or entry.name.lower().endswith(suffix)
        ),
        key=lambda entry: (
            entry.type_local_index is None,
            entry.type_local_index if entry.type_local_index is not None else entry.index,
        ),
    )


def _asset_id(family: str, container: str, member: str) -> str:
    return f"{family}:{container}!{member}"


def _archive_sources(sources: list[Path]) -> dict[str, tuple[Path, ZarArchive]]:
    result: dict[str, tuple[Path, ZarArchive]] = {}
    for source in sources:
        key = source.name.lower()
        if key in result:
            raise ValueError(f"duplicate actor archive name: {source.name}")
        result[key] = (source, ZarArchive.from_path(source))
    return result


def _resolve_archive(
    archives: dict[str, tuple[Path, ZarArchive]], object_path: str
) -> tuple[Path, ZarArchive]:
    archive_name = Path(object_path).name.lower()
    try:
        return archives[archive_name]
    except KeyError as error:
        raise ValueError(
            f"native EnKo object archive is not selected: {object_path}"
        ) from error


def _ror32(value: int, shift: int) -> int:
    shift &= 31
    return ((value >> shift) | (value << ((32 - shift) & 31))) & 0xFFFFFFFF


def _arm_add_immediate(
    view: BinaryView, address: int, source_register: int, target_register: int
) -> int:
    instruction = view.u32(_offset(address, 4, view))
    if (
        ((instruction >> 25) & 0x7) != 0x1
        or ((instruction >> 21) & 0xF) != 0x4
        or ((instruction >> 16) & 0xF) != source_register
        or ((instruction >> 12) & 0xF) != target_register
    ):
        raise ParseError(
            f"{view.source}: expected ARM ADD immediate at 0x{address:08X}"
        )
    return _ror32(instruction & 0xFF, ((instruction >> 8) & 0xF) * 2)


def _arm_ldr_immediate(
    view: BinaryView, address: int, base_register: int, target_register: int
) -> int:
    instruction = view.u32(_offset(address, 4, view))
    if (
        ((instruction >> 26) & 0x3) != 0x1
        or ((instruction >> 25) & 0x1) != 0
        or ((instruction >> 24) & 0x1) != 1
        or ((instruction >> 23) & 0x1) != 1
        or ((instruction >> 22) & 0x1) != 0
        or ((instruction >> 20) & 0x1) != 1
        or ((instruction >> 16) & 0xF) != base_register
        or ((instruction >> 12) & 0xF) != target_register
    ):
        raise ParseError(
            f"{view.source}: expected ARM LDR immediate at 0x{address:08X}"
        )
    return instruction & 0xFFF


def _override_limb_draw_abi(
    catalog: dict[str, Any] | None, code_sha256: str
) -> dict[str, Any] | None:
    if catalog is None:
        return None
    if catalog.get("code_bin_sha256") != code_sha256:
        raise ValueError("EnKo native ABI catalog targets a different code.bin")
    matches = [
        row
        for row in catalog.get("functions", [])
        if isinstance(row, dict) and row.get("name") == "EnKo_OverrideLimbDraw"
    ]
    expected_parameters = ["Oot3dPlayState*", "s32", "Oot3dMtx3x4*", "void*"]
    if (
        len(matches) != 1
        or int(str(matches[0].get("address", "0")), 0) != ENKO_OVERRIDE_LIMB_DRAW
        or matches[0].get("closure_kind") != "maintained_abi"
        or matches[0].get("return_type") != "s32"
        or matches[0].get("parameter_types") != expected_parameters
    ):
        raise ValueError("reviewed EnKo_OverrideLimbDraw ABI evidence is incomplete")
    row = matches[0]
    return {
        "name": row["name"],
        "address": int(str(row["address"]), 0),
        "address_hex": row["address"],
        "closure_kind": row["closure_kind"],
        "return_type": row["return_type"],
        "parameter_types": list(row["parameter_types"]),
        "parameter_names": list(row["parameter_names"]),
        "source_tranche": row["source_tranche"],
        "evidence_file": row["evidence_file"],
        "signature_evidence_file": row["signature_evidence_file"],
        "catalog_payload_sha256": catalog["payload_sha256"],
        "source_snapshot_id": catalog["source_snapshot_id"],
    }


def build_enko_native_runtime_contract(
    code_bin: Path,
    actor_sources: list[Path],
    native_abi_catalog: dict[str, Any] | None = None,
) -> dict[str, object]:
    code = code_bin.read_bytes()
    view = BinaryView(code, str(code_bin))
    archives = _archive_sources(actor_sources)

    source_offset = _offset(ANIMATION_SOURCE_TABLE, ANIMATION_SOURCE_COUNT * 4, view)
    animation_source_indices = [
        view.u32(source_offset + index * 4) for index in range(ANIMATION_SOURCE_COUNT)
    ]

    model_classes: list[dict[str, object]] = []
    model_archives: dict[int, ZarArchive] = {}
    for index in range(MODEL_CLASS_COUNT):
        offset = _offset(MODEL_CLASS_TABLE + index * 8, 8, view)
        object_id = view.u32(offset)
        cmb_index = view.u32(offset + 4)
        object_path = _object_path(view, object_id)
        _, archive = _resolve_archive(archives, object_path)
        cmb_files = _typed_files(archive, "cmb")
        if cmb_index >= len(cmb_files):
            raise ParseError(
                f"{archive.path}: CMB type-local index {cmb_index} is out of range"
            )
        member = cmb_files[cmb_index].name
        model_archives[index] = archive
        model_classes.append({
            "index": index,
            "object_id": object_id,
            "object_path": object_path,
            "source_container": object_path,
            "cmb_type_local_index": cmb_index,
            "cmb_member": member,
            "model_asset_id": _asset_id("cmb", object_path, member),
        })

    head_classes: list[dict[str, object]] = []
    for index in range(HEAD_CLASS_COUNT):
        offset = _offset(HEAD_TABLE + index * 0x10, 0x10, view)
        object_id = view.u32(offset)
        object_path = _object_path(view, object_id)
        _, archive = _resolve_archive(archives, object_path)
        cmb_files = _typed_files(archive, "cmb")
        if not cmb_files:
            raise ParseError(f"{archive.path}: EnKo head archive has no CMB")
        face_model_member = cmb_files[0].name
        clear_ids = [
            value for value in (view.u32(offset + 4), view.u32(offset + 8))
            if value != 0xFFFFFFFF
        ]
        head_classes.append({
            "index": index,
            "object_id": object_id,
            "object_path": object_path,
            "source_container": object_path,
            "face_model_member": face_model_member,
            "face_model_asset_id": _asset_id(
                "cmb", object_path, face_model_member
            ),
            "resource_visibility_clear_ids": clear_ids,
            "face_animation_selector": view.u32(offset + 0x0C),
        })

    model_scale = view.f32(_offset(MODEL_SCALE_LITERAL, 4, view))
    if not 0.0 < model_scale < 1.0:
        raise ParseError(f"{view.source}: invalid EnKo model scale {model_scale}")

    subtypes: list[dict[str, object]] = []
    for index in range(SUBTYPE_COUNT):
        offset = _offset(MODEL_INFO_TABLE + index * 11, 11, view)
        head_class = view.u8(offset)
        body_class = view.u8(offset + 1)
        legs_class = view.u8(offset + 6)
        if head_class >= len(head_classes) or legs_class >= len(model_classes):
            raise ParseError(f"{view.source}: invalid EnKo subtype {index} class selector")
        model_class = model_classes[legs_class]
        head = head_classes[head_class]
        subtypes.append({
            "index": index,
            "head_class_index": head_class,
            "body_class_index": body_class,
            "legs_class_index": legs_class,
            "model_class_index": legs_class,
            "model_asset_id": model_class["model_asset_id"],
            "face_model_asset_id": head["face_model_asset_id"],
            "model_scale": model_scale,
            "resource_visibility_clear_ids": head["resource_visibility_clear_ids"],
            "face_animation_selector": head["face_animation_selector"],
            "tunic_color": list(view.bytes(offset + 2, 4)),
            "boots_color": list(view.bytes(offset + 7, 4)),
        })

    animations: list[dict[str, object]] = []
    for semantic_index in range(ANIMATION_COUNT):
        offset = _offset(ANIMATION_TABLE + semantic_index * 0x18, 0x18, view)
        initialized_index = view.u32(offset)
        if semantic_index < len(ANIMATION_SOURCE_SLOT_BY_SEMANTIC):
            if initialized_index != 0:
                raise ParseError(
                    f"{view.source}: EnKo animation {semantic_index} no longer has constructor-filled index"
                )
            source_slot = ANIMATION_SOURCE_SLOT_BY_SEMANTIC[semantic_index]
            csab_index = animation_source_indices[source_slot]
        else:
            source_slot = None
            csab_index = initialized_index

        bindings = []
        for model_class in model_classes:
            model_class_index = int(model_class["index"])
            archive = model_archives[model_class_index]
            csab_files = _typed_files(archive, "csab")
            if csab_index >= len(csab_files):
                raise ParseError(
                    f"{archive.path}: CSAB type-local index {csab_index} is out of range"
                )
            member = csab_files[csab_index].name
            source_container = str(model_class["source_container"])
            bindings.append({
                "model_class_index": model_class_index,
                "source_container": source_container,
                "csab_member": member,
                "animation_asset_id": _asset_id("csab", source_container, member),
            })
        animations.append({
            "index": semantic_index,
            "semantic_index": semantic_index,
            "constructor_source_slot": source_slot,
            "csab_type_local_index": csab_index,
            "playback_speed": view.f32(offset + 4),
            "start_frame": view.f32(offset + 8),
            "end_frame": view.f32(offset + 0x0C),
            "playback_mode": view.u8(offset + 0x10),
            "morph_frames": view.f32(offset + 0x14),
            "bindings_by_model_class": bindings,
        })

    lookup_offset = _offset(
        QUEST_ANIMATION_LOOKUP, SUBTYPE_COUNT * QUEST_STATE_COUNT, view
    )
    quest_lookup = [
        [
            view.u8(lookup_offset + subtype * QUEST_STATE_COUNT + state)
            for state in range(QUEST_STATE_COUNT)
        ]
        for subtype in range(SUBTYPE_COUNT)
    ]
    if any(value >= ANIMATION_COUNT for row in quest_lookup for value in row):
        raise ParseError(f"{view.source}: EnKo quest animation lookup is out of range")

    torso_limb_index = _arm_cmp_r1_immediate(view, ENKO_TORSO_LIMB_COMPARE)
    head_limb_index = _arm_cmp_r1_immediate(view, ENKO_HEAD_LIMB_COMPARE)
    torso_rotation_offset = _arm_add_immediate(
        view, ENKO_TORSO_STATE_BASE_ADD, 3, 1
    ) + _arm_add_immediate(view, ENKO_TORSO_STATE_OFFSET_ADD, 1, 1)
    head_rotation_offset = _arm_ldr_immediate(
        view, ENKO_HEAD_STATE_WORD0_LOAD, 3, 0
    )
    if _arm_ldr_immediate(view, ENKO_HEAD_STATE_WORD1_LOAD, 3, 1) != (
        head_rotation_offset + 4
    ):
        raise ParseError(f"{view.source}: EnKo head rotation words are not contiguous")
    code_sha256 = hashlib.sha256(code).hexdigest()
    override_limb_draw_abi = _override_limb_draw_abi(
        native_abi_catalog, code_sha256
    )

    return {
        "format": FORMAT,
        "status": "complete",
        "source": {
            "code_bin": str(code_bin),
            "code_bin_sha256": code_sha256,
            "authority": "oot3d_code_bin_and_original_actor_zar",
        },
        "evidence": {
            "code_image_base": CODE_IMAGE_BASE,
            "constructor_address": ENKO_CONSTRUCTOR,
            "animation_source_table_address": ANIMATION_SOURCE_TABLE,
            "head_table_address": HEAD_TABLE,
            "model_class_table_address": MODEL_CLASS_TABLE,
            "animation_table_address": ANIMATION_TABLE,
            "quest_animation_lookup_address": QUEST_ANIMATION_LOOKUP,
            "model_info_table_address": MODEL_INFO_TABLE,
            "object_table_address": OBJECT_TABLE,
            "model_scale_literal_address": MODEL_SCALE_LITERAL,
            "override_limb_draw_address": ENKO_OVERRIDE_LIMB_DRAW,
            "torso_limb_compare_address": ENKO_TORSO_LIMB_COMPARE,
            "head_limb_compare_address": ENKO_HEAD_LIMB_COMPARE,
            "torso_state_base_add_address": ENKO_TORSO_STATE_BASE_ADD,
            "torso_state_offset_add_address": ENKO_TORSO_STATE_OFFSET_ADD,
            "head_state_word0_load_address": ENKO_HEAD_STATE_WORD0_LOAD,
            "head_state_word1_load_address": ENKO_HEAD_STATE_WORD1_LOAD,
            "npc_tracking_service_address": NPC_TRACKING_SERVICE,
            "npc_tracking_preset_pointer_literal": NPC_TRACKING_PRESET_POINTER_LITERAL,
            "npc_tracking_target_height_pointer_literal": (
                NPC_TRACKING_TARGET_HEIGHT_POINTER_LITERAL
            ),
        },
        "draw_callback": {
            "address": ENKO_OVERRIDE_LIMB_DRAW,
            "size": ENKO_OVERRIDE_LIMB_DRAW_SIZE,
            "sha256": _sha256_address_range(
                view, ENKO_OVERRIDE_LIMB_DRAW, ENKO_OVERRIDE_LIMB_DRAW_SIZE
            ),
            "torso_limb_index": torso_limb_index,
            "head_limb_index": head_limb_index,
            "torso_rotation_u16x3_offset": torso_rotation_offset,
            "head_rotation_u16x3_offset": head_rotation_offset,
            "rotation_order": ["x_from_rotation_y", "z_from_rotation_x"],
            "native_abi": override_limb_draw_abi,
        },
        "face_runtime": _enko_face_runtime(view),
        "tracking_runtime": _npc_tracking_runtime(view),
        "actor_profile": _actor_profile(view),
        "quest_state_semantics": list(QUEST_STATE_SEMANTICS),
        "animation_source_indices": animation_source_indices,
        "model_classes": model_classes,
        "head_classes": head_classes,
        "subtypes": subtypes,
        "animations": animations,
        "quest_animation_lookup": quest_lookup,
    }
