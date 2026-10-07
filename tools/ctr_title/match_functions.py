#!/usr/bin/env python3
"""Pairs the A32 functions of two CTR titles built from the same engine.

Grezzo's 3DS titles share engine code (matrix, renderer, command-list and
model code) compiled at different addresses. A title adapter recovered for
one title names addresses in its code.bin; this finds the same functions in
another title's code.bin so an adapter can be ported by evidence rather than
by guesswork.

Each function body (inventory entry..end, A32 only) is normalized: branch
and call targets, PC-relative loads, register allocation and saved-register
sets are dropped; mnemonics, struct offsets and immediates are kept.
Matching runs in three passes:

  1. exact: a normalized body that occurs once in each title;
  2. call graph: for a matched pair whose bodies have the same call
     sequence, the callees at the same positions are paired, repeatedly;
  3. similar: an unmatched function of the source title is paired with the
     unmatched target function of the closest size whose normalized body is
     at least --min-similarity similar (difflib ratio), when that best
     candidate is clearly better than the second.

Output CSV: source_entry, target_entry, method, similarity, source_name.
--query ADDR[,ADDR...] prints the pairing of source addresses, including
addresses inside a function (mapped by instruction position when the
bodies have the same length) such as call return addresses.

usage: match_functions.py SOURCE_CODE SOURCE_INVENTORY TARGET_CODE
           TARGET_INVENTORY OUTPUT.csv [--query ...] [--min-similarity R]
"""

from __future__ import annotations

import argparse
import bisect
import csv
import difflib
import hashlib
import io
import re
import sys
from collections import defaultdict
from pathlib import Path

from capstone import CS_ARCH_ARM, CS_MODE_ARM, Cs

BASE = 0x00100000
MAX_FUNCTION_BYTES = 0x8000


class Function:
    __slots__ = ("entry", "end", "name", "tokens", "calls", "digest")

    def __init__(self, entry: int, end: int, name: str):
        self.entry = entry
        self.end = end
        self.name = name
        self.tokens: list[str] = []
        self.calls: list[tuple[int, int]] = []  # (instruction index, target)
        self.digest = ""


REGISTER = re.compile(r"(?:r(?:1[0-5]|[0-9])|sb|sl|fp|ip|sp|lr|[sd](?:3[01]|[12][0-9]|[0-9]))")


def normalize(instruction) -> tuple[str, int | None]:
    """The instruction's identity without addresses or register allocation
    (another build of the same source allocates registers differently;
    struct offsets and immediates stay); and a call target."""
    mnemonic = instruction.mnemonic
    operands = instruction.op_str
    if mnemonic.startswith(("bl", "blx")) and operands.startswith("#"):
        return mnemonic, int(operands[1:], 16)
    if mnemonic.startswith("b") and operands.startswith("#"):
        return mnemonic, None
    if "[pc" in operands or mnemonic.startswith("adr"):  # literal loads
        return f"{mnemonic} lit", None
    if mnemonic.startswith(("push", "pop", "vpush", "vpop", "ldm", "stm", "vldm", "vstm")):
        return mnemonic, None  # saved-register sets follow the allocation
    return f"{mnemonic} {REGISTER.sub('R', operands)}", None


def load(code_path: Path, inventory_path: Path) -> tuple[bytes, list[Function]]:
    code = code_path.read_bytes()
    with inventory_path.open(newline="", encoding="utf-8-sig") as source:
        rows = list(csv.DictReader(source))
    functions = []
    for row in rows:
        entry = int(row["entry"], 0)
        end = int(row["end"], 0) + 1  # inclusive in the exports
        if entry & 3 or end <= entry or end - entry > MAX_FUNCTION_BYTES:
            continue
        name = row.get("maintained_name") or row.get("name") or ""
        functions.append(Function(entry, end, name))
    functions.sort(key=lambda f: f.entry)
    disassembler = Cs(CS_ARCH_ARM, CS_MODE_ARM)
    for function in functions:
        offset = function.entry - BASE
        window = code[offset:function.end - BASE]
        count = 0
        for instruction in disassembler.disasm(window, function.entry):
            if instruction.address != function.entry + count * 4:
                break
            token, target = normalize(instruction)
            if target is not None:
                function.calls.append((count, target))
            function.tokens.append(token)
            count += 1
        # Literal pools and padding decode as data or stop the disassembly;
        # trailing words that never execute are dropped from the identity.
        function.digest = hashlib.sha1("\n".join(function.tokens).encode()).hexdigest()
    return code, functions


def match(source: list[Function], target: list[Function], min_similarity: float):
    pairs: dict[int, tuple[int, str, float]] = {}
    target_used: set[int] = set()
    by_entry_source = {f.entry: f for f in source}
    by_entry_target = {f.entry: f for f in target}

    # 1. exact unique normalized bodies (ignore tiny bodies: too common).
    source_digests: dict[str, list[Function]] = defaultdict(list)
    target_digests: dict[str, list[Function]] = defaultdict(list)
    for f in source:
        source_digests[f.digest].append(f)
    for f in target:
        target_digests[f.digest].append(f)
    for digest, sources in source_digests.items():
        targets = target_digests.get(digest, [])
        if len(sources) == 1 and len(targets) == 1 and len(sources[0].tokens) >= 6:
            pairs[sources[0].entry] = (targets[0].entry, "exact", 1.0)
            target_used.add(targets[0].entry)

    # 2. call-graph propagation.
    changed = True
    while changed:
        changed = False
        for source_entry, (target_entry, _, _) in list(pairs.items()):
            s = by_entry_source[source_entry]
            t = by_entry_target[target_entry]
            if len(s.calls) != len(t.calls) or len(s.tokens) != len(t.tokens):
                continue
            for (si, starget), (ti, ttarget) in zip(s.calls, t.calls):
                if si != ti or starget in pairs or ttarget in target_used:
                    continue
                if starget not in by_entry_source or ttarget not in by_entry_target:
                    continue
                pairs[starget] = (ttarget, "call_graph", 1.0)
                target_used.add(ttarget)
                changed = True

    # 3. similarity among the remaining functions of comparable size.
    remaining = sorted((f for f in target if f.entry not in target_used),
                       key=lambda f: len(f.tokens))
    sizes = [len(f.tokens) for f in remaining]
    for s in source:
        if s.entry in pairs or len(s.tokens) < 12:
            continue
        low = bisect.bisect_left(sizes, int(len(s.tokens) * 0.8))
        high = bisect.bisect_right(sizes, int(len(s.tokens) * 1.25) + 1)
        scored = []
        matcher = difflib.SequenceMatcher(autojunk=False)
        matcher.set_seq2(s.tokens)
        for candidate in remaining[low:high]:
            if candidate.entry in target_used:
                continue
            matcher.set_seq1(candidate.tokens)
            if matcher.real_quick_ratio() < min_similarity or matcher.quick_ratio() < min_similarity:
                continue
            scored.append((matcher.ratio(), candidate.entry))
        scored.sort(reverse=True)
        if scored and scored[0][0] >= min_similarity and (
                len(scored) == 1 or scored[0][0] - scored[1][0] >= 0.05):
            pairs[s.entry] = (scored[0][1], "similar", round(scored[0][0], 3))
            target_used.add(scored[0][1])
    return pairs


def map_address(address: int, source: list[Function], target_by_entry: dict[int, Function],
                pairs) -> str:
    starts = [f.entry for f in source]
    index = bisect.bisect_right(starts, address) - 1
    if index < 0 or address >= source[index].end:
        return "not inside a source function"
    s = source[index]
    if s.entry not in pairs:
        return f"in 0x{s.entry:08X} ({s.name}), unmatched"
    target_entry, method, similarity = pairs[s.entry]
    t = target_by_entry[target_entry]
    offset = address - s.entry
    note = f"{method} {similarity}"
    if offset == 0:
        return f"0x{target_entry:08X} [{note}] {s.name}"
    if len(s.tokens) == len(t.tokens):
        return f"0x{target_entry + offset:08X} (+0x{offset:X} in 0x{target_entry:08X}) [{note}] {s.name}"
    # Different lengths: map a call return address by its call ordinal.
    calls = [i for i, _ in s.calls]
    position = offset // 4 - 1
    if position in calls and len(s.calls) == len(t.calls):
        ordinal = calls.index(position)
        ti = t.calls[ordinal][0]
        return (f"0x{target_entry + (ti + 1) * 4:08X} (return of call #{ordinal} in "
                f"0x{target_entry:08X}) [{note}] {s.name}")
    return f"in 0x{target_entry:08X}, bodies differ (+0x{offset:X} unmapped) [{note}] {s.name}"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source_code", type=Path)
    parser.add_argument("source_inventory", type=Path)
    parser.add_argument("target_code", type=Path)
    parser.add_argument("target_inventory", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--query", default="")
    parser.add_argument("--min-similarity", type=float, default=0.85)
    args = parser.parse_args()

    _, source = load(args.source_code, args.source_inventory)
    _, target = load(args.target_code, args.target_inventory)
    pairs = match(source, target, args.min_similarity)
    by_entry_source = {f.entry: f for f in source}
    target_by_entry = {f.entry: f for f in target}

    text = io.StringIO()
    writer = csv.writer(text, lineterminator="\n")
    writer.writerow(["source_entry", "target_entry", "method", "similarity", "source_name"])
    for source_entry in sorted(pairs):
        target_entry, method, similarity = pairs[source_entry]
        writer.writerow([f"0x{source_entry:08X}", f"0x{target_entry:08X}", method,
                         similarity, by_entry_source[source_entry].name])
    args.output.write_text(text.getvalue(), encoding="utf-8", newline="\n")
    counts = defaultdict(int)
    for _, method, _ in pairs.values():
        counts[method] += 1
    print(f"{len(source)} source functions, {len(target)} target functions, "
          f"{len(pairs)} paired: " + ", ".join(f"{k} {v}" for k, v in sorted(counts.items())))
    for item in filter(None, args.query.split(",")):
        address = int(item, 16)
        print(f"0x{address:08X} -> {map_address(address, source, target_by_entry, pairs)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
