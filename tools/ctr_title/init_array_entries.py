#!/usr/bin/env python3
"""Lists the constructors in a CTR title's static-initializer table.

ARM's C++ runtime walks the table with self-relative words: entry = word +
address of the word, odd for Thumb. Ghidra cannot follow those, so the
constructors (and code reached only from them) stay undiscovered. The table
bounds come from the title's initializer loop (see docs/MOONFALL_CURSE.md for
MM3D's). The output is one address per line ("0x%08X", odd for Thumb), the
seed list for ghidra_scripts/CreateFunctionsAt.java.

usage: init_array_entries.py code.bin 0xSTART 0xEND output.txt [--base 0x00100000]
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("code", type=Path)
    parser.add_argument("start", type=lambda text: int(text, 0))
    parser.add_argument("end", type=lambda text: int(text, 0))
    parser.add_argument("output", type=Path)
    parser.add_argument("--base", type=lambda text: int(text, 0), default=0x00100000)
    args = parser.parse_args()

    code = args.code.read_bytes()
    if args.start % 4 or args.end % 4 or not args.base <= args.start <= args.end <= args.base + len(code):
        raise SystemExit("table bounds must be word-aligned and inside code.bin")
    entries = []
    for address in range(args.start, args.end, 4):
        word = struct.unpack_from("<I", code, address - args.base)[0]
        target = (word + address) & 0xFFFFFFFF
        if not args.base <= target & ~1 < args.base + len(code):
            raise SystemExit(f"entry at 0x{address:08X} leaves code.bin: 0x{target:08X}")
        entries.append(target)
    args.output.write_text("".join(f"0x{entry:08X}\n" for entry in entries), encoding="utf-8")
    thumb = sum(entry & 1 for entry in entries)
    print(f"{len(entries)} initializers ({thumb} Thumb), {len(set(entries))} distinct")
    return 0


if __name__ == "__main__":
    sys.exit(main())
