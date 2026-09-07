#!/usr/bin/env python3
"""Audit the native record pointer returned by the title-intro helper.

This resolves the getter at 0x002D0258 beyond the first literal pool:
0x002D0260 is a literal that points into a native runtime record table.
"""

from __future__ import annotations

import csv
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
EXPORT = ANALYSIS / "title_intro_record_pointer_writer_ghidra_export"
DECOMPILED = EXPORT / "decompiled"

REFERENCES = ANALYSIS / "title_intro_record_pointer_references.csv"
DATA_WORDS = ANALYSIS / "title_intro_record_pointer_data_words.csv"
LITERAL_WORDS = ANALYSIS / "title_intro_record_pointer_literal_words.csv"

OUT_JSON = ANALYSIS / "title_intro_record_pointer_audit.json"
OUT_MD = ANALYSIS / "title_intro_record_pointer_audit.md"

EXPECTED_LITERALS = {
    "002d0260": "0x0054aca3",
    "0033f408": "0x0054aca0",
    "00460a74": "0x0054ac48",
    "00460a7c": "0x0054ac9d",
    "00460a80": "0x0054aca0",
    "00460a8c": "0x0054aca3",
    "00476e18": "0x0054ac48",
    "00477cec": "0x0054ac9d",
    "0049341c": "0x0054ac48",
}

WRITER_PATTERNS = {
    "00460878": {
        "file": "99002_00460878_FUN_00460878.c",
        "role": "per_frame_record_table_updater",
        "patterns": [
            ("record_a_base", "pbVar5 = DAT_00460a7c;"),
            ("record_a_byte0", "*DAT_00460a7c = *(byte *)(iVar3 + 0x18) & 0x3f;"),
            ("record_b_base", "pcVar6 = DAT_00460a80;"),
            ("record_c_byte1", "*(char *)(DAT_00460a8c + 1) = cVar9;"),
            ("record_c_byte2", "*(byte *)(iVar7 + 2) = bVar8;"),
        ],
    },
    "00476d44": {
        "file": "99003_00476d44_FUN_00476d44.c",
        "role": "record_c_progression_writer",
        "patterns": [
            ("active_mode_gate", "cVar2 = *(char *)(DAT_00476e18 + 0x24);"),
            ("time_delta_gate", "2 < (uint)(*(int *)(DAT_00476e18 + 0xd8) - *(int *)(DAT_00476e18 + 0xac))"),
            ("record_c_byte0", "*(byte *)(DAT_00476e18 + 0x5b) = *(byte *)(DAT_00476e18 + 0x18) & 0x3f;"),
            ("cursor_increment", "*(char *)(iVar3 + 0x3e) = *pcVar1 + '\\x01';"),
        ],
    },
    "00477c90": {
        "file": "99004_00477c90_FUN_00477c90.c",
        "role": "record_table_initializer",
        "patterns": [
            ("record_table_base", "puVar1 = DAT_00477cec;"),
            ("record_a_init_byte0", "*DAT_00477cec = 0xff;"),
            ("record_b_init_byte0", "puVar1[3] = 0xff;"),
            ("record_c_init_byte0", "puVar1[6] = 0xff;"),
            ("record_c_init_byte1", "puVar1[7] = 0xff;"),
        ],
    },
    "00493328": {
        "file": "99005_00493328_FUN_00493328.c",
        "role": "sequence_stage_controller",
        "patterns": [
            ("context_base", "iVar3 = DAT_0049341c;"),
            ("record_b_byte1_gate", "cVar1 = *(char *)(DAT_0049341c + 0x59);"),
            ("sequence_source", "pcVar4 = DAT_00493420;"),
            ("sequence_target", "*(char **)(iVar3 + 0xa8) = DAT_00493420;"),
        ],
    },
}


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


def word_map(rows: list[dict[str, str]]) -> dict[str, str]:
    return {row.get("address", "").lower(): row.get("u32", "").lower() for row in rows}


def references_to(rows: list[dict[str, str]], target: str, ref_type: str | None = None) -> list[dict[str, str]]:
    target = target.lower()
    result = [row for row in rows if row.get("target", "").lower() == target]
    if ref_type is not None:
        result = [row for row in result if row.get("reference_type", "").upper() == ref_type.upper()]
    return result


def has_ref(rows: list[dict[str, str]], target: str, from_entry: str, ref_type: str | None = None) -> bool:
    from_entry = from_entry.lower()
    return any(row.get("from_function_entry", "").lower() == from_entry for row in references_to(rows, target, ref_type))


def pattern_rows() -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for entry, spec in WRITER_PATTERNS.items():
        path = DECOMPILED / spec["file"]
        evidence = []
        for signal, needle in spec["patterns"]:
            hit = find_line(path, needle)
            evidence.append(
                {
                    "id": signal,
                    "status": "ok" if hit["found"] else "missing",
                    "evidence": hit,
                }
            )
        rows.append(
            {
                "entry": entry,
                "role": spec["role"],
                "file": rel(path),
                "ok": all(item["status"] == "ok" for item in evidence),
                "evidence": evidence,
            }
        )
    return rows


def build_report() -> dict[str, Any]:
    reference_rows = read_csv_rows(REFERENCES)
    data_words = word_map(read_csv_rows(DATA_WORDS))
    literal_words = word_map(read_csv_rows(LITERAL_WORDS))
    patterns = pattern_rows()
    literal_checks = {
        address: literal_words.get(address, data_words.get(address, "")) == value
        for address, value in EXPECTED_LITERALS.items()
    }
    checks = {
        "getter_literal_points_to_record_c": data_words.get("002d0260") == "0x0054aca3",
        "record_c_has_getter_data_ref": has_ref(reference_rows, "0054aca3", "002d0258", "DATA"),
        "record_c_has_progression_writer": has_ref(reference_rows, "0054aca3", "00476d44", "WRITE"),
        "record_c_has_initializer_writer": has_ref(reference_rows, "0054aca3", "00477c90", "WRITE"),
        "record_b_has_update_writer": has_ref(reference_rows, "0054aca0", "00460878", "WRITE"),
        "record_b_has_initializer_writer": has_ref(reference_rows, "0054aca0", "00477c90", "WRITE"),
        "literal_pool_values_match": all(literal_checks.values()),
        "writer_patterns_found": all(row["ok"] for row in patterns),
    }
    record_table = [
        {
            "record": "A",
            "address": "0x0054AC9D",
            "context_offset": "+0x55",
            "primary_writer": "00460878",
            "known_bytes": [
                "byte0 = context+0x18 masked to 0x3F",
                "byte1 = countdown/status value, 0xFE/0xFF fallback",
                "byte2 = context+0x3E",
            ],
        },
        {
            "record": "B",
            "address": "0x0054ACA0",
            "context_offset": "+0x58",
            "primary_writer": "00460878",
            "known_bytes": [
                "byte0 = lookup table result from context+0x1E",
                "byte1 = context+0x1D",
                "byte2 = context+0x44 derived slot or raw byte",
            ],
        },
        {
            "record": "C",
            "address": "0x0054ACA3",
            "context_offset": "+0x5B",
            "primary_writer": "00460878 and 00476D44",
            "known_bytes": [
                "byte0 = context+0x18 masked to 0x3F when progression gate fires",
                "byte1 = context+0x24 mode byte",
                "byte2 = context+0x3E cursor/count byte",
            ],
        },
    ]
    return {
        "format": "oot3d_title_intro_record_pointer_audit_v1",
        "inputs": {
            "references": rel(REFERENCES),
            "data_words": rel(DATA_WORDS),
            "literal_words": rel(LITERAL_WORDS),
            "writer_export": rel(EXPORT),
        },
        "summary": {
            "ok": all(checks.values()),
            "checks": checks,
            "next_gate": "Type the 0x0054AC48 runtime context and trace upstream writes to the bytes feeding record C, then connect those writes to native cutscene/QDB state.",
        },
        "resolved": {
            "context_base": "0x0054AC48",
            "record_table_base": "0x0054AC9D",
            "getter_002d0258_returns": "0x0054ACA3",
            "getter_0033f400_returns": "0x0054ACA0",
            "record_stride": 3,
            "interpretation": "0x002D0258 returns a native runtime record pointer into context+0x5B, not a direct pointer to a QDB row. The record is derived by native writers from the runtime context.",
        },
        "literal_checks": literal_checks,
        "references": reference_rows,
        "record_table": record_table,
        "writer_patterns": patterns,
        "unresolved": [
            "The upstream producers for context bytes +0x16, +0x18, +0x1D, +0x1E, +0x24, +0x2B, +0x3E, +0x44, +0x94, and +0xA8 still need typed source mapping.",
            "The table at 0x0054C212 used by record B byte0 needs a separate data export before naming that field.",
            "The sequence source at 0x0054BEB2 selected by 0x00493328 needs a separate data export before it can be tied to a specific asset/source index.",
        ],
    }


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")


def ref_text(row: dict[str, str]) -> str:
    return (
        f"`{row.get('target', '')}` from `{row.get('from_function_entry', '')}` "
        f"at `{row.get('from_address', '')}` `{row.get('reference_type', '')}`"
    )


def evidence_ref(item: dict[str, Any]) -> str:
    evidence = item.get("evidence", {})
    if not evidence.get("found"):
        return "`missing`"
    return f"`{evidence['file']}:{evidence['line']}`"


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    resolved = data["resolved"]
    lines = [
        "# OOT3D Title Intro Record Pointer Audit",
        "",
        "This audit resolves the native pointer returned by `0x002D0258`. The pointer is a literal into a runtime record table, and the table is populated by native writer functions.",
        "",
        "## Summary",
        "",
        f"- OK: {data['summary']['ok']}",
        f"- Context base: `{resolved['context_base']}`",
        f"- Record table base: `{resolved['record_table_base']}`",
        f"- `0x002D0258` returns: `{resolved['getter_002d0258_returns']}`",
        f"- `0x0033F400` returns: `{resolved['getter_0033f400_returns']}`",
        f"- Record stride: `{resolved['record_stride']}` bytes",
        f"- Interpretation: {resolved['interpretation']}",
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
            "## Record Table",
            "",
            "| Record | Address | Context offset | Primary writer | Known bytes |",
            "| --- | --- | --- | --- | --- |",
        ]
    )
    for row in data["record_table"]:
        lines.append(
            f"| `{row['record']}` | `{row['address']}` | `{row['context_offset']}` | "
            f"`{row['primary_writer']}` | {'; '.join(row['known_bytes'])} |"
        )

    lines.extend(
        [
            "",
            "## References",
            "",
            "| Target | From | Address | Type |",
            "| --- | --- | --- | --- |",
        ]
    )
    for row in data["references"]:
        lines.append(
            f"| `{row.get('target', '')}` | `{row.get('from_function_entry', '')}` "
            f"`{row.get('from_function', '')}` | `{row.get('from_address', '')}` | `{row.get('reference_type', '')}` |"
        )

    lines.extend(
        [
            "",
            "## Writer Evidence",
            "",
            "| Entry | Role | Evidence |",
            "| --- | --- | --- |",
        ]
    )
    for row in data["writer_patterns"]:
        evidence = "; ".join(evidence_ref(item) for item in row["evidence"])
        lines.append(f"| `{row['entry']}` | `{row['role']}` | {evidence} |")

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
        raise SystemExit("title intro record pointer audit failed")


if __name__ == "__main__":
    main()
