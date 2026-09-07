#!/usr/bin/env python3
"""Summarize OOT3D runtime JSONL traces and resolve dynamic callers."""

from __future__ import annotations

import argparse
import csv
import json
from bisect import bisect_right
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
FUNCTIONS_CSV = ROOT / "ghidra_export" / "functions.csv"
CALLGRAPH_JSON = ROOT / "analysis" / "callgraph.json"


def parse_hex(value: str | None) -> int | None:
    if not value:
        return None
    return int(value.removeprefix("0x"), 16)


def fmt_addr(value: int | None) -> str:
    if value is None:
        return ""
    return f"0x{value:08x}"


def load_functions() -> tuple[list[dict[str, Any]], dict[int, dict[str, Any]]]:
    functions: list[dict[str, Any]] = []
    with FUNCTIONS_CSV.open("r", encoding="utf-8", newline="") as handle:
        for row in csv.DictReader(handle):
            item = {
                "name": row["name"],
                "entry": int(row["entry"], 16),
                "body_min": int(row["body_min"], 16),
                "body_max": int(row["body_max"], 16),
            }
            functions.append(item)
    functions.sort(key=lambda item: (item["body_min"], item["body_max"]))
    return functions, {int(item["entry"]): item for item in functions}


def load_static_edges() -> set[tuple[int, int]]:
    graph = json.loads(CALLGRAPH_JSON.read_text(encoding="utf-8"))
    edges: set[tuple[int, int]] = set()
    for edge in graph["edges"]:
        edges.add((int(edge["caller"], 16), int(edge["callee"], 16)))
    return edges


def resolve_containing_function(
    functions: list[dict[str, Any]], starts: list[int], address: int | None
) -> dict[str, Any] | None:
    if address is None:
        return None
    best: dict[str, Any] | None = None
    best_key: tuple[int, int] | None = None
    for item in functions[: bisect_right(starts, address)]:
        if int(item["body_max"]) < address:
            continue
        key = (int(item["body_max"]) - int(item["body_min"]), -int(item["body_min"]))
        if best_key is None or key < best_key:
            best = item
            best_key = key
    return best


def load_events(trace_path: Path) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    with trace_path.open("r", encoding="utf-8") as handle:
        for line in handle:
            stripped = line.strip()
            if stripped:
                events.append(json.loads(stripped))
    return events


def summarize_trace(trace_path: Path) -> dict[str, Any]:
    functions, by_entry = load_functions()
    starts = [int(item["body_min"]) for item in functions]
    static_edges = load_static_edges()
    events = load_events(trace_path)

    function_counts: Counter[int] = Counter()
    dynamic_edges: Counter[tuple[int | None, int]] = Counter()
    unresolved_lrs: Counter[int] = Counter()
    first_tick: dict[int, int] = {}
    last_tick: dict[int, int] = {}

    for event in events:
        if event.get("event") != "function_enter":
            continue
        callee = parse_hex(event.get("function_entry") or event.get("pc"))
        if callee is None:
            continue
        lr = parse_hex(event.get("lr"))
        caller = resolve_containing_function(functions, starts, lr)
        if caller is None and lr is not None:
            caller = resolve_containing_function(functions, starts, lr - 4)
        caller_entry = int(caller["entry"]) if caller else None
        function_counts[callee] += 1
        dynamic_edges[(caller_entry, callee)] += 1
        if caller is None and lr is not None:
            unresolved_lrs[lr] += 1
        tick = event.get("tick")
        if isinstance(tick, int):
            first_tick.setdefault(callee, tick)
            last_tick[callee] = tick

    function_rows = []
    for entry, count in function_counts.most_common():
        func = by_entry.get(entry)
        function_rows.append(
            {
                "entry": fmt_addr(entry),
                "name": func["name"] if func else "",
                "events": count,
                "first_tick": first_tick.get(entry),
                "last_tick": last_tick.get(entry),
            }
        )

    edge_rows = []
    for (caller_entry, callee_entry), count in dynamic_edges.most_common():
        caller = by_entry.get(caller_entry) if caller_entry is not None else None
        callee = by_entry.get(callee_entry)
        static_match = caller_entry is not None and (caller_entry, callee_entry) in static_edges
        edge_rows.append(
            {
                "caller_entry": fmt_addr(caller_entry),
                "caller_name": caller["name"] if caller else "",
                "callee_entry": fmt_addr(callee_entry),
                "callee_name": callee["name"] if callee else "",
                "events": count,
                "static_callgraph_match": static_match,
            }
        )

    unresolved_rows = [
        {"lr": fmt_addr(lr), "events": count} for lr, count in unresolved_lrs.most_common(25)
    ]

    return {
        "trace": str(trace_path),
        "events": len(events),
        "function_entries": len(function_rows),
        "dynamic_edges": len(edge_rows),
        "static_edge_matches": sum(1 for row in edge_rows if row["static_callgraph_match"]),
        "unresolved_lr_sites": len(unresolved_lrs),
        "functions": function_rows,
        "edges": edge_rows,
        "unresolved_lrs": unresolved_rows,
    }


def write_markdown(summary: dict[str, Any], output_path: Path) -> None:
    lines = [
        "# Runtime Trace Caller Summary",
        "",
        f"- Trace: `{summary['trace']}`",
        f"- Events: {summary['events']}",
        f"- Function entries: {summary['function_entries']}",
        f"- Dynamic caller/callee edges: {summary['dynamic_edges']}",
        f"- Static callgraph matches: {summary['static_edge_matches']}",
        f"- Unresolved LR sites: {summary['unresolved_lr_sites']}",
        "",
        "## Functions",
        "",
        "| Entry | Name | Events | First tick | Last tick |",
        "| --- | --- | ---: | ---: | ---: |",
    ]
    for row in summary["functions"][:50]:
        lines.append(
            f"| `{row['entry']}` | `{row['name']}` | {row['events']} | {row['first_tick']} | {row['last_tick']} |"
        )

    lines.extend(
        [
            "",
            "## Dynamic Edges",
            "",
            "| Caller | Callee | Events | Static edge |",
            "| --- | --- | ---: | --- |",
        ]
    )
    for row in summary["edges"][:100]:
        caller = f"`{row['caller_entry']}` `{row['caller_name']}`" if row["caller_entry"] else "unresolved"
        callee = f"`{row['callee_entry']}` `{row['callee_name']}`"
        static_edge = "yes" if row["static_callgraph_match"] else "no"
        lines.append(f"| {caller} | {callee} | {row['events']} | {static_edge} |")

    if summary["unresolved_lrs"]:
        lines.extend(["", "## Unresolved LR Sites", "", "| LR | Events |", "| --- | ---: |"])
        for row in summary["unresolved_lrs"]:
            lines.append(f"| `{row['lr']}` | {row['events']} |")

    lines.append("")
    output_path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path, help="JSONL trace file")
    parser.add_argument("--json", type=Path, help="Optional JSON summary output")
    parser.add_argument("--markdown", type=Path, help="Optional Markdown summary output")
    args = parser.parse_args()

    summary = summarize_trace(args.trace)
    print(
        json.dumps(
            {
                "events": summary["events"],
                "function_entries": summary["function_entries"],
                "dynamic_edges": summary["dynamic_edges"],
                "static_edge_matches": summary["static_edge_matches"],
                "unresolved_lr_sites": summary["unresolved_lr_sites"],
            },
            indent=2,
        )
    )
    if args.json:
        args.json.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    if args.markdown:
        write_markdown(summary, args.markdown)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
