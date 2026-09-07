from __future__ import annotations

import hashlib
import math
from pathlib import Path

from .binary import BinaryView, ParseError
from .native_actor_contract_common import (
    actor_profile,
    arm_data_processing_immediate,
    asset_id,
    code_offset,
    object_path,
    resolve_archive,
    typed_files,
)


FORMAT = "oot3d_rigid_actor_native_runtime_contract_v1"
RESOURCE = "oot3d/catalog/contracts/oot3d-rigid-actors-native-runtime.json"

ENISHI_ACTOR_ID = 0x014E
ENISHI_INIT = 0x001B5460
ENISHI_DESTROY = 0x001B5698
ENISHI_DRAW = 0x001F6994
ENISHI_UPDATE = 0x001F69AC
ENISHI_INIT_SELECTOR_INSTRUCTION = 0x001B5484
ENISHI_DEFAULT_MODEL_MOVE = 0x001B5480
ENISHI_SELECTOR_ONE_COMPARE = 0x001B5488
ENISHI_SELECTOR_TWO_COMPARE = 0x001B548C
ENISHI_ALTERNATE_MODEL_MOVE = 0x001B5490
ENISHI_SCALE_TABLE_POINTER_LITERAL = 0x001B5674
ENISHI_DRAW_SELECTOR_INSTRUCTION = 0x001F699C
ENISHI_DRAW_TABLE_POINTER_LITERAL = 0x001F69A8
ENISHI_SUPPORTED_SELECTOR_COUNT = 3

ENAOBJ_ACTOR_ID = 0x0039
ENAOBJ_INIT = 0x001E0734
ENAOBJ_DESTROY = 0x001F9618
ENAOBJ_DRAW = 0x001E06A4
ENAOBJ_UPDATE = 0x0016B31C
ENAOBJ_MODEL_TABLE_ADDRESS_INSTRUCTION = 0x001E075C
ENAOBJ_SELECTOR_MASK_INSTRUCTION = 0x001E0774
ENAOBJ_MODEL_CLAMP_COMPARE = 0x001E0788
ENAOBJ_SCALE_JUMP_TABLE = 0x001E07C0
ENAOBJ_SCALE_DEFAULT_BRANCH = 0x001E07BC
ENAOBJ_SUPPORTED_SELECTOR_COUNT = 12

ENKANBAN_ACTOR_ID = 0x0141
ENKANBAN_INIT = 0x001F7180
ENKANBAN_DESTROY = 0x001F7484
ENKANBAN_DRAW = 0x0022BE4C
ENKANBAN_UPDATE = 0x0022C284
ENKANBAN_SCALE_LOAD = 0x001F718C
ENKANBAN_SCALE_CALL = 0x001F7194
ENKANBAN_EFFECT_MODEL_MOVE = 0x001F7368
ENKANBAN_PIECE_MODEL_TABLE_POINTER_LITERAL = 0x001F7480
ENKANBAN_PIECE_MODEL_COUNT = 11
ENKANBAN_KEEP_OBJECT_MOVE = 0x001F73D4
ENKANBAN_WHOLE_MODEL_MOVE = 0x001F7410
ENKANBAN_PIECE_MASK_TABLE_POINTER_LITERAL = 0x0022C228
ENKANBAN_DRAW_ZERO_LITERAL_LOAD = 0x0022BE70
ENKANBAN_DRAW_LOCAL_Z_LITERAL_LOAD = 0x0022C0B8

ENGS_ACTOR_ID = 0x01B9
ENGS_INIT = 0x0016461C
ENGS_DESTROY = 0x0016474C
ENGS_DRAW = 0x001B342C
ENGS_UPDATE = 0x001B37C0
ENGS_INIT_CHAIN_POINTER_LITERAL = 0x00164728
ENGS_MODEL_LOAD_SEQUENCE = 0x00164680
ENGS_MODEL_DRAW_SEQUENCE = 0x001B365C
ENGS_MODEL_HANDLE_OFFSET = 0x0270
ENGS_MODEL_INDEX = 0

_ENISHI_DRAW_WORDS = {
    0x00: 0xE1D021BC,
    0x04: 0xE59F3008,
    0x08: 0xE2022003,
    0x0C: 0xE7932102,
    0x10: 0xE12FFF12,
}

_RIGID_DRAW_HELPER_WORDS = {
    0x00: 0xE92D4010,
    0x04: 0xE24DD030,
    0x08: 0xE1A04000,
    0x0C: 0xE2801F52,
    0x10: 0xE1A0000D,
    0x18: 0xE5940200,
    0x1C: 0xE3500000,
    0x24: 0xE3A01001,
    0x28: 0xE5C010AC,
    0x2C: 0xE5940200,
    0x30: 0xE1A0100D,
    0x38: 0xE5940200,
    0x3C: 0xE3A01000,
    0x44: 0xE28DD030,
    0x48: 0xE8BD8010,
}

_RIGID_DRAW_HELPER_BRANCHES = {
    0x14: (0xE, True, 0x00372224),
    0x20: (0x0, False, None),
    0x34: (0xE, True, 0x003721E0),
    0x40: (0xE, True, 0x00372170),
}

_ENAOBJ_SELECTOR_WORDS = {
    0x001E0754: 0xE1D001BC,
    0x001E0764: 0xE1A00800,
    0x001E0768: 0xE1A00C20,
    0x001E0770: 0xE1D401BC,
    0x001E0774: 0xE20000FF,
    0x001E0778: 0xE1C401BC,
    0x001E0788: 0xE350000B,
    0x001E078C: 0xC3A0000B,
    0x001E079C: 0xE7D13000,
}

_ENAOBJ_DRAW_WORDS = {
    0x00: 0xE92D4030,
    0x08: 0xE59011AC,
    0x18: 0xE1D001BC,
    0x1C: 0xE350000B,
    0x58: 0xE59401AC,
    0x5C: 0xE2841F52,
    0x64: 0xE59401AC,
    0x68: 0xE3A01001,
    0x6C: 0xE5C010AC,
    0x70: 0xE59401AC,
    0x78: 0xE3A01000,
}


def _function_sha256(view: BinaryView, start: int, end: int) -> str:
    return hashlib.sha256(
        view.bytes(code_offset(start, end - start, view), end - start)
    ).hexdigest()


def _expect_words(view: BinaryView, base: int, words: dict[int, int], label: str) -> None:
    for relative, expected in words.items():
        address = base + relative
        actual = view.u32(code_offset(address, 4, view))
        if actual != expected:
            raise ParseError(
                f"{view.source}: {label} instruction at 0x{address:08X} changed "
                f"(0x{actual:08X} != 0x{expected:08X})"
            )


def _arm_branch(view: BinaryView, address: int) -> tuple[int, bool, int]:
    word = view.u32(code_offset(address, 4, view))
    if (word >> 25) & 0x7 != 0x5:
        raise ParseError(
            f"{view.source}: expected ARM branch at 0x{address:08X}"
        )
    immediate = word & 0x00FFFFFF
    if immediate & 0x00800000:
        immediate -= 0x01000000
    target = (address + 8 + (immediate << 2)) & 0xFFFFFFFF
    return (word >> 28) & 0xF, bool(word & 0x01000000), target


def _verify_rigid_draw_helper(view: BinaryView, address: int) -> dict[str, object]:
    _expect_words(view, address, _RIGID_DRAW_HELPER_WORDS, "rigid draw helper")
    branch_targets: list[dict[str, object]] = []
    for relative, (expected_condition, expected_link, expected_target) in (
        _RIGID_DRAW_HELPER_BRANCHES.items()
    ):
        branch_address = address + relative
        condition, link, target = _arm_branch(view, branch_address)
        if expected_target is None:
            expected_target = address + 0x44
        if (
            condition != expected_condition
            or link != expected_link
            or target != expected_target
        ):
            raise ParseError(
                f"{view.source}: rigid draw branch at 0x{branch_address:08X} changed"
            )
        branch_targets.append(
            {
                "address": branch_address,
                "condition": condition,
                "link": link,
                "target": target,
            }
        )
    return {
        "address": address,
        "size": 0x4C,
        "sha256": _function_sha256(view, address, address + 0x4C),
        "branch_targets": branch_targets,
    }


def _decode_and_mask(
    view: BinaryView, address: int, destination_register: int, source_register: int
) -> int:
    mask = arm_data_processing_immediate(view, address, 0, destination_register)
    word = view.u32(code_offset(address, 4, view))
    if (word >> 16) & 0xF != source_register:
        raise ParseError(
            f"{view.source}: selector source register at 0x{address:08X} changed"
        )
    return mask


def _arm_vldr_pc_literal(view: BinaryView, address: int) -> tuple[int, int]:
    word = view.u32(code_offset(address, 4, view))
    if (word & 0xFF3F0F00) != 0xED1F0A00:
        raise ParseError(
            f"{view.source}: expected VLDR PC literal at 0x{address:08X}"
        )
    displacement = (word & 0xFF) * 4
    base = address + 8
    literal = base + displacement if word & 0x00800000 else base - displacement
    register = (((word >> 12) & 0xF) << 1) | ((word >> 22) & 1)
    return register, literal


def _arm_vldr_s0_pc_literal(view: BinaryView, address: int) -> int:
    register, literal = _arm_vldr_pc_literal(view, address)
    if register != 0:
        raise ParseError(
            f"{view.source}: expected VLDR s0 PC literal at 0x{address:08X}"
        )
    return literal


def _decode_scaled_u16_init_chain_field(
    view: BinaryView, pointer_literal: int, field_offset: int
) -> tuple[int, float]:
    chain_address = view.u32(code_offset(pointer_literal, 4, view))
    for index in range(32):
        word = view.u32(code_offset(chain_address + index * 4, 4, view))
        entry_type = (word >> 1) & 0xF
        entry_offset = (word >> 5) & 0x7FF
        value = view.s16(code_offset(chain_address + index * 4 + 2, 2, view))
        if entry_type == 9 and entry_offset == field_offset:
            scale = value / 1000.0
            if not math.isfinite(scale) or scale <= 0.0:
                raise ParseError(f"{view.source}: invalid scaled InitChain field")
            return chain_address, scale
        if (word & 1) == 0:
            break
    raise ParseError(
        f"{view.source}: scaled InitChain field 0x{field_offset:X} was not found"
    )


def _build_enishi_definition(
    view: BinaryView, actor_sources: list[Path]
) -> dict[str, object]:
    profile = actor_profile(view, ENISHI_ACTOR_ID)
    expected_callbacks = {
        "init_address": ENISHI_INIT,
        "destroy_address": ENISHI_DESTROY,
        "update_address": ENISHI_UPDATE,
        "draw_address": ENISHI_DRAW,
    }
    for field, expected in expected_callbacks.items():
        if profile[field] != expected:
            raise ParseError(
                f"{view.source}: EnIshi {field} changed "
                f"(0x{profile[field]:08X} != 0x{expected:08X})"
            )

    init_selector_mask = _decode_and_mask(
        view, ENISHI_INIT_SELECTOR_INSTRUCTION, 5, 1
    )
    draw_selector_mask = _decode_and_mask(
        view, ENISHI_DRAW_SELECTOR_INSTRUCTION, 2, 2
    )
    if init_selector_mask != draw_selector_mask or init_selector_mask != 3:
        raise ParseError(f"{view.source}: EnIshi selector contract changed")

    default_model_index = arm_data_processing_immediate(
        view, ENISHI_DEFAULT_MODEL_MOVE, 13, 0
    )
    _expect_words(
        view,
        ENISHI_SELECTOR_ONE_COMPARE,
        {
            0x00: 0xE3550001,
            0x04: 0x13550002,
            0x08: 0x03A00004,
        },
        "EnIshi model selector",
    )
    alternate_model_index = view.u32(
        code_offset(ENISHI_ALTERNATE_MODEL_MOVE, 4, view)
    ) & 0xFF

    _expect_words(view, ENISHI_DRAW, _ENISHI_DRAW_WORDS, "EnIshi draw dispatcher")
    draw_table = view.u32(
        code_offset(ENISHI_DRAW_TABLE_POINTER_LITERAL, 4, view)
    )
    draw_helpers = [
        view.u32(code_offset(draw_table + selector * 4, 4, view))
        for selector in range(ENISHI_SUPPORTED_SELECTOR_COUNT)
    ]
    helper_evidence = [
        _verify_rigid_draw_helper(view, address) for address in draw_helpers
    ]

    source_container = object_path(view, profile["object_id"])
    archive = resolve_archive(actor_sources, source_container)
    models = typed_files(archive, "cmb")
    scale_table = view.u32(
        code_offset(ENISHI_SCALE_TABLE_POINTER_LITERAL, 4, view)
    )
    visual_states: list[dict[str, object]] = []
    for selector in range(ENISHI_SUPPORTED_SELECTOR_COUNT):
        model_index = default_model_index if selector == 0 else alternate_model_index
        if model_index >= len(models):
            raise ParseError(
                f"{archive.path}: EnIshi CMB index {model_index} is out of range"
            )
        scale = view.f32(code_offset(scale_table + selector * 4, 4, view))
        if not math.isfinite(scale) or scale <= 0.0:
            raise ParseError(
                f"{view.source}: EnIshi selector {selector} has invalid scale"
            )
        model = models[model_index]
        visual_states.append(
            {
                "selector": selector,
                "cmb_type_local_index": model_index,
                "cmb_member": model.name,
                "model_asset_id": asset_id("cmb", source_container, model.name),
                "model_scale": scale,
                "draw_callback_address": draw_helpers[selector],
            }
        )

    return {
        "actor_name": "EnIshi",
        "family": "rigid_single_model",
        "status": "initial_visual_complete",
        "actor_profile": profile,
        "profile_object_path": source_container,
        "selector_contract": {
            "source": "actor.params",
            "mask": init_selector_mask,
            "supported_values": list(range(ENISHI_SUPPORTED_SELECTOR_COUNT)),
            "unsupported_values": [3],
        },
        "model_selector": {
            "default_cmb_type_local_index": default_model_index,
            "alternate_cmb_type_local_index": alternate_model_index,
            "alternate_selectors": [1, 2],
        },
        "scale_table_address": scale_table,
        "draw_dispatch_table_address": draw_table,
        "visual_states": visual_states,
        "code_evidence": {
            "init_sha256": _function_sha256(view, ENISHI_INIT, ENISHI_DESTROY),
            "destroy_sha256": _function_sha256(view, ENISHI_DESTROY, 0x001B56C0),
            "draw_sha256": _function_sha256(view, ENISHI_DRAW, ENISHI_UPDATE),
            "update_sha256": _function_sha256(view, ENISHI_UPDATE, 0x001F69B4),
            "draw_helpers": helper_evidence,
        },
        "behavior_coverage": {
            "initial_visual": "complete",
            "shape_yaw_randomization": "pending_native_actor_behavior",
            "floor_raycast": "pending_native_collision_bridge",
            "cylinder_collision": "pending_native_actor_collision",
            "switch_and_save_kill": "pending_native_save_context_bridge",
            "pickup_and_break_states": "pending_native_action_state_machine",
        },
    }


def _build_enaobj_definition(
    view: BinaryView, actor_sources: list[Path]
) -> dict[str, object]:
    profile = actor_profile(view, ENAOBJ_ACTOR_ID)
    expected_callbacks = {
        "init_address": ENAOBJ_INIT,
        "destroy_address": ENAOBJ_DESTROY,
        "update_address": ENAOBJ_UPDATE,
        "draw_address": ENAOBJ_DRAW,
    }
    for field, expected in expected_callbacks.items():
        if profile[field] != expected:
            raise ParseError(
                f"{view.source}: EnAObj {field} changed "
                f"(0x{profile[field]:08X} != 0x{expected:08X})"
            )

    for address, expected in _ENAOBJ_SELECTOR_WORDS.items():
        _expect_words(view, address, {0: expected}, "EnAObj selector")
    selector_mask = _decode_and_mask(
        view, ENAOBJ_SELECTOR_MASK_INSTRUCTION, 0, 0
    )
    if selector_mask != 0xFF:
        raise ParseError(f"{view.source}: EnAObj selector mask changed")

    table_add = view.u32(
        code_offset(ENAOBJ_MODEL_TABLE_ADDRESS_INSTRUCTION, 4, view)
    )
    if (
        (table_add >> 28) != 0xE
        or ((table_add >> 21) & 0xF) != 4
        or ((table_add >> 16) & 0xF) != 15
        or ((table_add >> 12) & 0xF) != 2
    ):
        raise ParseError(f"{view.source}: EnAObj model-table address instruction changed")
    table_immediate = arm_data_processing_immediate(
        view, ENAOBJ_MODEL_TABLE_ADDRESS_INSTRUCTION, 4, 2
    )
    model_table = ENAOBJ_MODEL_TABLE_ADDRESS_INSTRUCTION + 8 + table_immediate

    scale_jump_table = [
        view.u32(code_offset(ENAOBJ_SCALE_JUMP_TABLE + selector * 4, 4, view))
        for selector in range(7)
    ]
    default_condition, default_link, default_scale_handler = _arm_branch(
        view, ENAOBJ_SCALE_DEFAULT_BRANCH
    )
    if default_condition != 0xE or default_link:
        raise ParseError(f"{view.source}: EnAObj default scale dispatch changed")
    scale_handlers = sorted(set([*scale_jump_table, default_scale_handler]))
    scale_by_handler: dict[int, float] = {}
    scale_literal_by_handler: dict[int, int] = {}
    for handler in scale_handlers:
        literal = _arm_vldr_s0_pc_literal(view, handler)
        scale = view.f32(code_offset(literal, 4, view))
        if not math.isfinite(scale) or scale <= 0.0:
            raise ParseError(
                f"{view.source}: EnAObj scale handler 0x{handler:08X} is invalid"
            )
        scale_literal_by_handler[handler] = literal
        scale_by_handler[handler] = scale

    _expect_words(view, ENAOBJ_DRAW, _ENAOBJ_DRAW_WORDS, "EnAObj draw")
    draw_submit_condition, draw_submit_link, draw_submit_target = _arm_branch(
        view, ENAOBJ_DRAW + 0x80
    )
    if (
        draw_submit_condition != 0xE
        or draw_submit_link
        or draw_submit_target != 0x00372170
    ):
        raise ParseError(f"{view.source}: EnAObj draw submit tail changed")

    source_container = object_path(view, profile["object_id"])
    archive = resolve_archive(actor_sources, source_container)
    models = typed_files(archive, "cmb")
    visual_states: list[dict[str, object]] = []
    for selector in range(ENAOBJ_SUPPORTED_SELECTOR_COUNT):
        model_index = view.u8(code_offset(model_table + selector, 1, view))
        if model_index >= len(models):
            raise ParseError(
                f"{archive.path}: EnAObj CMB index {model_index} is out of range"
            )
        scale_handler = (
            scale_jump_table[selector]
            if selector < len(scale_jump_table)
            else default_scale_handler
        )
        model = models[model_index]
        state: dict[str, object] = {
            "selector": selector,
            "cmb_type_local_index": model_index,
            "cmb_member": model.name,
            "model_asset_id": asset_id("cmb", source_container, model.name),
            "model_scale": scale_by_handler[scale_handler],
            "scale_handler_address": scale_handler,
            "scale_literal_address": scale_literal_by_handler[scale_handler],
        }
        if selector == 11:
            state["material_runtime"] = "pending_native_tev_constant_override"
        visual_states.append(state)

    return {
        "actor_name": "EnAObj",
        "family": "rigid_single_model",
        "status": "initial_visual_complete",
        "actor_profile": profile,
        "profile_object_path": source_container,
        "selector_contract": {
            "source": "actor.params",
            "mask": selector_mask,
            "supported_values": list(range(ENAOBJ_SUPPORTED_SELECTOR_COUNT)),
            "model_index_clamp_max": 11,
            "variant_config_source": "actor.params >> 8",
        },
        "model_table_address": model_table,
        "model_table_entry_size": 1,
        "scale_jump_table_address": ENAOBJ_SCALE_JUMP_TABLE,
        "scale_default_handler_address": default_scale_handler,
        "visual_states": visual_states,
        "code_evidence": {
            "init_sha256": _function_sha256(view, ENAOBJ_INIT, 0x001E0B3C),
            "destroy_sha256": _function_sha256(view, ENAOBJ_DESTROY, 0x001F9668),
            "draw_sha256": _function_sha256(view, ENAOBJ_DRAW, ENAOBJ_INIT),
            "update_sha256": _function_sha256(view, ENAOBJ_UPDATE, 0x0016B3DC),
            "draw_submit_address": 0x00372170,
        },
        "behavior_coverage": {
            "initial_visual": "complete",
            "selector_11_tev_constant_override": "pending_native_material_runtime",
            "dynamic_collision": "pending_native_collision_bridge",
            "cylinder_collision": "pending_native_actor_collision",
            "movement_and_rotation_states": "pending_native_action_state_machine",
            "interaction_text_variant": "pending_native_message_context_bridge",
        },
    }


def _build_enkanban_definition(
    view: BinaryView, actor_sources: list[Path]
) -> dict[str, object]:
    profile = actor_profile(view, ENKANBAN_ACTOR_ID)
    expected_callbacks = {
        "init_address": ENKANBAN_INIT,
        "destroy_address": ENKANBAN_DESTROY,
        "update_address": ENKANBAN_UPDATE,
        "draw_address": ENKANBAN_DRAW,
    }
    for field, expected in expected_callbacks.items():
        if profile[field] != expected:
            raise ParseError(
                f"{view.source}: EnKanban {field} changed "
                f"(0x{profile[field]:08X} != 0x{expected:08X})"
            )

    scale_register, scale_literal = _arm_vldr_pc_literal(
        view, ENKANBAN_SCALE_LOAD
    )
    scale_condition, scale_link, scale_target = _arm_branch(
        view, ENKANBAN_SCALE_CALL
    )
    if (
        scale_register != 0
        or scale_condition != 0xE
        or not scale_link
        or scale_target != 0x0037572C
    ):
        raise ParseError(f"{view.source}: EnKanban scale setup changed")
    model_scale = view.f32(code_offset(scale_literal, 4, view))
    if not math.isfinite(model_scale) or model_scale <= 0.0:
        raise ParseError(f"{view.source}: EnKanban scale literal is invalid")

    _expect_words(
        view,
        0x001F7210,
        {
            0x00: 0xE3E01000,
            0x04: 0xE1C0ACBE,
            0x0C: 0xE1C01ABE,
        },
        "EnKanban initial whole-state mask",
    )
    effect_model_index = arm_data_processing_immediate(
        view, ENKANBAN_EFFECT_MODEL_MOVE, 13, 1
    )
    _expect_words(
        view,
        0x001F73C4,
        {
            0x00: 0xE354000B,
            0x04: 0xE5810258,
        },
        "EnKanban piece-model loop",
    )
    keep_object_id = arm_data_processing_immediate(
        view, ENKANBAN_KEEP_OBJECT_MOVE, 13, 1
    )
    whole_model_index = arm_data_processing_immediate(
        view, ENKANBAN_WHOLE_MODEL_MOVE, 13, 1
    )
    piece_model_table = view.u32(
        code_offset(ENKANBAN_PIECE_MODEL_TABLE_POINTER_LITERAL, 4, view)
    )
    piece_mask_table = view.u32(
        code_offset(ENKANBAN_PIECE_MASK_TABLE_POINTER_LITERAL, 4, view)
    )

    source_container = object_path(view, profile["object_id"])
    archive = resolve_archive(actor_sources, source_container)
    models = typed_files(archive, "cmb")
    if effect_model_index >= len(models):
        raise ParseError(f"{archive.path}: EnKanban effect CMB is out of range")
    piece_model_indices = [
        view.u32(code_offset(piece_model_table + index * 4, 4, view))
        for index in range(ENKANBAN_PIECE_MODEL_COUNT)
    ]
    piece_masks = [
        view.u16(code_offset(piece_mask_table + index * 2, 2, view))
        for index in range(ENKANBAN_PIECE_MODEL_COUNT)
    ]
    if len(set(piece_model_indices)) != ENKANBAN_PIECE_MODEL_COUNT:
        raise ParseError(f"{view.source}: EnKanban piece-model table is not unique")
    if piece_masks != [1 << index for index in range(ENKANBAN_PIECE_MODEL_COUNT)]:
        raise ParseError(f"{view.source}: EnKanban piece-mask table changed")

    pieces: list[dict[str, object]] = []
    for index, (model_index, mask) in enumerate(
        zip(piece_model_indices, piece_masks, strict=True)
    ):
        if model_index >= len(models):
            raise ParseError(
                f"{archive.path}: EnKanban piece CMB index {model_index} is out of range"
            )
        model = models[model_index]
        pieces.append(
            {
                "piece_index": index,
                "state_mask": mask,
                "cmb_type_local_index": model_index,
                "cmb_member": model.name,
                "model_asset_id": asset_id("cmb", source_container, model.name),
            }
        )

    whole_source_container = object_path(view, keep_object_id)
    whole_archive = resolve_archive(actor_sources, whole_source_container)
    whole_models = typed_files(whole_archive, "cmb")
    if whole_model_index >= len(whole_models):
        raise ParseError(
            f"{whole_archive.path}: EnKanban whole CMB index {whole_model_index} is out of range"
        )
    whole_model = whole_models[whole_model_index]

    zero_register, zero_literal = _arm_vldr_pc_literal(
        view, ENKANBAN_DRAW_ZERO_LITERAL_LOAD
    )
    local_z_register, local_z_literal = _arm_vldr_pc_literal(
        view, ENKANBAN_DRAW_LOCAL_Z_LITERAL_LOAD
    )
    local_zero = view.f32(code_offset(zero_literal, 4, view))
    local_z = view.f32(code_offset(local_z_literal, 4, view))
    if (
        zero_register != 16
        or local_z_register != 2
        or local_zero != 0.0
        or not math.isfinite(local_z)
    ):
        raise ParseError(f"{view.source}: EnKanban whole-model translation changed")
    state_condition, state_link, state_target = _arm_branch(view, ENKANBAN_DRAW + 0x34)
    if state_condition != 0 or state_link or state_target != 0x0022C0B0:
        raise ParseError(f"{view.source}: EnKanban initial draw-state branch changed")
    _expect_words(
        view,
        0x0022C0B0,
        {
            0x00: 0xEEB00A48,
            0x08: 0xED9F1A5F,
            0x10: 0xEEF00A40,
            0x18: 0xE1DB0ABE,
            0x1C: 0xE2401CFF,
            0x20: 0xE25110FF,
            0x28: 0xE5960250,
            0x38: 0xE5960250,
            0x40: 0xE5940250,
            0x44: 0xE3A01000,
        },
        "EnKanban whole-model draw",
    )
    for address, target in ((0x0022C0EC, 0x003721E0), (0x0022C0F8, 0x00372170)):
        condition, link, actual_target = _arm_branch(view, address)
        if condition != 0xE or not link or actual_target != target:
            raise ParseError(f"{view.source}: EnKanban whole-model submit changed")

    effect_model = models[effect_model_index]
    return {
        "actor_name": "EnKanban",
        "family": "rigid_single_model",
        "status": "initial_visual_complete",
        "actor_profile": profile,
        "profile_object_path": source_container,
        "selector_contract": {
            "source": "constant",
            "value": 0,
            "supported_values": [0],
            "params_semantic": "message_id_and_special_spawn_mode",
        },
        "visual_states": [
            {
                "selector": 0,
                "object_id": keep_object_id,
                "object_path": whole_source_container,
                "cmb_type_local_index": whole_model_index,
                "cmb_member": whole_model.name,
                "model_asset_id": asset_id(
                    "cmb", whole_source_container, whole_model.name
                ),
                "model_scale": model_scale,
                "model_local_translation": [local_zero, local_zero, local_z],
                "native_piece_mask": 0xFFFF,
            }
        ],
        "piece_state_contract": {
            "actor_state_field_offset": 0x1AC,
            "piece_mask_field_offset": 0x1AE,
            "initial_actor_state": 0,
            "whole_model_mask_value": 0xFFFF,
            "piece_model_table_address": piece_model_table,
            "piece_mask_table_address": piece_mask_table,
            "pieces": pieces,
        },
        "effect_model": {
            "activity_field_offset": 0x1F0,
            "cmb_type_local_index": effect_model_index,
            "cmb_member": effect_model.name,
            "model_asset_id": asset_id(
                "cmb", source_container, effect_model.name
            ),
        },
        "code_evidence": {
            "init_sha256": _function_sha256(view, ENKANBAN_INIT, ENKANBAN_DESTROY),
            "destroy_sha256": _function_sha256(view, ENKANBAN_DESTROY, 0x001F750C),
            "draw_sha256": _function_sha256(view, ENKANBAN_DRAW, ENKANBAN_UPDATE),
            "update_sha256": _function_sha256(view, ENKANBAN_UPDATE, 0x0022D7A4),
            "scale_literal_address": scale_literal,
            "whole_draw_submit_address": 0x00372170,
        },
        "behavior_coverage": {
            "initial_whole_visual": "complete",
            "piece_and_effect_asset_binding": "complete",
            "floor_alignment": "pending_native_collision_bridge",
            "cylinder_collision": "pending_native_actor_collision",
            "message_interaction": "pending_native_message_context_bridge",
            "cut_and_piece_state_machine": "pending_native_action_state_machine",
            "piece_physics_and_water": "pending_native_action_state_machine",
        },
    }


def _build_engs_definition(
    view: BinaryView, actor_sources: list[Path]
) -> dict[str, object]:
    profile = actor_profile(view, ENGS_ACTOR_ID)
    expected_callbacks = {
        "init_address": ENGS_INIT,
        "destroy_address": ENGS_DESTROY,
        "update_address": ENGS_UPDATE,
        "draw_address": ENGS_DRAW,
    }
    for field, expected in expected_callbacks.items():
        if profile[field] != expected:
            raise ParseError(
                f"{view.source}: EnGs {field} changed "
                f"(0x{profile[field]:08X} != 0x{expected:08X})"
            )

    _expect_words(
        view,
        ENGS_MODEL_LOAD_SEQUENCE,
        {
            0x00: 0xE3A06000,
            0x2C: 0xE1A03006,
            0x30: 0xE2842E27,
            0x38: 0xE58D6000,
        },
        "EnGs model binding",
    )
    load_condition, load_link, load_target = _arm_branch(
        view, ENGS_MODEL_LOAD_SEQUENCE + 0x3C
    )
    if load_condition != 0xE or not load_link or load_target != 0x00372F38:
        raise ParseError(f"{view.source}: EnGs model-load call changed")

    _expect_words(
        view,
        ENGS_MODEL_DRAW_SEQUENCE,
        {
            0x00: 0xE5940270,
            0x04: 0xE3500000,
            0x08: 0x0A000006,
            0x0C: 0xE5C060AC,
            0x10: 0xE5940270,
            0x14: 0xE28D1004,
            0x1C: 0xE5940270,
            0x20: 0xE3A01000,
        },
        "EnGs model draw",
    )
    for relative, target, label in (
        (0x18, 0x003721E0, "matrix-copy"),
        (0x24, 0x00372170, "model-submit"),
    ):
        draw_condition, draw_link, draw_target = _arm_branch(
            view, ENGS_MODEL_DRAW_SEQUENCE + relative
        )
        if draw_condition != 0xE or not draw_link or draw_target != target:
            raise ParseError(f"{view.source}: EnGs {label} call changed")

    init_chain_address, model_scale = _decode_scaled_u16_init_chain_field(
        view, ENGS_INIT_CHAIN_POINTER_LITERAL, 0x54
    )
    source_container = object_path(view, profile["object_id"])
    archive = resolve_archive(actor_sources, source_container)
    models = typed_files(archive, "cmb")
    if ENGS_MODEL_INDEX >= len(models):
        raise ParseError(f"{archive.path}: EnGs CMB index is out of range")
    model = models[ENGS_MODEL_INDEX]

    return {
        "actor_name": "EnGs",
        "family": "rigid_single_model",
        "status": "initial_visual_complete",
        "actor_profile": profile,
        "profile_object_path": source_container,
        "selector_contract": {
            "source": "constant",
            "value": 0,
            "supported_values": [0],
            "params_semantic": "text_id_and_gossip_stone_variant",
        },
        "init_chain": {
            "address": init_chain_address,
            "model_scale_field_offset": 0x54,
            "model_scale": model_scale,
        },
        "model_handle_field_offset": ENGS_MODEL_HANDLE_OFFSET,
        "visual_states": [
            {
                "selector": 0,
                "cmb_type_local_index": ENGS_MODEL_INDEX,
                "cmb_member": model.name,
                "model_asset_id": asset_id("cmb", source_container, model.name),
                "model_scale": model_scale,
                "material_runtime": "pending_native_torch_animation_binding",
            }
        ],
        "code_evidence": {
            "init_sha256": _function_sha256(view, ENGS_INIT, 0x00164728),
            "destroy_sha256": _function_sha256(view, ENGS_DESTROY, 0x0016476C),
            "draw_sha256": _function_sha256(view, ENGS_DRAW, 0x001B37A4),
            "update_sha256": _function_sha256(view, ENGS_UPDATE, 0x001B3A64),
            "model_load_address": 0x00372F38,
            "model_submit_address": 0x00372170,
        },
        "behavior_coverage": {
            "initial_visual": "complete",
            "torch_material_animation": "pending_native_material_runtime",
            "cylinder_collision": "pending_native_actor_collision",
            "message_interaction": "pending_native_message_context_bridge",
            "hit_reaction_and_effects": "pending_native_action_state_machine",
        },
    }


def build_rigid_actor_native_runtime_contract(
    code_bin: Path, actor_sources: list[Path]
) -> dict[str, object]:
    code = code_bin.read_bytes()
    view = BinaryView(code, str(code_bin))
    actors = [
        _build_enishi_definition(view, actor_sources),
        _build_enaobj_definition(view, actor_sources),
        _build_enkanban_definition(view, actor_sources),
        _build_engs_definition(view, actor_sources),
    ]
    return {
        "format": FORMAT,
        "status": "initial_visual_complete",
        "source": {
            "kind": "oot3d_code_bin_and_native_zar",
            "code_bin_path": str(code_bin),
            "code_bin_sha256": hashlib.sha256(code).hexdigest(),
        },
        "actor_count": len(actors),
        "actors": actors,
    }
