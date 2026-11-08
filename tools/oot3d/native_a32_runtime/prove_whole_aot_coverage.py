"""Static completeness and entry-reachability proof for a whole-AOT selection.

Shared tool, no title addresses.  Inputs: the whole-AOT program, its selection
and code.bin (plus the executable size and optional inline-argument facts).

COMPLETENESS: every byte of the executable image [base, base + size) is
  * inside an instruction of a block owned by a SELECTED (translated)
    function (A32 4-byte or Thumb 2/4-byte records), or
  * an explicitly classified data word, derived independently of the program
    builder from the translated instructions themselves:
      literal      - addressed by a translated PC-relative load
                     (A32 LDR/LDRB/LDRH/LDRD/VLDR [pc, #imm], Thumb LDR literal),
                     including the second word of LDRD/VLDR.64 pairs;
      inline_arg   - a title fact (inline argument words after a BL);
      switch_table - a word read by a translated table branch
                     (A32 LDR pc, [pc, rX, lsl #2] / ADD pc idioms) up to the
                     first word that is not a code address of the function;
      pointer      - a word whose value is an address inside the image;
      padding      - an all-zero word or a Thumb alignment halfword
                     (0x0000 / 0x46C0 NOP) that no translated code reaches.
  Anything else is RESIDUAL and fails the proof (exit 1).

REACHABILITY: a static walk from the entry point (and from listed extra
entries) over direct edges (branch, fallthrough, resume, direct_call,
thumb_call, switch_case) only visits blocks of selected functions; indirect
sites (register calls/branches, returns) are listed by kind.  An edge to a
pc that is not a selected block or a selected function entry fails (exit 1).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from collections import Counter, deque
from pathlib import Path
from typing import Sequence


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _a32_literal_words(pc: int, raw: int) -> tuple[int, ...]:
    """Words read by an A32 PC-relative load at pc (empty if none)."""
    cond = raw >> 28
    if cond == 0xF:
        return ()
    base = pc + 8
    # LDR/LDRB immediate, rn = pc, load.
    if (raw & 0x0E000000) == 0x04000000 and (raw >> 16) & 0xF == 15 and raw & (1 << 20):
        if not raw & (1 << 24) or raw & (1 << 21):
            return ()
        offset = raw & 0xFFF
        address = base + offset if raw & (1 << 23) else base - offset
        return (address & ~3,)
    # LDRH/LDRSH/LDRSB/LDRD immediate (extra load/store), rn = pc.
    if (raw & 0x0E400090) == 0x00400090 and (raw >> 16) & 0xF == 15 and (raw & 0x60):
        if not raw & (1 << 24) or raw & (1 << 21):
            return ()
        offset = ((raw >> 4) & 0xF0) | (raw & 0xF)
        address = base + offset if raw & (1 << 23) else base - offset
        load = bool(raw & (1 << 20))
        if not load and (raw & 0x60) == 0x40:  # LDRD (L=0, SH=10)
            return (address & ~3, (address + 4) & ~3)
        if load:
            return (address & ~3,)
        return ()
    # VLDR, rn = pc.
    if (raw & 0x0F300E00) == 0x0D100A00 and (raw >> 16) & 0xF == 15:
        offset = (raw & 0xFF) * 4
        address = (base & ~3) + offset if raw & (1 << 23) else (base & ~3) - offset
        if raw & 0x100:  # double
            return (address, address + 4)
        return (address,)
    return ()


def _runs(addresses: list[int]) -> list[str]:
    runs: list[list[int]] = []
    for address in addresses:
        if runs and runs[-1][1] == address:
            runs[-1][1] = address + 1
        else:
            runs.append([address, address + 1])
    return [f"0x{start:08X}+{end - start}" for start, end in runs]


def prove(program: dict, selection: dict, code: bytes, executable_size: int,
          inline_callees: dict[int, int] | None = None,
          extra_entries: Sequence[int] = ()) -> dict:
    base = int(program["base"])
    if executable_size <= 0 or executable_size > len(code):
        raise ValueError("executable size is outside code.bin")
    selected = {int(item["entry"]) for item in selection.get("functions", [])}
    blocks = {int(block["id"]): block for block in program["blocks"]}
    functions = {int(function["entry"]): function for function in program["functions"]}
    owned: set[int] = set()
    for entry in selected:
        record = functions.get(entry)
        if record is None:
            raise ValueError(f"selected function 0x{entry:08X} is not in the program")
        owned.update(int(block_id) for block_id in record["blocks"])
    kind = bytearray(executable_size)  # 0 residual, 1 code
    selected_block_pcs: set[int] = set()
    literals: set[int] = set()
    inline_words: set[int] = set()
    inline_callees = inline_callees or {}
    switch_sites: list[tuple[int, int]] = []
    for block_id in owned:
        block = blocks[block_id]
        selected_block_pcs.add(int(block["pc"]))
        if block.get("state") == "thumb":
            for record in block["thumb_instructions"]:
                start = (int(record["pc"]) & ~1) - base
                kind[start:start + int(record["size"])] = b"\x01" * int(record["size"])
                if "literal" in record:
                    literals.add(int(record["literal"]))
            continue
        start = int(block["pc"]) - base
        end = int(block["end_pc"]) - base
        kind[start:end] = b"\x01" * (end - start)
        for offset in range(start, end, 4):
            raw = int.from_bytes(code[offset:offset + 4], "little")
            literals.update(_a32_literal_words(base + offset, raw))
            if (raw & 0x0F000000) == 0x0B000000 and raw >> 28 != 0xF:
                displacement = raw & 0x00FFFFFF
                if displacement & 0x00800000:
                    displacement -= 0x01000000
                target = (base + offset + 8 + displacement * 4) & 0xFFFFFFFF
                for index in range(inline_callees.get(target, 0)):
                    inline_words.add(base + offset + 4 + 4 * index)
            # LDR pc, [pc, rX, lsl #2]: the table follows the next word.
            if (raw & 0x0FFFFFF0) == 0x079FF100:
                switch_sites.append((base + offset, int(block["pc"])))
    image_end = base + len(code)
    classes: Counter[str] = Counter()
    residual: list[int] = []
    table_words: set[int] = set()
    code_targets = selected_block_pcs | selected
    for site, _ in switch_sites:
        address = site + 8
        while address < base + executable_size:
            word = int.from_bytes(code[address - base:address - base + 4], "little")
            if word not in code_targets:
                break
            table_words.add(address)
            address += 4
    position = 0
    while position < executable_size:
        if kind[position]:
            classes["code"] += 1
            position += 1
            continue
        address = base + position
        aligned = address & ~3
        word = int.from_bytes(code[aligned - base:aligned - base + 4], "little")
        half = int.from_bytes(code[position & ~1:(position & ~1) + 2], "little")
        if aligned in literals:
            label = "literal"
        elif aligned in inline_words:
            label = "inline_arg"
        elif aligned in table_words:
            label = "switch_table"
        elif word == 0:
            label = "padding"
        elif base <= (word & ~1) < image_end:
            label = "pointer"
        elif half in (0x0000, 0x46C0) and (
            kind[(position & ~1) - 2 if position >= 2 else 0]
            or kind[min(executable_size - 1, (position & ~1) + 2)]
        ):
            label = "padding"
        else:
            label = "residual"
            residual.append(address)
        classes[label] += 1
        position += 1
    # Reachability.
    pc_to_block = {int(block["pc"]): int(block["id"]) for block in program["blocks"]}
    roots = [base, *extra_entries]
    visited: set[int] = set()
    failures: list[str] = []
    indirect: Counter[str] = Counter()
    queue = deque()
    for root in roots:
        if root not in pc_to_block or pc_to_block[root] not in owned:
            failures.append(f"entry 0x{root:08X} is not a selected block")
        else:
            queue.append(pc_to_block[root])
    direct = {"branch", "fallthrough", "resume", "direct_call", "thumb_call", "switch_case"}
    while queue:
        block_id = queue.popleft()
        if block_id in visited:
            continue
        visited.add(block_id)
        for edge in blocks[block_id].get("successors", []):
            edge_kind = edge.get("kind")
            if edge_kind not in direct:
                indirect[edge_kind] += 1
                continue
            target = edge.get("target")
            if target is None:
                failures.append(f"{edge_kind} at 0x{int(edge['site']):08X} has no target")
                continue
            target_block = pc_to_block.get(int(target))
            if target_block is None or target_block not in owned:
                failures.append(
                    f"{edge_kind} at 0x{int(edge['site']):08X} reaches untranslated 0x{int(target):08X}")
                continue
            queue.append(target_block)
    thumb_blocks = sum(1 for block_id in visited if blocks[block_id].get("state") == "thumb")
    return {
        "format": "triaevum_whole_aot_coverage_proof_v1",
        "base": base,
        "executable_size": executable_size,
        "selected_functions": len(selected),
        "selected_thumb_functions": sum(1 for entry in selected if entry & 1),
        "byte_classes": dict(sorted(classes.items())),
        "residual_bytes": len(residual),
        "residual_first": [f"0x{address:08X}" for address in residual[:32]],
        "residual_runs": _runs(residual)[:400],
        "reachability": {
            "roots": [f"0x{root:08X}" for root in roots],
            "visited_blocks": len(visited),
            "visited_thumb_blocks": thumb_blocks,
            "indirect_sites_by_kind": dict(sorted(indirect.items())),
            "failures": failures[:64],
            "failure_count": len(failures),
        },
    }


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--program", type=Path, required=True)
    parser.add_argument("--selection", type=Path, required=True)
    parser.add_argument("--code", type=Path, required=True)
    parser.add_argument("--executable-size", type=lambda v: int(v, 0), required=True)
    parser.add_argument("--inline-argument-calls", type=Path, default=None)
    parser.add_argument("--entry", type=lambda v: int(v, 0), action="append", default=[])
    parser.add_argument("--output", type=Path, default=None)
    parser.add_argument("--check", choices=("all", "completeness", "reachability"),
                        default="all", help="which proof decides the exit code")
    args = parser.parse_args(argv)
    program = json.loads(args.program.read_text(encoding="utf-8"))
    selection = json.loads(args.selection.read_text(encoding="utf-8"))
    code = args.code.read_bytes()
    inline_callees: dict[int, int] = {}
    if args.inline_argument_calls:
        facts = json.loads(args.inline_argument_calls.read_text(encoding="utf-8"))
        for callee in facts.get("callees", []):
            inline_callees[int(str(callee["entry"]), 0)] = int(callee["inline_words"])
    result = prove(program, selection, code, args.executable_size,
                   inline_callees, args.entry)
    result["inputs"] = {
        "program_sha256": _sha256(args.program),
        "selection_sha256": _sha256(args.selection),
        "code_sha256": _sha256(args.code),
    }
    text = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    sys.stdout.write(text)
    complete = result["residual_bytes"] == 0
    reachable = result["reachability"]["failure_count"] == 0
    ok = {"all": complete and reachable, "completeness": complete,
          "reachability": reachable}[args.check]
    print(f"completeness: {'PASS' if complete else 'FAIL'}; "
          f"reachability: {'PASS' if reachable else 'FAIL'}; "
          f"check={args.check}: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
