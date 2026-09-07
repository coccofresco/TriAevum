from __future__ import annotations

import hashlib
from pathlib import Path

from .binary import BinaryView, ParseError
from .native_actor_contract_common import (
    CODE_IMAGE_BASE,
    actor_profile,
    arm_data_processing_immediate,
    asset_id,
    code_offset,
    native_random_zero_one_contract,
    object_path,
    resolve_archive,
    typed_files,
)


FORMAT = "oot3d_enmd_native_runtime_contract_v1"
RESOURCE = "oot3d/catalog/contracts/oot3d-enmd-native-runtime.json"
ENMD_ACTOR_ID = 0x016D

ANIMATION_TABLE_POINTER_LITERAL = 0x0016632C
ANIMATION_TABLE_STRIDE_FIRST = 0x003717B4
ANIMATION_TABLE_STRIDE_SECOND = 0x003717BC
ANIMATION_COUNT = 12
ANIMATION_STRIDE = 0x18
MODEL_SCALE_LITERAL = 0x00166330
INITIAL_KOKIRI_SCENE_COMPARE = 0x00166154
INITIAL_ANIMATION_SELECTOR_MOVE = 0x001661BC

BLINK_SEQUENCE_LENGTH_COMPARE = 0x001B73D8
BLINK_TIMER_BASE_MOVE = 0x001B73E4
BLINK_TIMER_RANGE_COPY = 0x001B73E8


def build_enmd_native_runtime_contract(
    code_bin: Path, actor_sources: list[Path]
) -> dict[str, object]:
    code = code_bin.read_bytes()
    view = BinaryView(code, str(code_bin))
    profile = actor_profile(view, ENMD_ACTOR_ID)
    object_path_value = object_path(view, profile["object_id"])
    archive = resolve_archive(actor_sources, object_path_value)
    cmb_files = typed_files(archive, "cmb")
    csab_files = typed_files(archive, "csab")
    cmab_files = typed_files(archive, "cmab")
    if len(cmb_files) != 1 or len(cmab_files) != 1:
        raise ParseError(f"{archive.path}: EnMd model or eye CMAB set is incomplete")

    if (
        view.u32(code_offset(ANIMATION_TABLE_STRIDE_FIRST, 4, view))
        != 0xE0820082
        or view.u32(code_offset(ANIMATION_TABLE_STRIDE_SECOND, 4, view))
        != 0xE0814180
    ):
        raise ParseError(f"{view.source}: EnMd animation table stride changed")
    table_address = view.u32(
        code_offset(ANIMATION_TABLE_POINTER_LITERAL, 4, view)
    )
    animations: list[dict[str, object]] = []
    for selector in range(ANIMATION_COUNT):
        offset = code_offset(
            table_address + selector * ANIMATION_STRIDE,
            ANIMATION_STRIDE,
            view,
        )
        csab_index = view.u32(offset)
        if csab_index >= len(csab_files):
            raise ParseError(
                f"{archive.path}: EnMd selector {selector} CSAB index is out of range"
            )
        if any(view.u8(offset + 0x11 + index) != 0 for index in range(3)):
            raise ParseError(
                f"{view.source}: EnMd selector {selector} playback mode padding changed"
            )
        csab = csab_files[csab_index]
        animations.append(
            {
                "selector_index": selector,
                "csab_type_local_index": csab_index,
                "source_container": object_path_value,
                "csab_member": csab.name,
                "animation_asset_id": asset_id(
                    "csab", object_path_value, csab.name
                ),
                "applied_playback_speed": view.f32(offset + 4),
                "start_frame": view.f32(offset + 8),
                "end_frame": view.f32(offset + 0x0C),
                "playback_mode": view.u8(offset + 0x10),
                "morph_frames": view.f32(offset + 0x14),
            }
        )

    native_scene_id = arm_data_processing_immediate(
        view, INITIAL_KOKIRI_SCENE_COMPARE, 0xA, 0
    )
    initial_selector = arm_data_processing_immediate(
        view, INITIAL_ANIMATION_SELECTOR_MOVE, 0xD, 2
    )
    model_scale = view.f32(code_offset(MODEL_SCALE_LITERAL, 4, view))
    if initial_selector >= len(animations) or not 0.0 < model_scale < 1.0:
        raise ParseError(f"{view.source}: EnMd initial visual state is invalid")

    blink_length = arm_data_processing_immediate(
        view, BLINK_SEQUENCE_LENGTH_COMPARE, 0xA, 0
    )
    blink_timer_base = arm_data_processing_immediate(
        view, BLINK_TIMER_BASE_MOVE, 0xD, 1
    )
    if blink_length != 3 or view.u32(
        code_offset(BLINK_TIMER_RANGE_COPY, 4, view)
    ) != 0xE1A00001:
        raise ParseError(f"{view.source}: EnMd blink sequence contract changed")
    model_member = cmb_files[0].name
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
            "animation_table_address": table_address,
            "animation_change_function_address": 0x003717AC,
            "init_address": profile["init_address"],
            "update_address": profile["update_address"],
            "draw_address": profile["draw_address"],
            "initial_scene_compare_address": INITIAL_KOKIRI_SCENE_COMPARE,
            "initial_selector_move_address": INITIAL_ANIMATION_SELECTOR_MOVE,
            "model_scale_literal_address": MODEL_SCALE_LITERAL,
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
            "eye_material_animation_channel": 0,
            "eye_cmab_member": cmab_files[0].name,
            "blink_timer_offset": 0x047C,
            "blink_index_offset": 0x047E,
            "blink_sequence": list(range(blink_length)),
            "blink_timer_base": blink_timer_base,
            "blink_timer_range": blink_timer_base,
            "random": native_random_zero_one_contract(view),
        },
    }
