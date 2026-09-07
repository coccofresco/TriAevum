#!/usr/bin/env python3
"""Promote decoded title-intro backing tables into maintained C data."""

from __future__ import annotations

import json
import struct
from pathlib import Path
from typing import Any

from build_title_intro_backing_table_audit import (
    COMPACT_LOOKUP,
    FALLBACK_ZERO_RUN,
    GLOBAL_FLAG_CONTEXT,
    HISTORY_MODE1,
    HISTORY_MODE_OTHER,
    INPUT_VECTOR_BYTES,
    PATTERNS,
    VECTOR_LOOKUP,
    byte_map,
    parse_history,
    parse_patterns,
)


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
OUT_HEADER = ROOT / "include" / "oot3d" / "title_intro_runtime_tables.h"
OUT_SOURCE = ROOT / "src" / "code" / "z_title_intro_runtime_tables.c"
OUT_JSON = ANALYSIS / "title_intro_runtime_tables.json"
OUT_MD = ANALYSIS / "title_intro_runtime_tables.md"
DEFERRED_TARGET_WORDS = ANALYSIS / "title_intro_deferred_table_target_words.csv"

RUNTIME_CONTEXT = 0x0054AC48
RECORD_TABLE = 0x0054AC9D
RECORD_C = 0x0054ACA3
DEFERRED_SEQUENCE_TABLE = 0x0054B5F2
DEFERRED_SEQUENCE_STRIDE = 0xA0
DEFERRED_SEQUENCE_LANE_COUNT = 15
DEFERRED_SEQUENCE_ROWS_PER_LANE = DEFERRED_SEQUENCE_STRIDE // 8
DEFERRED_SEQUENCE_DEFAULT_LANE = 14


def byte_map_from_csv(path: Path) -> dict[str, bytes]:
    import csv

    if not path.is_file():
        return {}
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return {
            row.get("address", "").lower(): bytes.fromhex(row.get("bytes", "").replace(" ", ""))
            for row in csv.DictReader(handle)
        }


def rel(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def records_until_sentinel(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for row in rows:
        result.append(row)
        if row["sentinel"]:
            break
    return result


def parse_vector_bits(data: bytes, count: int = 256) -> list[int]:
    bits: list[int] = []
    for offset in range(0, min(len(data), count * 4), 4):
        if offset + 4 > len(data):
            break
        bits.append(int.from_bytes(data[offset : offset + 4], "little"))
    return bits


def parse_sequence_row(raw: bytes) -> dict[str, Any]:
    return {
        "source_byte": int.from_bytes(raw[0:1], "little", signed=True),
        "unused_or_padding": raw[1],
        "duration": int.from_bytes(raw[2:4], "little"),
        "scale_byte": raw[4],
        "dispatch_byte": int.from_bytes(raw[5:6], "little", signed=True),
        "vector_y": int.from_bytes(raw[6:7], "little", signed=True),
        "alternate_source_byte": int.from_bytes(raw[7:8], "little", signed=True),
        "raw": raw.hex(" "),
        "sentinel": raw[0] == 0xFF,
    }


def parse_deferred_sequence_lanes(data: bytes) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    rows: list[dict[str, Any]] = []
    lanes: list[dict[str, Any]] = []
    for lane_index in range(DEFERRED_SEQUENCE_LANE_COUNT):
        lane_offset = lane_index * DEFERRED_SEQUENCE_STRIDE
        lane_rows: list[dict[str, Any]] = []
        for row_index in range(DEFERRED_SEQUENCE_ROWS_PER_LANE):
            start = lane_offset + row_index * 8
            raw = data[start : start + 8]
            if len(raw) < 8:
                break
            row = parse_sequence_row(raw)
            row["lane_index"] = lane_index
            row["row_index"] = row_index
            lane_rows.append(row)
            if row["sentinel"]:
                break
        row_start = len(rows)
        rows.extend(lane_rows)
        lanes.append(
            {
                "lane_index": lane_index,
                "native_address": DEFERRED_SEQUENCE_TABLE + lane_offset,
                "param1_value": lane_index + 1,
                "default_for_param_ge_15": lane_index == DEFERRED_SEQUENCE_DEFAULT_LANE,
                "row_start": row_start,
                "row_count": len(lane_rows),
                "sentinel_terminated": bool(lane_rows) and lane_rows[-1]["sentinel"],
            }
        )
    return rows, lanes


def f32_value(bits: int) -> float:
    return struct.unpack("<f", bits.to_bytes(4, "little"))[0]


def format_float(value: float) -> str:
    return value.hex() + "f"


def c_u8_array(values: list[int], indent: str = "    ") -> str:
    if not values:
        return indent + "0u"
    lines: list[str] = []
    for offset in range(0, len(values), 16):
        chunk = values[offset : offset + 16]
        lines.append(indent + ", ".join(f"{value}u" for value in chunk))
    return ",\n".join(lines)


def c_u32_array(values: list[int], indent: str = "    ") -> str:
    lines: list[str] = []
    for offset in range(0, len(values), 8):
        chunk = values[offset : offset + 8]
        lines.append(indent + ", ".join(f"0x{value:08X}u" for value in chunk))
    return ",\n".join(lines)


def build_tables() -> dict[str, Any]:
    data = byte_map()
    deferred_data = byte_map_from_csv(DEFERRED_TARGET_WORDS)
    history_other = records_until_sentinel(parse_history(data.get(HISTORY_MODE_OTHER, b"")))
    history_mode1 = records_until_sentinel(parse_history(data.get(HISTORY_MODE1, b"")))
    compact_lookup = list(data.get(COMPACT_LOOKUP, b"")[:16])
    patterns = parse_patterns(data.get(PATTERNS, b""))
    fallback_zero_run = list(data.get(FALLBACK_ZERO_RUN, b"")[:8])
    vector_bits = parse_vector_bits(data.get(VECTOR_LOOKUP, b""), 256)
    input_vector_prefix = list(data.get(INPUT_VECTOR_BYTES, b"")[:16])
    global_flag_prefix = list(data.get(GLOBAL_FLAG_CONTEXT, b"")[:32])
    deferred_sequence_rows, deferred_sequence_lanes = parse_deferred_sequence_lanes(
        deferred_data.get(f"{DEFERRED_SEQUENCE_TABLE:08x}", b"")
    )
    runtime_context = deferred_data.get(f"{RUNTIME_CONTEXT:08x}", b"")
    runtime_sequence_ptr = int.from_bytes(runtime_context[0xA8:0xAC], "little") if len(runtime_context) >= 0xAC else 0
    runtime_default_sequence_ptr = int.from_bytes(runtime_context[0xB0:0xB4], "little") if len(runtime_context) >= 0xB4 else 0
    runtime_mode1_history_ptr = int.from_bytes(runtime_context[0xB4:0xB8], "little") if len(runtime_context) >= 0xB8 else 0

    checks = {
        "history_other_records_include_leading_zero_and_sentinel": len(history_other) == 3
        and history_other[0]["raw"] == "00 00 00 00 00 00 00 00"
        and history_other[2]["sentinel"],
        "history_mode1_records_include_seed_and_sentinel": len(history_mode1) == 2
        and history_mode1[0]["previous_slot"] == 2
        and history_mode1[1]["sentinel"],
        "compact_lookup_count": len(compact_lookup) == 16,
        "pattern_count": len(patterns) == 12,
        "fallback_zero_run_count": len(fallback_zero_run) == 8,
        "vector_lookup_bit_count": len(vector_bits) == 256,
        "input_vector_prefix_count": len(input_vector_prefix) == 16,
        "global_flag_probe_prefix_count": len(global_flag_prefix) == 32,
        "deferred_sequence_lane_count": len(deferred_sequence_lanes) == DEFERRED_SEQUENCE_LANE_COUNT,
        "deferred_sequence_row_count": len(deferred_sequence_rows) == 81,
        "deferred_sequence_lanes_are_sentinel_terminated": all(
            lane["sentinel_terminated"] for lane in deferred_sequence_lanes
        ),
        "runtime_context_sequence_pointer_matches_base": runtime_sequence_ptr == DEFERRED_SEQUENCE_TABLE,
        "runtime_context_default_sequence_pointer_matches_lane14": runtime_default_sequence_ptr
        == DEFERRED_SEQUENCE_TABLE + DEFERRED_SEQUENCE_DEFAULT_LANE * DEFERRED_SEQUENCE_STRIDE,
        "runtime_context_mode1_history_pointer_matches": runtime_mode1_history_ptr == int(HISTORY_MODE1, 16),
    }
    return {
        "format": "oot3d_title_intro_runtime_tables_v1",
        "inputs": {
            "backing_table_words": rel(ANALYSIS / "title_intro_backing_table_words.csv"),
            "backing_table_audit": rel(ANALYSIS / "title_intro_backing_table_audit.json"),
            "deferred_table_target_words": rel(DEFERRED_TARGET_WORDS),
        },
        "summary": {
            "ok": all(checks.values()),
            "checks": checks,
            "header": rel(OUT_HEADER),
            "source": rel(OUT_SOURCE),
        },
        "native_addresses": {
            "runtime_context": f"0x{RUNTIME_CONTEXT:08X}",
            "record_table": f"0x{RECORD_TABLE:08X}",
            "record_c": f"0x{RECORD_C:08X}",
            "history_other_modes": "0x0054BE0A",
            "history_mode1": "0x0054BE12",
            "compact_lookup": "0x0054C212",
            "pattern_records": "0x0054C222",
            "fallback_zero_run": "0x0054C28F",
            "vector_lookup": "0x0054C2A0",
            "input_vector_prefix": "0x0054AC96",
            "global_flag_context_probe": "0x0054B5F2",
            "deferred_sequence_table": f"0x{DEFERRED_SEQUENCE_TABLE:08X}",
        },
        "tables": {
            "history_other_modes": history_other,
            "history_mode1": history_mode1,
            "compact_lookup": compact_lookup,
            "pattern_records": patterns,
            "fallback_zero_run": fallback_zero_run,
            "vector_lookup_bits": vector_bits,
            "input_vector_prefix": input_vector_prefix,
            "global_flag_context_probe_prefix": global_flag_prefix,
            "deferred_sequence_rows": deferred_sequence_rows,
            "deferred_sequence_lanes": deferred_sequence_lanes,
            "runtime_context_sequence_pointers": {
                "sequence_source": f"0x{runtime_sequence_ptr:08X}",
                "default_sequence_source": f"0x{runtime_default_sequence_ptr:08X}",
                "mode1_history_source": f"0x{runtime_mode1_history_ptr:08X}",
            },
        },
    }


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def write_json(path: Path, data: Any) -> None:
    write_text(path, json.dumps(data, indent=2) + "\n")


def history_initializer(rows: list[dict[str, Any]]) -> str:
    lines = []
    for row in rows:
        lines.append(
            "    { "
            f"{row['previous_slot']}u, {row['padding_or_unknown']}u, {row['duration_delta_s16']}, "
            f"{row['previous_x']}u, {row['previous_y']}u, {row['previous_z']}u, {row['source_flags_hi2']}u"
            " },"
        )
    return "\n".join(lines)


def pattern_initializer(rows: list[dict[str, Any]]) -> str:
    lines = []
    for row in rows:
        values = list(row["sequence"])
        values.extend([0] * (8 - len(values)))
        lines.append(
            "    { "
            f"{row['length']}u, {{ {', '.join(f'{value}u' for value in values)} }}"
            " },"
        )
    return "\n".join(lines)


def sequence_initializer(rows: list[dict[str, Any]]) -> str:
    lines = []
    for row in rows:
        lines.append(
            "    { "
            f"{row['source_byte']}, {row['unused_or_padding']}u, {row['duration']}u, "
            f"{row['scale_byte']}u, {row['dispatch_byte']}, {row['vector_y']}, "
            f"{row['alternate_source_byte']}"
            " },"
        )
    return "\n".join(lines)


def sequence_lane_initializer(rows: list[dict[str, Any]]) -> str:
    lines = []
    for row in rows:
        lines.append(
            "    { "
            f"{row['lane_index']}u, {row['param1_value']}u, "
            f"{1 if row['default_for_param_ge_15'] else 0}u, 0u, "
            f"{row['row_start']}u, {row['row_count']}u, "
            f"0x{row['native_address']:08X}u"
            " },"
        )
    return "\n".join(lines)


def write_header(path: Path) -> None:
    text = """#pragma once

#include <stdint.h>

#define OOT3D_TITLE_INTRO_RUNTIME_CONTEXT_ADDR 0x0054AC48u
#define OOT3D_TITLE_INTRO_RECORD_TABLE_ADDR 0x0054AC9Du
#define OOT3D_TITLE_INTRO_RECORD_C_ADDR 0x0054ACA3u
#define OOT3D_TITLE_INTRO_HISTORY_OTHER_MODES_ADDR 0x0054BE0Au
#define OOT3D_TITLE_INTRO_HISTORY_MODE1_ADDR 0x0054BE12u
#define OOT3D_TITLE_INTRO_COMPACT_LOOKUP_ADDR 0x0054C212u
#define OOT3D_TITLE_INTRO_PATTERN_RECORDS_ADDR 0x0054C222u
#define OOT3D_TITLE_INTRO_FALLBACK_ZERO_RUN_ADDR 0x0054C28Fu
#define OOT3D_TITLE_INTRO_VECTOR_LOOKUP_ADDR 0x0054C2A0u
#define OOT3D_TITLE_INTRO_INPUT_VECTOR_PREFIX_ADDR 0x0054AC96u
#define OOT3D_TITLE_INTRO_GLOBAL_FLAG_CONTEXT_PROBE_ADDR 0x0054B5F2u
#define OOT3D_TITLE_INTRO_DEFERRED_SEQUENCE_TABLE_ADDR 0x0054B5F2u
#define OOT3D_TITLE_INTRO_DEFERRED_SEQUENCE_TABLE_STRIDE 0xA0u

#define OOT3D_TITLE_INTRO_COMPACT_LOOKUP_COUNT 16u
#define OOT3D_TITLE_INTRO_PATTERN_RECORD_COUNT 12u
#define OOT3D_TITLE_INTRO_FALLBACK_ZERO_RUN_COUNT 8u
#define OOT3D_TITLE_INTRO_VECTOR_LOOKUP_COUNT 256u
#define OOT3D_TITLE_INTRO_INPUT_VECTOR_PREFIX_COUNT 16u
#define OOT3D_TITLE_INTRO_GLOBAL_FLAG_CONTEXT_PROBE_PREFIX_COUNT 32u
#define OOT3D_TITLE_INTRO_DEFERRED_SEQUENCE_LANE_COUNT 15u
#define OOT3D_TITLE_INTRO_DEFERRED_SEQUENCE_ROW_COUNT 81u
#define OOT3D_TITLE_INTRO_DEFERRED_SEQUENCE_DEFAULT_LANE 14u

typedef struct Oot3dTitleIntroHistoryRecord {
    uint8_t previousSlot;
    uint8_t paddingOrUnknown;
    int16_t durationDelta;
    uint8_t previousX;
    uint8_t previousY;
    uint8_t previousZ;
    uint8_t sourceFlagsHi2;
} Oot3dTitleIntroHistoryRecord;

typedef struct Oot3dTitleIntroPatternRecord {
    uint8_t length;
    uint8_t values[8];
} Oot3dTitleIntroPatternRecord;

typedef struct Oot3dTitleIntroSequenceRow {
    int8_t sourceByte;
    uint8_t unusedOrPadding;
    uint16_t duration;
    uint8_t scaleByte;
    int8_t dispatchByte;
    int8_t vectorY;
    int8_t alternateSourceByte;
} Oot3dTitleIntroSequenceRow;

typedef struct Oot3dTitleIntroSequenceLane {
    uint8_t laneIndex;
    uint8_t param1Value;
    uint8_t defaultForParamGe15;
    uint8_t reserved;
    uint16_t rowStart;
    uint16_t rowCount;
    uint32_t nativeAddress;
} Oot3dTitleIntroSequenceLane;

typedef struct Oot3dTitleIntroRuntimeTableMapRow {
    const char* symbol;
    uint32_t nativeAddress;
    uint32_t byteSize;
    const char* source;
} Oot3dTitleIntroRuntimeTableMapRow;

extern const Oot3dTitleIntroHistoryRecord gOot3dTitleIntroHistoryOtherModeRecords[];
extern const uint32_t gOot3dTitleIntroHistoryOtherModeRecordCount;
extern const Oot3dTitleIntroHistoryRecord gOot3dTitleIntroHistoryMode1Records[];
extern const uint32_t gOot3dTitleIntroHistoryMode1RecordCount;
extern const uint8_t gOot3dTitleIntroCompactLookup[OOT3D_TITLE_INTRO_COMPACT_LOOKUP_COUNT];
extern const Oot3dTitleIntroPatternRecord gOot3dTitleIntroPatternRecords[OOT3D_TITLE_INTRO_PATTERN_RECORD_COUNT];
extern const uint8_t gOot3dTitleIntroFallbackZeroRun[OOT3D_TITLE_INTRO_FALLBACK_ZERO_RUN_COUNT];
extern const uint32_t gOot3dTitleIntroVectorLookupBits[OOT3D_TITLE_INTRO_VECTOR_LOOKUP_COUNT];
extern const uint8_t gOot3dTitleIntroInputVectorPrefix[OOT3D_TITLE_INTRO_INPUT_VECTOR_PREFIX_COUNT];
extern const uint8_t gOot3dTitleIntroGlobalFlagContextProbePrefix[OOT3D_TITLE_INTRO_GLOBAL_FLAG_CONTEXT_PROBE_PREFIX_COUNT];
extern const Oot3dTitleIntroSequenceRow gOot3dTitleIntroDeferredSequenceRows[OOT3D_TITLE_INTRO_DEFERRED_SEQUENCE_ROW_COUNT];
extern const Oot3dTitleIntroSequenceLane gOot3dTitleIntroDeferredSequenceLanes[OOT3D_TITLE_INTRO_DEFERRED_SEQUENCE_LANE_COUNT];
extern const Oot3dTitleIntroRuntimeTableMapRow gOot3dTitleIntroRuntimeTableMapRows[];
extern const uint32_t gOot3dTitleIntroRuntimeTableMapRowCount;
"""
    write_text(path, text)


def write_source(path: Path, data: dict[str, Any]) -> None:
    tables = data["tables"]
    vector_values = [f32_value(bits) for bits in tables["vector_lookup_bits"]]
    source = f"""/* Generated by build_title_intro_runtime_tables.py.
 * Source evidence: code.bin table bytes exported by Ghidra and audited in title_intro_backing_table_audit.
 */

#include "oot3d/title_intro_runtime_tables.h"

const Oot3dTitleIntroHistoryRecord gOot3dTitleIntroHistoryOtherModeRecords[] = {{
{history_initializer(tables["history_other_modes"])}
}};
const uint32_t gOot3dTitleIntroHistoryOtherModeRecordCount = sizeof(gOot3dTitleIntroHistoryOtherModeRecords) / sizeof(gOot3dTitleIntroHistoryOtherModeRecords[0]);

const Oot3dTitleIntroHistoryRecord gOot3dTitleIntroHistoryMode1Records[] = {{
{history_initializer(tables["history_mode1"])}
}};
const uint32_t gOot3dTitleIntroHistoryMode1RecordCount = sizeof(gOot3dTitleIntroHistoryMode1Records) / sizeof(gOot3dTitleIntroHistoryMode1Records[0]);

const uint8_t gOot3dTitleIntroCompactLookup[OOT3D_TITLE_INTRO_COMPACT_LOOKUP_COUNT] = {{
{c_u8_array(tables["compact_lookup"])}
}};

const Oot3dTitleIntroPatternRecord gOot3dTitleIntroPatternRecords[OOT3D_TITLE_INTRO_PATTERN_RECORD_COUNT] = {{
{pattern_initializer(tables["pattern_records"])}
}};

const uint8_t gOot3dTitleIntroFallbackZeroRun[OOT3D_TITLE_INTRO_FALLBACK_ZERO_RUN_COUNT] = {{
{c_u8_array(tables["fallback_zero_run"])}
}};

const uint32_t gOot3dTitleIntroVectorLookupBits[OOT3D_TITLE_INTRO_VECTOR_LOOKUP_COUNT] = {{
{c_u32_array(tables["vector_lookup_bits"])}
}};

const uint8_t gOot3dTitleIntroInputVectorPrefix[OOT3D_TITLE_INTRO_INPUT_VECTOR_PREFIX_COUNT] = {{
{c_u8_array(tables["input_vector_prefix"])}
}};

const uint8_t gOot3dTitleIntroGlobalFlagContextProbePrefix[OOT3D_TITLE_INTRO_GLOBAL_FLAG_CONTEXT_PROBE_PREFIX_COUNT] = {{
{c_u8_array(tables["global_flag_context_probe_prefix"])}
}};

const Oot3dTitleIntroSequenceRow gOot3dTitleIntroDeferredSequenceRows[OOT3D_TITLE_INTRO_DEFERRED_SEQUENCE_ROW_COUNT] = {{
{sequence_initializer(tables["deferred_sequence_rows"])}
}};

const Oot3dTitleIntroSequenceLane gOot3dTitleIntroDeferredSequenceLanes[OOT3D_TITLE_INTRO_DEFERRED_SEQUENCE_LANE_COUNT] = {{
{sequence_lane_initializer(tables["deferred_sequence_lanes"])}
}};

const Oot3dTitleIntroRuntimeTableMapRow gOot3dTitleIntroRuntimeTableMapRows[] = {{
    {{ "gOot3dTitleIntroHistoryOtherModeRecords", OOT3D_TITLE_INTRO_HISTORY_OTHER_MODES_ADDR, sizeof(gOot3dTitleIntroHistoryOtherModeRecords), "code.bin:0x0054BE0A" }},
    {{ "gOot3dTitleIntroHistoryMode1Records", OOT3D_TITLE_INTRO_HISTORY_MODE1_ADDR, sizeof(gOot3dTitleIntroHistoryMode1Records), "code.bin:0x0054BE12" }},
    {{ "gOot3dTitleIntroCompactLookup", OOT3D_TITLE_INTRO_COMPACT_LOOKUP_ADDR, sizeof(gOot3dTitleIntroCompactLookup), "code.bin:0x0054C212" }},
    {{ "gOot3dTitleIntroPatternRecords", OOT3D_TITLE_INTRO_PATTERN_RECORDS_ADDR, sizeof(gOot3dTitleIntroPatternRecords), "code.bin:0x0054C222" }},
    {{ "gOot3dTitleIntroFallbackZeroRun", OOT3D_TITLE_INTRO_FALLBACK_ZERO_RUN_ADDR, sizeof(gOot3dTitleIntroFallbackZeroRun), "code.bin:0x0054C28F" }},
    {{ "gOot3dTitleIntroVectorLookupBits", OOT3D_TITLE_INTRO_VECTOR_LOOKUP_ADDR, sizeof(gOot3dTitleIntroVectorLookupBits), "code.bin:0x0054C2A0" }},
    {{ "gOot3dTitleIntroInputVectorPrefix", OOT3D_TITLE_INTRO_INPUT_VECTOR_PREFIX_ADDR, sizeof(gOot3dTitleIntroInputVectorPrefix), "code.bin:0x0054AC96" }},
    {{ "gOot3dTitleIntroGlobalFlagContextProbePrefix", OOT3D_TITLE_INTRO_GLOBAL_FLAG_CONTEXT_PROBE_ADDR, sizeof(gOot3dTitleIntroGlobalFlagContextProbePrefix), "code.bin:0x0054B5F2" }},
    {{ "gOot3dTitleIntroDeferredSequenceRows", OOT3D_TITLE_INTRO_DEFERRED_SEQUENCE_TABLE_ADDR, sizeof(gOot3dTitleIntroDeferredSequenceRows), "code.bin:0x0054B5F2" }},
}};
const uint32_t gOot3dTitleIntroRuntimeTableMapRowCount = sizeof(gOot3dTitleIntroRuntimeTableMapRows) / sizeof(gOot3dTitleIntroRuntimeTableMapRows[0]);

/* Exact f32 values represented by gOot3dTitleIntroVectorLookupBits:
 * first={format_float(vector_values[0])}, center={format_float(vector_values[128])}, last={format_float(vector_values[-1])}
 */
"""
    write_text(path, source)


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# OOT3D Title Intro Runtime Tables",
        "",
        "Generated C definitions promoted from the decoded native title-intro backing tables.",
        "",
        "## Summary",
        "",
        f"- OK: {summary['ok']}",
        f"- Header: `{summary['header']}`",
        f"- Source: `{summary['source']}`",
        "",
        "## Checks",
        "",
        "| Check | Status |",
        "| --- | --- |",
    ]
    for key, value in summary["checks"].items():
        lines.append(f"| `{key}` | `{value}` |")
    lines.extend(
        [
            "",
            "## Native Addresses",
            "",
            "| Symbol group | Native address |",
            "| --- | --- |",
        ]
    )
    for key, value in data["native_addresses"].items():
        lines.append(f"| `{key}` | `{value}` |")
    write_text(path, "\n".join(lines) + "\n")


def main() -> None:
    data = build_tables()
    write_json(OUT_JSON, data)
    write_markdown(OUT_MD, data)
    write_header(OUT_HEADER)
    write_source(OUT_SOURCE, data)
    if not data["summary"]["ok"]:
        raise SystemExit("title-intro runtime table promotion checks failed")


if __name__ == "__main__":
    main()
