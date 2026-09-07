#!/usr/bin/env python3
"""Decode native backing tables used by the title-intro primary producers."""

from __future__ import annotations

import csv
import json
import struct
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
WORDS = ANALYSIS / "title_intro_backing_table_words.csv"

OUT_JSON = ANALYSIS / "title_intro_backing_table_audit.json"
OUT_MD = ANALYSIS / "title_intro_backing_table_audit.md"

HISTORY_MODE_OTHER = "0054be0a"
HISTORY_MODE1 = "0054be12"
COMPACT_LOOKUP = "0054c212"
PATTERNS = "0054c222"
FALLBACK_ZERO_RUN = "0054c28f"
VECTOR_LOOKUP = "0054c2a0"
INPUT_VECTOR_BYTES = "0054ac96"
GLOBAL_FLAG_CONTEXT = "0054b5f2"

EXPECTED_COMPACT_LOOKUP_16 = [0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 5, 3, 3, 4, 4, 4]
EXPECTED_PATTERN_LENGTHS = [6, 8, 5, 6, 7, 6, 6, 6, 6, 6, 6, 6]


def read_csv_rows(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


def rel(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def byte_map() -> dict[str, bytes]:
    rows = read_csv_rows(WORDS)
    return {
        row.get("address", "").lower(): bytes.fromhex(row.get("bytes", "").replace(" ", ""))
        for row in rows
    }


def parse_history(data: bytes, count: int = 20) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for index in range(count):
        raw = data[index * 8 : index * 8 + 8]
        if len(raw) < 8:
            break
        rows.append(
            {
                "index": index,
                "raw": raw.hex(" "),
                "previous_slot": raw[0],
                "padding_or_unknown": raw[1],
                "duration_delta_s16": int.from_bytes(raw[2:4], "little", signed=True),
                "previous_x": raw[4],
                "previous_y": raw[5],
                "previous_z": raw[6],
                "source_flags_hi2": raw[7],
                "nonzero": any(raw),
                "sentinel": raw[0] == 0xFF,
            }
        )
    return rows


def parse_patterns(data: bytes, count: int = 12) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for index in range(count):
        raw = data[index * 9 : index * 9 + 9]
        if len(raw) < 9:
            break
        length = raw[0]
        rows.append(
            {
                "index": index,
                "raw": raw.hex(" "),
                "length": length,
                "sequence": list(raw[1 : 1 + min(length, 8)]),
                "padding": list(raw[1 + min(length, 8) :]),
                "valid_length": length <= 8,
            }
        )
    return rows


def parse_floats(data: bytes, count: int = 256) -> list[float]:
    values: list[float] = []
    for offset in range(0, min(len(data), count * 4), 4):
        if offset + 4 > len(data):
            break
        values.append(struct.unpack("<f", data[offset : offset + 4])[0])
    return values


def nonzero_history(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [row for row in rows if row["nonzero"] or row["sentinel"]]


def build_report() -> dict[str, Any]:
    data = byte_map()
    history_other = parse_history(data.get(HISTORY_MODE_OTHER, b""))
    history_mode1 = parse_history(data.get(HISTORY_MODE1, b""))
    compact_lookup = list(data.get(COMPACT_LOOKUP, b"")[:16])
    patterns = parse_patterns(data.get(PATTERNS, b""))
    fallback_run = list(data.get(FALLBACK_ZERO_RUN, b"")[:8])
    vector_values = parse_floats(data.get(VECTOR_LOOKUP, b""), 256)
    input_vector_prefix = list(data.get(INPUT_VECTOR_BYTES, b"")[:16])
    global_flag_records = parse_history(data.get(GLOBAL_FLAG_CONTEXT, b""), 8)
    checks = {
        "history_mode1_starts_with_slot2_duration3": bool(history_mode1)
        and history_mode1[0]["previous_slot"] == 2
        and history_mode1[0]["duration_delta_s16"] == 3,
        "history_mode1_has_sentinel_after_first_record": len(history_mode1) > 1
        and history_mode1[1]["sentinel"],
        "history_other_is_mode1_minus_one_record": len(history_other) > 1
        and history_other[1]["raw"] == history_mode1[0]["raw"],
        "compact_lookup_prefix_matches": compact_lookup == EXPECTED_COMPACT_LOOKUP_16,
        "pattern_lengths_match": [row["length"] for row in patterns] == EXPECTED_PATTERN_LENGTHS,
        "patterns_have_valid_lengths": all(row["valid_length"] for row in patterns),
        "fallback_zero_run_is_zero": fallback_run == [0] * 8,
        "vector_lookup_has_256_floats": len(vector_values) == 256,
        "vector_lookup_center_is_one": len(vector_values) > 128 and abs(vector_values[128] - 1.0) < 0.00001,
    }
    return {
        "format": "oot3d_title_intro_backing_table_audit_v1",
        "inputs": {
            "data_words": rel(WORDS),
        },
        "summary": {
            "ok": all(checks.values()),
            "checks": checks,
            "next_gate": "Promote these decoded tables into maintained OOT3D source/data definitions and connect them to the title QDB playback path.",
        },
        "tables": {
            "history_other_modes": {
                "address": "0x0054BE0A",
                "stride": 8,
                "nonzero_or_sentinel_records": nonzero_history(history_other),
            },
            "history_mode1": {
                "address": "0x0054BE12",
                "stride": 8,
                "nonzero_or_sentinel_records": nonzero_history(history_mode1),
            },
            "compact_lookup": {
                "address": "0x0054C212",
                "first_16": compact_lookup,
            },
            "patterns": {
                "address": "0x0054C222",
                "stride": 9,
                "records": patterns,
            },
            "fallback_zero_run": {
                "address": "0x0054C28F",
                "bytes": fallback_run,
            },
            "vector_lookup": {
                "address": "0x0054C2A0",
                "float_count": len(vector_values),
                "first": vector_values[0] if vector_values else None,
                "center": vector_values[128] if len(vector_values) > 128 else None,
                "last": vector_values[-1] if vector_values else None,
                "first_8": vector_values[:8],
            },
            "input_vector_prefix": {
                "address": "0x0054AC96",
                "bytes": input_vector_prefix,
            },
            "global_flag_context_probe": {
                "address": "0x0054B5F2",
                "first_history_like_records": global_flag_records,
                "note": "This is the base used for the +0x78C flag write, not the 0x002CFE34 pattern table.",
            },
        },
        "unresolved": [
            "The static table symbols still need promoted C definitions under decomp_support/src/include.",
            "The source path that selects or initializes these tables for spot00 title playback still needs to be connected to the QDB table rows.",
            "The dispatch helpers around 0x005B12E8 remain separate targets before executing the full title playback in-engine.",
        ],
    }


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    lines = [
        "# OOT3D Title Intro Backing Table Audit",
        "",
        "This audit decodes the native backing tables referenced by the primary title-intro producer functions. These bytes come from `code.bin` via Ghidra, not from emulator runtime capture.",
        "",
        "## Summary",
        "",
        f"- OK: {data['summary']['ok']}",
        f"- Next gate: {data['summary']['next_gate']}",
        "",
        "## Checks",
        "",
        "| Check | Status |",
        "| --- | --- |",
    ]
    for key, value in data["summary"]["checks"].items():
        lines.append(f"| `{key}` | `{value}` |")

    tables = data["tables"]
    lines.extend(
        [
            "",
            "## History Buffers",
            "",
            "| Table | Address | Index | Slot | Duration | XYZ | Flags | Raw |",
            "| --- | --- | ---: | ---: | ---: | --- | ---: | --- |",
        ]
    )
    for table_name in ("history_other_modes", "history_mode1"):
        table = tables[table_name]
        for row in table["nonzero_or_sentinel_records"]:
            xyz = f"{row['previous_x']},{row['previous_y']},{row['previous_z']}"
            lines.append(
                f"| `{table_name}` | `{table['address']}` | {row['index']} | {row['previous_slot']} | "
                f"{row['duration_delta_s16']} | `{xyz}` | {row['source_flags_hi2']} | `{row['raw']}` |"
            )

    lines.extend(
        [
            "",
            "## Compact Lookup",
            "",
            f"- Address: `{tables['compact_lookup']['address']}`",
            f"- First 16 values: `{', '.join(str(value) for value in tables['compact_lookup']['first_16'])}`",
            "",
            "## Pattern Records",
            "",
            "| Index | Length | Sequence | Raw |",
            "| ---: | ---: | --- | --- |",
        ]
    )
    for row in tables["patterns"]["records"]:
        lines.append(
            f"| {row['index']} | {row['length']} | `{', '.join(str(value) for value in row['sequence'])}` | `{row['raw']}` |"
        )

    vector = tables["vector_lookup"]
    lines.extend(
        [
            "",
            "## Vector Lookup",
            "",
            f"- Address: `{vector['address']}`",
            f"- Float count: `{vector['float_count']}`",
            f"- First: `{vector['first']}`",
            f"- Center index 128: `{vector['center']}`",
            f"- Last: `{vector['last']}`",
            f"- First 8: `{', '.join(f'{value:.6f}' for value in vector['first_8'])}`",
            "",
            "## Global Flag Context Probe",
            "",
            f"- Address: `{tables['global_flag_context_probe']['address']}`",
            f"- Note: {tables['global_flag_context_probe']['note']}",
            "",
            "## Unresolved",
            "",
        ]
    )
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
        raise SystemExit("title intro backing table audit failed")


if __name__ == "__main__":
    main()
