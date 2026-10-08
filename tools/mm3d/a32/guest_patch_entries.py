#!/usr/bin/env python3
"""Supplemental A32 registry entries for MM3D's guest patch sites.

Every guest patch site (tools/oot3d/native_game_runtime/mm3d_single_screen.cpp)
must start a registry block, and so must the instruction after it, where the
guest continues. The generator splits the block containing a supplemental
entry, so each boundary becomes an entry spanning to the end of its
inventory function. The boundaries come from the site table itself:

    oot3d_mm3d_single_screen_tests --block-boundaries > boundaries.txt
    guest_patch_entries.py --inventory INVENTORY.csv --boundaries boundaries.txt \
        [--merge OTHER_SUPPLEMENTAL.csv] --output SUPPLEMENTAL.csv

--merge appends the boundaries to another supplemental list (MM3D's interior
entries), since generate_aot.py takes one.
"""

import argparse
import csv
import sys
from pathlib import Path

FIELDS = ["entry", "size", "name", "evidence", "confidence", "notes"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--inventory", type=Path, required=True)
    parser.add_argument("--boundaries", type=Path, required=True)
    parser.add_argument("--merge", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    with args.inventory.open(newline="", encoding="utf-8-sig") as source:
        functions = sorted(
            (int(row["entry"], 0), int(row["entry"], 0) + int(row["size"], 0))
            for row in csv.DictReader(source)
        )
    rows = []
    if args.merge:
        with args.merge.open(newline="", encoding="utf-8-sig") as source:
            rows = [{field: row.get(field, "") for field in FIELDS}
                    for row in csv.DictReader(source)]
    known = {int(row["entry"], 0) for row in rows}
    boundaries = sorted({int(line, 0) for line in args.boundaries.read_text().split()})
    for pc in boundaries:
        containing = [(start, end) for start, end in functions if start <= pc < end]
        if pc in known:
            continue
        if containing:
            end = min(end for _, end in containing)
        else:
            # Code the inventory left between two functions: up to the next.
            following = [start for start, _ in functions if start > pc]
            if not following:
                print(f"guest patch boundary 0x{pc:08X} follows every inventory function",
                      file=sys.stderr)
                return 1
            end = min(following)
            print(f"guest patch boundary 0x{pc:08X} lies between inventory functions;"
                  f" bounded by 0x{end:08X}", file=sys.stderr)
        rows.append({
            "entry": f"0x{pc:08X}",
            "size": str(end - pc),
            "name": f"mm3d_guest_patch_{pc:08X}",
            "evidence": "guest_patch_site",
            "confidence": "certain",
            "notes": "A guest patch site or the instruction after it starts a block.",
        })
        known.add(pc)
    rows.sort(key=lambda row: int(row["entry"], 0))
    with args.output.open("w", newline="", encoding="utf-8") as target:
        writer = csv.DictWriter(target, fieldnames=FIELDS, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    print(f"{len(boundaries)} guest patch boundaries, {len(rows)} supplemental entries")
    return 0


if __name__ == "__main__":
    sys.exit(main())
