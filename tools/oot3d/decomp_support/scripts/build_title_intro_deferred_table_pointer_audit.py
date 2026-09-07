#!/usr/bin/env python3
"""Audit title-intro deferred helper literal pools and native table targets."""

from __future__ import annotations

import csv
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"

POINTER_WORDS = ANALYSIS / "title_intro_deferred_table_pointer_words.csv"
TARGET_WORDS = ANALYSIS / "title_intro_deferred_table_target_words.csv"
RUNTIME_TABLE_AUDIT = ANALYSIS / "title_intro_runtime_tables.json"

OUT_JSON = ANALYSIS / "title_intro_deferred_table_pointer_audit.json"
OUT_MD = ANALYSIS / "title_intro_deferred_table_pointer_audit.md"


LITERAL_ROLES = {
    "003371ac": ("0033704c", "global_flag_context_base", "code.bin static table base reused with +0x78C flag byte"),
    "003371b0": ("0033704c", "global_flag_promote_compare", "constant compared against request * 0x100000"),
    "003371b4": ("0033704c", "runtime_context", "title intro runtime context/state at 0x0054AC48"),
    "003371b8": ("0033704c", "override_cfff_result", "request rewrite when 0xCFFF and global flag is active"),
    "003371bc": ("0033704c", "override_0fff_result", "request rewrite when 0x0FFF and global flag is active"),
    "003371c0": ("0033704c", "record_table_state", "record/dispatch byte table near state+0x55"),
    "003371c4": ("0033704c", "runtime_request_progress_buffer", "BSS buffer cleared by 0032B184"),
    "003371c8": ("0033704c", "runtime_request_previous_progress_buffer", "BSS buffer cleared by 0032B184"),
    "003371cc": ("0033704c", "runtime_request_duration_buffer", "BSS buffer cleared by 0032B184"),
    "003371d0": ("0033704c", "runtime_request_lookup_buffer", "BSS buffer cleared by 00343280"),
    "003371d4": ("0033704c", "history_copy_source_lane12", "native lane used when runtime request has 0xD000 bits"),
    "0033f2c8": ("0033f248", "runtime_context", "sequence bind state"),
    "0033f2cc": ("0033f248", "runtime_external_handle", "external runtime handle used on sequence clear"),
    "0033f2d0": ("0033f248", "default_sequence_lane", "default lane for param_1 >= 15"),
    "0033f2d4": ("0033f248", "sequence_table_base", "base used as param_1*0xA0-0xA0 for lanes 1..14"),
    "00477c74": ("00477a1c", "runtime_context", "sequence advance state"),
    "00477c78": ("00477a1c", "runtime_external_handle", "external runtime handle used on sequence completion"),
    "00477c7c": ("00477a1c", "scale_byte_to_float_factor", "0.007874016 factor applied to row scale byte"),
    "00477c80": ("00477a1c", "record_b_dispatch_context", "runtime-only dispatch context"),
    "00477c84": ("00477a1c", "vector_lookup_base", "native vector-scale lookup table"),
    "00477c88": ("00477a1c", "dispatch_gate_context", "static context byte tested before record-B dispatch"),
    "00477c8c": ("00477a1c", "record_b_dispatch_payload", "record-B dispatch payload source"),
    "0047b134": ("0047afb0", "runtime_context", "request initializer/pattern matcher state"),
    "0047b138": ("0047afb0", "recent_slot_history_buffer", "static history window used by pattern matcher"),
    "0047b13c": ("0047afb0", "pattern_symbol_table", "symbol lookup used by pattern matcher"),
    "0047b140": ("0047afb0", "pattern_records", "native pattern record table"),
    "0047bfe0": ("0047bd30", "runtime_context", "request advance state"),
    "0047bfe4": ("0047bd30", "request_lookup_buffer", "runtime-only current lookup byte buffer"),
    "0047bfe8": ("0047bd30", "request_duration_buffer", "runtime-only duration/progress halfword buffer"),
    "0047bfec": ("0047bd30", "sequence_table_base", "native request/sequence lane table base"),
    "0047bff0": ("0047bd30", "request_cursor_buffer", "runtime-only per-lane cursor buffer"),
    "0047bff4": ("0047bd30", "request_previous_progress_buffer", "runtime-only previous progress buffer"),
}


def rel(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def read_csv_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


def read_u32_rows(path: Path) -> dict[str, dict[str, Any]]:
    rows: dict[str, dict[str, Any]] = {}
    for row in read_csv_rows(path):
        address = row["address"].lower()
        byte_count = len(bytes.fromhex(row["bytes"].replace(" ", "")))
        rows[address] = {
            "address": address,
            "u32": int(row["u32"], 16),
            "u32_hex": row["u32"],
            "byte_count": byte_count,
        }
    return rows


def target_kind(value: int) -> str:
    if 0x00540000 <= value < 0x00550000:
        return "code_bin_static_data"
    if 0x005B0000 <= value < 0x005C0000:
        return "runtime_bss_or_unmapped_buffer"
    if value == 0x010004E0:
        return "external_runtime_handle"
    return "constant"


def covered_by_static_export(value: int, target_rows: dict[str, dict[str, Any]]) -> bool:
    for row in target_rows.values():
        start = int(row["address"], 16)
        end = start + row["byte_count"]
        if start <= value < end:
            return True
    return False


def build_report() -> dict[str, Any]:
    pointer_rows = read_u32_rows(POINTER_WORDS)
    target_rows = read_u32_rows(TARGET_WORDS)
    runtime_table_audit = json.loads(RUNTIME_TABLE_AUDIT.read_text(encoding="utf-8"))
    rows: list[dict[str, Any]] = []

    for address, pointer in pointer_rows.items():
        helper, role, meaning = LITERAL_ROLES.get(address, ("unknown", "unknown", "unclassified literal"))
        value = pointer["u32"]
        rows.append(
            {
                "literal_address": f"0x{int(address, 16):08X}",
                "helper": helper,
                "role": role,
                "target": f"0x{value:08X}",
                "target_kind": target_kind(value),
                "static_export_covered": covered_by_static_export(value, target_rows),
                "meaning": meaning,
            }
        )

    checks = {
        "pointer_literal_count": len(pointer_rows) == len(LITERAL_ROLES),
        "runtime_table_audit_ok": bool(runtime_table_audit["summary"]["ok"]),
        "sequence_table_base_shared_by_0033f248_and_0047bd30": pointer_rows["0033f2d4"]["u32"]
        == pointer_rows["0047bfec"]["u32"]
        == 0x0054B5F2,
        "default_lane_matches_base_plus_14_stride": pointer_rows["0033f2d0"]["u32"]
        == 0x0054B5F2 + 14 * 0xA0,
        "runtime_context_shared": pointer_rows["003371b4"]["u32"]
        == pointer_rows["0033f2c8"]["u32"]
        == pointer_rows["00477c74"]["u32"]
        == pointer_rows["0047b134"]["u32"]
        == pointer_rows["0047bfe0"]["u32"]
        == 0x0054AC48,
        "pattern_table_resolved": pointer_rows["0047b140"]["u32"] == 0x0054C222,
        "vector_lookup_resolved": pointer_rows["00477c84"]["u32"] == 0x0054C2A0,
        "runtime_buffers_are_unmapped_bss": all(
            row["target_kind"] == "runtime_bss_or_unmapped_buffer"
            for row in rows
            if row["target"].startswith("0x005B")
        ),
        "static_codebin_targets_are_export_covered": all(
            row["static_export_covered"]
            for row in rows
            if row["target_kind"] == "code_bin_static_data"
        ),
    }

    return {
        "format": "oot3d_title_intro_deferred_table_pointer_audit_v1",
        "inputs": {
            "pointer_words": rel(POINTER_WORDS),
            "target_words": rel(TARGET_WORDS),
            "runtime_table_audit": rel(RUNTIME_TABLE_AUDIT),
        },
        "summary": {
            "ok": all(checks.values()),
            "checks": checks,
            "next_gate": "Use the promoted 0x0054B5F2 lane table and runtime-only 0x005B buffers to implement 0x0047BD30 and 0x00477A1C.",
        },
        "rows": rows,
    }


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def write_json(path: Path, data: Any) -> None:
    write_text(path, json.dumps(data, indent=2) + "\n")


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    lines = [
        "# OOT3D Title Intro Deferred Table Pointer Audit",
        "",
        "This audit maps deferred-helper literal pools to native OOT3D data targets. Static `0x0054...` targets are read from `code.bin`; `0x005B...` targets are runtime/BSS buffers and are intentionally not exported as static asset data.",
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

    lines.extend(
        [
            "",
            "## Literal Targets",
            "",
            "| Literal | Helper | Role | Target | Target kind | Static export | Meaning |",
            "| --- | --- | --- | --- | --- | --- | --- |",
        ]
    )
    for row in data["rows"]:
        lines.append(
            f"| `{row['literal_address']}` | `{row['helper']}` | `{row['role']}` | "
            f"`{row['target']}` | `{row['target_kind']}` | `{row['static_export_covered']}` | {row['meaning']} |"
        )
    write_text(path, "\n".join(lines) + "\n")


def main() -> None:
    data = build_report()
    write_json(OUT_JSON, data)
    write_markdown(OUT_MD, data)
    if not data["summary"]["ok"]:
        raise SystemExit("title-intro deferred table pointer audit failed")


if __name__ == "__main__":
    main()
