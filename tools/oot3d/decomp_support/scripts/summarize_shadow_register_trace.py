#!/usr/bin/env python3
"""Summarize validation-only OOT3D PICA shadow register traces."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path
from typing import Any


TRACE_FORMAT = "oot3d_shadow_register_trace_summary_v1"


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as infile:
        data = json.load(infile)
    if not isinstance(data, dict):
        raise ValueError(f"{path} did not contain a JSON object")
    return data


def add_counter(counter: Counter[str], value: Any) -> None:
    if value is None:
        counter["<missing>"] += 1
    else:
        counter[str(value)] += 1


def counter_json(counter: Counter[str]) -> list[dict[str, Any]]:
    return [
        {
            "value": value,
            "count": count,
        }
        for value, count in sorted(counter.items())
    ]


def render_counts(values: list[dict[str, Any]]) -> str:
    return ", ".join(f"`{item['value']}` x{item['count']}" for item in values)


def summarize_trace(path: Path) -> dict[str, Any]:
    trace = load_json(path)
    diagnostics = trace.get("diagnostics") if isinstance(trace.get("diagnostics"), dict) else {}
    shadow_registers = (
        trace.get("shadow_registers") if isinstance(trace.get("shadow_registers"), dict) else {}
    )
    shader_route = (
        trace.get("shadow_shader_route") if isinstance(trace.get("shadow_shader_route"), dict) else {}
    )
    texture_shadow = (
        shadow_registers.get("texture_shadow")
        if isinstance(shadow_registers.get("texture_shadow"), dict)
        else {}
    )
    framebuffer_shadow = (
        shadow_registers.get("framebuffer_shadow")
        if isinstance(shadow_registers.get("framebuffer_shadow"), dict)
        else {}
    )
    depthmap = shadow_registers.get("depthmap") if isinstance(shadow_registers.get("depthmap"), dict) else {}

    return {
        "path": str(path),
        "status": trace.get("format", ""),
        "write_count": trace.get("write_count"),
        "register_count": trace.get("register_count"),
        "has_texture_shadow_register": bool(diagnostics.get("has_texture_shadow_register")),
        "has_framebuffer_shadow_register": bool(diagnostics.get("has_framebuffer_shadow_register")),
        "has_depthmap_scale_register": bool(diagnostics.get("has_depthmap_scale_register")),
        "has_depthmap_offset_register": bool(diagnostics.get("has_depthmap_offset_register")),
        "dmp_shadow_z_uniforms_present": bool(diagnostics.get("dmp_shadow_z_uniforms_present")),
        "texture_shadow_raw": texture_shadow.get("raw"),
        "texture_shadow_orthographic": texture_shadow.get("orthographic"),
        "texture_shadow_compare_bias": texture_shadow.get("compare_bias"),
        "framebuffer_shadow_raw": framebuffer_shadow.get("raw"),
        "framebuffer_shadow_constant_raw": framebuffer_shadow.get("constant_raw"),
        "framebuffer_shadow_linear_raw": framebuffer_shadow.get("linear_raw"),
        "framebuffer_shadow_constant": framebuffer_shadow.get("constant"),
        "framebuffer_shadow_linear": framebuffer_shadow.get("linear"),
        "depthmap_scale_raw": depthmap.get("scale_raw"),
        "depthmap_offset_raw": depthmap.get("offset_raw"),
        "fragment_lighting_enabled": bool(shader_route.get("fragment_lighting_enabled")),
        "lighting_config0_raw": shader_route.get("lighting_config0_raw"),
        "lighting_config1_raw": shader_route.get("lighting_config1_raw"),
        "lighting_enable_shadow": bool(shader_route.get("enable_shadow")),
        "lighting_shadow_primary": bool(shader_route.get("shadow_primary")),
        "shadow_texture_is_shadow2d": bool(shader_route.get("shadow_texture_is_shadow2d")),
        "shadow_texture_param_raw": shader_route.get("shadow_texture_param_raw"),
        "shadow_texture_dim_raw": shader_route.get("shadow_texture_dim_raw"),
        "matches_primary_rgb_shadow_term": bool(shader_route.get("matches_primary_rgb_shadow_term")),
    }


def build_summary(trace_paths: list[Path], label: str) -> dict[str, Any]:
    frames = [summarize_trace(path) for path in trace_paths]
    counters: dict[str, Counter[str]] = {
        "texture_shadow_raw": Counter(),
        "framebuffer_shadow_raw": Counter(),
        "depthmap_scale_raw": Counter(),
        "depthmap_offset_raw": Counter(),
        "lighting_config0_raw": Counter(),
        "lighting_config1_raw": Counter(),
        "shadow_texture_param_raw": Counter(),
        "shadow_texture_dim_raw": Counter(),
    }
    bool_fields = [
        "has_texture_shadow_register",
        "has_framebuffer_shadow_register",
        "has_depthmap_scale_register",
        "has_depthmap_offset_register",
        "dmp_shadow_z_uniforms_present",
        "fragment_lighting_enabled",
        "lighting_enable_shadow",
        "lighting_shadow_primary",
        "shadow_texture_is_shadow2d",
        "matches_primary_rgb_shadow_term",
    ]
    bool_counts: dict[str, Counter[str]] = {field: Counter() for field in bool_fields}

    for frame in frames:
        for field, counter in counters.items():
            add_counter(counter, frame.get(field))
        for field, counter in bool_counts.items():
            counter[str(bool(frame.get(field)))] += 1

    frame_count = len(frames)
    all_fragop_default = (
        frame_count > 0
        and counters["framebuffer_shadow_raw"] == Counter({"0x00003C00": frame_count})
    )
    all_texture_shadow_zero = (
        frame_count > 0
        and counters["texture_shadow_raw"] == Counter({"0x00000000": frame_count})
    )
    all_depthmap_scale_default = (
        frame_count > 0
        and counters["depthmap_scale_raw"] == Counter({"0x00BF0000": frame_count})
    )
    no_primary_shadow_route = (
        frame_count > 0
        and bool_counts["matches_primary_rgb_shadow_term"] == Counter({"False": frame_count})
    )
    no_dmp_uniforms = (
        frame_count > 0
        and bool_counts["dmp_shadow_z_uniforms_present"] == Counter({"False": frame_count})
    )

    return {
        "format": TRACE_FORMAT,
        "label": label,
        "source_kind": "azahar_native_pica_register_trace_validation_only",
        "runtime_source_policy": "trace_values_must_not_be_promoted_to_engine_runtime_data",
        "frame_count": frame_count,
        "frames": frames,
        "distinct_values": {
            field: counter_json(counter) for field, counter in counters.items()
        },
        "boolean_counts": {
            field: counter_json(counter) for field, counter in bool_counts.items()
        },
        "conclusions": {
            "all_fragop_shadow_payloads_match_codebin_default_0x00003c00": all_fragop_default,
            "all_texunit0_shadow_payloads_zero": all_texture_shadow_zero,
            "all_depthmap_scale_payloads_match_0x00bf0000": all_depthmap_scale_default,
            "no_primary_rgb_shadow_term_frames": no_primary_shadow_route,
            "no_dmp_shadow_z_uniform_uploads": no_dmp_uniforms,
            "validation_supports_default_or_inactive_shadow_route_for_capture": (
                all_fragop_default
                and all_texture_shadow_zero
                and no_primary_shadow_route
                and no_dmp_uniforms
            ),
        },
    }


def write_markdown(summary: dict[str, Any], output: Path) -> None:
    conclusions = summary["conclusions"]
    lines = [
        f"# {summary['label']} Shadow Register Validation",
        "",
        "This is validation-only evidence from Azahar PICA/register traces. These values must not be used as engine runtime data or as replacements for native OOT3D asset/code origins.",
        "",
        f"- Trace frame count: `{summary['frame_count']}`",
        f"- Source policy: `{summary['runtime_source_policy']}`",
        f"- All `GPUREG_FRAGOP_SHADOW` payloads are `0x00003C00`: `{str(conclusions['all_fragop_shadow_payloads_match_codebin_default_0x00003c00']).lower()}`",
        f"- All `GPUREG_TEXUNIT0_SHADOW` payloads are `0x00000000`: `{str(conclusions['all_texunit0_shadow_payloads_zero']).lower()}`",
        f"- All `GPUREG_DEPTHMAP_SCALE` payloads are `0x00BF0000`: `{str(conclusions['all_depthmap_scale_payloads_match_0x00bf0000']).lower()}`",
        f"- Frames matching primary RGB shadow term: {render_counts(summary['boolean_counts']['matches_primary_rgb_shadow_term'])}",
        f"- DMP shadow Z uniforms present: {render_counts(summary['boolean_counts']['dmp_shadow_z_uniforms_present'])}",
        "",
        "## Distinct Register Values",
        "",
    ]
    for field, values in summary["distinct_values"].items():
        lines.append(f"- `{field}`: {render_counts(values)}")
    lines.extend(
        [
            "",
            "## Interpretation",
            "",
            "For this capture, the trace validates a default/inactive Shadow2D register state rather than a non-default self-shadow pass. The engine must therefore keep the visual Shadow2D pass blocked for this fixture until a native owner chain supplies active `TEXUNIT0_SHADOW`, depth-map, and shader-route values from OOT3D data/code.",
            "",
        ]
    )
    output.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--trace-dir", required=True, type=Path)
    parser.add_argument("--label", default="oot3d_trace")
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-md", required=True, type=Path)
    args = parser.parse_args()

    trace_paths = sorted(args.trace_dir.glob("*.native_pica_register_trace.json"))
    if not trace_paths:
        raise SystemExit(f"no normalized trace JSON files found in {args.trace_dir}")

    summary = build_summary(trace_paths, args.label)
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    write_markdown(summary, args.output_md)


if __name__ == "__main__":
    main()
