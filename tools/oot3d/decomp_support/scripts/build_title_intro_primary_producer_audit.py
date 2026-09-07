#!/usr/bin/env python3
"""Audit the primary native producers that feed title-intro record C."""

from __future__ import annotations

import csv
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
EXPORT = ANALYSIS / "title_intro_context_feed_producer_ghidra_export"
DECOMPILED = EXPORT / "decompiled"
LITERALS = ANALYSIS / "title_intro_primary_producer_literal_words.csv"

OUT_JSON = ANALYSIS / "title_intro_primary_producer_audit.json"
OUT_MD = ANALYSIS / "title_intro_primary_producer_audit.md"

SOURCE_SELECTOR = DECOMPILED / "99002_002d6798_FUN_002d6798.c"
HISTORY_COMMIT = DECOMPILED / "99000_002cfe34_FUN_002cfe34.c"

EXPECTED_LITERALS = {
    "002d0180": "0x0054ac48",
    "002d0184": "0x0054be12",
    "002d0188": "0x0054b5f2",
    "002d018c": "0x0054be0a",
    "002d6a2c": "0x0054ac48",
    "002d6a30": "0x0000fffc",
    "002d6a34": "0x0054ac96",
    "002d6a38": "0x0054c2a0",
    "002d6a3c": "0x005b12e8",
    "002d6a40": "0x3f800000",
    "002d6a44": "0x0054ac25",
    "002d6a48": "0x0054acd8",
    "002d6a4c": "0x010004e0",
}

SOURCE_SELECTOR_PATTERNS = [
    (
        "uses_runtime_context_base",
        "iVar2 = DAT_002d6a2c;",
        "The producer uses the 0x0054AC48 runtime context.",
    ),
    (
        "respects_dirty_countdown",
        "*(char *)(DAT_002d6a2c + 0x2d) = cVar1 + -1;",
        "A nonzero dirty/countdown byte decrements and short-circuits selection.",
    ),
    (
        "clears_gate_latched_flag",
        "*(undefined1 *)(DAT_002d6a2c + 0x13) = 0;",
        "The latched gate flag is cleared before recomputing selector state.",
    ),
    (
        "resets_slot_and_record_source",
        "*(undefined1 *)(iVar2 + 0x16) = 0xff;",
        "The current slot/id is reset to 0xFF before bit decoding.",
    ),
    (
        "resets_record_source_byte",
        "*(undefined1 *)(iVar2 + 0x18) = 0xff;",
        "The record source byte is reset to 0xFF before bit decoding.",
    ),
    (
        "masks_current_gate_word",
        "uVar8 = DAT_002d6a30 & uVar5;",
        "Only mask 0x0000FFFC participates in selector bit decoding.",
    ),
    (
        "bit_0x80_maps_slot2_source0",
        "*(undefined1 *)(iVar2 + 0x16) = 2;",
        "Bit 0x80 maps to slot 2 and source byte 0.",
    ),
    (
        "bit_0x200_maps_slot5_source1",
        "*(undefined1 *)(iVar2 + 0x16) = 5;",
        "Bit 0x200 maps to slot 5 and source byte 1.",
    ),
    (
        "bit_0x800_maps_slot9_source2",
        "*(undefined1 *)(iVar2 + 0x16) = 9;",
        "Bit 0x800 maps to slot 9 and source byte 2.",
    ),
    (
        "bit_0x400_maps_slot11_source3",
        "*(undefined1 *)(iVar2 + 0x16) = 0xb;",
        "Bit 0x400 maps to slot 11 and source byte 3.",
    ),
    (
        "bit_0x100_maps_slot14_source4",
        "*(undefined1 *)(iVar2 + 0x16) = 0xe;",
        "Bit 0x100 maps to slot 14 and source byte 4.",
    ),
    (
        "flag_0x2_adjusts_source_and_slot",
        "*(char *)(iVar2 + 0x18) = *(char *)(iVar2 + 0x18) + -0x80;",
        "When gate word bit 1 is set outside mode 2, the source byte receives the high-bit adjustment and slot increments.",
    ),
    (
        "flag_0x1_adjusts_source_and_slot",
        "*(char *)(iVar2 + 0x18) = *(char *)(iVar2 + 0x18) + '@';",
        "When gate word bit 0 is set outside mode 2, the source byte receives the 0x40 adjustment and slot decrements.",
    ),
    (
        "mode2_uses_unit_float",
        "*(undefined4 *)(iVar2 + 0x8c) = uVar4;",
        "Mode 2 bypasses the vector lookup and stores the literal 1.0f value.",
    ),
    (
        "normal_mode_uses_vector_lookup",
        "*(undefined4 *)(iVar2 + 0x8c) = *(undefined4 *)(DAT_002d6a38 + iVar6 * 4 + 0x200);",
        "Normal mode converts a signed vector byte into a lookup-table value.",
    ),
    (
        "slot_change_dispatch",
        "FUN_002cfcf0(DAT_002d6a3c,*(undefined1 *)(iVar2 + 0x16));",
        "A changed slot/id is dispatched through the native helper.",
    ),
]

HISTORY_COMMIT_PATTERNS = [
    (
        "uses_runtime_context_base",
        "iVar1 = DAT_002d0180;",
        "The commit helper uses the same 0x0054AC48 runtime context.",
    ),
    (
        "selects_history_buffer_by_mode",
        "if (*(char *)(DAT_002d0180 + 0x24) == '\\x01') {",
        "Mode 1 chooses the +0xB0 history buffer; other modes choose +0xB4.",
    ),
    (
        "history_byte0_previous_slot",
        "*(undefined1 *)(iVar8 + (uint)*(byte *)(DAT_002d0180 + 0x25) * 8) =",
        "History byte 0 stores previous slot/id from context+0x26.",
    ),
    (
        "history_duration_delta",
        "*(short *)(iVar8 + (uint)*(byte *)(iVar1 + 0x25) * 8 + 2) =",
        "History bytes +2/+3 store the current-progress minus last-progress delta.",
    ),
    (
        "history_flags_from_previous_source",
        "*(byte *)(iVar8 + (uint)*(byte *)(iVar1 + 0x25) * 8 + 7) = *(byte *)(iVar1 + 0x2a) & 0xc0;",
        "History byte +7 stores the high two source-byte flags.",
    ),
    (
        "latches_current_slot",
        "*(undefined1 *)(iVar1 + 0x26) = *(undefined1 *)(iVar1 + 0x16);",
        "The current slot/id is latched as previous slot/id.",
    ),
    (
        "latches_current_source",
        "*(undefined1 *)(iVar1 + 0x2a) = *(undefined1 *)(iVar1 + 0x18);",
        "The current source byte is latched for the next history record.",
    ),
    (
        "increments_pending_index",
        "bVar4 = *(char *)(iVar1 + 0x25) + 1;",
        "The pending/history index advances after writing a sample.",
    ),
    (
        "normal_return_gate",
        "if ((uVar5 != 0x6b) && (param_1 == 0)) {",
        "Without forced commit, the helper returns until the pending index reaches 0x6B.",
    ),
    (
        "mode2_cursor_threshold",
        "if ((*(char *)(iVar1 + 0x24) == '\\x02') && (7 < *(byte *)(iVar1 + 0x3e))) {",
        "Mode 2 with cursor/count > 7 enables compaction/pattern matching.",
    ),
    (
        "match_sets_mode_ff",
        "*(undefined1 *)(iVar1 + 0x24) = 0xff;",
        "A matched pattern marks the mode byte as 0xFF.",
    ),
    (
        "match_updates_global_flag",
        "*(undefined1 *)(iVar11 + 0x78c) = 0xff;",
        "A matched pattern also updates a global runtime flag at +0x78C.",
    ),
    (
        "clears_active_after_compaction",
        "*(undefined1 *)(iVar1 + 0x14) = 0;",
        "Unmatched compaction clears the active flag.",
    ),
    (
        "clears_mode_on_exit",
        "*(undefined1 *)(iVar1 + 0x24) = 0;",
        "The normal exit clears the mode byte.",
    ),
]

BIT_MAPPING = [
    {"mask": "0x80", "slot": 2, "source_byte_low": 0, "priority": 1},
    {"mask": "0x200", "slot": 5, "source_byte_low": 1, "priority": 2},
    {"mask": "0x800", "slot": 9, "source_byte_low": 2, "priority": 3},
    {"mask": "0x400", "slot": 11, "source_byte_low": 3, "priority": 4},
    {"mask": "0x100", "slot": 14, "source_byte_low": 4, "priority": 5},
]

HISTORY_RECORD_LAYOUT = [
    {"offset": "+0", "field": "previous_slot", "source": "context+0x26"},
    {"offset": "+1", "field": "unused_or_padding", "source": "not written in this helper"},
    {"offset": "+2", "field": "duration_delta_s16", "source": "context+0xD8 - context+0xAC"},
    {"offset": "+4", "field": "previous_x", "source": "context+0x27"},
    {"offset": "+5", "field": "previous_y", "source": "context+0x28"},
    {"offset": "+6", "field": "previous_z", "source": "context+0x29"},
    {"offset": "+7", "field": "source_flags_hi2", "source": "context+0x2A & 0xC0"},
]


def rel(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def read_text(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def read_csv_rows(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


def find_line(path: Path, needle: str) -> dict[str, Any]:
    lines = read_text(path).splitlines()
    for index, line in enumerate(lines, start=1):
        if needle in line:
            return {"file": rel(path), "line": index, "text": line.strip(), "found": True}
    return {"file": rel(path), "line": 0, "text": "", "found": False}


def evidence_for(path: Path, patterns: list[tuple[str, str, str]]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for signal, needle, meaning in patterns:
        hit = find_line(path, needle)
        rows.append(
            {
                "id": signal,
                "status": "ok" if hit["found"] else "missing",
                "meaning": meaning,
                "evidence": hit,
            }
        )
    return rows


def literal_map() -> dict[str, str]:
    return {row.get("address", "").lower(): row.get("u32", "").lower() for row in read_csv_rows(LITERALS)}


def build_report() -> dict[str, Any]:
    literals = literal_map()
    literal_checks = {
        address: literals.get(address) == expected
        for address, expected in EXPECTED_LITERALS.items()
    }
    selector_evidence = evidence_for(SOURCE_SELECTOR, SOURCE_SELECTOR_PATTERNS)
    commit_evidence = evidence_for(HISTORY_COMMIT, HISTORY_COMMIT_PATTERNS)
    checks = {
        "literal_values_match": all(literal_checks.values()),
        "source_selector_patterns_found": all(item["status"] == "ok" for item in selector_evidence),
        "history_commit_patterns_found": all(item["status"] == "ok" for item in commit_evidence),
    }
    return {
        "format": "oot3d_title_intro_primary_producer_audit_v1",
        "inputs": {
            "source_selector_decompile": rel(SOURCE_SELECTOR),
            "history_commit_decompile": rel(HISTORY_COMMIT),
            "literal_words": rel(LITERALS),
        },
        "summary": {
            "ok": all(checks.values()),
            "checks": checks,
            "source_selector_entry": "002d6798",
            "history_commit_entry": "002cfe34",
            "next_gate": "Decode the sequence/history backing tables at 0x0054BE12/0x0054BE0A, the derived pattern windows at 0x0054C212/0x0054C222/0x0054C28F, and the vector lookup at 0x0054C2A0.",
        },
        "resolved_literals": {
            "runtime_context": literals.get("002d6a2c"),
            "selector_mask": literals.get("002d6a30"),
            "input_vector_bytes": literals.get("002d6a34"),
            "vector_lookup_table": literals.get("002d6a38"),
            "dispatch_context": literals.get("002d6a3c"),
            "mode2_unit_float": literals.get("002d6a40"),
            "history_buffer_mode1": literals.get("002d0184"),
            "history_buffer_other_modes": literals.get("002d018c"),
            "global_flag_context": literals.get("002d0188"),
        },
        "literal_checks": literal_checks,
        "source_selector": {
            "role": "selector_bitmask_to_record_source_update",
            "entry": "002d6798",
            "bit_mapping": BIT_MAPPING,
            "evidence": selector_evidence,
        },
        "history_commit": {
            "role": "history_record_commit_and_mode_compactor",
            "entry": "002cfe34",
            "history_record_stride": 8,
            "history_record_layout": HISTORY_RECORD_LAYOUT,
            "evidence": commit_evidence,
        },
        "unresolved": [
            "The sequence/history tables at 0x0054BE12 and 0x0054BE0A must be exported as data, not inferred from runtime writes.",
            "The pattern windows derived from 0x0054BE12 (+0x400, +0x410, +0x47D) and the lookup table at 0x0054C2A0 still need typed table definitions.",
            "The dispatch helpers 0x002CFCF0/0x002CFD24/0x002CFD74/0x002CFE00 remain unnamed and need focused evidence before source promotion.",
        ],
    }


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")


def evidence_ref(item: dict[str, Any]) -> str:
    evidence = item.get("evidence", {})
    if not evidence.get("found"):
        return "`missing`"
    return f"`{evidence['file']}:{evidence['line']}`"


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    lines = [
        "# OOT3D Title Intro Primary Producer Audit",
        "",
        "This audit types the two primary native producers feeding the title-intro runtime record consumed by player state 37.",
        "",
        "## Summary",
        "",
        f"- OK: {data['summary']['ok']}",
        f"- Source selector: `{data['summary']['source_selector_entry']}`",
        f"- History commit: `{data['summary']['history_commit_entry']}`",
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
            "## Resolved Literals",
            "",
            "| Name | Value |",
            "| --- | --- |",
        ]
    )
    for key, value in data["resolved_literals"].items():
        lines.append(f"| `{key}` | `{value}` |")

    lines.extend(
        [
            "",
            "## Selector Bit Mapping",
            "",
            "| Mask | Slot | Source byte low bits | Priority |",
            "| --- | ---: | ---: | ---: |",
        ]
    )
    for row in data["source_selector"]["bit_mapping"]:
        lines.append(f"| `{row['mask']}` | {row['slot']} | {row['source_byte_low']} | {row['priority']} |")

    lines.extend(
        [
            "",
            "## History Record Layout",
            "",
            f"- Stride: `{data['history_commit']['history_record_stride']}` bytes",
            "",
            "| Offset | Field | Source |",
            "| --- | --- | --- |",
        ]
    )
    for row in data["history_commit"]["history_record_layout"]:
        lines.append(f"| `{row['offset']}` | `{row['field']}` | {row['source']} |")

    lines.extend(
        [
            "",
            "## Source Selector Evidence",
            "",
            "| Signal | Status | Evidence | Meaning |",
            "| --- | --- | --- | --- |",
        ]
    )
    for item in data["source_selector"]["evidence"]:
        lines.append(f"| `{item['id']}` | `{item['status']}` | {evidence_ref(item)} | {item['meaning']} |")

    lines.extend(
        [
            "",
            "## History Commit Evidence",
            "",
            "| Signal | Status | Evidence | Meaning |",
            "| --- | --- | --- | --- |",
        ]
    )
    for item in data["history_commit"]["evidence"]:
        lines.append(f"| `{item['id']}` | `{item['status']}` | {evidence_ref(item)} | {item['meaning']} |")

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
        raise SystemExit("title intro primary producer audit failed")


if __name__ == "__main__":
    main()
