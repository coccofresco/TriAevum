from __future__ import annotations

import hashlib
import math
from pathlib import Path

from .binary import BinaryView, ParseError
from .native_actor_contract_common import (
    actor_profile,
    asset_id,
    code_offset,
    object_path,
    resolve_archive,
    typed_files,
)


FORMAT = "oot3d_item_actor_native_runtime_contract_v1"
RESOURCE = "oot3d/catalog/contracts/oot3d-item-actors-native-runtime.json"

ENITEM00_ACTOR_ID = 0x0015
ENITEM00_INIT = 0x001F69B4
ENITEM00_DESTROY = 0x001F706C
ENITEM00_DRAW = 0x0022B6C0
ENITEM00_UPDATE = 0x0022B71C
ENITEM00_SELECTOR_MASK_INSTRUCTION = 0x001F6A0C
ENITEM00_SCALE_JUMP_TABLE = 0x001F6A64
ENITEM00_MODEL_TABLE_POINTER_LITERAL = 0x001F6D64
ENITEM00_INITIAL_ACTION_POINTER_LITERAL = 0x001F7064
ENITEM00_SUPPORTED_SELECTOR_COUNT = 26
ENITEM00_DRAW_SUPPRESSION_MASK = 0x4000
ENITEM00_THROWN_SPAWN_MASK = 0x8000

MODEL_VISIBILITY_PRESET = 0x00369178
MODEL_SHOW_MESH = 0x0037266C
MODEL_HIDE_MESH = 0x0036932C

_SCALE_SWITCH_EXIT = 0x001F6C8C
_ACTION_END = 0x004C0534

_EXPECTED_SCALE_HANDLERS = {
    0x001F6ACC,
    0x001F6AE8,
    0x001F6B0C,
    0x001F6B34,
    0x001F6B68,
    0x001F6B90,
    0x001F6BB0,
    0x001F6BCC,
    0x001F6BE8,
    0x001F6C04,
    0x001F6C20,
    0x001F6C3C,
    0x001F6C5C,
}

_PRESET_MOVE_ADDRESS_BY_SELECTOR = {
    0: 0x001F6D8C,
    1: 0x001F6DA0,
    2: 0x001F6D74,
    19: 0x001F6DCC,
    20: 0x001F6DBC,
}

_DRAW_WORDS = {
    0x00: 0xE92D4010,
    0x04: 0xE1A04000,
    0x08: 0xE2800C01,
    0x0C: 0xE1D01AFE,
    0x10: 0xE1D00BF0,
    0x14: 0xE1100001,
    0x1C: 0xE5940210,
    0x20: 0xE2841F52,
    0x24: 0xE3500000,
    0x2C: 0xE5D42214,
    0x30: 0xE3520000,
    0x3C: 0xE5940210,
    0x40: 0xE3A01001,
    0x44: 0xE5C010AC,
    0x48: 0xE5940210,
    0x50: 0xE3A01000,
    0x58: 0xE8BD8010,
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


def _arm_immediate(
    view: BinaryView,
    address: int,
    *,
    opcode: int,
    destination_register: int,
    source_register: int | None = None,
) -> int:
    word = view.u32(code_offset(address, 4, view))
    if (
        ((word >> 25) & 1) != 1
        or ((word >> 21) & 0xF) != opcode
        or ((word >> 12) & 0xF) != destination_register
        or (source_register is not None and ((word >> 16) & 0xF) != source_register)
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


def _arm_branch(view: BinaryView, address: int) -> tuple[int, bool, int]:
    word = view.u32(code_offset(address, 4, view))
    if ((word >> 25) & 0x7) != 0x5:
        raise ParseError(f"{view.source}: expected ARM branch at 0x{address:08X}")
    immediate = word & 0x00FFFFFF
    if immediate & 0x00800000:
        immediate -= 0x01000000
    return (word >> 28) & 0xF, bool(word & 0x01000000), (
        address + 8 + (immediate << 2)
    ) & 0xFFFFFFFF


def _vfp_scalar_register(word: int) -> int:
    return (((word >> 12) & 0xF) << 1) | ((word >> 22) & 1)


def _arm_vldr_pc_literal(view: BinaryView, address: int) -> tuple[int, int]:
    word = view.u32(code_offset(address, 4, view))
    if (word & 0xFF3F0F00) != 0xED1F0A00:
        raise ParseError(f"{view.source}: expected VLDR PC literal at 0x{address:08X}")
    displacement = (word & 0xFF) * 4
    base = address + 8
    literal = base + displacement if word & 0x00800000 else base - displacement
    return _vfp_scalar_register(word), literal


def _decode_scale_states(view: BinaryView) -> tuple[list[int], dict[int, dict[str, object]]]:
    handlers = [
        view.u32(code_offset(ENITEM00_SCALE_JUMP_TABLE + selector * 4, 4, view))
        for selector in range(ENITEM00_SUPPORTED_SELECTOR_COUNT)
    ]
    if set(handlers) != _EXPECTED_SCALE_HANDLERS:
        raise ParseError(f"{view.source}: EnItem00 scale jump table changed")

    decoded: dict[int, dict[str, object]] = {}
    ordered = sorted(set(handlers))
    for index, handler in enumerate(ordered):
        end = ordered[index + 1] if index + 1 < len(ordered) else _SCALE_SWITCH_EXIT
        loaded_literals: dict[int, int] = {}
        scale_register: int | None = None
        scale_literal: int | None = None
        for address in range(handler, end, 4):
            word = view.u32(code_offset(address, 4, view))
            if (word & 0xFF3F0F00) == 0xED1F0A00:
                register, literal = _arm_vldr_pc_literal(view, address)
                loaded_literals[register] = literal
            if (word & 0xFFFF0FFF) == 0xED840A6D:
                scale_register = _vfp_scalar_register(word)
                scale_literal = loaded_literals.get(scale_register)
        y_offset_literal = loaded_literals.get(16)
        if scale_register is None or scale_literal is None or y_offset_literal is None:
            raise ParseError(
                f"{view.source}: EnItem00 scale handler 0x{handler:08X} is incomplete"
            )
        scale = view.f32(code_offset(scale_literal, 4, view))
        y_offset = view.f32(code_offset(y_offset_literal, 4, view))
        if not math.isfinite(scale) or scale <= 0.0 or not math.isfinite(y_offset):
            raise ParseError(
                f"{view.source}: EnItem00 scale handler 0x{handler:08X} has invalid data"
            )
        decoded[handler] = {
            "model_scale": scale,
            "shape_y_offset": y_offset,
            "scale_literal_address": scale_literal,
            "shape_y_offset_literal_address": y_offset_literal,
        }
    return handlers, decoded


def _decode_visibility_presets(view: BinaryView) -> tuple[dict[int, int], dict[int, int]]:
    base_preset = _arm_immediate(
        view, MODEL_VISIBILITY_PRESET + 0x10, opcode=2, destination_register=0,
        source_register=1,
    )
    preset_count = _arm_immediate(
        view, MODEL_VISIBILITY_PRESET + 0x14, opcode=10,
        destination_register=0,
    )
    dispatch = [
        view.u32(code_offset(MODEL_VISIBILITY_PRESET + 0x20 + index * 4, 4, view))
        for index in range(preset_count)
    ]
    handlers = sorted(set(dispatch) - {MODEL_VISIBILITY_PRESET + 0x1C})
    visible_mesh_by_preset: dict[int, int] = {}
    for handler_index, handler in enumerate(handlers):
        end = handlers[handler_index + 1] if handler_index + 1 < len(handlers) else MODEL_HIDE_MESH
        shown: list[int] = []
        pending_mesh: int | None = None
        for address in range(handler, end, 4):
            word = view.u32(code_offset(address, 4, view))
            if (
                ((word >> 25) & 1) == 1
                and ((word >> 21) & 0xF) == 13
                and ((word >> 12) & 0xF) == 1
            ):
                pending_mesh = _arm_immediate(
                    view, address, opcode=13, destination_register=1
                )
                continue
            if ((word >> 25) & 0x7) == 0x5 and bool(word & 0x01000000):
                _, _, target = _arm_branch(view, address)
                if target == MODEL_SHOW_MESH and pending_mesh is not None:
                    shown.append(pending_mesh)
        if len(shown) != 1:
            raise ParseError(
                f"{view.source}: mesh-visibility handler 0x{handler:08X} changed"
            )
        preset = base_preset + dispatch.index(handler)
        visible_mesh_by_preset[preset] = shown[0]

    preset_by_selector: dict[int, int] = {}
    for selector, address in _PRESET_MOVE_ADDRESS_BY_SELECTOR.items():
        preset = _arm_immediate(view, address, opcode=13, destination_register=1)
        if preset not in visible_mesh_by_preset:
            raise ParseError(
                f"{view.source}: EnItem00 selector {selector} has unknown mesh preset"
            )
        preset_by_selector[selector] = preset
    for call_address in (0x001F6DC0, 0x001F6DD0):
        _, link, target = _arm_branch(view, call_address)
        if not link or target != MODEL_VISIBILITY_PRESET:
            raise ParseError(f"{view.source}: EnItem00 mesh preset call changed")
    return preset_by_selector, visible_mesh_by_preset


def _decode_spin_contract(view: BinaryView, action: int) -> tuple[list[int], int]:
    expected_branches = {
        action + 0x24: (0xD, action + 0x50),
        action + 0x34: (0x9, action + 0x50),
        action + 0x4C: (0x8, action + 0x60),
    }
    for address, (condition, target) in expected_branches.items():
        actual_condition, link, actual_target = _arm_branch(view, address)
        if link or actual_condition != condition or actual_target != target:
            raise ParseError(f"{view.source}: EnItem00 spin dispatch changed")

    first_max = _arm_immediate(
        view, action + 0x20, opcode=10, destination_register=0
    )
    isolated = _arm_immediate(
        view, action + 0x28, opcode=10, destination_register=0
    )
    range_a_start = _arm_immediate(
        view, action + 0x2C, opcode=2, destination_register=1, source_register=0
    )
    range_a_width = _arm_immediate(
        view, action + 0x30, opcode=10, destination_register=0, source_register=1
    )
    isolated_b = _arm_immediate(
        view, action + 0x38, opcode=10, destination_register=0
    )
    range_b_start = _arm_immediate(
        view, action + 0x3C, opcode=2, destination_register=1, source_register=0
    )
    range_b_width = _arm_immediate(
        view, action + 0x40, opcode=10, destination_register=0, source_register=1
    )
    range_c_start = _arm_immediate(
        view, action + 0x44, opcode=2, destination_register=1, source_register=0
    )
    range_c_width = _arm_immediate(
        view, action + 0x48, opcode=10, destination_register=0, source_register=1
    )
    selectors = sorted(
        set(range(first_max + 1))
        | {isolated, isolated_b}
        | set(range(range_a_start, range_a_start + range_a_width + 1))
        | set(range(range_b_start, range_b_start + range_b_width + 1))
        | set(range(range_c_start, range_c_start + range_c_width + 1))
    )
    selectors = [
        selector
        for selector in selectors
        if 0 <= selector < ENITEM00_SUPPORTED_SELECTOR_COUNT
    ]
    _expect_words(
        view,
        action + 0x50,
        {0x00: 0xE1D40BBE, 0x08: 0xE1C40BBE},
        "EnItem00 shape-yaw update",
    )
    yaw_step = _arm_immediate(
        view, action + 0x54, opcode=4, destination_register=0, source_register=0
    )
    return selectors, yaw_step


def _resolve_contract_archive(actor_sources: list[Path], path: str):
    explicit = [source for source in actor_sources if source.name.lower() == Path(path).name.lower()]
    if explicit:
        return resolve_archive(explicit, path)
    siblings = {
        source.parent / Path(path).name
        for source in actor_sources
        if (source.parent / Path(path).name).is_file()
    }
    if len(siblings) != 1:
        raise ValueError(f"native item object archive selection is not unique: {path}")
    return resolve_archive(list(siblings), path)


def _build_enitem00_definition(
    view: BinaryView, actor_sources: list[Path]
) -> dict[str, object]:
    profile = actor_profile(view, ENITEM00_ACTOR_ID)
    expected_callbacks = {
        "init_address": ENITEM00_INIT,
        "destroy_address": ENITEM00_DESTROY,
        "update_address": ENITEM00_UPDATE,
        "draw_address": ENITEM00_DRAW,
    }
    for field, expected in expected_callbacks.items():
        if profile[field] != expected:
            raise ParseError(
                f"{view.source}: EnItem00 {field} changed "
                f"(0x{profile[field]:08X} != 0x{expected:08X})"
            )

    selector_mask = _arm_immediate(
        view, ENITEM00_SELECTOR_MASK_INSTRUCTION, opcode=0,
        destination_register=0, source_register=0,
    )
    if selector_mask != 0xFF:
        raise ParseError(f"{view.source}: EnItem00 selector mask changed")

    scale_handlers, scales = _decode_scale_states(view)
    preset_by_selector, visible_mesh_by_preset = _decode_visibility_presets(view)
    action = view.u32(code_offset(ENITEM00_INITIAL_ACTION_POINTER_LITERAL, 4, view))
    spin_selectors, yaw_step = _decode_spin_contract(view, action)

    _expect_words(view, ENITEM00_DRAW, _DRAW_WORDS, "EnItem00 draw")
    for address, target in ((ENITEM00_DRAW + 0x38, 0x003721E0),
                            (ENITEM00_DRAW + 0x54, 0x00372170)):
        _, link, actual_target = _arm_branch(view, address)
        if actual_target != target or link != (address == ENITEM00_DRAW + 0x38):
            raise ParseError(f"{view.source}: EnItem00 draw submit path changed")

    model_table = view.u32(
        code_offset(ENITEM00_MODEL_TABLE_POINTER_LITERAL, 4, view)
    )
    visual_states: list[dict[str, object]] = []
    archive_cache: dict[str, object] = {}
    for selector in range(ENITEM00_SUPPORTED_SELECTOR_COUNT):
        record = code_offset(model_table + selector * 4, 4, view)
        object_id = view.s16(record)
        model_index = view.u8(record + 2)
        record_flags = view.u8(record + 3)
        source_container = object_path(view, object_id)
        archive = archive_cache.get(source_container)
        if archive is None:
            archive = _resolve_contract_archive(actor_sources, source_container)
            archive_cache[source_container] = archive
        models = typed_files(archive, "cmb")
        if model_index >= len(models):
            raise ParseError(
                f"{archive.path}: EnItem00 CMB index {model_index} is out of range"
            )
        model = models[model_index]
        scale_state = scales[scale_handlers[selector]]
        state: dict[str, object] = {
            "selector": selector,
            "object_id": object_id,
            "object_path": source_container,
            "table_flags": record_flags,
            "cmb_type_local_index": model_index,
            "cmb_member": model.name,
            "model_asset_id": asset_id("cmb", source_container, model.name),
            "scale_handler_address": scale_handlers[selector],
            **scale_state,
        }
        if selector in preset_by_selector:
            preset = preset_by_selector[selector]
            state["mesh_visibility_preset"] = preset
            state["visible_mesh_indices"] = [visible_mesh_by_preset[preset]]
        if selector in spin_selectors:
            state["shape_yaw_step_per_native_tick"] = yaw_step
        visual_states.append(state)

    return {
        "actor_name": "EnItem00",
        "family": "rigid_single_model",
        "status": "initial_visual_complete",
        "actor_profile": profile,
        "selector_contract": {
            "source": "actor.params",
            "mask": selector_mask,
            "supported_values": list(range(ENITEM00_SUPPORTED_SELECTOR_COUNT)),
            "collectible_flag_source": "(actor.params >> 8) & 0x3F",
        },
        "initial_draw_gate": {
            "source": "actor.params",
            "suppressed_when_mask_nonzero": ENITEM00_DRAW_SUPPRESSION_MASK,
            "native_actor_field_offset": 0x214,
        },
        "spawn_mode_contract": {
            "source": "actor.params",
            "thrown_spawn_mask": ENITEM00_THROWN_SPAWN_MASK,
            "world_spawn_value": 0,
        },
        "model_table_address": model_table,
        "model_table_entry_size": 4,
        "scale_jump_table_address": ENITEM00_SCALE_JUMP_TABLE,
        "initial_action_address": action,
        "native_tick_rate": 30.0,
        "visual_states": visual_states,
        "code_evidence": {
            "init_sha256": _function_sha256(view, ENITEM00_INIT, ENITEM00_DESTROY),
            "destroy_sha256": _function_sha256(view, ENITEM00_DESTROY, 0x001F7094),
            "draw_sha256": _function_sha256(view, ENITEM00_DRAW, ENITEM00_UPDATE),
            "update_sha256": _function_sha256(view, ENITEM00_UPDATE, 0x0022BD24),
            "initial_action_sha256": _function_sha256(view, action, _ACTION_END),
            "mesh_visibility_preset_function": MODEL_VISIBILITY_PRESET,
            "mesh_visibility_preset_sha256": _function_sha256(
                view, MODEL_VISIBILITY_PRESET, MODEL_HIDE_MESH
            ),
        },
        "behavior_coverage": {
            "initial_visual": "complete",
            "mesh_visibility_presets": "complete",
            "shape_y_offset": "complete",
            "world_spawn_shape_yaw": "complete",
            "initial_draw_gate": "complete",
            "collectible_flag_kill": "pending_native_save_context_bridge",
            "pickup_and_item_grant": "pending_native_inventory_bridge",
            "cylinder_collision": "pending_native_actor_collision",
            "thrown_spawn_motion": "pending_native_action_state_machine",
            "floor_bg_check": "pending_native_collision_bridge",
        },
    }


def build_item_actor_native_runtime_contract(
    code_bin: Path, actor_sources: list[Path]
) -> dict[str, object]:
    code = code_bin.read_bytes()
    view = BinaryView(code, str(code_bin))
    actors = [_build_enitem00_definition(view, actor_sources)]
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
