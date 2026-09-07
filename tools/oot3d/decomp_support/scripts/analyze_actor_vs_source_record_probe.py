#!/usr/bin/env python3
"""Analyze OOT3D actor light source-record writer probes.

This consumes an Azahar writer trace aimed at the scratch light-record block
that feeds FUN_0035bbe0. The report is validation/backtrace evidence only:
it must not become runtime light data for the engine.
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


RECORD_STRIDE = 0x10
RECORD_FIELD_NAMES = {
    0x0: "color_r_u8",
    0x1: "color_g_u8",
    0x2: "color_b_u8",
    0x3: "selection_weight_or_priority_u8",
    0x8: "direction_x_s8",
    0x9: "direction_y_s8",
    0xA: "direction_z_s8",
}
CODE_VMA_BASE = 0x00100000
LIGHT_HANDLER_TABLE_LITERAL = 0x0035BFB0


def parse_hex(value: str) -> int:
    return int(value, 16) & 0xFFFFFFFFFFFFFFFF


def parse_int(value: str) -> int:
    return int(value, 0)


def u8_to_s8(value: int) -> int:
    value &= 0xFF
    return value - 0x100 if value & 0x80 else value


def bytes_from_write(value: int, size: int) -> list[int]:
    return [(value >> (8 * index)) & 0xFF for index in range(size)]


def counter_top_hex(counter: Counter[int], limit: int = 12) -> list[dict[str, Any]]:
    return [{"value": f"0x{value:08X}", "count": count} for value, count in counter.most_common(limit)]


def load_functions(path: Path | None) -> list[dict[str, Any]]:
    if path is None or not path.exists():
        return []
    functions: list[dict[str, Any]] = []
    with path.open("r", newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            try:
                entry = int(row["entry"], 16)
                body_min = int(row["body_min"], 16)
                body_max = int(row["body_max"], 16)
            except Exception:
                continue
            functions.append(
                {
                    "name": row.get("name", ""),
                    "entry": entry,
                    "body_min": body_min,
                    "body_max": body_max,
                    "signature": row.get("signature", ""),
                }
            )
    functions.sort(key=lambda item: (item["body_min"], item["body_max"], item["entry"]))
    return functions


def map_pc(functions: list[dict[str, Any]], pc: int) -> dict[str, Any]:
    exact = [function for function in functions if function["entry"] == pc]
    containing = [
        function
        for function in functions
        if function["body_min"] <= pc <= function["body_max"]
    ]
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


def format_function(function: dict[str, Any]) -> dict[str, Any]:
    return {
        "name": function["name"],
        "entry": f"0x{function['entry']:08X}",
        "body_min": f"0x{function['body_min']:08X}",
        "body_max": f"0x{function['body_max']:08X}",
        "signature": function.get("signature", ""),
    }


def write_key(row: dict[str, str]) -> tuple[Any, ...]:
    register_names = ["sp", "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12"]
    return tuple(
        [row.get("source", ""), row.get("title_id", ""), row["address"], row["size"], row["value"], row["pc"], row["lr"]]
        + [row.get(name, "") for name in register_names]
    )


def value_to_hex(value: int | None) -> str | None:
    if value is None:
        return None
    return f"0x{value:08X}"


def row_reg(row: dict[str, str], name: str) -> int | None:
    value = row.get(name)
    if not value:
        return None
    try:
        return int(value, 16)
    except Exception:
        return None


def analyze(writer_trace_path: Path, functions_csv: Path | None, code_bin: Path | None) -> dict[str, Any]:
    functions = load_functions(functions_csv)
    code_bin_size = code_bin.stat().st_size if code_bin is not None and code_bin.exists() else None
    code_bytes = code_bin.read_bytes() if code_bin is not None and code_bin.exists() else b""
    handler_table = read_handler_table(code_bytes, functions)

    memory: dict[int, int] = {}
    byte_writers: dict[int, Counter[int]] = defaultdict(Counter)
    byte_lrs: dict[int, Counter[int]] = defaultdict(Counter)
    trace_pc_counts: Counter[int] = Counter()
    trace_lr_counts: Counter[int] = Counter()
    watch_counts: Counter[int] = Counter()
    record_groups: dict[int, dict[str, Any]] = {}
    raw_rows = 0
    unique_writes = 0
    previous_key: tuple[Any, ...] | None = None

    with writer_trace_path.open("r", newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            raw_rows += 1
            try:
                address = int(row["address"], 16)
                size = parse_int(row["size"])
                value = parse_hex(row["value"])
                pc = int(row["pc"], 16)
                lr = int(row["lr"], 16)
                watch_address = int(row["watch_address"], 16)
            except Exception:
                continue

            key = write_key(row)
            if key == previous_key:
                continue
            previous_key = key
            unique_writes += 1
            trace_pc_counts[pc] += 1
            trace_lr_counts[lr] += 1
            watch_counts[watch_address] += 1

            written_bytes = bytes_from_write(value, size)
            for index, byte in enumerate(written_bytes):
                byte_address = address + index
                memory[byte_address] = byte
                byte_writers[byte_address][pc] += 1
                byte_lrs[byte_address][lr] += 1

            destination = row_reg(row, "r2")
            source_payload = row_reg(row, "r1")
            scratch_base = row_reg(row, "r0")
            if destination is None:
                continue
            offset = address - destination
            if 0 <= offset < RECORD_STRIDE:
                group = record_groups.setdefault(
                    destination,
                    {
                        "destination": destination,
                        "scratch_bases": Counter(),
                        "source_payloads": Counter(),
                        "writer_pcs": Counter(),
                        "link_registers": Counter(),
                        "write_count": 0,
                    },
                )
                if scratch_base is not None:
                    group["scratch_bases"][scratch_base] += 1
                if source_payload is not None:
                    group["source_payloads"][source_payload] += 1
                group["writer_pcs"][pc] += 1
                group["link_registers"][lr] += 1
                group["write_count"] += 1

    direct_record_pcs = Counter()
    direct_record_lrs = Counter()
    records = []
    for destination, group in sorted(record_groups.items()):
        record_bytes = [memory.get(destination + offset) for offset in range(RECORD_STRIDE)]
        if all(byte is None for byte in record_bytes):
            continue
        direct_record_pcs.update(group["writer_pcs"])
        direct_record_lrs.update(group["link_registers"])
        color = [record_bytes[index] for index in (0, 1, 2)]
        direction_raw = [record_bytes[index] for index in (8, 9, 10)]
        direction_s8 = [u8_to_s8(value) if value is not None else None for value in direction_raw]
        direction_127 = [
            (float(value) / 127.0 if value is not None else None)
            for value in direction_s8
        ]
        direction_len = (
            math.sqrt(sum(float(value) * float(value) for value in direction_127 if value is not None))
            if all(value is not None for value in direction_127)
            else None
        )
        field_writers = {}
        for offset, name in RECORD_FIELD_NAMES.items():
            address = destination + offset
            field_writers[f"0x{offset:02X}"] = {
                "name": name,
                "address": f"0x{address:08X}",
                "value_u8": record_bytes[offset],
                "top_writer_pcs": counter_top_hex(byte_writers[address], 6),
                "top_link_registers": counter_top_hex(byte_lrs[address], 6),
            }
        source_payloads = [
            {
                "address": f"0x{value:08X}",
                "count": count,
                "code_bin_offset_if_code_vma_0x00100000": code_offset(value, code_bin_size),
            }
            for value, count in group["source_payloads"].most_common(8)
        ]
        is_light_candidate = (
            all(value is not None for value in color)
            and all(value is not None for value in direction_s8)
            and direction_len is not None
            and 0.1 <= direction_len <= 1.5
            and bool(source_payloads)
        )
        records.append(
            {
                "destination": f"0x{destination:08X}",
                "is_native_light_record_candidate": is_light_candidate,
                "write_count": group["write_count"],
                "scratch_bases": counter_top_hex(group["scratch_bases"], 8),
                "source_payloads": source_payloads,
                "writer_pcs": [map_pc(functions, value) | {"count": count} for value, count in group["writer_pcs"].most_common(8)],
                "link_registers": [map_pc(functions, value) | {"count": count} for value, count in group["link_registers"].most_common(8)],
                "bytes_hex": " ".join("--" if byte is None else f"{byte:02X}" for byte in record_bytes),
                "color_rgb_u8": color,
                "direction_s8": direction_s8,
                "direction_scaled_by_1_over_127": direction_127,
                "direction_scaled_length": direction_len,
                "selection_weight_or_priority_u8": record_bytes[3],
                "field_writers": field_writers,
            }
        )
    records.sort(key=lambda item: (item["write_count"], item["destination"]), reverse=True)
    light_records = [record for record in records if record["is_native_light_record_candidate"]]
    light_record_pcs: Counter[int] = Counter()
    light_record_lrs: Counter[int] = Counter()
    for record in light_records:
        for item in record["writer_pcs"]:
            light_record_pcs[int(item["pc"], 16)] += int(item["count"])
        for item in record["link_registers"]:
            light_record_lrs[int(item["pc"], 16)] += int(item["count"])

    return {
        "format": "oot3d_actor_vs_source_record_probe_v1",
        "policy": "validation_only_emulator_trace_not_runtime_asset_source",
        "writer_trace": str(writer_trace_path),
        "functions_csv": str(functions_csv) if functions_csv is not None else None,
        "code_bin": str(code_bin) if code_bin is not None else None,
        "code_bin_size": code_bin_size,
        "handler_table": handler_table,
        "raw_trace_rows": raw_rows,
        "unique_write_rows_after_adjacent_watch_dedupe": unique_writes,
        "watch_windows": counter_top_hex(watch_counts, 12),
        "top_trace_pcs": [map_pc(functions, value) | {"count": count} for value, count in trace_pc_counts.most_common(24)],
        "top_trace_link_registers": [map_pc(functions, value) | {"count": count} for value, count in trace_lr_counts.most_common(24)],
        "direct_record_write_pcs": [
            map_pc(functions, value) | {"count": count} for value, count in direct_record_pcs.most_common(16)
        ],
        "direct_record_link_registers": [
            map_pc(functions, value) | {"count": count} for value, count in direct_record_lrs.most_common(16)
        ],
        "native_light_record_candidate_count": len(light_records),
        "native_light_record_candidate_write_pcs": [
            map_pc(functions, value) | {"count": count} for value, count in light_record_pcs.most_common(16)
        ],
        "native_light_record_candidate_link_registers": [
            map_pc(functions, value) | {"count": count} for value, count in light_record_lrs.most_common(16)
        ],
        "record_count": len(records),
        "native_light_record_candidates": light_records[:96],
        "records": records[:96],
    }


def code_offset(address: int, code_bin_size: int | None) -> str | None:
    if code_bin_size is None:
        return None
    offset = address - CODE_VMA_BASE
    if 0 <= offset < code_bin_size:
        return f"0x{offset:08X}"
    return None


def read_u32_le(code_bytes: bytes, address: int) -> int | None:
    offset = address - CODE_VMA_BASE
    if offset < 0 or offset + 4 > len(code_bytes):
        return None
    return struct.unpack_from("<I", code_bytes, offset)[0]


def read_handler_table(code_bytes: bytes, functions: list[dict[str, Any]]) -> dict[str, Any]:
    pointer = read_u32_le(code_bytes, LIGHT_HANDLER_TABLE_LITERAL)
    entries: list[dict[str, Any]] = []
    if pointer is not None:
        for index in range(3):
            value = read_u32_le(code_bytes, pointer + index * 4)
            if value is not None:
                entries.append({"index": index, "handler": map_pc(functions, value)})
    return {
        "literal_address": f"0x{LIGHT_HANDLER_TABLE_LITERAL:08X}",
        "table_pointer": f"0x{pointer:08X}" if pointer is not None else None,
        "entries": entries,
    }


def write_markdown(report: dict[str, Any], output_path: Path) -> None:
    lines = [
        "# OOT3D Actor/VS Source Record Probe",
        "",
        "This report is validation/backtrace evidence only. It must not be used as runtime replacement data.",
        "",
        "## Inputs",
        "",
        f"- Writer trace: `{report['writer_trace']}`",
        f"- Functions CSV: `{report['functions_csv']}`",
        f"- code.bin: `{report['code_bin']}`",
        f"- Handler table: `{report['handler_table']['literal_address']} -> {report['handler_table']['table_pointer']}`",
        f"- Raw trace rows: `{report['raw_trace_rows']}`",
        f"- Unique writes after adjacent watch dedupe: `{report['unique_write_rows_after_adjacent_watch_dedupe']}`",
        "",
        "## Native Light Record Writer PCs",
        "",
        "| PC | Count | Mapping | Note |",
        "|---|---:|---|---|",
    ]
    for item in report["native_light_record_candidate_write_pcs"]:
        lines.append(
            f"| `{item['pc']}` | {item['count']} | {mapping_label(item)} | `{item['mapping_note']}` |"
        )
    lines.extend(["", "## Handler Table", "", "| Index | Handler | Mapping | Note |", "|---:|---|---|---|"])
    for entry in report["handler_table"]["entries"]:
        handler = entry["handler"]
        lines.append(
            f"| {entry['index']} | `{handler['pc']}` | {mapping_label(handler)} | `{handler['mapping_note']}` |"
        )
    lines.extend(
        [
            "",
            "## Native Light Record Link Registers",
            "",
            "| LR | Count | Mapping | Note |",
            "|---|---:|---|---|",
        ]
    )
    for item in report["native_light_record_candidate_link_registers"]:
        lines.append(
            f"| `{item['pc']}` | {item['count']} | {mapping_label(item)} | `{item['mapping_note']}` |"
        )
    lines.extend(
        [
            "",
            "## Reconstructed Source Records",
            "",
        ]
    )
    for record in report["native_light_record_candidates"][:24]:
        lines.extend(
            [
                f"### {record['destination']}",
                "",
                f"- Bytes: `{record['bytes_hex']}`",
                f"- RGB u8: `{record['color_rgb_u8']}`",
                f"- Direction s8: `{record['direction_s8']}`",
                f"- Direction scaled by `1/127`: `{record['direction_scaled_by_1_over_127']}`",
                f"- Direction length after scale: `{record['direction_scaled_length']}`",
                f"- Source payloads: `{', '.join(payload['address'] for payload in record['source_payloads'][:4])}`",
                f"- Scratch bases: `{', '.join(item['value'] for item in record['scratch_bases'][:4])}`",
                "",
            ]
        )
    lines.extend(
        [
            "## Interpretation",
            "",
            "- `FUN_0035BBE0` consumes these records as count + `0x10`-stride native light entries.",
            "- The direct writer PC/LR pair identifies the handler dispatch path before PICA packet generation.",
            "- Source payload addresses are runtime pointers; if they are not inside `code.bin`, the next step is to bind them to the original asset or runtime structure that created them.",
            "",
        ]
    )
    output_path.write_text("\n".join(lines), encoding="utf-8")


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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--writer-trace", required=True, type=Path)
    parser.add_argument("--functions-csv", type=Path)
    parser.add_argument("--code-bin", type=Path)
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-md", required=True, type=Path)
    args = parser.parse_args()

    report = analyze(args.writer_trace, args.functions_csv, args.code_bin)
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(report, indent=2), encoding="utf-8")
    write_markdown(report, args.output_md)
    print(
        json.dumps(
            {
                "output_json": str(args.output_json),
                "output_md": str(args.output_md),
                "record_count": report["record_count"],
                "native_light_record_candidate_count": report["native_light_record_candidate_count"],
                "top_native_light_record_pc": (
                    report["native_light_record_candidate_write_pcs"][0]
                    if report["native_light_record_candidate_write_pcs"]
                    else None
                ),
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
