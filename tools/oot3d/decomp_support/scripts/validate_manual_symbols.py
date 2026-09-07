#!/usr/bin/env python3
"""Validate manually named symbols against the Ghidra baseline."""

from __future__ import annotations

import csv
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FUNCTIONS = ROOT / "ghidra_export" / "functions.csv"
SYMBOLS = ROOT / "symbols" / "manual_symbols.csv"


def main() -> int:
    with FUNCTIONS.open(newline="", encoding="utf-8") as f:
        functions = {row["entry"].lower(): row for row in csv.DictReader(f)}

    errors: list[str] = []
    seen_entries: set[str] = set()
    seen_names: dict[str, str] = {}
    source_cache: dict[str, str | None] = {}
    with SYMBOLS.open(newline="", encoding="utf-8") as f:
        for row_number, row in enumerate(csv.DictReader(f), start=2):
            entry = row["entry"].lower()
            old_name = row["old_name"]
            new_name = row["new_name"]
            source_file = row["source_file"]

            if entry in seen_entries:
                errors.append(f"row {row_number}: duplicate entry {entry}")
            seen_entries.add(entry)

            existing_entry = seen_names.get(new_name)
            if existing_entry is not None:
                errors.append(f"row {row_number}: duplicate symbol name {new_name} for entries {existing_entry} and {entry}")
            seen_names[new_name] = entry

            function = functions.get(entry)
            if function is None:
                errors.append(f"row {row_number}: entry {entry} is not present in ghidra_export/functions.csv")
                continue

            exported_name = function["name"]
            if exported_name not in {old_name, new_name}:
                errors.append(
                    f"row {row_number}: exported name {exported_name} is neither old_name {old_name} nor new_name {new_name} for {entry}"
                )

            source = ROOT / source_file
            source_text = source_cache.get(source_file)
            if source_text is None and source_file not in source_cache:
                source_text = source.read_text(encoding="utf-8", errors="replace") if source.is_file() else None
                source_cache[source_file] = source_text
            if source_text is None:
                errors.append(f"row {row_number}: source file {source_file} does not exist")
            else:
                if new_name not in source_text:
                    errors.append(f"row {row_number}: source file {source_file} does not contain {new_name}")

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    print(f"validated {len(seen_entries)} manual symbols")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
