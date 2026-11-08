#!/usr/bin/env python3
"""Drop stale supplemental entries from MM3D's registry inventory for whole AOT.

The registry overlay inventory (a32_generated_overlay) was laid out from the
producers' outputs at the time it was generated. Two producers have since been
corrected, and the whole-AOT program must follow the current ones, not the
stale rows:

* guest patch entries (guest_patch_entries.py): a row named
  mm3d_guest_patch_XXXXXXXX whose address is no longer a block boundary of the
  current site table (88b48850: epilogue observers export no literal Pc+4
  boundary; 0x0011EF5C/0x0011EFA8/0x0011EFF8 are literal-pool words);
* interior entries (tools/ctr_title/data_pointer_entries.py): a row named
  ctr_interior_entry_XXXXXXXX whose word holds an address of its own function
  is a slot of that function's address table (0x00315F74, 0x003C0000).

usage: whole_aot_inventory.py --code code.bin --inventory IN.csv
           --boundaries boundaries.txt --output OUT.csv
"""

from __future__ import annotations

import argparse
import bisect
import csv
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "tools" / "ctr_title"))

from data_pointer_entries import is_own_address_table_slot  # noqa: E402

GUEST_PATCH = "mm3d_guest_patch_"
INTERIOR = "ctr_interior_entry_"
BASE = 0x00100000


def prune(code: bytes, rows: list[dict[str, str]], boundaries: set[int],
          base: int = BASE) -> tuple[list[dict[str, str]], list[tuple[int, str]]]:
    owners = sorted(
        (int(row["entry"], 0), int(row["entry"], 0) + int(row["size"], 0))
        for row in rows
        if not row["name"].startswith((GUEST_PATCH, INTERIOR))
    )
    starts = [start for start, _ in owners]
    kept: list[dict[str, str]] = []
    dropped: list[tuple[int, str]] = []
    for row in rows:
        entry = int(row["entry"], 0)
        name = row["name"]
        if name.startswith(GUEST_PATCH) and entry not in boundaries:
            dropped.append((entry, "guest patch row is not a current block boundary"))
            continue
        if name.startswith(INTERIOR):
            index = bisect.bisect_right(starts, entry) - 1
            owner = owners[index] if index >= 0 and entry < owners[index][1] else None
            if owner is not None and is_own_address_table_slot(code, base, entry, owner):
                dropped.append((entry, "interior entry is a slot of its function's address table"))
                continue
        kept.append(row)
    return kept, dropped


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--code", type=Path, required=True)
    parser.add_argument("--inventory", type=Path, required=True)
    parser.add_argument("--boundaries", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    with args.inventory.open(newline="", encoding="utf-8-sig") as source:
        reader = csv.DictReader(source)
        fields = list(reader.fieldnames or ())
        rows = list(reader)
    boundaries = {int(line, 0) for line in args.boundaries.read_text().split()}
    kept, dropped = prune(args.code.read_bytes(), rows, boundaries)
    with args.output.open("w", newline="", encoding="utf-8") as target:
        writer = csv.DictWriter(target, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(kept)
    for entry, reason in dropped:
        print(f"dropped 0x{entry:08X}: {reason}")
    print(f"{len(rows)} rows, {len(dropped)} dropped, {len(kept)} kept")
    return 0


if __name__ == "__main__":
    sys.exit(main())
