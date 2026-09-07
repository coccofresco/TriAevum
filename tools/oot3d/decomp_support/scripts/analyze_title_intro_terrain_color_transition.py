#!/usr/bin/env python3
"""Summarize title-intro terrain lighting/color transition evidence.

The script consumes native runtime traces and demo render JSON stats. Emulator
or demo screenshots are validation evidence only; runtime values must stay
derived from OOT3D assets/code tables.
"""

from __future__ import annotations

import argparse
import json
import re
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def get_path(data: Any, dotted: str, default: Any = None) -> Any:
    current = data
    for part in dotted.split("."):
        if not isinstance(current, dict) or part not in current:
            return default
        current = current[part]
    return current


def color_tuple(value: Any) -> tuple[int, int, int] | None:
    if not isinstance(value, dict):
        return None
    try:
        return (int(value["r"]), int(value["g"]), int(value["b"]))
    except (KeyError, TypeError, ValueError):
        return None


def list_tuple(value: Any) -> tuple[int, ...] | None:
    if not isinstance(value, list):
        return None
    try:
        return tuple(int(item) for item in value)
    except (TypeError, ValueError):
        return None


def frame_from_path(path: Path) -> int | None:
    match = re.search(r"frame(\d+)", path.stem)
    return int(match.group(1)) if match else None


def compact_color(value: Any) -> list[int] | None:
    rgb = color_tuple(value)
    return list(rgb) if rgb is not None else None


def summarize_trace(trace: dict[str, Any]) -> dict[str, Any]:
    frames = trace.get("frames") or []
    events: list[dict[str, Any]] = []
    previous_key: tuple[Any, ...] | None = None
    first_day_time: int | None = None
    last_day_time: int | None = None
    set_time_frames: list[int] = []
    lighting_frames: list[int] = []
    color_addend_frames: list[int] = []

    for frame in frames:
        env = frame.get("environment") or {}
        frame_index = int(frame.get("frame") or 0)
        day_time = env.get("day_time")
        if isinstance(day_time, int):
            if first_day_time is None:
                first_day_time = day_time
            last_day_time = day_time
        if env.get("set_time_applied"):
            set_time_frames.append(frame_index)
        if env.get("environment_light_setting_applied"):
            lighting_frames.append(frame_index)
        if env.get("color_addend_ramp_active"):
            color_addend_frames.append(frame_index)

        key = (
            env.get("set_time_applied"),
            env.get("environment_light_setting_applied"),
            env.get("environment_light_setting_target"),
            env.get("environment_light_setting_raw_index"),
            tuple(env.get("ambient_color_addends") or []),
            tuple(env.get("light_color_addends") or []),
            tuple(env.get("fog_color_addends") or []),
            env.get("light_mode_current"),
            env.get("light_mode_target"),
            env.get("light_mode_blend_active"),
        )
        if previous_key is None or key != previous_key:
            row = env.get("environment_light_setting_row")
            set_time_row = env.get("set_time_row")
            events.append(
                {
                    "frame": frame_index,
                    "day_time": day_time,
                    "skybox_time": env.get("skybox_time"),
                    "set_time_applied": bool(env.get("set_time_applied")),
                    "set_time_start_frame": get_path(set_time_row, "start_frame"),
                    "light_setting_applied": bool(env.get("environment_light_setting_applied")),
                    "light_setting_resolved": bool(env.get("environment_light_setting_resolved")),
                    "light_setting_raw_index": env.get("environment_light_setting_raw_index"),
                    "light_setting_target": env.get("environment_light_setting_target"),
                    "light_setting_start_frame": env.get("environment_light_setting_start_frame"),
                    "light_setting_cutscene_source_index": get_path(row, "cutscene_source_index"),
                    "color_addend_ramp_active": bool(env.get("color_addend_ramp_active")),
                    "ambient_color_addends": env.get("ambient_color_addends"),
                    "light_color_addends": env.get("light_color_addends"),
                    "fog_color_addends": env.get("fog_color_addends"),
                    "light_mode_resolved": bool(env.get("light_mode_resolved")),
                    "light_mode_current": env.get("light_mode_current"),
                    "light_mode_target": env.get("light_mode_target"),
                    "light_mode_blend_active": bool(env.get("light_mode_blend_active")),
                    "light_mode_blend_weight": env.get("light_mode_blend_weight"),
                }
            )
            previous_key = key

    return {
        "qdb_index": trace.get("qdb_index"),
        "orchestration_index": trace.get("orchestration_index"),
        "cutscene_source_index": get_path(trace, "orchestration.cutscene_source_index"),
        "native_end_frame": trace.get("native_end_frame"),
        "sampled_frame_count": trace.get("sampled_frame_count"),
        "first_day_time": first_day_time,
        "last_day_time": last_day_time,
        "set_time_frames": set_time_frames,
        "lighting_command_frames": lighting_frames,
        "color_addend_ramp_frames": color_addend_frames,
        "state_change_events": events,
    }


def summarize_render_stats(path: Path) -> dict[str, Any]:
    data = load_json(path)
    title = data.get("title_intro_runtime") or {}
    scene = data.get("engine_render_scene") or {}
    lighting = scene.get("pica_lighting") or {}
    resolved = lighting.get("resolved_runtime_light_setting") or {}
    room = scene.get("room") or {}
    fog = scene.get("pica_fog") or {}
    background = scene.get("environment_background") or {}
    fast3d = data.get("fast3d_adapter_lifetime") or data.get("last_fast3d_adapter") or {}
    return {
        "path": str(path),
        "requested_frame_from_path": frame_from_path(path),
        "title_intro_frame": title.get("frame"),
        "day_time": title.get("runtime_environment_rendered_day_time"),
        "skybox_time": title.get("runtime_environment_rendered_skybox_time"),
        "room_average_vertex_rgb": compact_color(room.get("average_vertex_color")),
        "room_pica_lighting_batch_count": room.get("native_pica_lighting_batch_count"),
        "room_vertex_lighting_batch_count": room.get("native_pica_vertex_lighting_applied_batch_count"),
        "room_hemisphere_lighting_batch_count": room.get("native_pica_hemisphere_lighting_applied_batch_count"),
        "pica_lighting_available": lighting.get("available"),
        "pica_lighting_applied_batch_count": lighting.get("applied_batch_count"),
        "pica_lighting_applied_vertex_count": lighting.get("applied_vertex_count"),
        "resolved_light_branch": resolved.get("branch"),
        "resolved_active_angle": resolved.get("active_angle"),
        "resolved_current_mode": resolved.get("current_mode"),
        "resolved_target_mode": resolved.get("target_mode"),
        "resolved_current_entry_index": resolved.get("current_entry_index"),
        "resolved_from_to_light_setting": [
            resolved.get("current_from_light_setting_index"),
            resolved.get("current_to_light_setting_index"),
        ],
        "resolved_angle_weight": resolved.get("angle_weight"),
        "resolved_ambient_rgb": compact_color(resolved.get("ambient_color")),
        "resolved_light0_rgb": compact_color(resolved.get("light0_color")),
        "resolved_light1_rgb": compact_color(resolved.get("light1_color")),
        "resolved_final_fog_rgb": compact_color(resolved.get("final_fog_color")),
        "pica_fog_rgb": compact_color(fog.get("color")),
        "clear_rgb": compact_color(background.get("clear_color")),
        "backend_draw_call_count": fast3d.get("backend_draw_call_count"),
    }


def changed(values: list[Any]) -> bool:
    normalized = [json.dumps(value, sort_keys=True) for value in values if value is not None]
    return len(set(normalized)) > 1


def summarize(render_stats: list[dict[str, Any]], trace: dict[str, Any] | None) -> dict[str, Any]:
    sorted_stats = sorted(
        render_stats,
        key=lambda row: (
            row.get("day_time") is None,
            int(row.get("day_time") or 0),
            int(row.get("requested_frame_from_path") or 0),
        ),
    )
    return {
        "format": "oot3d_title_intro_terrain_color_transition_report_v1",
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "policy": {
            "runtime_source": "OOT3D native assets, extracted tables, and imported/decompiled code.bin behavior",
            "emulator_or_screenshot_role": "validation_only_not_runtime_data",
            "window_probe_policy": "use headless trace/self-test stats for intermediate probes; open the demo only on verified visual slices",
        },
        "trace": trace,
        "render_samples": sorted_stats,
        "checks": {
            "render_samples_present": len(sorted_stats) > 0,
            "terrain_average_vertex_color_changes": changed(
                [row.get("room_average_vertex_rgb") for row in sorted_stats]
            ),
            "fog_color_changes": changed([row.get("pica_fog_rgb") for row in sorted_stats]),
            "clear_color_changes": changed([row.get("clear_rgb") for row in sorted_stats]),
            "ambient_color_changes": changed([row.get("resolved_ambient_rgb") for row in sorted_stats]),
            "light0_color_changes": changed([row.get("resolved_light0_rgb") for row in sorted_stats]),
            "all_samples_use_transition_table_branch": all(
                row.get("resolved_light_branch") == "transition_table_mode_angle"
                for row in sorted_stats
            )
            if sorted_stats
            else False,
            "all_samples_have_room_lighting_batches": all(
                int(row.get("room_pica_lighting_batch_count") or 0) > 0
                for row in sorted_stats
            )
            if sorted_stats
            else False,
        },
    }


def write_markdown(path: Path, report: dict[str, Any]) -> None:
    trace = report.get("trace") or {}
    lines = [
        "# Title Intro Terrain Color Transition",
        "",
        "## Scope",
        "",
        "This report validates terrain/environment color changes from native OOT3D runtime data. It does not consume emulator or screenshot RGB as runtime input.",
        "",
        "## Native Runtime",
        "",
        f"- QDB index: `{trace.get('qdb_index')}`.",
        f"- Cutscene source index: `{trace.get('cutscene_source_index')}`.",
        f"- Native end frame: `{trace.get('native_end_frame')}`.",
        f"- Sampled dayTime range: `{trace.get('first_day_time')}` -> `{trace.get('last_day_time')}`.",
        f"- SETTIME frames: `{trace.get('set_time_frames')}`.",
        f"- SET_LIGHTING command frames in sampled runtime: `{trace.get('lighting_command_frames')}`.",
        "",
        "## Render Samples",
        "",
        "| frame | dayTime | branch | entry | weight | terrain avg RGB | ambient RGB | light0 RGB | fog/clear RGB |",
        "| --- | ---: | --- | ---: | ---: | --- | --- | --- | --- |",
    ]
    for row in report.get("render_samples") or []:
        lines.append(
            f"| `{row.get('requested_frame_from_path')}` | `{row.get('day_time')}` | "
            f"`{row.get('resolved_light_branch')}` | `{row.get('resolved_current_entry_index')}` | "
            f"`{row.get('resolved_angle_weight')}` | `{row.get('room_average_vertex_rgb')}` | "
            f"`{row.get('resolved_ambient_rgb')}` | `{row.get('resolved_light0_rgb')}` | "
            f"`{row.get('pica_fog_rgb')}` / `{row.get('clear_rgb')}` |"
        )
    lines.extend(["", "## Checks", ""])
    for key, value in (report.get("checks") or {}).items():
        lines.append(f"- `{key}`: `{value}`.")
    lines.append("")
    path.write_text("\n".join(lines), encoding="ascii")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runtime-trace", type=Path)
    parser.add_argument("--render-stats", type=Path, nargs="+", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--markdown-output", type=Path)
    args = parser.parse_args()

    trace_summary = summarize_trace(load_json(args.runtime_trace)) if args.runtime_trace else None
    render_stats = [summarize_render_stats(path) for path in args.render_stats]
    report = summarize(render_stats, trace_summary)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding="ascii")
    if args.markdown_output:
        args.markdown_output.parent.mkdir(parents=True, exist_ok=True)
        write_markdown(args.markdown_output, report)
    print(json.dumps({"output": str(args.output), "markdown_output": str(args.markdown_output)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
