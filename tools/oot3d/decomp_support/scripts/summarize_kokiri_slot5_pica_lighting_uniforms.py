#!/usr/bin/env python3
"""Build the reduced Kokiri slot 5 PICA lighting-uniform summary used by compares.

The input is a normalized Azahar native PICA register trace. The output is
validation evidence only; it must not be used as runtime asset data.
"""

from __future__ import annotations

import argparse
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


COLOR_FIELDS = (
    "material_diffuse",
    "material_ambient",
    "ambient_color",
    "diffuse_color",
    "secondary_color",
)

VECTOR_FIELDS = (
    "light_vector",
    "negated_light_vector",
    "world_light_vector_from_normal_matrix",
    "world_negated_light_vector_from_normal_matrix",
)


def color_key(value: Any) -> str | None:
    if isinstance(value, dict):
        try:
            return ",".join(
                str(int(value[channel]))
                for channel in ("r", "g", "b", "a")
            )
        except (KeyError, TypeError, ValueError):
            return None
    if isinstance(value, list) and len(value) >= 4:
        try:
            return ",".join(str(int(round(float(component) * 255.0))) for component in value[:4])
        except (TypeError, ValueError):
            return None
    return None


def vector_key(value: Any) -> str | None:
    if not isinstance(value, list) or len(value) < 3:
        return None
    try:
        return ",".join(f"{float(component):.6f}" for component in value[:3])
    except (TypeError, ValueError):
        return None


def counter_json(counter: Counter[str]) -> dict[str, int]:
    return {key: int(value) for key, value in counter.most_common()}


def summarize(trace: dict[str, Any], source: Path) -> dict[str, Any]:
    draw_trace = (
        trace.get("frame_capture", {})
        .get("cmb_vertex_lighting_uniform_draw_trace", {})
    )
    draws = draw_trace.get("draws", [])
    classes: dict[str, dict[str, Any]] = defaultdict(
        lambda: {
            "count": 0,
            "vertices": 0,
            "colors": defaultdict(Counter),
            "vectors": defaultdict(Counter),
            "fog_modes": Counter(),
            "fog_colors": Counter(),
            "examples": [],
        }
    )
    for draw in draws if isinstance(draws, list) else []:
        if not isinstance(draw, dict):
            continue
        class_name = str(draw.get("classification") or "unclassified")
        item = classes[class_name]
        item["count"] += 1
        item["vertices"] += int(draw.get("vertex_count", 0) or 0)
        for field in COLOR_FIELDS:
            key = color_key(draw.get(f"{field}_u8")) or color_key(draw.get(field))
            if key is not None:
                item["colors"][field][key] += 1
        for field in VECTOR_FIELDS:
            key = vector_key(draw.get(field))
            if key is not None:
                item["vectors"][field][key] += 1
        fog = draw.get("fog")
        if isinstance(fog, dict):
            item["fog_modes"][str(fog.get("mode_name", "Unknown"))] += 1
            color = fog.get("color_rgb_u8")
            if isinstance(color, dict):
                try:
                    color_key_rgb = "{},{},{}".format(
                        int(color.get("r")),
                        int(color.get("g")),
                        int(color.get("b")),
                    )
                    item["fog_colors"][color_key_rgb] += 1
                except (TypeError, ValueError):
                    pass
        if len(item["examples"]) < 6:
            item["examples"].append(
                {
                    "draw_index": draw.get("draw_index"),
                    "vertex_count": draw.get("vertex_count"),
                    "fog": draw.get("fog"),
                }
            )

    compact_classes: dict[str, Any] = {}
    for name, item in sorted(classes.items()):
        compact_classes[name] = {
            "count": int(item["count"]),
            "vertices": int(item["vertices"]),
            "colors": {
                field: counter_json(counter)
                for field, counter in sorted(item["colors"].items())
            },
            "vectors": {
                field: counter_json(counter)
                for field, counter in sorted(item["vectors"].items())
            },
            "fog_modes": counter_json(item["fog_modes"]),
            "fog_colors": counter_json(item["fog_colors"]),
            "examples": item["examples"],
        }
    return {
        "format": "oot3d_kokiri_slot5_pica_lighting_uniform_summary_v1",
        "policy": "validation_only_emulator_trace_not_runtime_asset_source",
        "source": str(source),
        "draw_trace_available": bool(draw_trace.get("available")),
        "draw_count": int(draw_trace.get("draw_count", 0) or 0),
        "vertex_count": int(draw_trace.get("vertex_count", 0) or 0),
        "classification_counts": draw_trace.get("classification_counts", {}),
        "classification_vertex_counts": draw_trace.get("classification_vertex_counts", {}),
        "classes": compact_classes,
    }


def write_markdown(report: dict[str, Any], path: Path) -> None:
    lines = [
        "# Kokiri Slot 5 PICA Lighting Uniform Summary",
        "",
        "Validation evidence only; not runtime asset data.",
        "",
        f"- source: `{report['source']}`",
        f"- draw trace available: `{report['draw_trace_available']}`",
        f"- draw count: `{report['draw_count']}`",
        f"- vertex count: `{report['vertex_count']}`",
        "",
        "## Classes",
        "",
    ]
    for name, item in report["classes"].items():
        lines.append(
            f"- `{name}`: draws `{item['count']}`, vertices `{item['vertices']}`, "
            f"fog modes `{item.get('fog_modes', {})}`, fog colors `{item.get('fog_colors', {})}`"
        )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pica-trace", required=True, type=Path)
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-md", type=Path)
    args = parser.parse_args()

    trace = json.loads(args.pica_trace.read_text(encoding="utf-8"))
    report = summarize(trace, args.pica_trace)
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(report, indent=2), encoding="utf-8", newline="\n")
    if args.output_md is not None:
        args.output_md.parent.mkdir(parents=True, exist_ok=True)
        write_markdown(report, args.output_md)
    print(
        json.dumps(
            {
                "output_json": str(args.output_json),
                "class_count": len(report["classes"]),
                "draw_count": report["draw_count"],
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
