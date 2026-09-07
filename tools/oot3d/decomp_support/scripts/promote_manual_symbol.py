#!/usr/bin/env python3
"""Promote a Ghidra function entry into the manual symbol map.

This removes the repetitive CSV editing step from the decompilation workflow:
the tool looks up the current exported function name, verifies that the
maintained source contains the requested symbol, and appends or updates
symbols/manual_symbols.csv.
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FUNCTIONS = ROOT / "ghidra_export" / "functions.csv"
SYMBOLS = ROOT / "symbols" / "manual_symbols.csv"
FIELDS = ["entry", "old_name", "new_name", "kind", "confidence", "source_file", "notes"]


def normalize_entry(entry: str) -> str:
    value = entry.lower().removeprefix("0x")
    if len(value) != 8:
        raise ValueError(f"entry must be 8 hex digits: {entry}")
    int(value, 16)
    return value


def load_functions() -> dict[str, str]:
    with FUNCTIONS.open(newline="", encoding="utf-8") as f:
        return {row["entry"].lower(): row["name"] for row in csv.DictReader(f)}


def load_symbols() -> list[dict[str, str]]:
    if not SYMBOLS.exists():
        return []
    with SYMBOLS.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def write_symbols(rows: list[dict[str, str]]) -> None:
    rows = sorted(rows, key=lambda row: int(row["entry"], 16))
    with SYMBOLS.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--entry", required=True, help="Function entry address, for example 0036df4c")
    parser.add_argument("--new-name", required=True, help="Maintained symbol name")
    parser.add_argument("--source-file", required=True, help="Maintained source path relative to repo root")
    parser.add_argument("--confidence", default="medium", choices=["low", "medium", "high"])
    parser.add_argument("--kind", default="function")
    parser.add_argument("--notes", required=True)
    parser.add_argument("--update", action="store_true", help="Update an existing manual symbol entry")
    args = parser.parse_args()

    entry = normalize_entry(args.entry)
    functions = load_functions()
    if entry not in functions:
        raise SystemExit(f"entry {entry} is not present in {FUNCTIONS.relative_to(ROOT)}")

    source_path = ROOT / args.source_file
    if not source_path.is_file():
        raise SystemExit(f"source file does not exist: {args.source_file}")
    source_text = source_path.read_text(encoding="utf-8", errors="replace")
    if args.new_name not in source_text:
        raise SystemExit(f"source file {args.source_file} does not contain symbol {args.new_name}")

    rows = load_symbols()
    existing = [row for row in rows if row["entry"].lower() == entry]
    if existing and not args.update:
        raise SystemExit(f"entry {entry} already exists in {SYMBOLS.relative_to(ROOT)}; pass --update to modify it")
    duplicate_name = [
        row for row in rows if row["entry"].lower() != entry and row["new_name"] == args.new_name
    ]
    if duplicate_name:
        raise SystemExit(
            f"symbol name {args.new_name} already belongs to entry {duplicate_name[0]['entry']}; "
            "choose a unique reviewed name"
        )

    new_row = {
        "entry": entry,
        "old_name": existing[0]["old_name"] if existing else functions[entry],
        "new_name": args.new_name,
        "kind": args.kind,
        "confidence": args.confidence,
        "source_file": args.source_file.replace("\\", "/"),
        "notes": args.notes,
    }

    rows = [row for row in rows if row["entry"].lower() != entry]
    rows.append(new_row)
    write_symbols(rows)
    print(f"promoted {entry}: {new_row['old_name']} -> {args.new_name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
