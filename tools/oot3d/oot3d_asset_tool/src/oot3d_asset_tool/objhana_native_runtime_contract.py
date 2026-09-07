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


FORMAT = "oot3d_objhana_native_runtime_contract_v1"
RESOURCE = "oot3d/catalog/contracts/oot3d-objhana-native-runtime.json"

OBJHANA_ACTOR_ID = 0x014F
SELECTOR_MASK_INSTRUCTION = 0x001E174C
VISUAL_TABLE_POINTER_LITERAL = 0x001E1818
VISUAL_TABLE_ENTRY_STRIDE = 0x10
SUPPORTED_SELECTOR_COUNT = 3


def build_objhana_native_runtime_contract(
    code_bin: Path, actor_sources: list[Path]
) -> dict[str, object]:
    code = code_bin.read_bytes()
    view = BinaryView(code, str(code_bin))
    profile = actor_profile(view, OBJHANA_ACTOR_ID)
    source_container = object_path(view, profile["object_id"])
    archive = resolve_archive(actor_sources, source_container)
    models = typed_files(archive, "cmb")

    selector_mask = arm_data_processing_immediate(
        view, SELECTOR_MASK_INSTRUCTION, 0, 6
    )
    visual_table = view.u32(
        code_offset(VISUAL_TABLE_POINTER_LITERAL, 4, view)
    )
    visual_states: list[dict[str, object]] = []
    for selector in range(SUPPORTED_SELECTOR_COUNT):
        offset = code_offset(
            visual_table + selector * VISUAL_TABLE_ENTRY_STRIDE,
            VISUAL_TABLE_ENTRY_STRIDE,
            view,
        )
        model_index = view.u32(offset)
        model_scale = view.f32(offset + 4)
        if model_index >= len(models):
            raise ParseError(
                f"{archive.path}: ObjHana CMB index {model_index} is out of range"
            )
        if not math.isfinite(model_scale) or model_scale <= 0.0:
            raise ParseError(
                f"{view.source}: ObjHana selector {selector} has invalid scale"
            )
        model = models[model_index]
        visual_states.append(
            {
                "selector": selector,
                "cmb_type_local_index": model_index,
                "cmb_member": model.name,
                "model_asset_id": asset_id("cmb", source_container, model.name),
                "model_scale": model_scale,
                "culling_forward": view.f32(offset + 8),
                "collider_radius": view.s16(offset + 12),
                "collider_height": view.s16(offset + 14),
            }
        )

    return {
        "format": FORMAT,
        "status": "initial_visual_complete",
        "source": {
            "kind": "oot3d_code_bin_and_native_zar",
            "code_bin_path": str(code_bin),
            "code_bin_sha256": hashlib.sha256(code).hexdigest(),
        },
        "actor_profile": profile,
        "profile_object_path": source_container,
        "selector_mask": selector_mask,
        "visual_table_address": visual_table,
        "visual_table_entry_stride": VISUAL_TABLE_ENTRY_STRIDE,
        "visual_states": visual_states,
        "unsupported_selector": {
            "selector": 3,
            "reason": "no native ObjHana visual-table record is addressed by known scene data",
        },
        "behavior_coverage": {
            "initial_visual": "complete",
            "culling_forward": "decoded_pending_runtime_culling",
            "cylinder_collision": "decoded_pending_native_actor_collision",
            "selector_2_save_kill": "pending_native_save_context_bridge",
        },
    }
