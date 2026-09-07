#!/usr/bin/env python3
"""Compare the title-intro slot 6 native demo probe against an Azahar PICA dump.

The emulator capture is validation-only evidence. This script reports where the
demo's native OOT3D data/code interpretation agrees with the captured PICA state
and where the next source/decompilation work should focus.
"""

from __future__ import annotations

import argparse
import json
import math
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def get_path(data: dict[str, Any], dotted: str, default: Any = None) -> Any:
    current: Any = data
    for part in dotted.split("."):
        if not isinstance(current, dict) or part not in current:
            return default
        current = current[part]
    return current


def rgb_from_mapping(value: Any) -> list[int] | None:
    if not isinstance(value, dict):
        return None
    keys = ("r", "g", "b")
    if not all(key in value for key in keys):
        return None
    try:
        return [int(value[key]) for key in keys]
    except (TypeError, ValueError):
        return None


def rgb_key_to_list(key: str | None) -> list[int] | None:
    if not key:
        return None
    parts = key.split(",")
    if len(parts) != 3:
        return None
    try:
        return [int(part) for part in parts]
    except ValueError:
        return None


def dominant_fog_color(draw_summary: dict[str, Any]) -> list[int] | None:
    colors = draw_summary.get("fog_color_counts") or {}
    if not isinstance(colors, dict) or not colors:
        return None
    key, _count = max(colors.items(), key=lambda item: int(item[1] or 0))
    return rgb_key_to_list(str(key))


def texture0(draw: dict[str, Any] | None) -> dict[str, Any] | None:
    if not isinstance(draw, dict):
        return None
    for texture in draw.get("textures") or []:
        if texture.get("index") == 0:
            return texture
    return None


def compact_kankyo_draws(frame: dict[str, Any], expected_vertices: int) -> list[dict[str, Any]]:
    draws = []
    for draw in frame.get("draw_events_compact") or []:
        if draw.get("num_vertices") != expected_vertices:
            continue
        tex0 = texture0(draw)
        if not tex0 or not tex0.get("enabled"):
            continue
        draws.append(draw)
    return draws


def vector3_from_mapping(value: Any) -> list[float] | None:
    if not isinstance(value, dict):
        return None
    keys = ("x", "y", "z")
    if not all(key in value for key in keys):
        return None
    try:
        return [float(value[key]) for key in keys]
    except (TypeError, ValueError):
        return None


def pica_eye_from_draw(draw: dict[str, Any]) -> list[float] | None:
    uniforms = draw.get("vs_uniforms_f76_f86")
    if not isinstance(uniforms, dict):
        return None
    try:
        f76 = uniforms["f76"]
        f77 = uniforms["f77"]
        f78 = uniforms["f78"]
        return [float(f76[3]), float(f77[3]), float(f78[3])]
    except (KeyError, IndexError, TypeError, ValueError):
        return None


def dominant_emulator_eye(frame: dict[str, Any]) -> list[float] | None:
    for draw in frame.get("draw_events_compact") or []:
        eye = pica_eye_from_draw(draw)
        if eye is not None:
            return eye
    return None


def distance3(a: list[float] | None, b: list[float] | None) -> float | None:
    if a is None or b is None:
        return None
    return math.sqrt(sum((float(x) - float(y)) ** 2 for x, y in zip(a, b, strict=True)))


def check(status: str, check_id: str, severity: str, summary: str, **details: Any) -> dict[str, Any]:
    return {
        "id": check_id,
        "status": status,
        "severity": severity,
        "summary": summary,
        "details": details,
    }


def summarize(emulator: dict[str, Any], demo: dict[str, Any], emulator_path: Path, demo_path: Path) -> dict[str, Any]:
    frame = emulator.get("frame") or {}
    draw_summary = frame.get("draw_summary") or {}
    fast3d = demo.get("fast3d_adapter_lifetime") or {}
    fast3d_lifetime_sampled = bool(fast3d)
    engine_scene = demo.get("engine_render_scene") or {}
    environment_background = engine_scene.get("environment_background") or {}
    primitive_backend_input = get_path(
        engine_scene,
        "environment_background.native_kankyo_lens_effect.primitive_backend_input",
        {},
    ) or {}
    title_runtime = demo.get("title_intro_runtime") or {}
    pica_fog = get_path(engine_scene, "pica_fog", {}) or {}
    actor_vs_packet = get_path(engine_scene, "pica_lighting.actor_vs_light_packet", {}) or {}
    camera_segment = title_runtime.get("camera_segment") or {}
    emulator_eye = dominant_emulator_eye(frame)
    demo_eye = vector3_from_mapping(get_path(demo, "camera.position"))
    eye_distance = distance3(emulator_eye, demo_eye)
    camera_match_threshold = 20.0
    camera_match = eye_distance is None or eye_distance <= camera_match_threshold

    expected_vertices = int(fast3d.get("native_kankyo_primitive_expected_expanded_vertex_count") or 78)
    kankyo_draws = compact_kankyo_draws(frame, expected_vertices)
    kankyo_draw = kankyo_draws[0] if kankyo_draws else None
    kankyo_tex0 = texture0(kankyo_draw)

    emulator_fog_color = dominant_fog_color(draw_summary)
    demo_fog_color = rgb_from_mapping(fast3d.get("native_pica_fog_color"))
    if demo_fog_color is None:
        demo_fog_color = rgb_from_mapping(get_path(engine_scene, "pica_fog.color"))
    demo_fog_source = fast3d.get("native_pica_fog_source_kind") or pica_fog.get("source_kind")
    demo_fog_preaddend = rgb_from_mapping(pica_fog.get("preaddend_color"))
    demo_fog_final = rgb_from_mapping(pica_fog.get("final_color"))
    demo_actor_vs_fog_color = rgb_from_mapping(actor_vs_packet.get("pica_fog_color"))
    fog_color_delta = (
        [int(demo_fog_color[i]) - int(emulator_fog_color[i]) for i in range(3)]
        if emulator_fog_color is not None and demo_fog_color is not None
        else None
    )
    fog_color_max_abs_delta = (
        max(abs(value) for value in fog_color_delta)
        if fog_color_delta is not None
        else None
    )
    fog_color_within_one_lsb = (
        fog_color_max_abs_delta is not None and fog_color_max_abs_delta <= 1
    )
    if emulator_fog_color == demo_fog_color:
        fog_color_status = "pass"
    elif camera_match and fog_color_within_one_lsb:
        fog_color_status = "warn"
    elif camera_match:
        fog_color_status = "fail"
    else:
        fog_color_status = "warn"

    checks: list[dict[str, Any]] = []

    checks.append(
        check(
            "info",
            "title_intro_demo_timeline_context",
            "medium",
            "The demo/emulator comparison must name the sampled title-intro frame; slot 6 camera validation and frame-600 fog validation are different evidence slices.",
            demo_title_intro_frame=title_runtime.get("frame"),
            demo_initial_scene_cutscene_frame=title_runtime.get("initial_scene_cutscene_frame"),
            demo_runtime_day_time=title_runtime.get("runtime_environment_rendered_day_time"),
            demo_runtime_skybox_time=title_runtime.get("runtime_environment_rendered_skybox_time"),
            demo_camera_segment_index=camera_segment.get("segment_index"),
            demo_camera_segment_start_frame=camera_segment.get("start_frame"),
            demo_camera_segment_end_frame=camera_segment.get("end_frame"),
        )
    )

    checks.append(
        check(
            "pass" if camera_match else "fail",
            "title_intro_camera_match_for_color_validation",
            "high",
            "Color, fog, and terrain lighting deltas are meaningful only when the emulator and demo samples are from the same camera/timeline slice.",
            emulator_pica_eye=emulator_eye,
            demo_camera_position=demo_eye,
            eye_distance=eye_distance,
            threshold=camera_match_threshold,
            demo_title_intro_frame=title_runtime.get("frame"),
            demo_camera_segment_index=camera_segment.get("segment_index"),
        )
    )

    demo_visible_vertices = fast3d.get("native_kankyo_primitive_visible_vertex_count")
    if not fast3d_lifetime_sampled:
        kankyo_vertex_status = "info"
        kankyo_vertex_summary = (
            "This demo stats file does not include Fast3D lifetime counters; "
            "Kankyo primitive visibility was not sampled in this fog/runtime probe."
        )
    else:
        kankyo_vertex_status = "pass" if kankyo_draw and demo_visible_vertices == expected_vertices else "fail"
        kankyo_vertex_summary = "Visible Kankyo primitive draw count should match the native 28C quad expansion."
    checks.append(
        check(
            kankyo_vertex_status,
            "kankyo_primitive_vertex_count",
            "high",
            kankyo_vertex_summary,
            demo_fast3d_lifetime_sampled=fast3d_lifetime_sampled,
            emulator_candidate_count=len(kankyo_draws),
            emulator_candidate_draw_index=kankyo_draw.get("draw_index") if kankyo_draw else None,
            expected_vertices=expected_vertices,
            demo_visible_vertices=demo_visible_vertices,
        )
    )

    if not fast3d_lifetime_sampled:
        kankyo_fog_status = "info"
        kankyo_fog_summary = (
            "This demo stats file does not include Fast3D lifetime counters; "
            "Kankyo fog backend sampling is unchanged by this fog/runtime probe."
        )
    else:
        kankyo_fog_status = (
            "pass"
            if kankyo_draw
            and bool(kankyo_draw.get("fog_enabled"))
            and bool(fast3d.get("native_kankyo_primitive_pica_fog_applied"))
            else "fail"
        )
        kankyo_fog_summary = "Kankyo primitive should use the same PICA fog shader path as ordinary native batches."
    checks.append(
        check(
            kankyo_fog_status,
            "kankyo_primitive_pica_fog_backend",
            "high",
            kankyo_fog_summary,
            demo_fast3d_lifetime_sampled=fast3d_lifetime_sampled,
            emulator_fog_enabled=kankyo_draw.get("fog_enabled") if kankyo_draw else None,
            emulator_fog_mode=kankyo_draw.get("fog_mode") if kankyo_draw else None,
            demo_pica_fog_decoded=fast3d.get("native_kankyo_primitive_pica_fog_decoded"),
            demo_pica_fog_applied=fast3d.get("native_kankyo_primitive_pica_fog_applied"),
            demo_pica_fog_pending=fast3d.get("native_kankyo_primitive_pica_fog_pending"),
        )
    )

    if not fast3d_lifetime_sampled:
        kankyo_texture_status = "info"
        kankyo_texture_summary = (
            "This demo stats file exposes native Kankyo CTXB selection but not backend texture-submit counters."
        )
    else:
        kankyo_texture_status = (
            "pass" if kankyo_tex0 and fast3d.get("native_kankyo_primitive_decoded_texture_submit_count") else "warn"
        )
        kankyo_texture_summary = (
            "The emulator Kankyo draw binds a native texture and the demo must submit a decoded CTXB texture."
        )
    checks.append(
        check(
            kankyo_texture_status,
            "kankyo_primitive_texture_presence",
            "medium",
            kankyo_texture_summary,
            demo_fast3d_lifetime_sampled=fast3d_lifetime_sampled,
            emulator_texture0=kankyo_tex0,
            demo_texture_name=fast3d.get("native_kankyo_primitive_texture_name"),
            demo_decoded_texture_submit_count=fast3d.get("native_kankyo_primitive_decoded_texture_submit_count"),
            demo_native_kankyo_ctxb_names=environment_background.get("native_kankyo_skybox_1d_extra_ctxb_names"),
        )
    )

    checks.append(
        check(
            fog_color_status,
            "title_intro_fog_color_source_selection",
            "high",
            "Fog color must be selected from the same native OOT3D runtime source that feeds GPUREG_FOG_COLOR.",
            emulator_dominant_fog_color=emulator_fog_color,
            demo_native_pica_fog_color=demo_fog_color,
            color_delta=fog_color_delta,
            max_abs_color_delta=fog_color_max_abs_delta,
            within_one_lsb=fog_color_within_one_lsb,
            comparison_camera_matched=camera_match,
            emulator_pica_eye=emulator_eye,
            demo_camera_position=demo_eye,
            eye_distance=eye_distance,
            demo_fog_source=demo_fog_source,
            demo_engine_fog_color_source=pica_fog.get("color_source"),
            demo_engine_fog_preaddend_color=demo_fog_preaddend,
            demo_engine_fog_color_addend_i16=pica_fog.get("color_addend_i16"),
            demo_engine_fog_final_color=demo_fog_final,
            demo_engine_fog_final_formula_resolved=pica_fog.get("runtime_final_fog_color_formula_resolved"),
            demo_engine_fog_addend_resolved=pica_fog.get("runtime_fog_color_addend_resolved"),
            demo_actor_vs_material_packet_fog_color=demo_actor_vs_fog_color,
            demo_actor_vs_material_packet_fog_source=actor_vs_packet.get("pica_fog_color_source"),
            demo_material_scalar_runtime_list_binder_resolved=primitive_backend_input.get(
                "material_scalar_runtime_list_binder_resolved"
            ),
            demo_material_scalar_runtime_list_source_matches_fog_source=primitive_backend_input.get(
                "material_scalar_runtime_list_source_matches_fog_source"
            ),
            demo_material_scalar_runtime_list_packet_resolver_resolved=primitive_backend_input.get(
                "material_scalar_runtime_list_packet_resolver_resolved"
            ),
            demo_material_scalar_gameplay_draw_order_resolved=primitive_backend_input.get(
                "material_scalar_gameplay_draw_order_resolved"
            ),
            demo_material_scalar_runtime_list_source_status=primitive_backend_input.get(
                "material_scalar_runtime_list_source_status"
            ),
            demo_resolved_runtime_light_source=get_path(
                engine_scene,
                "pica_lighting.resolved_runtime_light_setting.source",
                get_path(engine_scene, "pica_lighting.resolved_runtime_light_setting.branch"),
            ),
            next_action=(
                "Resolve/import the native state selection and timing for the material-scalar fog "
                "source updated by 00464B2C. Gameplay_Draw passes play+0x0A82/83/84 into 00464B2C, "
                "while the resolved 002D960C/003687A8/00368704 binder propagates the active source "
                "object into object+0x04..0x0C and the PICA packet. The remaining mismatch is the "
                "producer/selector for the source object active at the draw, not the runtime list "
                "binder; do not copy the emulator RGB as runtime data."
            ),
        )
    )

    shadow2d_count = int(draw_summary.get("shadow2d_texture_draw_count") or 0)
    primary_shadow_count = int(draw_summary.get("primary_rgb_shadow_route_draw_count") or 0)
    checks.append(
        check(
            "pass" if shadow2d_count == 0 and primary_shadow_count == 0 else "warn",
            "slot6_shadow2d_scope",
            "low",
            "This slot-6 frame should not drive the next implementation step toward Shadow2D.",
            emulator_shadow2d_texture_draw_count=shadow2d_count,
            emulator_primary_rgb_shadow_route_draw_count=primary_shadow_count,
            demo_shadow2d_backend_pending=fast3d.get("native_pica_shadow2d_backend_pass_pending"),
        )
    )

    fragment_lighting_count = int(draw_summary.get("fragment_lighting_enabled_draw_count") or 0)
    checks.append(
        check(
            "pass" if fragment_lighting_count == 0 else "warn",
            "slot6_fragment_lighting_scope",
            "low",
            "The captured title-intro frame does not require fragment-lighting work before the fog/material source mismatch.",
            emulator_fragment_lighting_enabled_draw_count=fragment_lighting_count,
            demo_lut_input_fragment_lighting_batch_count=fast3d.get(
                "native_pica_material_lut_input_fragment_lighting_batch_count"
            ),
        )
    )

    return {
        "format": "oot3d_title_intro_slot6_demo_vs_emulator_gap_v2",
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "strategy": {
            "baseline": "slot6_focused_capture",
            "reason": (
                "Savestate slot 6 starts inside the target open-title intro and avoids boot/loading "
                "filtering. Boot-from-start capture is deferred until exact pre-intro/logo entry "
                "state is required."
            ),
            "emulator_role": (
                "Validation and PICA backend reference only; demo runtime data must come from "
                "OOT3D assets, code.bin-derived tables, and decompiled native behavior."
            ),
            "pica_reference": [
                "E:/azahar pcvr/src/video_core/renderer_software/sw_rasterizer.cpp::WriteFog",
                "E:/azahar pcvr/src/video_core/shader/generator/glsl_fs_shader_gen.cpp::WriteFog",
            ],
        },
        "inputs": {
            "emulator_summary": str(emulator_path),
            "demo_stats": str(demo_path),
        },
        "emulator": {
            "pica_eye": emulator_eye,
            "draw_count": draw_summary.get("draw_count"),
            "fog_enabled_draw_count": draw_summary.get("fog_enabled_draw_count"),
            "fog_color_counts": draw_summary.get("fog_color_counts"),
            "dominant_fog_color": emulator_fog_color,
            "fragment_lighting_enabled_draw_count": draw_summary.get("fragment_lighting_enabled_draw_count"),
            "shadow2d_texture_draw_count": draw_summary.get("shadow2d_texture_draw_count"),
            "primary_rgb_shadow_route_draw_count": draw_summary.get("primary_rgb_shadow_route_draw_count"),
            "kankyo_candidate_draw": kankyo_draw,
        },
        "demo": {
            "fast3d_lifetime_sampled": fast3d_lifetime_sampled,
            "camera_position": demo_eye,
            "emulator_demo_eye_distance": eye_distance,
            "camera_match_for_color_validation": camera_match,
            "title_intro_frame": title_runtime.get("frame"),
            "initial_scene_cutscene_frame": title_runtime.get("initial_scene_cutscene_frame"),
            "runtime_day_time": title_runtime.get("runtime_environment_rendered_day_time"),
            "runtime_skybox_time": title_runtime.get("runtime_environment_rendered_skybox_time"),
            "camera_segment": {
                "segment_index": camera_segment.get("segment_index"),
                "start_frame": camera_segment.get("start_frame"),
                "end_frame": camera_segment.get("end_frame"),
            },
            "backend_draw_call_count": fast3d.get("backend_draw_call_count"),
            "native_kankyo_primitive_visible_draw_call_count": fast3d.get(
                "native_kankyo_primitive_visible_draw_call_count"
            ),
            "native_kankyo_primitive_visible_vertex_count": demo_visible_vertices,
            "native_kankyo_primitive_visible_triangle_count": fast3d.get(
                "native_kankyo_primitive_visible_triangle_count"
            ),
            "native_kankyo_primitive_texture_name": fast3d.get("native_kankyo_primitive_texture_name"),
            "native_kankyo_primitive_pica_fog_decoded": fast3d.get("native_kankyo_primitive_pica_fog_decoded"),
            "native_kankyo_primitive_pica_fog_applied": fast3d.get("native_kankyo_primitive_pica_fog_applied"),
            "native_kankyo_primitive_pica_fog_pending": fast3d.get("native_kankyo_primitive_pica_fog_pending"),
            "native_pica_fog_color": demo_fog_color,
            "native_pica_fog_source_kind": demo_fog_source,
            "engine_fog_color_source": pica_fog.get("color_source"),
            "engine_fog_preaddend_color": demo_fog_preaddend,
            "engine_fog_color_addend_i16": pica_fog.get("color_addend_i16"),
            "engine_fog_final_color": demo_fog_final,
            "engine_fog_addend_resolved": pica_fog.get("runtime_fog_color_addend_resolved"),
            "actor_vs_material_packet_fog_color": demo_actor_vs_fog_color,
            "actor_vs_material_packet_fog_source": actor_vs_packet.get("pica_fog_color_source"),
            "material_scalar_runtime_list_binder_resolved": primitive_backend_input.get(
                "material_scalar_runtime_list_binder_resolved"
            ),
            "material_scalar_runtime_list_source_matches_fog_source": primitive_backend_input.get(
                "material_scalar_runtime_list_source_matches_fog_source"
            ),
            "material_scalar_runtime_list_packet_resolver_resolved": primitive_backend_input.get(
                "material_scalar_runtime_list_packet_resolver_resolved"
            ),
            "material_scalar_gameplay_draw_order_resolved": primitive_backend_input.get(
                "material_scalar_gameplay_draw_order_resolved"
            ),
            "material_scalar_runtime_list_source_status": primitive_backend_input.get(
                "material_scalar_runtime_list_source_status"
            ),
        },
        "checks": checks,
    }


def write_markdown(path: Path, report: dict[str, Any]) -> None:
    lines = [
        "# Title Intro Slot 6 Demo vs Emulator",
        "",
        "## Strategy",
        "",
        f"- Baseline: `{report['strategy']['baseline']}`.",
        f"- Reason: {report['strategy']['reason']}",
        f"- Emulator role: {report['strategy']['emulator_role']}",
        "",
        "## Summary",
        "",
        f"- Emulator draws: `{report['emulator'].get('draw_count')}`.",
        f"- Emulator fog-enabled draws: `{report['emulator'].get('fog_enabled_draw_count')}`.",
        f"- Emulator dominant fog RGB: `{report['emulator'].get('dominant_fog_color')}`.",
        f"- Emulator PICA eye / demo camera / distance: `{report['emulator'].get('pica_eye')}` / "
        f"`{report['demo'].get('camera_position')}` / "
        f"`{report['demo'].get('emulator_demo_eye_distance')}`.",
        f"- Demo title-intro frame: `{report['demo'].get('title_intro_frame')}` "
        f"(cutscene `{report['demo'].get('initial_scene_cutscene_frame')}`, "
        f"dayTime `{report['demo'].get('runtime_day_time')}`).",
        f"- Demo fog RGB: `{report['demo'].get('native_pica_fog_color')}`.",
        f"- Demo fog pre-addend/addend/final: "
        f"`{report['demo'].get('engine_fog_preaddend_color')}` + "
        f"`{report['demo'].get('engine_fog_color_addend_i16')}` -> "
        f"`{report['demo'].get('engine_fog_final_color')}`.",
        f"- Demo actor/material packet fog RGB: `{report['demo'].get('actor_vs_material_packet_fog_color')}`.",
        f"- Demo material runtime list binder/resolver: "
        f"`{report['demo'].get('material_scalar_runtime_list_binder_resolved')}` / "
        f"`{report['demo'].get('material_scalar_runtime_list_packet_resolver_resolved')}`.",
        f"- Demo material Gameplay_Draw order resolved: "
        f"`{report['demo'].get('material_scalar_gameplay_draw_order_resolved')}`.",
        f"- Demo Kankyo draw vertices: `{report['demo'].get('native_kankyo_primitive_visible_vertex_count')}`.",
        f"- Demo Kankyo PICA fog applied: `{report['demo'].get('native_kankyo_primitive_pica_fog_applied')}`.",
        "",
        "## Checks",
        "",
    ]
    for item in report.get("checks") or []:
        lines.append(
            f"- `{item['id']}`: `{item['status']}` ({item['severity']}) - {item['summary']}"
        )
    lines.append("")
    lines.append(
        "The emulator RGB/register values are validation evidence, not replacement runtime data."
    )
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--emulator-summary", type=Path, required=True)
    parser.add_argument("--demo-stats", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=None)
    parser.add_argument("--markdown-output", type=Path, default=None)
    args = parser.parse_args()

    emulator = load_json(args.emulator_summary)
    demo = load_json(args.demo_stats)
    report = summarize(emulator, demo, args.emulator_summary, args.demo_stats)

    output = args.output or args.emulator_summary.with_name("title_intro_slot6_demo_vs_emulator_gap.json")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2), encoding="ascii")

    markdown_output = args.markdown_output or output.with_suffix(".md")
    write_markdown(markdown_output, report)

    print(json.dumps({"output": str(output), "markdown_output": str(markdown_output)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
