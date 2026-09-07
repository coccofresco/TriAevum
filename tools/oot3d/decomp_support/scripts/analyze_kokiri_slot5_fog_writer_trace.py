#!/usr/bin/env python3
"""Correlate OOT3D fog PICA writes with Azahar writer trace rows.

The output is validation evidence only. It identifies native code paths that
write the command-buffer words later decoded as PICA fog state; it must not be
used as replacement runtime data by the engine.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


FOG_REGISTER_NAMES = {
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
}

SUMMARY_REGISTER_NAMES = {
    "GPUREG_TEXENV_UPDATE_BUFFER",
    "GPUREG_FOG_COLOR",
    "GPUREG_FOG_LUT_INDEX",
    "GPUREG_FOG_LUT_DATA0",
}

WATCHLIST_RE = re.compile(
    r"^(0x[0-9A-Fa-f]+)\s+\d+\s+# .*?paddr=(0x[0-9A-Fa-f]+)"
)

VRAM_PADDR = 0x18000000
VRAM_SIZE = 0x00600000
N3DS_EXTRA_RAM_PADDR = 0x1F000000
N3DS_EXTRA_RAM_SIZE = 0x00400000
FCRAM_PADDR = 0x20000000
FCRAM_SIZE = 0x08000000
FCRAM_N3DS_SIZE = 0x10000000

VRAM_VADDR = 0x1F000000
N3DS_EXTRA_RAM_VADDR = 0x1E800000
LINEAR_HEAP_VADDR = 0x14000000
NEW_LINEAR_HEAP_VADDR = 0x30000000


def parse_int(value: Any) -> int:
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value, 0)
    raise ValueError(f"not an integer-like value: {value!r}")


def fmt_u32(value: int) -> str:
    return f"0x{value & 0xFFFFFFFF:08X}"


def parse_mask(value: Any) -> int:
    if isinstance(value, str):
        text = value.strip()
        if re.fullmatch(r"[01]{1,4}", text):
            return int(text, 2)
        if re.fullmatch(r"[0-9A-Fa-f]{1,2}", text):
            return int(text, 16)
    return parse_int(value)


def parse_reg(value: Any) -> int:
    if isinstance(value, str) and value.lower().startswith("0x"):
        return int(value, 16)
    return parse_int(value)


def pica_header_word(register: int, mask: int) -> int:
    return ((mask & 0xF) << 16) | (register & 0xFFFF)


def virtual_aliases_for_paddr(paddr: int) -> list[int]:
    if VRAM_PADDR <= paddr < VRAM_PADDR + VRAM_SIZE:
        return [VRAM_VADDR + (paddr - VRAM_PADDR)]
    if N3DS_EXTRA_RAM_PADDR <= paddr < N3DS_EXTRA_RAM_PADDR + N3DS_EXTRA_RAM_SIZE:
        return [N3DS_EXTRA_RAM_VADDR + (paddr - N3DS_EXTRA_RAM_PADDR)]
    if FCRAM_PADDR <= paddr < FCRAM_PADDR + FCRAM_SIZE:
        offset = paddr - FCRAM_PADDR
        return [LINEAR_HEAP_VADDR + offset, NEW_LINEAR_HEAP_VADDR + offset]
    if FCRAM_PADDR + FCRAM_SIZE <= paddr < FCRAM_PADDR + FCRAM_N3DS_SIZE:
        return [NEW_LINEAR_HEAP_VADDR + (paddr - FCRAM_PADDR)]
    return []


def top_counter(counter: Counter[tuple[str, str]], limit: int = 12) -> list[dict[str, Any]]:
    return [
        {
            "pc": pc,
            "lr": lr,
            "count": count,
        }
        for (pc, lr), count in counter.most_common(limit)
    ]


def top_value_counter(counter: Counter[int], limit: int = 12) -> list[dict[str, Any]]:
    return [
        {
            "value": fmt_u32(value),
            "count": count,
        }
        for value, count in counter.most_common(limit)
    ]


def load_fog_writes(pica_trace_path: Path) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    trace = json.loads(pica_trace_path.read_text(encoding="utf-8"))
    writes: list[dict[str, Any]] = []
    for ordinal, write in enumerate(trace.get("writes", [])):
        if not isinstance(write, dict) or write.get("name") not in FOG_REGISTER_NAMES:
            continue
        if "cmd_list_word_addr" not in write:
            continue
        register = parse_reg(write.get("cmd_id"))
        mask = parse_mask(write.get("mask", "1111"))
        value = parse_int(write.get("value"))
        writes.append(
            {
                "ordinal": ordinal,
                "trace_line": write.get("trace_line"),
                "name": write.get("name"),
                "register": register,
                "register_hex": f"0x{register:03X}",
                "mask": mask,
                "value": value,
                "value_hex": fmt_u32(value),
                "pica_header": pica_header_word(register, mask),
                "pica_header_hex": fmt_u32(pica_header_word(register, mask)),
                "cmd_list_word_addr": parse_int(write["cmd_list_word_addr"]),
                "cmd_list_word_addr_hex": fmt_u32(parse_int(write["cmd_list_word_addr"])),
            }
        )
    return trace, writes


def load_paddr_aliases(watchlist_path: Path) -> dict[int, set[int]]:
    aliases: dict[int, set[int]] = defaultdict(set)
    for line in watchlist_path.read_text(encoding="utf-8").splitlines():
        match = WATCHLIST_RE.search(line)
        if not match:
            continue
        watch_alias = int(match.group(1), 16)
        paddr = int(match.group(2), 16)
        exact_aliases = virtual_aliases_for_paddr(paddr)
        if exact_aliases:
            aliases[paddr].update(exact_aliases)
        else:
            aliases[paddr].add(watch_alias)
    return aliases


def build_target_index(
    fog_writes: list[dict[str, Any]],
    paddr_aliases: dict[int, set[int]],
    window_bytes: int,
) -> dict[int, list[dict[str, Any]]]:
    target_index: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for write in fog_writes:
        aliases = paddr_aliases.get(write["cmd_list_word_addr"], set())
        for alias in aliases:
            for delta in range(-window_bytes, window_bytes + 1, 4):
                target_index[alias + delta].append(
                    {
                        "write": write,
                        "alias": alias,
                        "delta": delta,
                    }
                )
    return target_index


def row_u32(row: dict[str, str], key: str) -> int:
    return int(row[key], 16) & 0xFFFFFFFF


def writer_row_identity(row: dict[str, str]) -> tuple[str, ...]:
    return tuple(
        row.get(key, "")
        for key in (
            "address",
            "size",
            "value",
            "pc",
            "lr",
            "sp",
            "r0",
            "r1",
            "r2",
            "r3",
            "r4",
            "r5",
            "r6",
            "r7",
            "r8",
            "r9",
            "r10",
            "r11",
            "r12",
        )
    )


def best_matching_target(row: dict[str, str], targets: list[dict[str, Any]]) -> tuple[dict[str, Any], str] | None:
    row_value = row_u32(row, "value")
    address = int(row["address"], 16)
    matches: list[tuple[int, int, dict[str, Any], str]] = []
    for target in targets:
        write = target["write"]
        distance = abs(address - int(target["alias"]))
        if row_value == int(write["value"]):
            matches.append((0, distance, target, "value_word"))
        elif row_value == int(write["pica_header"]):
            matches.append((1, distance, target, "pica_header_word"))
    if not matches:
        return None
    matches.sort(key=lambda item: (item[0], item[1], int(item[2]["write"]["ordinal"])))
    return matches[0][2], matches[0][3]


def analyze_writer_trace(
    writer_trace_path: Path,
    fog_writes: list[dict[str, Any]],
    paddr_aliases: dict[int, set[int]],
    window_bytes: int,
) -> dict[str, Any]:
    target_index = build_target_index(fog_writes, paddr_aliases, window_bytes)
    register_counts: dict[str, dict[str, Any]] = defaultdict(
        lambda: {
            "trace_write_count": 0,
            "trace_values": Counter(),
            "header_values": Counter(),
            "value_word_pc_lr": Counter(),
            "value_word_by_value_pc_lr": defaultdict(Counter),
            "header_word_pc_lr": Counter(),
            "address_word_pc_lr": Counter(),
            "address_word_values": Counter(),
            "address_word_by_value_pc_lr": defaultdict(Counter),
            "matched_row_count": 0,
            "address_row_count": 0,
            "sample_rows": [],
            "address_sample_rows": [],
        }
    )

    for write in fog_writes:
        item = register_counts[write["name"]]
        item["trace_write_count"] += 1
        item["trace_values"][int(write["value"])] += 1
        item["header_values"][int(write["pica_header"])] += 1

    row_count = 0
    duplicate_candidate_row_count = 0
    candidate_row_count = 0
    match_count = 0
    seen_candidate_rows: set[tuple[str, ...]] = set()
    pc_lr_counts: Counter[tuple[str, str]] = Counter()
    match_kind_counts: Counter[str] = Counter()
    unmatched_candidate_values: Counter[int] = Counter()

    with writer_trace_path.open("r", newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            row_count += 1
            try:
                address = int(row["address"], 16)
                value = row_u32(row, "value")
            except Exception:
                continue
            targets = target_index.get(address)
            if not targets:
                continue
            identity = writer_row_identity(row)
            if identity in seen_candidate_rows:
                duplicate_candidate_row_count += 1
                continue
            seen_candidate_rows.add(identity)
            candidate_row_count += 1
            seen_target_registers: set[str] = set()
            for target in sorted(
                targets,
                key=lambda item: (
                    abs(address - int(item["alias"])),
                    int(item["write"]["ordinal"]),
                ),
            ):
                write = target["write"]
                name = write["name"]
                if int(target["delta"]) != 0:
                    continue
                if name in seen_target_registers:
                    continue
                seen_target_registers.add(name)
                register = register_counts[name]
                key = (row.get("pc", ""), row.get("lr", ""))
                register["address_row_count"] += 1
                register["address_word_pc_lr"][key] += 1
                register["address_word_values"][value] += 1
                register["address_word_by_value_pc_lr"][value][key] += 1
                if len(register["address_sample_rows"]) < 8:
                    register["address_sample_rows"].append(
                        {
                            "serial": row.get("serial", ""),
                            "address": row.get("address", ""),
                            "value": fmt_u32(value),
                            "pc": row.get("pc", ""),
                            "lr": row.get("lr", ""),
                            "watch_address": row.get("watch_address", ""),
                            "target_alias": fmt_u32(int(target["alias"])),
                            "target_delta_bytes": int(target["delta"]),
                            "trace_line": write.get("trace_line"),
                        }
                    )
            matched = best_matching_target(row, targets)
            if matched is None:
                unmatched_candidate_values[value] += 1
                continue

            target, match_kind = matched
            write = target["write"]
            key = (row.get("pc", ""), row.get("lr", ""))
            pc_lr_counts[key] += 1
            match_kind_counts[match_kind] += 1
            match_count += 1

            register = register_counts[write["name"]]
            register["matched_row_count"] += 1
            if match_kind == "value_word":
                register["value_word_pc_lr"][key] += 1
                register["value_word_by_value_pc_lr"][value][key] += 1
            else:
                register["header_word_pc_lr"][key] += 1
            if len(register["sample_rows"]) < 8:
                register["sample_rows"].append(
                    {
                        "serial": row.get("serial", ""),
                        "address": row.get("address", ""),
                        "value": fmt_u32(value),
                        "pc": row.get("pc", ""),
                        "lr": row.get("lr", ""),
                        "watch_address": row.get("watch_address", ""),
                        "target_alias": fmt_u32(int(target["alias"])),
                        "target_delta_bytes": int(target["delta"]),
                        "trace_line": write.get("trace_line"),
                        "match_kind": match_kind,
                    }
                )

    registers: dict[str, Any] = {}
    for name in sorted(register_counts):
        item = register_counts[name]
        registers[name] = {
            "trace_write_count": item["trace_write_count"],
            "trace_values": top_value_counter(item["trace_values"]),
            "header_values": top_value_counter(item["header_values"]),
            "matched_row_count": item["matched_row_count"],
            "address_row_count": item["address_row_count"],
            "address_word_top_pc_lr": top_counter(item["address_word_pc_lr"]),
            "address_word_values": top_value_counter(item["address_word_values"]),
            "address_word_by_value_top_pc_lr": {
                fmt_u32(value): top_counter(counter)
                for value, counter in sorted(item["address_word_by_value_pc_lr"].items())
            },
            "value_word_top_pc_lr": top_counter(item["value_word_pc_lr"]),
            "value_word_by_value_top_pc_lr": {
                fmt_u32(value): top_counter(counter)
                for value, counter in sorted(item["value_word_by_value_pc_lr"].items())
            },
            "header_word_top_pc_lr": top_counter(item["header_word_pc_lr"]),
            "sample_rows": item["sample_rows"],
            "address_sample_rows": item["address_sample_rows"],
        }

    return {
        "writer_trace": str(writer_trace_path),
        "writer_trace_row_count": row_count,
        "window_bytes": window_bytes,
        "target_address_count": len(target_index),
        "candidate_row_count": candidate_row_count,
        "duplicate_candidate_row_count": duplicate_candidate_row_count,
        "matched_row_count": match_count,
        "match_kind_counts": dict(match_kind_counts),
        "top_matched_pc_lr": top_counter(pc_lr_counts, 16),
        "top_unmatched_candidate_values": top_value_counter(unmatched_candidate_values, 16),
        "registers": registers,
    }


def summarize_trace(trace: dict[str, Any], fog_writes: list[dict[str, Any]], paddr_aliases: dict[int, set[int]]) -> dict[str, Any]:
    draw_summary = trace.get("frame_capture", {}).get("draw_summary", {})
    value_counts: dict[str, Counter[int]] = defaultdict(Counter)
    alias_counts: Counter[int] = Counter()
    missing_alias_count = 0
    for write in fog_writes:
        value_counts[write["name"]][int(write["value"])] += 1
        aliases = paddr_aliases.get(write["cmd_list_word_addr"], set())
        if not aliases:
            missing_alias_count += 1
        alias_counts[len(aliases)] += 1
    return {
        "source_kind": trace.get("source_kind", ""),
        "fog_write_count": len(fog_writes),
        "draw_count": draw_summary.get("draw_count", 0),
        "fog_decoded_draw_count": draw_summary.get("fog_decoded_draw_count", 0),
        "fog_enabled_draw_count": draw_summary.get("fog_enabled_draw_count", 0),
        "fog_mode_counts": draw_summary.get("fog_mode_counts", {}),
        "fog_color_counts": draw_summary.get("fog_color_counts", {}),
        "fog_write_value_counts": {
            name: top_value_counter(counter)
            for name, counter in sorted(value_counts.items())
            if name in SUMMARY_REGISTER_NAMES
        },
        "paddr_alias_count_distribution": {
            str(count): occurrences for count, occurrences in sorted(alias_counts.items())
        },
        "missing_alias_write_count": missing_alias_count,
    }


def write_markdown(report: dict[str, Any], path: Path) -> None:
    trace = report["trace_summary"]
    writer = report["writer_summary"]
    lines = [
        "# OOT3D Fog Writer Trace",
        "",
        "This report is validation evidence only. It must not be used as runtime replacement data.",
        "",
        "## Inputs",
        "",
        f"- PICA trace: `{report['pica_trace']}`",
        f"- Writer trace: `{writer['writer_trace']}`",
        f"- Watchlist: `{report['watchlist']}`",
        "",
        "## PICA Fog State",
        "",
        f"- Fog writes decoded: `{trace['fog_write_count']}`",
        f"- Fog-enabled draws: `{trace['fog_enabled_draw_count']}/{trace['fog_decoded_draw_count']}`",
        f"- Draw fog modes: `{trace['fog_mode_counts']}`",
        f"- Draw fog colors: `{trace['fog_color_counts']}`",
        "",
        "## Writer Correlation",
        "",
        f"- Candidate command-buffer writer rows: `{writer['candidate_row_count']}`",
        f"- Matched value/header rows: `{writer['matched_row_count']}`",
        f"- Match kinds: `{writer['match_kind_counts']}`",
        "",
        "| Count | PC | LR |",
        "|---:|---|---|",
    ]
    for item in writer["top_matched_pc_lr"][:12]:
        lines.append(f"| {item['count']} | `{item['pc']}` | `{item['lr']}` |")

    lines.extend(["", "## Registers", ""])
    for name in sorted(writer["registers"]):
        item = writer["registers"][name]
        lines.extend(
            [
                f"### {name}",
                "",
                f"- Trace writes: `{item['trace_write_count']}`",
                f"- Matched writer rows: `{item['matched_row_count']}`",
                f"- Target-address writer rows: `{item.get('address_row_count', 0)}`",
                f"- Trace values: `{item['trace_values']}`",
                f"- Header values: `{item['header_values']}`",
                f"- Target-address values: `{item.get('address_word_values', [])}`",
                "",
                "| Kind | Count | PC | LR |",
                "|---|---:|---|---|",
            ]
        )
        for match_kind, key in (
            ("value", "value_word_top_pc_lr"),
            ("header", "header_word_top_pc_lr"),
            ("target-address", "address_word_top_pc_lr"),
        ):
            for pc_lr in item[key][:8]:
                lines.append(
                    f"| {match_kind} | {pc_lr['count']} | `{pc_lr['pc']}` | `{pc_lr['lr']}` |"
                )
        lines.append("")
        value_groups = item.get("value_word_by_value_top_pc_lr", {})
        if value_groups:
            lines.extend(["| Value | Count | PC | LR |", "|---|---:|---|---|"])
            for value, pc_lrs in value_groups.items():
                for pc_lr in pc_lrs[:6]:
                    lines.append(
                        f"| `{value}` | {pc_lr['count']} | `{pc_lr['pc']}` | `{pc_lr['lr']}` |"
                    )
            lines.append("")
        address_value_groups = item.get("address_word_by_value_top_pc_lr", {})
        if address_value_groups:
            lines.extend(["| Target Address Value | Count | PC | LR |", "|---|---:|---|---|"])
            for value, pc_lrs in address_value_groups.items():
                for pc_lr in pc_lrs[:6]:
                    lines.append(
                        f"| `{value}` | {pc_lr['count']} | `{pc_lr['pc']}` | `{pc_lr['lr']}` |"
                    )
            lines.append("")

    lines.extend(
        [
            "## Interpretation",
            "",
            "- `GPUREG_TEXENV_UPDATE_BUFFER` and `GPUREG_FOG_COLOR` value/header rows identify the material scalar emit path that submits native fog mode and color packets.",
            "- `GPUREG_FOG_LUT_INDEX` and `GPUREG_FOG_LUT_DATA0` rows identify the LUT/table emit path; zero-valued index rows are noisier and must be interpreted together with header and non-zero LUT data rows.",
            "- The command-buffer evidence should guide Ghidra/dataflow work back to code.bin and asset records; it is not a source of runtime constants.",
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pica-trace", required=True, type=Path)
    parser.add_argument("--writer-trace", required=True, type=Path)
    parser.add_argument("--watchlist", required=True, type=Path)
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-md", required=True, type=Path)
    parser.add_argument("--window-bytes", type=int, default=32)
    args = parser.parse_args()

    trace, fog_writes = load_fog_writes(args.pica_trace)
    aliases = load_paddr_aliases(args.watchlist)
    report = {
        "format": "oot3d_kokiri_slot5_fog_writer_trace_v1",
        "policy": "validation_only_emulator_trace_not_runtime_asset_source",
        "pica_trace": str(args.pica_trace),
        "watchlist": str(args.watchlist),
        "trace_summary": summarize_trace(trace, fog_writes, aliases),
        "writer_summary": analyze_writer_trace(
            args.writer_trace,
            fog_writes,
            aliases,
            args.window_bytes,
        ),
    }
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(report, indent=2), encoding="utf-8", newline="\n")
    write_markdown(report, args.output_md)
    print(
        json.dumps(
            {
                "output_json": str(args.output_json),
                "output_md": str(args.output_md),
                "fog_write_count": report["trace_summary"]["fog_write_count"],
                "matched_row_count": report["writer_summary"]["matched_row_count"],
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
