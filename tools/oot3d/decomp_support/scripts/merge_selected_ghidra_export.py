#!/usr/bin/env python3
"""Merge a selective Ghidra function export into the main function index."""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXPORT = ROOT / "ghidra_export"
DISASM_HEADER_RE = re.compile(r"^//\s+(?P<name>\S+)\s+@\s+(?P<entry>[0-9a-fA-F]+)\s*$", re.MULTILINE)


def read_rows(path: Path) -> tuple[list[str], list[dict[str, str]]]:
    with path.open(newline="", encoding="utf-8-sig") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames is None:
            raise ValueError(f"missing CSV header: {path}")
        return list(reader.fieldnames), list(reader)


def write_rows(path: Path, fieldnames: list[str], rows: list[dict[str, str]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        handle.write(",".join(fieldnames) + "\r\n")
        writer = csv.DictWriter(handle, fieldnames=fieldnames, quoting=csv.QUOTE_ALL)
        writer.writerows(rows)


def disassembly_blocks(text: str) -> dict[str, tuple[int, int, str]]:
    matches = list(DISASM_HEADER_RE.finditer(text))
    blocks: dict[str, tuple[int, int, str]] = {}
    for index, match in enumerate(matches):
        start = match.start()
        while start > 0 and text[start - 1] == "\n":
            start -= 1
        end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        entry = match.group("entry").lower()
        blocks[entry] = (start, end, text[start:end].strip("\n"))
    return blocks


def merge_disassembly(full: Path, selected: Path) -> tuple[int, int]:
    if not full.is_file() or not selected.is_file():
        return 0, 0

    full_text = full.read_text(encoding="utf-8")
    selected_text = selected.read_text(encoding="utf-8")
    full_blocks = disassembly_blocks(full_text)
    selected_blocks = disassembly_blocks(selected_text)

    replaced = 0
    appended = 0
    replacements: list[tuple[int, int, str]] = []
    append_blocks: list[str] = []
    for entry, (_, _, block) in selected_blocks.items():
        existing = full_blocks.get(entry)
        if existing:
            replacements.append((existing[0], existing[1], "\n\n" + block + "\n"))
            replaced += 1
        else:
            append_blocks.append(block)
            appended += 1

    for start, end, block in sorted(replacements, reverse=True):
        full_text = full_text[:start] + block + full_text[end:]
    if append_blocks:
        full_text = full_text.rstrip() + "\n\n" + "\n\n".join(append_blocks) + "\n"

    full.write_text(full_text, encoding="utf-8")
    return replaced, appended


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export-dir", type=Path, default=EXPORT)
    parser.add_argument("--functions", type=Path)
    parser.add_argument("--selected", type=Path)
    parser.add_argument("--disassembly", type=Path)
    parser.add_argument("--selected-disassembly", type=Path)
    args = parser.parse_args()

    functions = args.functions or (args.export_dir / "functions.csv")
    selected = args.selected or (args.export_dir / "functions_selected.csv")
    fieldnames, rows = read_rows(functions)
    selected_fieldnames, selected_rows = read_rows(selected)
    if fieldnames != selected_fieldnames:
        raise ValueError(f"field mismatch: {functions} vs {selected}")

    selected_by_entry = {row["entry"].lower(): row for row in selected_rows}
    replaced = 0
    merged: list[dict[str, str]] = []
    seen: set[str] = set()
    for row in rows:
        entry = row["entry"].lower()
        if entry in selected_by_entry:
            merged.append(selected_by_entry[entry])
            replaced += 1
            seen.add(entry)
        else:
            merged.append(row)

    appended = 0
    for entry, row in sorted(selected_by_entry.items(), key=lambda item: int(item[0], 16)):
        if entry not in seen:
            merged.append(row)
            appended += 1

    write_rows(functions, fieldnames, merged)
    print(f"merged selected function index rows: replaced={replaced} appended={appended}")
    disasm_replaced, disasm_appended = merge_disassembly(
        args.disassembly or (args.export_dir / "disassembly.txt"),
        args.selected_disassembly or (args.export_dir / "disassembly_selected.txt"),
    )
    if disasm_replaced or disasm_appended:
        print(f"merged selected disassembly blocks: replaced={disasm_replaced} appended={disasm_appended}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
