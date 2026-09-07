#!/usr/bin/env python3
"""Correlate OOT3D native light records with an Azahar PICA VS trace.

The script is intentionally diagnostic-only: it reports which bytes from the
native ZSI light settings can explain observed VS uniforms, without making the
engine consume a trace as runtime data.
"""

from __future__ import annotations

import argparse
import itertools
import json
import math
from pathlib import Path
from typing import Any


def _load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def _color_from_object(value: Any) -> list[int] | None:
    if isinstance(value, dict):
        try:
            return [int(value["r"]), int(value["g"]), int(value["b"])]
        except (KeyError, TypeError, ValueError):
            return None
    if isinstance(value, list) and len(value) >= 3:
        try:
            return [int(round(float(value[0]) * 255.0)),
                    int(round(float(value[1]) * 255.0)),
                    int(round(float(value[2]) * 255.0))]
        except (TypeError, ValueError):
            return None
    return None


def _trace_block(trace_json: dict[str, Any]) -> dict[str, Any]:
    return (
        trace_json.get("frame_capture", {})
        .get("vertex_hemisphere_lighting_uniform_trace", {})
    )


def _trace_color(trace: dict[str, Any], key: str) -> list[int] | None:
    direct = trace.get(f"{key}_u8")
    if isinstance(direct, list) and len(direct) >= 3:
        return [int(direct[0]), int(direct[1]), int(direct[2])]
    return _color_from_object(trace.get(key))


def _active_light_records(summary: dict[str, Any]) -> list[dict[str, Any]]:
    lighting = summary.get("scene", {}).get("native_pica_lighting", {})
    active_setup = lighting.get("active_setup_index", 0)
    records = lighting.get("light_settings", [])
    return [r for r in records if r.get("setup_index") == active_setup]


def _rgb(raw: list[int], offset: int) -> list[int] | None:
    if offset < 0 or offset + 3 > len(raw):
        return None
    return [int(raw[offset]), int(raw[offset + 1]), int(raw[offset + 2])]


def _bgr(raw: list[int], offset: int) -> list[int] | None:
    value = _rgb(raw, offset)
    if value is None:
        return None
    return [value[2], value[1], value[0]]


def _exact_color_hits(records: list[dict[str, Any]], target: list[int]) -> list[dict[str, Any]]:
    hits: list[dict[str, Any]] = []
    reverse = list(reversed(target))
    for record in records:
        raw = [int(v) for v in record.get("raw_bytes", [])]
        for offset in range(0, max(0, len(raw) - 2)):
            triplet = raw[offset:offset + 3]
            if triplet == target:
                hits.append({
                    "record_index": record.get("index"),
                    "record_offset": record.get("offset"),
                    "field_offset": offset,
                    "channel_order": "rgb",
                })
            if triplet == reverse:
                hits.append({
                    "record_index": record.get("index"),
                    "record_offset": record.get("offset"),
                    "field_offset": offset,
                    "channel_order": "bgr",
                })
    return hits


def _actor_vs_packet_candidates(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    candidates: list[dict[str, Any]] = []
    previous_by_index: dict[int, dict[str, Any]] = {
        int(r.get("index", -1)): r for r in records
    }
    for record in records:
        raw = [int(v) for v in record.get("raw_bytes", [])]
        index = int(record.get("index", -1))
        previous = previous_by_index.get(index - 1)
        previous_raw = [int(v) for v in previous.get("raw_bytes", [])] if previous else []
        ambient = None
        if len(previous_raw) > 0x1B and raw:
            ambient = [previous_raw[0x1A], previous_raw[0x1B], raw[0x00]]
        candidates.append({
            "record_index": index,
            "record_offset": record.get("offset"),
            "source": (
                "native_zsi_light_settings_actor_vs_packet_candidate:"
                "ambient=previous_record_tail_0x1a_0x1b_plus_current_0x00,"
                "diffuse0=current_0x04_rgb,diffuse1=current_0x0a_rgb"
            ),
            "ambient_color": ambient,
            "diffuse0_color": _rgb(raw, 0x04),
            "diffuse1_color": _rgb(raw, 0x0A),
            "record_raw_hex": record.get("raw_bytes_hex"),
        })
    return candidates


def _signed_byte(value: int) -> int:
    value = int(value) & 0xFF
    return value - 256 if value >= 128 else value


def _normalize(vec: list[float]) -> list[float] | None:
    length = math.sqrt(sum(v * v for v in vec))
    if length <= 1e-9:
        return None
    return [v / length for v in vec]


def _dot(lhs: list[float], rhs: list[float]) -> float:
    return sum(a * b for a, b in zip(lhs, rhs))


def _vec3(value: Any) -> list[float] | None:
    if isinstance(value, list) and len(value) >= 3:
        try:
            return [float(v) for v in value[:3]]
        except (TypeError, ValueError):
            return None
    return None


def _rounded_vec3(value: list[float]) -> list[float]:
    return [round(float(component), 9) for component in value[:3]]


def _stable_world_vectors_from_trace(trace: dict[str, Any], key: str) -> dict[str, Any]:
    draws = trace.get("draws")
    if not isinstance(draws, list):
        return {"available": False, "reason": "trace_draws_missing"}
    vectors = []
    for draw in draws:
        if not isinstance(draw, dict):
            continue
        vec = _vec3(draw.get(key))
        if vec is not None:
            vectors.append(vec)
    unique = []
    for vec in vectors:
        rounded = _rounded_vec3(vec)
        if rounded not in unique:
            unique.append(rounded)
    return {
        "available": bool(vectors),
        "sample_count": len(vectors),
        "unique_rounded_vectors": unique,
        "stable_across_candidate_draws": len(unique) == 1 and bool(vectors),
    }


def _best_vector_byte_triplets(records: list[dict[str, Any]], target: list[float],
                               limit: int) -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []
    for record in records:
        raw = [int(v) for v in record.get("raw_bytes", [])]
        for offset in range(0, max(0, len(raw) - 2)):
            signed = [_signed_byte(v) for v in raw[offset:offset + 3]]
            if signed == [0, 0, 0]:
                continue
            for permutation in itertools.permutations(range(3)):
                for signs in itertools.product([-1.0, 1.0], repeat=3):
                    vec = _normalize([
                        float(signed[permutation[i]]) * signs[i]
                        for i in range(3)
                    ])
                    if vec is None:
                        continue
                    similarity = max(-1.0, min(1.0, _dot(vec, target)))
                    angle = math.degrees(math.acos(similarity))
                    results.append({
                        "record_index": record.get("index"),
                        "record_offset": record.get("offset"),
                        "field_offset": offset,
                        "signed_triplet": signed,
                        "axis_permutation": list(permutation),
                        "axis_signs": [int(s) for s in signs],
                        "normalized_vector": [round(v, 9) for v in vec],
                        "angle_error_degrees": round(angle, 6),
                    })
    results.sort(key=lambda item: item["angle_error_degrees"])
    return results[:limit]


def _color_match_report(candidates: list[dict[str, Any]],
                        trace_colors: dict[str, list[int] | None]) -> list[dict[str, Any]]:
    report: list[dict[str, Any]] = []
    for candidate in candidates:
        matches = {
            "ambient_matches_trace": (
                candidate.get("ambient_color") == trace_colors.get("ambient_color")
            ),
            "diffuse0_matches_trace": (
                candidate.get("diffuse0_color") == trace_colors.get("diffuse_color")
            ),
            "diffuse1_matches_trace": (
                candidate.get("diffuse1_color") == trace_colors.get("secondary_color")
            ),
        }
        report.append({**candidate, **matches})
    return report


def _draw_context_for_offset(trace_json: dict[str, Any], offset_words: int) -> dict[str, Any]:
    draws = trace_json.get("frame_capture", {}).get("draw_events", [])
    if not isinstance(draws, list):
        return {}
    normalized = [
        draw for draw in draws
        if isinstance(draw, dict) and isinstance(draw.get("cmd_list_offset_words"), int)
    ]
    normalized.sort(key=lambda draw: int(draw["cmd_list_offset_words"]))
    previous_draw = None
    next_draw = None
    for draw in normalized:
        draw_offset = int(draw["cmd_list_offset_words"])
        if draw_offset <= offset_words:
            previous_draw = draw
            continue
        next_draw = draw
        break
    return {
        "previous_draw": previous_draw,
        "next_draw": next_draw,
        "probable_target_draw": next_draw,
        "probable_target_draw_basis": (
            "PICA register writes affect subsequent draw commands in command-list order"
            if next_draw is not None else
            "no later draw in captured command list"
        ),
    }


def _uniform_upload_clusters(trace_json: dict[str, Any],
                             trace_colors: dict[str, list[int] | None]) -> list[dict[str, Any]]:
    uploads = trace_json.get("shader_uniform_uploads", {})
    blocks = uploads.get("vertex_hemisphere_blocks", [])
    if not isinstance(blocks, list):
        return []
    normalized_blocks = [
        block for block in blocks
        if isinstance(block, dict) and isinstance(block.get("index_cmd_list_offset_words"), int)
    ]
    normalized_blocks.sort(key=lambda block: int(block["index_cmd_list_offset_words"]))

    clusters: list[list[dict[str, Any]]] = []
    current: list[dict[str, Any]] = []
    last_offset: int | None = None
    for block in normalized_blocks:
        offset = int(block["index_cmd_list_offset_words"])
        if last_offset is None or offset - last_offset <= 10:
            current.append(block)
        else:
            if current:
                clusters.append(current)
            current = [block]
        last_offset = offset
    if current:
        clusters.append(current)

    out: list[dict[str, Any]] = []
    for cluster in clusters:
        by_index = {int(block["uniform_index"]): block for block in cluster if "uniform_index" in block}
        if not {80, 81, 82, 83, 84}.issubset(by_index):
            continue
        values = {}
        for index in [80, 81, 82, 83, 84, 85, 86]:
            block = by_index.get(index)
            if block is None:
                continue
            values[f"f{index}"] = {
                "index_cmd_list_offset_words": block.get("index_cmd_list_offset_words"),
                "raw_words": block.get("raw_words"),
                "xyzw": block.get("xyzw"),
                "rgb_u8_if_color": block.get("rgb_u8_if_color"),
            }
        start_offset = cluster[0].get("index_cmd_list_offset_words")
        cluster_out = {
            "start_cmd_list_offset_words": cluster[0].get("index_cmd_list_offset_words"),
            "end_cmd_list_offset_words": cluster[-1].get("index_cmd_list_offset_words"),
            "uniform_indices": [int(block["uniform_index"]) for block in cluster if "uniform_index" in block],
            "values": values,
            "matches_trace_colors": {
                "f81_secondary": values.get("f81", {}).get("rgb_u8_if_color") == trace_colors.get("secondary_color"),
                "f82_ambient": values.get("f82", {}).get("rgb_u8_if_color") == trace_colors.get("ambient_color"),
                "f84_diffuse": values.get("f84", {}).get("rgb_u8_if_color") == trace_colors.get("diffuse_color"),
            },
        }
        if isinstance(start_offset, int):
            cluster_out["draw_context"] = _draw_context_for_offset(trace_json, start_offset)
        out.append(cluster_out)
    return out


def build_report(summary: dict[str, Any], trace_json: dict[str, Any],
                 vector_limit: int) -> dict[str, Any]:
    trace = _trace_block(trace_json)
    records = _active_light_records(summary)
    trace_colors = {
        "ambient_color": _trace_color(trace, "ambient_color"),
        "diffuse_color": _trace_color(trace, "diffuse_color"),
        "secondary_color": _trace_color(trace, "secondary_color"),
    }
    trace_light_vector = _vec3(trace.get("light_vector"))
    trace_negated_light_vector = _vec3(trace.get("negated_light_vector"))
    trace_world_light_vector = _vec3(trace.get("world_light_vector_from_normal_matrix"))
    trace_world_negated_light_vector = _vec3(
        trace.get("world_negated_light_vector_from_normal_matrix")
    )

    color_hits = {
        key: _exact_color_hits(records, value)
        for key, value in trace_colors.items()
        if value is not None
    }
    packet_candidates = _color_match_report(
        _actor_vs_packet_candidates(records),
        trace_colors,
    )

    vectors: dict[str, Any] = {}
    if trace_light_vector is not None:
        vectors["light_vector"] = {
            "trace": trace_light_vector,
            "best_signed_byte_triplets": _best_vector_byte_triplets(
                records, trace_light_vector, vector_limit
            ),
        }
    if trace_negated_light_vector is not None:
        vectors["negated_light_vector"] = {
            "trace": trace_negated_light_vector,
            "best_signed_byte_triplets": _best_vector_byte_triplets(
                records, trace_negated_light_vector, vector_limit
            ),
        }
    if trace_world_light_vector is not None:
        vectors["world_light_vector_from_normal_matrix"] = {
            "trace": trace_world_light_vector,
            "basis": "VS f76..f78 normal matrix rows multiplied by f83 model-space light vector",
            "stability": _stable_world_vectors_from_trace(
                trace, "world_light_vector_from_normal_matrix"
            ),
            "best_signed_byte_triplets": _best_vector_byte_triplets(
                records, trace_world_light_vector, vector_limit
            ),
        }
    if trace_world_negated_light_vector is not None:
        vectors["world_negated_light_vector_from_normal_matrix"] = {
            "trace": trace_world_negated_light_vector,
            "basis": "VS f76..f78 normal matrix rows multiplied by f80 negated model-space light vector",
            "stability": _stable_world_vectors_from_trace(
                trace, "world_negated_light_vector_from_normal_matrix"
            ),
            "best_signed_byte_triplets": _best_vector_byte_triplets(
                records, trace_world_negated_light_vector, vector_limit
            ),
        }

    full_packet_matches = [
        item for item in packet_candidates
        if item["ambient_matches_trace"] and item["diffuse0_matches_trace"] and
        item["diffuse1_matches_trace"]
    ]
    upload_clusters = _uniform_upload_clusters(trace_json, trace_colors)
    return {
        "source_kind": "oot3d_native_pica_light_origin_probe",
        "purpose": (
            "diagnostic correlation only; Azahar trace remains a validation target, "
            "not runtime replacement asset data"
        ),
        "active_setup_index": (
            summary.get("scene", {})
            .get("native_pica_lighting", {})
            .get("active_setup_index", 0)
        ),
        "active_record_count": len(records),
        "trace_colors": trace_colors,
        "exact_color_hits": color_hits,
        "actor_vs_packet_candidates": packet_candidates,
        "full_actor_vs_packet_color_matches": full_packet_matches,
        "uniform_upload_clusters": upload_clusters,
        "vector_candidates": vectors,
        "native_decode_status": {
            "colors": (
                "native_zsi_actor_vs_packet_candidate_exact_for_link_house_trace_and_pica_upload"
                if full_packet_matches else
                "unresolved"
            ),
            "vectors": (
                "pica_upload_vectors_confirmed_as_model_space_values_with_stable_"
                "normal_matrix_backtransform"
                if trace_world_light_vector is not None else
                "unresolved_transform_or_runtime_upload; best byte triplets are "
                "diagnostic only and must not be used as engine semantics"
            ),
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--summary", required=True, type=Path)
    parser.add_argument("--trace", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--vector-limit", type=int, default=8)
    args = parser.parse_args()

    report = build_report(_load_json(args.summary), _load_json(args.trace), args.vector_limit)
    text = json.dumps(report, indent=2, sort_keys=True)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n", encoding="utf-8")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
