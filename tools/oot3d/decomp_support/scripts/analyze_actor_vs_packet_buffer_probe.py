#!/usr/bin/env python3
"""Correlate OOT3D actor/VS packet-buffer writes with an Azahar PICA frame.

The output is validation evidence only. It identifies native runtime packet
buffers and writer PCs that produce the VS uniforms observed in an emulator
capture; it must not be used as replacement runtime data by the engine.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import struct
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


FIELD_OFFSETS = {
    0x80: "record_pointer_or_owner",
    0x88: "f81_secondary_color_r",
    0x8C: "f81_secondary_color_g",
    0x90: "f81_secondary_color_b",
    0x94: "f81_secondary_color_a",
    0x98: "f82_ambient_color_r",
    0x9C: "f82_ambient_color_g",
    0xA0: "f82_ambient_color_b",
    0xA4: "f82_ambient_color_a",
    0xC8: "slot0_source_vector_x",
    0xCC: "slot0_source_vector_y",
    0xD0: "slot0_source_vector_z",
    0xD4: "slot0_enable_intensity",
    0xD8: "f80_slot0_prepared_vector_x",
    0xDC: "f80_slot0_prepared_vector_y",
    0xE0: "f80_slot0_prepared_vector_z",
    0xE4: "slot0_prepared_intensity",
    0xE8: "f84_diffuse0_color_r",
    0xEC: "f84_diffuse0_color_g",
    0xF0: "f84_diffuse0_color_b",
    0xF4: "f84_diffuse0_color_a",
    0x128: "slot1_source_vector_x",
    0x12C: "slot1_source_vector_y",
    0x130: "slot1_source_vector_z",
    0x134: "slot1_enable_intensity",
    0x138: "f83_slot1_prepared_vector_x",
    0x13C: "f83_slot1_prepared_vector_y",
    0x140: "f83_slot1_prepared_vector_z",
    0x144: "slot1_prepared_intensity",
    0x198: "f86_slot2_prepared_vector_x",
    0x19C: "f86_slot2_prepared_vector_y",
    0x1A0: "f86_slot2_prepared_vector_z",
}


def parse_u32_hex(value: str) -> int:
    return int(value, 16) & 0xFFFFFFFF


def u32_to_f32(value: int) -> float:
    return struct.unpack(">f", struct.pack(">I", value & 0xFFFFFFFF))[0]


def vec_norm(value: tuple[float, float, float]) -> tuple[float, float, float]:
    length = math.sqrt(sum(component * component for component in value))
    if length <= 1e-8:
        return value
    return tuple(component / length for component in value)


def vec_dot(lhs: tuple[float, float, float], rhs: tuple[float, float, float]) -> float:
    return sum(a * b for a, b in zip(lhs, rhs))


def load_target_vectors(pica_trace_path: Path) -> dict[str, Any]:
    with pica_trace_path.open("r", encoding="utf-8") as handle:
        trace = json.load(handle)

    vertex_trace = trace.get("frame_capture", {}).get("vertex_hemisphere_lighting_uniform_trace", {})
    if not vertex_trace.get("available"):
        raise ValueError(f"vertex hemisphere uniform trace is not available in {pica_trace_path}")

    f80 = vertex_trace.get("negated_light_vector")
    f83 = vertex_trace.get("light_vector")
    if not isinstance(f80, list) or not isinstance(f83, list) or len(f80) < 3 or len(f83) < 3:
        raise ValueError(f"missing f80/f83 target vectors in {pica_trace_path}")

    return {
        "source_trace": str(pica_trace_path),
        "selected_draw_index": vertex_trace.get("selected_draw_index", -1),
        "candidate_draw_count": vertex_trace.get("candidate_draw_count", 0),
        "selected_vertex_count": vertex_trace.get("selected_vertex_count", 0),
        "f80_negated_light_vector": [float(f80[0]), float(f80[1]), float(f80[2])],
        "f83_light_vector": [float(f83[0]), float(f83[1]), float(f83[2])],
        "ambient_color_u8": vertex_trace.get("ambient_color_u8", {}),
        "diffuse_color_u8": vertex_trace.get("diffuse_color_u8", {}),
        "secondary_color_u8": vertex_trace.get("secondary_color_u8", {}),
        "world_light_vector_from_normal_matrix": vertex_trace.get("world_light_vector_from_normal_matrix", []),
        "world_negated_light_vector_from_normal_matrix": vertex_trace.get(
            "world_negated_light_vector_from_normal_matrix", []
        ),
    }


def counter_top_values(counter: Counter[int], limit: int = 4) -> list[dict[str, Any]]:
    values = []
    for raw, count in counter.most_common(limit):
        value: dict[str, Any] = {
            "raw": f"0x{raw:08X}",
            "count": count,
        }
        try:
            decoded = u32_to_f32(raw)
            if math.isfinite(decoded):
                value["f32"] = decoded
        except Exception:
            pass
        values.append(value)
    return values


def counter_top_pcs(counter: Counter[int], limit: int = 8) -> list[dict[str, Any]]:
    return [{"pc": f"0x{pc:08X}", "count": count} for pc, count in counter.most_common(limit)]


def most_common_f32(fields: dict[int, dict[str, Any]], offset: int) -> float | None:
    field = fields.get(offset)
    if not field:
        return None
    values: Counter[int] = field["values"]
    if not values:
        return None
    return u32_to_f32(values.most_common(1)[0][0])


def vector_from_offsets(fields: dict[int, dict[str, Any]], offsets: tuple[int, int, int]) -> list[float] | None:
    values = [most_common_f32(fields, offset) for offset in offsets]
    if any(value is None for value in values):
        return None
    return [float(value) for value in values if value is not None]


def color_from_offsets(fields: dict[int, dict[str, Any]], offsets: tuple[int, int, int, int]) -> list[float] | None:
    return vector_from_offsets(fields, offsets[:3])


def analyze_writer_trace(writer_trace_path: Path, target: dict[str, Any]) -> dict[str, Any]:
    base_counts: Counter[int] = Counter()
    base_fields: dict[int, dict[int, dict[str, Any]]] = defaultdict(
        lambda: defaultdict(lambda: {"count": 0, "pcs": Counter(), "values": Counter()})
    )
    row_count = 0

    with writer_trace_path.open("r", newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            row_count += 1
            try:
                base = int(row["watch_address"], 16)
                address = int(row["address"], 16)
                pc = int(row["pc"], 16)
                value = parse_u32_hex(row["value"])
            except Exception:
                continue

            offset = address - base
            base_counts[base] += 1
            if 0 <= offset <= 0x220 and (offset in FIELD_OFFSETS or offset % 4 == 0):
                field = base_fields[base][offset]
                field["count"] += 1
                field["pcs"][pc] += 1
                field["values"][value] += 1

    target_f80 = tuple(float(component) for component in target["f80_negated_light_vector"])
    target_f83 = tuple(float(component) for component in target["f83_light_vector"])
    target_f80_n = vec_norm(target_f80)
    target_f83_n = vec_norm(target_f83)

    candidates = []
    for base, fields in base_fields.items():
        f80 = vector_from_offsets(fields, (0xD8, 0xDC, 0xE0))
        f83 = vector_from_offsets(fields, (0x138, 0x13C, 0x140))
        if f80 is None or f83 is None:
            continue

        f80_tuple = (f80[0], f80[1], f80[2])
        f83_tuple = (f83[0], f83[1], f83[2])
        score = vec_dot(vec_norm(f80_tuple), target_f80_n) + vec_dot(vec_norm(f83_tuple), target_f83_n)
        candidate_fields = {}
        for offset in sorted(fields):
            if offset not in FIELD_OFFSETS:
                continue
            field = fields[offset]
            candidate_fields[f"0x{offset:03X}"] = {
                "name": FIELD_OFFSETS[offset],
                "count": field["count"],
                "writers": counter_top_pcs(field["pcs"]),
                "values": counter_top_values(field["values"]),
            }

        candidates.append(
            {
                "base": f"0x{base:08X}",
                "write_count": base_counts[base],
                "target_match_score": score,
                "matches_target_vectors": score >= 1.999,
                "f80_prepared_vector": f80,
                "f83_prepared_vector": f83,
                "slot0_source_vector": vector_from_offsets(fields, (0xC8, 0xCC, 0xD0)),
                "slot1_source_vector": vector_from_offsets(fields, (0x128, 0x12C, 0x130)),
                "ambient_f82_rgb": color_from_offsets(fields, (0x98, 0x9C, 0xA0, 0xA4)),
                "diffuse0_f84_rgb": color_from_offsets(fields, (0xE8, 0xEC, 0xF0, 0xF4)),
                "secondary_f81_rgb": color_from_offsets(fields, (0x88, 0x8C, 0x90, 0x94)),
                "fields": candidate_fields,
            }
        )

    candidates.sort(key=lambda item: (item["target_match_score"], item["write_count"]), reverse=True)
    return {
        "format": "oot3d_actor_vs_packet_buffer_probe_v1",
        "policy": "validation_only_emulator_trace_not_runtime_asset_source",
        "writer_trace": str(writer_trace_path),
        "writer_trace_row_count": row_count,
        "target_uniforms": target,
        "base_count": len(base_counts),
        "top_bases_by_write_count": [
            {"base": f"0x{base:08X}", "write_count": count}
            for base, count in base_counts.most_common(24)
        ],
        "candidate_count": len(candidates),
        "target_match_count": sum(1 for candidate in candidates if candidate["matches_target_vectors"]),
        "candidates": candidates[:64],
    }


def write_markdown(report: dict[str, Any], path: Path) -> None:
    target = report["target_uniforms"]
    lines = [
        "# OOT3D Actor/VS Packet Buffer Probe",
        "",
        "This report is validation evidence only. It must not be used as runtime replacement data.",
        "",
        "## Inputs",
        "",
        f"- Writer trace: `{report['writer_trace']}`",
        f"- PICA trace: `{target['source_trace']}`",
        f"- Selected vertex/hemisphere draw: `{target['selected_draw_index']}`",
        "",
        "## Emulator Target",
        "",
        f"- f80 negated light vector: `{target['f80_negated_light_vector']}`",
        f"- f83 light vector: `{target['f83_light_vector']}`",
        f"- Ambient f82 color: `{target['ambient_color_u8']}`",
        f"- Diffuse f84 color: `{target['diffuse_color_u8']}`",
        f"- Secondary f81 color: `{target['secondary_color_u8']}`",
        "",
        "## Matched Packet Buffers",
        "",
    ]
    matches = [candidate for candidate in report["candidates"] if candidate["matches_target_vectors"]]
    if not matches:
        lines.append("No packet buffer matched the selected emulator f80/f83 vector pair.")
    else:
        for candidate in matches[:12]:
            lines.extend(
                [
                    f"### {candidate['base']}",
                    "",
                    f"- Match score: `{candidate['target_match_score']:.6f}`",
                    f"- Write count: `{candidate['write_count']}`",
                    f"- Source slot0 vector: `{candidate['slot0_source_vector']}`",
                    f"- Source slot1 vector: `{candidate['slot1_source_vector']}`",
                    f"- Prepared f80 vector: `{candidate['f80_prepared_vector']}`",
                    f"- Prepared f83 vector: `{candidate['f83_prepared_vector']}`",
                    f"- Ambient f82 RGB: `{candidate['ambient_f82_rgb']}`",
                    f"- Diffuse f84 RGB: `{candidate['diffuse0_f84_rgb']}`",
                    f"- Secondary f81 RGB: `{candidate['secondary_f81_rgb']}`",
                    "",
                    "| Offset | Field | Top writers | Top values |",
                    "|---|---|---|---|",
                ]
            )
            for offset, field in candidate["fields"].items():
                if offset not in {
                    "0x088",
                    "0x08C",
                    "0x090",
                    "0x098",
                    "0x09C",
                    "0x0A0",
                    "0x0C8",
                    "0x0CC",
                    "0x0D0",
                    "0x0D4",
                    "0x0D8",
                    "0x0DC",
                    "0x0E0",
                    "0x0E8",
                    "0x0EC",
                    "0x0F0",
                    "0x128",
                    "0x12C",
                    "0x130",
                    "0x134",
                    "0x138",
                    "0x13C",
                    "0x140",
                }:
                    continue
                writers = ", ".join(f"{item['pc']}:{item['count']}" for item in field["writers"][:4])
                values = ", ".join(
                    f"{item['raw']}/{item.get('f32', '')}:{item['count']}" for item in field["values"][:4]
                )
                lines.append(f"| `{offset}` | {field['name']} | `{writers}` | `{values}` |")
            lines.append("")
    lines.extend(
        [
            "## Interpretation",
            "",
            "- A matched buffer proves that native code populated packet fields later uploaded as VS uniforms.",
            "- Source vectors at packet offsets `+0xC8/+0xCC/+0xD0` and `+0x128/+0x12C/+0x130` are the runtime packet inputs before `0x003130A4` prepares f80/f83.",
            "- Writer PCs identify code paths to decompile or bind to already decoded native asset records; the emulator trace remains validation-only.",
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--writer-trace", required=True, type=Path)
    parser.add_argument("--pica-trace", required=True, type=Path)
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-md", required=True, type=Path)
    args = parser.parse_args()

    target = load_target_vectors(args.pica_trace)
    report = analyze_writer_trace(args.writer_trace, target)
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(report, indent=2), encoding="utf-8")
    write_markdown(report, args.output_md)
    print(json.dumps({
        "output_json": str(args.output_json),
        "output_md": str(args.output_md),
        "target_match_count": report["target_match_count"],
        "candidate_count": report["candidate_count"],
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
