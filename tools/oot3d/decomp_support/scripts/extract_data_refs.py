#!/usr/bin/env python3
"""Extract data/global references for decompilation candidate functions."""

from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
DISASSEMBLY = ROOT / "ghidra_export" / "disassembly.txt"
CALLGRAPH = ANALYSIS / "callgraph.json"
TRIAGE = ANALYSIS / "leaf_function_triage.csv"
LEAF_CANDIDATES = ANALYSIS / "leaf_function_candidates.json"

HEADER_RE = re.compile(r"^//\s+(?P<name>\S+)\s+@\s+(?P<entry>[0-9a-fA-F]+)\s*$")
DIRECT_LITERAL_RE = re.compile(r"\[(?:0x)?(?P<addr>[0-9a-fA-F]{6,8})\]")
PC_LITERAL_RE = re.compile(r"\[pc,#(?P<offset>0x[0-9a-fA-F]+|\d+)\]")
ABS_BRANCH_RE = re.compile(r"\b(?:b|bl|blx)\w*\s+(?:0x)?(?P<addr>[0-9a-fA-F]{6,8})\b", re.IGNORECASE)
DATA_RE = re.compile(r"\b(?P<prefix>DAT|PTR|s_|u_|FLOAT|DWORD|WORD|BYTE|LAB)_?(?P<addr>[0-9a-fA-F]{8})\b")


def normalize_entry(value: str) -> str:
    return value.strip().removeprefix("0x").lower().zfill(8)


def load_callgraph() -> dict[str, dict[str, object]]:
    callgraph = json.loads(CALLGRAPH.read_text(encoding="utf-8"))
    return {node["entry"].lower(): node for node in callgraph["nodes"]}


def load_triage() -> dict[str, dict[str, str]]:
    if not TRIAGE.is_file():
        return {}
    with TRIAGE.open(newline="", encoding="utf-8") as handle:
        return {row["entry"].lower(): row for row in csv.DictReader(handle)}


def load_leaf_candidates(limit: int) -> list[str]:
    if limit <= 0 or not LEAF_CANDIDATES.is_file():
        return []
    rows = json.loads(LEAF_CANDIDATES.read_text(encoding="utf-8"))
    return [row["entry"].lower() for row in rows[:limit]]


def load_disassembly_blocks() -> dict[str, dict[str, object]]:
    blocks: dict[str, dict[str, object]] = {}
    current_entry: str | None = None
    current_name = ""
    current_lines: list[str] = []

    def finish() -> None:
        if current_entry is None:
            return
        blocks[current_entry] = {"name": current_name, "lines": list(current_lines)}

    for line in DISASSEMBLY.read_text(encoding="utf-8").splitlines():
        header = HEADER_RE.match(line)
        if header:
            finish()
            current_entry = header.group("entry").lower()
            current_name = header.group("name")
            current_lines = []
            continue

        if current_entry is None:
            continue
        if not line.strip():
            finish()
            current_entry = None
            current_name = ""
            current_lines = []
            continue

        current_lines.append(line)

    finish()
    return blocks


def refs_from_disassembly(lines: list[str]) -> dict[str, object]:
    direct_literals: set[str] = set()
    pc_literals: list[str] = []
    branch_targets: set[str] = set()

    for line in lines:
        for match in DIRECT_LITERAL_RE.finditer(line):
            direct_literals.add(normalize_entry(match.group("addr")))
        pc_match = PC_LITERAL_RE.search(line)
        if pc_match:
            pc_literals.append(pc_match.group("offset").lower())
        branch_match = ABS_BRANCH_RE.search(line)
        if branch_match:
            branch_targets.add(normalize_entry(branch_match.group("addr")))

    return {
        "direct_literals": sorted(direct_literals),
        "pc_literal_count": len(pc_literals),
        "pc_literal_offsets": pc_literals,
        "branch_targets": sorted(branch_targets),
    }


def refs_from_decompiled(file_name: str) -> dict[str, object]:
    path = ROOT / file_name
    if not path.is_file():
        return {"symbol_refs": []}

    refs: dict[str, set[str]] = {}
    for match in DATA_RE.finditer(path.read_text(encoding="utf-8", errors="replace")):
        refs.setdefault(normalize_entry(match.group("addr")), set()).add(match.group(0))

    return {
        "symbol_refs": [
            {"address": address, "names": sorted(names)}
            for address, names in sorted(refs.items())
        ]
    }


def gather_entries(args: argparse.Namespace, triage: dict[str, dict[str, str]]) -> list[str]:
    entries: list[str] = []
    if args.from_triage:
        entries.extend(sorted(triage))
    entries.extend(load_leaf_candidates(args.leaf_limit))
    entries.extend(normalize_entry(entry) for entry in args.entries)

    seen: set[str] = set()
    ordered: list[str] = []
    for entry in entries:
        if entry not in seen:
            seen.add(entry)
            ordered.append(entry)
    return ordered


def make_markdown(rows: list[dict[str, object]], report_limit: int) -> str:
    shown = rows[:report_limit] if report_limit else rows
    lines = [
        "# Data Reference Candidates",
        "",
        "Generated from `scripts/extract_data_refs.py`.",
        "",
        f"- Functions analyzed: {len(rows)}",
        f"- Functions shown: {len(shown)}",
        "",
        "| Entry | Name | Triage | Direct literals | DAT/PTR refs | PC literals | Callers | File |",
        "| --- | --- | --- | --- | --- | ---: | ---: | --- |",
    ]
    for row in shown:
        symbol_refs = ", ".join(ref["address"] for ref in row["symbol_refs"]) or "-"
        direct_literals = ", ".join(row["direct_literals"]) or "-"
        triage_reason = row.get("triage_reason") or "-"
        if len(triage_reason) > 80:
            triage_reason = triage_reason[:77] + "..."
        display_row = dict(row)
        display_row["direct_literals"] = direct_literals
        display_row["symbol_refs"] = symbol_refs
        display_row["triage_reason"] = triage_reason.replace("|", "\\|")
        lines.append(
            "| {entry} | {name} | {triage_status}: {triage_reason} | {direct_literals} | {symbol_refs} | {pc_literal_count} | {caller_count} | {file} |".format(
                **display_row,
            )
        )
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("entries", nargs="*", help="Function entries to inspect.")
    parser.add_argument("--from-triage", action="store_true", help="Include entries from analysis/leaf_function_triage.csv.")
    parser.add_argument("--leaf-limit", type=int, default=80, help="Include the top N leaf candidates.")
    parser.add_argument("--report-limit", type=int, default=120, help="Limit rows written to the Markdown report; 0 writes all.")
    parser.add_argument("--out-json", type=Path, default=ANALYSIS / "data_ref_candidates.json")
    parser.add_argument("--out-md", type=Path, default=ANALYSIS / "data_ref_candidates.md")
    args = parser.parse_args()

    callgraph = load_callgraph()
    triage = load_triage()
    disassembly = load_disassembly_blocks()
    entries = gather_entries(args, triage)

    rows: list[dict[str, object]] = []
    for entry in entries:
        node = callgraph.get(entry)
        if not node:
            continue
        disasm_refs = refs_from_disassembly(disassembly.get(entry, {}).get("lines", []))
        decomp_refs = refs_from_decompiled(str(node.get("file", "")))
        triage_row = triage.get(entry, {})
        rows.append(
            {
                "entry": entry,
                "name": node["name"],
                "file": node["file"],
                "caller_count": node["caller_count"],
                "call_count": node["call_count"],
                "address_span": node["address_span"],
                "triage_status": triage_row.get("status", "-"),
                "triage_reason": triage_row.get("reason", ""),
                **disasm_refs,
                **decomp_refs,
            }
        )

    args.out_json.write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    args.out_md.write_text(make_markdown(rows, args.report_limit), encoding="utf-8")
    print(json.dumps({"data_ref_candidates": len(rows), "top_report": str(args.out_md.relative_to(ROOT))}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
