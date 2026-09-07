#!/usr/bin/env python3
"""Map OOT3D player state/mode values to the switch handlers in 00473ef8."""

from __future__ import annotations

import argparse
import csv
import json
import struct
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

from classify_direct_tail_call_target_identities import normalize_addr, read_json
from probe_direct_source_subregions import rel
from synthesize_target_disassembly import DEFAULT_CODE_BIN


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_STATE_TABLE = ROOT / "analysis" / "direct_state_mode_table.json"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_state_mode_switch_map.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_state_mode_switch_map.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_state_mode_switch_map.md"
DEFAULT_SWITCH_ENTRY = "0x00473ef8"
DEFAULT_JUMP_TABLE = "0x00474010"
DEFAULT_FIRST_STATE = 9
DEFAULT_CASE_COUNT = 43


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


def read_jump_table(code: bytes, code_base: int, table_addr: int, first_state: int, count: int) -> dict[int, str]:
    result: dict[int, str] = {}
    for index in range(count):
        offset = table_addr - code_base + index * 4
        if offset < 0 or offset > len(code) - 4:
            continue
        target = struct.unpack_from("<I", code, offset)[0]
        result[first_state + index] = f"{target:08x}"
    return result


def state_rows_by_value(table: dict[str, Any]) -> dict[int, dict[str, Any]]:
    result: dict[int, dict[str, Any]] = {}
    for row in table.get("rows", []):
        if isinstance(row, dict) and row.get("state") is not None:
            result[int(row["state"])] = row
    return result


def callsites_by_state(table: dict[str, Any]) -> dict[int, list[dict[str, Any]]]:
    result: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for site in table.get("callsites", []):
        if not isinstance(site, dict) or site.get("r1_immediate") is None:
            continue
        result[int(site["r1_immediate"])].append(site)
    return result


def handler_owner(addr: str, handler_ranges: list[tuple[int, int, list[int]]]) -> list[int]:
    value = int(normalize_addr(addr), 16)
    for start, end, states in handler_ranges:
        if start <= value < end:
            return states
    return []


def build_handler_ranges(jump_table: dict[int, str], function_end: int) -> list[tuple[int, int, list[int]]]:
    grouped: dict[int, list[int]] = defaultdict(list)
    for state, target in jump_table.items():
        grouped[int(target, 16)].append(state)
    starts = sorted(grouped)
    ranges: list[tuple[int, int, list[int]]] = []
    for index, start in enumerate(starts):
        end = starts[index + 1] if index + 1 < len(starts) else function_end
        ranges.append((start, end, grouped[start]))
    return ranges


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    code = args.code_bin.read_bytes()
    code_base = int(str(args.code_base), 0)
    jump_table_addr = int(str(args.jump_table), 0)
    first_state = int(args.first_state)
    case_count = int(args.case_count)
    function_end = int(str(args.function_end), 0)
    state_table = read_json(args.state_table, {})
    jump_table = read_jump_table(code, code_base, jump_table_addr, first_state, case_count)
    row_by_state = state_rows_by_value(state_table)
    sites_by_state = callsites_by_state(state_table)
    states_by_handler: dict[str, list[int]] = defaultdict(list)
    for state, target in jump_table.items():
        states_by_handler[target].append(state)
    handler_ranges = build_handler_ranges(jump_table, function_end)
    rows: list[dict[str, Any]] = []
    transition_edges: list[dict[str, Any]] = []
    for state in sorted(jump_table):
        target = jump_table[state]
        table_row = row_by_state.get(state, {})
        sites = sites_by_state.get(state, [])
        player_sites = [site for site in sites if site.get("function_entry") == "00473ef8"]
        for site in player_sites:
            source_states = handler_owner(str(site.get("callsite_addr", "")), handler_ranges)
            transition_edges.append(
                {
                    "source_states": ";".join(str(value) for value in source_states),
                    "target_state": state,
                    "callsite_addr": site.get("callsite_addr", ""),
                    "handler_target": target,
                    "target_flag_pattern": table_row.get("flag_pattern", ""),
                }
            )
        rows.append(
            {
                "state": state,
                "handler_target": target,
                "shared_handler_states": ";".join(str(value) for value in states_by_handler[target]),
                "handler_share_count": len(states_by_handler[target]),
                "observed_setter_callsite_count": len(sites),
                "player_setter_callsite_count": len(player_sites),
                "player_setter_callsites": ";".join(str(site.get("callsite_addr", "")) for site in player_sites),
                "flag_pattern": table_row.get("flag_pattern", ""),
                "used_by_player_function": bool(table_row.get("used_by_player_function", False)),
                "has_table_row": state in row_by_state,
            }
        )
    shared_handlers = {target: states for target, states in states_by_handler.items() if len(states) > 1}
    edge_counter = Counter((edge["source_states"], edge["target_state"]) for edge in transition_edges)
    summary = {
        "switch_entry": normalize_addr(args.switch_entry),
        "jump_table": f"{jump_table_addr:08x}",
        "first_state": first_state,
        "case_count": case_count,
        "switch_states": len(jump_table),
        "unique_handlers": len(states_by_handler),
        "shared_handlers": len(shared_handlers),
        "observed_switch_states_with_table_rows": sum(1 for row in rows if row["has_table_row"]),
        "player_used_switch_states": sum(1 for row in rows if row["used_by_player_function"]),
        "player_setter_callsite_count": sum(int(row["player_setter_callsite_count"]) for row in rows),
        "transition_edges": len(transition_edges),
        "unique_transition_edges": len(edge_counter),
        "next_gate": "Correlate switch handlers and transition edges with N64 source blocks before naming state values.",
    }
    return {
        "format": "oot3d_direct_state_mode_switch_map_v1",
        "inputs": {
            "code_bin": str(args.code_bin),
            "state_table": rel(args.state_table),
            "switch_entry": normalize_addr(args.switch_entry),
            "jump_table": f"{jump_table_addr:08x}",
        },
        "summary": summary,
        "rows": rows,
        "transition_edges": transition_edges,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct State/Mode Switch Map",
        "",
        "This report maps `FUN_00340bdc` state values onto the `oot3d_player_action_swing_bottle` switch handlers.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Switch entry | `{summary['switch_entry']}` |",
        f"| Jump table | `{summary['jump_table']}` |",
        f"| Switch states | {summary['switch_states']} |",
        f"| Unique handlers | {summary['unique_handlers']} |",
        f"| Shared handlers | {summary['shared_handlers']} |",
        f"| Player-used switch states | {summary['player_used_switch_states']} |",
        f"| Player setter callsites | {summary['player_setter_callsite_count']} |",
        f"| Transition edges | {summary['transition_edges']} |",
        f"| Unique transition edges | {summary['unique_transition_edges']} |",
        "",
        f"Next gate: {summary['next_gate']}",
        "",
        "## States",
        "",
        "| State | Handler | Shared states | Flags | Player setters | Setter callsites |",
        "| ---: | --- | --- | --- | ---: | --- |",
    ]
    for row in data["rows"]:
        lines.append(
            f"| {row['state']} | `{row['handler_target']}` | `{row['shared_handler_states']}` | "
            f"`{row['flag_pattern']}` | {row['player_setter_callsite_count']} | "
            f"`{row['player_setter_callsites']}` |"
        )
    lines.extend(
        [
            "",
            "## Transition Edges",
            "",
            "| Source handler states | Target state | Callsite | Target flags |",
            "| --- | ---: | --- | --- |",
        ]
    )
    for edge in data["transition_edges"]:
        lines.append(
            f"| `{edge['source_states']}` | {edge['target_state']} | "
            f"`{edge['callsite_addr']}` | `{edge['target_flag_pattern']}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--code-bin", type=Path, default=DEFAULT_CODE_BIN)
    parser.add_argument("--code-base", default="0x00100000")
    parser.add_argument("--state-table", type=Path, default=DEFAULT_STATE_TABLE)
    parser.add_argument("--switch-entry", default=DEFAULT_SWITCH_ENTRY)
    parser.add_argument("--jump-table", default=DEFAULT_JUMP_TABLE)
    parser.add_argument("--first-state", type=int, default=DEFAULT_FIRST_STATE)
    parser.add_argument("--case-count", type=int, default=DEFAULT_CASE_COUNT)
    parser.add_argument("--function-end", default="0x00475c70")
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()

    if not args.code_bin.is_file():
        raise SystemExit(f"missing code image: {args.code_bin}")
    if not args.state_table.is_file():
        raise SystemExit(f"missing state table report: {args.state_table}")

    data = build_report(args)
    fields = [
        "state",
        "handler_target",
        "shared_handler_states",
        "handler_share_count",
        "observed_setter_callsite_count",
        "player_setter_callsite_count",
        "player_setter_callsites",
        "flag_pattern",
        "used_by_player_function",
        "has_table_row",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct state/mode switch map: "
        f"{summary['switch_states']} states, {summary['unique_handlers']} handlers, "
        f"{summary['transition_edges']} transition edges"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
