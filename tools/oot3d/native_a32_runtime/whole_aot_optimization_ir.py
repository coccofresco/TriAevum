"""Analysis IR for whole-AOT architectural-state optimization.

The existing whole-AOT frontend decodes A32 instructions directly into C++.
This module keeps optimization decisions independent from that backend: guest
state use/def, per-flag liveness, natural CFG regions, and dirty state at
observable exits. Unknown encodings are deliberately conservative.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntFlag
from typing import Iterable, Protocol


class Flag(IntFlag):
    NONE = 0
    V = 1 << 0
    C = 1 << 1
    Z = 1 << 2
    N = 1 << 3
    NZ = N | Z
    NZC = N | Z | C
    NZCV = N | Z | C | V


ALL_GPRS = (1 << 15) - 1


class InstructionLike(Protocol):
    pc: int
    raw: int


class BlockLike(Protocol):
    pc: int
    instructions: tuple[InstructionLike, ...]
    successors: tuple[dict[str, object], ...]


class FunctionLike(Protocol):
    entry: int
    name: str
    blocks: tuple[BlockLike, ...]


@dataclass(frozen=True)
class ArchitecturalEffects:
    gpr_reads: int = 0
    gpr_writes: int = 0
    flag_reads: Flag = Flag.NONE
    flag_writes: Flag = Flag.NONE
    predicated: bool = False
    barrier: bool = False


@dataclass(frozen=True)
class InstructionOptimization:
    pc: int
    effects: ArchitecturalEffects
    live_flags_before: Flag
    live_flags_after: Flag
    required_flag_writes: Flag
    dead_flag_writes: Flag


@dataclass(frozen=True)
class BlockOptimization:
    pc: int
    instructions: tuple[InstructionOptimization, ...]
    live_flags_in: Flag
    live_flags_out: Flag
    dirty_gprs_in: int
    dirty_gprs_out: int
    dirty_flags_in: Flag
    dirty_flags_out: Flag
    observable_exit: bool


@dataclass(frozen=True)
class RegionOptimization:
    header: int
    blocks: tuple[int, ...]
    exits: tuple[tuple[int, int | None, str], ...]
    is_loop: bool


@dataclass(frozen=True)
class FunctionOptimization:
    entry: int
    name: str
    promoted_gprs: int
    promoted_flags: Flag
    blocks: tuple[BlockOptimization, ...]
    regions: tuple[RegionOptimization, ...]

    @property
    def dead_flag_write_count(self) -> int:
        return sum(
            int(item.dead_flag_writes != Flag.NONE)
            for block in self.blocks
            for item in block.instructions
        )

    @property
    def partial_flag_write_count(self) -> int:
        return sum(
            int(
                item.required_flag_writes != Flag.NONE
                and item.required_flag_writes != item.effects.flag_writes
            )
            for block in self.blocks
            for item in block.instructions
        )


def _reg(index: int) -> int:
    return 0 if index == 15 else 1 << index


def _condition_reads(condition: int) -> Flag:
    return {
        0x0: Flag.Z,
        0x1: Flag.Z,
        0x2: Flag.C,
        0x3: Flag.C,
        0x4: Flag.N,
        0x5: Flag.N,
        0x6: Flag.V,
        0x7: Flag.V,
        0x8: Flag.C | Flag.Z,
        0x9: Flag.C | Flag.Z,
        0xA: Flag.N | Flag.V,
        0xB: Flag.N | Flag.V,
        0xC: Flag.N | Flag.Z | Flag.V,
        0xD: Flag.N | Flag.Z | Flag.V,
    }.get(condition, Flag.NONE)


def _operand2_register_reads(raw: int) -> int:
    if raw & (1 << 25):
        return 0
    reads = _reg(raw & 0xF)
    if raw & (1 << 4):
        reads |= _reg((raw >> 8) & 0xF)
    return reads


def _operand2_reads_carry(raw: int, logical_flags: bool) -> bool:
    if raw & (1 << 25):
        rotate = ((raw >> 8) & 0xF) * 2
        return logical_flags and rotate == 0
    shift_type = (raw >> 5) & 0x3
    if raw & (1 << 4):
        return logical_flags
    amount = (raw >> 7) & 0x1F
    return shift_type == 3 and amount == 0 or logical_flags and amount == 0


def instruction_effects(raw: int) -> ArchitecturalEffects:
    """Return conservative A32 architectural effects for optimization.

    The decoder only needs facts used by promotion and flag liveness. The
    execution frontend remains the authority for instruction semantics.
    """

    condition = (raw >> 28) & 0xF
    predicated = condition not in {0xE, 0xF}
    flag_reads = _condition_reads(condition)
    gpr_reads = 0
    gpr_writes = 0
    flag_writes = Flag.NONE

    if raw == 0xF57FF01F or (raw & 0xFFFFFFF0) in {
        0xF57FF040, 0xF57FF050, 0xF57FF060
    } or (raw & 0x0FFFFFFF) == 0x0320F000 or (
        (raw & 0xFF30F000) == 0xF510F000
        or (raw & 0xFF30F010) == 0xF710F000
        or (raw & 0xFF70F000) == 0xF450F000
        or (raw & 0xFF70F010) == 0xF650F000
    ):
        return ArchitecturalEffects(
            gpr_reads, gpr_writes, flag_reads, flag_writes, predicated
        )

    if (raw & 0x0FBF0FFF) == 0x010F0000:
        gpr_writes |= _reg((raw >> 12) & 0xF)
        return ArchitecturalEffects(
            gpr_reads, gpr_writes, flag_reads, flag_writes, predicated
        )

    if (raw & 0x0FB0FFF0) == 0x0120F000:
        gpr_reads |= _reg(raw & 0xF)
        if (raw >> 16) & 0x8:
            flag_writes = Flag.NZCV
        return ArchitecturalEffects(
            gpr_reads, gpr_writes, flag_reads, flag_writes, predicated
        )

    if (raw & 0x0FF00000) in {0x03000000, 0x03400000}:
        gpr_writes |= _reg((raw >> 12) & 0xF)
        return ArchitecturalEffects(
            gpr_reads, gpr_writes, flag_reads, flag_writes, predicated
        )

    if (raw & 0x0FFF0FFF) in {0x0E1D0F70, 0x0E070F9A, 0x0E070FBA}:
        if (raw & 0x0FFF0FFF) == 0x0E1D0F70:
            gpr_writes |= _reg((raw >> 12) & 0xF)
        else:
            gpr_reads |= _reg((raw >> 12) & 0xF)
        return ArchitecturalEffects(
            gpr_reads, gpr_writes, flag_reads, flag_writes, predicated
        )

    if (raw & 0x0E000000) == 0x0A000000 and condition != 0xF:
        if raw & (1 << 24):
            gpr_writes |= _reg(14)
        return ArchitecturalEffects(
            gpr_reads, gpr_writes, flag_reads, flag_writes, predicated
        )

    if (raw & 0x0FFFFFF0) in {0x012FFF10, 0x012FFF30}:
        gpr_reads |= _reg(raw & 0xF)
        if (raw & 0x0FFFFFF0) == 0x012FFF30:
            gpr_writes |= _reg(14)
        return ArchitecturalEffects(
            gpr_reads, gpr_writes, flag_reads, flag_writes, predicated
        )

    if (raw & 0x0E000000) == 0x08000000:
        base = (raw >> 16) & 0xF
        registers = raw & 0x7FFF
        gpr_reads |= _reg(base)
        if raw & (1 << 20):
            gpr_writes |= registers
        else:
            gpr_reads |= registers
        if raw & (1 << 21):
            gpr_writes |= _reg(base)
        return ArchitecturalEffects(
            gpr_reads, gpr_writes, flag_reads, flag_writes, predicated
        )

    opcode_byte = (raw >> 20) & 0xFF
    if (
        condition != 0xF
        and 0x18 <= opcode_byte <= 0x1F
        and (raw & 0x00000FF0) == 0x00000F90
    ):
        base = (raw >> 16) & 0xF
        destination = (raw >> 12) & 0xF
        gpr_reads |= _reg(base)
        gpr_writes |= _reg(destination)
        if opcode_byte in {0x19, 0x1B, 0x1D, 0x1F}:
            gpr_reads |= _reg(raw & 0xF)
        return ArchitecturalEffects(
            gpr_reads, gpr_writes, flag_reads, flag_writes, predicated
        )

    if (raw & 0x0C000000) == 0x04000000:
        base = (raw >> 16) & 0xF
        destination = (raw >> 12) & 0xF
        gpr_reads |= _reg(base)
        if not raw & (1 << 25):
            gpr_reads |= _reg(raw & 0xF)
        if raw & (1 << 20):
            gpr_writes |= _reg(destination)
        else:
            gpr_reads |= _reg(destination)
        if raw & (1 << 21) or not raw & (1 << 24):
            gpr_writes |= _reg(base)
        return ArchitecturalEffects(
            gpr_reads, gpr_writes, flag_reads, flag_writes, predicated
        )

    if (raw & 0x0FC000F0) == 0x00000090:
        gpr_reads |= _reg(raw & 0xF) | _reg((raw >> 8) & 0xF)
        if raw & (1 << 21):
            gpr_reads |= _reg((raw >> 12) & 0xF)
        gpr_writes |= _reg((raw >> 16) & 0xF)
        if raw & (1 << 20):
            flag_writes = Flag.NZ
        return ArchitecturalEffects(
            gpr_reads, gpr_writes, flag_reads, flag_writes, predicated
        )

    if (raw & 0x0FE000F0) in {
        0x00800090, 0x00A00090, 0x00C00090, 0x00E00090
    }:
        gpr_reads |= _reg(raw & 0xF) | _reg((raw >> 8) & 0xF)
        if raw & (1 << 21):
            gpr_reads |= _reg((raw >> 12) & 0xF) | _reg((raw >> 16) & 0xF)
        gpr_writes |= _reg((raw >> 12) & 0xF) | _reg((raw >> 16) & 0xF)
        if raw & (1 << 20):
            flag_writes = Flag.NZ
        return ArchitecturalEffects(
            gpr_reads, gpr_writes, flag_reads, flag_writes, predicated
        )

    if (raw & 0x0FFF0FFF) == 0x0EF10A10:
        core = (raw >> 12) & 0xF
        if core == 15:
            flag_writes = Flag.NZCV
        else:
            gpr_writes |= _reg(core)
        return ArchitecturalEffects(
            gpr_reads, gpr_writes, flag_reads, flag_writes, predicated
        )

    if (raw & 0x0FFF0FFF) == 0x0EE10A10:
        gpr_reads |= _reg((raw >> 12) & 0xF)
        return ArchitecturalEffects(
            gpr_reads, gpr_writes, flag_reads, flag_writes, predicated
        )

    if (raw & 0x0C000000) == 0x0C000000:
        # VFP arithmetic and transfers update VFP state/FPSCR, not NZCV. Core
        # register transfer forms are represented conservatively here; VMRS
        # APSR_nzcv was handled above.
        core_pair = (raw & 0x0FF00FD0) in {0x0C400B10, 0x0C500B10}
        push = (raw & 0x0FBF0E00) == 0x0D2D0A00
        pop = (raw & 0x0FBF0E00) == 0x0CBD0A00
        if core_pair:
            first = (raw >> 12) & 0xF
            second = (raw >> 16) & 0xF
            if raw & (1 << 20):
                gpr_writes |= _reg(first) | _reg(second)
            else:
                gpr_reads |= _reg(first) | _reg(second)
        elif push or pop:
            gpr_reads |= _reg(13)
            gpr_writes |= _reg(13)
        elif (raw & 0x0E000E00) == 0x0C000A00:
            base = (raw >> 16) & 0xF
            gpr_reads |= _reg(base)
            if raw & (1 << 21):
                gpr_writes |= _reg(base)
        elif (raw & 0x0F300E00) in {0x0D000A00, 0x0D100A00}:
            gpr_reads |= _reg((raw >> 16) & 0xF)
        elif (raw & 0x0FF00F7F) == 0x0E000A10:
            gpr_reads |= _reg((raw >> 12) & 0xF)
        elif (raw & 0x0FF00F7F) == 0x0E100A10:
            gpr_writes |= _reg((raw >> 12) & 0xF)
        return ArchitecturalEffects(
            gpr_reads, gpr_writes, flag_reads, flag_writes, predicated
        )

    non_flag_misc = (
        (raw & 0x0FF000D0) in {0x07400010, 0x07400050}
        or (raw & 0x0F000010) == 0x06000010
        or (raw & 0x0FF000F0) in {
            0x01000050, 0x01200050, 0x01400050, 0x01600050,
            0x00400090, 0x00600090,
        }
        or (raw & 0x0FFF0FF0) == 0x016F0F10
        or (raw & 0x0FF00090) in {0x01000080, 0x01400080, 0x01600080}
        or (raw & 0x0E000090) == 0x00000090
    )
    if non_flag_misc:
        # These families have irregular register fields but cannot modify
        # N/Z/C/V. Keep GPR promotion conservative without poisoning flag
        # liveness with false producers.
        return ArchitecturalEffects(
            ALL_GPRS, ALL_GPRS, flag_reads, Flag.NONE, predicated
        )

    if (raw & 0x0C000000) == 0:
        opcode = (raw >> 21) & 0xF
        set_flags = bool(raw & (1 << 20))
        rn = (raw >> 16) & 0xF
        rd = (raw >> 12) & 0xF
        logical = opcode in {0x0, 0x1, 0x8, 0x9, 0xC, 0xD, 0xE, 0xF}
        test = opcode in {0x8, 0x9, 0xA, 0xB}
        if opcode not in {0xD, 0xF}:
            gpr_reads |= _reg(rn)
        gpr_reads |= _operand2_register_reads(raw)
        if not test:
            gpr_writes |= _reg(rd)
        if opcode in {0x5, 0x6, 0x7} or _operand2_reads_carry(
            raw, logical and set_flags
        ):
            flag_reads |= Flag.C
        if set_flags:
            flag_writes = Flag.NZC if logical else Flag.NZCV
        return ArchitecturalEffects(
            gpr_reads, gpr_writes, flag_reads, flag_writes, predicated
        )

    return ArchitecturalEffects(
        ALL_GPRS,
        ALL_GPRS,
        flag_reads | Flag.NZCV,
        Flag.NZCV,
        predicated,
        True,
    )


def _internal_successors(block: BlockLike, block_pcs: set[int]) -> tuple[int, ...]:
    return tuple(
        sorted(
            {
                int(edge["target"])
                for edge in block.successors
                if "target" in edge and int(edge["target"]) in block_pcs
            }
        )
    )


def _has_observable_exit(block: BlockLike, block_pcs: set[int]) -> bool:
    if not block.successors:
        return True
    for edge in block.successors:
        target = int(edge["target"]) if "target" in edge else None
        if target not in block_pcs:
            return True
    return False


def _strongly_connected_regions(
    blocks: tuple[BlockLike, ...]
) -> tuple[RegionOptimization, ...]:
    block_pcs = {block.pc for block in blocks}
    graph = {
        block.pc: _internal_successors(block, block_pcs) for block in blocks
    }
    index = 0
    indices: dict[int, int] = {}
    lowlinks: dict[int, int] = {}
    stack: list[int] = []
    on_stack: set[int] = set()
    components: list[tuple[int, ...]] = []

    def visit(node: int) -> None:
        nonlocal index
        indices[node] = index
        lowlinks[node] = index
        index += 1
        stack.append(node)
        on_stack.add(node)
        for successor in graph[node]:
            if successor not in indices:
                visit(successor)
                lowlinks[node] = min(lowlinks[node], lowlinks[successor])
            elif successor in on_stack:
                lowlinks[node] = min(lowlinks[node], indices[successor])
        if lowlinks[node] == indices[node]:
            component: list[int] = []
            while True:
                member = stack.pop()
                on_stack.remove(member)
                component.append(member)
                if member == node:
                    break
            components.append(tuple(sorted(component)))

    for pc in sorted(graph):
        if pc not in indices:
            visit(pc)

    regions: list[RegionOptimization] = []
    blocks_by_pc = {block.pc: block for block in blocks}
    for component in components:
        members = set(component)
        self_loop = len(component) == 1 and component[0] in graph[component[0]]
        exits: list[tuple[int, int | None, str]] = []
        for pc in component:
            block = blocks_by_pc[pc]
            if not block.successors:
                exits.append((pc, None, "implicit"))
            for edge in block.successors:
                target = int(edge["target"]) if "target" in edge else None
                if target not in members:
                    exits.append((pc, target, str(edge.get("kind", "unknown"))))
        regions.append(
            RegionOptimization(
                min(component), component,
                tuple(sorted(exits, key=lambda item: (
                    item[0], item[1] is None,
                    item[1] if item[1] is not None else 0, item[2]
                ))),
                len(component) > 1 or self_loop,
            )
        )
    return tuple(sorted(regions, key=lambda item: item.header))


def analyze_function(function: FunctionLike) -> FunctionOptimization:
    block_pcs = {block.pc for block in function.blocks}
    effects = {
        block.pc: tuple(instruction_effects(item.raw) for item in block.instructions)
        for block in function.blocks
    }
    successors = {
        block.pc: _internal_successors(block, block_pcs)
        for block in function.blocks
    }
    observable = {
        block.pc: _has_observable_exit(block, block_pcs)
        for block in function.blocks
    }

    live_in = {pc: Flag.NONE for pc in block_pcs}
    live_out = {pc: Flag.NONE for pc in block_pcs}
    changed = True
    while changed:
        changed = False
        for block in reversed(function.blocks):
            pc = block.pc
            outgoing = Flag.NZCV if observable[pc] else Flag.NONE
            for successor in successors[pc]:
                outgoing |= live_in[successor]
            live = outgoing
            for item in reversed(effects[pc]):
                if item.predicated:
                    live = live | item.flag_reads
                else:
                    live = (live & ~item.flag_writes) | item.flag_reads
            if outgoing != live_out[pc] or live != live_in[pc]:
                live_out[pc] = outgoing
                live_in[pc] = live
                changed = True

    dirty_gpr_in = {pc: 0 for pc in block_pcs}
    dirty_gpr_out = {pc: 0 for pc in block_pcs}
    dirty_flag_in = {pc: Flag.NONE for pc in block_pcs}
    dirty_flag_out = {pc: Flag.NONE for pc in block_pcs}
    predecessors: dict[int, list[int]] = {pc: [] for pc in block_pcs}
    for pc, targets in successors.items():
        for target in targets:
            predecessors[target].append(pc)
    changed = True
    while changed:
        changed = False
        for block in function.blocks:
            pc = block.pc
            incoming_gprs = 0
            incoming_flags = Flag.NONE
            for predecessor in predecessors[pc]:
                incoming_gprs |= dirty_gpr_out[predecessor]
                incoming_flags |= dirty_flag_out[predecessor]
            outgoing_gprs = incoming_gprs
            outgoing_flags = incoming_flags
            for item in effects[pc]:
                outgoing_gprs |= item.gpr_writes
                outgoing_flags |= item.flag_writes
            values = (incoming_gprs, incoming_flags, outgoing_gprs, outgoing_flags)
            old = (
                dirty_gpr_in[pc], dirty_flag_in[pc],
                dirty_gpr_out[pc], dirty_flag_out[pc],
            )
            if values != old:
                dirty_gpr_in[pc], dirty_flag_in[pc] = values[:2]
                dirty_gpr_out[pc], dirty_flag_out[pc] = values[2:]
                changed = True

    optimized_blocks: list[BlockOptimization] = []
    promoted_gprs = 0
    promoted_flags = Flag.NONE
    for block in function.blocks:
        live = live_out[block.pc]
        reverse: list[InstructionOptimization] = []
        for instruction, item in reversed(tuple(zip(block.instructions, effects[block.pc]))):
            required = item.flag_writes & live
            dead = item.flag_writes & ~live
            if item.predicated:
                before = live | item.flag_reads
            else:
                before = (live & ~item.flag_writes) | item.flag_reads
            reverse.append(
                InstructionOptimization(
                    instruction.pc, item, before, live, required, dead
                )
            )
            live = before
            promoted_gprs |= item.gpr_reads | item.gpr_writes
            promoted_flags |= item.flag_reads | item.flag_writes
        optimized_blocks.append(
            BlockOptimization(
                block.pc,
                tuple(reversed(reverse)),
                live_in[block.pc],
                live_out[block.pc],
                dirty_gpr_in[block.pc],
                dirty_gpr_out[block.pc],
                dirty_flag_in[block.pc],
                dirty_flag_out[block.pc],
                observable[block.pc],
            )
        )

    return FunctionOptimization(
        function.entry,
        function.name,
        promoted_gprs,
        promoted_flags,
        tuple(optimized_blocks),
        _strongly_connected_regions(function.blocks),
    )


def analyze_functions(
    functions: Iterable[FunctionLike],
) -> tuple[FunctionOptimization, ...]:
    return tuple(analyze_function(function) for function in functions)


def register_names(mask: int) -> tuple[str, ...]:
    return tuple(f"r{index}" for index in range(15) if mask & (1 << index))


def flag_names(mask: Flag) -> tuple[str, ...]:
    return tuple(name for flag, name in (
        (Flag.N, "N"), (Flag.Z, "Z"), (Flag.C, "C"), (Flag.V, "V")
    ) if mask & flag)
