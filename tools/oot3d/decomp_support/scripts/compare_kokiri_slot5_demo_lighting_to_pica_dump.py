#!/usr/bin/env python3
"""Compare Kokiri slot 5 demo lighting diagnostics against an emulator PICA dump.

The emulator side is validation evidence only. Runtime values promoted into the
engine still have to come from OOT3D assets or code.bin-derived structures.
"""

from __future__ import annotations

import argparse
import json
import math
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def rgba(value: Any) -> list[int] | None:
    if not isinstance(value, dict):
        return None
    try:
        return [int(value["r"]), int(value["g"]), int(value["b"]), int(value.get("a", 255))]
    except (KeyError, TypeError, ValueError):
        return None


def vec3(value: Any) -> list[float] | None:
    if isinstance(value, dict):
        keys = ("x", "y", "z")
        if all(key in value for key in keys):
            return [float(value[key]) for key in keys]
    if isinstance(value, list) and len(value) >= 3:
        return [float(value[0]), float(value[1]), float(value[2])]
    return None


def parse_counter_key(key: str) -> list[float]:
    return [float(part.strip()) for part in key.split(",")]


def dominant(counter: dict[str, int] | None) -> tuple[str | None, int]:
    if not isinstance(counter, dict) or not counter:
        return None, 0
    key, count = max(counter.items(), key=lambda item: int(item[1]))
    return str(key), int(count)


def dominant_color(class_info: dict[str, Any], name: str) -> list[int] | None:
    key, _count = dominant(class_info.get("colors", {}).get(name))
    if key is None:
        return None
    values = [int(float(part.strip())) for part in key.split(",")]
    return values if len(values) == 4 else None


def dominant_vector(class_info: dict[str, Any], name: str) -> list[float] | None:
    key, _count = dominant(class_info.get("vectors", {}).get(name))
    if key is None:
        return None
    values = parse_counter_key(key)
    return values[:3] if len(values) >= 3 else None


def color_candidates(class_info: dict[str, Any], name: str) -> list[list[int]]:
    colors = class_info.get("colors", {}).get(name)
    if not isinstance(colors, dict):
        return []
    out: list[list[int]] = []
    for key in colors:
        values = [int(float(part.strip())) for part in str(key).split(",")]
        if len(values) == 4:
            out.append(values)
    return out


def vector_candidates(class_info: dict[str, Any], name: str) -> list[list[float]]:
    vectors = class_info.get("vectors", {}).get(name)
    if not isinstance(vectors, dict):
        return []
    out: list[list[float]] = []
    for key in vectors:
        values = parse_counter_key(str(key))
        if len(values) >= 3:
            out.append(values[:3])
    return out


def dominant_fog_color(pica_trace: dict[str, Any] | None) -> list[int] | None:
    if not isinstance(pica_trace, dict):
        return None
    draw_summary = ((pica_trace.get("frame_capture") or {}).get("draw_summary") or {})
    key, _count = dominant(draw_summary.get("fog_color_counts"))
    if key is not None:
        values = [int(float(part.strip())) for part in key.split(",")]
        if len(values) == 3:
            return [values[0], values[1], values[2], 255]
    register = ((pica_trace.get("registers") or {}).get("GPUREG_FOG_COLOR") or "")
    if isinstance(register, str) and register.startswith("0x"):
        value = int(register, 16)
        return [value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF, 255]
    return None


def parse_u32_hex(value: Any) -> int | None:
    if isinstance(value, int):
        return value & 0xFFFFFFFF
    if isinstance(value, str):
        try:
            return int(value, 0) & 0xFFFFFFFF
        except ValueError:
            return None
    return None


def top_counter(counter: Counter[str], limit: int = 8) -> list[dict[str, Any]]:
    return [
        {
            "value": value,
            "count": count,
        }
        for value, count in counter.most_common(limit)
    ]


def fog_trace_summary(pica_trace: dict[str, Any] | None) -> dict[str, Any]:
    if not isinstance(pica_trace, dict):
        return {
            "available": False,
            "source_kind": "missing_emulator_pica_trace",
        }
    draw_summary = ((pica_trace.get("frame_capture") or {}).get("draw_summary") or {})
    write_value_counts: dict[str, Counter[str]] = defaultdict(Counter)
    write_counts: Counter[str] = Counter()
    for write in pica_trace.get("writes", []) or []:
        if not isinstance(write, dict):
            continue
        name = str(write.get("name", ""))
        if name not in {
            "GPUREG_TEXENV_UPDATE_BUFFER",
            "GPUREG_FOG_COLOR",
            "GPUREG_FOG_LUT_INDEX",
            "GPUREG_FOG_LUT_DATA0",
            "GPUREG_FOG_LUT_DATA1",
            "GPUREG_FOG_LUT_DATA2",
            "GPUREG_FOG_LUT_DATA3",
            "GPUREG_FOG_LUT_DATA4",
            "GPUREG_FOG_LUT_DATA5",
            "GPUREG_FOG_LUT_DATA6",
            "GPUREG_FOG_LUT_DATA7",
        }:
            continue
        write_counts[name] += 1
        value = parse_u32_hex(write.get("value"))
        if value is not None:
            write_value_counts[name][f"0x{value:08X}"] += 1
    return {
        "available": True,
        "source_kind": pica_trace.get("source_kind"),
        "draw_count": draw_summary.get("draw_count", 0),
        "decoded_draw_count": draw_summary.get("fog_decoded_draw_count", 0),
        "enabled_draw_count": draw_summary.get("fog_enabled_draw_count", 0),
        "mode_counts": draw_summary.get("fog_mode_counts", {}),
        "color_counts": draw_summary.get("fog_color_counts", {}),
        "write_counts": dict(write_counts),
        "write_value_counts": {
            name: top_counter(counter)
            for name, counter in sorted(write_value_counts.items())
        },
        "semantics": {
            "pica_formula": (
                "fog_index = depth * 128 or (1-depth) * 128 when fog_flip; "
                "factor = clamp(lut.value + lut.diff * fract(fog_index), 0, 1); "
                "rgb = mix(fog_color, combiner_rgb, factor)"
            ),
            "lut_entry_format": (
                "PICA Fog::LutEntry raw: bits0..12 signed 1.1.11 difference, "
                "bits13..23 unsigned 0.0.11 value"
            ),
            "reference": "Azahar video_core shader/software rasterizer PICA fog implementation",
        },
    }


def fog_writer_summary(fog_writer_report: dict[str, Any] | None) -> dict[str, Any]:
    if not isinstance(fog_writer_report, dict):
        return {
            "available": False,
            "source_kind": "missing_fog_writer_trace_report",
        }
    trace_summary = fog_writer_report.get("trace_summary") or {}
    writer_summary = fog_writer_report.get("writer_summary") or {}
    registers = writer_summary.get("registers") or {}
    compact_registers: dict[str, Any] = {}
    for name in (
        "GPUREG_TEXENV_UPDATE_BUFFER",
        "GPUREG_FOG_COLOR",
        "GPUREG_FOG_LUT_INDEX",
        "GPUREG_FOG_LUT_DATA0",
    ):
        item = registers.get(name) or {}
        compact_registers[name] = {
            "trace_write_count": item.get("trace_write_count", 0),
            "matched_row_count": item.get("matched_row_count", 0),
            "trace_values": item.get("trace_values", [])[:8],
            "value_word_top_pc_lr": item.get("value_word_top_pc_lr", [])[:8],
            "header_word_top_pc_lr": item.get("header_word_top_pc_lr", [])[:8],
        }
    return {
        "available": True,
        "policy": fog_writer_report.get("policy"),
        "fog_write_count": trace_summary.get("fog_write_count", 0),
        "matched_row_count": writer_summary.get("matched_row_count", 0),
        "top_matched_pc_lr": writer_summary.get("top_matched_pc_lr", [])[:12],
        "registers": compact_registers,
    }


def demo_backend_fog_summary(demo: dict[str, Any]) -> dict[str, Any]:
    stats = demo.get("fast3d_adapter_lifetime")
    if not isinstance(stats, dict):
        stats = {}
    fog_keys = sorted(key for key in stats if "fog" in key.lower())
    return {
        "available": bool(fog_keys),
        "stats_keys": fog_keys,
        "shader_supported": bool(stats.get("native_pica_fog_shader_supported", False)),
        "shader_lut_supported": bool(stats.get("native_pica_fog_lut_shader_supported", False)),
        "applied_batch_count": int(stats.get("native_pica_fog_applied_batch_count", 0) or 0),
        "pending_batch_count": int(stats.get("native_pica_fog_pending_batch_count", 0) or 0),
        "decoded_batch_count": int(stats.get("native_pica_fog_decoded_batch_count", 0) or 0),
        "source_kind": stats.get("native_pica_fog_source_kind", ""),
        "blocked_reason": stats.get("native_pica_fog_blocked_reason", ""),
    }


def dot(a: list[float] | None, b: list[float] | None) -> float | None:
    if a is None or b is None:
        return None
    return sum(a[index] * b[index] for index in range(3))


def distance(a: list[float] | list[int] | None, b: list[float] | list[int] | None) -> float | None:
    if a is None or b is None or len(a) != len(b):
        return None
    return math.sqrt(sum((float(a[index]) - float(b[index])) ** 2 for index in range(len(a))))


def clamp_u8(value: float) -> int:
    return max(0, min(255, int(round(value))))


def mapped_primary_color_u8(event: dict[str, Any]) -> list[int] | None:
    mapped = event.get("mapped")
    if not isinstance(mapped, dict):
        return None
    color = mapped.get("color")
    if not isinstance(color, list) or len(color) < 3:
        return None
    try:
        return [
            clamp_u8(float(color[0]) * 255.0),
            clamp_u8(float(color[1]) * 255.0),
            clamp_u8(float(color[2]) * 255.0),
        ]
    except (TypeError, ValueError):
        return None


def empty_rgb_stats() -> dict[str, Any]:
    return {
        "count": 0,
        "min": [255, 255, 255],
        "max": [0, 0, 0],
        "sum": [0, 0, 0],
    }


def add_rgb_sample(stats: dict[str, Any], color: list[int]) -> None:
    stats["count"] += 1
    for index in range(3):
        stats["min"][index] = min(stats["min"][index], color[index])
        stats["max"][index] = max(stats["max"][index], color[index])
        stats["sum"][index] += color[index]


def finalize_rgb_stats(stats: dict[str, Any]) -> dict[str, Any]:
    count = int(stats.get("count", 0) or 0)
    if count <= 0:
        return {
            "count": 0,
            "available": False,
            "min": None,
            "max": None,
            "average": None,
        }
    return {
        "count": count,
        "available": True,
        "min": [int(value) for value in stats["min"]],
        "max": [int(value) for value in stats["max"]],
        "average": [
            float(stats["sum"][index]) / float(count)
            for index in range(3)
        ],
    }


def color_stats_average_rgb_u8(stats: dict[str, Any] | None) -> list[float] | None:
    if not isinstance(stats, dict):
        return None
    average = rgba(stats.get("average"))
    if average is not None:
        return [float(average[0]), float(average[1]), float(average[2])]
    return None


def weighted_demo_color_stats(batches: list[dict[str, Any]], stats_key: str) -> dict[str, Any]:
    aggregate = empty_rgb_stats()
    for batch in batches:
        stats = batch.get(stats_key)
        if not isinstance(stats, dict) or not stats.get("available", True):
            continue
        count = int(stats.get("count", batch.get("vertex_count", 0)) or 0)
        if count <= 0:
            continue
        avg = color_stats_average_rgb_u8(stats)
        min_color = rgba(stats.get("min"))
        max_color = rgba(stats.get("max"))
        if avg is None or min_color is None or max_color is None:
            continue
        aggregate["count"] += count
        for index in range(3):
            aggregate["min"][index] = min(aggregate["min"][index], int(min_color[index]))
            aggregate["max"][index] = max(aggregate["max"][index], int(max_color[index]))
            aggregate["sum"][index] += float(avg[index]) * float(count)
    return finalize_rgb_stats(aggregate)


def draw_trace_for_class(pica_trace: dict[str, Any] | None, class_name: str) -> list[dict[str, Any]]:
    if not isinstance(pica_trace, dict):
        return []
    draw_trace = (
        (pica_trace.get("frame_capture") or {})
        .get("cmb_vertex_lighting_uniform_draw_trace")
        or {}
    )
    draws = draw_trace.get("draws")
    if not isinstance(draws, list):
        return []
    return [
        draw for draw in draws
        if isinstance(draw, dict) and str(draw.get("classification") or "") == class_name
    ]


def pica_primary_output_stats_for_draws(
    pica_trace: dict[str, Any] | None,
    draws: list[dict[str, Any]],
) -> dict[str, Any]:
    if not isinstance(pica_trace, dict):
        return {
            "available": False,
            "source_kind": "missing_pica_trace",
        }
    source_path = pica_trace.get("source_path")
    if not isinstance(source_path, str) or not source_path:
        return {
            "available": False,
            "source_kind": "missing_pica_jsonl_source_path",
        }
    jsonl_path = Path(source_path)
    if not jsonl_path.exists():
        return {
            "available": False,
            "source_kind": "missing_pica_jsonl_source_file",
            "source_path": source_path,
        }
    draw_indices = {
        int(draw.get("draw_index"))
        for draw in draws
        if draw.get("draw_index") is not None
    }
    per_draw: dict[int, dict[str, Any]] = {
        index: empty_rgb_stats()
        for index in draw_indices
    }
    aggregate = empty_rgb_stats()
    with jsonl_path.open("r", encoding="utf-8") as handle:
        for line in handle:
            if not line.strip():
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                continue
            if event.get("event") != "output_vertex":
                continue
            draw_index = event.get("draw_index")
            if draw_index not in draw_indices:
                continue
            color = mapped_primary_color_u8(event)
            if color is None:
                continue
            add_rgb_sample(aggregate, color)
            add_rgb_sample(per_draw[int(draw_index)], color)
    return {
        "available": True,
        "source_path": source_path,
        "draw_count": len(draw_indices),
        "draw_indices": sorted(draw_indices),
        "stats": finalize_rgb_stats(aggregate),
        "per_draw": {
            str(draw_index): finalize_rgb_stats(stats)
            for draw_index, stats in sorted(per_draw.items())
        },
    }


def static_room_primary_compare(
    engine_scene: dict[str, Any],
    pica_trace: dict[str, Any] | None,
) -> dict[str, Any]:
    room = engine_scene.get("room")
    if not isinstance(room, dict):
        return {
            "available": False,
            "source_kind": "missing_demo_room",
        }
    room_batches = [
        batch for batch in room.get("native_batches", []) or []
        if isinstance(batch, dict)
    ]
    static_draws = draw_trace_for_class(
        pica_trace,
        "static_baked_ambient_only_vertex_hemisphere",
    )
    pica_primary = pica_primary_output_stats_for_draws(pica_trace, static_draws)
    demo_vertex_primary = weighted_demo_color_stats(room_batches, "vertex_color_stats")
    demo_shader_primary = weighted_demo_color_stats(room_batches, "estimated_shader_primary_color_stats")
    strict_prefix: list[dict[str, Any]] = []
    pica_per_draw = pica_primary.get("per_draw") if isinstance(pica_primary, dict) else {}
    for batch_index, batch in enumerate(room_batches):
        if batch_index >= len(static_draws):
            break
        draw = static_draws[batch_index]
        draw_index = draw.get("draw_index")
        draw_vertices = int(draw.get("vertex_count", 0) or 0)
        batch_vertices = int(batch.get("vertex_count", 0) or 0)
        if draw_vertices != batch_vertices:
            break
        pica_stats = (pica_per_draw or {}).get(str(draw_index), {})
        demo_stats = batch.get("vertex_color_stats") or {}
        pica_avg = (pica_stats or {}).get("average")
        demo_avg = color_stats_average_rgb_u8(demo_stats)
        strict_prefix.append(
            {
                "batch_index": batch_index,
                "draw_index": draw_index,
                "vertex_count": batch_vertices,
                "pica_average_rgb": pica_avg,
                "demo_vertex_primary_average_rgb": demo_avg,
                "average_rgb_distance": distance(pica_avg, demo_avg),
            }
        )
    strict_distances = [
        float(item["average_rgb_distance"])
        for item in strict_prefix
        if item.get("average_rgb_distance") is not None
    ]
    return {
        "available": bool(static_draws) and bool(room_batches),
        "source_kind": "pica_output_vertex_mapped_color_vs_demo_estimated_shader_primary",
        "static_draw_count": len(static_draws),
        "room_batch_count": len(room_batches),
        "pica_static_primary": pica_primary,
        "demo_room_vertex_primary": demo_vertex_primary,
        "demo_room_estimated_shader_primary": demo_shader_primary,
        "aggregate_average_rgb_distance": distance(
            (pica_primary.get("stats") or {}).get("average")
            if isinstance(pica_primary, dict)
            else None,
            demo_vertex_primary.get("average") if isinstance(demo_vertex_primary, dict) else None,
        ),
        "strict_prefix_match_count": len(strict_prefix),
        "strict_prefix_average_rgb_distance": (
            sum(strict_distances) / len(strict_distances)
            if strict_distances
            else None
        ),
        "strict_prefix": strict_prefix,
    }


def actor_link_primary_compare(
    engine_scene: dict[str, Any],
    pica_trace: dict[str, Any] | None,
) -> dict[str, Any]:
    link = engine_scene.get("link_child")
    if not isinstance(link, dict):
        return {
            "available": False,
            "source_kind": "missing_demo_link_child",
        }
    link_batches = [
        batch for batch in link.get("native_batches", []) or []
        if isinstance(batch, dict)
    ]
    actor_draws = draw_trace_for_class(
        pica_trace,
        "actor_skeleton_directional_vertex_hemisphere",
    )
    if not link_batches or not actor_draws:
        return {
            "available": False,
            "source_kind": "missing_link_batches_or_actor_draws",
            "link_batch_count": len(link_batches),
            "actor_draw_count": len(actor_draws),
        }

    pairs: list[dict[str, Any]] = []
    draw_index = 0
    for batch_index, batch in enumerate(link_batches):
        batch_vertices = int(batch.get("vertex_count", 0) or 0)
        while draw_index < len(actor_draws):
            draw_vertices = int(actor_draws[draw_index].get("vertex_count", 0) or 0)
            if draw_vertices == batch_vertices:
                break
            draw_index += 1
        if draw_index >= len(actor_draws):
            break
        draw = actor_draws[draw_index]
        pairs.append(
            {
                "batch_index": batch_index,
                "draw_index": draw.get("draw_index"),
                "vertex_count": batch_vertices,
                "draw": draw,
            }
        )
        draw_index += 1

    pica_primary = pica_primary_output_stats_for_draws(
        pica_trace,
        [pair["draw"] for pair in pairs if isinstance(pair.get("draw"), dict)],
    )
    pica_per_draw = pica_primary.get("per_draw") if isinstance(pica_primary, dict) else {}
    aligned: list[dict[str, Any]] = []
    distances: list[float] = []
    for pair in pairs:
        batch = link_batches[int(pair["batch_index"])]
        pica_stats = (pica_per_draw or {}).get(str(pair.get("draw_index")), {})
        demo_stats = ((batch.get("native_pica_lighting_diagnostics") or {}).get("output_color_stats") or {})
        pica_avg = (pica_stats or {}).get("average")
        demo_avg = color_stats_average_rgb_u8(demo_stats)
        item_distance = distance(pica_avg, demo_avg)
        if item_distance is not None:
            distances.append(float(item_distance))
        draw = pair.get("draw") if isinstance(pair.get("draw"), dict) else {}
        aligned.append(
            {
                "batch_index": pair.get("batch_index"),
                "draw_index": pair.get("draw_index"),
                "vertex_count": pair.get("vertex_count"),
                "pica_output_primary_average_rgb": pica_avg,
                "demo_output_primary_average_rgb": demo_avg,
                "average_rgb_distance": item_distance,
                "pica_diffuse0_color": rgba(draw.get("diffuse_color_u8")),
                "pica_diffuse1_color": rgba(draw.get("secondary_color_u8")),
                "pica_f80_world_vector": vec3(draw.get("world_negated_light_vector_from_normal_matrix")),
                "pica_f83_world_vector": vec3(draw.get("world_light_vector_from_normal_matrix")),
            }
        )

    first_draw = pairs[0].get("draw") if pairs and isinstance(pairs[0].get("draw"), dict) else {}
    return {
        "available": bool(pairs),
        "source_kind": "link_child_demo_batches_vs_pica_actor_draws_matched_by_vertex_count",
        "link_batch_count": len(link_batches),
        "actor_draw_count": len(actor_draws),
        "matched_pair_count": len(pairs),
        "skipped_actor_draws": [
            {
                "draw_index": draw.get("draw_index"),
                "vertex_count": draw.get("vertex_count"),
            }
            for draw in actor_draws
            if draw.get("draw_index") not in {pair.get("draw_index") for pair in pairs}
        ],
        "pica_link_primary": pica_primary,
        "demo_link_output_primary": weighted_demo_color_stats(
            [
                {
                    "vertex_count": batch.get("vertex_count", 0),
                    "stats": ((batch.get("native_pica_lighting_diagnostics") or {}).get("output_color_stats") or {}),
                }
                for batch in link_batches
            ],
            "stats",
        ),
        "mean_average_rgb_distance": (
            sum(distances) / len(distances)
            if distances
            else None
        ),
        "max_average_rgb_distance": max(distances) if distances else None,
        "first_pica_diffuse0_color": rgba(first_draw.get("diffuse_color_u8")),
        "first_pica_diffuse1_color": rgba(first_draw.get("secondary_color_u8")),
        "first_pica_f80_world_vector": vec3(first_draw.get("world_negated_light_vector_from_normal_matrix")),
        "first_pica_f83_world_vector": vec3(first_draw.get("world_light_vector_from_normal_matrix")),
        "aligned_pairs": aligned,
    }


def color_matches(actual: list[int] | None, expected: list[int] | None, tolerance: int = 0) -> bool:
    if actual is None or expected is None or len(actual) != len(expected):
        return False
    return all(abs(actual[index] - expected[index]) <= tolerance for index in range(len(actual)))


def color_matches_any(actual: list[int] | None, candidates: list[list[int]], tolerance: int = 0) -> bool:
    return any(color_matches(actual, candidate, tolerance) for candidate in candidates)


def vec_matches(actual: list[float] | None, expected: list[float] | None, tolerance: float = 0.035) -> bool:
    dist = distance(actual, expected)
    return dist is not None and dist <= tolerance


def vec_matches_any(actual: list[float] | None, candidates: list[list[float]], tolerance: float = 0.035) -> bool:
    return any(vec_matches(actual, candidate, tolerance) for candidate in candidates)


def iter_models(engine_scene: dict[str, Any]):
    for name in ("room", "link_child"):
        model = engine_scene.get(name)
        if isinstance(model, dict):
            yield name, model
    for collection in ("environment_models", "native_actor_visuals"):
        for index, model in enumerate(engine_scene.get(collection, []) or []):
            if isinstance(model, dict):
                yield f"{collection}[{index}]", model


def batch_material_and_diagnostics(batch: dict[str, Any]) -> tuple[dict[str, Any], dict[str, Any]]:
    material = batch.get("material") or {}
    diagnostics = material.get("native_pica_lighting_diagnostics")
    if not isinstance(diagnostics, dict):
        diagnostics = batch.get("native_pica_lighting_diagnostics")
    if not isinstance(diagnostics, dict):
        diagnostics = {}
    return material, diagnostics


def model_batch_summary(engine_scene: dict[str, Any]) -> dict[str, Any]:
    models: list[dict[str, Any]] = []
    global_applications: Counter[str] = Counter()
    global_output_ranges: Counter[str] = Counter()
    global_vector_sources: Counter[str] = Counter()
    for name, model in iter_models(engine_scene):
        app_counter: Counter[str] = Counter()
        vector_counter: Counter[str] = Counter()
        output_counter: Counter[str] = Counter()
        batch_count = 0
        vertex_count = 0
        for batch in model.get("native_batches", []) or []:
            batch_count += 1
            vertex_count += int(batch.get("vertex_count", 0) or 0)
            material, diagnostics = batch_material_and_diagnostics(batch)
            application = str(
                material.get("native_pica_lighting_application")
                or batch.get("native_pica_lighting_application")
                or ""
            )
            app_counter[application] += 1
            global_applications[application] += 1
            vector_source = str(diagnostics.get("light0_vector_source") or "")
            vector_counter[vector_source] += 1
            global_vector_sources[vector_source] += 1
            output_stats = diagnostics.get("output_color_stats") or {}
            output_min = rgba(output_stats.get("min"))
            output_max = rgba(output_stats.get("max"))
            if output_min is not None and output_max is not None:
                key = "flat" if output_min == output_max else "varied"
                output_counter[key] += 1
                global_output_ranges[key] += 1
        models.append(
            {
                "name": name,
                "batch_count": batch_count,
                "vertex_count": vertex_count,
                "applications": dict(app_counter),
                "light0_vector_sources": dict(vector_counter),
                "output_color_ranges": dict(output_counter),
            }
        )
    return {
        "models": models,
        "applications": dict(global_applications),
        "light0_vector_sources": dict(global_vector_sources),
        "output_color_ranges": dict(global_output_ranges),
    }


def first_batch_diagnostics(engine_scene: dict[str, Any], model_name: str) -> dict[str, Any] | None:
    model = engine_scene.get(model_name)
    if not isinstance(model, dict):
        return None
    for batch in model.get("native_batches", []) or []:
        _material, diagnostics = batch_material_and_diagnostics(batch)
        if isinstance(diagnostics, dict):
            return diagnostics
    return None


def compare(
    demo: dict[str, Any],
    emulator: dict[str, Any],
    emulator_pica_trace: dict[str, Any] | None = None,
    emulator_fog_writer_report: dict[str, Any] | None = None,
) -> dict[str, Any]:
    engine_scene = demo.get("engine_render_scene") or {}
    lighting = engine_scene.get("pica_lighting") or {}
    packet = lighting.get("actor_vs_light_packet") or {}
    background = engine_scene.get("environment_background") or {}
    classes = emulator.get("classes") or {}
    static_class = classes.get("static_baked_ambient_only_vertex_hemisphere") or {}
    actor_class = classes.get("actor_skeleton_directional_vertex_hemisphere") or {}
    other_class = classes.get("other_cmb_vertex_lighting_uniforms") or {}
    room_diag = first_batch_diagnostics(engine_scene, "room") or {}
    link_diag = first_batch_diagnostics(engine_scene, "link_child") or {}
    room_primary_compare = static_room_primary_compare(engine_scene, emulator_pica_trace)
    link_primary_compare = actor_link_primary_compare(engine_scene, emulator_pica_trace)

    pica_fog = fog_trace_summary(emulator_pica_trace)
    writer_fog = fog_writer_summary(emulator_fog_writer_report)
    expected = {
        "static": {
            "draw_count": static_class.get("count"),
            "vertex_count": static_class.get("vertices"),
            "ambient_color": dominant_color(static_class, "ambient_color"),
            "diffuse_color": dominant_color(static_class, "diffuse_color"),
            "secondary_color": dominant_color(static_class, "secondary_color"),
            "material_diffuse": dominant_color(static_class, "material_diffuse"),
            "world_light_vector": dominant_vector(static_class, "world_light_vector_from_normal_matrix"),
        },
        "actor": {
            "draw_count": actor_class.get("count"),
            "vertex_count": actor_class.get("vertices"),
            "ambient_color": dominant_color(actor_class, "ambient_color"),
            "diffuse_color": dominant_color(actor_class, "diffuse_color"),
            "secondary_color": dominant_color(actor_class, "secondary_color"),
            "material_diffuse": dominant_color(actor_class, "material_diffuse"),
            "world_light_vector": dominant_vector(actor_class, "world_light_vector_from_normal_matrix"),
            "world_negated_light_vector": dominant_vector(
                actor_class, "world_negated_light_vector_from_normal_matrix"
            ),
        },
        "other": {
            "draw_count": other_class.get("count"),
            "vertex_count": other_class.get("vertices"),
            "ambient_color": dominant_color(other_class, "ambient_color"),
            "diffuse_color": dominant_color(other_class, "diffuse_color"),
            "secondary_color": dominant_color(other_class, "secondary_color"),
        },
        "fog": {
            "color": dominant_fog_color(emulator_pica_trace),
            "pica_trace": pica_fog,
            "writer_trace": writer_fog,
        },
    }

    actual = {
        "record_selector": lighting.get("record_selector"),
        "record_index": lighting.get("record_index"),
        "floor_light_setting_raw_index": lighting.get("floor_light_setting_raw_index"),
        "floor_light_setting_index": lighting.get("floor_light_setting_index"),
        "resolved_runtime_branch": (lighting.get("resolved_runtime_light_setting") or {}).get("branch"),
        "ambient_color": rgba(lighting.get("ambient_color")),
        "diffuse_color": rgba(lighting.get("diffuse_color")),
        "light1_color": rgba(lighting.get("light1_color")),
        "vertex_hemisphere_ambient_color": rgba(lighting.get("vertex_hemisphere_ambient_color")),
        "vertex_hemisphere_diffuse_color": rgba(lighting.get("vertex_hemisphere_diffuse_color")),
        "vertex_hemisphere_secondary_color": rgba(lighting.get("vertex_hemisphere_secondary_color")),
        "actor_packet": {
            "available": bool(packet.get("available")),
            "color_packet_available": bool(packet.get("color_packet_available")),
            "compact_payload_source_resolved": bool(packet.get("compact_payload_source_resolved")),
            "runtime_light_packet_pack_negates_prepared_vector": bool(
                packet.get("runtime_light_packet_pack_negates_prepared_vector")
            ),
            "vector_origin_resolved": bool(packet.get("vector_origin_resolved")),
            "environment_record_index": packet.get("environment_record_index"),
            "record_index": packet.get("record_index"),
            "record_selection_index_delta": packet.get("record_selection_index_delta"),
            "ambient_color": rgba(packet.get("ambient_color")),
            "diffuse0_color": rgba(packet.get("diffuse0_color")),
            "diffuse1_color": rgba(packet.get("diffuse1_color")),
            "pica_fog_color_available": bool(packet.get("pica_fog_color_available")),
            "pica_fog_color_offset": packet.get("pica_fog_color_offset"),
            "pica_fog_color": rgba(packet.get("pica_fog_color")),
            "compact_payload_slot0_direction": vec3(packet.get("compact_payload_slot0_direction")),
            "compact_payload_slot1_direction": vec3(packet.get("compact_payload_slot1_direction")),
            "compact_payload_slot0_direction_s8": packet.get("compact_payload_slot0_direction_s8"),
            "compact_payload_slot1_direction_s8": packet.get("compact_payload_slot1_direction_s8"),
        },
        "environment_background": {
            "available": bool(background.get("available")),
            "used_for_render": bool(background.get("used_for_render")),
            "source_kind": background.get("source_kind"),
            "clear_color": rgba(background.get("clear_color")),
        },
        "backend_fog": demo_backend_fog_summary(demo),
        "static_room_primary_compare": room_primary_compare,
        "link_actor_primary_compare": link_primary_compare,
        "room_first_batch": {
            "ambient_color": rgba(room_diag.get("ambient_color")),
            "diffuse0_color": rgba(room_diag.get("diffuse0_color")),
            "diffuse1_color": rgba(room_diag.get("diffuse1_color")),
            "material_diffuse": rgba(room_diag.get("effective_material_diffuse_color")),
            "light0_vector": vec3(room_diag.get("light0_vector")),
            "light1_vector": vec3(room_diag.get("light1_vector")),
            "output_min": rgba((room_diag.get("output_color_stats") or {}).get("min")),
            "output_max": rgba((room_diag.get("output_color_stats") or {}).get("max")),
        },
        "link_first_batch": {
            "ambient_color": rgba(link_diag.get("ambient_color")),
            "diffuse0_color": rgba(link_diag.get("diffuse0_color")),
            "diffuse1_color": rgba(link_diag.get("diffuse1_color")),
            "material_diffuse": rgba(link_diag.get("effective_material_diffuse_color")),
            "light0_vector": vec3(link_diag.get("light0_vector")),
            "light1_vector": vec3(link_diag.get("light1_vector")),
            "output_min": rgba((link_diag.get("output_color_stats") or {}).get("min")),
            "output_max": rgba((link_diag.get("output_color_stats") or {}).get("max")),
        },
        "batch_summary": model_batch_summary(engine_scene),
    }

    checks: list[dict[str, Any]] = []

    def add_check(name: str, status: str, detail: str, **extra: Any) -> None:
        checks.append({"name": name, "status": status, "detail": detail, **extra})

    actor_expected = expected["actor"]
    actor_packet = actual["actor_packet"]
    actor_ambient_candidates = color_candidates(actor_class, "ambient_color")
    actor_diffuse_candidates = color_candidates(actor_class, "diffuse_color")
    actor_secondary_candidates = color_candidates(actor_class, "secondary_color")
    actor_world_vector_candidates = vector_candidates(actor_class, "world_light_vector_from_normal_matrix")
    actor_negated_world_vector_candidates = vector_candidates(
        actor_class, "world_negated_light_vector_from_normal_matrix"
    )
    add_check(
        "actor_packet_ambient_color",
        "pass"
        if color_matches_any(actor_packet["ambient_color"], actor_ambient_candidates)
        else "fail",
        "Actor/VS ambient color should match one of the emulator actor VSH ambient uniform packets.",
        expected=actor_expected["ambient_color"],
        expected_candidates=actor_ambient_candidates,
        actual=actor_packet["ambient_color"],
    )
    add_check(
        "actor_packet_diffuse0_color",
        "pass"
        if color_matches_any(actor_packet["diffuse0_color"], actor_diffuse_candidates)
        else "fail",
        "Actor/VS slot0 color should match one of the emulator actor diffuse uniform packets.",
        expected=actor_expected["diffuse_color"],
        expected_candidates=actor_diffuse_candidates,
        actual=actor_packet["diffuse0_color"],
    )
    add_check(
        "actor_packet_diffuse1_color",
        "pass"
        if color_matches_any(actor_packet["diffuse1_color"], actor_secondary_candidates)
        else "fail",
        "Actor/VS slot1 color should match one of the emulator actor secondary uniform packets.",
        expected=actor_expected["secondary_color"],
        expected_candidates=actor_secondary_candidates,
        actual=actor_packet["diffuse1_color"],
    )
    expected_fog = expected["fog"]["color"]
    if expected_fog is not None:
        add_check(
            "actor_packet_pica_fog_color",
            "pass" if color_matches(actor_packet["pica_fog_color"], expected_fog) else "fail",
            "Actor/material packet PICA fog color should match the dominant emulator GPUREG_FOG_COLOR value.",
            expected=expected_fog,
            actual=actor_packet["pica_fog_color"],
            fog_color_counts=(expected["fog"]["pica_trace"] or {}).get("color_counts", {}),
            fog_mode_counts=(expected["fog"]["pica_trace"] or {}).get("mode_counts", {}),
        )
        add_check(
            "environment_clear_uses_pica_fog_color",
            "pass"
            if color_matches(actual["environment_background"]["clear_color"], expected_fog)
            else "fail",
            "The native kankyo background clear color should use the resolved PICA fog color when available.",
            expected=expected_fog,
            actual=actual["environment_background"]["clear_color"],
        )

    actor_slot0 = actor_packet["compact_payload_slot0_direction"]
    actor_expected_world = actor_expected["world_light_vector"]
    actor_expected_negated = actor_expected["world_negated_light_vector"]
    compact_prepack_expected = vec_matches_any(
        actor_slot0,
        actor_world_vector_candidates + actor_negated_world_vector_candidates,
    )
    add_check(
        "actor_packet_slot0_prepack_vector",
        "pass" if compact_prepack_expected else "warning",
        "Compact slot0 should correspond to one of the emulator actor vector packets in the multi-packet frame.",
        expected=actor_expected_world,
        expected_candidates=actor_world_vector_candidates,
        expected_negated=actor_expected_negated,
        expected_negated_candidates=actor_negated_world_vector_candidates,
        actual=actor_slot0,
        dot_expected=dot(actor_slot0, actor_expected_world),
        dot_expected_negated=dot(actor_slot0, actor_expected_negated),
    )
    actor_effective_vector = actual["link_first_batch"]["light0_vector"]
    add_check(
        "actor_effective_light0_vector_orientation",
        "pass" if vec_matches_any(actor_effective_vector, actor_world_vector_candidates) else "fail",
        "The Link/actor batch effective light0 vector should match one of the emulator actor VSH light vectors.",
        expected=actor_expected_world,
        expected_candidates=actor_world_vector_candidates,
        actual=actor_effective_vector,
        dot_expected=dot(actor_effective_vector, actor_expected_world),
    )
    if link_primary_compare.get("available"):
        mean_link_distance = link_primary_compare.get("mean_average_rgb_distance")
        max_link_distance = link_primary_compare.get("max_average_rgb_distance")
        add_check(
            "link_actor_primary_output_matches_pica",
            "pass"
            if mean_link_distance is not None and float(mean_link_distance) <= 8.0
            else "fail",
            "Link batches should match the PICA output primary RGB for the actor draws matched by native vertex counts.",
            mean_average_rgb_distance=mean_link_distance,
            max_average_rgb_distance=max_link_distance,
            matched_pair_count=link_primary_compare.get("matched_pair_count"),
            first_pica_diffuse0_color=link_primary_compare.get("first_pica_diffuse0_color"),
            first_pica_diffuse1_color=link_primary_compare.get("first_pica_diffuse1_color"),
        )
        add_check(
            "link_actor_f80_vector_matches_pica",
            "pass"
            if vec_matches(
                actual["link_first_batch"]["light0_vector"],
                link_primary_compare.get("first_pica_f80_world_vector"),
            )
            else "fail",
            "Link diffuse0/f84 term should use the PICA f80 world vector for the matched Link draw.",
            expected=link_primary_compare.get("first_pica_f80_world_vector"),
            actual=actual["link_first_batch"]["light0_vector"],
            dot_expected=dot(
                actual["link_first_batch"]["light0_vector"],
                link_primary_compare.get("first_pica_f80_world_vector"),
            ),
        )
        add_check(
            "link_actor_f83_vector_matches_pica",
            "pass"
            if vec_matches(
                actual["link_first_batch"]["light1_vector"],
                link_primary_compare.get("first_pica_f83_world_vector"),
            )
            else "fail",
            "Link diffuse1/f81 term should use the PICA f83 world vector for the matched Link draw.",
            expected=link_primary_compare.get("first_pica_f83_world_vector"),
            actual=actual["link_first_batch"]["light1_vector"],
            dot_expected=dot(
                actual["link_first_batch"]["light1_vector"],
                link_primary_compare.get("first_pica_f83_world_vector"),
            ),
        )

    static_expected = expected["static"]
    room_actual = actual["room_first_batch"]
    add_check(
        "static_room_ambient_color",
        "pass" if color_matches(room_actual["ambient_color"], static_expected["ambient_color"]) else "fail",
        "Room static first batch should consume the dominant emulator static ambient color.",
        expected=static_expected["ambient_color"],
        actual=room_actual["ambient_color"],
    )
    add_check(
        "static_room_material_diffuse_zero",
        "pass"
        if color_matches(room_actual["material_diffuse"], static_expected["material_diffuse"])
        else "fail",
        "The dominant emulator static draw has material diffuse zero, so no directional darkening is expected there.",
        expected=static_expected["material_diffuse"],
        actual=room_actual["material_diffuse"],
    )
    static_diffuse_zero = color_matches(room_actual["material_diffuse"], static_expected["material_diffuse"])
    if vec_matches(room_actual["light0_vector"], static_expected["world_light_vector"]):
        static_vec_status = "pass"
        static_vec_detail = "Room static vector matches the emulator static world light vector."
    elif static_diffuse_zero:
        static_vec_status = "warning"
        static_vec_detail = (
            "Room static vector differs, but the dominant emulator static draw has material diffuse zero, "
            "so this vector does not drive visible directional darkening for that class."
        )
    elif dot(room_actual["light0_vector"], static_expected["world_light_vector"]) is not None and (
        dot(room_actual["light0_vector"], static_expected["world_light_vector"]) or 0.0
    ) < -0.90:
        static_vec_status = "fail"
        static_vec_detail = "Room static vector points opposite the emulator static world light vector."
    else:
        static_vec_status = "warning"
        static_vec_detail = "Room static vector differs from the dominant emulator static vector."
    add_check(
        "static_room_vector_orientation",
        static_vec_status,
        static_vec_detail,
        expected=static_expected["world_light_vector"],
        actual=room_actual["light0_vector"],
        dot_expected=dot(room_actual["light0_vector"], static_expected["world_light_vector"]),
    )

    output_ranges = actual["batch_summary"].get("output_color_ranges", {})
    add_check(
        "demo_batch_lighting_range",
        "warning" if int(output_ranges.get("varied", 0) or 0) == 0 else "pass",
        "Counts batches whose lighting output has per-batch color range; static ambient-only draws can still be flat by design.",
        output_color_ranges=output_ranges,
    )
    primary_aggregate_distance = room_primary_compare.get("aggregate_average_rgb_distance")
    primary_prefix_distance = room_primary_compare.get("strict_prefix_average_rgb_distance")
    primary_prefix_count = int(room_primary_compare.get("strict_prefix_match_count", 0) or 0)
    if room_primary_compare.get("available"):
        add_check(
            "static_room_primary_output_matches_pica",
            "pass"
            if primary_aggregate_distance is not None and primary_aggregate_distance <= 8.0
            else "fail",
            (
                "Room ambient-only batches should preserve the native PICA mapped vertex primary RGB; "
                "ambient-only must not collapse to a uniform ambient color."
            ),
            expected=(room_primary_compare.get("pica_static_primary") or {}).get("stats"),
            actual=room_primary_compare.get("demo_room_vertex_primary"),
            average_rgb_distance=primary_aggregate_distance,
        )
        add_check(
            "static_room_primary_strict_prefix",
            "pass"
            if primary_prefix_count > 0 and primary_prefix_distance is not None and primary_prefix_distance <= 8.0
            else "warning",
            (
                "Draw/batch 1:1 validation for the prefix whose vertex counts match exactly; "
                "later multi-stage water draws may be split differently by the emulator."
            ),
            strict_prefix_match_count=primary_prefix_count,
            strict_prefix_average_rgb_distance=primary_prefix_distance,
            strict_prefix=room_primary_compare.get("strict_prefix", [])[:8],
        )
    else:
        add_check(
            "static_room_primary_output_matches_pica",
            "warning",
            "PICA output_vertex primary data was not available for direct room primary validation.",
            actual=room_primary_compare,
        )

    fog_enabled_draw_count = int((expected["fog"]["pica_trace"] or {}).get("enabled_draw_count", 0) or 0)
    if fog_enabled_draw_count > 0:
        backend_fog = actual["backend_fog"]
        add_check(
            "emulator_pica_fog_draw_route",
            "pass",
            "The validation frame has native PICA fog enabled on real draws; visual comparisons must account for this route.",
            draw_count=(expected["fog"]["pica_trace"] or {}).get("draw_count"),
            decoded_draw_count=(expected["fog"]["pica_trace"] or {}).get("decoded_draw_count"),
            enabled_draw_count=fog_enabled_draw_count,
            mode_counts=(expected["fog"]["pica_trace"] or {}).get("mode_counts", {}),
            write_value_counts=(expected["fog"]["pica_trace"] or {}).get("write_value_counts", {}),
        )
        add_check(
            "demo_backend_pica_fog_application",
            "pass" if int(backend_fog.get("applied_batch_count", 0) or 0) > 0 else "fail",
            (
                "The demo backend must apply the native PICA fog term per fragment; clear color alone "
                "does not reproduce the emulator frame."
            ),
            expected_enabled_draw_count=fog_enabled_draw_count,
            actual_backend_fog=backend_fog,
        )
    if (expected["fog"]["writer_trace"] or {}).get("available"):
        fog_color_register = (expected["fog"]["writer_trace"].get("registers") or {}).get("GPUREG_FOG_COLOR") or {}
        fog_lut_register = (expected["fog"]["writer_trace"].get("registers") or {}).get("GPUREG_FOG_LUT_DATA0") or {}
        add_check(
            "emulator_pica_fog_writer_evidence",
            "pass"
            if int(fog_color_register.get("matched_row_count", 0) or 0) > 0
            and int(fog_lut_register.get("matched_row_count", 0) or 0) > 0
            else "warning",
            "The validation-only writer trace correlates fog color and LUT writes back to native code paths.",
            writer_trace=(expected["fog"]["writer_trace"] or {}),
        )

    overall = "pass"
    if any(check["status"] == "fail" for check in checks):
        overall = "fail"
    elif any(check["status"] == "warning" for check in checks):
        overall = "warning"

    return {
        "format": "oot3d_kokiri_slot5_demo_vs_pica_lighting_compare_v2",
        "status": overall,
        "demo_source": str(demo.get("format", "")),
        "emulator_source": emulator.get("source"),
        "expected_from_emulator": expected,
        "actual_from_demo": actual,
        "checks": checks,
    }


def write_markdown(report: dict[str, Any], path: Path) -> None:
    lines: list[str] = []
    lines.append("# Kokiri Slot 5 Demo vs Emulator PICA Lighting Compare")
    lines.append("")
    lines.append(f"- status: `{report['status']}`")
    lines.append(f"- emulator source: `{report.get('emulator_source')}`")
    actual = report["actual_from_demo"]
    lines.append(f"- demo selected record: `{actual.get('record_index')}`")
    lines.append(f"- demo resolved branch: `{actual.get('resolved_runtime_branch')}`")
    packet = actual["actor_packet"]
    lines.append(
        "- actor packet: "
        f"env record `{packet.get('environment_record_index')}`, "
        f"packet record `{packet.get('record_index')}`, "
        f"delta `{packet.get('record_selection_index_delta')}`"
    )
    lines.append(
        f"- PICA fog: packet `{packet.get('pica_fog_color')}`, "
        f"clear `{actual.get('environment_background', {}).get('clear_color')}`"
    )
    backend_fog = actual.get("backend_fog", {})
    lines.append(
        "- backend fog: "
        f"applied batches `{backend_fog.get('applied_batch_count')}`, "
        f"decoded batches `{backend_fog.get('decoded_batch_count')}`, "
        f"shader supported `{backend_fog.get('shader_supported')}`"
    )
    lines.append("")
    lines.append("## Checks")
    lines.append("")
    for check in report["checks"]:
        lines.append(f"- `{check['status']}` `{check['name']}`: {check['detail']}")
        if "expected" in check or "actual" in check:
            lines.append(
                f"  expected `{check.get('expected')}`, actual `{check.get('actual')}`"
            )
        if "expected_negated" in check:
            lines.append(f"  expected_negated `{check.get('expected_negated')}`")
        if "dot_expected" in check:
            lines.append(
                f"  dot_expected `{check.get('dot_expected')}`, "
                f"dot_expected_negated `{check.get('dot_expected_negated')}`"
            )
        if "output_color_ranges" in check:
            lines.append(f"  output_color_ranges `{check.get('output_color_ranges')}`")
        if "average_rgb_distance" in check:
            lines.append(f"  average_rgb_distance `{check.get('average_rgb_distance')}`")
        if "mean_average_rgb_distance" in check:
            lines.append(
                f"  mean_average_rgb_distance `{check.get('mean_average_rgb_distance')}`, "
                f"max_average_rgb_distance `{check.get('max_average_rgb_distance')}`"
            )
        if "strict_prefix_match_count" in check:
            lines.append(
                f"  strict_prefix_match_count `{check.get('strict_prefix_match_count')}`, "
                f"strict_prefix_average_rgb_distance `{check.get('strict_prefix_average_rgb_distance')}`"
            )
        if "fog_color_counts" in check:
            lines.append(f"  fog_color_counts `{check.get('fog_color_counts')}`")
        if "fog_mode_counts" in check:
            lines.append(f"  fog_mode_counts `{check.get('fog_mode_counts')}`")
    primary_compare = actual.get("static_room_primary_compare", {})
    if primary_compare:
        lines.append("")
        lines.append("## Static Room Primary")
        lines.append("")
        lines.append(
            "- aggregate: "
            f"distance `{primary_compare.get('aggregate_average_rgb_distance')}`, "
            f"PICA `{(primary_compare.get('pica_static_primary') or {}).get('stats')}`, "
            f"demo vertex `{primary_compare.get('demo_room_vertex_primary')}`"
        )
        lines.append(
            "- estimated fragment primary: "
            f"`{primary_compare.get('demo_room_estimated_shader_primary')}`"
        )
        lines.append(
            "- strict prefix: "
            f"matches `{primary_compare.get('strict_prefix_match_count')}`, "
            f"average distance `{primary_compare.get('strict_prefix_average_rgb_distance')}`"
        )
    link_compare = actual.get("link_actor_primary_compare", {})
    if link_compare:
        lines.append("")
        lines.append("## Link Actor Primary")
        lines.append("")
        lines.append(
            "- aggregate: "
            f"matched pairs `{link_compare.get('matched_pair_count')}`, "
            f"mean distance `{link_compare.get('mean_average_rgb_distance')}`, "
            f"max distance `{link_compare.get('max_average_rgb_distance')}`"
        )
        lines.append(
            "- first matched PICA packet: "
            f"diffuse0 `{link_compare.get('first_pica_diffuse0_color')}`, "
            f"diffuse1 `{link_compare.get('first_pica_diffuse1_color')}`, "
            f"f80 `{link_compare.get('first_pica_f80_world_vector')}`, "
            f"f83 `{link_compare.get('first_pica_f83_world_vector')}`"
        )
        lines.append(
            "- skipped actor draws: "
            f"`{link_compare.get('skipped_actor_draws')}`"
        )
    lines.append("")
    lines.append("## Batch Summary")
    lines.append("")
    for model in actual["batch_summary"]["models"]:
        lines.append(
            f"- `{model['name']}`: batches `{model['batch_count']}`, "
            f"vertices `{model['vertex_count']}`, applications `{model['applications']}`, "
            f"output ranges `{model['output_color_ranges']}`"
        )
    lines.append("")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--demo-json", required=True, type=Path)
    parser.add_argument("--emulator-uniform-summary", required=True, type=Path)
    parser.add_argument("--emulator-pica-trace-json", type=Path)
    parser.add_argument("--emulator-fog-writer-summary-json", type=Path)
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-md", required=True, type=Path)
    args = parser.parse_args()

    emulator_pica_trace = (
        load_json(args.emulator_pica_trace_json)
        if args.emulator_pica_trace_json is not None
        else None
    )
    emulator_fog_writer_report = (
        load_json(args.emulator_fog_writer_summary_json)
        if args.emulator_fog_writer_summary_json is not None
        else None
    )
    report = compare(
        load_json(args.demo_json),
        load_json(args.emulator_uniform_summary),
        emulator_pica_trace,
        emulator_fog_writer_report,
    )
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    write_markdown(report, args.output_md)
    print(f"status={report['status']}")
    print(f"wrote {args.output_json}")
    print(f"wrote {args.output_md}")
    return 0 if report["status"] != "fail" else 2


if __name__ == "__main__":
    raise SystemExit(main())
