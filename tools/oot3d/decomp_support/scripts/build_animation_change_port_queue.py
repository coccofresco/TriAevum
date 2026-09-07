#!/usr/bin/env python3
"""Build a batch queue from resolved OOT3D Animation_Change ABI callsites."""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
RAW_REG_RE = re.compile(r"\b[rs]\d+\b|\?")


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except ValueError:
        return str(path).replace("\\", "/")


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        return []
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def read_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    return json.loads(path.read_text(encoding="utf-8"))


def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def expression_score(expr: str) -> int:
    if not expr:
        return 0
    if RAW_REG_RE.search(expr):
        return 0
    if "rand" in expr:
        return 2
    if "lastFrame" in expr:
        return 2
    return 3


def row_resolved(row: dict[str, str]) -> bool:
    if row.get("resolved_abi") == "True":
        return True
    return all(
        expression_score(row.get(field, "")) > 0
        for field in ("play_speed_expr", "start_frame_expr", "end_frame_expr", "morph_expr")
    )


def classify_group(group: dict[str, Any]) -> str:
    mapped_status = group["mapped_status"]
    resolved = int(group["resolved_calls"])
    unresolved = int(group["unresolved_calls"])
    callsites = int(group["target_calls"])
    span = int(group["address_span"])
    line_count = int(group["line_count"])
    has_source = bool(group["port_file"])

    if mapped_status == "matched-c":
        return "mapped-matched-template"
    if mapped_status and resolved and unresolved == 0 and has_source:
        return "mapped-source-ready"
    if mapped_status and resolved:
        return "mapped-partial-abi"
    if resolved >= 2 and span <= 0x900:
        return "unmapped-batch-family"
    if resolved and span <= 0x180 and line_count <= 80:
        return "unmapped-small-resolved"
    if resolved:
        return "unmapped-resolved"
    if callsites:
        return "abi-unresolved"
    return "manual"


def priority_score(group: dict[str, Any]) -> int:
    category_weight = {
        "mapped-source-ready": 1000,
        "mapped-partial-abi": 900,
        "unmapped-small-resolved": 800,
        "unmapped-batch-family": 700,
        "mapped-matched-template": 500,
        "unmapped-resolved": 400,
        "abi-unresolved": 100,
    }.get(group["category"], 0)
    resolved = int(group["resolved_calls"])
    exact_literals = int(group["literal_refs"])
    span = int(group["address_span"])
    return category_weight + resolved * 20 + exact_literals * 3 - min(span // 0x100, 40)


def build_rows() -> tuple[list[dict[str, Any]], dict[str, Any]]:
    abi_rows = read_csv(ROOT / "analysis" / "animation_change_abi_calls.csv")
    port_rows = read_csv(ROOT / "metadata" / "n64_port_map.csv")
    functions = {row["entry"].lower(): row for row in read_json(ROOT / "analysis" / "functions_enriched.json", [])}
    map_by_entry = {row.get("oot3d_entry", "").lower(): row for row in port_rows}

    groups: dict[str, dict[str, Any]] = {}
    signatures_by_entry: dict[str, Counter[str]] = defaultdict(Counter)
    unresolved_examples: dict[str, list[str]] = defaultdict(list)
    resolved_examples: dict[str, list[str]] = defaultdict(list)

    for row in abi_rows:
        entry = row.get("entry", "").lower()
        if not entry:
            continue
        info = functions.get(entry, {})
        mapped = map_by_entry.get(entry, {})
        group = groups.setdefault(
            entry,
            {
                "entry": entry,
                "function": row.get("function", ""),
                "mapped_status": mapped.get("status", ""),
                "n64_name": mapped.get("n64_name", ""),
                "n64_source": mapped.get("n64_source", ""),
                "port_file": mapped.get("port_file", ""),
                "address_span": int(info.get("address_span", 0) or 0),
                "line_count": int(info.get("line_count", 0) or 0),
                "call_count": int(info.get("call_count", 0) or 0),
                "caller_count": int(info.get("caller_count", 0) or 0),
                "target_calls": 0,
                "resolved_calls": 0,
                "unresolved_calls": 0,
                "literal_refs": 0,
                "rand_calls": 0,
            },
        )
        group["target_calls"] += 1
        resolved = row_resolved(row)
        if resolved:
            group["resolved_calls"] += 1
            signature = (
                f"{row.get('play_speed_expr','')};{row.get('start_frame_expr','')};"
                f"{row.get('end_frame_expr','')};{row.get('morph_expr','')}"
            )
            signatures_by_entry[entry][signature] += 1
            if len(resolved_examples[entry]) < 3:
                resolved_examples[entry].append(
                    f"{row.get('call_addr')} anim={row.get('anim')} mode={row.get('mode')} {signature}"
                )
        else:
            group["unresolved_calls"] += 1
            if len(unresolved_examples[entry]) < 3:
                unresolved_examples[entry].append(
                    f"{row.get('call_addr')} anim={row.get('anim')} mode={row.get('mode')} {row.get('pattern')}"
                )
        group["literal_refs"] += len([item for item in row.get("literal_refs", "").split() if item])
        group["rand_calls"] += int(row.get("rand_calls", 0) or 0)

    rows: list[dict[str, Any]] = []
    for entry, group in groups.items():
        group["category"] = classify_group(group)
        group["priority_score"] = priority_score(group)
        group["top_signatures"] = " | ".join(
            f"{signature}:{count}" for signature, count in signatures_by_entry[entry].most_common(3)
        )
        group["resolved_examples"] = " | ".join(resolved_examples[entry])
        group["unresolved_examples"] = " | ".join(unresolved_examples[entry])
        rows.append(group)

    rows.sort(key=lambda row: (-int(row["priority_score"]), row["entry"]))
    summary = {
        "target_functions": len(rows),
        "target_calls": sum(int(row["target_calls"]) for row in rows),
        "resolved_functions": sum(1 for row in rows if int(row["resolved_calls"]) > 0),
        "fully_resolved_functions": sum(1 for row in rows if int(row["resolved_calls"]) and int(row["unresolved_calls"]) == 0),
        "categories": dict(Counter(row["category"] for row in rows).most_common()),
        "mapped_ready": sum(1 for row in rows if row["category"] == "mapped-source-ready"),
        "unmapped_small_resolved": sum(1 for row in rows if row["category"] == "unmapped-small-resolved"),
        "unmapped_batch_family": sum(1 for row in rows if row["category"] == "unmapped-batch-family"),
    }
    return rows, summary


def write_markdown(path: Path, rows: list[dict[str, Any]], summary: dict[str, Any]) -> None:
    lines = [
        "# Animation Change Port Queue",
        "",
        "Queue derived from resolved `FUN_00375c08` ABI windows. It ranks functions for batch N64-source porting or new map expansion.",
        "",
        "## Summary",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Functions with Animation_Change target calls | {summary['target_functions']} |",
        f"| Target Animation_Change calls | {summary['target_calls']} |",
        f"| Functions with at least one resolved ABI call | {summary['resolved_functions']} |",
        f"| Functions with all ABI calls resolved | {summary['fully_resolved_functions']} |",
        f"| Mapped source-ready groups | {summary['mapped_ready']} |",
        f"| Unmapped small resolved groups | {summary['unmapped_small_resolved']} |",
        f"| Unmapped batch-family groups | {summary['unmapped_batch_family']} |",
        "",
        "Categories: " + ", ".join(f"{key}: {value}" for key, value in summary["categories"].items()),
        "",
        "## Top Queue",
        "",
        "| Score | Category | Entry | Function | Calls | Resolved | Span | Lines | Map | Top signatures |",
        "| ---: | --- | --- | --- | ---: | ---: | ---: | ---: | --- | --- |",
    ]

    for row in rows[:80]:
        map_label = row["mapped_status"] or "-"
        if row["n64_name"]:
            map_label = f"{map_label} `{row['n64_name']}`"
        lines.append(
            f"| {row['priority_score']} | `{row['category']}` | `{row['entry']}` | `{row['function']}` | "
            f"{row['target_calls']} | {row['resolved_calls']} | {row['address_span']} | {row['line_count']} | "
            f"{map_label} | `{row['top_signatures']}` |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-csv", type=Path, default=ROOT / "analysis" / "animation_change_port_queue.csv")
    parser.add_argument("--out-json", type=Path, default=ROOT / "analysis" / "animation_change_port_queue.json")
    parser.add_argument("--out-md", type=Path, default=ROOT / "analysis" / "animation_change_port_queue.md")
    args = parser.parse_args()

    rows, summary = build_rows()
    fieldnames = [
        "priority_score",
        "category",
        "entry",
        "function",
        "mapped_status",
        "n64_name",
        "n64_source",
        "port_file",
        "target_calls",
        "resolved_calls",
        "unresolved_calls",
        "literal_refs",
        "rand_calls",
        "address_span",
        "line_count",
        "call_count",
        "caller_count",
        "top_signatures",
        "resolved_examples",
        "unresolved_examples",
    ]
    write_csv(args.out_csv, rows, fieldnames)
    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps({"summary": summary, "rows": rows}, indent=2) + "\n", encoding="utf-8")
    write_markdown(args.out_md, rows, summary)
    print(
        "animation queue: "
        f"{summary['resolved_functions']} resolved functions, "
        f"{summary['fully_resolved_functions']} fully resolved; "
        f"{summary['mapped_ready']} mapped-ready, "
        f"{summary['unmapped_small_resolved']} small unmapped"
    )
    print(rel(args.out_md))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
