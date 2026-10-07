#!/usr/bin/env python3
"""Turns a raw Ghidra function inventory (ghidra_inventory.py) into the one
the A32 generators accept: one contiguous, disjoint, word-aligned interval
per function inside the executable text.

Each interval runs from the entry to the body's last address, cut at the next
function's entry. Entries outside the text, not word-aligned (Thumb) or
duplicated are dropped and reported.

usage: normalize_inventory.py raw.csv manifest.json output.csv [--prefix mm3d]
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path


MAINTAINED_FIELDS = (
    "module", "logical_file", "owner", "original_source", "maintained_name", "maintained_kind",
    "maintained_confidence", "maintained_source_file", "maintained_notes", "confidence",
    "evidence", "status",
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("raw", type=Path)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--prefix", default="title")
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    text = next(s for s in manifest["process"]["segments"] if s["name"] == "text")
    text_start = text["address"]
    text_end = text_start + text["declared_code_size"]

    with args.raw.open(newline="", encoding="utf-8-sig") as source:
        reader = csv.DictReader(source)
        fields = list(reader.fieldnames or [])
        rows = list(reader)
    # The maintained columns of a reviewed inventory, which the generators
    # write too: here every function is Ghidra's automatic discovery.
    for field in MAINTAINED_FIELDS:
        if field not in fields:
            fields.append(field)
    for row in rows:
        row.setdefault("confidence", "auto")
        row.setdefault("evidence", "ghidra_auto_analysis")
        row.setdefault("status", "ghidra_auto")

    dropped: dict[str, int] = {}
    kept: dict[int, dict[str, str]] = {}
    for row in rows:
        entry = int(row["entry"], 16)
        last = int(row["end"], 16)
        reason = None
        if not text_start <= entry < text_end:
            reason = "outside text"
        elif entry % 4:
            reason = "not word-aligned (Thumb)"
        elif entry in kept:
            reason = "duplicate entry"
        if reason:
            dropped[reason] = dropped.get(reason, 0) + 1
            continue
        row["_last"] = str(min(last, text_end - 1))
        kept[entry] = row

    entries = sorted(kept)
    output_rows = []
    truncated = 0
    for index, entry in enumerate(entries):
        row = kept[entry]
        end = int(row.pop("_last")) + 1
        following = entries[index + 1] if index + 1 < len(entries) else text_end
        if end > following:
            end = following
            truncated += 1
        end -= (end - entry) % 4
        if end <= entry:
            dropped["empty after cut"] = dropped.get("empty after cut", 0) + 1
            continue
        row["end"] = f"0x{end - 1:08X}"
        row["size"] = str(end - entry)
        if row.get("name", "").startswith("FUN_"):
            row["name"] = f"{args.prefix}_fun_{entry:08X}"
        output_rows.append(row)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as target:
        writer = csv.DictWriter(target, fieldnames=fields)
        writer.writeheader()
        writer.writerows(output_rows)
    covered = sum(int(row["size"]) for row in output_rows)
    print(f"functions {len(output_rows)} of {len(rows)}; truncated {truncated}; "
          f"dropped {dropped}; text coverage {covered / (text_end - text_start):.1%}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
