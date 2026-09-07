#!/usr/bin/env python3
"""Normalize Azahar PICA command-list traces for the OOT3D native demo."""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import sys
from pathlib import Path
from typing import Any


TRACE_FORMAT = "oot3d_native_pica_register_trace_v1"
SOURCE_KIND_AZAHAR_COPY_ALL = "azahar_pica_command_list_copy_all"
SOURCE_KIND_AZAHAR_FRAME_JSONL = "azahar_pica_frame_jsonl"
SOURCE_KIND_JSON = "oot3d_native_pica_register_trace_json"

REG_TEXUNIT0_SHADOW = 0x08B
REG_LIGHTING_ENABLE0 = 0x08F
REG_FRAGOP_SHADOW = 0x130
REG_LIGHTING_CONFIG0 = 0x1C3
REG_LIGHTING_CONFIG1 = 0x1C4
REG_DEPTHMAP_SCALE = 0x04D
REG_DEPTHMAP_OFFSET = 0x04E
REG_TEXUNIT0_DIM = 0x082
REG_TEXUNIT0_PARAM = 0x083
REG_TEXUNIT1_DIM = 0x092
REG_TEXUNIT1_PARAM = 0x093
REG_TEXUNIT2_DIM = 0x09A
REG_TEXUNIT2_PARAM = 0x09B
REG_TEXENV0_SOURCE = 0x0C0
REG_TEXENV0_OPERAND = 0x0C1
REG_TEXENV0_COMBINER = 0x0C2
REG_TEXENV0_COLOR = 0x0C3
REG_TEXENV0_SCALE = 0x0C4
REG_TEXENV1_SOURCE = 0x0C8
REG_TEXENV1_OPERAND = 0x0C9
REG_TEXENV1_COMBINER = 0x0CA
REG_TEXENV1_COLOR = 0x0CB
REG_TEXENV1_SCALE = 0x0CC
REG_TEXENV2_SOURCE = 0x0D0
REG_TEXENV2_OPERAND = 0x0D1
REG_TEXENV2_COMBINER = 0x0D2
REG_TEXENV2_COLOR = 0x0D3
REG_TEXENV2_SCALE = 0x0D4
REG_TEXENV3_SOURCE = 0x0D8
REG_TEXENV3_OPERAND = 0x0D9
REG_TEXENV3_COMBINER = 0x0DA
REG_TEXENV3_COLOR = 0x0DB
REG_TEXENV3_SCALE = 0x0DC
REG_TEXENV_UPDATE_BUFFER = 0x0E0
REG_FOG_COLOR = 0x0E1
REG_FOG_LUT_INDEX = 0x0E6
REG_FOG_LUT_DATA0 = 0x0E8
REG_FOG_LUT_DATA7 = 0x0EF
REG_TEXENV4_SOURCE = 0x0F0
REG_TEXENV4_OPERAND = 0x0F1
REG_TEXENV4_COMBINER = 0x0F2
REG_TEXENV4_COLOR = 0x0F3
REG_TEXENV4_SCALE = 0x0F4
REG_TEXENV5_SOURCE = 0x0F8
REG_TEXENV5_OPERAND = 0x0F9
REG_TEXENV5_COMBINER = 0x0FA
REG_TEXENV5_COLOR = 0x0FB
REG_TEXENV5_SCALE = 0x0FC
REG_TEXENV_BUFFER_COLOR = 0x0FD
REG_GSH_FLOATUNIFORM_INDEX = 0x290
REG_GSH_FLOATUNIFORM_DATA0 = 0x291
REG_GSH_FLOATUNIFORM_DATA7 = 0x298
REG_VSH_FLOATUNIFORM_INDEX = 0x2C0
REG_VSH_FLOATUNIFORM_DATA0 = 0x2C1
REG_VSH_FLOATUNIFORM_DATA7 = 0x2C8

REGISTER_NAMES = {
    REG_DEPTHMAP_SCALE: "GPUREG_DEPTHMAP_SCALE",
    REG_DEPTHMAP_OFFSET: "GPUREG_DEPTHMAP_OFFSET",
    REG_TEXUNIT0_DIM: "GPUREG_TEXUNIT0_DIM",
    REG_TEXUNIT0_PARAM: "GPUREG_TEXUNIT0_PARAM",
    REG_TEXUNIT0_SHADOW: "GPUREG_TEXUNIT0_SHADOW",
    REG_LIGHTING_ENABLE0: "GPUREG_LIGHTING_ENABLE0",
    REG_TEXUNIT1_DIM: "GPUREG_TEXUNIT1_DIM",
    REG_TEXUNIT1_PARAM: "GPUREG_TEXUNIT1_PARAM",
    REG_TEXUNIT2_DIM: "GPUREG_TEXUNIT2_DIM",
    REG_TEXUNIT2_PARAM: "GPUREG_TEXUNIT2_PARAM",
    REG_TEXENV_UPDATE_BUFFER: "GPUREG_TEXENV_UPDATE_BUFFER",
    REG_FOG_COLOR: "GPUREG_FOG_COLOR",
    REG_FOG_LUT_INDEX: "GPUREG_FOG_LUT_INDEX",
    REG_TEXENV_BUFFER_COLOR: "GPUREG_TEXENV_BUFFER_COLOR",
    REG_FRAGOP_SHADOW: "GPUREG_FRAGOP_SHADOW",
    REG_LIGHTING_CONFIG0: "GPUREG_LIGHTING_CONFIG0",
    REG_LIGHTING_CONFIG1: "GPUREG_LIGHTING_CONFIG1",
    REG_GSH_FLOATUNIFORM_INDEX: "GPUREG_GSH_FLOATUNIFORM_INDEX",
    REG_VSH_FLOATUNIFORM_INDEX: "GPUREG_VSH_FLOATUNIFORM_INDEX",
}
for stage, base in enumerate(
    (REG_TEXENV0_SOURCE, REG_TEXENV1_SOURCE, REG_TEXENV2_SOURCE,
     REG_TEXENV3_SOURCE, REG_TEXENV4_SOURCE, REG_TEXENV5_SOURCE)
):
    REGISTER_NAMES[base + 0] = f"GPUREG_TEXENV{stage}_SOURCE"
    REGISTER_NAMES[base + 1] = f"GPUREG_TEXENV{stage}_OPERAND"
    REGISTER_NAMES[base + 2] = f"GPUREG_TEXENV{stage}_COMBINER"
    REGISTER_NAMES[base + 3] = f"GPUREG_TEXENV{stage}_COLOR"
    REGISTER_NAMES[base + 4] = f"GPUREG_TEXENV{stage}_SCALE"
for offset in range(REG_FOG_LUT_DATA7 - REG_FOG_LUT_DATA0 + 1):
    REGISTER_NAMES[REG_FOG_LUT_DATA0 + offset] = f"GPUREG_FOG_LUT_DATA{offset}"
for offset in range(8):
    REGISTER_NAMES[REG_GSH_FLOATUNIFORM_DATA0 + offset] = f"GPUREG_GSH_FLOATUNIFORM_DATA{offset}"
    REGISTER_NAMES[REG_VSH_FLOATUNIFORM_DATA0 + offset] = f"GPUREG_VSH_FLOATUNIFORM_DATA{offset}"


def format_reg(value: int) -> str:
    return f"0x{value:03X}"


def format_u32(value: int) -> str:
    return f"0x{value & 0xFFFFFFFF:08X}"


def register_name(reg: int) -> str:
    return REGISTER_NAMES.get(reg, format_reg(reg))


def parse_int(value: Any, *, default_base: int = 0, field: str = "value") -> int:
    if isinstance(value, bool):
        raise ValueError(f"{field} is boolean, expected integer")
    if isinstance(value, int):
        return value
    if not isinstance(value, str):
        raise ValueError(f"{field} is not an integer-like value: {value!r}")
    text = value.strip()
    if not text:
        raise ValueError(f"{field} is empty")
    if default_base:
        return int(text, default_base)
    return int(text, 0)


def parse_register(value: Any) -> int:
    if isinstance(value, str):
        text = value.strip()
        if text.upper().startswith("GPUREG_"):
            for reg, name in REGISTER_NAMES.items():
                if name == text.upper():
                    return reg
        if re.fullmatch(r"[0-9a-fA-F]{1,4}", text):
            return int(text, 16)
    return parse_int(value, field="register")


def parse_mask(value: Any) -> int:
    if isinstance(value, str):
        text = value.strip()
        if re.fullmatch(r"[01]{1,4}", text):
            return int(text, 2)
        if re.fullmatch(r"[0-9a-fA-F]{1,2}", text):
            return int(text, 16)
    return parse_int(value, field="mask")


def parse_u32_hex(value: Any, *, field: str) -> int:
    if isinstance(value, str):
        text = value.strip()
        if re.fullmatch(r"[0-9a-fA-F]{1,8}", text):
            return int(text, 16)
    return parse_int(value, field=field)


def decode_float1_5_10(raw: int) -> float | str:
    raw &= 0xFFFF
    sign = -1.0 if raw & 0x8000 else 1.0
    exponent = (raw >> 10) & 0x1F
    fraction = raw & 0x03FF
    if exponent == 0:
        value = 0.0 if fraction == 0 else math.ldexp(fraction / 1024.0, -14)
    elif exponent == 0x1F:
        return "-inf" if sign < 0 and fraction == 0 else "inf" if fraction == 0 else "nan"
    else:
        value = math.ldexp(1.0 + fraction / 1024.0, exponent - 15)
    return sign * value


def decode_texture_shadow(raw: int) -> dict[str, Any]:
    bias_raw = (raw >> 1) & 0x7FFFFF
    return {
        "register": "GPUREG_TEXUNIT0_SHADOW",
        "register_index": format_reg(REG_TEXUNIT0_SHADOW),
        "raw": format_u32(raw),
        "orthographic": bool(raw & 0x1),
        "bias_raw": bias_raw,
        "compare_bias": bias_raw << 1,
    }


def decode_framebuffer_shadow(raw: int) -> dict[str, Any]:
    constant_raw = raw & 0xFFFF
    linear_raw = (raw >> 16) & 0xFFFF
    return {
        "register": "GPUREG_FRAGOP_SHADOW",
        "register_index": format_reg(REG_FRAGOP_SHADOW),
        "raw": format_u32(raw),
        "constant_raw": format_u32(constant_raw),
        "linear_raw": format_u32(linear_raw),
        "constant": decode_float1_5_10(constant_raw),
        "linear": decode_float1_5_10(linear_raw),
    }


def decode_fog_state(registers: dict[str, str]) -> dict[str, Any]:
    mode_raw = registers.get(register_name(REG_TEXENV_UPDATE_BUFFER))
    color_raw = registers.get(register_name(REG_FOG_COLOR))
    lut_index_raw = registers.get(register_name(REG_FOG_LUT_INDEX))
    lut_data = [
        registers.get(register_name(REG_FOG_LUT_DATA0 + offset))
        for offset in range(REG_FOG_LUT_DATA7 - REG_FOG_LUT_DATA0 + 1)
    ]
    out: dict[str, Any] = {
        "available": mode_raw is not None or color_raw is not None or lut_index_raw is not None,
        "source_kind": "oot3d_native_pica_texturing_registers",
        "mode_register": register_name(REG_TEXENV_UPDATE_BUFFER),
        "color_register": register_name(REG_FOG_COLOR),
        "lut_index_register": register_name(REG_FOG_LUT_INDEX),
        "lut_data_registers": [
            register_name(REG_FOG_LUT_DATA0 + offset)
            for offset in range(REG_FOG_LUT_DATA7 - REG_FOG_LUT_DATA0 + 1)
        ],
    }
    if mode_raw is not None:
        raw = parse_u32_hex(mode_raw, field="GPUREG_TEXENV_UPDATE_BUFFER")
        fog_mode = raw & 0x7
        out.update(
            {
                "mode_raw": mode_raw,
                "mode": fog_mode,
                "mode_name": {0: "None", 5: "Fog", 7: "Gas"}.get(fog_mode, f"Unknown({fog_mode})"),
                "fog_enabled": fog_mode == 5,
                "gas_enabled": fog_mode == 7,
                "fog_or_gas_enabled": fog_mode in {5, 7},
                "fog_flip": bool((raw >> 16) & 0x1),
                "tev_update_mask_rgb": (raw >> 8) & 0xF,
                "tev_update_mask_alpha": (raw >> 12) & 0xF,
            }
        )
    if color_raw is not None:
        raw = parse_u32_hex(color_raw, field="GPUREG_FOG_COLOR")
        out.update(
            {
                "color_raw": color_raw,
                "color_rgb_u8": {
                    "r": raw & 0xFF,
                    "g": (raw >> 8) & 0xFF,
                    "b": (raw >> 16) & 0xFF,
                },
            }
        )
    if lut_index_raw is not None:
        raw = parse_u32_hex(lut_index_raw, field="GPUREG_FOG_LUT_INDEX")
        out.update(
            {
                "lut_index_raw": lut_index_raw,
                "lut_offset": raw & 0xFFFF,
            }
        )
    if any(value is not None for value in lut_data):
        out["lut_data_raw"] = lut_data
    return out


def decode_shadow_shader_route(registers: dict[str, str]) -> dict[str, Any]:
    config0_raw = registers.get("GPUREG_LIGHTING_CONFIG0")
    config1_raw = registers.get("GPUREG_LIGHTING_CONFIG1")
    lighting_enable0_raw = registers.get("GPUREG_LIGHTING_ENABLE0")
    out: dict[str, Any] = {
        "fragment_lighting_enable_decoded": lighting_enable0_raw is not None,
        "lighting_config0_decoded": config0_raw is not None,
        "lighting_config1_decoded": config1_raw is not None,
        "shadow_texture_param_decoded": False,
        "shadow_texture_dim_decoded": False,
        "matches_primary_rgb_shadow_term": False,
    }
    if lighting_enable0_raw is not None:
        raw = parse_u32_hex(lighting_enable0_raw, field="GPUREG_LIGHTING_ENABLE0")
        out["fragment_lighting_enabled"] = bool(raw & 0x1)
        out["fragment_lighting_enable_raw"] = lighting_enable0_raw
    if config0_raw is not None:
        raw = parse_u32_hex(config0_raw, field="GPUREG_LIGHTING_CONFIG0")
        selector = (raw >> 24) & 0x3
        out.update(
            {
                "lighting_config0_raw": config0_raw,
                "enable_shadow": bool(raw & 0x1),
                "shadow_primary": bool((raw >> 16) & 0x1),
                "shadow_secondary": bool((raw >> 17) & 0x1),
                "shadow_invert": bool((raw >> 18) & 0x1),
                "shadow_alpha": bool((raw >> 19) & 0x1),
                "shadow_selector": selector,
            }
        )
        param_name = {
            0: "GPUREG_TEXUNIT0_PARAM",
            1: "GPUREG_TEXUNIT1_PARAM",
            2: "GPUREG_TEXUNIT2_PARAM",
        }.get(selector)
        dim_name = {
            0: "GPUREG_TEXUNIT0_DIM",
            1: "GPUREG_TEXUNIT1_DIM",
            2: "GPUREG_TEXUNIT2_DIM",
        }.get(selector)
        if param_name and param_name in registers:
            texture_param_raw = parse_u32_hex(registers[param_name], field=param_name)
            texture_type = (texture_param_raw >> 28) & 0x7
            out.update(
                {
                    "shadow_texture_param_decoded": True,
                    "shadow_texture_param_register": param_name,
                    "shadow_texture_param_raw": registers[param_name],
                    "shadow_texture_type": texture_type,
                    "shadow_texture_is_shadow2d": texture_type == 2,
                }
            )
        if dim_name and dim_name in registers:
            texture_dim_raw = parse_u32_hex(registers[dim_name], field=dim_name)
            out.update(
                {
                    "shadow_texture_dim_decoded": True,
                    "shadow_texture_dim_register": dim_name,
                    "shadow_texture_dim_raw": registers[dim_name],
                    "shadow_texture_width": (texture_dim_raw >> 16) & 0x7FF,
                    "shadow_texture_height": texture_dim_raw & 0x7FF,
                }
            )
    if config1_raw is not None:
        raw = parse_u32_hex(config1_raw, field="GPUREG_LIGHTING_CONFIG1")
        disabled_shadow_mask = raw & 0xFF
        out.update(
            {
                "lighting_config1_raw": config1_raw,
                "disabled_shadow_mask": disabled_shadow_mask,
                "per_light_shadow_enable_mask": (~disabled_shadow_mask) & 0xFF,
            }
        )
    out["matches_primary_rgb_shadow_term"] = bool(
        out.get("fragment_lighting_enabled")
        and out.get("enable_shadow")
        and out.get("shadow_primary")
        and out.get("per_light_shadow_enable_mask", 0) != 0
        and out.get("shadow_texture_is_shadow2d")
    )
    if out["matches_primary_rgb_shadow_term"]:
        out["decode_source"] = "oot3d_native_pica_lighting_shadow_route_register_trace"
    return out


def normalize_write(reg: int, mask: int, value: int, name: str | None = None,
                    metadata: dict[str, Any] | None = None) -> dict[str, Any]:
    out = {
        "name": name or register_name(reg),
        "cmd_id": format_reg(reg),
        "mask": format(mask & 0xF, "04b"),
        "value": format_u32(value),
    }
    if metadata:
        out.update(metadata)
    return out


def expanded_byte_mask(mask: int) -> int:
    expanded = 0
    for byte_index in range(4):
        if mask & (1 << byte_index):
            expanded |= 0xFF << (byte_index * 8)
    return expanded & 0xFFFFFFFF


def load_json_trace(path: Path) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError("JSON trace root must be an object")
    writes: list[dict[str, Any]] = []
    raw_writes = data.get("writes")
    if isinstance(raw_writes, list):
        for index, item in enumerate(raw_writes):
            if not isinstance(item, dict):
                raise ValueError(f"writes[{index}] is not an object")
            reg = parse_register(item.get("cmd_id", item.get("register", item.get("name"))))
            mask = parse_mask(item.get("mask", "1111"))
            value = parse_u32_hex(item.get("value", item.get("final_value")), field="value")
            metadata: dict[str, Any] = {}
            if isinstance(item.get("cmd_list_addr"), str):
                metadata["cmd_list_addr"] = item["cmd_list_addr"]
            if isinstance(item.get("cmd_list_offset_words"), int):
                metadata["cmd_list_offset_words"] = item["cmd_list_offset_words"]
            if isinstance(item.get("cmd_list_word_addr"), str):
                metadata["cmd_list_word_addr"] = item["cmd_list_word_addr"]
            writes.append(normalize_write(reg, mask, value, item.get("name") if isinstance(item.get("name"), str) else None,
                                          metadata))
    return writes, data


def _float_payload(value: Any) -> float | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, (int, float)):
        value = float(value)
        return value if math.isfinite(value) else None
    return None


def _update_min_max(summary: dict[str, Any], key: str, values: list[float]) -> None:
    if "min" not in summary:
        summary["min"] = values[:]
        summary["max"] = values[:]
        return
    summary["min"] = [min(before, value) for before, value in zip(summary["min"], values)]
    summary["max"] = [max(before, value) for before, value in zip(summary["max"], values)]


def _uniform_vec4(item: dict[str, Any], index: int) -> list[float] | None:
    vs_uniforms = item.get("vs_uniforms")
    if not isinstance(vs_uniforms, dict):
        return None
    uniforms = vs_uniforms.get("f")
    if not isinstance(uniforms, list) or index < 0 or index >= len(uniforms):
        return None
    value = uniforms[index]
    if not isinstance(value, list) or len(value) < 4:
        return None
    components = [_float_payload(component) for component in value[:4]]
    if any(component is None for component in components):
        return None
    return [float(component) for component in components if component is not None]


def _vec3_nonzero(value: list[float] | None) -> bool:
    return value is not None and any(abs(component) > 0.000001 for component in value[:3])


def _vec3_neutral(value: list[float] | None) -> bool:
    if value is None:
        return False
    return (
        abs(value[0] - value[1]) <= 0.00001
        and abs(value[1] - value[2]) <= 0.00001
    )


def _vec3_inside_unit_color_range(value: list[float] | None) -> bool:
    if value is None:
        return False
    return all(0.000001 < component < 0.999999 for component in value[:3])


def _color_u8_from_unit_vec4(value: list[float]) -> dict[str, int]:
    return {
        "r": max(0, min(255, round(value[0] * 255.0))),
        "g": max(0, min(255, round(value[1] * 255.0))),
        "b": max(0, min(255, round(value[2] * 255.0))),
        "a": max(0, min(255, round(value[3] * 255.0))),
    }


def _draw_snapshot_registers(item: dict[str, Any]) -> dict[str, str]:
    snapshots = item.get("register_snapshot")
    if not isinstance(snapshots, dict):
        return {}
    registers: dict[str, str] = {}
    for snapshot in snapshots.values():
        if not isinstance(snapshot, dict):
            continue
        values = snapshot.get("values")
        if not isinstance(values, list):
            continue
        try:
            first_register = parse_register(snapshot.get("first"))
        except Exception:
            continue
        for offset, raw_value in enumerate(values):
            if not isinstance(raw_value, str):
                continue
            register = first_register + offset
            registers[register_name(register)] = format_u32(
                parse_u32_hex(raw_value, field="register_snapshot.value")
            )
    return registers


def _draw_fog_state(item: dict[str, Any]) -> dict[str, Any] | None:
    registers = _draw_snapshot_registers(item)
    if not registers:
        return None
    fog = decode_fog_state(registers)
    return fog if fog.get("available") else None


def _vec3_close(value: list[float] | None, target: float, epsilon: float = 0.00001) -> bool:
    return value is not None and all(abs(component - target) <= epsilon for component in value[:3])


def _mat3_uniform_rows(item: dict[str, Any], first_uniform: int) -> list[list[float]] | None:
    rows: list[list[float]] = []
    for index in range(first_uniform, first_uniform + 3):
        value = _uniform_vec4(item, index)
        if value is None:
            return None
        rows.append([float(value[0]), float(value[1]), float(value[2])])
    return rows


def _mat3_mul_vec3(matrix: list[list[float]], vector: list[float]) -> list[float]:
    return [
        sum(matrix[row][column] * vector[column] for column in range(3))
        for row in range(3)
    ]


def _normalized_vec3(value: list[float] | None) -> list[float] | None:
    if value is None:
        return None
    length = math.sqrt(sum(component * component for component in value[:3]))
    if length <= 1e-9:
        return None
    return [component / length for component in value[:3]]


def _shader_stage_key(value: Any) -> str | None:
    if not isinstance(value, str):
        return None
    normalized = value.strip().lower()
    if normalized in {"vs", "vsh"}:
        return "vsh"
    if normalized in {"gs", "gsh"}:
        return "gsh"
    return None


def _float_vec4(value: Any) -> list[float] | None:
    if not isinstance(value, list) or len(value) < 4:
        return None
    out: list[float] = []
    for component in value[:4]:
        if isinstance(component, bool) or not isinstance(component, (int, float)):
            return None
        out.append(float(component))
    return out


def _shader_uniform_upload_event_block(item: dict[str, Any],
                                       line_number: int | None) -> dict[str, Any] | None:
    key = _shader_stage_key(item.get("shader"))
    if key is None:
        return None
    uniform_index = item.get("uniform_index")
    if isinstance(uniform_index, bool) or not isinstance(uniform_index, int):
        return None
    xyzw = _float_vec4(item.get("xyzw"))
    if xyzw is None:
        return None

    block: dict[str, Any] = {
        "shader": key,
        "uniform_index": uniform_index,
        "format": item.get("format") if isinstance(item.get("format"), str) else None,
        "offset_basis": "completed_uniform_data_write",
        "index_cmd_list_offset_words": item.get("cmd_list_offset_words"),
        "completion_cmd_list_offset_words": item.get("cmd_list_offset_words"),
        "xyzw": xyzw,
        "rgb_u8_if_color": maybe_color_u8_from_vec4_xyzw(xyzw),
        "source_event": "shader_uniform_upload",
    }
    if line_number is not None:
        block["trace_line"] = line_number
    if isinstance(item.get("cmd_list_addr"), str):
        block["cmd_list_addr"] = item["cmd_list_addr"]
    if isinstance(item.get("cmd_list_word_addr"), str):
        block["cmd_list_word_addr"] = item["cmd_list_word_addr"]
    write_register_id = item.get("write_register_id")
    if isinstance(write_register_id, int):
        block["write_register_id"] = format_reg(write_register_id)
    elif isinstance(write_register_id, str):
        block["write_register_id"] = format_reg(parse_register(write_register_id))
    if isinstance(item.get("write_register_name"), str):
        block["write_register_name"] = item["write_register_name"]
    if "write_value" in item:
        block["write_value"] = format_u32(parse_u32_hex(item["write_value"], field="write_value"))
    if isinstance(item.get("vertex_hemisphere_candidate"), bool):
        block["vertex_hemisphere_candidate"] = item["vertex_hemisphere_candidate"]
    return block


def _summarize_cmb_vertex_lighting_uniform_trace(summary: dict[str, Any], item: dict[str, Any]) -> None:
    material_diffuse = _uniform_vec4(item, 8)
    material_ambient = _uniform_vec4(item, 9)
    light_vector_negated = _uniform_vec4(item, 80)
    secondary_color = _uniform_vec4(item, 81)
    ambient_color = _uniform_vec4(item, 82)
    light_vector = _uniform_vec4(item, 83)
    diffuse_color = _uniform_vec4(item, 84)
    if (
        material_diffuse is None
        or material_ambient is None
        or ambient_color is None
        or light_vector is None
        or light_vector_negated is None
    ):
        return

    normal_matrix = _mat3_uniform_rows(item, 76)
    world_light_vector = None
    world_negated_light_vector = None
    if normal_matrix is not None:
        world_light_vector = _normalized_vec3(_mat3_mul_vec3(normal_matrix, light_vector[:3]))
        world_negated_light_vector = _normalized_vec3(
            _mat3_mul_vec3(normal_matrix, light_vector_negated[:3])
        )

    ambient_only = (
        _vec3_nonzero(ambient_color)
        and not _vec3_nonzero(diffuse_color)
        and not _vec3_nonzero(secondary_color)
    )
    actor_directional = (
        _vec3_nonzero(ambient_color)
        and _vec3_nonzero(diffuse_color)
        and _vec3_neutral(material_diffuse)
        and _vec3_neutral(material_ambient)
        and _vec3_inside_unit_color_range(material_diffuse)
        and _vec3_inside_unit_color_range(material_ambient)
    )
    if ambient_only and _vec3_close(material_ambient, 1.0) and _vec3_close(material_diffuse, 0.0):
        classification = "static_baked_ambient_only_vertex_hemisphere"
    elif actor_directional:
        classification = "actor_skeleton_directional_vertex_hemisphere"
    else:
        classification = "other_cmb_vertex_lighting_uniforms"

    trace = summary.setdefault(
        "cmb_vertex_lighting_uniform_draw_trace",
        {
            "available": True,
            "source_kind": "azahar_pica_frame_vs_uniforms",
            "format": "oot3d_native_cmb_vertex_lighting_vs_uniform_draw_trace_v1",
            "basis": "draw_begin.vs_uniforms.f8/f9 and f80-f84 entries for native OOT3D CMB draw calls",
            "material_diffuse_uniform_index": 8,
            "material_ambient_uniform_index": 9,
            "ambient_uniform_index": 82,
            "diffuse_uniform_index": 84,
            "secondary_color_uniform_index": 81,
            "light_vector_uniform_index": 83,
            "negated_light_vector_uniform_index": 80,
            "draw_count": 0,
            "vertex_count": 0,
            "classification_counts": {},
            "classification_vertex_counts": {},
            "draws": [],
        },
    )
    trace["draw_count"] += 1
    vertex_count = int(item.get("num_vertices", 0))
    trace["vertex_count"] += vertex_count
    classification_counts = trace["classification_counts"]
    classification_counts[classification] = int(classification_counts.get(classification, 0)) + 1
    classification_vertex_counts = trace["classification_vertex_counts"]
    classification_vertex_counts[classification] = (
        int(classification_vertex_counts.get(classification, 0)) + vertex_count
    )
    fog_state = _draw_fog_state(item)
    trace["draws"].append(
        {
            "draw_index": item.get("draw_index"),
            "vertex_count": vertex_count,
            "classification": classification,
            "textures": item.get("textures") if isinstance(item.get("textures"), list) else [],
            "fog": fog_state,
            "material_diffuse": material_diffuse,
            "material_diffuse_u8": _color_u8_from_unit_vec4(material_diffuse),
            "material_ambient": material_ambient,
            "material_ambient_u8": _color_u8_from_unit_vec4(material_ambient),
            "ambient_color": ambient_color,
            "ambient_color_u8": _color_u8_from_unit_vec4(ambient_color),
            "diffuse_color": diffuse_color,
            "diffuse_color_u8": _color_u8_from_unit_vec4(diffuse_color)
            if diffuse_color is not None
            else None,
            "secondary_color": secondary_color,
            "secondary_color_u8": _color_u8_from_unit_vec4(secondary_color)
            if secondary_color is not None
            else None,
            "light_vector": light_vector,
            "negated_light_vector": light_vector_negated,
            "normal_matrix_uniform_start": 76 if normal_matrix is not None else None,
            "normal_matrix": normal_matrix,
            "world_light_vector_from_normal_matrix": world_light_vector,
            "world_negated_light_vector_from_normal_matrix": world_negated_light_vector,
        }
    )


def _summarize_vertex_hemisphere_uniform_trace(summary: dict[str, Any], item: dict[str, Any]) -> None:
    material_diffuse = _uniform_vec4(item, 8)
    material_ambient = _uniform_vec4(item, 9)
    light_vector_negated = _uniform_vec4(item, 80)
    secondary_color = _uniform_vec4(item, 81)
    ambient_color = _uniform_vec4(item, 82)
    light_vector = _uniform_vec4(item, 83)
    diffuse_color = _uniform_vec4(item, 84)
    normal_matrix = _mat3_uniform_rows(item, 76)
    world_light_vector = None
    world_negated_light_vector = None
    if normal_matrix is not None and light_vector is not None and light_vector_negated is not None:
        world_light_vector = _normalized_vec3(_mat3_mul_vec3(normal_matrix, light_vector[:3]))
        world_negated_light_vector = _normalized_vec3(
            _mat3_mul_vec3(normal_matrix, light_vector_negated[:3])
        )

    if (
        not _vec3_nonzero(ambient_color)
        or not _vec3_nonzero(diffuse_color)
        or not _vec3_neutral(material_diffuse)
        or not _vec3_neutral(material_ambient)
        or not _vec3_inside_unit_color_range(material_diffuse)
        or not _vec3_inside_unit_color_range(material_ambient)
    ):
        return

    trace = summary.setdefault(
        "vertex_hemisphere_lighting_uniform_trace",
        {
            "available": True,
            "source_kind": "azahar_pica_frame_vs_uniforms",
            "format": "oot3d_native_cmb_vertex_hemisphere_vs_uniform_trace_v1",
            "basis": "draw_begin.vs_uniforms.f entries used by OOT3D CMB skeleton vertex/hemisphere lighting",
            "formula": "material_ambient.rgb * f82.rgb + material_diffuse.rgb * (f84.rgb * max(dot(normal, f80.xyz), 0) + f81.rgb * max(dot(normal, f83.xyz), 0))",
            "material_diffuse_uniform_index": 8,
            "material_ambient_uniform_index": 9,
            "ambient_uniform_index": 82,
            "diffuse_uniform_index": 84,
            "light_vector_uniform_index": 83,
            "secondary_color_uniform_index": 81,
            "negated_light_vector_uniform_index": 80,
            "candidate_draw_count": 0,
            "candidate_vertex_count": 0,
            "draws": [],
        },
    )
    trace["candidate_draw_count"] += 1
    trace["candidate_vertex_count"] += int(item.get("num_vertices", 0))

    draw = {
        "draw_index": item.get("draw_index"),
        "vertex_count": item.get("num_vertices"),
        "textures": item.get("textures") if isinstance(item.get("textures"), list) else [],
        "fog": _draw_fog_state(item),
        "material_diffuse": material_diffuse,
        "material_ambient": material_ambient,
        "ambient_color": ambient_color,
        "ambient_color_u8": _color_u8_from_unit_vec4(ambient_color),
        "diffuse_color": diffuse_color,
        "diffuse_color_u8": _color_u8_from_unit_vec4(diffuse_color),
        "secondary_color": secondary_color,
        "secondary_color_u8": _color_u8_from_unit_vec4(secondary_color) if secondary_color is not None else None,
        "light_vector": light_vector,
        "negated_light_vector": light_vector_negated,
        "normal_matrix_uniform_start": 76 if normal_matrix is not None else None,
        "normal_matrix": normal_matrix,
        "world_light_vector_from_normal_matrix": world_light_vector,
        "world_negated_light_vector_from_normal_matrix": world_negated_light_vector,
    }
    trace["draws"].append(draw)
    if "selected_draw_index" not in trace:
        trace.update(
            {
                "selected_draw_index": item.get("draw_index"),
                "selected_vertex_count": item.get("num_vertices"),
                "material_diffuse": material_diffuse,
                "material_ambient": material_ambient,
                "ambient_color": ambient_color,
                "ambient_color_u8": _color_u8_from_unit_vec4(ambient_color),
                "diffuse_color": diffuse_color,
                "diffuse_color_u8": _color_u8_from_unit_vec4(diffuse_color),
                "secondary_color": secondary_color,
                "secondary_color_u8": _color_u8_from_unit_vec4(secondary_color) if secondary_color is not None else None,
                "light_vector": light_vector,
                "negated_light_vector": light_vector_negated,
                "normal_matrix_uniform_start": 76 if normal_matrix is not None else None,
                "normal_matrix": normal_matrix,
                "world_light_vector_from_normal_matrix": world_light_vector,
                "world_negated_light_vector_from_normal_matrix": world_negated_light_vector,
            }
        )


def _summarize_azahar_frame_event(summary: dict[str, Any], item: dict[str, Any],
                                  line_number: int | None = None) -> None:
    event = item.get("event")
    if isinstance(event, str):
        event_counts = summary.setdefault("event_counts", {})
        event_counts[event] = int(event_counts.get(event, 0)) + 1

    if event == "capture_begin":
        if "frame_index" in item:
            summary["frame_index"] = item["frame_index"]
        if "capture_index" in item:
            summary["capture_index"] = item["capture_index"]
        return

    if event == "shader_uniform_upload":
        block = _shader_uniform_upload_event_block(item, line_number)
        if block is not None:
            uploads = summary.setdefault("shader_uniform_upload_events", [])
            uploads.append(block)
        return

    if event == "draw_begin":
        draw_events = summary.setdefault("draw_events", [])
        draw_event: dict[str, Any] = {
            "draw_index": item.get("draw_index"),
            "cmd_list_addr": item.get("cmd_list_addr"),
            "cmd_list_offset_words": item.get("cmd_list_offset_words"),
            "mode": item.get("mode"),
            "is_indexed": item.get("is_indexed"),
            "num_vertices": item.get("num_vertices"),
            "vertex_offset": item.get("vertex_offset"),
            "triangle_topology": item.get("triangle_topology"),
        }
        textures = item.get("textures")
        if isinstance(textures, list):
            compact_textures = []
            for texture in textures:
                if not isinstance(texture, dict):
                    continue
                compact_textures.append({
                    "index": texture.get("index"),
                    "enabled": texture.get("enabled"),
                    "format": texture.get("format"),
                    "type": texture.get("type"),
                    "width": texture.get("width"),
                    "height": texture.get("height"),
                    "address": texture.get("address"),
                })
            draw_event["textures"] = compact_textures
        shadow_summary = item.get("shadow_summary")
        if isinstance(shadow_summary, dict):
            draw_event["shadow_summary"] = shadow_summary
        fog_state = _draw_fog_state(item)
        if fog_state is not None:
            draw_event["fog"] = fog_state
        vs_uniforms = item.get("vs_uniforms")
        if isinstance(vs_uniforms, dict) and isinstance(vs_uniforms.get("f"), list):
            f_uniforms = vs_uniforms["f"]
            material_uniforms = {}
            for uniform_index in (8, 9):
                if uniform_index < len(f_uniforms):
                    material_uniforms[f"f{uniform_index}"] = f_uniforms[uniform_index]
            if material_uniforms:
                draw_event["vs_uniforms_f8_f9"] = material_uniforms
            light_uniforms = {}
            for uniform_index in range(76, 87):
                if uniform_index < len(f_uniforms):
                    light_uniforms[f"f{uniform_index}"] = f_uniforms[uniform_index]
            if light_uniforms:
                draw_event["vs_uniforms_f76_f86"] = light_uniforms
        draw_events.append(draw_event)

        draw_summary = summary.setdefault(
            "draw_summary",
            {
                "draw_count": 0,
                "fragment_lighting_enabled_draw_count": 0,
                "lighting_shadow_enabled_draw_count": 0,
                "primary_rgb_shadow_route_draw_count": 0,
                "shadow2d_texture_draw_count": 0,
                "enabled_texture_draw_count": 0,
                "enabled_texture_type_counts": {},
                "fog_decoded_draw_count": 0,
                "fog_enabled_draw_count": 0,
                "fog_or_gas_enabled_draw_count": 0,
                "fog_mode_counts": {},
                "fog_color_counts": {},
            },
        )
        draw_summary["draw_count"] += 1

        if fog_state is not None:
            draw_summary["fog_decoded_draw_count"] += 1
            mode_name = str(fog_state.get("mode_name", "Unknown"))
            fog_mode_counts = draw_summary["fog_mode_counts"]
            fog_mode_counts[mode_name] = int(fog_mode_counts.get(mode_name, 0)) + 1
            if fog_state.get("fog_enabled"):
                draw_summary["fog_enabled_draw_count"] += 1
            if fog_state.get("fog_or_gas_enabled"):
                draw_summary["fog_or_gas_enabled_draw_count"] += 1
            color = fog_state.get("color_rgb_u8")
            if isinstance(color, dict):
                color_key = "{},{},{}".format(color.get("r"), color.get("g"), color.get("b"))
                fog_color_counts = draw_summary["fog_color_counts"]
                fog_color_counts[color_key] = int(fog_color_counts.get(color_key, 0)) + 1

        shadow = item.get("shadow_summary")
        if isinstance(shadow, dict):
            fragment_lighting = bool(shadow.get("lighting_enable0_raw") not in (None, "0x00000000", 0))
            enable_shadow = bool(shadow.get("lighting_enable_shadow"))
            shadow_primary = bool(shadow.get("lighting_shadow_primary"))
            disabled_mask = shadow.get("lighting_disable_shadow_mask", 0xFF)
            try:
                disabled_mask_int = parse_int(disabled_mask, field="lighting_disable_shadow_mask")
            except Exception:
                disabled_mask_int = 0xFF
            if fragment_lighting:
                draw_summary["fragment_lighting_enabled_draw_count"] += 1
            if enable_shadow:
                draw_summary["lighting_shadow_enabled_draw_count"] += 1
            if fragment_lighting and enable_shadow and shadow_primary and ((~disabled_mask_int) & 0xFF):
                draw_summary["primary_rgb_shadow_route_draw_count"] += 1

        textures = item.get("textures")
        if isinstance(textures, list):
            has_shadow2d = False
            for texture in textures:
                if not isinstance(texture, dict) or not texture.get("enabled"):
                    continue
                draw_summary["enabled_texture_draw_count"] += 1
                texture_type = texture.get("type", 0)
                try:
                    texture_type_int = parse_int(texture_type, field="texture.type")
                except Exception:
                    texture_type_int = -1
                type_counts = draw_summary["enabled_texture_type_counts"]
                type_key = str(texture_type_int)
                type_counts[type_key] = int(type_counts.get(type_key, 0)) + 1
                if texture_type_int == 2:
                    has_shadow2d = True
            if has_shadow2d:
                draw_summary["shadow2d_texture_draw_count"] += 1
        _summarize_cmb_vertex_lighting_uniform_trace(summary, item)
        _summarize_vertex_hemisphere_uniform_trace(summary, item)
        snapshots = item.get("register_snapshot")
        if isinstance(snapshots, dict):
            registers = summary.setdefault("registers_from_draw_snapshots", {})
            if isinstance(registers, dict):
                registers.update(_draw_snapshot_registers(item))
                summary["snapshot_register_count"] = len(registers)
        return

    if event == "output_vertex":
        mapped = item.get("mapped")
        if not isinstance(mapped, dict):
            return
        vertex_summary = summary.setdefault(
            "output_vertex_summary",
            {
                "vertex_count": 0,
                "tc0_w": { "sample_count": 0, "nonzero_count": 0 },
                "primary_color": { "sample_count": 0, "nonwhite_count": 0 },
            },
        )
        vertex_summary["vertex_count"] += 1

        tc0_w = _float_payload(mapped.get("tc0_w"))
        if tc0_w is not None:
            tc0_w_summary = vertex_summary["tc0_w"]
            tc0_w_summary["sample_count"] += 1
            tc0_w_summary["min"] = min(tc0_w_summary.get("min", tc0_w), tc0_w)
            tc0_w_summary["max"] = max(tc0_w_summary.get("max", tc0_w), tc0_w)
            if abs(tc0_w) > 0.000001:
                tc0_w_summary["nonzero_count"] += 1

        color = mapped.get("color")
        if isinstance(color, list) and len(color) >= 4:
            color_values = [_float_payload(value) for value in color[:4]]
            if all(value is not None for value in color_values):
                typed_values = [float(value) for value in color_values if value is not None]
                color_summary = vertex_summary["primary_color"]
                color_summary["sample_count"] += 1
                _update_min_max(color_summary, "primary_color", typed_values)
                if any(abs(component - 1.0) > 0.000001 for component in typed_values[:3]):
                    color_summary["nonwhite_count"] += 1


def load_jsonl_trace(path: Path) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    writes: list[dict[str, Any]] = []
    frame_summary: dict[str, Any] = {
        "source_kind": SOURCE_KIND_AZAHAR_FRAME_JSONL,
        "frame_capture": {
            "format": "azahar_pica_frame_jsonl_v1",
            "source_path": str(path),
        },
    }
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        if not line.strip():
            continue
        item = json.loads(line)
        if not isinstance(item, dict):
            raise ValueError(f"{path}:{line_number}: line is not a JSON object")
        _summarize_azahar_frame_event(frame_summary["frame_capture"], item, line_number)
        reg_value = item.get("cmd_id", item.get("register", item.get("reg", item.get("id"))))
        value = item.get("value", item.get("final_value", item.get("write_value")))
        if reg_value is None or value is None:
            continue
        reg = parse_register(reg_value)
        mask = parse_mask(item.get("mask", "1111"))
        name = item.get("name") if isinstance(item.get("name"), str) else None
        metadata = {
            "trace_line": line_number,
        }
        if isinstance(item.get("cmd_list_addr"), str):
            metadata["cmd_list_addr"] = item["cmd_list_addr"]
        if isinstance(item.get("cmd_list_offset_words"), int):
            metadata["cmd_list_offset_words"] = item["cmd_list_offset_words"]
        if isinstance(item.get("cmd_list_word_addr"), str):
            metadata["cmd_list_word_addr"] = item["cmd_list_word_addr"]
        if isinstance(item.get("frame_index"), int):
            metadata["frame_index"] = item["frame_index"]
        if isinstance(item.get("capture_index"), int):
            metadata["capture_index"] = item["capture_index"]
        writes.append(normalize_write(reg, mask, parse_u32_hex(value, field="value"), name, metadata))
    return writes, frame_summary


def load_azahar_copy_all(path: Path) -> list[dict[str, Any]]:
    text = path.read_text(encoding="utf-8-sig")
    rows = csv.reader(text.splitlines(), delimiter="\t")
    writes: list[dict[str, Any]] = []
    for line_number, row in enumerate(rows, start=1):
        row = [cell.strip() for cell in row if cell.strip()]
        if not row:
            continue
        if row[0].lower() in {"command name", "name"}:
            continue
        if len(row) < 4:
            raise ValueError(f"{path}:{line_number}: expected 4 tab-separated columns")
        name, reg_text, mask_text, value_text = row[:4]
        reg = parse_register(reg_text)
        mask = parse_mask(mask_text)
        value = parse_u32_hex(value_text, field="value")
        writes.append(normalize_write(reg, mask, value, name))
    return writes


def load_writes(path: Path, input_format: str) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    if input_format == "json":
        return load_json_trace(path)
    if input_format == "jsonl":
        return load_jsonl_trace(path)
    if input_format == "copy-all":
        return load_azahar_copy_all(path), {}
    if input_format != "auto":
        raise ValueError(f"unsupported input format: {input_format}")

    suffix = path.suffix.lower()
    if suffix == ".json":
        return load_json_trace(path)
    if suffix == ".jsonl":
        return load_jsonl_trace(path)
    return load_azahar_copy_all(path), {}


def summarize_shader_uniform_writes(writes: list[dict[str, Any]]) -> dict[str, Any]:
    state = {
        "gsh": {"current_index": None, "format": "float24", "write_count": 0, "data_word_count": 0, "last_written_index": None},
        "vsh": {"current_index": None, "format": "float24", "write_count": 0, "data_word_count": 0, "last_written_index": None},
    }
    for write in writes:
        reg = parse_register(write["cmd_id"])
        value = parse_u32_hex(write["value"], field="value")
        if reg in {REG_GSH_FLOATUNIFORM_INDEX, REG_VSH_FLOATUNIFORM_INDEX}:
            key = "gsh" if reg == REG_GSH_FLOATUNIFORM_INDEX else "vsh"
            state[key]["current_index"] = value & 0x7F
            state[key]["format"] = "float32" if value & 0x80000000 else "float24"
            continue
        if REG_GSH_FLOATUNIFORM_DATA0 <= reg <= REG_GSH_FLOATUNIFORM_DATA7:
            key = "gsh"
        elif REG_VSH_FLOATUNIFORM_DATA0 <= reg <= REG_VSH_FLOATUNIFORM_DATA7:
            key = "vsh"
        else:
            continue
        state[key]["data_word_count"] += 1
        if (reg - (REG_GSH_FLOATUNIFORM_DATA0 if key == "gsh" else REG_VSH_FLOATUNIFORM_DATA0)) % 4 == 3:
            state[key]["write_count"] += 1
            if state[key]["current_index"] is not None:
                state[key]["last_written_index"] = state[key]["current_index"]
                state[key]["current_index"] += 1
    return state


def float32_from_u32(value: int) -> float:
    import struct

    return struct.unpack("<f", (value & 0xFFFFFFFF).to_bytes(4, "little"))[0]


def maybe_color_u8_from_vec4_xyzw(value: list[float]) -> list[int] | None:
    if len(value) < 4:
        return None
    rgb = value[:3]
    if not all(math.isfinite(component) and -0.00001 <= component <= 1.00001 for component in rgb):
        return None
    return [max(0, min(255, int(round(component * 255.0)))) for component in rgb]


def build_shader_uniform_upload_summary(blocks: list[dict[str, Any]], *,
                                        source_kind: str,
                                        component_order_basis: str | None = None) -> dict[str, Any]:
    vertex_hemisphere_blocks = [
        block for block in blocks
        if block["shader"] == "vsh" and 80 <= int(block["uniform_index"]) <= 86
    ]
    link_like_blocks = [
        block for block in vertex_hemisphere_blocks
        if int(block["uniform_index"]) in {80, 81, 82, 83, 84}
        and block.get("xyzw") is not None
    ]
    out: dict[str, Any] = {
        "source_kind": source_kind,
        "block_count": len(blocks),
        "vertex_hemisphere_block_count": len(vertex_hemisphere_blocks),
        "blocks": blocks,
        "vertex_hemisphere_blocks": vertex_hemisphere_blocks,
        "link_like_vertex_hemisphere_blocks": link_like_blocks,
    }
    if component_order_basis:
        out["component_order_basis"] = component_order_basis
    return out


def decode_shader_uniform_upload_blocks(writes: list[dict[str, Any]]) -> dict[str, Any]:
    state = {
        "gsh": {"current_index": None, "format": "float24", "index_write": None, "pending_words": []},
        "vsh": {"current_index": None, "format": "float24", "index_write": None, "pending_words": []},
    }
    blocks: list[dict[str, Any]] = []

    for ordinal, write in enumerate(writes):
        reg = parse_register(write["cmd_id"])
        value = parse_u32_hex(write["value"], field="value")
        if reg in {REG_GSH_FLOATUNIFORM_INDEX, REG_VSH_FLOATUNIFORM_INDEX}:
            key = "gsh" if reg == REG_GSH_FLOATUNIFORM_INDEX else "vsh"
            state[key]["current_index"] = value & 0x7F
            state[key]["format"] = "float32" if value & 0x80000000 else "float24"
            state[key]["index_write"] = write
            state[key]["pending_words"] = []
            continue

        if REG_GSH_FLOATUNIFORM_DATA0 <= reg <= REG_GSH_FLOATUNIFORM_DATA7:
            key = "gsh"
        elif REG_VSH_FLOATUNIFORM_DATA0 <= reg <= REG_VSH_FLOATUNIFORM_DATA7:
            key = "vsh"
        else:
            continue

        if state[key]["current_index"] is None:
            continue
        pending_words = state[key]["pending_words"]
        pending_words.append(write)
        if len(pending_words) < 4:
            continue

        raw_words = [parse_u32_hex(item["value"], field="uniform_upload_word") for item in pending_words[:4]]
        raw_words_hex = [format_u32(item) for item in raw_words]
        pica_write_components = None
        xyzw = None
        color_u8 = None
        if state[key]["format"] == "float32":
            pica_write_components = [float32_from_u32(item) for item in raw_words]
            # PICA float uniform uploads are written in w,z,y,x order.
            xyzw = [
                pica_write_components[3],
                pica_write_components[2],
                pica_write_components[1],
                pica_write_components[0],
            ]
            color_u8 = maybe_color_u8_from_vec4_xyzw(xyzw)
        index_write = state[key]["index_write"] if isinstance(state[key]["index_write"], dict) else {}
        data_offsets = [
            item.get("cmd_list_offset_words")
            for item in pending_words[:4]
            if isinstance(item.get("cmd_list_offset_words"), int)
        ]
        block = {
            "shader": key,
            "uniform_index": state[key]["current_index"],
            "format": state[key]["format"],
            "index_cmd_list_offset_words": index_write.get("cmd_list_offset_words"),
            "data_cmd_list_offset_words": data_offsets,
            "raw_words": raw_words_hex,
            "pica_write_component_order": "wzyx",
            "pica_write_components": pica_write_components,
            "xyzw": xyzw,
            "rgb_u8_if_color": color_u8,
            "write_ordinal": ordinal,
        }
        if isinstance(index_write.get("cmd_list_addr"), str):
            block["cmd_list_addr"] = index_write["cmd_list_addr"]
        blocks.append(block)
        state[key]["current_index"] = int(state[key]["current_index"]) + 1
        state[key]["pending_words"] = []

    return build_shader_uniform_upload_summary(
        blocks,
        source_kind="oot3d_pica_command_list_shader_uniform_upload_decode",
        component_order_basis=(
            "PICA float uniform data words are command-list w,z,y,x and are normalized to xyzw here"
        ),
    )


def build_output(path: Path, writes: list[dict[str, Any]], passthrough: dict[str, Any]) -> dict[str, Any]:
    registers: dict[str, str] = {}
    frame_capture = passthrough.get("frame_capture")
    if isinstance(frame_capture, dict) and isinstance(frame_capture.get("registers_from_draw_snapshots"), dict):
        for name, value in frame_capture["registers_from_draw_snapshots"].items():
            if not isinstance(name, str):
                continue
            registers[name] = format_u32(parse_u32_hex(value, field=f"{name}.snapshot_value"))
    for write in writes:
        reg = parse_register(write["cmd_id"])
        value = parse_u32_hex(write["value"], field="value")
        mask = expanded_byte_mask(parse_mask(write.get("mask", "1111")) & 0xF)
        previous = parse_u32_hex(registers.get(register_name(reg), "0"), field="previous_register_value")
        registers[register_name(reg)] = format_u32((previous & ~mask) | (value & mask))

    shadow_registers: dict[str, Any] = {}
    if register_name(REG_TEXUNIT0_SHADOW) in registers:
        shadow_registers["texture_shadow"] = decode_texture_shadow(
            parse_u32_hex(registers[register_name(REG_TEXUNIT0_SHADOW)], field="texture_shadow")
        )
    if register_name(REG_FRAGOP_SHADOW) in registers:
        shadow_registers["framebuffer_shadow"] = decode_framebuffer_shadow(
            parse_u32_hex(registers[register_name(REG_FRAGOP_SHADOW)], field="framebuffer_shadow")
        )
    if register_name(REG_DEPTHMAP_SCALE) in registers or register_name(REG_DEPTHMAP_OFFSET) in registers:
        shadow_registers["depthmap"] = {
            "scale_raw": registers.get(register_name(REG_DEPTHMAP_SCALE)),
            "offset_raw": registers.get(register_name(REG_DEPTHMAP_OFFSET)),
        }
    shadow_shader_route = decode_shadow_shader_route(registers)
    fog_state = decode_fog_state(registers)

    register_decoded_uniform_uploads = decode_shader_uniform_upload_blocks(writes)
    uniform_uploads = register_decoded_uniform_uploads
    direct_upload_blocks: list[dict[str, Any]] = []
    if isinstance(frame_capture, dict) and isinstance(frame_capture.get("shader_uniform_upload_events"), list):
        direct_upload_blocks = [
            block for block in frame_capture["shader_uniform_upload_events"]
            if isinstance(block, dict)
        ]
    if direct_upload_blocks:
        uniform_uploads = build_shader_uniform_upload_summary(
            direct_upload_blocks,
            source_kind="azahar_pica_frame_shader_uniform_upload_events",
            component_order_basis=(
                "Azahar decoded ShaderSetup::uniforms.f values emitted at the completing "
                "shader uniform data write"
            ),
        )
        uniform_uploads["register_write_decode_fallback_summary"] = {
            "source_kind": register_decoded_uniform_uploads["source_kind"],
            "block_count": register_decoded_uniform_uploads["block_count"],
            "vertex_hemisphere_block_count": (
                register_decoded_uniform_uploads["vertex_hemisphere_block_count"]
            ),
            "used_as_primary_source": False,
        }

    output = {
        "format": TRACE_FORMAT,
        "source_kind": passthrough.get("source_kind", SOURCE_KIND_JSON if passthrough else SOURCE_KIND_AZAHAR_COPY_ALL),
        "source_path": str(path),
        "write_count": len(writes),
        "register_count": len(registers),
        "writes": writes,
        "registers": registers,
        "shadow_registers": shadow_registers,
        "shadow_shader_route": shadow_shader_route,
        "fog_state": fog_state,
        "shader_uniform_writes": summarize_shader_uniform_writes(writes),
        "shader_uniform_uploads": uniform_uploads,
        "diagnostics": {
            "has_fog_mode_register": register_name(REG_TEXENV_UPDATE_BUFFER) in registers,
            "has_fog_color_register": register_name(REG_FOG_COLOR) in registers,
            "has_fog_lut_index_register": register_name(REG_FOG_LUT_INDEX) in registers,
            "final_fog_or_gas_enabled": bool(fog_state.get("fog_or_gas_enabled")),
            "has_texture_shadow_register": register_name(REG_TEXUNIT0_SHADOW) in registers,
            "has_framebuffer_shadow_register": register_name(REG_FRAGOP_SHADOW) in registers,
            "has_depthmap_scale_register": register_name(REG_DEPTHMAP_SCALE) in registers,
            "has_depthmap_offset_register": register_name(REG_DEPTHMAP_OFFSET) in registers,
            "has_lighting_config0_register": register_name(REG_LIGHTING_CONFIG0) in registers,
            "has_lighting_config1_register": register_name(REG_LIGHTING_CONFIG1) in registers,
            "has_lighting_enable0_register": register_name(REG_LIGHTING_ENABLE0) in registers,
            "shadow_shader_route_matches_primary_rgb_shadow_term": shadow_shader_route[
                "matches_primary_rgb_shadow_term"
            ],
            "shadow_shader_route_has_texture_dimension_register": shadow_shader_route[
                "shadow_texture_dim_decoded"
            ],
            "dmp_shadow_z_uniforms_present": isinstance(passthrough.get("uniforms"), dict)
            and "dmp_Texture[0].shadowZBias" in passthrough["uniforms"]
            and "dmp_Texture[0].shadowZScale" in passthrough["uniforms"],
        },
    }
    if isinstance(passthrough.get("uniforms"), dict):
        output["uniforms"] = passthrough["uniforms"]
    if isinstance(passthrough.get("dmp_uniforms"), dict):
        output["dmp_uniforms"] = passthrough["dmp_uniforms"]
    if isinstance(passthrough.get("frame_capture"), dict):
        output["frame_capture"] = passthrough["frame_capture"]
    return output


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2), encoding="utf-8", newline="\n")


def write_manifest(manifest_path: Path, output_path: Path, trace_path: Path) -> None:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(manifest, dict):
        raise ValueError("manifest root must be an object")
    sources = manifest.setdefault("sources", {})
    if not isinstance(sources, dict):
        raise ValueError("manifest sources must be an object")
    sources["native_pica_register_trace"] = {
        "kind": "oot3d_native_pica_register_trace",
        "path": str(trace_path),
    }
    write_json(output_path, manifest)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path, help="Azahar Copy All TSV, JSON, or JSONL trace")
    parser.add_argument("--output", required=True, type=Path, help="Normalized native PICA register trace JSON")
    parser.add_argument("--input-format", choices=["auto", "copy-all", "json", "jsonl"], default="auto")
    parser.add_argument("--manifest", type=Path, help="Optional source demo manifest to clone")
    parser.add_argument("--manifest-out", type=Path, help="Optional derived manifest with native_pica_register_trace wired")
    parser.add_argument("--require-shadow-registers", action="store_true", help="Fail unless both 0x08B and 0x130 are present")
    args = parser.parse_args(argv)

    writes, passthrough = load_writes(args.input, args.input_format)
    output = build_output(args.input, writes, passthrough)
    diagnostics = output["diagnostics"]
    if args.require_shadow_registers and (
        not diagnostics["has_texture_shadow_register"] or not diagnostics["has_framebuffer_shadow_register"]
    ):
        missing = [
            name
            for name, present in {
                "GPUREG_TEXUNIT0_SHADOW": diagnostics["has_texture_shadow_register"],
                "GPUREG_FRAGOP_SHADOW": diagnostics["has_framebuffer_shadow_register"],
            }.items()
            if not present
        ]
        raise SystemExit(f"missing required shadow registers: {', '.join(missing)}")

    write_json(args.output, output)
    if args.manifest or args.manifest_out:
        if not args.manifest or not args.manifest_out:
            raise SystemExit("--manifest and --manifest-out must be provided together")
        write_manifest(args.manifest, args.manifest_out, args.output.resolve())

    print(json.dumps({
        "status": "valid",
        "output": str(args.output),
        "write_count": output["write_count"],
        "register_count": output["register_count"],
        "shadow_registers": diagnostics,
        "manifest_out": str(args.manifest_out) if args.manifest_out else None,
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
