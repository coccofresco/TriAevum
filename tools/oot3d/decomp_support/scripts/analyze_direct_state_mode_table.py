#!/usr/bin/env python3
"""Decode the state/mode side-effect table used by OOT3D helper FUN_00340bdc."""

from __future__ import annotations

import argparse
import csv
import json
import re
import struct
from collections import Counter
from pathlib import Path
from typing import Any

from classify_direct_tail_call_target_identities import DEFAULT_DISASSEMBLY, disassembly_callsite_index, normalize_addr
from probe_direct_source_subregions import rel
from synthesize_target_disassembly import DEFAULT_CODE_BIN


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TARGET = "00340bdc"
DEFAULT_LITERAL_ADDR = "0x00340cfc"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_state_mode_table.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_state_mode_table.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_state_mode_table.md"
INSN_ADDR_RE = re.compile(r"^(?P<addr>[0-9a-fA-F]{8}):")
MOV_R1_IMM_RE = re.compile(r"\bmov(?:s|ne|eq|ls|hi)?\s+r1,\s*#(?P<value>\d+)\b")


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def read_u32(code: bytes, code_base: int, addr: int) -> int:
    offset = addr - code_base
    if offset < 0 or offset > len(code) - 4:
        raise ValueError(f"address out of code image: 0x{addr:08x}")
    return struct.unpack_from("<I", code, offset)[0]


def read_row(code: bytes, code_base: int, table_addr: int, state: int) -> tuple[int, int, int, int]:
    offset = table_addr - code_base + state * 4
    if offset < 0 or offset > len(code) - 4:
        return (0, 0, 0, 0)
    return tuple(code[offset : offset + 4])  # type: ignore[return-value]


def disassembly_lines(path: Path) -> tuple[list[str], dict[str, int]]:
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines() if path.is_file() else []
    by_addr: dict[str, int] = {}
    for i, line in enumerate(lines):
        match = INSN_ADDR_RE.match(line)
        if match:
            by_addr[normalize_addr(match.group("addr"))] = i
    return lines, by_addr


def r1_immediate_before(lines: list[str], by_addr: dict[str, int], callsite_addr: str) -> int | None:
    idx = by_addr.get(normalize_addr(callsite_addr))
    if idx is None:
        return None
    for prev in reversed(lines[max(0, idx - 5) : idx]):
        if ":" not in prev:
            continue
        match = MOV_R1_IMM_RE.search(prev.lower())
        if match:
            return int(match.group("value"))
    return None


def collect_callsite_states(disassembly: Path, target_addr: str) -> list[dict[str, Any]]:
    callsite_by_dest, _ = disassembly_callsite_index(disassembly)
    lines, by_addr = disassembly_lines(disassembly)
    result: list[dict[str, Any]] = []
    for site in callsite_by_dest.get(normalize_addr(target_addr), []):
        value = r1_immediate_before(lines, by_addr, site.get("callsite_addr", ""))
        result.append(
            {
                "callsite_addr": normalize_addr(site.get("callsite_addr")),
                "function_entry": normalize_addr(site.get("function_entry")),
                "function_name": site.get("function_name", ""),
                "r1_immediate": value,
            }
        )
    return result


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    code = args.code_bin.read_bytes()
    code_base = int(str(args.code_base), 0)
    literal_addr = int(str(args.literal_addr), 0)
    table_addr = read_u32(code, code_base, literal_addr)
    callsites = collect_callsite_states(args.disassembly, args.target)
    known_states = sorted({int(site["r1_immediate"]) for site in callsites if site.get("r1_immediate") is not None})
    player_states = sorted(
        {
            int(site["r1_immediate"])
            for site in callsites
            if site.get("r1_immediate") is not None and site.get("function_entry") == "00473ef8"
        }
    )
    global_counts = Counter(int(site["r1_immediate"]) for site in callsites if site.get("r1_immediate") is not None)
    player_counts = Counter(
        int(site["r1_immediate"])
        for site in callsites
        if site.get("r1_immediate") is not None and site.get("function_entry") == "00473ef8"
    )
    rows: list[dict[str, Any]] = []
    for state in known_states:
        row = read_row(code, code_base, table_addr, state)
        rows.append(
            {
                "state": state,
                "table_addr": f"{table_addr + state * 4:08x}",
                "flag0_entry_class": row[0],
                "flag1_call_0032c560": row[1],
                "flag2_call_0032c550": row[2],
                "flag3_call_0032c540_or_0032c570": row[3],
                "flag_pattern": "".join(str(value) for value in row),
                "global_callsite_count": global_counts[state],
                "player_function_callsite_count": player_counts[state],
                "used_by_player_function": state in player_counts,
            }
        )
    player_only_rows = [row for row in rows if row["used_by_player_function"]]
    nonzero_rows = [row for row in rows if row["flag_pattern"] != "0000"]
    summary = {
        "target": normalize_addr(args.target),
        "literal_addr": f"{literal_addr:08x}",
        "table_addr": f"{table_addr:08x}",
        "known_state_values": len(known_states),
        "player_state_values": len(player_states),
        "known_states": known_states,
        "player_states": player_states,
        "nonzero_table_rows": len(nonzero_rows),
        "player_nonzero_table_rows": sum(1 for row in player_only_rows if row["flag_pattern"] != "0000"),
        "unique_flag_patterns": sorted({row["flag_pattern"] for row in rows}),
        "player_unique_flag_patterns": sorted({row["flag_pattern"] for row in player_only_rows}),
        "global_callsite_count": len(callsites),
        "player_function_callsite_count": sum(1 for site in callsites if site.get("function_entry") == "00473ef8"),
        "next_gate": "Assign semantic names to state values and side-effect flags before using FUN_00340bdc in promoted C.",
    }
    return {
        "format": "oot3d_direct_state_mode_table_v1",
        "inputs": {
            "code_bin": str(args.code_bin),
            "disassembly": rel(args.disassembly),
            "target": normalize_addr(args.target),
            "literal_addr": f"{literal_addr:08x}",
        },
        "summary": summary,
        "rows": rows,
        "callsites": callsites,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct State/Mode Table",
        "",
        "This report decodes the 4-byte state/mode side-effect table used by `FUN_00340bdc`.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Target | `{summary['target']}` |",
        f"| Literal address | `{summary['literal_addr']}` |",
        f"| Table address | `{summary['table_addr']}` |",
        f"| Known state values | {summary['known_state_values']} |",
        f"| Player state values | {summary['player_state_values']} |",
        f"| Nonzero table rows | {summary['nonzero_table_rows']} |",
        f"| Player nonzero table rows | {summary['player_nonzero_table_rows']} |",
        f"| Global callsites | {summary['global_callsite_count']} |",
        f"| Player callsites | {summary['player_function_callsite_count']} |",
        "",
        f"Next gate: {summary['next_gate']}",
        "",
        "## Player States",
        "",
        "| State | Flags | Global calls | Player calls | Table address |",
        "| ---: | --- | ---: | ---: | --- |",
    ]
    for row in data["rows"]:
        if not row.get("used_by_player_function"):
            continue
        lines.append(
            f"| {row['state']} | `{row['flag_pattern']}` | {row['global_callsite_count']} | "
            f"{row['player_function_callsite_count']} | `{row['table_addr']}` |"
        )
    lines.extend(
        [
            "",
            "## All Observed States",
            "",
            "| State | Flags | Global calls | Player calls | Table address |",
            "| ---: | --- | ---: | ---: | --- |",
        ]
    )
    for row in data["rows"]:
        lines.append(
            f"| {row['state']} | `{row['flag_pattern']}` | {row['global_callsite_count']} | "
            f"{row['player_function_callsite_count']} | `{row['table_addr']}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--code-bin", type=Path, default=DEFAULT_CODE_BIN)
    parser.add_argument("--code-base", default="0x00100000")
    parser.add_argument("--disassembly", type=Path, default=DEFAULT_DISASSEMBLY)
    parser.add_argument("--target", default=DEFAULT_TARGET)
    parser.add_argument("--literal-addr", default=DEFAULT_LITERAL_ADDR)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()

    if not args.code_bin.is_file():
        raise SystemExit(f"missing code image: {args.code_bin}")
    if not args.disassembly.is_file():
        raise SystemExit(f"missing disassembly: {args.disassembly}")

    data = build_report(args)
    fields = [
        "state",
        "table_addr",
        "flag0_entry_class",
        "flag1_call_0032c560",
        "flag2_call_0032c550",
        "flag3_call_0032c540_or_0032c570",
        "flag_pattern",
        "global_callsite_count",
        "player_function_callsite_count",
        "used_by_player_function",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct state/mode table: "
        f"{summary['known_state_values']} observed states, "
        f"{summary['player_state_values']} player states, table {summary['table_addr']}"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
