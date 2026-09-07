#!/usr/bin/env python3
"""Analyze Kokiri slot 5 runtime-light writer traces.

This report joins a targeted Azahar writer trace with native 0x0045DD50 field
semantics. It is validation/backtrace evidence only: engine values must still
come from native OOT3D assets and code-derived state, not from this trace.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


OWNER_RE = re.compile(r"owner_base=(0x[0-9A-Fa-f]+)")
PLAY_RE = re.compile(r"play_base=(0x[0-9A-Fa-f]+)")
WATCH_RE = re.compile(r"^(0x[0-9A-Fa-f]+)\s+(\d+)\s*(?:[#;]\s*(.*))?$")


def parse_int(value: Any) -> int:
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value, 0)
    raise ValueError(f"not an integer-like value: {value!r}")


def fmt_u32(value: int) -> str:
    return f"0x{value & 0xFFFFFFFF:08X}"


def fmt_i16(value: int) -> str:
    return str(value - 0x10000 if value & 0x8000 else value)


def bytes_from_write(value: int, size: int) -> list[int]:
    return [(value >> (8 * index)) & 0xFF for index in range(size)]


def clamp_u8(value: int) -> int:
    return min(255, max(0, value))


def read_u8(memory: dict[int, dict[str, Any]], address: int) -> int | None:
    item = memory.get(address)
    return None if item is None else int(item["value"])


def read_u8_triplet(memory: dict[int, dict[str, Any]], address: int) -> list[int] | None:
    values = [read_u8(memory, address + index) for index in range(3)]
    if any(value is None for value in values):
        return None
    return [int(value) for value in values if value is not None]


def read_i16(memory: dict[int, dict[str, Any]], address: int) -> int | None:
    lo = read_u8(memory, address)
    hi = read_u8(memory, address + 1)
    if lo is None or hi is None:
        return None
    value = lo | (hi << 8)
    return value - 0x10000 if value & 0x8000 else value


def read_i16_triplet(memory: dict[int, dict[str, Any]], address: int) -> list[int] | None:
    values = [read_i16(memory, address + index * 2) for index in range(3)]
    if any(value is None for value in values):
        return None
    return [int(value) for value in values if value is not None]


def add_triplet(raw: list[int] | None, addend: list[int] | None) -> list[int] | None:
    if raw is None or addend is None:
        return None
    return [clamp_u8(raw[index] + addend[index]) for index in range(3)]


def infer_addend(raw: list[int] | None, final: list[int] | None) -> list[int] | None:
    if raw is None or final is None:
        return None
    return [final[index] - raw[index] for index in range(3)]


def triplet_json(value: list[int] | None) -> list[int] | None:
    return None if value is None else [int(item) for item in value]


def load_watchlist(path: Path) -> tuple[list[dict[str, Any]], int | None, int | None]:
    watches: list[dict[str, Any]] = []
    owner_base: int | None = None
    play_base: int | None = None
    for line in path.read_text(encoding="utf-8").splitlines():
        match = WATCH_RE.match(line.strip())
        if not match:
            continue
        address = int(match.group(1), 16)
        size = int(match.group(2), 10)
        label = match.group(3) or ""
        watches.append({"address": address, "end": address + size, "size": size, "label": label})
        owner_match = OWNER_RE.search(label)
        play_match = PLAY_RE.search(label)
        if owner_match:
            owner_base = int(owner_match.group(1), 16)
        if play_match:
            play_base = int(play_match.group(1), 16)
    return watches, owner_base, play_base


def find_watch_labels(watches: list[dict[str, Any]], address: int, size: int) -> list[str]:
    end = address + size
    labels: list[str] = []
    for watch in watches:
        if end <= watch["address"] or address >= watch["end"]:
            continue
        labels.append(str(watch.get("label", "")))
    return labels


def load_pica_fog_color(path: Path | None) -> dict[str, Any] | None:
    if path is None or not path.exists():
        return None
    trace = json.loads(path.read_text(encoding="utf-8"))
    draw_summary = trace.get("draw_summary", {})
    color_counts = draw_summary.get("fog_color_counts", {})
    top_color: list[int] | None = None
    top_count = 0
    if isinstance(color_counts, dict):
        for key, count in color_counts.items():
            try:
                rgb = [int(part.strip()) for part in str(key).split(",")]
            except ValueError:
                continue
            if len(rgb) == 3 and int(count) > top_count:
                top_color = rgb
                top_count = int(count)
    if top_color is None:
        fog_state = trace.get("fog_state", {})
        rgb = fog_state.get("color_rgb_u8")
        if isinstance(rgb, dict):
            top_color = [int(rgb.get("r", 0)), int(rgb.get("g", 0)), int(rgb.get("b", 0))]
            top_count = 0
    return {
        "path": str(path),
        "top_fog_color_rgb_u8": top_color,
        "top_fog_color_count": top_count,
        "fog_color_counts": color_counts,
        "fog_enabled_draw_count": draw_summary.get("fog_enabled_draw_count"),
        "fog_decoded_draw_count": draw_summary.get("fog_decoded_draw_count"),
    }


def analyze_trace(
    writer_trace: Path,
    watchlist: Path,
    pica_trace: Path | None,
    owner_base_override: int | None,
) -> dict[str, Any]:
    watches, owner_base, play_base = load_watchlist(watchlist)
    if owner_base_override is not None:
        owner_base = owner_base_override
        play_base = owner_base - 0x3190
    if owner_base is None:
        raise SystemExit("owner_base was not found in the watchlist; pass --owner-base")
    if play_base is None:
        play_base = owner_base - 0x3190

    memory: dict[int, dict[str, Any]] = {}
    rows = 0
    rows_matching_watches = 0
    pc_by_label: dict[str, Counter[str]] = defaultdict(Counter)
    pc_lr_by_label: dict[str, Counter[tuple[str, str]]] = defaultdict(Counter)
    value_by_label: dict[str, Counter[str]] = defaultdict(Counter)

    with writer_trace.open("r", newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            rows += 1
            try:
                address = int(row["address"], 16)
                size = int(row["size"], 0)
                value = int(row["value"], 16)
                serial = int(row["serial"], 0)
            except Exception:
                continue
            labels = find_watch_labels(watches, address, size)
            if not labels:
                continue
            rows_matching_watches += 1
            pc = row.get("pc", "")
            lr = row.get("lr", "")
            bytes_written = bytes_from_write(value, size)
            for index, byte in enumerate(bytes_written):
                memory[address + index] = {
                    "value": byte,
                    "serial": serial,
                    "pc": pc,
                    "lr": lr,
                    "label": row.get("label", ""),
                }
            for label in labels:
                pc_by_label[label][pc] += 1
                pc_lr_by_label[label][(pc, lr)] += 1
                value_by_label[label][f"0x{value:0{max(2, size * 2)}X}"] += 1

    fields = {
        "ambient": {
            "raw_preaddend": read_u8_triplet(memory, owner_base + 0xB2),
            "addend_i16": read_i16_triplet(memory, owner_base + 0x6C),
            "final_output": read_u8_triplet(memory, play_base + 0xA7E),
        },
        "light0": {
            "raw_preaddend": read_u8_triplet(memory, owner_base + 0xB8),
            "addend_i16": read_i16_triplet(memory, owner_base + 0x72),
            "final_compact_payload": read_u8_triplet(memory, owner_base + 0x33),
        },
        "light1": {
            "raw_preaddend": read_u8_triplet(memory, owner_base + 0xBE),
            "addend_i16": read_i16_triplet(memory, owner_base + 0x72),
            "final_compact_payload": read_u8_triplet(memory, owner_base + 0x4B),
        },
        "fog": {
            "raw_preaddend": read_u8_triplet(memory, owner_base + 0xC1),
            "addend_i16": read_i16_triplet(memory, owner_base + 0x78),
            "final_output": read_u8_triplet(memory, play_base + 0xA82),
        },
    }
    for field in fields.values():
        final_key = "final_output" if "final_output" in field else "final_compact_payload"
        field["expected_final_from_raw_plus_addend"] = add_triplet(
            field.get("raw_preaddend"), field.get("addend_i16")
        )
        field["inferred_addend_from_raw_to_final"] = infer_addend(
            field.get("raw_preaddend"), field.get(final_key)
        )
        field["raw_plus_addend_matches_final"] = (
            field.get("expected_final_from_raw_plus_addend") == field.get(final_key)
            if field.get("expected_final_from_raw_plus_addend") is not None and field.get(final_key) is not None
            else None
        )

    pica = load_pica_fog_color(pica_trace)
    if pica and fields["fog"]["final_output"] is not None:
        pica["matches_runtime_final_fog"] = pica.get("top_fog_color_rgb_u8") == fields["fog"]["final_output"]

    return {
        "format": "oot3d_kokiri_slot5_runtime_light_trace_v1",
        "policy": "validation_only_not_runtime_replacement_data",
        "writer_trace": str(writer_trace),
        "watchlist": str(watchlist),
        "pica_trace": pica,
        "owner_base": fmt_u32(owner_base),
        "play_base": fmt_u32(play_base),
        "row_count": rows,
        "rows_matching_watch_ranges": rows_matching_watches,
        "field_values": fields,
        "top_pcs_by_watch_label": {
            label: [{"pc": pc, "count": count} for pc, count in counter.most_common(12)]
            for label, counter in pc_by_label.items()
        },
        "top_pc_lrs_by_watch_label": {
            label: [
                {"pc": pc, "lr": lr, "count": count}
                for (pc, lr), count in counter.most_common(12)
            ]
            for label, counter in pc_lr_by_label.items()
        },
        "top_values_by_watch_label": {
            label: [{"value": value, "count": count} for value, count in counter.most_common(12)]
            for label, counter in value_by_label.items()
        },
    }


def md_triplet(value: list[int] | None) -> str:
    return "--" if value is None else ", ".join(str(item) for item in value)


def write_markdown(path: Path, report: dict[str, Any]) -> None:
    fields = report["field_values"]
    lines = [
        "# OOT3D Kokiri Slot 5 Runtime Light Trace",
        "",
        "This report is validation/backtrace evidence only. It must not be used as runtime replacement data.",
        "",
        "## Inputs",
        "",
        f"- Writer trace: `{report['writer_trace']}`",
        f"- Watchlist: `{report['watchlist']}`",
        f"- Owner base: `{report['owner_base']}`",
        f"- Play base: `{report['play_base']}`",
        f"- Matching rows: `{report['rows_matching_watch_ranges']}` / `{report['row_count']}`",
        "",
        "## Field Reconstruction",
        "",
        "| Field | Raw/pre-addend | Addend i16 | Expected final | Observed final | Match |",
        "|---|---|---|---|---|---|",
    ]
    for name, field in fields.items():
        final_key = "final_output" if "final_output" in field else "final_compact_payload"
        lines.append(
            "| `{}` | `{}` | `{}` | `{}` | `{}` | `{}` |".format(
                name,
                md_triplet(field.get("raw_preaddend")),
                md_triplet(field.get("addend_i16")),
                md_triplet(field.get("expected_final_from_raw_plus_addend")),
                md_triplet(field.get(final_key)),
                field.get("raw_plus_addend_matches_final"),
            )
        )
    pica = report.get("pica_trace")
    if pica:
        lines.extend(
            [
                "",
                "## PICA Fog Cross-Check",
                "",
                f"- PICA trace: `{pica.get('path')}`",
                f"- Top PICA fog RGB: `{md_triplet(pica.get('top_fog_color_rgb_u8'))}`",
                f"- Runtime final fog matches PICA: `{pica.get('matches_runtime_final_fog')}`",
            ]
        )
    lines.extend(["", "## Top Writers", ""])
    for label, entries in report.get("top_pc_lrs_by_watch_label", {}).items():
        lines.append(f"### {label}")
        lines.append("")
        lines.append("| PC | LR | Count |")
        lines.append("|---|---|---:|")
        for entry in entries[:8]:
            lines.append(f"| `{entry['pc']}` | `{entry['lr']}` | {entry['count']} |")
        lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--writer-trace", type=Path, required=True)
    parser.add_argument("--watchlist", type=Path, required=True)
    parser.add_argument("--pica-trace", type=Path, default=None)
    parser.add_argument("--owner-base", type=lambda text: int(text, 0), default=None)
    parser.add_argument("--output-json", type=Path, required=True)
    parser.add_argument("--output-md", type=Path, required=True)
    args = parser.parse_args()

    report = analyze_trace(args.writer_trace, args.watchlist, args.pica_trace, args.owner_base)
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(report, indent=2) + "\n", encoding="ascii")
    write_markdown(args.output_md, report)
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
