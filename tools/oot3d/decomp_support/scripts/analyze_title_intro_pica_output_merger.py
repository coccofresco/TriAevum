#!/usr/bin/env python3
"""Decode output-merger and TextureEnv state for OOT3D title-intro PICA draws."""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path
from typing import Any


DEFAULT_CAPTURE = Path(
    "captures/azahar_pica/title_intro_slot6_kankyo_focused_20260707_082324/"
    "oot3d_pica_frame_000001.jsonl"
)
DEFAULT_OUTPUT_JSON = Path(
    "tools/oot3d/decomp_support/analysis/title_intro_pica_output_merger_trace.json"
)
DEFAULT_OUTPUT_CSV = Path(
    "tools/oot3d/decomp_support/analysis/title_intro_pica_output_merger_trace.csv"
)
DEFAULT_OUTPUT_MD = Path(
    "tools/oot3d/decomp_support/analysis/title_intro_pica_output_merger_trace.md"
)


REG_NAMES = {
    0x0C0: "GPUREG_TEXENV0_SOURCE",
    0x0C1: "GPUREG_TEXENV0_OPERAND",
    0x0C2: "GPUREG_TEXENV0_COMBINER",
    0x0C3: "GPUREG_TEXENV0_COLOR",
    0x0C4: "GPUREG_TEXENV0_SCALE",
    0x0C8: "GPUREG_TEXENV1_SOURCE",
    0x0C9: "GPUREG_TEXENV1_OPERAND",
    0x0CA: "GPUREG_TEXENV1_COMBINER",
    0x0CB: "GPUREG_TEXENV1_COLOR",
    0x0CC: "GPUREG_TEXENV1_SCALE",
    0x0D0: "GPUREG_TEXENV2_SOURCE",
    0x0D1: "GPUREG_TEXENV2_OPERAND",
    0x0D2: "GPUREG_TEXENV2_COMBINER",
    0x0D3: "GPUREG_TEXENV2_COLOR",
    0x0D4: "GPUREG_TEXENV2_SCALE",
    0x0D8: "GPUREG_TEXENV3_SOURCE",
    0x0D9: "GPUREG_TEXENV3_OPERAND",
    0x0DA: "GPUREG_TEXENV3_COMBINER",
    0x0DB: "GPUREG_TEXENV3_COLOR",
    0x0DC: "GPUREG_TEXENV3_SCALE",
    0x0E0: "GPUREG_TEXENV_UPDATE_BUFFER",
    0x0E1: "GPUREG_FOG_COLOR",
    0x0F0: "GPUREG_TEXENV4_SOURCE",
    0x0F1: "GPUREG_TEXENV4_OPERAND",
    0x0F2: "GPUREG_TEXENV4_COMBINER",
    0x0F3: "GPUREG_TEXENV4_COLOR",
    0x0F4: "GPUREG_TEXENV4_SCALE",
    0x0F8: "GPUREG_TEXENV5_SOURCE",
    0x0F9: "GPUREG_TEXENV5_OPERAND",
    0x0FA: "GPUREG_TEXENV5_COMBINER",
    0x0FB: "GPUREG_TEXENV5_COLOR",
    0x0FC: "GPUREG_TEXENV5_SCALE",
    0x0FD: "GPUREG_TEXENV_BUFFER_COLOR",
    0x100: "GPUREG_COLOR_OPERATION",
    0x101: "GPUREG_BLEND_FUNC",
    0x102: "GPUREG_LOGIC_OP",
    0x103: "GPUREG_BLEND_COLOR",
    0x104: "GPUREG_FRAGOP_ALPHA_TEST",
    0x107: "GPUREG_DEPTH_COLOR_MASK",
}

BLEND_EQUATIONS = {
    0: "Add",
    1: "Subtract",
    2: "ReverseSubtract",
    3: "Min",
    4: "Max",
}

BLEND_FACTORS = {
    0: "Zero",
    1: "One",
    2: "SourceColor",
    3: "OneMinusSourceColor",
    4: "DestColor",
    5: "OneMinusDestColor",
    6: "SourceAlpha",
    7: "OneMinusSourceAlpha",
    8: "DestAlpha",
    9: "OneMinusDestAlpha",
    10: "ConstantColor",
    11: "OneMinusConstantColor",
    12: "ConstantAlpha",
    13: "OneMinusConstantAlpha",
    14: "SourceAlphaSaturate",
}

COMPARE_FUNCS = {
    0: "Never",
    1: "Always",
    2: "Equal",
    3: "NotEqual",
    4: "LessThan",
    5: "LessThanOrEqual",
    6: "GreaterThan",
    7: "GreaterThanOrEqual",
}

FRAGMENT_OPERATION_MODES = {
    0: "Default",
    1: "Gas",
    3: "Shadow",
}

LOGIC_OPS = {
    0: "Clear",
    1: "And",
    2: "AndReverse",
    3: "Copy",
    4: "Set",
    5: "CopyInverted",
    6: "NoOp",
    7: "Invert",
    8: "Nand",
    9: "Or",
    10: "Nor",
    11: "Xor",
    12: "Equiv",
    13: "AndInverted",
    14: "OrReverse",
    15: "OrInverted",
}

TEXTURE_FORMATS = {
    0: "RGBA8",
    1: "RGB8",
    2: "RGB5A1",
    3: "RGB565",
    4: "RGBA4",
    5: "IA8",
    6: "RG8",
    7: "I8",
    8: "A8",
    9: "IA4",
    10: "I4",
    11: "A4",
    12: "ETC1",
    13: "ETC1A4",
}

TEXENV_SOURCES = {
    0x0: "PrimaryColor",
    0x1: "PrimaryFragmentColor",
    0x2: "SecondaryFragmentColor",
    0x3: "Texture0",
    0x4: "Texture1",
    0x5: "Texture2",
    0x6: "Texture3",
    0xD: "PreviousBuffer",
    0xE: "Constant",
    0xF: "Previous",
}

COLOR_MODIFIERS = {
    0x0: "SourceColor",
    0x1: "OneMinusSourceColor",
    0x2: "SourceAlpha",
    0x3: "OneMinusSourceAlpha",
    0x4: "SourceRed",
    0x5: "OneMinusSourceRed",
    0x8: "SourceGreen",
    0x9: "OneMinusSourceGreen",
    0xC: "SourceBlue",
    0xD: "OneMinusSourceBlue",
}

ALPHA_MODIFIERS = {
    0x0: "SourceAlpha",
    0x1: "OneMinusSourceAlpha",
    0x2: "SourceRed",
    0x3: "OneMinusSourceRed",
    0x4: "SourceGreen",
    0x5: "OneMinusSourceGreen",
    0x6: "SourceBlue",
    0x7: "OneMinusSourceBlue",
}

TEXENV_OPERATIONS = {
    0: "Replace",
    1: "Modulate",
    2: "Add",
    3: "AddSigned",
    4: "Lerp",
    5: "Subtract",
    6: "Dot3_RGB",
    7: "Dot3_RGBA",
    8: "MultiplyThenAdd",
    9: "AddThenMultiply",
}

FOG_MODES = {
    0: "None",
    5: "Fog",
    7: "Gas",
}


def parse_u32(value: Any) -> int | None:
    if value is None:
        return None
    if isinstance(value, int):
        return value & 0xFFFFFFFF
    if isinstance(value, str):
        return int(value, 16) & 0xFFFFFFFF
    return None


def hex_u32(value: int | None) -> str | None:
    return None if value is None else f"0x{value & 0xFFFFFFFF:08X}"


def enum_name(table: dict[int, str], value: int | None) -> str | None:
    if value is None:
        return None
    return table.get(value, f"Unknown({value})")


def decode_color(value: int | None) -> dict[str, Any] | None:
    if value is None:
        return None
    return {
        "raw": hex_u32(value),
        "r": value & 0xFF,
        "g": (value >> 8) & 0xFF,
        "b": (value >> 16) & 0xFF,
        "a": (value >> 24) & 0xFF,
    }


def decode_color_operation(value: int | None) -> dict[str, Any] | None:
    if value is None:
        return None
    mode = value & 0x3
    return {
        "raw": hex_u32(value),
        "fragment_operation_mode": mode,
        "fragment_operation_mode_name": enum_name(FRAGMENT_OPERATION_MODES, mode),
        "alphablend_enable": (value >> 8) & 0x1,
    }


def decode_blend_func(value: int | None) -> dict[str, Any] | None:
    if value is None:
        return None
    eq_rgb = value & 0x7
    eq_a = (value >> 8) & 0x7
    src_rgb = (value >> 16) & 0xF
    dst_rgb = (value >> 20) & 0xF
    src_a = (value >> 24) & 0xF
    dst_a = (value >> 28) & 0xF
    return {
        "raw": hex_u32(value),
        "equation_rgb": eq_rgb,
        "equation_rgb_name": enum_name(BLEND_EQUATIONS, eq_rgb),
        "equation_a": eq_a,
        "equation_a_name": enum_name(BLEND_EQUATIONS, eq_a),
        "src_rgb": src_rgb,
        "src_rgb_name": enum_name(BLEND_FACTORS, src_rgb),
        "dst_rgb": dst_rgb,
        "dst_rgb_name": enum_name(BLEND_FACTORS, dst_rgb),
        "src_a": src_a,
        "src_a_name": enum_name(BLEND_FACTORS, src_a),
        "dst_a": dst_a,
        "dst_a_name": enum_name(BLEND_FACTORS, dst_a),
    }


def decode_logic_op(value: int | None) -> dict[str, Any] | None:
    if value is None:
        return None
    op = value & 0xF
    return {"raw": hex_u32(value), "op": op, "op_name": enum_name(LOGIC_OPS, op)}


def decode_alpha_test(value: int | None) -> dict[str, Any] | None:
    if value is None:
        return None
    func = (value >> 4) & 0x7
    return {
        "raw": hex_u32(value),
        "enable": value & 0x1,
        "func": func,
        "func_name": enum_name(COMPARE_FUNCS, func),
        "ref": (value >> 8) & 0xFF,
    }


def decode_depth_color_mask(value: int | None) -> dict[str, Any] | None:
    if value is None:
        return None
    return {
        "raw": hex_u32(value),
        "depth_test_enable": value & 0x1,
        "depth_test_func": enum_name(COMPARE_FUNCS, (value >> 4) & 0x7),
        "red_enable": (value >> 8) & 0x1,
        "green_enable": (value >> 9) & 0x1,
        "blue_enable": (value >> 10) & 0x1,
        "alpha_enable": (value >> 11) & 0x1,
        "depth_write_enable": (value >> 12) & 0x1,
    }


def decode_texenv(source: int | None, operand: int | None, combiner: int | None,
                  color: int | None, scale: int | None) -> dict[str, Any]:
    def source_field(raw: int | None, shift: int) -> dict[str, Any] | None:
        if raw is None:
            return None
        value = (raw >> shift) & 0xF
        return {"value": value, "name": enum_name(TEXENV_SOURCES, value)}

    def color_modifier(raw: int | None, shift: int) -> dict[str, Any] | None:
        if raw is None:
            return None
        value = (raw >> shift) & 0xF
        return {"value": value, "name": enum_name(COLOR_MODIFIERS, value)}

    def alpha_modifier(raw: int | None, shift: int) -> dict[str, Any] | None:
        if raw is None:
            return None
        value = (raw >> shift) & 0x7
        return {"value": value, "name": enum_name(ALPHA_MODIFIERS, value)}

    color_op = None if combiner is None else combiner & 0xF
    alpha_op = None if combiner is None else (combiner >> 16) & 0xF
    color_scale = None if scale is None else scale & 0x3
    alpha_scale = None if scale is None else (scale >> 16) & 0x3
    return {
        "source_raw": hex_u32(source),
        "operand_raw": hex_u32(operand),
        "combiner_raw": hex_u32(combiner),
        "const_color": decode_color(color),
        "scale_raw": hex_u32(scale),
        "color_sources": [
            source_field(source, 0),
            source_field(source, 4),
            source_field(source, 8),
        ],
        "alpha_sources": [
            source_field(source, 16),
            source_field(source, 20),
            source_field(source, 24),
        ],
        "color_modifiers": [
            color_modifier(operand, 0),
            color_modifier(operand, 4),
            color_modifier(operand, 8),
        ],
        "alpha_modifiers": [
            alpha_modifier(operand, 12),
            alpha_modifier(operand, 16),
            alpha_modifier(operand, 20),
        ],
        "color_op": color_op,
        "color_op_name": enum_name(TEXENV_OPERATIONS, color_op),
        "alpha_op": alpha_op,
        "alpha_op_name": enum_name(TEXENV_OPERATIONS, alpha_op),
        "color_scale": color_scale,
        "alpha_scale": alpha_scale,
    }


def decode_texenv_update_buffer(value: int | None) -> dict[str, Any] | None:
    if value is None:
        return None
    fog_mode = value & 0x7
    return {
        "raw": hex_u32(value),
        "fog_mode": fog_mode,
        "fog_mode_name": enum_name(FOG_MODES, fog_mode),
        "fog_flip": (value >> 16) & 0x1,
        "update_mask_rgb": (value >> 8) & 0xF,
        "update_mask_a": (value >> 12) & 0xF,
    }


def snapshot_value(draw: dict[str, Any], reg_id: int) -> int | None:
    snapshot = draw.get("register_snapshot") or {}
    for section in snapshot.values():
        if not isinstance(section, dict):
            continue
        first = parse_u32(section.get("first"))
        last = parse_u32(section.get("last"))
        values = section.get("values") or []
        if first is None or last is None or not (first <= reg_id <= last):
            continue
        index = reg_id - first
        if index < 0 or index >= len(values):
            continue
        return parse_u32(values[index])
    return None


def collect_registers(draw: dict[str, Any], stream_state: dict[int, int]) -> dict[str, int | None]:
    values: dict[str, int | None] = {}
    for reg_id, name in REG_NAMES.items():
        value = snapshot_value(draw, reg_id)
        if value is None:
            value = stream_state.get(reg_id)
        values[name] = value
    return values


def texture_summary(textures: list[dict[str, Any]]) -> list[dict[str, Any]]:
    out = []
    for texture in textures:
        fmt = texture.get("format")
        out.append(
            {
                "index": texture.get("index"),
                "enabled": texture.get("enabled"),
                "format": fmt,
                "format_name": enum_name(TEXTURE_FORMATS, fmt) if isinstance(fmt, int) else None,
                "type": texture.get("type"),
                "width": texture.get("width"),
                "height": texture.get("height"),
                "address": texture.get("address"),
                "register_base": texture.get("register_base"),
            }
        )
    return out


def is_fast3d_set_use_alpha_compatible(color_operation: dict[str, Any] | None,
                                       blend: dict[str, Any] | None) -> bool:
    if not color_operation or not blend:
        return False
    if color_operation.get("alphablend_enable") != 1:
        return False
    return (
        blend.get("equation_rgb_name") == "Add"
        and blend.get("equation_a_name") == "Add"
        and blend.get("src_rgb_name") == "SourceAlpha"
        and blend.get("dst_rgb_name") == "OneMinusSourceAlpha"
        and blend.get("src_a_name") == "SourceAlpha"
        and blend.get("dst_a_name") == "OneMinusSourceAlpha"
    )


def is_opaque_replace_compatible(blend: dict[str, Any] | None) -> bool:
    if not blend:
        return False
    return (
        blend.get("equation_rgb_name") == "Add"
        and blend.get("equation_a_name") == "Add"
        and blend.get("src_rgb_name") == "One"
        and blend.get("dst_rgb_name") == "Zero"
        and blend.get("src_a_name") == "One"
        and blend.get("dst_a_name") == "Zero"
    )


def classify_draw(draw: dict[str, Any], max_draw_index: int, tail_draws: int,
                  interesting_vertices: set[int]) -> list[str]:
    reasons = []
    draw_index = int(draw.get("draw_index") or -1)
    if draw_index >= max_draw_index - tail_draws + 1:
        reasons.append("tail_draw")
    if int(draw.get("num_vertices") or -1) in interesting_vertices:
        reasons.append("interesting_vertex_count")
    enabled_textures = [texture for texture in draw.get("textures") or [] if texture.get("enabled")]
    if any(texture.get("width") == 128 and texture.get("height") == 128 for texture in enabled_textures):
        reasons.append("enabled_128x128_texture")
    if any(texture.get("format") in (5, 8, 9, 11, 13) for texture in enabled_textures):
        reasons.append("texture_format_has_or_encodes_alpha")
    return reasons


def decode_draw(draw: dict[str, Any], stream_state: dict[int, int], max_draw_index: int,
                tail_draws: int, interesting_vertices: set[int]) -> dict[str, Any]:
    regs = collect_registers(draw, stream_state)
    color_operation = decode_color_operation(regs["GPUREG_COLOR_OPERATION"])
    blend = decode_blend_func(regs["GPUREG_BLEND_FUNC"])
    logic = decode_logic_op(regs["GPUREG_LOGIC_OP"])
    alpha_test = decode_alpha_test(regs["GPUREG_FRAGOP_ALPHA_TEST"])
    depth_color_mask = decode_depth_color_mask(regs["GPUREG_DEPTH_COLOR_MASK"])
    texenv_update = decode_texenv_update_buffer(regs["GPUREG_TEXENV_UPDATE_BUFFER"])
    fog_color = decode_color(regs["GPUREG_FOG_COLOR"])

    texenv_stages = []
    for stage in range(6):
        base = 0x0C0 + stage * 8
        if stage >= 4:
            base = 0x0F0 + (stage - 4) * 8
        texenv_stages.append(
            {
                "stage": stage,
                **decode_texenv(
                    regs.get(REG_NAMES[base]),
                    regs.get(REG_NAMES[base + 1]),
                    regs.get(REG_NAMES[base + 2]),
                    regs.get(REG_NAMES[base + 3]),
                    regs.get(REG_NAMES[base + 4]),
                ),
            }
        )

    fast3d_normal_alpha = is_fast3d_set_use_alpha_compatible(color_operation, blend)
    opaque_replace_compatible = is_opaque_replace_compatible(blend)
    alpha_blend_enabled = color_operation and color_operation.get("alphablend_enable") == 1
    requires_native_blend_state = bool(
        alpha_blend_enabled and not fast3d_normal_alpha and not opaque_replace_compatible
    )
    alpha_test_enabled = bool(alpha_test and alpha_test.get("enable") == 1)
    candidate_reasons = classify_draw(draw, max_draw_index, tail_draws, interesting_vertices)
    reasons = list(candidate_reasons)
    if requires_native_blend_state:
        reasons.append("requires_native_blend_state")
        candidate_reasons.append("requires_native_blend_state")
    if alpha_test_enabled:
        reasons.append("alpha_test_enabled")
        candidate_reasons.append("alpha_test_enabled")
    if texenv_update and texenv_update.get("fog_mode_name") in ("Fog", "Gas"):
        reasons.append("fog_or_gas_enabled")

    return {
        "draw_index": draw.get("draw_index"),
        "cmd_list_addr": draw.get("cmd_list_addr"),
        "cmd_list_offset_words": draw.get("cmd_list_offset_words"),
        "num_vertices": draw.get("num_vertices"),
        "triangle_topology": draw.get("triangle_topology"),
        "is_indexed": draw.get("is_indexed"),
        "vertex_base_addr": draw.get("vertex_base_addr"),
        "textures": texture_summary(draw.get("textures") or []),
        "output_merger": {
            "color_operation": color_operation,
            "blend_func": blend,
            "logic_op": logic,
            "blend_color": decode_color(regs["GPUREG_BLEND_COLOR"]),
            "alpha_test": alpha_test,
            "depth_color_mask": depth_color_mask,
            "fast3d_set_use_alpha_normal_compatible": fast3d_normal_alpha,
            "opaque_replace_compatible": opaque_replace_compatible,
            "requires_native_blend_state": requires_native_blend_state,
        },
        "texture_env": {
            "update_buffer": texenv_update,
            "fog_color": fog_color,
            "buffer_color": decode_color(regs["GPUREG_TEXENV_BUFFER_COLOR"]),
            "stages": texenv_stages,
        },
        "candidate_reasons": reasons,
        "is_candidate": bool(candidate_reasons),
    }


def parse_trace(path: Path, tail_draws: int, interesting_vertices: set[int]) -> dict[str, Any]:
    stream_state: dict[int, int] = {}
    draw_events: list[tuple[dict[str, Any], dict[int, int]]] = []
    write_counts: Counter[str] = Counter()
    capture_begin: dict[str, Any] | None = None
    command_list_begin: dict[str, Any] | None = None

    with path.open(encoding="utf-8", errors="replace") as handle:
        for line_number, line in enumerate(handle, 1):
            event = json.loads(line)
            kind = event.get("event")
            if kind == "capture_begin":
                capture_begin = event
                continue
            if kind == "command_list_begin":
                command_list_begin = event
                continue
            if kind == "register_write":
                reg_id = int(event["id"])
                value = parse_u32(event.get("final_value") or event.get("write_value"))
                if value is not None:
                    stream_state[reg_id] = value
                name = event.get("name") or REG_NAMES.get(reg_id) or f"0x{reg_id:03X}"
                if reg_id in REG_NAMES:
                    write_counts[name] += 1
                continue
            if kind == "draw_begin":
                event["_trace_line"] = line_number
                draw_events.append((event, dict(stream_state)))

    max_draw_index = max((int(draw.get("draw_index") or -1) for draw, _state in draw_events), default=-1)
    decoded_draws = [
        decode_draw(draw, draw_state, max_draw_index, tail_draws, interesting_vertices)
        for draw, draw_state in draw_events
    ]
    candidates = [draw for draw in decoded_draws if draw.get("is_candidate")]
    blend_state_counts = Counter(
        (draw["output_merger"]["blend_func"] or {}).get("raw") or "missing"
        for draw in decoded_draws
    )
    color_operation_counts = Counter(
        (draw["output_merger"]["color_operation"] or {}).get("raw") or "missing"
        for draw in decoded_draws
    )

    return {
        "format": "oot3d_title_intro_pica_output_merger_trace_v1",
        "source_path": str(path),
        "source_kind": "azahar_pica_frame_jsonl",
        "azahar_reference_sources": {
            "framebuffer_regs": "E:/azahar pcvr/src/video_core/pica/regs_framebuffer.h",
            "texturing_regs": "E:/azahar pcvr/src/video_core/pica/regs_texturing.h",
            "opengl_mapping": "E:/azahar pcvr/src/video_core/renderer_opengl/pica_to_gl.h",
            "opengl_state_sync": "E:/azahar pcvr/src/video_core/renderer_opengl/gl_rasterizer.cpp",
        },
        "capture_begin": capture_begin,
        "command_list_begin": command_list_begin,
        "draw_count": len(decoded_draws),
        "candidate_count": len(candidates),
        "write_counts_for_decoded_registers": dict(write_counts),
        "blend_func_counts": dict(blend_state_counts),
        "color_operation_counts": dict(color_operation_counts),
        "candidate_draws": candidates,
        "draws": decoded_draws,
        "conclusions": build_conclusions(decoded_draws, candidates),
    }


def build_conclusions(draws: list[dict[str, Any]], candidates: list[dict[str, Any]]) -> list[str]:
    conclusions = []
    native_blend_candidates = [
        draw for draw in candidates
        if draw["output_merger"]["requires_native_blend_state"]
    ]
    normal_alpha_candidates = [
        draw for draw in candidates
        if draw["output_merger"]["fast3d_set_use_alpha_normal_compatible"]
    ]
    alpha_blend_draws = [
        draw for draw in draws
        if (draw["output_merger"]["color_operation"] or {}).get("alphablend_enable") == 1
    ]
    if native_blend_candidates:
        indices = ", ".join(str(draw["draw_index"]) for draw in native_blend_candidates[:16])
        conclusions.append(
            "Some title-intro overlay/effect candidates require native PICA blend state beyond "
            f"Fast3D SetUseAlpha(bool): draw(s) {indices}."
        )
    if normal_alpha_candidates:
        indices = ", ".join(str(draw["draw_index"]) for draw in normal_alpha_candidates[:16])
        conclusions.append(
            "Some candidates use ordinary source-alpha blending and can share the native state "
            f"path once TextureEnv supplies the correct fragment alpha: draw(s) {indices}."
        )
    if alpha_blend_draws:
        conclusions.append(
            "The OOT3D path controls visibility through output-merger state on draw packets; "
            "backend submission must carry decoded COLOR_OPERATION/BLEND_FUNC/BLEND_COLOR/"
            "FRAGOP_ALPHA_TEST rather than a single boolean alpha switch."
        )
    return conclusions


def compact_texture_cell(draw: dict[str, Any]) -> str:
    cells = []
    for texture in draw.get("textures") or []:
        if not texture.get("enabled"):
            continue
        cells.append(
            "t{index}:{format_name}:{width}x{height}@{address}".format(
                index=texture.get("index"),
                format_name=texture.get("format_name"),
                width=texture.get("width"),
                height=texture.get("height"),
                address=texture.get("address"),
            )
        )
    return ", ".join(cells) if cells else "-"


def write_csv(path: Path, decoded: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "draw_index",
        "cmd_list_offset_words",
        "num_vertices",
        "triangle_topology",
        "textures",
        "color_operation",
        "alphablend_enable",
        "blend_func",
        "blend_equation_rgb",
        "blend_src_rgb",
        "blend_dst_rgb",
        "blend_equation_a",
        "blend_src_a",
        "blend_dst_a",
        "fast3d_normal_alpha_compatible",
        "requires_native_blend_state",
        "alpha_test",
        "texenv0_source",
        "texenv0_operand",
        "texenv0_combiner",
        "texenv0_color",
        "fog_mode",
        "candidate_reasons",
    ]
    with path.open("w", newline="", encoding="ascii") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for draw in decoded["candidate_draws"]:
            output = draw["output_merger"]
            color = output["color_operation"] or {}
            blend = output["blend_func"] or {}
            alpha_test = output["alpha_test"] or {}
            texenv0 = draw["texture_env"]["stages"][0]
            update = draw["texture_env"]["update_buffer"] or {}
            writer.writerow(
                {
                    "draw_index": draw.get("draw_index"),
                    "cmd_list_offset_words": draw.get("cmd_list_offset_words"),
                    "num_vertices": draw.get("num_vertices"),
                    "triangle_topology": draw.get("triangle_topology"),
                    "textures": compact_texture_cell(draw),
                    "color_operation": color.get("raw"),
                    "alphablend_enable": color.get("alphablend_enable"),
                    "blend_func": blend.get("raw"),
                    "blend_equation_rgb": blend.get("equation_rgb_name"),
                    "blend_src_rgb": blend.get("src_rgb_name"),
                    "blend_dst_rgb": blend.get("dst_rgb_name"),
                    "blend_equation_a": blend.get("equation_a_name"),
                    "blend_src_a": blend.get("src_a_name"),
                    "blend_dst_a": blend.get("dst_a_name"),
                    "fast3d_normal_alpha_compatible": output.get(
                        "fast3d_set_use_alpha_normal_compatible"
                    ),
                    "requires_native_blend_state": output.get("requires_native_blend_state"),
                    "alpha_test": alpha_test.get("raw"),
                    "texenv0_source": texenv0.get("source_raw"),
                    "texenv0_operand": texenv0.get("operand_raw"),
                    "texenv0_combiner": texenv0.get("combiner_raw"),
                    "texenv0_color": (texenv0.get("const_color") or {}).get("raw"),
                    "fog_mode": update.get("fog_mode_name"),
                    "candidate_reasons": ";".join(draw.get("candidate_reasons") or []),
                }
            )


def blend_short(draw: dict[str, Any]) -> str:
    blend = draw["output_merger"]["blend_func"] or {}
    return (
        f"{blend.get('equation_rgb_name')} "
        f"{blend.get('src_rgb_name')}->{blend.get('dst_rgb_name')} / "
        f"{blend.get('equation_a_name')} "
        f"{blend.get('src_a_name')}->{blend.get('dst_a_name')}"
    )


def write_markdown(path: Path, decoded: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# Title intro PICA output-merger trace",
        "",
        "This report decodes draw-time PICA framebuffer output-merger and TextureEnv state "
        "from the Azahar slot 6 title-intro capture. Emulator data is validation evidence; "
        "runtime implementation must still be sourced from OOT3D assets/code paths.",
        "",
        "## Inputs",
        "",
        f"- Trace: `{decoded['source_path']}`",
        "- Azahar framebuffer bitfields: `E:/azahar pcvr/src/video_core/pica/regs_framebuffer.h`",
        "- Azahar TextureEnv bitfields: `E:/azahar pcvr/src/video_core/pica/regs_texturing.h`",
        "- Azahar OpenGL mapping: `E:/azahar pcvr/src/video_core/renderer_opengl/pica_to_gl.h`",
        "",
        "## Conclusions",
        "",
    ]
    if decoded.get("conclusions"):
        lines.extend(f"- {item}" for item in decoded["conclusions"])
    else:
        lines.append("- No candidate draw required native blend state beyond the current generic path.")
    lines.extend(
        [
            "",
            "## Candidate draws",
            "",
            "| Draw | Off | Verts | Topo | Textures | ColorOp | Blend | AlphaTest | TexEnv0 | Fog | Native blend needed | Reasons |",
            "| ---: | ---: | ---: | ---: | --- | --- | --- | --- | --- | --- | --- | --- |",
        ]
    )
    for draw in decoded["candidate_draws"]:
        output = draw["output_merger"]
        color = output["color_operation"] or {}
        alpha_test = output["alpha_test"] or {}
        texenv0 = draw["texture_env"]["stages"][0]
        update = draw["texture_env"]["update_buffer"] or {}
        lines.append(
            "| {draw} | {off} | {verts} | {topo} | {textures} | {color} a={alpha} | {blend} | {atest} | {src}/{op}/{comb}/{const} | {fog} | {native} | {reasons} |".format(
                draw=draw.get("draw_index"),
                off=draw.get("cmd_list_offset_words"),
                verts=draw.get("num_vertices"),
                topo=draw.get("triangle_topology"),
                textures=compact_texture_cell(draw),
                color=color.get("raw"),
                alpha=color.get("alphablend_enable"),
                blend=blend_short(draw),
                atest=alpha_test.get("raw"),
                src=texenv0.get("source_raw"),
                op=texenv0.get("operand_raw"),
                comb=texenv0.get("combiner_raw"),
                const=(texenv0.get("const_color") or {}).get("raw"),
                fog=update.get("fog_mode_name"),
                native=output.get("requires_native_blend_state"),
                reasons=", ".join(draw.get("candidate_reasons") or []),
            )
        )
    lines.extend(
        [
            "",
            "## Backend implication",
            "",
            "The existing Fast3D abstraction exposes a boolean alpha switch, while these draw packets "
            "carry independent PICA color operation, blend equation/factors, blend constant, alpha "
            "test and TextureEnv alpha/color expressions. The next aligned engine step is a typed "
            "native PICA output-merger state on the OOT3D render path, mapped to OpenGL with the "
            "same equations/factors Azahar uses, then consumed by Kankyo/lens/logo draw submission.",
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="ascii")


def parse_vertex_counts(value: str) -> set[int]:
    return {int(part.strip()) for part in value.split(",") if part.strip()}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--trace", type=Path, default=DEFAULT_CAPTURE)
    parser.add_argument("--output-json", type=Path, default=DEFAULT_OUTPUT_JSON)
    parser.add_argument("--output-csv", type=Path, default=DEFAULT_OUTPUT_CSV)
    parser.add_argument("--output-md", type=Path, default=DEFAULT_OUTPUT_MD)
    parser.add_argument("--tail-draws", type=int, default=12)
    parser.add_argument("--interesting-vertices", default="6,12,78,204")
    args = parser.parse_args()

    interesting_vertices = parse_vertex_counts(args.interesting_vertices)
    decoded = parse_trace(args.trace, args.tail_draws, interesting_vertices)

    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(decoded, indent=2), encoding="ascii")
    write_csv(args.output_csv, decoded)
    write_markdown(args.output_md, decoded)
    print(
        json.dumps(
            {
                "trace": str(args.trace),
                "output_json": str(args.output_json),
                "output_csv": str(args.output_csv),
                "output_md": str(args.output_md),
                "draw_count": decoded["draw_count"],
                "candidate_count": decoded["candidate_count"],
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
