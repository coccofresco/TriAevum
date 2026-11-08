#!/usr/bin/env python3
"""Lists the A32 functions a CTR title reaches only through data pointers.

Virtual tables, callback tables and function-pointer literals hold code
addresses that Ghidra's analysis does not follow: the functions they name, and
the code only those functions reach, stay out of the inventory, and the
runtime stops the first time the title calls one. This scans code.bin for
word-aligned words whose value is a word-aligned address inside text but
outside every inventory function, keeps the targets whose code decodes as a
function (valid A32 instructions up to a return or a tail branch), and writes
the seed list for ghidra_scripts/CreateFunctionsAt.java ("0x%08X" per line).
Run it again on the next inventory until it finds nothing: the new functions'
literal pools name more. Odd (Thumb) pointers and targets inside Ghidra's
Thumb ranges are left out; the runtime interprets Thumb itself.

A pointer in the data segments can also name an address inside an inventory
function: a second entry, such as a virtual method that adjusts `this` and
falls into the next one. Only function entries are callable, so
--interior-entries writes those (when they decode as a function) in the A32
generator's supplemental-entry schema, each up to its function's end. Pass the
normalized inventory the generator uses.

usage: data_pointer_entries.py code.bin process-manifest.json inventory.csv
           output.txt [--thumb-ranges ranges.csv] [--interior-entries out.csv]
"""

from __future__ import annotations

import argparse
import bisect
import csv
import io
import json
import struct
import sys
from pathlib import Path

from capstone import CS_ARCH_ARM, CS_MODE_ARM, Cs

MAX_INSTRUCTIONS = 512


def read_intervals(path: Path, start_field: str, end_field: str) -> list[tuple[int, int]]:
    with path.open(newline="", encoding="utf-8-sig") as source:
        rows = list(csv.DictReader(source))
    # End addresses are inclusive in Ghidra's exports.
    return sorted((int(row[start_field], 0), int(row[end_field], 0) + 1) for row in rows)


def containing(intervals: list[tuple[int, int]], starts: list[int],
               address: int) -> tuple[int, int] | None:
    index = bisect.bisect_right(starts, address) - 1
    return intervals[index] if index >= 0 and address < intervals[index][1] else None


def covered(intervals: list[tuple[int, int]], starts: list[int], address: int) -> bool:
    return containing(intervals, starts, address) is not None


def ends_function(instruction) -> bool:
    """A return, or an unconditional branch away (tail call or loop exit)."""
    mnemonic = instruction.mnemonic
    operands = instruction.op_str
    if mnemonic in ("b", "bx"):
        return True
    if mnemonic in ("pop", "ldm", "ldmib", "ldmda", "ldmdb"):
        return "pc" in operands
    if mnemonic in ("mov", "ldr"):
        return operands.startswith("pc,")
    return False


def starts_function(disassembler: Cs, code: bytes, base: int, address: int) -> bool:
    """What precedes a function: the previous one's return or tail branch,
    padding, or its literal pool (data, which rarely decodes as an
    always-executed instruction). An always-executed instruction that falls
    through marks a label inside a function, or a constant that only looks
    like an address."""
    if address - 4 < base:
        return True
    window = code[address - 4 - base:address - base]
    previous = list(disassembler.disasm(window, address - 4))
    if not previous or window in (b"\0\0\0\0", b"\x00\xf0\x20\xe3"):  # data, padding, nop
        return True
    word = struct.unpack_from("<I", window)[0]
    return ends_function(previous[0]) or word >> 28 < 0xE  # conditional: data-like


def decodes_as_function(disassembler: Cs, code: bytes, base: int, address: int,
                        text_end: int) -> bool:
    window = code[address - base:min(text_end, address + MAX_INSTRUCTIONS * 4) - base]
    count = 0
    for instruction in disassembler.disasm(window, address):
        if instruction.address != address + count * 4:
            return False
        count += 1
        if instruction.bytes == b"\0\0\0\0":  # padding, not code
            return False
        if ends_function(instruction):
            return True
    return False  # an undecodable word, or no return within the window


def is_own_address_table_slot(code: bytes, base: int, target: int,
                              function: tuple[int, int]) -> bool:
    """A word inside a function that holds an address of that same function is
    a slot of its own address table (an LDR-PC jump table, a run of identical
    pointers), not a second entry: a data pointer that happens to name it
    names data. A32 code never encodes its own function's address."""
    offset = target - base
    if offset < 0 or offset + 4 > len(code):
        return False
    value = struct.unpack_from("<I", code, offset)[0]
    start, end = function
    return value % 4 == 0 and start <= value < end


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("code", type=Path)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("inventory", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--thumb-ranges", type=Path)
    parser.add_argument("--interior-entries", type=Path)
    args = parser.parse_args()

    code = args.code.read_bytes()
    process = json.loads(args.manifest.read_text(encoding="utf-8"))["process"]
    text = process["segments"][0]
    base = text["address"]
    text_end = base + text["declared_code_size"]

    functions = read_intervals(args.inventory, "entry", "end")
    function_starts = [start for start, _ in functions]
    thumb = read_intervals(args.thumb_ranges, "start", "end") if args.thumb_ranges else []
    thumb_starts = [start for start, _ in thumb]

    disassembler = Cs(CS_ARCH_ARM, CS_MODE_ARM)
    targets: dict[int, int] = {}
    interior: dict[int, int] = {}  # target -> first data word naming it
    for offset in range(0, len(code) - 3, 4):
        value = struct.unpack_from("<I", code, offset)[0]
        if value % 4 or not base <= value < text_end or covered(thumb, thumb_starts, value):
            continue
        function = containing(functions, function_starts, value)
        if function is None:
            targets[value] = targets.get(value, 0) + 1
        elif value != function[0] and base + offset >= text_end:
            interior.setdefault(value, base + offset)

    decoded = [target for target in sorted(targets)
               if decodes_as_function(disassembler, code, base, target, text_end)]
    accepted = [target for target in decoded
                if starts_function(disassembler, code, base, target)]
    args.output.write_text("".join(f"0x{entry:08X}\n" for entry in accepted),
                           encoding="utf-8", newline="\n")
    print(f"{len(targets)} pointer targets outside the inventory, "
          f"{len(decoded)} decode as functions, {len(accepted)} at a function boundary")

    if args.interior_entries:
        rows = []
        for target in sorted(interior):
            if not decodes_as_function(disassembler, code, base, target, text_end):
                continue
            start, end = containing(functions, function_starts, target)
            if is_own_address_table_slot(code, base, target, (start, end)):
                continue
            rows.append({
                "entry": f"0x{target:08X}",
                "size": str(end - target),
                "name": f"ctr_interior_entry_{target:08X}",
                "evidence": f"data_pointer:0x{interior[target]:08X}",
                "confidence": "medium",
                "notes": f"Second entry of the function at 0x{start:08X}, named by a data pointer.",
            })
        text_output = io.StringIO()
        writer = csv.DictWriter(text_output, fieldnames=list(rows[0]) if rows else
                                ["entry", "size", "name", "evidence", "confidence", "notes"],
                                lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
        args.interior_entries.write_text(text_output.getvalue(), encoding="utf-8",
                                         newline="\n")
        print(f"{len(interior)} data pointers inside inventory functions, "
              f"{len(rows)} decode as second entries")
    return 0


if __name__ == "__main__":
    sys.exit(main())
