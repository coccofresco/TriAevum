#!/usr/bin/env python3
"""Validate data-symbol labels against the extracted OOT3D code image."""

from __future__ import annotations

import csv
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SYMBOLS = ROOT / "symbols" / "data_symbols.csv"
CODE_BIN = ROOT.parent / "work" / "extract" / "exefs" / "code.bin"
CODE_BASE = 0x00100000
FIELDS = ["address", "old_name", "new_name", "kind", "confidence", "source_file", "notes"]
NAME_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def main() -> int:
    if not SYMBOLS.is_file():
        print(f"missing {SYMBOLS.relative_to(ROOT)}", file=sys.stderr)
        return 1
    if not CODE_BIN.is_file():
        print(f"missing code image {CODE_BIN}", file=sys.stderr)
        return 1
    code_size = CODE_BIN.stat().st_size
    code_min = CODE_BASE
    code_max = CODE_BASE + code_size

    errors: list[str] = []
    seen_addresses: set[str] = set()
    seen_names: set[str] = set()
    with SYMBOLS.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames != FIELDS:
            errors.append(f"unexpected header: {reader.fieldnames}")
        for row_number, row in enumerate(reader, start=2):
            address = row["address"].lower().removeprefix("0x")
            name = row["new_name"]
            if not re.fullmatch(r"[0-9a-f]{8}", address):
                errors.append(f"row {row_number}: invalid address {row['address']}")
                continue
            value = int(address, 16)
            if not (code_min <= value < code_max):
                errors.append(f"row {row_number}: address {address} outside code image range")
            if address in seen_addresses:
                errors.append(f"row {row_number}: duplicate address {address}")
            seen_addresses.add(address)
            if not NAME_RE.fullmatch(name):
                errors.append(f"row {row_number}: invalid symbol name {name}")
            if name in seen_names:
                errors.append(f"row {row_number}: duplicate symbol name {name}")
            seen_names.add(name)
            source = ROOT / row["source_file"]
            if not source.is_file():
                errors.append(f"row {row_number}: source file does not exist: {row['source_file']}")
            if row["confidence"] not in {"low", "medium", "high"}:
                errors.append(f"row {row_number}: invalid confidence {row['confidence']}")

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print(f"validated {len(seen_addresses)} data symbols")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
