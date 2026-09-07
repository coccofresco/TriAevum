#!/usr/bin/env python3
"""Build a watchlist for title-intro material scalar source/object ownership.

This starts from a material-scalar source trace. It extracts the source pointer
passed to FUN_00368704 and the runtime object copied into packet fog fields, then
watches the source gate fields and object scalar fields in a follow-up emulator
run. Captured values are validation/backtrace evidence only.
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path
from typing import Any


DEFAULT_TRACE = Path(
    "captures/azahar_pica/title_intro_slot6_material_scalar_source_trace_20260707_01/"
    "derived/oot3d_pica_writer_trace.csv"
)
DEFAULT_OUTPUT = Path(
    "captures/azahar_pica/derived/watch_addresses.title_intro_material_scalar_object.txt"
)


def parse_hex(value: str | None) -> int | None:
    if value is None or value == "":
        return None
    try:
        return int(value, 16)
    except ValueError:
        return None


def fmt_u32(value: int) -> str:
    return f"0x{value & 0xFFFFFFFF:08X}"


def plausible_ptr(value: int | None) -> bool:
    return value is not None and 0x08000000 <= value < 0x18000000


def discover(trace: Path) -> dict[str, Any]:
    source_packet_counts: Counter[tuple[int, int]] = Counter()
    source_object_counts: Counter[tuple[int, int]] = Counter()
    object_counts: Counter[int] = Counter()

    with trace.open("r", newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            pc = row.get("pc", "")
            lr = row.get("lr", "")
            r0 = parse_hex(row.get("r0"))
            r1 = parse_hex(row.get("r1"))
            r4 = parse_hex(row.get("r4"))
            r5 = parse_hex(row.get("r5"))
            if pc == "0x00368750" and lr in {"0x0036878C", "0x004C0654"}:
                if plausible_ptr(r4) and plausible_ptr(r5):
                    source_packet_counts[(int(r4), int(r5))] += 1
                if plausible_ptr(r4) and plausible_ptr(r1):
                    object_base = int(r1) - 4
                    if plausible_ptr(object_base):
                        source_object_counts[(int(r4), object_base)] += 1
                        object_counts[object_base] += 1
            if pc in {"0x004C05C0", "0x002D6124", "0x0033EA1C", "0x00368750"}:
                if plausible_ptr(r0) and plausible_ptr(r1):
                    object_base = int(r1) - 4
                    if plausible_ptr(object_base):
                        object_counts[object_base] += 1

    sources: Counter[int] = Counter()
    for (source, _packet), count in source_packet_counts.items():
        sources[source] += count
    objects: Counter[int] = Counter(object_counts)
    source_objects: Counter[tuple[int, int]] = Counter(source_object_counts)

    return {
        "source_packet_counts": [
            {"source": fmt_u32(source), "packet": fmt_u32(packet), "count": int(count)}
            for (source, packet), count in source_packet_counts.most_common()
        ],
        "source_object_counts": [
            {"source": fmt_u32(source), "object": fmt_u32(obj), "count": int(count)}
            for (source, obj), count in source_objects.most_common()
        ],
        "sources": [{"source": fmt_u32(source), "count": int(count)} for source, count in sources.most_common()],
        "objects": [{"object": fmt_u32(obj), "count": int(count)} for obj, count in objects.most_common()],
    }


def build_watches(report: dict[str, Any], source_limit: int, object_limit: int) -> list[tuple[int, int, str]]:
    watches: list[tuple[int, int, str]] = []
    for entry in report["sources"][:source_limit]:
        source = int(entry["source"], 16)
        count = int(entry["count"])
        watches.append(
            (
                source,
                0x50,
                "title_intro_material_scalar_source_struct "
                f"source={fmt_u32(source)} count={count} object_ptr=+0x00 enabled=+0x44 dirty=+0x45",
            )
        )
    for entry in report["objects"][:object_limit]:
        obj = int(entry["object"], 16)
        count = int(entry["count"])
        watches.append(
            (
                obj,
                0x90,
                "title_intro_material_scalar_object_header "
                f"object={fmt_u32(obj)} count={count} fog_rgb=+0x04 payload_ptr=+0x24",
            )
        )
        watches.append(
            (
                obj + 0x460,
                0x30,
                "title_intro_material_scalar_object_payload_table_head "
                f"object={fmt_u32(obj)} table=object+0x468",
            )
        )
    return sorted(watches, key=lambda item: (item[0], item[1], item[2]))


def write_watchlist(path: Path, watches: list[tuple[int, int, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# OOT3D title intro material scalar source/object validation watchlist.",
        "# Generated from material-scalar source trace registers; validation only.",
    ]
    for address, size, label in watches:
        lines.append(f"{fmt_u32(address)} {size} # {label}")
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", type=Path, default=DEFAULT_TRACE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--summary-out", type=Path)
    parser.add_argument("--source-limit", type=int, default=4)
    parser.add_argument("--object-limit", type=int, default=4)
    args = parser.parse_args()

    report = discover(args.trace)
    watches = build_watches(report, max(1, args.source_limit), max(1, args.object_limit))
    write_watchlist(args.output, watches)

    summary = {
        "format": "oot3d_title_intro_material_scalar_object_watchlist_v1",
        "policy": "validation_only_not_runtime_replacement_data",
        "trace": str(args.trace),
        "output": str(args.output),
        "watch_count": len(watches),
        **report,
    }
    if args.summary_out is not None:
        args.summary_out.parent.mkdir(parents=True, exist_ok=True)
        args.summary_out.write_text(json.dumps(summary, indent=2) + "\n", encoding="ascii")
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
