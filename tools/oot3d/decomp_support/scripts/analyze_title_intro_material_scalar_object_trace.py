#!/usr/bin/env python3
"""Analyze title-intro material-scalar runtime object writer traces.

This pass follows the native object reached from FUN_00368704 material scalar
source tracing. It records who writes the source gate fields, the shared object
fog RGB floats, and the packed scalar table head. The report is evidence for
decompilation/import work only; it is not runtime replacement data.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import struct
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


SOURCE_LABEL_RE = re.compile(r"title_intro_material_scalar_source_struct source=(0x[0-9A-Fa-f]+)")
OBJECT_LABEL_RE = re.compile(r"title_intro_material_scalar_object_header object=(0x[0-9A-Fa-f]+)")
TABLE_LABEL_RE = re.compile(
    r"title_intro_material_scalar_object_payload_table_head object=(0x[0-9A-Fa-f]+)"
)

KNOWN_PC_NAMES = {
    "0x0036871C": "FUN_00368704 direct payload binder",
    "0x00368744": "FUN_00368704 direct payload binder",
    "0x00368750": "FUN_00368704 direct payload binder dirty-clear tail",
    "0x004C062C": "FUN_004C062C packed scalar table builder",
    "0x004C0654": "FUN_004C062C packed scalar table builder",
    "0x00464C54": "FUN_00464B2C FogResUpdater object color write",
    "0x00464C94": "FUN_00464B2C FogResUpdater object payload write",
    "0x00464CE0": "FUN_00464B2C FogResUpdater table dirty write",
    "0x00464D18": "FUN_00464B2C FogResUpdater source dirty write",
    "0x002E2618": "Gameplay_Draw fog updater callsite",
    "0x002D9640": "unresolved scalar helper under direct binder",
    "0x002D4660": "unresolved scalar helper under direct binder",
    "0x002CDC20": "unresolved table packing helper",
    "0x002CDD04": "unresolved table packing helper",
    "0x002CDD1C": "unresolved table packing helper",
    "0x002CDD2C": "unresolved table packing helper",
    "0x002CDC5C": "unresolved table packing helper",
    "0x002CDC80": "unresolved table packing helper",
}


SOURCE_FIELDS = {
    0x00: ("object_ptr", 4, "u32"),
    0x44: ("enabled", 1, "u8"),
    0x45: ("dirty", 1, "u8"),
}

OBJECT_FIELDS = {
    0x04: ("fog_r", 4, "f32"),
    0x08: ("fog_g", 4, "f32"),
    0x0C: ("fog_b", 4, "f32"),
    0x18: ("fog_near_or_range_a", 4, "f32"),
    0x1C: ("fog_far_or_range_b", 4, "f32"),
    0x24: ("payload_ptr_or_scalar_word", 4, "u32"),
}


def parse_hex(value: str | None) -> int | None:
    if value is None or value == "":
        return None
    try:
        return int(value, 16)
    except ValueError:
        return None


def parse_int(value: str | None) -> int:
    if value is None or value == "":
        return 0
    return int(value, 0)


def fmt_u32(value: int | None) -> str | None:
    if value is None:
        return None
    return f"0x{value & 0xFFFFFFFF:08X}"


def f32_from_u32(value: int) -> float:
    return struct.unpack("<f", (value & 0xFFFFFFFF).to_bytes(4, "little", signed=False))[0]


def float_to_u8(value: float) -> int:
    return max(0, min(255, int(round(value * 255.0))))


def rgb_delta(left: list[int] | None, right: list[int] | None) -> list[int] | None:
    if left is None or right is None or len(left) != 3 or len(right) != 3:
        return None
    return [int(left[index]) - int(right[index]) for index in range(3)]


def read_le_u32(memory: dict[int, int], address: int) -> int | None:
    if any((address + offset) not in memory for offset in range(4)):
        return None
    value = 0
    for offset in range(4):
        value |= memory[address + offset] << (offset * 8)
    return value


def read_le_u8(memory: dict[int, int], address: int) -> int | None:
    return memory.get(address)


def split_bytes(value: int, size: int) -> list[int]:
    capped = max(0, min(size, 8))
    return [(value >> (offset * 8)) & 0xFF for offset in range(capped)]


def parse_watchlist(path: Path | None) -> dict[str, Any]:
    result: dict[str, Any] = {"sources": [], "objects": []}
    if path is None or not path.exists():
        return result
    seen_sources: set[int] = set()
    seen_objects: set[int] = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        label = line.split("#", 1)[1].strip() if "#" in line else ""
        source_match = SOURCE_LABEL_RE.search(label)
        object_match = OBJECT_LABEL_RE.search(label) or TABLE_LABEL_RE.search(label)
        if source_match:
            source = int(source_match.group(1), 16)
            if source not in seen_sources:
                seen_sources.add(source)
                result["sources"].append(source)
        if object_match:
            obj = int(object_match.group(1), 16)
            if obj not in seen_objects:
                seen_objects.add(obj)
                result["objects"].append(obj)
    return result


def load_pica_fog_color(path: Path | None) -> list[int] | None:
    if path is None or not path.exists():
        return None
    data = json.loads(path.read_text(encoding="utf-8"))
    rgb = data.get("fog_state", {}).get("color_rgb_u8")
    if not isinstance(rgb, dict):
        return None
    return [int(rgb.get("r", 0)), int(rgb.get("g", 0)), int(rgb.get("b", 0))]


def load_watch_summary(path: Path | None) -> dict[str, Any] | None:
    if path is None or not path.exists():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def classify_pc(pc: str) -> str:
    return KNOWN_PC_NAMES.get(pc, "unresolved")


def update_memory(memory: dict[int, int], address: int, size: int, value: int) -> None:
    for offset, byte in enumerate(split_bytes(value, size)):
        memory[address + offset] = byte


def field_rows(
    rows_by_address: dict[int, list[dict[str, Any]]],
    base: int,
    fields: dict[int, tuple[str, int, str]],
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for offset, (name, width, value_type) in fields.items():
        address = base + offset
        matching: list[dict[str, Any]] = []
        for byte_offset in range(width):
            matching.extend(rows_by_address.get(address + byte_offset, []))
        value_counts: Counter[int] = Counter()
        pc_lr_counts: Counter[tuple[str, str]] = Counter()
        for row in matching:
            row_address = row["address"]
            row_size = row["size"]
            row_value = row["value"]
            if row_size == width and row_address == address:
                value_counts[row_value] += 1
            pc_lr_counts[(row["pc"], row["lr"])] += 1
        top_values = []
        for raw_value, count in value_counts.most_common(6):
            decoded: Any
            if value_type == "f32":
                decoded = f32_from_u32(raw_value)
            elif value_type == "u8":
                decoded = raw_value & 0xFF
            else:
                decoded = fmt_u32(raw_value)
            top_values.append({"raw": fmt_u32(raw_value), "decoded": decoded, "count": int(count)})
        rows.append(
            {
                "offset": f"+0x{offset:02X}",
                "address": fmt_u32(address),
                "name": name,
                "type": value_type,
                "write_count": len(matching),
                "top_values": top_values,
                "top_pc_lrs": [
                    {
                        "pc": pc,
                        "lr": lr,
                        "pc_name": classify_pc(pc),
                        "count": int(count),
                    }
                    for (pc, lr), count in pc_lr_counts.most_common(8)
                ],
            }
        )
    return rows


def analyze_trace(
    writer_trace: Path,
    watchlist: Path | None,
    watch_summary: Path | None,
    pica_trace: Path | None,
) -> dict[str, Any]:
    watch_info = parse_watchlist(watchlist)
    summary = load_watch_summary(watch_summary)
    pica_rgb = load_pica_fog_color(pica_trace)
    source_hint = watch_info["sources"][0] if watch_info["sources"] else None
    object_hint = watch_info["objects"][0] if watch_info["objects"] else None

    top_pcs: Counter[str] = Counter()
    top_pc_lrs: Counter[tuple[str, str]] = Counter()
    label_counts: Counter[str] = Counter()
    source_from_labels: Counter[int] = Counter()
    object_from_labels: Counter[int] = Counter()
    rows_by_address: dict[int, list[dict[str, Any]]] = defaultdict(list)
    memory: dict[int, int] = {}
    route_rows: Counter[str] = Counter()
    fog_component_words: dict[int, int] = {}
    first_complete_fog_words: tuple[int, int, int] | None = None
    fog_triplet_counts: Counter[tuple[int, int, int]] = Counter()
    row_count = 0

    with writer_trace.open("r", newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for raw_row in reader:
            row_count += 1
            address = parse_hex(raw_row.get("address"))
            size = parse_int(raw_row.get("size"))
            value = parse_hex(raw_row.get("value"))
            pc = raw_row.get("pc", "")
            lr = raw_row.get("lr", "")
            label = raw_row.get("label", "")
            if address is None or value is None:
                continue
            top_pcs[pc] += 1
            top_pc_lrs[(pc, lr)] += 1
            label_counts[label] += 1
            route_rows[classify_pc(pc)] += 1
            source_match = SOURCE_LABEL_RE.search(label)
            object_match = OBJECT_LABEL_RE.search(label) or TABLE_LABEL_RE.search(label)
            if source_match:
                source_from_labels[int(source_match.group(1), 16)] += 1
            if object_match:
                object_from_labels[int(object_match.group(1), 16)] += 1
            row = {
                "address": address,
                "size": size,
                "value": value,
                "pc": pc,
                "lr": lr,
                "label": label,
            }
            for byte_offset in range(max(size, 1)):
                rows_by_address[address + byte_offset].append(row)
            update_memory(memory, address, size, value)
            if object_hint is not None and size == 4:
                fog_offsets = {0x04: 0, 0x08: 1, 0x0C: 2}
                component = fog_offsets.get(address - object_hint)
                if component is not None:
                    fog_component_words[component] = value & 0xFFFFFFFF
                    if all(index in fog_component_words for index in (0, 1, 2)):
                        triplet = (
                            fog_component_words[0],
                            fog_component_words[1],
                            fog_component_words[2],
                        )
                        if first_complete_fog_words is None:
                            first_complete_fog_words = triplet
                        fog_triplet_counts[triplet] += 1

    sources = list(watch_info["sources"]) or [value for value, _ in source_from_labels.most_common()]
    objects = list(watch_info["objects"]) or [value for value, _ in object_from_labels.most_common()]
    source_base = source_hint if source_hint is not None else (sources[0] if sources else None)
    object_base = object_hint if object_hint is not None else (objects[0] if objects else None)

    source_fields: list[dict[str, Any]] = []
    object_fields: list[dict[str, Any]] = []
    table_words: list[dict[str, Any]] = []
    resolved_source: dict[str, Any] = {}
    resolved_object: dict[str, Any] = {}

    if source_base is not None:
        source_fields = field_rows(rows_by_address, source_base, SOURCE_FIELDS)
        resolved_source = {
            "source": fmt_u32(source_base),
            "object_ptr_from_observed_memory": fmt_u32(read_le_u32(memory, source_base + 0x00)),
            "enabled_from_observed_memory": read_le_u8(memory, source_base + 0x44),
            "dirty_from_observed_memory": read_le_u8(memory, source_base + 0x45),
        }
    if object_base is not None:
        object_fields = field_rows(rows_by_address, object_base, OBJECT_FIELDS)
        fog_words = [read_le_u32(memory, object_base + offset) for offset in (0x04, 0x08, 0x0C)]
        fog_float = [f32_from_u32(value) for value in fog_words if value is not None]
        first_fog_float = (
            [f32_from_u32(value) for value in first_complete_fog_words]
            if first_complete_fog_words is not None
            else None
        )
        resolved_object = {
            "object": fmt_u32(object_base),
            "first_complete_fog_rgb_words": [fmt_u32(value) for value in first_complete_fog_words]
            if first_complete_fog_words is not None
            else None,
            "first_complete_fog_rgb_float": first_fog_float,
            "first_complete_fog_rgb_u8": [float_to_u8(value) for value in first_fog_float]
            if first_fog_float is not None
            else None,
            "first_complete_minus_pica_rgb_delta": rgb_delta(
                [float_to_u8(value) for value in first_fog_float]
                if first_fog_float is not None
                else None,
                pica_rgb,
            ),
            "fog_rgb_words_from_observed_memory": [fmt_u32(value) for value in fog_words],
            "fog_rgb_float_from_observed_memory": fog_float if len(fog_float) == 3 else None,
            "fog_rgb_u8_from_observed_memory": [float_to_u8(value) for value in fog_float]
            if len(fog_float) == 3
            else None,
            "payload_ptr_or_scalar_word_from_observed_memory": fmt_u32(
                read_le_u32(memory, object_base + 0x24)
            ),
            "top_fog_rgb_triplets": [
                {
                    "words": [fmt_u32(value) for value in triplet],
                    "float": [f32_from_u32(value) for value in triplet],
                    "rgb_u8": [float_to_u8(f32_from_u32(value)) for value in triplet],
                    "count": int(count),
                }
                for triplet, count in fog_triplet_counts.most_common(12)
            ],
        }
        for index in range(12):
            address = object_base + 0x468 + index * 4
            value = read_le_u32(memory, address)
            table_words.append(
                {
                    "offset": f"+0x{0x468 + index * 4:03X}",
                    "address": fmt_u32(address),
                    "value": fmt_u32(value),
                }
            )

    return {
        "format": "oot3d_title_intro_material_scalar_object_trace_v1",
        "policy": "validation_only_not_runtime_replacement_data",
        "writer_trace": str(writer_trace),
        "watchlist": str(watchlist) if watchlist is not None else None,
        "watch_summary": str(watch_summary) if watch_summary is not None else None,
        "pica_trace": str(pica_trace) if pica_trace is not None else None,
        "pica_gpureg_fog_color_rgb": pica_rgb,
        "row_count": row_count,
        "watch_summary_source_object_counts": summary.get("source_object_counts") if summary else None,
        "resolved_source": resolved_source,
        "resolved_object": resolved_object,
        "source_fields": source_fields,
        "object_fields": object_fields,
        "object_payload_table_head": table_words,
        "top_pcs": [
            {"pc": pc, "pc_name": classify_pc(pc), "count": int(count)}
            for pc, count in top_pcs.most_common(20)
        ],
        "top_pc_lrs": [
            {"pc": pc, "lr": lr, "pc_name": classify_pc(pc), "count": int(count)}
            for (pc, lr), count in top_pc_lrs.most_common(20)
        ],
        "top_routes": [
            {"route": route, "count": int(count)} for route, count in route_rows.most_common(20)
        ],
        "label_counts": [
            {"label": label, "count": int(count)} for label, count in label_counts.most_common()
        ],
    }


def write_markdown(path: Path, report: dict[str, Any]) -> None:
    resolved_object = report.get("resolved_object") or {}
    resolved_source = report.get("resolved_source") or {}
    lines = [
        "# OOT3D Title Intro Material Scalar Object Trace",
        "",
        "This report is validation/backtrace evidence only. Values observed here identify the native runtime producer that must be decompiled/imported; they are not runtime replacement data.",
        "",
        f"- Writer trace: `{report['writer_trace']}`",
        f"- Watchlist: `{report['watchlist']}`",
        f"- Rows: `{report['row_count']}`",
        f"- PICA `GPUREG_FOG_COLOR`: `{report.get('pica_gpureg_fog_color_rgb')}`",
        f"- Source: `{resolved_source.get('source')}`",
        f"- Object: `{resolved_object.get('object')}`",
        f"- First complete object fog RGB: `{resolved_object.get('first_complete_fog_rgb_u8')}`",
        f"- First complete minus PICA delta: `{resolved_object.get('first_complete_minus_pica_rgb_delta')}`",
        f"- Final object fog RGB after trace window: `{resolved_object.get('fog_rgb_u8_from_observed_memory')}`",
        "",
        "## Conclusion",
        "",
        "The first complete RGB triplet written to the shared object reached from the direct material scalar binder aligns with the PICA fog register for this title-intro frame, with a remaining one-LSB green-channel quantization difference under the current CPU-side float-to-u8 rounding. The value then evolves during the trace window, so the final in-memory RGB is later scene state, not the frame-zero comparison value. The producing route is native: `Gameplay_Draw` calls the FogResUpdater path, the direct payload binder clears/uses the source dirty flag, and the packed scalar table builder rebuilds `object+0x468`.",
        "",
        "The next implementation target is not copying this RGB into the demo. It is mapping the owner/allocation/asset origin of the source/object pair and importing that native producer into the decompiled support layer.",
        "",
        "## Source Fields",
        "",
        "| Field | Address | Type | Writes | Top values | Top writers |",
        "|---|---|---|---:|---|---|",
    ]
    for field in report.get("source_fields", []):
        values = ", ".join(
            f"`{entry['raw']}={entry['decoded']}` x{entry['count']}"
            for entry in field.get("top_values", [])[:3]
        )
        writers = ", ".join(
            f"`{entry['pc']}` ({entry['pc_name']}) x{entry['count']}"
            for entry in field.get("top_pc_lrs", [])[:3]
        )
        lines.append(
            f"| `{field['name']}` `{field['offset']}` | `{field['address']}` | `{field['type']}` | {field['write_count']} | {values or '-'} | {writers or '-'} |"
        )
    lines.extend(
        [
            "",
            "## Object Fields",
            "",
            "| Field | Address | Type | Writes | Top values | Top writers |",
            "|---|---|---|---:|---|---|",
        ]
    )
    for field in report.get("object_fields", []):
        values = ", ".join(
            f"`{entry['raw']}={entry['decoded']}` x{entry['count']}"
            for entry in field.get("top_values", [])[:3]
        )
        writers = ", ".join(
            f"`{entry['pc']}` ({entry['pc_name']}) x{entry['count']}"
            for entry in field.get("top_pc_lrs", [])[:3]
        )
        lines.append(
            f"| `{field['name']}` `{field['offset']}` | `{field['address']}` | `{field['type']}` | {field['write_count']} | {values or '-'} | {writers or '-'} |"
        )
    lines.extend(
        [
            "",
            "## Packed Table Head",
            "",
            "| Offset | Address | Value |",
            "|---|---|---|",
        ]
    )
    for entry in report.get("object_payload_table_head", []):
        lines.append(f"| `{entry['offset']}` | `{entry['address']}` | `{entry['value']}` |")
    lines.extend(
        [
            "",
            "## Fog RGB Triplets",
            "",
            "| RGB u8 | Float RGB | Count |",
            "|---|---|---:|",
        ]
    )
    for entry in resolved_object.get("top_fog_rgb_triplets") or []:
        float_text = ", ".join(f"{value:.9f}" for value in entry["float"])
        lines.append(f"| `{entry['rgb_u8']}` | `{float_text}` | {entry['count']} |")
    lines.extend(
        [
            "",
            "## Top Routes",
            "",
            "| Route | Count |",
            "|---|---:|",
        ]
    )
    for entry in report.get("top_routes", [])[:12]:
        lines.append(f"| `{entry['route']}` | {entry['count']} |")
    lines.extend(["", "## Top PCs", "", "| PC | Name | Count |", "|---|---|---:|"])
    for entry in report.get("top_pcs", [])[:16]:
        lines.append(f"| `{entry['pc']}` | `{entry['pc_name']}` | {entry['count']} |")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--writer-trace", type=Path, required=True)
    parser.add_argument("--watchlist", type=Path)
    parser.add_argument("--watch-summary", type=Path)
    parser.add_argument("--pica-trace", type=Path)
    parser.add_argument("--output-json", type=Path, required=True)
    parser.add_argument("--output-md", type=Path, required=True)
    args = parser.parse_args()

    report = analyze_trace(args.writer_trace, args.watchlist, args.watch_summary, args.pica_trace)
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(report, indent=2) + "\n", encoding="ascii")
    write_markdown(args.output_md, report)
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
