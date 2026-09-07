#!/usr/bin/env python3
"""Analyze the title-intro fog source gap across demo frame probes.

The emulator capture is validation-only evidence. This report keeps the runtime
work pointed at OOT3D assets/code.bin-derived behavior by proving whether the
demo is still selecting global environment fog or a resolved material packet
source.
"""

from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


DEFAULT_DEMO_STATS = [
    Path("build-codex/title_intro_material_scalar_runtime_list_launch.json"),
    Path("build-codex/title_intro_frame_1_fog_probe.json"),
    Path("build-codex/title_intro_frame_30_fog_probe.json"),
    Path("build-codex/title_intro_frame_60_fog_probe.json"),
    Path("build-codex/title_intro_frame_90_fog_probe.json"),
    Path("build-codex/title_intro_frame_120_fog_probe.json"),
]


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
    if not all(key in value for key in ("r", "g", "b")):
        return None
    try:
        return [int(value["r"]), int(value["g"]), int(value["b"])]
    except (TypeError, ValueError):
        return None


def rgb_key_to_list(value: str | None) -> list[int] | None:
    if not value:
        return None
    parts = value.split(",")
    if len(parts) != 3:
        return None
    try:
        return [int(part) for part in parts]
    except ValueError:
        return None


def dominant_fog_color(emulator: dict[str, Any]) -> list[int] | None:
    colors = get_path(emulator, "frame.draw_summary.fog_color_counts", {}) or {}
    if not isinstance(colors, dict) or not colors:
        return None
    key, _count = max(colors.items(), key=lambda item: int(item[1] or 0))
    return rgb_key_to_list(str(key))


def unique_colors(colors: list[list[int] | None]) -> list[list[int] | None]:
    seen: set[str] = set()
    result: list[list[int] | None] = []
    for color in colors:
        key = "none" if color is None else ",".join(str(part) for part in color)
        if key in seen:
            continue
        seen.add(key)
        result.append(color)
    return result


def demo_record(path: Path, data: dict[str, Any]) -> dict[str, Any]:
    engine_scene = data.get("engine_render_scene") or {}
    pica_fog = engine_scene.get("pica_fog") or {}
    actor_vs_packet = get_path(engine_scene, "pica_lighting.actor_vs_light_packet", {}) or {}
    runtime_light = get_path(engine_scene, "pica_lighting.resolved_runtime_light_setting", {}) or {}
    primitive_input = get_path(
        engine_scene,
        "environment_background.native_kankyo_lens_effect.primitive_backend_input",
        {},
    ) or {}
    fast3d = data.get("fast3d_adapter_lifetime") or {}
    title_runtime = data.get("title_intro_runtime") or {}

    selected_fog = rgb_from_mapping(fast3d.get("native_pica_fog_color"))
    if selected_fog is None:
        selected_fog = rgb_from_mapping(pica_fog.get("color"))

    return {
        "path": str(path),
        "exists": True,
        "title_intro_frame": title_runtime.get("frame"),
        "initial_scene_cutscene_frame": title_runtime.get("initial_scene_cutscene_frame"),
        "runtime_day_time": title_runtime.get("runtime_environment_rendered_day_time"),
        "runtime_skybox_time": title_runtime.get("runtime_environment_rendered_skybox_time"),
        "selected_fog_color": selected_fog,
        "selected_fog_source_kind": fast3d.get("native_pica_fog_source_kind") or pica_fog.get("source_kind"),
        "selected_fog_color_source": pica_fog.get("color_source"),
        "selected_fog_used_for_render": fast3d.get("native_pica_fog_used_for_render"),
        "environment_final_fog_color": rgb_from_mapping(runtime_light.get("final_fog_color")),
        "environment_final_fog_used_for_render": runtime_light.get("runtime_final_fog_color_used_for_render"),
        "environment_fog_addend_resolved": runtime_light.get("runtime_fog_color_addend_resolved"),
        "actor_vs_material_packet_fog_color": rgb_from_mapping(actor_vs_packet.get("pica_fog_color")),
        "actor_vs_material_packet_fog_source": actor_vs_packet.get("pica_fog_color_source"),
        "material_scalar_runtime_list_binder_resolved": primitive_input.get(
            "material_scalar_runtime_list_binder_resolved"
        ),
        "material_scalar_runtime_list_source_matches_fog_source": primitive_input.get(
            "material_scalar_runtime_list_source_matches_fog_source"
        ),
        "material_scalar_runtime_list_packet_resolver_resolved": primitive_input.get(
            "material_scalar_runtime_list_packet_resolver_resolved"
        ),
        "material_scalar_gameplay_draw_order_resolved": primitive_input.get(
            "material_scalar_gameplay_draw_order_resolved"
        ),
        "material_scalar_runtime_list_source_status": primitive_input.get(
            "material_scalar_runtime_list_source_status"
        ),
    }


def summarize(
    emulator: dict[str, Any],
    emulator_summary_path: Path,
    demo_paths: list[Path],
) -> dict[str, Any]:
    emulator_fog = dominant_fog_color(emulator)
    records: list[dict[str, Any]] = []
    missing: list[str] = []

    for path in demo_paths:
        if not path.exists():
            missing.append(str(path))
            continue
        records.append(demo_record(path, load_json(path)))

    selected_colors = [record.get("selected_fog_color") for record in records]
    actor_colors = [record.get("actor_vs_material_packet_fog_color") for record in records]
    selected_matches = [color == emulator_fog for color in selected_colors]
    actor_matches = [color == emulator_fog for color in actor_colors]
    binder_flags = [
        bool(record.get("material_scalar_runtime_list_binder_resolved"))
        and bool(record.get("material_scalar_runtime_list_packet_resolver_resolved"))
        and bool(record.get("material_scalar_runtime_list_source_matches_fog_source"))
        for record in records
    ]
    gameplay_order_flags = [
        bool(record.get("material_scalar_gameplay_draw_order_resolved"))
        for record in records
        if record.get("material_scalar_gameplay_draw_order_resolved") is not None
    ]

    return {
        "format": "oot3d_title_intro_material_fog_source_gap_v1",
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "inputs": {
            "emulator_summary": str(emulator_summary_path),
            "demo_stats": [str(path) for path in demo_paths],
            "missing_demo_stats": missing,
        },
        "emulator_reference": {
            "dominant_fog_color": emulator_fog,
            "fog_color_counts": get_path(emulator, "frame.draw_summary.fog_color_counts", {}),
            "fog_enabled_draw_count": get_path(emulator, "frame.draw_summary.fog_enabled_draw_count"),
            "draw_count": get_path(emulator, "frame.draw_summary.draw_count"),
        },
        "demo_records": records,
        "diagnosis": {
            "selected_demo_fog_colors": unique_colors(selected_colors),
            "actor_vs_material_packet_fog_colors": unique_colors(actor_colors),
            "any_selected_fog_matches_emulator": any(selected_matches),
            "any_actor_vs_material_packet_fog_matches_emulator": any(actor_matches),
            "material_runtime_list_route_resolved_for_all_samples": bool(records) and all(binder_flags),
            "gameplay_draw_order_resolved_for_reported_samples": (
                bool(gameplay_order_flags) and all(gameplay_order_flags)
            ),
            "current_gap": (
                "material_scalar_state_selection_timing_pending"
                if records and all(binder_flags) and not any(selected_matches)
                else "needs_additional_evidence"
            ),
            "source_selection_conclusion": (
                "The resolved runtime list binder reaches the FogResUpdater/material-scalar route. "
                "Native Gameplay_Draw passes play+0x0A82/83/84 into 00464B2C, which then writes "
                "object+0x04..0x0C and the material packet for the active source object. The missing "
                "native piece is therefore the producer/selector and timing for that material-scalar "
                "source object at the draw, not the binder or a copied material packet color."
            ),
            "next_workorder": (
                "Trace/decompile the code.bin writer/selector that chooses the runtime source object "
                "resolved by 003687A8 and updated by 00464B2C for the title-intro draw, including "
                "which play+0x0A82/83/84 sample is latched into object+0x04..0x0C. Expose that as "
                "native material-scalar fog input in the engine. Keep the emulator RGB as validation "
                "only."
            ),
        },
    }


def write_markdown(path: Path, report: dict[str, Any]) -> None:
    diagnosis = report["diagnosis"]
    reference = report["emulator_reference"]
    lines = [
        "# Title Intro Material Fog Source Gap",
        "",
        "## Result",
        "",
        f"- Emulator dominant fog RGB: `{reference.get('dominant_fog_color')}`.",
        f"- Demo selected fog RGB samples: `{diagnosis.get('selected_demo_fog_colors')}`.",
        f"- Demo actor/material packet fog RGB samples: `{diagnosis.get('actor_vs_material_packet_fog_colors')}`.",
        f"- Runtime material list binder route resolved for all samples: "
        f"`{diagnosis.get('material_runtime_list_route_resolved_for_all_samples')}`.",
        f"- Gameplay_Draw material order resolved for reported samples: "
        f"`{diagnosis.get('gameplay_draw_order_resolved_for_reported_samples')}`.",
        f"- Current gap: `{diagnosis.get('current_gap')}`.",
        "",
        "## Interpretation",
        "",
        diagnosis.get("source_selection_conclusion", ""),
        "",
        "## Next Workorder",
        "",
        diagnosis.get("next_workorder", ""),
        "",
        "## Samples",
        "",
    ]
    for record in report.get("demo_records") or []:
        lines.append(
            "- "
            f"`{record.get('path')}`: frame `{record.get('title_intro_frame')}`, "
            f"dayTime `{record.get('runtime_day_time')}`, "
            f"selected `{record.get('selected_fog_color')}`, "
            f"actor/material `{record.get('actor_vs_material_packet_fog_color')}`."
        )
    missing = report["inputs"].get("missing_demo_stats") or []
    if missing:
        lines.extend(["", "## Missing Inputs", ""])
        for item in missing:
            lines.append(f"- `{item}`")
    lines.append("")
    lines.append("Emulator colors remain validation evidence, not runtime replacement data.")
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--emulator-summary", type=Path, required=True)
    parser.add_argument("--demo-stats", type=Path, action="append", default=None)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--markdown-output", type=Path, default=None)
    args = parser.parse_args()

    demo_paths = args.demo_stats or DEFAULT_DEMO_STATS
    report = summarize(load_json(args.emulator_summary), args.emulator_summary, demo_paths)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding="ascii")

    markdown_output = args.markdown_output or args.output.with_suffix(".md")
    write_markdown(markdown_output, report)

    print(json.dumps({"output": str(args.output), "markdown_output": str(markdown_output)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
