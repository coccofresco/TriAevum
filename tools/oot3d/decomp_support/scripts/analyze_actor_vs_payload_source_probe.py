#!/usr/bin/env python3
"""Analyze OOT3D actor/VS compact light payload source probes.

This consumes an Azahar writer trace seeded around the compact runtime
payloads passed to the native light-record handler. The report is
validation/backtrace evidence only: the engine must reconstruct these values
from native OOT3D assets and code-derived transforms, not copy trace values.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


REGISTER_NAMES = ["sp", "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12"]
COMPACT_PAYLOAD_RE = re.compile(r"payload_slot(?P<slot>[0-9]+)_dir_rgb", re.IGNORECASE)


def parse_int(value: str) -> int:
    return int(value, 0)


def parse_hex(value: str) -> int:
    return int(value, 16) & 0xFFFFFFFFFFFFFFFF


def bytes_from_write(value: int, size: int) -> list[int]:
    return [(value >> (8 * index)) & 0xFF for index in range(size)]


def u8_to_s8(value: int) -> int:
    value &= 0xFF
    return value - 0x100 if value & 0x80 else value


def scaled_s8(value: int) -> float:
    return u8_to_s8(value) / 127.0


def format_function(function: dict[str, Any]) -> dict[str, Any]:
    return {
        "name": function["name"],
        "entry": f"0x{function['entry']:08X}",
        "body_min": f"0x{function['body_min']:08X}",
        "body_max": f"0x{function['body_max']:08X}",
        "signature": function.get("signature", ""),
    }


def load_functions(paths: list[Path]) -> list[dict[str, Any]]:
    by_entry: dict[int, dict[str, Any]] = {}
    for path in paths:
        if not path.exists():
            continue
        with path.open("r", newline="", encoding="utf-8") as handle:
            reader = csv.DictReader(handle)
            for row in reader:
                try:
                    entry = int(row["entry"], 16)
                    body_min = int(row["body_min"], 16)
                    body_max = int(row["body_max"], 16)
                except Exception:
                    continue
                current = by_entry.get(entry)
                candidate = {
                    "name": row.get("name", ""),
                    "entry": entry,
                    "body_min": body_min,
                    "body_max": body_max,
                    "signature": row.get("signature", ""),
                }
                if current is None or (body_max - body_min) < (current["body_max"] - current["body_min"]):
                    by_entry[entry] = candidate
    functions = list(by_entry.values())
    functions.sort(key=lambda item: (item["body_min"], item["body_max"], item["entry"]))
    return functions


def map_pc(functions: list[dict[str, Any]], pc: int) -> dict[str, Any]:
    exact = [function for function in functions if function["entry"] == pc]
    containing = [function for function in functions if function["body_min"] <= pc <= function["body_max"]]
    containing.sort(key=lambda item: (item["body_max"] - item["body_min"], item["entry"]))
    prev_functions = [function for function in functions if function["entry"] <= pc]
    next_functions = [function for function in functions if function["entry"] > pc]
    previous = prev_functions[-1] if prev_functions else None
    next_function = next_functions[0] if next_functions else None
    return {
        "pc": f"0x{pc:08X}",
        "exact_function": format_function(exact[0]) if exact else None,
        "smallest_containing_function": format_function(containing[0]) if containing else None,
        "previous_function": format_function(previous) if previous else None,
        "next_function": format_function(next_function) if next_function else None,
        "mapping_note": (
            "unmapped_or_thumb_interwork_candidate"
            if not exact and (not containing or containing[0]["body_max"] - containing[0]["body_min"] > 0x10000)
            else "mapped"
        ),
    }


def mapping_label(item: dict[str, Any]) -> str:
    exact = item.get("exact_function")
    containing = item.get("smallest_containing_function")
    if exact:
        return f"`{exact['name']} @ {exact['entry']}`"
    if containing:
        return f"`{containing['name']} @ {containing['entry']}`"
    previous = item.get("previous_function")
    next_function = item.get("next_function")
    prev_label = f"prev `{previous['name']} @ {previous['entry']}`" if previous else "prev none"
    next_label = f"next `{next_function['name']} @ {next_function['entry']}`" if next_function else "next none"
    return f"{prev_label}; {next_label}"


def counter_top_hex(counter: Counter[int], key_name: str = "value", limit: int = 12) -> list[dict[str, Any]]:
    return [{key_name: f"0x{value:08X}", "count": count} for value, count in counter.most_common(limit)]


def counter_top_pcs(functions: list[dict[str, Any]], counter: Counter[int], limit: int = 12) -> list[dict[str, Any]]:
    return [map_pc(functions, value) | {"count": count} for value, count in counter.most_common(limit)]


def row_reg(row: dict[str, str], name: str) -> int | None:
    value = row.get(name)
    if not value:
        return None
    try:
        return int(value, 16)
    except Exception:
        return None


def write_key(row: dict[str, str]) -> tuple[Any, ...]:
    return tuple(
        [
            row.get("label", ""),
            row.get("source", ""),
            row.get("title_id", ""),
            row["address"],
            row["size"],
            row["value"],
            row["pc"],
            row["lr"],
        ]
        + [row.get(name, "") for name in REGISTER_NAMES]
    )


def decode_compact_payload(memory: dict[int, int], base: int) -> dict[str, Any] | None:
    raw = [memory.get(base + index) for index in range(6)]
    if any(value is None for value in raw):
        return None
    raw_bytes = [int(value) for value in raw if value is not None]
    direction_s8 = [u8_to_s8(value) for value in raw_bytes[:3]]
    direction_scaled = [scaled_s8(value) for value in raw_bytes[:3]]
    direction_length = math.sqrt(sum(component * component for component in direction_scaled))
    return {
        "base": f"0x{base:08X}",
        "bytes_hex": " ".join(f"{value:02X}" for value in raw_bytes),
        "direction_s8": direction_s8,
        "direction_scaled_by_1_over_127": direction_scaled,
        "direction_scaled_length": direction_length,
        "color_rgb_u8": raw_bytes[3:6],
    }


def byte_writer_summary(
    memory: dict[int, int],
    byte_pcs: dict[int, Counter[int]],
    byte_lrs: dict[int, Counter[int]],
    functions: list[dict[str, Any]],
    base: int,
    count: int,
) -> list[dict[str, Any]]:
    rows = []
    for offset in range(count):
        address = base + offset
        value = memory.get(address)
        rows.append(
            {
                "offset": f"0x{offset:02X}",
                "address": f"0x{address:08X}",
                "value": f"0x{value:02X}" if value is not None else None,
                "top_writer_pcs": counter_top_pcs(functions, byte_pcs.get(address, Counter()), 6),
                "top_link_registers": counter_top_pcs(functions, byte_lrs.get(address, Counter()), 4),
            }
        )
    return rows


def analyze(writer_trace_path: Path, functions_csv: list[Path]) -> dict[str, Any]:
    functions = load_functions(functions_csv)

    raw_rows = 0
    unique_rows = 0
    previous_key: tuple[Any, ...] | None = None
    labels: dict[str, dict[str, Any]] = {}
    all_pc_counts: Counter[int] = Counter()
    all_lr_counts: Counter[int] = Counter()
    seed_label_counts: Counter[str] = Counter()

    with writer_trace_path.open("r", newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            raw_rows += 1
            try:
                label = row.get("label", "") or "unlabeled"
                address = int(row["address"], 16)
                size = parse_int(row["size"])
                value = parse_hex(row["value"])
                watch_address = int(row["watch_address"], 16)
                watch_size = parse_int(row["watch_size"])
                pc = int(row["pc"], 16)
                lr = int(row["lr"], 16)
            except Exception:
                continue

            if "actor_vs_source_payload" not in label:
                continue

            key = write_key(row)
            if key == previous_key:
                continue
            previous_key = key
            unique_rows += 1
            all_pc_counts[pc] += 1
            all_lr_counts[lr] += 1
            seed_label_counts[label] += 1

            group = labels.setdefault(
                label,
                {
                    "label": label,
                    "watch_addresses": Counter(),
                    "watch_sizes": Counter(),
                    "pc_counts": Counter(),
                    "lr_counts": Counter(),
                    "register_counts": {name: Counter() for name in REGISTER_NAMES},
                    "memory": {},
                    "byte_pcs": defaultdict(Counter),
                    "byte_lrs": defaultdict(Counter),
                    "first_writes": [],
                    "write_count": 0,
                    "address_min": None,
                    "address_max": None,
                },
            )
            group["write_count"] += 1
            group["watch_addresses"][watch_address] += 1
            group["watch_sizes"][watch_size] += 1
            group["pc_counts"][pc] += 1
            group["lr_counts"][lr] += 1
            group["address_min"] = address if group["address_min"] is None else min(group["address_min"], address)
            group["address_max"] = address + size - 1 if group["address_max"] is None else max(group["address_max"], address + size - 1)

            for register in REGISTER_NAMES:
                register_value = row_reg(row, register)
                if register_value is not None:
                    group["register_counts"][register][register_value] += 1

            if len(group["first_writes"]) < 32:
                group["first_writes"].append(
                    {
                        "serial": row.get("serial", ""),
                        "address": f"0x{address:08X}",
                        "size": size,
                        "value": f"0x{value:016X}",
                        "watch_address": f"0x{watch_address:08X}",
                        "pc": map_pc(functions, pc),
                        "lr": map_pc(functions, lr),
                        "r0": f"0x{row_reg(row, 'r0'):08X}" if row_reg(row, "r0") is not None else None,
                        "r1": f"0x{row_reg(row, 'r1'):08X}" if row_reg(row, "r1") is not None else None,
                        "r2": f"0x{row_reg(row, 'r2'):08X}" if row_reg(row, "r2") is not None else None,
                        "r4": f"0x{row_reg(row, 'r4'):08X}" if row_reg(row, "r4") is not None else None,
                    }
                )

            for index, byte in enumerate(bytes_from_write(value, size)):
                byte_address = address + index
                group["memory"][byte_address] = byte
                group["byte_pcs"][byte_address][pc] += 1
                group["byte_lrs"][byte_address][lr] += 1

    label_reports = []
    compact_payloads = []
    for label, group in sorted(labels.items(), key=lambda item: item[1]["write_count"], reverse=True):
        watch_address = group["watch_addresses"].most_common(1)[0][0] if group["watch_addresses"] else None
        watch_size = group["watch_sizes"].most_common(1)[0][0] if group["watch_sizes"] else 0
        owner_bases = group["register_counts"]["r4"]
        owner_base = owner_bases.most_common(1)[0][0] if owner_bases else None
        payload_match = COMPACT_PAYLOAD_RE.search(label)
        compact_payload = (
            decode_compact_payload(group["memory"], watch_address)
            if watch_address is not None and payload_match
            else None
        )
        if compact_payload is not None:
            compact_payload["label"] = label
            compact_payload["slot"] = int(payload_match.group("slot")) if payload_match else None
            compact_payload["owner_base_from_r4"] = f"0x{owner_base:08X}" if owner_base is not None else None
            compact_payload["owner_structure_offset_from_r4"] = (
                f"0x{(watch_address - owner_base) & 0xFFFFFFFF:02X}" if owner_base is not None else None
            )
            compact_payload["byte_writers"] = byte_writer_summary(
                group["memory"], group["byte_pcs"], group["byte_lrs"], functions, watch_address, 6
            )
            compact_payloads.append(compact_payload)

        label_reports.append(
            {
                "label": label,
                "write_count": group["write_count"],
                "watch_addresses": counter_top_hex(group["watch_addresses"], "address", 8),
                "watch_sizes": [{"size": size, "count": count} for size, count in group["watch_sizes"].most_common(8)],
                "address_range": (
                    f"0x{group['address_min']:08X}..0x{group['address_max']:08X}"
                    if group["address_min"] is not None and group["address_max"] is not None
                    else None
                ),
                "top_writer_pcs": counter_top_pcs(functions, group["pc_counts"], 16),
                "top_link_registers": counter_top_pcs(functions, group["lr_counts"], 12),
                "top_register_values": {
                    register: counter_top_hex(counter, "value", 8)
                    for register, counter in group["register_counts"].items()
                    if register in {"r0", "r1", "r2", "r4", "r6", "r9", "r11"} and counter
                },
                "owner_base_candidate_from_r4": f"0x{owner_base:08X}" if owner_base is not None else None,
                "watch_offset_from_owner_base": (
                    f"0x{(watch_address - owner_base) & 0xFFFFFFFF:02X}"
                    if watch_address is not None and owner_base is not None
                    else None
                ),
                "decoded_compact_payload": compact_payload,
                "first_writes": group["first_writes"],
                "payload_window_byte_writers": (
                    byte_writer_summary(group["memory"], group["byte_pcs"], group["byte_lrs"], functions, watch_address, min(watch_size, 64))
                    if watch_address is not None
                    else []
                ),
            }
        )

    native_environment_payload_owner = any(
        any(
            (pc.get("smallest_containing_function") or {}).get("entry") == "0x0045DD50"
            for pc in label_report["top_writer_pcs"][:8]
        )
        for label_report in label_reports
        if "payload_slot" in label_report["label"]
    )

    return {
        "format": "oot3d_actor_vs_payload_source_probe_v1",
        "policy": "validation_only_emulator_trace_not_runtime_asset_source",
        "writer_trace": str(writer_trace_path),
        "functions_csv": [str(path) for path in functions_csv],
        "raw_trace_rows": raw_rows,
        "unique_seed_rows_after_adjacent_dedupe": unique_rows,
        "seed_label_counts": [{"label": label, "count": count} for label, count in seed_label_counts.most_common(16)],
        "top_trace_pcs": counter_top_pcs(functions, all_pc_counts, 24),
        "top_trace_link_registers": counter_top_pcs(functions, all_lr_counts, 24),
        "compact_payload_count": len(compact_payloads),
        "compact_payloads": compact_payloads,
        "native_environment_payload_owner_resolved": native_environment_payload_owner,
        "owner_interpretation": {
            "producer_function": "FUN_0045dd50",
            "producer_function_entry": "0x0045DD50",
            "observed_slot0_payload_offset_from_r4": "0x30",
            "observed_slot1_payload_offset_from_r4": "0x48",
            "observed_slot0_direction_source_offsets_from_r4": ["0xB5", "0xB6", "0xB7"],
            "observed_slot1_direction_source_offsets_from_r4": ["0xBB", "0xBC", "0xBD"],
            "observed_slot0_color_offsets_from_r4": ["0x33", "0x34", "0x35"],
            "observed_slot1_color_offsets_from_r4": ["0x4B", "0x4C", "0x4D"],
            "consumer_handler": "0x00253A4C via FUN_0035BF50 handler table",
            "basis": "writer trace rows map compact payload writes to 0x0045DD50; prior source-record probe maps the same payload addresses to the native 0x00253A4C light-record handler",
        },
        "labels": label_reports,
    }


def write_markdown(report: dict[str, Any], output_path: Path) -> None:
    lines = [
        "# OOT3D Actor/VS Payload Source Probe",
        "",
        "This report is validation/backtrace evidence only. It must not be used as runtime replacement data.",
        "",
        "## Inputs",
        "",
        f"- Writer trace: `{report['writer_trace']}`",
        f"- Functions CSV: `{', '.join(report['functions_csv'])}`",
        f"- Raw trace rows: `{report['raw_trace_rows']}`",
        f"- Unique seed rows after adjacent dedupe: `{report['unique_seed_rows_after_adjacent_dedupe']}`",
        "",
        "## Seed Labels",
        "",
        "| Label | Rows |",
        "|---|---:|",
    ]
    for item in report["seed_label_counts"]:
        lines.append(f"| `{item['label']}` | {item['count']} |")

    lines.extend(
        [
            "",
            "## Compact Payloads",
            "",
            "| Label | Slot | Owner base | Offset | Bytes | Dir s8 | RGB |",
            "|---|---:|---|---|---|---|---|",
        ]
    )
    for payload in report["compact_payloads"]:
        lines.append(
            "| "
            f"`{payload['label']}` | "
            f"{payload['slot']} | "
            f"`{payload['owner_base_from_r4']}` | "
            f"`{payload['owner_structure_offset_from_r4']}` | "
            f"`{payload['bytes_hex']}` | "
            f"`{payload['direction_s8']}` | "
            f"`{payload['color_rgb_u8']}` |"
        )

    lines.extend(
        [
            "",
            "## Payload Writer PCs",
            "",
            "| Label | PC | Count | Mapping | Note |",
            "|---|---|---:|---|---|",
        ]
    )
    for label in report["labels"]:
        if "payload_slot" not in label["label"]:
            continue
        for item in label["top_writer_pcs"][:10]:
            lines.append(
                f"| `{label['label']}` | `{item['pc']}` | {item['count']} | "
                f"{mapping_label(item)} | `{item['mapping_note']}` |"
            )

    lines.extend(
        [
            "",
            "## Byte Writers For Compact Payload Fields",
            "",
        ]
    )
    for payload in report["compact_payloads"]:
        lines.extend(
            [
                f"### {payload['label']}",
                "",
                "| Offset | Address | Value | Top writer PCs |",
                "|---|---|---|---|",
            ]
        )
        for byte in payload["byte_writers"]:
            writers = ", ".join(f"{item['pc']}:{item['count']}" for item in byte["top_writer_pcs"][:4])
            lines.append(
                f"| `{byte['offset']}` | `{byte['address']}` | `{byte['value']}` | `{writers}` |"
            )
        lines.append("")

    lines.extend(
        [
            "## Interpretation",
            "",
            f"- Native environment payload owner resolved: `{report['native_environment_payload_owner_resolved']}`.",
            "- Compact actor/VS payloads are runtime `dir[0..2] + rgb[3..5]` records, not raw final PICA uniform values.",
            "- The payload writes map to `FUN_0045dd50` (`0x0045DD50`), the native environment-light setting consumer already associated with scene light-setting interpolation.",
            "- Observed owner-base offsets are `r4+0x30` for slot 0 and `r4+0x48` for slot 1. Direction bytes are copied from `r4+0xB5..0xB7` and `r4+0xBB..0xBD`; color bytes are populated at `r4+0x33..0x35` and `r4+0x4B..0x4D`.",
            "- The previous source-record probe proves `FUN_0035BF50 -> 0x00253A4C` consumes these compact payloads and expands them into native `0x10`-stride light records.",
            "- Engine work should therefore consume the native `0x0045DD50` resolved runtime light-setting state and reproduce the compact payload transform; emulator bytes remain comparison evidence only.",
            "",
        ]
    )
    output_path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--writer-trace", required=True, type=Path)
    parser.add_argument("--functions-csv", action="append", type=Path, default=[])
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-md", required=True, type=Path)
    args = parser.parse_args()

    report = analyze(args.writer_trace, args.functions_csv)
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(report, indent=2), encoding="utf-8")
    write_markdown(report, args.output_md)
    print(
        json.dumps(
            {
                "output_json": str(args.output_json),
                "output_md": str(args.output_md),
                "compact_payload_count": report["compact_payload_count"],
                "native_environment_payload_owner_resolved": report["native_environment_payload_owner_resolved"],
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
