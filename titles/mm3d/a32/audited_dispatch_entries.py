"""MM3D title facts: audited dispatch entries (guarded) -> extractor inputs.

Each row of audited_dispatch_entries.csv names an A32 address that the guest
can reach without a static edge (switch cases of an ``add pc`` table, return
slots of the RVCT multi-return compare helper, unreferenced code).  The
guard word must equal code.bin at that address.  The tool appends a 4-byte
supplemental inventory interval and a boundary-audit row with the row's
classification (whole_aot_program.py seeds them as block starts/roots).
Exit 1 on any guard mismatch or malformed row.
"""
from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

CLASSES = ("switch_owned_case", "multi_return_slot", "unreferenced_code")


def load(facts: Path, code: bytes, base: int) -> list[dict[str, str]]:
    rows = list(csv.DictReader(facts.open(newline="", encoding="utf-8-sig")))
    seen = set()
    for row in rows:
        entry = int(row["entry"], 0)
        if entry % 4 or entry in seen or row["classification"] not in CLASSES:
            raise ValueError(f"invalid audited entry 0x{entry:08X}")
        seen.add(entry)
        actual = int.from_bytes(code[entry - base:entry - base + 4], "little")
        if actual != int(row["guard_word"], 0):
            raise ValueError(
                f"guard at 0x{entry:08X} is 0x{actual:08X}, expected {row['guard_word']}")
    return rows


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--code", type=Path, required=True)
    parser.add_argument("--facts", type=Path, required=True)
    parser.add_argument("--base", type=lambda v: int(v, 0), default=0x100000)
    parser.add_argument("--supplemental-in", type=Path, required=True)
    parser.add_argument("--supplemental-out", type=Path, required=True)
    parser.add_argument("--audit-in", type=Path, required=True)
    parser.add_argument("--audit-out", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        rows = load(args.facts, args.code.read_bytes(), args.base)
    except ValueError as error:
        print(f"audited dispatch entries: FAIL: {error}")
        return 1
    supplemental = args.supplemental_in.read_text(encoding="utf-8")
    audit = args.audit_in.read_text(encoding="utf-8")
    if not supplemental.endswith("\n") or not audit.endswith("\n"):
        print("audited dispatch entries: FAIL: inputs must end with a newline")
        return 1
    with args.supplemental_out.open("w", newline="\n", encoding="utf-8") as out:
        out.write(supplemental)
        writer = csv.writer(out, lineterminator="\n")
        for row in rows:
            entry = int(row["entry"], 0)
            writer.writerow([f"0x{entry:08X}", 4, f"mm3d_dispatch_entry_{entry:08X}",
                             row["classification"], "certain", row["meaning"]])
    with args.audit_out.open("w", newline="\n", encoding="utf-8") as out:
        out.write(audit)
        writer = csv.writer(out, lineterminator="\n")
        for row in rows:
            entry = int(row["entry"], 0)
            writer.writerow([f"0x{entry:08X}", 4, f"mm3d_dispatch_entry_{entry:08X}",
                             "non_callable_internal", "not_independent_body",
                             "not_applicable", row["classification"],
                             f"guard_word={row['guard_word']}", row["meaning"]])
    print(f"audited dispatch entries: PASS ({len(rows)} rows)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
