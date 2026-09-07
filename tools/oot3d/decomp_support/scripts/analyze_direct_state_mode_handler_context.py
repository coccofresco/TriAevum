#!/usr/bin/env python3
"""Summarize per-handler context for the 00473ef8 state/mode switch."""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

from classify_direct_tail_call_target_identities import DEFAULT_DISASSEMBLY, normalize_addr, read_json
from probe_direct_source_subregions import rel


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SWITCH_MAP = ROOT / "analysis" / "direct_state_mode_switch_map.json"
DEFAULT_MANUAL_SYMBOLS = ROOT / "symbols" / "manual_symbols.csv"
DEFAULT_OUT_JSON = ROOT / "analysis" / "direct_state_mode_handler_context.json"
DEFAULT_OUT_CSV = ROOT / "analysis" / "direct_state_mode_handler_context.csv"
DEFAULT_OUT_MD = ROOT / "analysis" / "direct_state_mode_handler_context.md"
INSN_RE = re.compile(r"^(?P<addr>[0-9a-fA-F]{8}):\s+(?P<op>.+?)\s*$")
CALL_RE = re.compile(r"\bblx?\s+(?P<dest>0x[0-9a-fA-F]+)\b")
BRANCH_RE = re.compile(r"\bb(?:x|l|l?x|eq|ne|cs|cc|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)?\s+(?P<dest>0x[0-9a-fA-F]+)\b")
OFFSET_RE = re.compile(r"\[(?P<reg>r4|r5|sl|r6|r8),\s*#(?P<offset>\d+)\]")


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


def read_manual_symbols(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    if not path.is_file():
        return result
    with path.open(newline="", encoding="utf-8-sig") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            if row.get("kind") == "function":
                result[normalize_addr(row.get("entry"))] = str(row.get("new_name") or row.get("old_name") or "")
    return result


def read_disassembly(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    if not path.is_file():
        return rows
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = INSN_RE.match(raw)
        if not match:
            continue
        rows.append({"addr": normalize_addr(match.group("addr")), "op": match.group("op").strip()})
    return rows


def rows_in_range(disasm: list[dict[str, Any]], start: int, end: int) -> list[dict[str, Any]]:
    return [row for row in disasm if start <= int(row["addr"], 16) < end]


def handler_ranges(switch_map: dict[str, Any], function_end: int) -> dict[str, tuple[int, int, list[int]]]:
    grouped: dict[str, list[int]] = defaultdict(list)
    for row in switch_map.get("rows", []):
        if isinstance(row, dict) and row.get("handler_target") and row.get("state") is not None:
            grouped[normalize_addr(row["handler_target"])].append(int(row["state"]))
    starts = sorted(int(addr, 16) for addr in grouped)
    result: dict[str, tuple[int, int, list[int]]] = {}
    for index, start in enumerate(starts):
        end = starts[index + 1] if index + 1 < len(starts) else function_end
        addr = f"{start:08x}"
        result[addr] = (start, end, sorted(grouped[addr]))
    return result


def transition_index(switch_map: dict[str, Any]) -> tuple[dict[int, list[dict[str, Any]]], dict[str, list[dict[str, Any]]]]:
    incoming: dict[int, list[dict[str, Any]]] = defaultdict(list)
    outgoing_by_handler: dict[str, list[dict[str, Any]]] = defaultdict(list)
    state_to_handler = {
        int(row["state"]): normalize_addr(row["handler_target"])
        for row in switch_map.get("rows", [])
        if isinstance(row, dict) and row.get("state") is not None and row.get("handler_target")
    }
    for edge in switch_map.get("transition_edges", []):
        if not isinstance(edge, dict):
            continue
        target_state = int(edge.get("target_state", 0))
        incoming[target_state].append(edge)
        source_states = [
            int(part)
            for part in str(edge.get("source_states", "")).split(";")
            if part.strip().isdigit()
        ]
        for state in source_states:
            handler = state_to_handler.get(state)
            if handler:
                outgoing_by_handler[handler].append(edge)
    return incoming, outgoing_by_handler


def classify_features(calls: list[str], offsets: list[str], ops: list[str]) -> list[str]:
    features: list[str] = []
    call_set = set(calls)
    offset_set = set(offsets)
    op_text = "\n".join(ops)
    if "Message_ContinueTextbox" in call_set:
        features.append("message_continue")
    if "00340bdc" in call_set or "FUN_00340bdc" in call_set:
        features.append("state_transition")
    if "003725e0" in call_set or "FUN_003725e0" in call_set:
        features.append("state_reset_wrapper")
    if any(symbol in call_set for symbol in ["003371d8", "FUN_003371d8"]):
        features.append("item_record_fetch")
    if any(symbol in call_set for symbol in ["003523dc", "FUN_003523dc"]):
        features.append("screen_effect_or_cutscene_gate")
    if "r4#480" in offset_set:
        features.append("item_record")
    if "r4#496" in offset_set:
        features.append("state_byte")
    if "r4#723" in offset_set:
        features.append("timer_or_substate_byte")
    if "r4#768" in offset_set or "r4#769" in offset_set:
        features.append("item_or_magic_substate")
    if "r4#220" in offset_set or "r4#224" in offset_set or "r4#226" in offset_set:
        features.append("item_action_fields")
    if "vldr" in op_text or "vstr" in op_text:
        features.append("float_or_vector_math")
    if not features:
        features.append("unclassified")
    return features


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    switch_map = read_json(args.switch_map, {})
    symbols = read_manual_symbols(args.manual_symbols)
    disasm = read_disassembly(args.disassembly)
    function_end = int(str(args.function_end), 0)
    ranges = handler_ranges(switch_map, function_end)
    _, outgoing_by_handler = transition_index(switch_map)
    rows: list[dict[str, Any]] = []
    feature_counter: Counter[str] = Counter()
    for handler, (start, end, states) in sorted(ranges.items()):
        body = rows_in_range(disasm, start, end)
        calls_raw: list[str] = []
        calls_named: list[str] = []
        branches: list[str] = []
        offsets: list[str] = []
        for row in body:
            op = str(row["op"]).lower()
            call = CALL_RE.search(op)
            if call:
                dest = normalize_addr(call.group("dest"))
                calls_raw.append(dest)
                calls_named.append(symbols.get(dest) or f"FUN_{dest}")
            branch = BRANCH_RE.search(op)
            if branch and not op.startswith("bl"):
                branches.append(normalize_addr(branch.group("dest")))
            for offset in OFFSET_RE.finditer(op):
                offsets.append(f"{offset.group('reg')}#{offset.group('offset')}")
        features = classify_features(calls_named, offsets, [str(row["op"]).lower() for row in body])
        feature_counter.update(features)
        outgoing = outgoing_by_handler.get(handler, [])
        rows.append(
            {
                "handler": handler,
                "range": f"{start:08x}-{end:08x}",
                "states": ";".join(str(state) for state in states),
                "state_count": len(states),
                "instruction_count": len(body),
                "call_count": len(calls_raw),
                "calls": ";".join(calls_named),
                "raw_call_targets": ";".join(calls_raw),
                "branch_count": len(branches),
                "local_branch_targets": ";".join(branches),
                "offsets": ";".join(sorted(set(offsets))),
                "feature_tags": ";".join(features),
                "outgoing_transition_count": len(outgoing),
                "outgoing_targets": ";".join(str(edge.get("target_state", "")) for edge in outgoing),
                "body_excerpt": " | ".join(f"{row['addr']}: {row['op']}" for row in body[:12]),
            }
        )
    summary = {
        "handlers": len(rows),
        "classified_handlers": sum(1 for row in rows if row["feature_tags"] != "unclassified"),
        "unclassified_handlers": sum(1 for row in rows if row["feature_tags"] == "unclassified"),
        "handlers_with_outgoing_transitions": sum(1 for row in rows if int(row["outgoing_transition_count"]) > 0),
        "feature_counts": dict(sorted(feature_counter.items())),
        "next_gate": "Match handler feature clusters to N64 source blocks before assigning final state names.",
    }
    return {
        "format": "oot3d_direct_state_mode_handler_context_v1",
        "inputs": {
            "switch_map": rel(args.switch_map),
            "disassembly": rel(args.disassembly),
            "manual_symbols": rel(args.manual_symbols),
        },
        "summary": summary,
        "rows": rows,
    }


def write_markdown(path: Path, data: dict[str, Any]) -> None:
    summary = data["summary"]
    lines = [
        "# Direct State/Mode Handler Context",
        "",
        "This report summarizes per-handler call, offset, and transition context for the `00473ef8` state/mode switch.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Handlers | {summary['handlers']} |",
        f"| Classified handlers | {summary['classified_handlers']} |",
        f"| Unclassified handlers | {summary['unclassified_handlers']} |",
        f"| Handlers with outgoing transitions | {summary['handlers_with_outgoing_transitions']} |",
        "",
        f"Next gate: {summary['next_gate']}",
        "",
        "## Feature Counts",
        "",
        "| Feature | Count |",
        "| --- | ---: |",
    ]
    for feature, count in summary["feature_counts"].items():
        lines.append(f"| `{feature}` | {count} |")
    lines.extend(
        [
            "",
            "## Handlers",
            "",
            "| Handler | States | Features | Calls | Offsets | Outgoing states |",
            "| --- | --- | --- | --- | --- | --- |",
        ]
    )
    for row in data["rows"]:
        lines.append(
            f"| `{row['handler']}` | `{row['states']}` | `{row['feature_tags']}` | "
            f"`{row['calls']}` | `{row['offsets']}` | `{row['outgoing_targets']}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--switch-map", type=Path, default=DEFAULT_SWITCH_MAP)
    parser.add_argument("--disassembly", type=Path, default=DEFAULT_DISASSEMBLY)
    parser.add_argument("--manual-symbols", type=Path, default=DEFAULT_MANUAL_SYMBOLS)
    parser.add_argument("--function-end", default="0x00475c70")
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_OUT_CSV)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()
    if not args.switch_map.is_file():
        raise SystemExit(f"missing switch map: {args.switch_map}")
    if not args.disassembly.is_file():
        raise SystemExit(f"missing disassembly: {args.disassembly}")
    data = build_report(args)
    fields = [
        "handler",
        "range",
        "states",
        "state_count",
        "instruction_count",
        "call_count",
        "calls",
        "raw_call_targets",
        "branch_count",
        "local_branch_targets",
        "offsets",
        "feature_tags",
        "outgoing_transition_count",
        "outgoing_targets",
        "body_excerpt",
    ]
    write_json(args.out_json, data)
    write_csv(args.out_csv, data["rows"], fields)
    write_markdown(args.out_md, data)
    summary = data["summary"]
    print(
        "direct state/mode handler context: "
        f"{summary['handlers']} handlers, {summary['classified_handlers']} classified, "
        f"{summary['unclassified_handlers']} unclassified"
    )
    print(str(args.out_md.relative_to(ROOT)).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
