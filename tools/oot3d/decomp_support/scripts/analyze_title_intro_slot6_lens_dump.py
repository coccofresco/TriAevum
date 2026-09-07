#!/usr/bin/env python3
"""Summarize an event-aligned OOT3D title-intro lens dump from Azahar."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path
from typing import Any, Iterable


def read_jsonl(path: Path, allow_truncated_tail: bool = False) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            events.append(json.loads(line))
        except json.JSONDecodeError as exc:
            if allow_truncated_tail and line_number == len(path.read_text(encoding="utf-8").splitlines()):
                break
            raise ValueError(f"invalid JSONL at {path}:{line_number}: {exc}") from exc
    return events


def pica_draws(path: Path) -> list[dict[str, Any]]:
    events = read_jsonl(path, allow_truncated_tail=True)
    if not events or events[-1].get("event") != "capture_end":
        return []
    return [event for event in events if event.get("event") == "draw_begin"]


def texture0(draw: dict[str, Any]) -> dict[str, Any] | None:
    for texture in draw.get("textures", []):
        if texture.get("index") == 0:
            return texture
    return None


def draw_signature(draw: dict[str, Any]) -> dict[str, Any]:
    tex0 = texture0(draw)
    return {
        "draw_index": draw.get("draw_index"),
        "num_vertices": draw.get("num_vertices"),
        "triangle_topology": draw.get("triangle_topology"),
        "texture0": tex0,
        "cmd_list_addr": draw.get("cmd_list_addr"),
        "cmd_list_offset_words": draw.get("cmd_list_offset_words"),
    }


def unique_preserving_order(values: Iterable[Any]) -> list[Any]:
    result: list[Any] = []
    for value in values:
        if value not in result:
            result.append(value)
    return result


def write_markdown(path: Path, report: dict[str, Any]) -> None:
    batch = report.get("first_visible_quad_batch")
    lines = [
        "# OOT3D Title Intro Slot 6 Lens Dump",
        "",
        "This capture is validation evidence. Runtime values remain sourced from native OOT3D assets and code.bin behavior.",
        "",
        "## Synchronization",
        "",
        f"- Native lens batch observed: `{report['native_lens_batch_observed']}`.",
        f"- Producer observation range: `{report['producer_elapsed_ms_range']}` ms.",
        f"- Native lens visibility range: `{report['native_lens_visibility_range']}`.",
        f"- Independent terminal sun draw observed: `{report['terminal_sun_draw_observed']}`.",
        f"- Screenshot: `{report['screenshot_path']}`.",
        f"- Complete PICA frames: `{report['pica_frame_count']}`.",
        "",
        "## Runtime Inputs",
        "",
        f"- Enqueued indices before the visible batch: `{report['enqueue_indices_before_batch']}`.",
        f"- Native s0 values: `{report['native_s0_values_before_batch']}`.",
        f"- Native s1 values: `{report['native_s1_values_before_batch']}`.",
        f"- Unique u32x4 lane words: `{report['u32x4_lane_unique_words']}`.",
        f"- Unique float2 lane values: `{report['float2_lane_unique_values']}`.",
        "",
        "## PICA Candidates",
        "",
        "| Frame | Draw | Vertices | Texture0 |",
        "|---:|---:|---:|---|",
    ]
    if batch is not None:
        lines[9:9] = [
            f"- First visible native quad batch at `{batch.get('elapsed_ms')}` ms.",
            f"- Native queued elements: `{batch.get('queued_count')}`.",
            f"- Runtime flags: `{batch.get('runtime_flags')}`.",
            f"- Descriptor: `{batch.get('descriptor')}`.",
        ]
    for candidate in report["pica_lens_draw_candidates"]:
        texture = candidate.get("texture0") or {}
        tex_label = "{}x{} fmt={} enabled={}".format(
            texture.get("width"), texture.get("height"), texture.get("format"), texture.get("enabled")
        )
        lines.append(
            f"| {candidate['capture_file_index']} | {candidate.get('draw_index')} | "
            f"{candidate.get('num_vertices')} | `{tex_label}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture-dir", type=Path, required=True)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--output-json", type=Path, required=True)
    parser.add_argument("--output-md", type=Path, required=True)
    args = parser.parse_args()

    events = read_jsonl(args.trace)
    event_counts = Counter(str(event.get("event")) for event in events)
    visible_batches = [
        event
        for event in events
        if event.get("event") == "lens_quad_batch_draw"
        and int(str(event.get("queued_count", "0")).replace("0x", ""), 16) > 0
    ]
    batch = visible_batches[0] if visible_batches else None
    batch_serial = int(batch["serial"]) if batch is not None else 1 << 62
    enqueue_events = [
        event
        for event in events
        if event.get("event") == "lens_element_enqueue" and int(event["serial"]) < batch_serial
    ]
    if enqueue_events:
        latest_enqueue_serial = max(int(event["serial"]) for event in enqueue_events)
        enqueue_events = [
            event
            for event in enqueue_events
            if int(event["serial"]) > latest_enqueue_serial - 32
        ]

    lane_words = unique_preserving_order(
        word
        for entry in (batch or {}).get("entries", [])
        for word in entry.get("u32x4_lane_raw", [])
    )
    float2_values = unique_preserving_order(
        value
        for entry in (batch or {}).get("entries", [])
        for value in entry.get("float2_lane_f32", [])
    )
    producer_events = [event for event in events if event.get("event") == "lens_producer_enter"]
    producer_elapsed = [int(event["elapsed_ms"]) for event in producer_events]
    visibility_values = [
        event.get("kankyo_words", {}).get("lens_visibility_68", {}).get("f32")
        for event in producer_events
    ]
    visibility_values = [value for value in visibility_values if value is not None]

    pica_files = [
        path
        for path in sorted(args.capture_dir.glob("oot3d_pica_frame_*.jsonl"))
        if pica_draws(path) or '"event":"capture_end"' in path.read_text(encoding="utf-8", errors="ignore")[-256:]
    ]
    pica_candidates: list[dict[str, Any]] = []
    pica_terminal_candidates: list[dict[str, Any]] = []
    pica_draw_counts: list[int] = []
    for file_index, pica_path in enumerate(pica_files):
        draws = pica_draws(pica_path)
        pica_draw_counts.append(len(draws))
        for draw in draws:
            vertex_count = int(draw.get("num_vertices", 0))
            tex0 = texture0(draw)
            # FUN_002D97E4 always emits elements 0..11 to the primary runtime
            # object and FUN_003FC2F8 expands each element to six vertices.
            # Other 128x128 scene materials are not lens candidates.
            if vertex_count != 12 * 6 or tex0 is None:
                continue
            if (
                not tex0.get("enabled")
                or tex0.get("format") != 12
                or tex0.get("width") != 128
                or tex0.get("height") != 128
            ):
                continue
            candidate = draw_signature(draw)
            candidate["capture_file_index"] = file_index
            candidate["capture_path"] = str(pica_path)
            pica_candidates.append(candidate)
        for draw_offset, draw in enumerate(draws):
            tex0 = texture0(draw)
            preceding_vertex_counts = {
                int(item.get("num_vertices", 0)) for item in draws[:draw_offset]
            }
            if (
                int(draw.get("num_vertices", 0)) == 4
                and tex0 is not None
                and tex0.get("enabled")
                and tex0.get("format") == 12
                and tex0.get("width") == 128
                and tex0.get("height") == 128
                and 336 in preceding_vertex_counts
                and 72 in preceding_vertex_counts
            ):
                candidate = draw_signature(draw)
                candidate["capture_file_index"] = file_index
                candidate["capture_path"] = str(pica_path)
                candidate["classification"] = "independent_kankyo_terminal_sun_after_cloud"
                pica_terminal_candidates.append(candidate)

    trace_begin = next((event for event in events if event.get("event") == "trace_begin"), {})
    trigger_after_ms = int(trace_begin.get("trigger_after_ms", 0))
    terminal_runtime_events = [
        event
        for event in events
        if event.get("event") == "lens_terminal_draw"
        and int(event.get("elapsed_ms", 0)) >= trigger_after_ms
    ]
    terminal_packet_events = [
        event
        for event in events
        if event.get("event") == "primitive_packet_submit"
        and event.get("is_terminal_lens_object") is True
        and int(event.get("elapsed_ms", 0)) >= trigger_after_ms
    ]

    report = {
        "format": "oot3d_title_intro_slot6_native_lens_dump_analysis_v1",
        "policy": "validation_only_not_runtime_replacement_data",
        "capture_dir": str(args.capture_dir),
        "trace_path": str(args.trace),
        "screenshot_path": str(args.capture_dir / "emulator_slot6_native_lens.png"),
        "event_counts": dict(sorted(event_counts.items())),
        "native_lens_batch_observed": batch is not None,
        "producer_elapsed_ms_range": [min(producer_elapsed), max(producer_elapsed)] if producer_elapsed else None,
        "native_lens_visibility_range": [min(visibility_values), max(visibility_values)] if visibility_values else None,
        "terminal_sun_draw_observed": bool(terminal_runtime_events or terminal_packet_events),
        "first_terminal_runtime_draw_after_trigger": terminal_runtime_events[0] if terminal_runtime_events else None,
        "first_terminal_packet_after_trigger": terminal_packet_events[0] if terminal_packet_events else None,
        "first_visible_quad_batch": batch,
        "enqueue_indices_before_batch": [event.get("element_index") for event in enqueue_events],
        "native_s0_values_before_batch": unique_preserving_order(
            event.get("native_s0_f32") for event in enqueue_events
        ),
        "native_s1_values_before_batch": unique_preserving_order(
            event.get("native_s1_f32") for event in enqueue_events
        ),
        "u32x4_lane_unique_words": lane_words,
        "float2_lane_unique_values": float2_values,
        "pica_frame_count": len(pica_files),
        "pica_draw_counts": pica_draw_counts,
        "pica_primary_lens_expected_vertex_count": 72,
        "pica_lens_draw_candidates": pica_candidates,
        "pica_terminal_sun_draw_candidates": pica_terminal_candidates,
    }
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(report, indent=2) + "\n", encoding="ascii")
    write_markdown(args.output_md, report)
    print(json.dumps({
        "status": "pass",
        "native_lens_batch_observed": batch is not None,
        "first_visible_batch_elapsed_ms": batch.get("elapsed_ms") if batch else None,
        "queued_count": batch.get("queued_count") if batch else None,
        "pica_frame_count": len(pica_files),
        "pica_lens_candidate_count": len(pica_candidates),
        "pica_terminal_sun_candidate_count": len(pica_terminal_candidates),
        "output_json": str(args.output_json),
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
