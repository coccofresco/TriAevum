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
      padding      - an all-zero word, an A32 NOP word, or a Thumb alignment halfword
                     (0x0000 / 0x46C0 NOP) that no translated code reaches.
  A maximal run of bytes left after these word classes may still be data:
      fill         - only 0xFF bytes (linker fill);
      string       - printable ASCII bytes and NUL terminators, starting with
                     a printable byte and holding or followed by a NUL;
      padding      - A32 NOP words (MOV r0, r0) no translated code reaches;
      title_data   - a run fully inside a guarded title data fact
                     (--data-facts CSV: start,size,guard_word,meaning);
      addressed    - a run holding an address that translated code or a
                     classified data word names (A32/Thumb ADR targets,
                     literal and pointer word values);
      data_island  - a run inside a non-code island (bytes between translated
                     instructions) that holds such an address elsewhere.
  fill/string/padding/addressed/data_island never apply to a code-shaped
  run (a function prologue, an A32 BX lr, AL-condition A32 words ending in
  an unconditional terminator, or straight-line Thumb ending in a return or
  a branch): such a run stays residual unless a guarded title data fact
  covers it.
  Anything else is RESIDUAL and fails the proof (exit 1).

REACHABILITY: a static walk from the entry point (and from listed extra
entries) over direct edges (branch, fallthrough, resume, direct_call,
thumb_call, switch_case) only visits blocks of selected functions; indirect
sites (register calls/branches, returns) are listed by kind.  An edge to a
pc that is not a selected block or a selected function entry fails (exit 1).
"""

from __future__ import annotations

import argparse
import bisect
import csv
import hashlib
import json
import sys
from collections import Counter, deque
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))
import whole_aot_thumb as thumb_decoder  # noqa: E402


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


def _run_bounds(addresses: list[int]) -> list[tuple[int, int]]:
    runs: list[list[int]] = []
    for address in addresses:
        if runs and runs[-1][1] == address:
            runs[-1][1] = address + 1
        else:
            runs.append([address, address + 1])
    return [(start, end) for start, end in runs]


def _runs(addresses: list[int]) -> list[str]:
    runs: list[list[int]] = []
    for address in addresses:
        if runs and runs[-1][1] == address:
            runs[-1][1] = address + 1
        else:
            runs.append([address, address + 1])
    return [f"0x{start:08X}+{end - start}" for start, end in runs]


def _ror(value: int, amount: int) -> int:
    amount %= 32
    if not amount:
        return value
    return ((value >> amount) | (value << (32 - amount))) & 0xFFFFFFFF


def _a32_adr_target(pc: int, raw: int) -> int | None:
    """Address computed by A32 ADD/SUB rd, pc, #imm (ADR)."""
    if raw >> 28 == 0xF or (raw & 0x0E000000) != 0x02000000 or (raw >> 16) & 0xF != 15:
        return None
    opcode = (raw >> 21) & 0xF
    if opcode not in (2, 4):
        return None
    immediate = _ror(raw & 0xFF, 2 * ((raw >> 8) & 0xF))
    return (pc + 8 + immediate if opcode == 4 else pc + 8 - immediate) & 0xFFFFFFFF


_A32_SHAPE_WINDOW = 4  # AL-condition A32 words ending in a terminator
_A32_SHAPE_STRAIGHT = 6  # AL-condition A32 words in a row
_THUMB_SHAPE_MINIMUM = 3  # decoded Thumb instructions before a plain B


def _a32_terminator(word: int) -> bool:
    """Unconditional A32 control transfer that ends straight-line code: B,
    BX/BLX reg, POP/LDM sp! with pc, LDR pc, [sp], #4, MOV pc, lr."""
    return ((word & 0xFF000000) == 0xEA000000
            or (word & 0xFFFFFFD0) == 0xE12FFF10
            or (word & 0xFFFF8000) == 0xE8BD8000
            or word in (0xE49DF004, 0xE1A0F00E))


def _thumb_shaped_from(code: bytes, offset: int, end: int) -> bool:
    # A plain B ends the straight line only after instructions a compiler
    # emits: a 0x0000 halfword (MOVS r0, r0) marks a byte table instead.
    decoded = 0
    zero_halfword = False
    while offset + 2 <= end:
        try:
            item = thumb_decoder.decode(code, 0, offset)
        except thumb_decoder.ThumbDecodeError:
            return False
        if offset + item.size > end:
            return False
        if item.kind in ("bx", "pop_pc"):
            return True
        if item.kind == "b":
            return decoded >= _THUMB_SHAPE_MINIMUM and not zero_halfword
        if item.kind in ("udf", "blx_reg"):
            return False
        zero_halfword |= code[offset] == 0 and code[offset + 1] == 0
        decoded += 1  # bcc / bl / blx_imm continue the straight line
        offset += item.size
    return False


def _code_shaped(code: bytes, start: int, end: int) -> bool:
    """A run that looks like code is never data. It is code-shaped when it
    holds a function prologue (Thumb PUSH {.., lr} or A32 PUSH {.., lr}) or
    an A32 BX lr at natural alignment; or a window of AL-condition A32 words
    ending in an unconditional A32 terminator (B, BX reg, POP {.., pc},
    LDR pc, [sp], #4, MOV pc, lr), which is the tail of a function whose
    prologue lies in another run; or six AL-condition A32 words in a row; or
    one AL word that only code holds (B/BL with a target inside the image,
    BX reg, the NOP hint, POP/VPOP, LDR rX, [pc, #imm], arithmetic
    with an immediate operand); or when it decodes from its start as
    straight-line Thumb code (conditional branches and calls continue it)
    ending inside the run in a return (BX / POP {.., pc}) or, after a few
    instructions, in an unconditional B."""
    for offset in range(start + (start & 1), end - 1, 2):
        if code[offset + 1] == 0xB5:
            return True
        if offset % 4 == 0 and offset + 4 <= end:
            word = int.from_bytes(code[offset:offset + 4], "little")
            if (word & 0xFFFF4000) == 0xE92D4000 or word == 0xE12FFF1E:
                return True
    streak = 0
    for offset in range((start + 3) & ~3, end - 3, 4):
        word = int.from_bytes(code[offset:offset + 4], "little")
        streak = streak + 1 if (word >> 28) == 0xE else 0
        if streak >= _A32_SHAPE_WINDOW and _a32_terminator(word):
            return True
        if streak >= _A32_SHAPE_STRAIGHT:
            return True
        if ((word & 0xFFFFFFF0) == 0xE12FFF10 or word == 0xE320F000
                or (word & 0xFFFF0000) in (0xE8BD0000, 0xECBD0000)
                or (word & 0xFF7F0000) == 0xE51F0000
                or (word & 0xFF000000) == 0xE2000000):
            # AL BX reg, the ARMv6K NOP hint, POP/VPOP, LDR rX, [pc, #imm],
            # AL AND/EOR/SUB/RSB/ADD/ADC/SBC/RSC with an immediate operand
            # (0xE3 is left out: reciprocal tables hold 0xE38E38E3).
            return True
        if (word & 0xFE000000) == 0xEA000000:
            # An AL B/BL whose target lies inside the image.
            delta = ((word & 0xFFFFFF) ^ 0x800000) - 0x800000
            if 0 <= offset + 8 + 4 * delta < len(code):
                return True
    return _thumb_shaped_from(code, start + (start & 1), end)


def _run_class(code: bytes, start: int, end: int) -> str | None:
    data = code[start:end]
    if all(byte == 0xFF for byte in data):
        return "fill"
    terminated = 0 in data or (end < len(code) and code[end] == 0)
    if (data[0] != 0 and terminated
            and all(32 <= byte < 127 or byte in (0, 9, 10, 13) for byte in data)):
        return "string"
    if start % 4 == 0 and (end - start) % 4 == 0 and all(
            int.from_bytes(data[index:index + 4], "little") == 0xE1A00000
            for index in range(0, len(data), 4)):
        return "padding"
    return None


def load_data_facts(path: Path, code: bytes, base: int) -> list[tuple[int, int]]:
    """Guarded title data facts (start,size,guard_word,meaning)."""
    facts = []
    with path.open(newline="", encoding="utf-8-sig") as source:
        for row in csv.DictReader(source):
            start, size = int(row["start"], 0), int(row["size"], 0)
            offset = start - base
            if size <= 0 or offset < 0 or offset + size > len(code):
                raise ValueError(f"data fact 0x{start:08X}+{size} is outside code")
            actual = int.from_bytes(code[offset:offset + 4], "little")
            if actual != int(row["guard_word"], 0):
                raise ValueError(
                    f"data fact guard at 0x{start:08X} is 0x{actual:08X}, "
                    f"expected {row['guard_word']}")
            facts.append((start, start + size))
    return facts


def prove(program: dict, selection: dict, code: bytes, executable_size: int,
          inline_callees: dict[int, int] | None = None,
          extra_entries: Sequence[int] = (),
          data_facts: Sequence[tuple[int, int]] = ()) -> dict:
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
    references: set[int] = set()
    for block_id in owned:
        block = blocks[block_id]
        selected_block_pcs.add(int(block["pc"]))
        if block.get("state") == "thumb":
            for record in block["thumb_instructions"]:
                start = (int(record["pc"]) & ~1) - base
                kind[start:start + int(record["size"])] = b"\x01" * int(record["size"])
                if "literal" in record:
                    literals.add(int(record["literal"]))
                if record.get("kind") == "const" and "target" in record:
                    references.add(int(record["target"]))
            continue
        start = int(block["pc"]) - base
        end = int(block["end_pc"]) - base
        kind[start:end] = b"\x01" * (end - start)
        for offset in range(start, end, 4):
            raw = int.from_bytes(code[offset:offset + 4], "little")
            literals.update(_a32_literal_words(base + offset, raw))
            adr = _a32_adr_target(base + offset, raw)
            if adr is not None:
                references.add(adr)
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
        elif word in (0, 0xE1A00000):  # zero word or A32 NOP (MOV r0, r0)
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
        if label in ("literal", "pointer") and base <= (word & ~1) < image_end:
            references.add(word & ~1)
        classes[label] += 1
        position += 1
    # Run-level data classes over what the word classes left.
    reference_offsets = sorted(
        address - base for address in references
        if base <= address < base + executable_size)

    def referenced(start: int, end: int) -> bool:
        index = bisect.bisect_left(reference_offsets, start)
        return index < len(reference_offsets) and reference_offsets[index] < end

    remaining: list[int] = []
    run_classes: Counter[str] = Counter()
    classified_runs: list[str] = []
    for run_start, run_end in _run_bounds(residual):
        start, end = run_start - base, run_end - base
        shaped = _code_shaped(code, start, end)
        label = None if shaped else _run_class(code, start, end)
        if label is None and any(
                low <= run_start and run_end <= high for low, high in data_facts):
            label = "title_data"
        if label is None and not shaped:
            if referenced(start, end):
                label = "addressed"
            else:
                low, high = start, end
                while low > 0 and not kind[low - 1]:
                    low -= 1
                while high < executable_size and not kind[high]:
                    high += 1
                if referenced(low, high):
                    label = "data_island"
        if label is None:
            remaining.extend(range(run_start, run_end))
        else:
            run_classes[label] += end - start
            classified_runs.append(f"0x{run_start:08X}+{run_end - run_start}:{label}")
    for label, count in run_classes.items():
        classes[label] += count
        classes["residual"] -= count
    if classes.get("residual", 1) == 0:
        del classes["residual"]
    residual = remaining
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
        "classified_runs": classified_runs,
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
    parser.add_argument("--data-facts", type=Path, default=None,
                        help="guarded title data facts CSV (start,size,guard_word,meaning)")
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
    data_facts = (load_data_facts(args.data_facts, code, int(program["base"]))
                  if args.data_facts else [])
    result = prove(program, selection, code, args.executable_size,
                   inline_callees, args.entry, data_facts)
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
