#!/usr/bin/env python3
"""Analyze OOT3D QM message payload byte streams without exporting dialogue."""

from __future__ import annotations

import argparse
import json
from collections import Counter, defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
DEFAULT_QM = ROOT.parent / "work" / "extract" / "romfs" / "message" / "eu" / "eu.qm"
DEFAULT_QM_INDEX = ANALYSIS / "qm_message_index.json"
CONTROL_PREFIX = 0x7F


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        try:
            return str(path.relative_to(ROOT.parent)).replace("\\", "/")
        except ValueError:
            return str(path).replace("\\", "/")


def utf8_prefix_length(data: bytes) -> int:
    length = 0
    i = 0
    while i < len(data):
        byte = data[i]
        if byte < 0x80:
            i += 1
            length += 1
            continue
        if 0xC2 <= byte <= 0xDF and i + 1 < len(data) and 0x80 <= data[i + 1] <= 0xBF:
            i += 2
            length += 2
            continue
        if (
            0xE0 <= byte <= 0xEF
            and i + 2 < len(data)
            and 0x80 <= data[i + 1] <= 0xBF
            and 0x80 <= data[i + 2] <= 0xBF
        ):
            i += 3
            length += 3
            continue
        if (
            0xF0 <= byte <= 0xF4
            and i + 3 < len(data)
            and 0x80 <= data[i + 1] <= 0xBF
            and 0x80 <= data[i + 2] <= 0xBF
            and 0x80 <= data[i + 3] <= 0xBF
        ):
            i += 4
            length += 4
            continue
        break
    return length


def classify_payload(data: bytes) -> dict[str, object]:
    text_bytes = 0
    control_bytes = 0
    control_ops: Counter[int] = Counter()
    leading_ops: list[str] = []
    ends_with_control_end = len(data) >= 2 and data[-2:] == bytes([CONTROL_PREFIX, 0x00])
    segments = 0
    invalid_bytes = 0
    i = 0
    while i < len(data):
        if data[i] == CONTROL_PREFIX and i + 1 < len(data):
            op = data[i + 1]
            control_ops[op] += 1
            if len(leading_ops) < 8:
                leading_ops.append(f"7f{op:02x}")
            control_bytes += 2
            i += 2
            continue

        run = utf8_prefix_length(data[i:])
        if run:
            text_bytes += run
            segments += 1
            i += run
            continue

        invalid_bytes += 1
        i += 1

    return {
        "size": len(data),
        "text_bytes": text_bytes,
        "control_bytes": control_bytes,
        "invalid_bytes": invalid_bytes,
        "text_ratio": round(text_bytes / len(data), 4) if data else 0,
        "control_ratio": round(control_bytes / len(data), 4) if data else 0,
        "segment_count": segments,
        "control_ops": {f"0x{op:02x}": count for op, count in sorted(control_ops.items())},
        "leading_ops": leading_ops,
        "ends_with_control_end": ends_with_control_end,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qm", type=Path, default=DEFAULT_QM)
    parser.add_argument("--qm-index", type=Path, default=DEFAULT_QM_INDEX)
    parser.add_argument("--out-json", type=Path, default=ANALYSIS / "qm_payload_analysis.json")
    parser.add_argument("--out-md", type=Path, default=ANALYSIS / "qm_payload_analysis.md")
    args = parser.parse_args()

    qm_bytes = args.qm.read_bytes()
    index = json.loads(args.qm_index.read_text(encoding="utf-8"))
    by_slot: dict[str, dict[str, object]] = defaultdict(
        lambda: {
            "payload_count": 0,
            "total_bytes": 0,
            "text_bytes": 0,
            "control_bytes": 0,
            "invalid_bytes": 0,
            "control_ops": Counter(),
            "leading_patterns": Counter(),
            "control_end_count": 0,
        }
    )
    samples: list[dict[str, object]] = []

    # The index stores full IDs but only sample payload offsets. Re-parse the
    # full record table from the indexed metadata so the aggregate covers every
    # payload while keeping this script independent from the table inference.
    entry_size = int(index["entry_size"])
    count = int(index["count"])
    table_base = 0x10
    for record_index in range(count):
        offset = table_base + record_index * entry_size
        words = [
            int.from_bytes(qm_bytes[offset + word * 4 : offset + word * 4 + 4], "little")
            for word in range(entry_size // 4)
        ]
        text_id = f"0x{words[0]:04x}"
        for slot, word_index in enumerate(range(8, min(len(words), 24), 2)):
            payload_offset = words[word_index]
            payload_size = words[word_index + 1]
            if payload_offset == 0 or payload_size == 0 or payload_offset + payload_size > len(qm_bytes):
                continue
            slot_name = f"slot_{slot}"
            data = qm_bytes[payload_offset : payload_offset + payload_size]
            classified = classify_payload(data)
            slot_row = by_slot[slot_name]
            slot_row["payload_count"] += 1
            slot_row["total_bytes"] += classified["size"]
            slot_row["text_bytes"] += classified["text_bytes"]
            slot_row["control_bytes"] += classified["control_bytes"]
            slot_row["invalid_bytes"] += classified["invalid_bytes"]
            slot_row["control_ops"].update({int(op, 16): count for op, count in classified["control_ops"].items()})
            slot_row["leading_patterns"].update([" ".join(classified["leading_ops"][:4])])
            if classified["ends_with_control_end"]:
                slot_row["control_end_count"] += 1
            if len(samples) < 32:
                samples.append({"text_id": text_id, "slot": slot_name, **classified})

    slot_reports: list[dict[str, object]] = []
    global_ops: Counter[int] = Counter()
    total_payloads = 0
    total_bytes = 0
    total_invalid = 0
    for slot, row in sorted(by_slot.items()):
        payload_count = int(row["payload_count"])
        slot_bytes = int(row["total_bytes"])
        global_ops.update(row["control_ops"])
        total_payloads += payload_count
        total_bytes += slot_bytes
        total_invalid += int(row["invalid_bytes"])
        slot_reports.append(
            {
                "slot": slot,
                "payload_count": payload_count,
                "total_bytes": slot_bytes,
                "text_bytes": row["text_bytes"],
                "control_bytes": row["control_bytes"],
                "invalid_bytes": row["invalid_bytes"],
                "text_ratio": round(row["text_bytes"] / slot_bytes, 4) if slot_bytes else 0,
                "control_ratio": round(row["control_bytes"] / slot_bytes, 4) if slot_bytes else 0,
                "top_control_ops": [
                    {"op": f"0x{op:02x}", "count": count}
                    for op, count in row["control_ops"].most_common(16)
                ],
                "top_leading_patterns": [
                    {"pattern": pattern, "count": count}
                    for pattern, count in row["leading_patterns"].most_common(8)
                ],
                "control_end_count": row["control_end_count"],
            }
        )

    report = {
        "qm": rel(args.qm),
        "payload_count": total_payloads,
        "payload_bytes": total_bytes,
        "invalid_bytes": total_invalid,
        "slots": slot_reports,
        "global_top_control_ops": [
            {"op": f"0x{op:02x}", "count": count}
            for op, count in global_ops.most_common(24)
        ],
        "samples": samples,
    }
    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    args.out_md.write_text(make_markdown(report), encoding="utf-8")
    print(f"wrote {args.out_json}")
    print(f"wrote {args.out_md}")
    return 0


def make_markdown(report: dict[str, object]) -> str:
    lines = [
        "# QM Payload Analysis",
        "",
        "Generated from `scripts/analyze_qm_payloads.py`. Dialogue text is not exported.",
        "",
        f"- File: `{report['qm']}`",
        f"- Payloads analyzed: {report['payload_count']}",
        f"- Payload bytes analyzed: {report['payload_bytes']}",
        f"- Invalid/unclassified bytes: {report['invalid_bytes']}",
        "",
        "## Slot Summary",
        "",
        "| Slot | Payloads | Bytes | Text ratio | Control ratio | Invalid bytes | Ends `7f00` | Top control ops |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |",
    ]
    for slot in report["slots"]:
        ops = ", ".join(f"{op['op']}:{op['count']}" for op in slot["top_control_ops"][:8]) or "-"
        lines.append(
            f"| `{slot['slot']}` | {slot['payload_count']} | {slot['total_bytes']} | {slot['text_ratio']} | {slot['control_ratio']} | {slot['invalid_bytes']} | {slot['control_end_count']} | {ops} |"
        )

    lines.extend(
        [
            "",
            "## Global Control Ops",
            "",
            "| Op | Count |",
            "| --- | ---: |",
        ]
    )
    for row in report["global_top_control_ops"]:
        lines.append(f"| `{row['op']}` | {row['count']} |")

    lines.extend(
        [
            "",
            "## Sample Stream Metadata",
            "",
            "| Text ID | Slot | Size | Text ratio | Control ratio | Leading ops | Ends `7f00` |",
            "| --- | --- | ---: | ---: | ---: | --- | ---: |",
        ]
    )
    for sample in report["samples"][:24]:
        lines.append(
            f"| `{sample['text_id']}` | `{sample['slot']}` | {sample['size']} | {sample['text_ratio']} | {sample['control_ratio']} | {', '.join(sample['leading_ops']) or '-'} | {sample['ends_with_control_end']} |"
        )
    lines.append("")
    return "\n".join(lines)


if __name__ == "__main__":
    raise SystemExit(main())
