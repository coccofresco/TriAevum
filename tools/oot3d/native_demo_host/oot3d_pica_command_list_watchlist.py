#!/usr/bin/env python3
"""Build Azahar memory-watch ranges from an OOT3D PICA frame JSONL dump."""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


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


@dataclass(frozen=True)
class WatchRange:
    vaddr: int
    size: int
    label: str


def _parse_int(value: Any, field: str) -> int:
    if isinstance(value, bool):
        raise ValueError(f"{field} is boolean, expected integer")
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value, 0)
    raise ValueError(f"{field} has unsupported value {value!r}")


def _command_word_addr(item: dict[str, Any]) -> int | None:
    if "cmd_list_word_addr" in item:
        return _parse_int(item["cmd_list_word_addr"], "cmd_list_word_addr")
    if "cmd_list_addr" not in item or "cmd_list_offset_words" not in item:
        return None
    return (
        _parse_int(item["cmd_list_addr"], "cmd_list_addr")
        + int(item["cmd_list_offset_words"]) * 4
    )


def _virtual_aliases(paddr: int) -> list[tuple[str, int]]:
    if VRAM_PADDR <= paddr < VRAM_PADDR + VRAM_SIZE:
        return [("vram", VRAM_VADDR + (paddr - VRAM_PADDR))]
    if N3DS_EXTRA_RAM_PADDR <= paddr < N3DS_EXTRA_RAM_PADDR + N3DS_EXTRA_RAM_SIZE:
        return [("n3ds_extra_ram", N3DS_EXTRA_RAM_VADDR + (paddr - N3DS_EXTRA_RAM_PADDR))]
    if FCRAM_PADDR <= paddr < FCRAM_PADDR + FCRAM_SIZE:
        offset = paddr - FCRAM_PADDR
        return [
            ("linear_heap", LINEAR_HEAP_VADDR + offset),
            ("new_linear_heap", NEW_LINEAR_HEAP_VADDR + offset),
        ]
    if FCRAM_PADDR + FCRAM_SIZE <= paddr < FCRAM_PADDR + FCRAM_N3DS_SIZE:
        return [("new_linear_heap", NEW_LINEAR_HEAP_VADDR + (paddr - FCRAM_PADDR))]
    return []


def _selected_event(item: dict[str, Any], mode: str, register_names: set[str]) -> bool:
    event = item.get("event")
    if register_names:
        return event == "register_write" and str(item.get("name", "")) in register_names
    if mode == "all":
        return event in {"register_write", "shader_uniform_upload"}
    if event != "shader_uniform_upload":
        return False
    if mode == "uniforms":
        return True
    if mode == "vertex-hemisphere":
        try:
            index = int(item.get("uniform_index"))
        except (TypeError, ValueError):
            return False
        return item.get("shader") == "vs" and 76 <= index <= 86
    raise ValueError(f"unknown mode {mode}")


def _load_watch_ranges(
    path: Path, mode: str, span_bytes: int, register_names: set[str]
) -> tuple[list[WatchRange], dict[str, int]]:
    ranges: list[WatchRange] = []
    stats = {
        "jsonl_lines": 0,
        "selected_events": 0,
        "events_without_command_word_addr": 0,
        "events_without_virtual_alias": 0,
    }

    with path.open("r", encoding="utf-8") as f:
        for line_number, line in enumerate(f, start=1):
            line = line.strip()
            if not line:
                continue
            stats["jsonl_lines"] += 1
            item = json.loads(line)
            if not _selected_event(item, mode, register_names):
                continue
            stats["selected_events"] += 1
            word_addr = _command_word_addr(item)
            if word_addr is None:
                stats["events_without_command_word_addr"] += 1
                continue

            span = max(4, min(span_bytes, 64))
            aligned_start = max(0, word_addr - span // 2) & ~0x3
            label_parts = [
                f"oot3d_pica_{item.get('event', 'event')}",
                f"line={line_number}",
                f"paddr=0x{word_addr:08X}",
            ]
            if item.get("shader") is not None:
                label_parts.append(f"shader={item['shader']}")
            if item.get("uniform_index") is not None:
                label_parts.append(f"uniform=f{item['uniform_index']}")
            label = " ".join(label_parts)

            aliases = _virtual_aliases(aligned_start)
            if not aliases:
                stats["events_without_virtual_alias"] += 1
                continue
            for alias_name, vaddr in aliases:
                ranges.append(WatchRange(vaddr=vaddr, size=span, label=f"{label} alias={alias_name}"))

    return ranges, stats


def _dedupe_ranges(ranges: Iterable[WatchRange]) -> list[WatchRange]:
    by_key: dict[tuple[int, int], str] = {}
    for watch in ranges:
        key = (watch.vaddr, watch.size)
        existing = by_key.get(key)
        if existing is None:
            by_key[key] = watch.label
        elif watch.label not in existing:
            by_key[key] = f"{existing} | {watch.label}"
    return [
        WatchRange(vaddr=vaddr, size=size, label=label)
        for (vaddr, size), label in sorted(by_key.items())
    ]


def _write_watchlist(path: Path, ranges: list[WatchRange]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as f:
        for watch in ranges:
            f.write(f"0x{watch.vaddr:08X} {watch.size} # {watch.label}\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path, help="Azahar OOT3D PICA frame JSONL")
    parser.add_argument("--output", required=True, type=Path, help="watch_addresses.txt output")
    parser.add_argument(
        "--mode",
        choices=("vertex-hemisphere", "uniforms", "all"),
        default="vertex-hemisphere",
        help="which command-list events should become memory watches",
    )
    parser.add_argument(
        "--span-bytes",
        type=int,
        default=64,
        help="bytes watched around each command word; Azahar clamps watches to 64 bytes",
    )
    parser.add_argument(
        "--register-name",
        action="append",
        default=[],
        help="limit output to register_write events with this PICA register name; may be repeated",
    )
    parser.add_argument("--summary-out", type=Path, help="optional JSON summary")
    args = parser.parse_args()

    register_names = {str(name) for name in args.register_name}
    ranges, stats = _load_watch_ranges(args.input, args.mode, args.span_bytes, register_names)
    ranges = _dedupe_ranges(ranges)
    _write_watchlist(args.output, ranges)

    summary = {
        "input": str(args.input),
        "output": str(args.output),
        "mode": args.mode,
        "register_names": sorted(register_names),
        "span_bytes": max(4, min(args.span_bytes, 64)),
        "watch_count": len(ranges),
        **stats,
    }
    if args.summary_out:
        args.summary_out.parent.mkdir(parents=True, exist_ok=True)
        args.summary_out.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
