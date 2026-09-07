#!/usr/bin/env python3
"""Audit the OOT3D title-intro record-byte applicator FUN_002D038C."""

from __future__ import annotations

import csv
import json
import re
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"

DECOMP = ANALYSIS / "title_intro_player_action_consumer_ghidra_export" / "decompiled" / "99016_002d038c_FUN_002d038c.c"
DISASM = ANALYSIS / "title_intro_player_action_consumer_ghidra_export" / "disassembly_selected.txt"
WORDS = ANALYSIS / "title_intro_record_apply_words.csv"
BINDING = ANALYSIS / "title_intro_playback_binding_audit.json"
CONSUMER = ANALYSIS / "title_intro_player_action_consumer_audit.json"

OUT_JSON = ANALYSIS / "title_intro_record_apply_audit.json"
OUT_MD = ANALYSIS / "title_intro_record_apply_audit.md"


EXPECTED_WORDS = {
    "002d0404": ("record_byte_table", "0x005a2e7c"),
    "002d0408": ("first_change_latch", "0x0055a21c"),
    "002d040c": ("first_change_init_context", "0x005be5b8"),
    "002d0410": ("adjacent_literal_0", "0x00100000"),
    "002d0414": ("adjacent_literal_1", "0x0048b210"),
    "002d0418": ("record_change_notify_context", "0x005c1878"),
}


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def read_words(path: Path) -> dict[str, dict[str, str]]:
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return {row["address"].lower(): row for row in csv.DictReader(handle)}


def rel(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def decomp_signals(text: str) -> dict[str, bool]:
    compact = re.sub(r"\s+", " ", text)
    return {
        "loads_record_byte_table": "DAT_002d0404" in text,
        "skips_when_value_unchanged": "!= param_2" in compact,
        "index_under_8_has_side_effects": "param_1 < 8" in compact,
        "uses_first_change_latch": "FUN_003679b4(DAT_002d0408)" in compact,
        "calls_first_change_init": "FUN_0036788c(DAT_002d040c)" in compact,
        "calls_change_notify": "FUN_00494de0(DAT_002d0418,param_1,param_2)" in compact,
        "stores_new_byte": "*(char *)(iVar1 + param_1) = (char)param_2" in compact,
    }


def callsites_for(target: str, disasm_text: str) -> list[str]:
    pattern = re.compile(r"^([0-9a-f]{8}):\s+bl\s+0x" + re.escape(target.lower()) + r"\b", re.IGNORECASE)
    result = []
    for line in disasm_text.splitlines():
        match = pattern.search(line.strip())
        if match:
            result.append(match.group(1).lower())
    return result


def state37_callsites(callsites: list[str]) -> list[str]:
    return [addr for addr in callsites if 0x00475654 <= int(addr, 16) < 0x004757B8]


def build_report() -> dict[str, Any]:
    decomp_text = DECOMP.read_text(encoding="utf-8", errors="replace")
    disasm_text = DISASM.read_text(encoding="utf-8", errors="replace")
    words = read_words(WORDS)
    binding = read_json(BINDING)
    consumer = read_json(CONSUMER)
    signals = decomp_signals(decomp_text)
    literals = []
    literal_checks = {}
    for address, (role, expected) in EXPECTED_WORDS.items():
        row = words.get(address, {})
        value = row.get("u32", "").lower()
        literals.append(
            {
                "literal_pool_address": address,
                "role": role,
                "value": value,
                "expected": expected,
                "bytes": row.get("bytes", ""),
            }
        )
        literal_checks[role] = value == expected

    all_callsites = callsites_for("002d038c", disasm_text)
    state_callsites = state37_callsites(all_callsites)
    state37 = next(row for row in consumer["state_rows"] if int(row["state"]) == 37)
    checks = {
        "binding_audit_ok": bool(binding["summary"]["ok"]),
        "consumer_state37_has_apply_call": "FUN_002d038c" in state37["calls"],
        "state37_has_two_direct_apply_callsites": state_callsites == ["00475690", "004756a8"],
        "all_expected_literal_words_match": all(literal_checks.values()),
        **signals,
    }
    return {
        "format": "oot3d_title_intro_record_apply_audit_v1",
        "inputs": {
            "decompiled": rel(DECOMP),
            "disassembly": rel(DISASM),
            "literal_words": rel(WORDS),
            "playback_binding": rel(BINDING),
            "consumer_audit": rel(CONSUMER),
        },
        "summary": {
            "ok": all(checks.values()),
            "checks": checks,
            "entry": "002d038c",
            "role": "record_byte_table_apply_and_notify",
            "next_gate": "Use the promoted applicator and dispatch adapters as part of the open-title scene orchestration path.",
        },
        "literals": literals,
        "literal_checks": literal_checks,
        "semantics": {
            "record_byte_table": "If the stored byte at index differs from the incoming value, the helper applies the change to the native byte table at 0x005A2E7C.",
            "side_effect_scope": "For index < 8, a first-change latch at 0x0055A21C may initialize the global context at 0x005BE5B8, then FUN_00494DE0 is called with context 0x005C1878, index, and value.",
            "unchanged_value": "No side effects and no write occur when the current table byte already equals the incoming value.",
            "state37_use": "State 37 calls this helper twice in its direct range: once for record byte0 at the current cursor, then once to write 0xFF to the next slot after incrementing the cursor.",
        },
        "callsites": {
            "all": all_callsites,
            "state37": state_callsites,
        },
        "unresolved": [
            "FUN_00494DE0 still needs naming and argument typing beyond the observed context/index/value call shape.",
            "The adjacent literal values 0x00100000 and 0x0048B210 are exported but not assigned gameplay semantics here.",
            "The open-title scene orchestration remains the next unresolved path before full title cue execution.",
        ],
    }


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def write_json(path: Path, data: Any) -> None:
    write_text(path, json.dumps(data, indent=2) + "\n")


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    lines = [
        "# OOT3D Title Intro Record Apply Audit",
        "",
        "This audit types `FUN_002D038C`, the helper used by title-intro player state 37 to apply decoded record bytes to the native byte table.",
        "",
        "## Summary",
        "",
        f"- OK: {data['summary']['ok']}",
        f"- Entry: `{data['summary']['entry']}`",
        f"- Role: `{data['summary']['role']}`",
        f"- Next gate: {data['summary']['next_gate']}",
        "",
        "## Checks",
        "",
        "| Check | Status |",
        "| --- | --- |",
    ]
    for key, value in data["summary"]["checks"].items():
        lines.append(f"| `{key}` | `{value}` |")

    lines.extend(["", "## Literal Pool", "", "| Address | Role | Value |", "| --- | --- | --- |"])
    for row in data["literals"]:
        lines.append(f"| `{row['literal_pool_address']}` | `{row['role']}` | `{row['value']}` |")

    lines.extend(["", "## State 37 Callsites", ""])
    lines.append("- " + ", ".join(f"`{addr}`" for addr in data["callsites"]["state37"]))

    lines.extend(["", "## Semantics", ""])
    for key, value in data["semantics"].items():
        lines.append(f"- `{key}`: {value}")

    lines.extend(["", "## Unresolved", ""])
    for item in data["unresolved"]:
        lines.append(f"- {item}")
    write_text(OUT_MD, "\n".join(lines) + "\n")


def main() -> None:
    data = build_report()
    write_json(OUT_JSON, data)
    write_markdown(OUT_MD, data)
    if not data["summary"]["ok"]:
        raise SystemExit("title-intro record apply audit failed")


if __name__ == "__main__":
    main()
