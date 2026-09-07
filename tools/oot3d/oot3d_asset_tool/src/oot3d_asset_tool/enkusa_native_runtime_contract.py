from __future__ import annotations

import hashlib
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


FORMAT = "oot3d_enkusa_native_runtime_contract_v1"
RESOURCE = "oot3d/catalog/contracts/oot3d-enkusa-native-runtime.json"

ENKUSA_ACTOR_ID = 0x0125
INIT_CHAIN_POINTER_LITERAL = 0x001B6668
OBJECT_SELECTOR_TABLE_POINTER_LITERAL = 0x001B6688
OBJECT_READY_CALLBACK_LITERAL = 0x001B668C
OBJECT_READY_CALLBACK = 0x00110450
DRAW_CALLBACK_LITERAL = 0x00110558
DRAW_CALLBACK = 0x001F7510
SELECTOR_MASK = 0x3
MODEL_SCALE_FIELD_OFFSET = 0x54
MODEL_SCALE_INIT_CHAIN_TYPE = 9

MODEL_INDEX_MOVES = {
    0: 0x001104B8,
    1: 0x00110488,
}
DESTROYED_MODEL_INDEX_MOVE = 0x001104A0


def _decode_init_chain_scale(view: BinaryView) -> tuple[int, float]:
    address = view.u32(code_offset(INIT_CHAIN_POINTER_LITERAL, 4, view))
    for index in range(32):
        word = view.u32(code_offset(address + index * 4, 4, view))
        entry_type = (word >> 1) & 0xF
        field_offset = (word >> 5) & 0x7FF
        value = view.s16(code_offset(address + index * 4 + 2, 2, view))
        if entry_type == MODEL_SCALE_INIT_CHAIN_TYPE and field_offset == MODEL_SCALE_FIELD_OFFSET:
            return address, value / 1000.0
        if (word & 1) == 0:
            break
    raise ParseError(f"{view.source}: EnKusa model-scale InitChain entry not found")


def build_enkusa_native_runtime_contract(
    code_bin: Path, actor_sources: list[Path]
) -> dict[str, object]:
    code = code_bin.read_bytes()
    view = BinaryView(code, str(code_bin))
    profile = actor_profile(view, ENKUSA_ACTOR_ID)
    profile_object_path = object_path(view, profile["object_id"])
    init_chain_address, model_scale = _decode_init_chain_scale(view)

    ready_callback = view.u32(
        code_offset(OBJECT_READY_CALLBACK_LITERAL, 4, view)
    )
    draw_callback = view.u32(code_offset(DRAW_CALLBACK_LITERAL, 4, view))
    if ready_callback != OBJECT_READY_CALLBACK or draw_callback != DRAW_CALLBACK:
        raise ParseError(f"{view.source}: EnKusa callback literals changed")

    selector_table = view.u32(
        code_offset(OBJECT_SELECTOR_TABLE_POINTER_LITERAL, 4, view)
    )
    object_ids = [
        view.u16(code_offset(selector_table + selector * 2, 2, view))
        for selector in range(4)
    ]
    model_indices = {
        selector: arm_data_processing_immediate(view, address, 13, 3)
        for selector, address in MODEL_INDEX_MOVES.items()
    }
    destroyed_model_index = arm_data_processing_immediate(
        view, DESTROYED_MODEL_INDEX_MOVE, 13, 3
    )

    visual_states: list[dict[str, object]] = []
    for selector in range(3):
        object_id = object_ids[selector]
        source_container = object_path(view, object_id)
        archive = resolve_archive(actor_sources, source_container)
        models = typed_files(archive, "cmb")
        intact_index = model_indices[0 if selector == 0 else 1]
        if intact_index >= len(models):
            raise ParseError(
                f"{archive.path}: EnKusa intact CMB index {intact_index} is out of range"
            )
        state: dict[str, object] = {
            "selector": selector,
            "object_id": object_id,
            "source_container": source_container,
            "intact_cmb_type_local_index": intact_index,
            "intact_cmb_member": models[intact_index].name,
            "intact_model_asset_id": asset_id(
                "cmb", source_container, models[intact_index].name
            ),
        }
        if selector != 0:
            if destroyed_model_index >= len(models):
                raise ParseError(
                    f"{archive.path}: EnKusa destroyed CMB index is out of range"
                )
            state.update(
                {
                    "destroyed_cmb_type_local_index": destroyed_model_index,
                    "destroyed_cmb_member": models[destroyed_model_index].name,
                    "destroyed_model_asset_id": asset_id(
                        "cmb", source_container, models[destroyed_model_index].name
                    ),
                }
            )
        visual_states.append(state)

    return {
        "format": FORMAT,
        "status": "initial_visual_complete",
        "source": {
            "kind": "oot3d_code_bin_and_native_zar",
            "code_bin_path": str(code_bin),
            "code_bin_sha256": hashlib.sha256(code).hexdigest(),
        },
        "actor_profile": profile,
        "profile_object_path": profile_object_path,
        "init_chain": {
            "address": init_chain_address,
            "model_scale_field_offset": MODEL_SCALE_FIELD_OFFSET,
            "model_scale": model_scale,
        },
        "runtime_callbacks": {
            "object_ready_callback": ready_callback,
            "draw_callback": draw_callback,
        },
        "selector_mask": SELECTOR_MASK,
        "object_selector_table_address": selector_table,
        "object_selector_ids": object_ids,
        "visual_states": visual_states,
        "unsupported_selector": {
            "selector": 3,
            "object_id": object_ids[3],
            "native_result": "actor_kill",
        },
        "behavior_coverage": {
            "initial_visual": "complete",
            "floor_raycast": "pending_native_collision_bridge",
            "cut_collision": "pending_native_actor_collision",
            "drops": "pending_native_item_runtime",
            "destroyed_transition": "pending_native_action_state_machine",
            "regrowth": "pending_native_action_state_machine",
        },
    }
