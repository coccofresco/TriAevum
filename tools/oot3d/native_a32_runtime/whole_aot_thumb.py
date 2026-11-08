"""Thumb (T16 + BL/BLX pair) decoding for the whole-AOT program.

Shared mechanism, no title facts: a title supplies its Thumb ranges
(``--thumb-ranges``).  A title without Thumb ranges never reaches this module.

Each accepted Thumb instruction becomes a record that the C++ lowering
(whole_aot_cpp.py) consumes:

* ``pc`` is the instruction address with bit 0 set.  That is the address the
  runtime dispatches in Thumb state (state.r[15] carries the Thumb bit, as a
  BX/BLX target and a Thumb link value do), so block labels, dispatch entries,
  resume and fallthrough targets of Thumb code are all odd.
* ``raw`` is an A32 *proxy* encoding with exactly the same architectural
  effect on registers, flags and memory (Thumb low-register data processing
  sets flags like the A32 S form; LDR literal is re-based on the odd pc so
  that ``pc + 8 + offset`` equals ``Align(address + 4, 4) + imm``).  The
  optimizer's liveness analysis and the A32 emitters read it unchanged.
* ``kind`` selects Thumb-specific lowering where no proxy exists: ``const``
  (ADR: constant value), ``add_pc`` (hi-register ADD of the Thumb PC value
  ``address + 4``), and the control transfers ``b``, ``bcc``, ``bl``,
  ``blx_imm``, ``blx_reg``, ``bx``, ``pop_pc`` and ``udf`` (a trap
  without successors).

The decoder accepts exactly the Thumb forms the MM3D EUR code uses (Thumb-1 incl. UDF:
no IT blocks, no other 32-bit Thumb-2 forms) and REJECTS every other
encoding with ThumbDecodeError, so coverage remains provable.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable


class ThumbDecodeError(ValueError):
    pass


_CONDITIONS = ("eq", "ne", "hs", "lo", "mi", "pl", "vs", "vc",
               "hi", "ls", "ge", "lt", "gt", "le")


@dataclass(frozen=True, slots=True)
class ThumbInstruction:
    address: int          # even address of the first halfword
    size: int             # 2 or 4
    thumb: int            # T16 halfword, or (first << 16) | second
    raw: int              # A32 proxy encoding
    kind: str             # "a32", "const", "add_pc", "ldr_literal", or a control kind
    mnemonic: str
    target: int | None = None     # control target (bit 0 = Thumb) or constant
    condition: str | None = None
    literal: int | None = None    # address of a literal word read (LDR literal)

    @property
    def pc(self) -> int:
        return self.address | 1

    @property
    def next_pc(self) -> int:
        return (self.address + self.size) | 1

    def to_dict(self) -> dict[str, object]:
        result: dict[str, object] = {
            "pc": self.pc, "size": self.size, "thumb": self.thumb,
            "raw": self.raw, "kind": self.kind, "mnemonic": self.mnemonic,
        }
        if self.target is not None:
            result["target"] = self.target
        if self.condition is not None:
            result["condition"] = self.condition
        if self.literal is not None:
            result["literal"] = self.literal
        return result


TERMINAL_KINDS = frozenset({"b", "bcc", "bl", "blx_imm", "blx_reg", "bx", "pop_pc", "udf"})
_ALU = {
    0x0: ("ands", 0xE0100000), 0x1: ("eors", 0xE0300000),
    0x5: ("adcs", 0xE0B00000), 0x6: ("sbcs", 0xE0D00000),
    0xC: ("orrs", 0xE1900000), 0xE: ("bics", 0xE1D00000),
}


def _halfword(code: bytes, base: int, address: int) -> int:
    offset = address - base
    if offset < 0 or offset + 2 > len(code) or address & 1:
        raise ThumbDecodeError(f"Thumb address 0x{address:08X} is outside code")
    return int.from_bytes(code[offset:offset + 2], "little")


def _sign(value: int, bits: int) -> int:
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def _literal_proxy(rd: int, odd_pc: int, literal: int) -> int:
    # A32 LDR rd, [pc, #off] lowers with base odd_pc + 8.
    offset = literal - (odd_pc + 8)
    up = 1 if offset >= 0 else 0
    magnitude = abs(offset)
    if magnitude > 0xFFF:
        raise ThumbDecodeError("literal offset out of range")
    return 0xE51F0000 | (up << 23) | (rd << 12) | magnitude


def decode(code: bytes, base: int, address: int) -> ThumbInstruction:
    h = _halfword(code, base, address)
    pc = address | 1
    a = address

    def make(raw: int, mnemonic: str, kind: str = "a32", **extra) -> ThumbInstruction:
        return ThumbInstruction(a, 2, h, raw & 0xFFFFFFFF, kind, mnemonic, **extra)

    top5 = h >> 11
    rd = h & 7
    rm3 = (h >> 3) & 7
    if top5 <= 2:  # LSL/LSR/ASR #imm5
        op = top5
        imm5 = (h >> 6) & 0x1F
        return make(0xE1B00000 | (rd << 12) | (imm5 << 7) | (op << 5) | rm3,
                    ("lsls", "lsrs", "asrs")[op])
    if top5 == 3:  # ADDS/SUBS register or imm3
        immediate = (h >> 10) & 1
        sub = (h >> 9) & 1
        field = (h >> 6) & 7
        rn = rm3
        if immediate:
            raw = (0xE2500000 if sub else 0xE2900000) | (rn << 16) | (rd << 12) | field
        else:
            raw = (0xE0500000 if sub else 0xE0900000) | (rn << 16) | (rd << 12) | field
        return make(raw, "subs" if sub else "adds")
    if (h >> 13) == 1:  # MOVS/CMP/ADDS/SUBS imm8
        op = (h >> 11) & 3
        rdn = (h >> 8) & 7
        imm8 = h & 0xFF
        raw = (0xE3B00000 | (rdn << 12),
               0xE3500000 | (rdn << 16),
               0xE2900000 | (rdn << 16) | (rdn << 12),
               0xE2500000 | (rdn << 16) | (rdn << 12))[op] | imm8
        return make(raw, ("movs", "cmp", "adds", "subs")[op])
    if (h >> 10) == 0x10:  # data processing register
        op = (h >> 6) & 0xF
        rdn = rd
        rm = rm3
        if op in _ALU:
            name, opcode = _ALU[op]
            return make(opcode | (rdn << 16) | (rdn << 12) | rm, name)
        if op in (0x2, 0x3, 0x4, 0x7):  # LSL/LSR/ASR/ROR register
            shift = {0x2: 0, 0x3: 1, 0x4: 2, 0x7: 3}[op]
            return make(0xE1B00010 | (rdn << 12) | (rm << 8) | (shift << 5) | rdn,
                        ("lsls", "lsrs", "asrs", "rors")[shift])
        if op == 0x8:
            return make(0xE1100000 | (rdn << 16) | rm, "tst")
        if op == 0x9:
            return make(0xE2700000 | (rm << 16) | (rdn << 12), "rsbs")
        if op == 0xA:
            return make(0xE1500000 | (rdn << 16) | rm, "cmp")
        if op == 0xB:
            return make(0xE1700000 | (rdn << 16) | rm, "cmn")
        if op == 0xD:
            return make(0xE0100090 | (rdn << 16) | (rdn << 8) | rm, "muls")
        if op == 0xF:
            return make(0xE1F00000 | (rdn << 12) | rm, "mvns")
    if (h >> 10) == 0x11:  # hi-register operations and branch/exchange
        op = (h >> 8) & 3
        rm = (h >> 3) & 0xF
        rdn = ((h >> 4) & 8) | rd
        if op == 3:
            if h & 7:
                raise ThumbDecodeError(f"BX/BLX SBZ bits at 0x{a:08X}")
            if rm == 15:
                raise ThumbDecodeError(f"BX/BLX pc at 0x{a:08X}")
            if h & 0x80:
                return make(0xE12FFF30 | rm, "blx", "blx_reg")
            return make(0xE12FFF10 | rm, "bx", "bx")
        if rdn == 15:
            raise ThumbDecodeError(f"hi-register write to pc at 0x{a:08X}")
        if op == 0:
            if rm == 15:
                # ADD rdn, pc reads the Thumb PC value address + 4.
                return make(0xE2800000 | (rdn << 16) | (rdn << 12), "add",
                            "add_pc", target=(a + 4) & 0xFFFFFFFF)
            return make(0xE0800000 | (rdn << 16) | (rdn << 12) | rm, "add")
        if rm == 15:
            raise ThumbDecodeError(f"hi-register pc operand at 0x{a:08X}")
        if op == 1:
            return make(0xE1500000 | (rdn << 16) | rm, "cmp")
        return make(0xE1A00000 | (rdn << 12) | rm, "mov")
    if top5 == 9:  # LDR literal
        rt = (h >> 8) & 7
        literal = ((a + 4) & ~3) + (h & 0xFF) * 4
        return make(_literal_proxy(rt, pc, literal), "ldr", "ldr_literal", literal=literal)
    if (h >> 12) == 5:  # register offset
        op = (h >> 9) & 7
        rm = (h >> 6) & 7
        rn = rm3
        raw = (0xE7800000, 0xE18000B0, 0xE7C00000, 0xE19000D0,
               0xE7900000, 0xE19000B0, 0xE7D00000, 0xE19000F0)[op]
        return make(raw | (rn << 16) | (rd << 12) | rm,
                    ("str", "strh", "strb", "ldrsb", "ldr", "ldrh", "ldrb", "ldrsh")[op])
    if (h >> 13) == 3:  # STR/LDR/STRB/LDRB imm5
        byte = (h >> 12) & 1
        load = (h >> 11) & 1
        imm5 = (h >> 6) & 0x1F
        offset = imm5 if byte else imm5 * 4
        raw = 0xE5800000 | (byte << 22) | (load << 20) | (rm3 << 16) | (rd << 12) | offset
        return make(raw, ("str", "ldr", "strb", "ldrb")[byte * 2 + load])
    if (h >> 12) == 8:  # STRH/LDRH imm5
        load = (h >> 11) & 1
        offset = ((h >> 6) & 0x1F) * 2
        raw = (0xE1D000B0 if load else 0xE1C000B0) | (rm3 << 16) | (rd << 12) | \
            ((offset >> 4) << 8) | (offset & 0xF)
        return make(raw, "ldrh" if load else "strh")
    if (h >> 12) == 9:  # STR/LDR [sp, #imm8*4]
        load = (h >> 11) & 1
        rt = (h >> 8) & 7
        raw = 0xE58D0000 | (load << 20) | (rt << 12) | ((h & 0xFF) * 4)
        return make(raw, "ldr" if load else "str")
    if (h >> 12) == 0xA:  # ADR / ADD rd, sp, #imm8*4
        rdx = (h >> 8) & 7
        imm = (h & 0xFF) * 4
        if h & 0x800:
            return make(0xE28D0F00 | (rdx << 12) | (h & 0xFF), "add")
        value = ((a + 4) & ~3) + imm
        return make(0xE3000000 | (rdx << 12), "adr", "const", target=value)
    if (h >> 8) == 0xB0:  # ADD/SUB sp, sp, #imm7*4
        imm7 = h & 0x7F
        raw = (0xE24DDF00 if h & 0x80 else 0xE28DDF00) | imm7
        return make(raw, "sub" if h & 0x80 else "add")
    if (h & 0xF600) == 0xB400:  # PUSH / POP
        pop = (h >> 11) & 1
        extra = (h >> 8) & 1
        registers = h & 0xFF
        if not registers and not extra:
            raise ThumbDecodeError(f"empty PUSH/POP at 0x{a:08X}")
        if pop:
            raw = 0xE8BD0000 | registers | (extra << 15)
            return make(raw, "pop", "pop_pc" if extra else "a32")
        return make(0xE92D0000 | registers | (extra << 14), "push")
    if (h >> 12) == 0xC:  # STMIA / LDMIA rn!
        load = (h >> 11) & 1
        rn = (h >> 8) & 7
        registers = h & 0xFF
        if not registers:
            raise ThumbDecodeError(f"empty LDM/STM at 0x{a:08X}")
        writeback = 0 if load and registers & (1 << rn) else 1
        raw = 0xE8800000 | (writeback << 21) | (load << 20) | (rn << 16) | registers
        return make(raw, "ldm" if load else "stm")
    if (h >> 12) == 0xD:  # Bcc
        cond = (h >> 8) & 0xF
        if cond == 0xE:  # UDF #imm8: a permanently undefined trap
            imm = h & 0xFF
            return make(0xE7F000F0 | ((imm & 0xF0) << 4) | (imm & 0xF), "udf", "udf")
        if cond == 0xF:
            raise ThumbDecodeError(f"SVC at 0x{a:08X} is not an accepted Thumb form")
        target = ((a + 4 + _sign(h & 0xFF, 8) * 2) & 0xFFFFFFFF) | 1
        return make((cond << 28) | 0x0A000000, "b" + _CONDITIONS[cond], "bcc",
                    target=target, condition=_CONDITIONS[cond])
    if top5 == 0x1C:  # B
        target = ((a + 4 + _sign(h & 0x7FF, 11) * 2) & 0xFFFFFFFF) | 1
        return make(0xEA000000, "b", "b", target=target)
    if top5 == 0x1E:  # BL/BLX prefix: must pair with the suffix
        second = _halfword(code, base, a + 2)
        suffix = second >> 11
        if suffix not in (0x1F, 0x1D):
            raise ThumbDecodeError(f"unpaired BL prefix at 0x{a:08X}")
        offset = (_sign(h & 0x7FF, 11) << 12) | ((second & 0x7FF) << 1)
        destination = (a + 4 + offset) & 0xFFFFFFFF
        thumb = (h << 16) | second
        if suffix == 0x1F:
            return ThumbInstruction(a, 4, thumb, 0xEB000000, "bl", "bl",
                                    target=destination | 1)
        if second & 1:
            raise ThumbDecodeError(f"BLX suffix with H bit at 0x{a:08X}")
        return ThumbInstruction(a, 4, thumb, 0xFA000000, "blx_imm", "blx",
                                target=destination & ~3)
    raise ThumbDecodeError(f"Thumb encoding 0x{h:04X} at 0x{a:08X} is not an accepted form")


def successors(item: ThumbInstruction) -> tuple[tuple[str, int | None, str | None], ...]:
    """(kind, target, condition) edges in the program's edge vocabulary."""
    kind = item.kind
    if kind == "b":
        return (("branch", item.target, None),)
    if kind == "bcc":
        return (("branch", item.target, item.condition),
                ("fallthrough", item.next_pc, item.condition))
    if kind in ("bl", "blx_imm"):
        return (("direct_call", item.target, None), ("resume", item.next_pc, None))
    if kind == "blx_reg":
        return (("indirect_call", None, None), ("resume", item.next_pc, None))
    if kind == "bx":
        rm = item.raw & 0xF
        return (("return", None, None),) if rm == 14 else (("indirect_branch", None, None),)
    if kind == "pop_pc":
        return (("return", None, None),)
    if kind == "udf":  # traps: no successor
        return ()
    return (("fallthrough", item.next_pc, None),)


@dataclass(frozen=True, slots=True)
class ThumbFlow:
    instructions: dict[int, ThumbInstruction]   # by even address
    block_starts: frozenset[int]                # odd pcs
    entries: frozenset[int]                     # odd pcs of Thumb function entries
    arm_call_targets: frozenset[int]            # even A32 targets of BLX imm
    literal_words: frozenset[int]               # aligned addresses read as literals
    # Calls whose link address lies outside the Thumb ranges (a call to a
    # function that does not return, placed last in its range).  They have
    # no resume edge: an unexpected return dispatches an unknown pc.
    noreturn_calls: frozenset[int] = frozenset()

    def edges(self, item: ThumbInstruction) -> tuple[tuple[str, int | None, str | None], ...]:
        result = successors(item)
        if item.pc in self.noreturn_calls:
            result = tuple(edge for edge in result if edge[0] != "resume")
        return result


def walk(code: bytes, base: int, ranges: Iterable[tuple[int, int]],
         extra_entries: Iterable[int] = (), *, range_entries: bool = True,
         region=None) -> ThumbFlow:
    """Decode every Thumb instruction reachable from the range starts.

    ``ranges`` hold (start, end_exclusive) pairs.  Control may not leave the
    Thumb ranges except through BLX imm (A32 callee) or register transfers.
    """
    spans = tuple(sorted(ranges))

    def inside(address: int) -> bool:
        if any(start <= address < end for start, end in spans):
            return True
        return region is not None and region(address)

    entries = {start | 1 for start, _ in spans} if range_entries else set()
    entries |= {e | 1 for e in extra_entries}
    for entry in entries:
        if not inside(entry & ~1):
            raise ThumbDecodeError(f"Thumb entry 0x{entry:08X} is outside the ranges")
    instructions: dict[int, ThumbInstruction] = {}
    block_starts = set(entries)
    arm_targets: set[int] = set()
    literals: set[int] = set()
    noreturn: set[int] = set()
    # A call that ends a listed range never returns: its callee is a
    # no-return function everywhere (abort/terminate style helpers).
    noreturn_callees: set[int] = set()
    for start, end in spans:
        if end - start >= 4:
            try:
                last = decode(code, base, end - 4)
            except ThumbDecodeError:
                continue
            if last.size == 4 and last.kind in ("bl", "blx_imm"):
                noreturn_callees.add(last.target)
    work = sorted(entries)
    while work:
        address = work.pop() & ~1
        while address not in instructions:
            if not inside(address):
                raise ThumbDecodeError(f"Thumb flow leaves the ranges at 0x{address:08X}")
            item = decode(code, base, address)
            if item.size == 4 and not inside(address + 2):
                raise ThumbDecodeError(f"BL pair crosses a range end at 0x{address:08X}")
            instructions[address] = item
            if item.literal is not None:
                literals.add(item.literal)
            follow = None
            for kind, target, _ in successors(item):
                if kind == "direct_call":
                    if item.kind == "bl":
                        if target not in entries:
                            entries.add(target)
                            block_starts.add(target)
                            work.append(target)
                    else:
                        arm_targets.add(target)
                elif kind == "branch":
                    block_starts.add(target)
                    work.append(target)
                elif kind == "resume" and (
                    not inside(target & ~1) or item.target in noreturn_callees
                ):
                    noreturn.add(item.pc)
                elif kind in ("fallthrough", "resume"):
                    follow = target
            if item.kind in TERMINAL_KINDS:
                block_starts.add(item.next_pc)
            if follow is None:
                break
            if item.kind in TERMINAL_KINDS:
                block_starts.add(follow)
            address = follow & ~1
    # Next-after-terminal markers past unreachable bytes are not blocks.
    block_starts = {target for target in block_starts if (target & ~1) in instructions}
    overlap = {
        literal for literal in literals
        for offset in (0, 2) if literal + offset in instructions
    }
    if overlap:
        raise ThumbDecodeError(
            "literal words decoded as code: " + ", ".join(f"0x{x:08X}" for x in sorted(overlap)))
    for address, item in instructions.items():
        if item.size == 4 and address + 2 in instructions:
            raise ThumbDecodeError(f"overlapping Thumb decode at 0x{address + 2:08X}")
    return ThumbFlow(instructions, frozenset(block_starts), frozenset(entries),
                     frozenset(arm_targets), frozenset(literals), frozenset(noreturn))


def make_blocks(flow: ThumbFlow) -> list[tuple[int, list[ThumbInstruction]]]:
    """Group reachable Thumb instructions into basic blocks (odd start pcs)."""
    blocks: list[tuple[int, list[ThumbInstruction]]] = []
    current: list[ThumbInstruction] = []
    for address in sorted(flow.instructions):
        item = flow.instructions[address]
        if current and (
            current[-1].address + current[-1].size != address
            or item.pc in flow.block_starts
            or current[-1].kind in TERMINAL_KINDS
        ):
            blocks.append((current[0].pc, current))
            current = []
        current.append(item)
    if current:
        blocks.append((current[0].pc, current))
    return blocks


@dataclass(frozen=True, slots=True)
class ThumbDiscovery:
    flow: ThumbFlow
    origins: dict[int, str]          # odd entry -> range|function|pointer|residue|bl_target
    data_runs: tuple[tuple[int, int], ...]   # [start, end) bytes inside ranges that are not code
    pointer_sites: dict[int, tuple[int, ...]]  # odd entry -> word addresses naming it


def covered_bytes(flow: ThumbFlow) -> set[int]:
    covered: set[int] = set()
    for address, item in flow.instructions.items():
        covered.update(range(address, address + item.size))
    for literal in flow.literal_words:
        covered.update(range(literal, literal + 4))
    return covered


def discover(code: bytes, base: int, ranges: Iterable[tuple[int, int]],
             function_entries: Iterable[int] = (), region=None) -> ThumbDiscovery:
    """Entries of every Thumb byte.

    Listed functions, address-taken Thumb pointers (aligned words of the
    image holding address|1 inside a range), Thumb BL targets and, to a fixed
    point, residue runs that decode as closed Thumb code.  A residue run that
    does not decode is data; the completeness proof lists it explicitly.

    ``region`` (optional) admits addresses outside the listed ranges that
    no A32 code owns: Thumb flow and address-taken Thumb pointers may reach
    them (the listed ranges are a lower bound of the Thumb code).  Residue
    runs are only searched inside the listed ranges.
    """
    spans = tuple(sorted(ranges))

    def inside(address: int) -> bool:
        if any(start <= address < end for start, end in spans):
            return True
        return region is not None and region(address)

    origins: dict[int, str] = {}
    for start, _ in spans:
        origins.setdefault(start | 1, "range")
    for entry in function_entries:
        origins.setdefault(entry | 1, "function")
    pointer_sites: dict[int, list[int]] = {}
    for offset in range(0, len(code) - 3, 4):
        word = int.from_bytes(code[offset:offset + 4], "little")
        if word & 1 and inside(word & ~1):
            pointer_sites.setdefault(word, []).append(base + offset)
    for pointer in sorted(pointer_sites):
        origins.setdefault(pointer, "pointer")
    rejected: set[int] = set()
    while True:
        try:
            flow = walk(code, base, spans, (entry & ~1 for entry in origins), region=region)
        except ThumbDecodeError:
            # A pointer-shaped data word may name non-code: drop pointer and
            # residue entries that do not decode by themselves, then retry.
            bad = []
            for entry, origin in sorted(origins.items()):
                if origin not in ("pointer", "residue"):
                    continue
                try:
                    walk(code, base, spans, (entry & ~1,), region=region)
                except ThumbDecodeError:
                    bad.append(entry)
            if not bad:
                raise
            for entry in bad:
                origins.pop(entry)
                pointer_sites.pop(entry, None)
                rejected.add(entry)
            continue
        covered = covered_bytes(flow)
        residue_start = None
        for start, end in spans:
            for address in range(start, end, 2):
                if address not in covered and (address | 1) not in rejected:
                    residue_start = address
                    break
            if residue_start is not None:
                break
        if residue_start is None:
            break
        try:
            walk(code, base, spans, (residue_start,), region=region)
            origins[residue_start | 1] = "residue"
        except ThumbDecodeError:
            rejected.add(residue_start | 1)
    for entry in flow.entries:
        origins.setdefault(entry, "bl_target")
    covered = covered_bytes(flow)
    data_runs: list[tuple[int, int]] = []
    for start, end in spans:
        run = None
        for address in range(start, end, 2):
            if address in covered:
                if run is not None:
                    data_runs.append((run, address))
                    run = None
            elif run is None:
                run = address
        if run is not None:
            data_runs.append((run, end))
    return ThumbDiscovery(flow, origins, tuple(data_runs),
                          {k: tuple(v) for k, v in pointer_sites.items()})
