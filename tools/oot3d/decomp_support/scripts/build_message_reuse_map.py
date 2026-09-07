#!/usr/bin/env python3
"""Build a text-free reuse map between OOT3D QM records and N64 OoT messages."""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
DEFAULT_QM = ROOT.parent / "work" / "extract" / "romfs" / "message" / "eu" / "eu.qm"
DEFAULT_QM_INDEX = ANALYSIS / "qm_message_index.json"
DEFAULT_N64_TEXT_DIR = ROOT.parent / "external" / "oot" / "extracted" / "ntsc-1.2" / "text"

DEFINE_MESSAGE_RE = re.compile(
    r"\bDEFINE_MESSAGE(?:_[A-Z0-9]+)?\s*\(\s*(?P<id>0x[0-9A-Fa-f]+|\d+)\s*,\s*"
    r"(?P<type>[A-Z0-9_]+)\s*,\s*(?P<position>[A-Z0-9_]+)\s*,",
    re.MULTILINE,
)


def normalize_id(value: str) -> str:
    return f"0x{int(value, 16):04x}"


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        try:
            return str(path.relative_to(ROOT.parent)).replace("\\", "/")
        except ValueError:
            return str(path).replace("\\", "/")


def load_n64_metadata(text_dir: Path) -> dict[str, dict[str, str]]:
    rows: dict[str, dict[str, str]] = {}
    for path in [text_dir / "message_data.h", text_dir / "message_data_staff.h"]:
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for match in DEFINE_MESSAGE_RE.finditer(text):
            text_id = normalize_id(match.group("id"))
            rows[text_id] = {
                "n64_source": rel(path),
                "n64_textbox_type": match.group("type"),
                "n64_textbox_position": match.group("position"),
            }
    return rows


def load_qm_records(qm_path: Path, qm_index: Path) -> dict[str, dict[str, object]]:
    qm_bytes = qm_path.read_bytes()
    index = json.loads(qm_index.read_text(encoding="utf-8"))
    entry_size = int(index["entry_size"])
    record_count = int(index["count"])
    records: dict[str, dict[str, object]] = {}
    for record_index in range(record_count):
        offset = 0x10 + record_index * entry_size
        words = [
            int.from_bytes(qm_bytes[offset + word * 4 : offset + word * 4 + 4], "little")
            for word in range(entry_size // 4)
        ]
        text_id = f"0x{words[0]:04x}"
        payloads: list[dict[str, int | str]] = []
        for slot, word_index in enumerate(range(8, min(len(words), 24), 2)):
            payload_offset = words[word_index]
            payload_size = words[word_index + 1]
            if payload_offset == 0 or payload_size == 0 or payload_offset + payload_size > len(qm_bytes):
                continue
            payloads.append(
                {
                    "slot": f"slot_{slot}",
                    "offset": payload_offset,
                    "size": payload_size,
                }
            )
        records[text_id] = {
            "qm_record_index": record_index,
            "qm_word_1": words[1],
            "qm_word_2": words[2],
            "qm_word_3": words[3],
            "qm_payload_count": len(payloads),
            "qm_payload_sizes": {payload["slot"]: payload["size"] for payload in payloads},
        }
    return records


def make_markdown(report: dict[str, object]) -> str:
    lines = [
        "# Message Reuse Map",
        "",
        "Generated from `scripts/build_message_reuse_map.py`. Dialogue text is not exported.",
        "",
        f"- OOT3D QM records: {report['qm_record_count']}",
        f"- N64 extracted message IDs: {report['n64_message_count']}",
        f"- Shared IDs: {report['shared_count']}",
        f"- OOT3D-only IDs: {report['oot3d_only_count']}",
        f"- N64-only IDs: {report['n64_only_count']}",
        "",
        "## N64 Textbox Types In Shared IDs",
        "",
        "| Textbox type | Count |",
        "| --- | ---: |",
    ]
    for item in report["shared_textbox_type_counts"]:
        lines.append(f"| `{item['type']}` | {item['count']} |")

    lines.extend(
        [
            "",
            "## N64 Textbox Positions In Shared IDs",
            "",
            "| Position | Count |",
            "| --- | ---: |",
        ]
    )
    for item in report["shared_textbox_position_counts"]:
        lines.append(f"| `{item['position']}` | {item['count']} |")

    lines.extend(
        [
            "",
            "## Shared ID Sample",
            "",
            "| Text ID | N64 type | N64 position | QM record | QM payloads |",
            "| --- | --- | --- | ---: | ---: |",
        ]
    )
    for row in report["shared_sample"]:
        lines.append(
            f"| `{row['text_id']}` | `{row['n64_textbox_type']}` | `{row['n64_textbox_position']}` | {row['qm_record_index']} | {row['qm_payload_count']} |"
        )
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qm", type=Path, default=DEFAULT_QM)
    parser.add_argument("--qm-index", type=Path, default=DEFAULT_QM_INDEX)
    parser.add_argument("--n64-text-dir", type=Path, default=DEFAULT_N64_TEXT_DIR)
    parser.add_argument("--out-json", type=Path, default=ANALYSIS / "message_reuse_map.json")
    parser.add_argument("--out-md", type=Path, default=ANALYSIS / "message_reuse_map.md")
    args = parser.parse_args()

    qm_records = load_qm_records(args.qm, args.qm_index)
    n64_messages = load_n64_metadata(args.n64_text_dir)
    shared_ids = sorted(set(qm_records) & set(n64_messages), key=lambda value: int(value, 16))
    oot3d_only = sorted(set(qm_records) - set(n64_messages), key=lambda value: int(value, 16))
    n64_only = sorted(set(n64_messages) - set(qm_records), key=lambda value: int(value, 16))

    shared_rows: list[dict[str, object]] = []
    type_counts: Counter[str] = Counter()
    position_counts: Counter[str] = Counter()
    for text_id in shared_ids:
        row = {"text_id": text_id, **qm_records[text_id], **n64_messages[text_id]}
        shared_rows.append(row)
        type_counts[row["n64_textbox_type"]] += 1
        position_counts[row["n64_textbox_position"]] += 1

    report = {
        "qm": rel(args.qm),
        "n64_text_dir": rel(args.n64_text_dir),
        "qm_record_count": len(qm_records),
        "n64_message_count": len(n64_messages),
        "shared_count": len(shared_rows),
        "oot3d_only_count": len(oot3d_only),
        "n64_only_count": len(n64_only),
        "shared_textbox_type_counts": [
            {"type": key, "count": count}
            for key, count in type_counts.most_common()
        ],
        "shared_textbox_position_counts": [
            {"position": key, "count": count}
            for key, count in position_counts.most_common()
        ],
        "shared_sample": shared_rows[:80],
        "oot3d_only_sample": oot3d_only[:80],
        "n64_only_sample": n64_only[:80],
        "shared": shared_rows,
    }
    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    args.out_md.write_text(make_markdown(report), encoding="utf-8")
    print(f"wrote {args.out_json}")
    print(f"wrote {args.out_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
