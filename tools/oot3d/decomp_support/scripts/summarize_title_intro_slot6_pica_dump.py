#!/usr/bin/env python3
"""Summarize an OOT3D title-intro slot 6 PICA emulator capture."""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


LABEL_PC_RE = re.compile(r"pc=(0x[0-9a-fA-F]+)")
LABEL_LR_RE = re.compile(r"lr=(0x[0-9a-fA-F]+)")


def _top_pairs(counter: Counter[tuple[str, str]], limit: int) -> list[dict[str, Any]]:
    return [{"pc": pc, "lr": lr, "count": count} for (pc, lr), count in counter.most_common(limit)]


def summarize_writer_trace(path: Path, limit: int) -> dict[str, Any]:
    rows = 0
    trace_pc: Counter[str] = Counter()
    trace_lr: Counter[str] = Counter()
    trace_pair: Counter[tuple[str, str]] = Counter()
    label_pc: Counter[str] = Counter()
    label_lr: Counter[str] = Counter()
    label_pair: Counter[tuple[str, str]] = Counter()
    watch_address: Counter[str] = Counter()
    values_by_label_pc: dict[str, Counter[str]] = defaultdict(Counter)

    if not path.exists():
        return {"available": False, "path": str(path)}

    with path.open(newline="", encoding="utf-8", errors="replace") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            rows += 1
            pc = row.get("pc", "").lower()
            lr = row.get("lr", "").lower()
            if pc:
                trace_pc[pc] += 1
            if lr:
                trace_lr[lr] += 1
            if pc and lr:
                trace_pair[(pc, lr)] += 1
            watch = row.get("watch_address", "")
            if watch:
                watch_address[watch] += 1

            label = row.get("label", "")
            pc_match = LABEL_PC_RE.search(label)
            lr_match = LABEL_LR_RE.search(label)
            label_pc_value = pc_match.group(1).lower() if pc_match else ""
            label_lr_value = lr_match.group(1).lower() if lr_match else ""
            if label_pc_value:
                label_pc[label_pc_value] += 1
                values_by_label_pc[label_pc_value][row.get("value", "")] += 1
            if label_lr_value:
                label_lr[label_lr_value] += 1
            if label_pc_value and label_lr_value:
                label_pair[(label_pc_value, label_lr_value)] += 1

    return {
        "available": True,
        "path": str(path),
        "row_count": rows,
        "top_trace_pc": trace_pc.most_common(limit),
        "top_trace_lr": trace_lr.most_common(limit),
        "top_trace_pc_lr_pairs": _top_pairs(trace_pair, limit),
        "top_seed_label_pc": label_pc.most_common(limit),
        "top_seed_label_lr": label_lr.most_common(limit),
        "top_seed_label_pc_lr_pairs": _top_pairs(label_pair, limit),
        "top_watch_addresses": watch_address.most_common(limit),
        "top_values_by_seed_label_pc": {
            pc: values_by_label_pc[pc].most_common(8)
            for pc, _ in label_pc.most_common(min(limit, 12))
        },
    }


def summarize_pica_frame(path: Path, limit: int) -> dict[str, Any]:
    if not path.exists():
        return {"available": False, "path": str(path)}

    with path.open(encoding="utf-8") as handle:
        data = json.load(handle)

    frame_capture = data.get("frame_capture", {})
    draw_summary = frame_capture.get("draw_summary", {})
    draw_events = frame_capture.get("draw_events", [])
    vertex_counts: Counter[int] = Counter()
    fog_colors: Counter[str] = Counter()
    fog_modes: Counter[str] = Counter()
    texture_bindings: Counter[str] = Counter()
    compact_draws: list[dict[str, Any]] = []

    for draw in draw_events:
        vertex_counts[draw.get("num_vertices")] += 1
        fog = draw.get("fog") or {}
        fog_modes[str(fog.get("mode_name", fog.get("mode")))] += 1
        color = fog.get("color_rgb_u8") or fog.get("color_rgb") or fog.get("color")
        if color is not None:
            fog_colors[str(color)] += 1
        textures = []
        for texture in draw.get("textures", []) or []:
            key = (
                texture.get("index"),
                texture.get("enabled"),
                texture.get("format"),
                texture.get("width"),
                texture.get("height"),
                texture.get("address"),
            )
            texture_bindings[str(key)] += 1
            textures.append(
                {
                    "index": texture.get("index"),
                    "enabled": texture.get("enabled"),
                    "format": texture.get("format"),
                    "width": texture.get("width"),
                    "height": texture.get("height"),
                    "address": texture.get("address"),
                }
            )
        compact_draws.append(
            {
                "draw_index": draw.get("draw_index"),
                "cmd_list_addr": draw.get("cmd_list_addr"),
                "cmd_list_offset_words": draw.get("cmd_list_offset_words"),
                "num_vertices": draw.get("num_vertices"),
                "triangle_topology": draw.get("triangle_topology"),
                "fog_mode": fog.get("mode_name", fog.get("mode")),
                "fog_enabled": fog.get("fog_or_gas_enabled", fog.get("fog_enabled")),
                "fog_color_rgb_u8": color,
                "textures": textures,
                "vs_uniforms_f8_f9": draw.get("vs_uniforms_f8_f9"),
                "vs_uniforms_f76_f86": draw.get("vs_uniforms_f76_f86"),
                "shadow_summary": draw.get("shadow_summary"),
            }
        )

    return {
        "available": True,
        "path": str(path),
        "write_count": data.get("write_count"),
        "register_count": data.get("register_count"),
        "draw_summary": draw_summary,
        "top_vertex_counts": vertex_counts.most_common(limit),
        "fog_modes": fog_modes.most_common(limit),
        "fog_colors": fog_colors.most_common(limit),
        "top_texture_bindings": texture_bindings.most_common(limit),
        "draw_events_compact": compact_draws,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("capture_dir", type=Path)
    parser.add_argument("--frame-index", type=int, default=1)
    parser.add_argument("--limit", type=int, default=20)
    parser.add_argument("--output", type=Path, default=None)
    args = parser.parse_args()

    capture_dir = args.capture_dir
    derived_dir = capture_dir / "derived"
    frame_path = derived_dir / f"oot3d_pica_frame_{args.frame_index:06d}.native_pica_register_trace.json"
    writer_path = derived_dir / "oot3d_pica_writer_trace.csv"
    summary_path = capture_dir / "emulator_dump_summary.json"

    output = {
        "format": "oot3d_title_intro_slot6_pica_dump_summary_v1",
        "capture_dir": str(capture_dir),
        "emulator_summary": json.loads(summary_path.read_text(encoding="utf-8"))
        if summary_path.exists()
        else None,
        "frame": summarize_pica_frame(frame_path, args.limit),
        "writer_trace": summarize_writer_trace(writer_path, args.limit),
    }

    out_path = args.output or derived_dir / "title_intro_slot6_pica_dump_summary.json"
    out_path.write_text(json.dumps(output, indent=2), encoding="ascii")
    print(json.dumps({"output": str(out_path), "capture_dir": str(capture_dir)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
