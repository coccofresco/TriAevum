#!/usr/bin/env python3
"""Inspect OOT3D QM message container indices without exporting dialogue text."""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
DEFAULT_QM = ROOT.parent / "work" / "extract" / "romfs" / "message" / "eu" / "eu.qm"
HEADER_SIZE = 0x10


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        try:
            return str(path.relative_to(ROOT.parent)).replace("\\", "/")
        except ValueError:
            return str(path).replace("\\", "/")


def infer_entry_size(data: bytes, count: int) -> tuple[int, int]:
    if len(data) < HEADER_SIZE + 0x60:
        raise ValueError("QM file is too small for a header and one entry")

    # The first text payload offset is stored in the first entry. Valid OOT3D EU
    # files end the record table exactly at that first payload offset.
    first_words = struct.unpack_from("<24I", data, HEADER_SIZE)
    offsets = [
        value
        for value in first_words
        if HEADER_SIZE < value < len(data)
        and (value - HEADER_SIZE) % count == 0
        and (value - HEADER_SIZE) // count >= 0x20
    ]
    if not offsets:
        raise ValueError("could not infer first payload offset from the first record")
    first_payload = min(offsets)
    table_size = first_payload - HEADER_SIZE
    if table_size % count != 0:
        raise ValueError(f"record table size {table_size:#x} is not divisible by count {count}")
    return table_size // count, first_payload


def parse_qm(path: Path) -> dict[str, object]:
    data = path.read_bytes()
    if len(data) < HEADER_SIZE:
        raise ValueError("QM file is too small")
    magic, version, count, reserved = struct.unpack_from("<4sIII", data, 0)
    if magic[:2] != b"QM":
        raise ValueError(f"unexpected QM magic: {magic!r}")

    entry_size, first_payload = infer_entry_size(data, count)
    if entry_size % 4 != 0:
        raise ValueError(f"entry size is not word-aligned: {entry_size}")

    records: list[dict[str, object]] = []
    slot_counts: dict[str, int] = {}
    duplicate_ids: dict[str, int] = {}
    seen_ids: set[int] = set()
    min_payload = len(data)
    max_payload = 0
    words_per_entry = entry_size // 4

    for index in range(count):
        entry_offset = HEADER_SIZE + index * entry_size
        words = struct.unpack_from(f"<{words_per_entry}I", data, entry_offset)
        text_id = words[0]
        if text_id in seen_ids:
            duplicate_ids[f"0x{text_id:04x}"] = duplicate_ids.get(f"0x{text_id:04x}", 1) + 1
        seen_ids.add(text_id)

        payloads: list[dict[str, int | str]] = []
        # In the EU container, payload offset/size pairs start at word 8.
        # Slot names are deliberately neutral until the
        # runtime loader confirms language ordering.
        for slot, word_index in enumerate(range(8, min(words_per_entry, 24), 2)):
            payload_offset = words[word_index]
            payload_size = words[word_index + 1]
            if payload_offset == 0 or payload_size == 0:
                continue
            if payload_offset + payload_size > len(data):
                continue
            slot_name = f"slot_{slot}"
            slot_counts[slot_name] = slot_counts.get(slot_name, 0) + 1
            min_payload = min(min_payload, payload_offset)
            max_payload = max(max_payload, payload_offset + payload_size)
            payloads.append(
                {
                    "slot": slot_name,
                    "offset": payload_offset,
                    "size": payload_size,
                }
            )

        records.append(
            {
                "index": index,
                "text_id": f"0x{text_id:04x}",
                "word_1": words[1],
                "word_2": words[2],
                "word_3": words[3],
                "payloads": payloads,
            }
        )

    return {
        "path": rel(path),
        "size": len(data),
        "magic_hex": magic.hex(),
        "version": version,
        "count": count,
        "reserved": reserved,
        "entry_size": entry_size,
        "first_payload_offset": first_payload,
        "payload_range": [min_payload if max_payload else None, max_payload or None],
        "unique_text_id_count": len(seen_ids),
        "text_ids": [f"0x{text_id:04x}" for text_id in sorted(seen_ids)],
        "duplicate_text_ids": duplicate_ids,
        "payload_slot_counts": dict(sorted(slot_counts.items())),
        "records_sample": records[:24],
    }


def make_markdown(report: dict[str, object]) -> str:
    lines = [
        "# QM Message Index",
        "",
        "Generated from `scripts/inspect_qm_messages.py`. Dialogue payload bytes are not exported.",
        "",
        f"- File: `{report['path']}`",
        f"- Size: {report['size']}",
        f"- Version: {report['version']}",
        f"- Records: {report['count']}",
        f"- Entry size: {report['entry_size']}",
        f"- First payload offset: `{int(report['first_payload_offset']):#x}`",
        f"- Unique text IDs: {report['unique_text_id_count']}",
        "",
        "## Payload Slots",
        "",
        "| Slot | Records with payload |",
        "| --- | ---: |",
    ]
    for slot, count in report["payload_slot_counts"].items():
        lines.append(f"| `{slot}` | {count} |")

    lines.extend(
        [
            "",
            "## Record Sample",
            "",
            "| Index | Text ID | w1 | w2 | w3 | Payloads |",
            "| ---: | --- | ---: | ---: | ---: | --- |",
        ]
    )
    for row in report["records_sample"]:
        payloads = ", ".join(f"{payload['slot']}@{payload['offset']:#x}+{payload['size']:#x}" for payload in row["payloads"]) or "-"
        lines.append(f"| {row['index']} | `{row['text_id']}` | {row['word_1']} | {row['word_2']} | {row['word_3']} | {payloads} |")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qm", type=Path, default=DEFAULT_QM)
    parser.add_argument("--out-json", type=Path, default=ANALYSIS / "qm_message_index.json")
    parser.add_argument("--out-md", type=Path, default=ANALYSIS / "qm_message_index.md")
    args = parser.parse_args()

    report = parse_qm(args.qm)
    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    args.out_md.write_text(make_markdown(report), encoding="utf-8")
    print(f"wrote {args.out_json}")
    print(f"wrote {args.out_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
