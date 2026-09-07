from __future__ import annotations

import hashlib
from pathlib import Path

from .binary import BinaryView, ParseError
from .native_actor_contract_common import (
    ACTOR_OVERLAY_ENTRY_STRIDE,
    ACTOR_OVERLAY_PROFILE_POINTER_OFFSET,
    ACTOR_OVERLAY_TABLE_POINTER_LITERAL,
    CODE_IMAGE_BASE,
    OBJECT_TABLE,
    actor_profile,
    arm_data_processing_immediate,
    asset_id,
    code_offset,
    object_path,
    native_random_zero_one_contract,
    resolve_archive,
    typed_files,
)


FORMAT = "oot3d_ensa_native_runtime_contract_v1"
RESOURCE = "oot3d/catalog/contracts/oot3d-ensa-native-runtime.json"

ENSA_ACTOR_ID = 0x0146

ANIMATION_SELECTOR_TABLE_POINTER_LITERAL = 0x0035FBF4
ANIMATION_SELECTOR_TABLE_STRIDE_INSTRUCTION = 0x0035FBBC
ANIMATION_SELECTOR_COUNT = 15
ANIMATION_SELECTOR_STRIDE = 0x10
ANIMATION_START_FRAME_LITERAL = 0x0035FBF8
ANIMATION_PLAYBACK_SPEED_LITERAL = 0x0035FBFC
MODEL_SCALE_LITERAL = 0x001688B0
INITIAL_KOKIRI_SCENE_COMPARE = 0x001686B4
INITIAL_KOKIRI_SELECTOR_MOVE = 0x001687C4

FACE_EYE_CHANNEL = 0
FACE_MOUTH_CHANNEL = 1
FACE_MOUTH_TABLE_POINTER_LITERAL = 0x001B943C
FACE_MOUTH_TABLE_COUNT = 5
BLINK_SEQUENCE_LENGTH_COMPARE = 0x001B96CC
BLINK_TIMER_BASE_MOVE = 0x001B96D8
BLINK_TIMER_RANGE_COPY = 0x001B96DC


def build_ensa_native_runtime_contract(
    code_bin: Path, actor_sources: list[Path]
) -> dict[str, object]:
    code = code_bin.read_bytes()
    view = BinaryView(code, str(code_bin))
    profile = actor_profile(view, ENSA_ACTOR_ID)
    object_path_value = object_path(view, profile["object_id"])
    archive = resolve_archive(actor_sources, object_path_value)
    cmb_files = typed_files(archive, "cmb")
    csab_files = typed_files(archive, "csab")
    cmab_files = typed_files(archive, "cmab")
    if len(cmb_files) != 1 or len(cmab_files) < 2:
        raise ParseError(f"{archive.path}: EnSa model or face CMAB set is incomplete")

    if view.u32(code_offset(ANIMATION_SELECTOR_TABLE_STRIDE_INSTRUCTION, 4, view)) != 0xE0804201:
        raise ParseError(f"{view.source}: EnSa animation selector stride changed")
    table_address = view.u32(
        code_offset(ANIMATION_SELECTOR_TABLE_POINTER_LITERAL, 4, view)
    )
    start_frame = view.f32(code_offset(ANIMATION_START_FRAME_LITERAL, 4, view))
    applied_speed = view.f32(
        code_offset(ANIMATION_PLAYBACK_SPEED_LITERAL, 4, view)
    )
    animations: list[dict[str, object]] = []
    for selector in range(ANIMATION_SELECTOR_COUNT):
        offset = code_offset(
            table_address + selector * ANIMATION_SELECTOR_STRIDE,
            ANIMATION_SELECTOR_STRIDE,
            view,
        )
        csab_index = view.u32(offset)
        if csab_index >= len(csab_files):
            raise ParseError(
                f"{archive.path}: EnSa selector {selector} CSAB index is out of range"
            )
        if any(view.u8(offset + 9 + index) != 0 for index in range(3)):
            raise ParseError(
                f"{view.source}: EnSa selector {selector} playback mode padding changed"
            )
        csab = csab_files[csab_index]
        animations.append(
            {
                "selector_index": selector,
                "csab_type_local_index": csab_index,
                "source_container": object_path_value,
                "csab_member": csab.name,
                "animation_asset_id": asset_id("csab", object_path_value, csab.name),
                "table_playback_speed": view.f32(offset + 4),
                "applied_playback_speed": applied_speed,
                "start_frame": start_frame,
                "end_frame_source": "native_csab_frame_count",
                "playback_mode": view.u8(offset + 8),
                "morph_frames": view.f32(offset + 0x0C),
            }
        )

    native_scene_id = arm_data_processing_immediate(
        view, INITIAL_KOKIRI_SCENE_COMPARE, 0xA, 0
    )
    initial_selector = arm_data_processing_immediate(
        view, INITIAL_KOKIRI_SELECTOR_MOVE, 0xD, 1
    )
    if initial_selector >= len(animations):
        raise ParseError(f"{view.source}: EnSa initial selector is out of range")

    model_scale = view.f32(code_offset(MODEL_SCALE_LITERAL, 4, view))
    if not 0.0 < model_scale < 1.0:
        raise ParseError(f"{view.source}: invalid EnSa model scale {model_scale}")

    blink_length = arm_data_processing_immediate(
        view, BLINK_SEQUENCE_LENGTH_COMPARE, 0xA, 0
    )
    blink_timer_base = arm_data_processing_immediate(
        view, BLINK_TIMER_BASE_MOVE, 0xD, 1
    )
    if blink_length != 3 or view.u32(
        code_offset(BLINK_TIMER_RANGE_COPY, 4, view)
    ) != 0xE1A00001:
        raise ParseError(f"{view.source}: EnSa blink sequence contract changed")
    mouth_table_address = view.u32(
        code_offset(FACE_MOUTH_TABLE_POINTER_LITERAL, 4, view)
    )
    mouth_frames = list(
        view.bytes(code_offset(mouth_table_address, FACE_MOUTH_TABLE_COUNT, view),
                   FACE_MOUTH_TABLE_COUNT)
    )
    model_member = cmb_files[0].name
    eye_member = cmab_files[FACE_EYE_CHANNEL].name
    mouth_member = cmab_files[FACE_MOUTH_CHANNEL].name
    return {
        "format": FORMAT,
        "status": "initial_route_complete",
        "source": {
            "code_bin": str(code_bin),
            "code_bin_sha256": hashlib.sha256(code).hexdigest(),
            "authority": "oot3d_code_bin_and_original_actor_zar",
        },
        "evidence": {
            "code_image_base": CODE_IMAGE_BASE,
            "animation_selector_table_address": table_address,
            "animation_selector_function_address": 0x0035FBB0,
            "init_address": profile["init_address"],
            "update_address": profile["update_address"],
            "draw_address": profile["draw_address"],
            "initial_scene_compare_address": INITIAL_KOKIRI_SCENE_COMPARE,
            "initial_selector_move_address": INITIAL_KOKIRI_SELECTOR_MOVE,
            "model_scale_literal_address": MODEL_SCALE_LITERAL,
            "mouth_table_address": mouth_table_address,
        },
        "actor_profile": profile,
        "object": {
            "object_id": profile["object_id"],
            "object_path": object_path_value,
            "source_container": object_path_value,
        },
        "model": {
            "cmb_type_local_index": 0,
            "cmb_member": model_member,
            "model_asset_id": asset_id("cmb", object_path_value, model_member),
            "model_scale": model_scale,
        },
        "animations": animations,
        "spawn_state_semantics": [
            {
                "index": initial_selector,
                "semantic": "kokiri_forest_initial_child_day",
                "conditions": {
                    "scene.native_id": str(native_scene_id),
                    "player.age": "child",
                    "world.day_phase": "day",
                    "gameplay.layer": "normal",
                    "quest.kokiri_emerald": "false",
                    "event.zeldas_letter": "false",
                    "quest.forest_medallion": "false",
                },
            }
        ],
        "face_runtime": {
            "face_animation_set_offset": 0x0228,
            "eye_material_animation_channel": FACE_EYE_CHANNEL,
            "mouth_material_animation_channel": FACE_MOUTH_CHANNEL,
            "eye_cmab_member": eye_member,
            "mouth_cmab_member": mouth_member,
            "blink_timer_offset": 0x047C,
            "blink_index_offset": 0x0480,
            "mouth_index_offset": 0x0482,
            "blink_sequence": list(range(blink_length)),
            "blink_timer_base": blink_timer_base,
            "blink_timer_range": blink_timer_base,
            "mouth_frame_lookup": mouth_frames,
            "initial_mouth_state": 0,
            "random": native_random_zero_one_contract(view),
        },
    }
