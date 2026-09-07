#!/usr/bin/env python3
"""Rank small leaf functions that are good candidates for maintained source."""

from __future__ import annotations

import csv
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ANALYSIS = ROOT / "analysis"
DISASSEMBLY = ROOT / "ghidra_export" / "disassembly.txt"
CALLGRAPH = ANALYSIS / "callgraph.json"
SYMBOLS = ROOT / "symbols" / "manual_symbols.csv"
TRIAGE = ANALYSIS / "leaf_function_triage.csv"

HEADER_RE = re.compile(r"^//\s+(?P<name>\S+)\s+@\s+(?P<entry>[0-9a-fA-F]+)\s*$")
INSN_RE = re.compile(r"^[0-9a-fA-F]+:\s+(?P<op>[A-Za-z0-9_.]+)")
DIRECT_LITERAL_RE = re.compile(r"\[(?:0x)?(?P<addr>[0-9a-fA-F]{6,8})\]")
PC_LITERAL_RE = re.compile(r"\[pc,#(?P<offset>0x[0-9a-fA-F]+|\d+)\]")
DATA_RE = re.compile(r"\b(?:DAT|PTR|s_|u_|FLOAT|DWORD|WORD|BYTE)_?(?P<addr>[0-9a-fA-F]{8})\b")


def load_manual_entries() -> set[str]:
    if not SYMBOLS.is_file():
        return set()

    with SYMBOLS.open(newline="", encoding="utf-8") as handle:
        return {row["entry"].lower() for row in csv.DictReader(handle) if row.get("kind") == "function"}


def load_triage() -> dict[str, dict[str, str]]:
    if not TRIAGE.is_file():
        return {}

    with TRIAGE.open(newline="", encoding="utf-8") as handle:
        return {row["entry"].lower(): row for row in csv.DictReader(handle)}


def load_disassembly_ops() -> dict[str, dict[str, object]]:
    functions: dict[str, dict[str, object]] = {}
    current_entry: str | None = None

    for line in DISASSEMBLY.read_text(encoding="utf-8").splitlines():
        header = HEADER_RE.match(line)
        if header:
            current_entry = header.group("entry").lower()
            functions[current_entry] = {"ops": [], "direct_literals": set(), "pc_literal_count": 0}
            continue

        if current_entry is None:
            continue
        if not line.strip():
            current_entry = None
            continue

        insn = INSN_RE.match(line)
        if insn:
            functions[current_entry]["ops"].append(insn.group("op").lower())
        for literal in DIRECT_LITERAL_RE.finditer(line):
            functions[current_entry]["direct_literals"].add(literal.group("addr").lower().zfill(8))
        if PC_LITERAL_RE.search(line):
            functions[current_entry]["pc_literal_count"] += 1

    return functions


def count_decompiled_data_refs(file_name: str) -> int:
    path = ROOT / file_name
    if not path.is_file():
        return 0
    refs = {match.group("addr").lower() for match in DATA_RE.finditer(path.read_text(encoding="utf-8", errors="replace"))}
    return len(refs)


def classify_ops(ops: list[str]) -> tuple[str, int]:
    branches = sum(1 for op in ops if op.startswith("b") and op not in {"bic", "biceq", "bicne"})
    vfp = sum(1 for op in ops if op.startswith("v"))
    memory = sum(1 for op in ops if op.startswith(("ldr", "str", "ldm", "stm", "vldm", "vstm")))
    arithmetic = sum(1 for op in ops if op.startswith(("add", "sub", "mul", "mla", "orr", "and", "eor", "cmp", "tst", "mov", "cpy", "bic")))

    if branches <= 1 and vfp and memory:
        return "vfp-leaf", 0
    if branches <= 1 and memory and arithmetic:
        return "memory-arithmetic-leaf", 1
    if branches <= 2 and memory:
        return "memory-leaf", 2
    if branches <= 1:
        return "simple-leaf", 3
    return "branchy-leaf", 8


def main() -> int:
    manual_entries = load_manual_entries()
    triage = load_triage()
    disasm_by_entry = load_disassembly_ops()
    callgraph = json.loads(CALLGRAPH.read_text(encoding="utf-8"))

    rows: list[dict[str, object]] = []
    for node in callgraph["nodes"]:
        entry = node["entry"].lower()
        if entry in manual_entries:
            continue
        if triage.get(entry, {}).get("status") == "defer":
            continue
        if int(node["call_count"]) != 0:
            continue
        span = int(node["address_span"])
        if span > 96:
            continue

        disasm = disasm_by_entry.get(entry, {})
        ops = disasm.get("ops", [])
        if not ops:
            continue

        category, category_score = classify_ops(ops)
        direct_literal_count = len(disasm.get("direct_literals", set()))
        pc_literal_count = int(disasm.get("pc_literal_count", 0))
        symbol_ref_count = count_decompiled_data_refs(node["file"])
        data_ref_score = (direct_literal_count + symbol_ref_count + pc_literal_count) * 250
        score = category_score * 1000 + data_ref_score + span + (int(node["caller_count"]) * -3)
        rows.append(
            {
                "entry": entry,
                "name": node["name"],
                "category": category,
                "score": score,
                "address_span": span,
                "instruction_count": len(ops),
                "caller_count": node["caller_count"],
                "direct_literal_count": direct_literal_count,
                "symbol_ref_count": symbol_ref_count,
                "pc_literal_count": pc_literal_count,
                "file": node["file"],
                "ops": ops,
            }
        )

    rows.sort(key=lambda row: (int(row["score"]), int(row["address_span"]), str(row["entry"])))

    out_json = ANALYSIS / "leaf_function_candidates.json"
    out_md = ANALYSIS / "leaf_function_candidates.md"
    out_json.write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")

    lines = [
        "# Leaf Function Candidates",
        "",
        "Generated from `scripts/rank_leaf_functions.py`.",
        "",
        f"- Candidates: {len(rows)}",
        f"- Deferred by triage: {sum(1 for row in triage.values() if row.get('status') == 'defer')}",
        "",
        "| Entry | Name | Category | Span | Insns | Callers | Data refs | Score | File |",
        "| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | --- |",
    ]
    for row in rows[:80]:
        lines.append(
            "| {entry} | {name} | {category} | {address_span} | {instruction_count} | {caller_count} | {data_refs} | {score} | {file} |".format(
                data_refs=int(row["direct_literal_count"]) + int(row["symbol_ref_count"]) + int(row["pc_literal_count"]),
                **row
            )
        )
    lines.append("")
    out_md.write_text("\n".join(lines), encoding="utf-8")
    print(json.dumps({"leaf_function_candidates": len(rows), "top_report": str(out_md.relative_to(ROOT))}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
