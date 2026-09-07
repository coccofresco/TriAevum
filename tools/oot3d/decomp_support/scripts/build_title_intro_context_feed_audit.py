#!/usr/bin/env python3
"""Summarize native producers for the title-intro runtime context fields."""

from __future__ import annotations

import csv
import json
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
REFS = ANALYSIS / "title_intro_context_feed_references.csv"
PRODUCER_EXPORT = ANALYSIS / "title_intro_context_feed_producer_ghidra_export"
FUNCTIONS_CSV = PRODUCER_EXPORT / "functions_selected.csv"

OUT_JSON = ANALYSIS / "title_intro_context_feed_audit.json"
OUT_MD = ANALYSIS / "title_intro_context_feed_audit.md"

FIELD_MAP = {
    "0054ac5b": ("+0x13", "gate_latched_flag", "latched/toggled by gate helpers"),
    "0054ac5c": ("+0x14", "active_flag", "mode and gate active state"),
    "0054ac5d": ("+0x15", "global_gate_byte", "global gate selected by 0x003523DC"),
    "0054ac5e": ("+0x16", "current_slot_or_id", "compared by record-C progression"),
    "0054ac5f": ("+0x17", "previous_slot_or_id", "previous slot latch"),
    "0054ac60": ("+0x18", "record_source_byte", "masked to 0x3F for record A/C byte0"),
    "0054ac65": ("+0x1D", "record_b_byte1_source", "feeds record B byte1"),
    "0054ac66": ("+0x1E", "record_b_lookup_source", "feeds record B byte0 lookup"),
    "0054ac6c": ("+0x24", "mode_byte", "feeds record C byte1"),
    "0054ac6d": ("+0x25", "pending_flag", "mode gate pending state"),
    "0054ac6e": ("+0x26", "last_slot", "record-C progression comparison"),
    "0054ac6f": ("+0x27", "last_x", "record-C stability comparison"),
    "0054ac70": ("+0x28", "last_y", "record-C stability comparison"),
    "0054ac71": ("+0x29", "last_z", "record-C stability comparison"),
    "0054ac73": ("+0x2B", "countdown_override", "record A byte1 one-shot override"),
    "0054ac75": ("+0x2D", "dirty_flag", "slot/state changed marker"),
    "0054ac76": ("+0x2E", "sequence_stage", "sequence stage controller"),
    "0054ac77": ("+0x2F", "sequence_index", "sequence stage index"),
    "0054ac86": ("+0x3E", "record_cursor_count", "feeds record A/C byte2"),
    "0054ac8a": ("+0x42", "sequence_cursor", "sequence scan cursor"),
    "0054ac8c": ("+0x44", "record_b_slot_source", "feeds record B byte2"),
    "0054ac90": ("+0x48", "sequence_timer", "sequence stage timer"),
    "0054ac92": ("+0x4A", "latched_vector_x", "gate vector source"),
    "0054ac94": ("+0x4C", "latched_vector_y", "gate vector source"),
    "0054ac9d": ("+0x55", "record_a_byte0", "record A table entry"),
    "0054aca0": ("+0x58", "record_b_byte0", "record B table entry"),
    "0054aca1": ("+0x59", "record_b_byte1", "record B completion gate"),
    "0054aca3": ("+0x5B", "record_c_byte0", "record C consumed by state 37"),
    "0054acdc": ("+0x94", "runtime_flags_or_request", "gate and request flags"),
    "0054ace0": ("+0x98", "sequence_state_word", "sequence state word"),
    "0054acec": ("+0xA4", "last_d8_snapshot", "record update snapshot"),
    "0054acf0": ("+0xA8", "sequence_source_pointer", "selected sequence source"),
    "0054acf4": ("+0xAC", "last_progress_time", "record-C delta gate lower bound"),
    "0054ad04": ("+0xBC", "time_counter", "per-frame counter source"),
    "0054ad1c": ("+0xD4", "gate_saved_word_source", "gate saved word source"),
    "0054ad20": ("+0xD8", "current_progress_time", "record-C delta gate upper bound"),
    "0054ad24": ("+0xDC", "gate_current_word", "gate current word"),
    "0054ad2c": ("+0xE4", "gate_previous_word", "gate previous word"),
}

REQUIRED_WRITERS = {
    "0054ac60": {"002d6798"},
    "0054ac6c": {"002d0264"},
    "0054ac86": {"002d0264", "00476d44"},
    "0054aca3": {"00476d44", "00477c90"},
    "0054acf4": {"002d0264", "00476d44"},
    "0054ad20": {"00460878"},
}

REQUIRED_EXPORTS = {
    "002cfe34",
    "002d0264",
    "002d6798",
    "0033f248",
    "003523dc",
    "00460878",
    "00476d44",
    "00477a1c",
    "00477c90",
    "00493328",
}


def rel(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def read_csv_rows(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


def field_rows(refs: list[dict[str, str]]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    by_target: dict[str, list[dict[str, str]]] = {}
    for ref in refs:
        by_target.setdefault(ref.get("target", "").lower(), []).append(ref)
    for target, (offset, name, feed) in FIELD_MAP.items():
        refs_for_target = by_target.get(target, [])
        write_entries = sorted(
            {
                row.get("from_function_entry", "").lower()
                for row in refs_for_target
                if row.get("reference_type", "").upper() == "WRITE"
            }
        )
        read_entries = sorted(
            {
                row.get("from_function_entry", "").lower()
                for row in refs_for_target
                if row.get("reference_type", "").upper() == "READ"
            }
        )
        rows.append(
            {
                "target": target,
                "offset": offset,
                "name": name,
                "feed": feed,
                "ref_count": len(refs_for_target),
                "write_entries": write_entries,
                "read_entries": read_entries,
                "refs": refs_for_target,
            }
        )
    return rows


def build_report() -> dict[str, Any]:
    refs = read_csv_rows(REFS)
    functions = read_csv_rows(FUNCTIONS_CSV)
    exported = {row.get("entry", "").lower() for row in functions}
    rows = field_rows(refs)
    by_type = Counter(row.get("reference_type", "") for row in refs)
    by_function = Counter(row.get("from_function_entry", "").lower() for row in refs)
    field_by_target = {row["target"]: row for row in rows}
    required_writer_checks = {
        target: REQUIRED_WRITERS[target].issubset(set(field_by_target.get(target, {}).get("write_entries", [])))
        for target in REQUIRED_WRITERS
    }
    checks = {
        "references_present": len(refs) > 0,
        "producer_exports_present": REQUIRED_EXPORTS.issubset(exported),
        "required_record_c_feeds_have_writers": all(required_writer_checks.values()),
    }
    return {
        "format": "oot3d_title_intro_context_feed_audit_v1",
        "inputs": {
            "references": rel(REFS),
            "producer_export": rel(PRODUCER_EXPORT),
        },
        "summary": {
            "ok": all(checks.values()),
            "checks": checks,
            "reference_count": len(refs),
            "reference_types": dict(by_type),
            "producer_count": len(exported),
            "top_reference_functions": [
                {"entry": entry, "count": count}
                for entry, count in by_function.most_common(12)
            ],
            "next_gate": "Use title_intro_primary_producer_audit to decode the sequence/history backing tables at 0x0054BE12/0x0054BE0A, derived pattern windows at 0x0054C212/0x0054C222/0x0054C28F, and vector lookup at 0x0054C2A0.",
        },
        "required_writer_checks": required_writer_checks,
        "fields": rows,
        "producer_exports": sorted(exported),
        "unresolved": [
            "0x002D6798 and 0x002CFE34 are typed by title_intro_primary_producer_audit; their backing tables still need data export.",
            "0x002D0264 still drives mode/pending/last-value fields around +0x24..+0x3E and should remain linked to the primary producer audit.",
            "0x00477A1C and 0x0033F248 feed record B and sequence state; they are secondary for record C but needed for full title cue playback.",
        ],
    }


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    lines = [
        "# OOT3D Title Intro Context Feed Audit",
        "",
        "This audit groups native cross-references to the runtime context at `0x0054AC48`, focusing on fields that feed the three-byte record consumed by title state 37.",
        "",
        "## Summary",
        "",
        f"- OK: {data['summary']['ok']}",
        f"- References: {data['summary']['reference_count']}",
        f"- Producer exports: {data['summary']['producer_count']}",
        f"- Reference types: {', '.join(f'{k}={v}' for k, v in data['summary']['reference_types'].items())}",
        f"- Next gate: {data['summary']['next_gate']}",
        "",
        "## Checks",
        "",
        "| Check | Status |",
        "| --- | --- |",
    ]
    for key, value in data["summary"]["checks"].items():
        lines.append(f"| `{key}` | `{value}` |")

    lines.extend(
        [
            "",
            "## Required Writer Checks",
            "",
            "| Target | Required writers found |",
            "| --- | --- |",
        ]
    )
    for target, ok in data["required_writer_checks"].items():
        required = ", ".join(f"`{entry}`" for entry in sorted(REQUIRED_WRITERS[target]))
        lines.append(f"| `{target}` {FIELD_MAP[target][0]} | `{ok}` ({required}) |")

    lines.extend(
        [
            "",
            "## Context Fields",
            "",
            "| Offset | Target | Field | Feed | Writers | Readers |",
            "| --- | --- | --- | --- | --- | --- |",
        ]
    )
    for row in data["fields"]:
        writers = ", ".join(f"`{entry}`" for entry in row["write_entries"]) or "-"
        readers = ", ".join(f"`{entry}`" for entry in row["read_entries"][:8]) or "-"
        if len(row["read_entries"]) > 8:
            readers += f", +{len(row['read_entries']) - 8}"
        lines.append(
            f"| `{row['offset']}` | `{row['target']}` | `{row['name']}` | {row['feed']} | {writers} | {readers} |"
        )

    lines.extend(
        [
            "",
            "## Top Reference Functions",
            "",
            "| Entry | References |",
            "| --- | ---: |",
        ]
    )
    for row in data["summary"]["top_reference_functions"]:
        lines.append(f"| `{row['entry']}` | {row['count']} |")

    lines.extend(["", "## Unresolved", ""])
    for item in data["unresolved"]:
        lines.append(f"- {item}")
    lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main() -> None:
    data = build_report()
    write_json(OUT_JSON, data)
    write_markdown(OUT_MD, data)
    if not data["summary"]["ok"]:
        raise SystemExit("title intro context feed audit failed")


if __name__ == "__main__":
    main()
