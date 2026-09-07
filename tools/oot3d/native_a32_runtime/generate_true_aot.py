"""Generate direct C++ CFG regions for manifest-selected OOT3D A32 blocks."""

from __future__ import annotations

import hashlib
import json
import re
from dataclasses import dataclass
from pathlib import Path


FORMAT = "oot3d_true_aot_blocks_v1"
OUTPUT_FORMAT = "oot3d_true_aot_generated_v1"
HEADER_NAME = "oot3d_a32_true_aot_generated.h"
SOURCE_NAME = "oot3d_a32_true_aot_generated.cpp"
MANIFEST_NAME = "true_aot_manifest.json"


@dataclass(frozen=True)
class Region:
    pc: int
    end_pc: int
    symbol: str
    entry_points: tuple[int, ...]
    words: tuple[int, ...]


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _write_if_different(path: Path, data: bytes) -> None:
    if path.is_file() and path.read_bytes() == data:
        return
    path.write_bytes(data)


def _rotate_right(value: int, amount: int) -> int:
    amount &= 31
    if amount == 0:
        return value & 0xFFFFFFFF
    return ((value >> amount) | (value << (32 - amount))) & 0xFFFFFFFF


def _branch_target(pc: int, raw: int) -> int:
    displacement = raw & 0x00FFFFFF
    if displacement & 0x00800000:
        displacement -= 0x01000000
    return (pc + 8 + displacement * 4) & 0xFFFFFFFF


def _symbol(value: str, pc: int) -> str:
    sanitized = re.sub(r"[^A-Za-z0-9_]", "_", value)
    if not sanitized or sanitized[0].isdigit():
        sanitized = "Block_" + sanitized
    return f"{sanitized}_{pc:08X}"


def _condition(raw: int) -> int:
    return (raw >> 28) & 0xF


def _operand(raw: int, pc: int, set_flags: bool) -> str:
    if raw & (1 << 25):
        immediate = raw & 0xFF
        rotate = ((raw >> 8) & 0xF) * 2
        return f"0x{_rotate_right(immediate, rotate):08X}U"
    register = raw & 0xF
    if register == 15:
        raise ValueError("PC operand is not yet true-AOT supported")
    if raw & (1 << 4):
        raise ValueError(
            f"register-controlled shift at 0x{pc:08X} is unsupported"
        )
    shift = (raw >> 7) & 0x1F
    shift_type = (raw >> 5) & 0x3
    if shift == 0 and shift_type == 0:
        return f"state.r[{register}]"
    if shift == 0 and shift_type == 3:
        raise ValueError(f"RRX operand at 0x{pc:08X} is unsupported")
    if set_flags:
        raise ValueError(
            f"flag-setting shifted operand at 0x{pc:08X} is unsupported"
        )
    methods = ("ShiftLsl", "ShiftLsr", "ShiftAsr", "ShiftRor")
    return f"{methods[shift_type]}(state.r[{register}], {shift}U)"


def _emit_data_processing(pc: int, raw: int) -> list[str]:
    opcode = (raw >> 21) & 0xF
    set_flags = bool(raw & (1 << 20))
    rn = (raw >> 16) & 0xF
    rd = (raw >> 12) & 0xF
    if rn == 15 or rd == 15:
        raise ValueError(f"PC data operand at 0x{pc:08X} is unsupported")
    operand = _operand(raw, pc, set_flags)
    condition = _condition(raw)
    lines = [f"if (ConditionPassed(0x{condition:X}U, state.cpsr)) {{"]
    if opcode == 0x4:
        lines.append(f"    const uint32_t value = state.r[{rn}] + {operand};")
        if set_flags:
            lines.append(
                f"    UpdateAddFlags(state, state.r[{rn}], {operand}, value);"
            )
        lines.append(f"    state.r[{rd}] = value;")
    elif opcode == 0x2:
        lines.append(f"    const uint32_t value = state.r[{rn}] - {operand};")
        if set_flags:
            lines.append(
                f"    UpdateSubtractFlags(state, state.r[{rn}], {operand}, value);"
            )
        lines.append(f"    state.r[{rd}] = value;")
    elif opcode == 0x3:
        lines.append(f"    const uint32_t value = {operand} - state.r[{rn}];")
        if set_flags:
            lines.append(
                f"    UpdateSubtractFlags(state, {operand}, state.r[{rn}], value);"
            )
        lines.append(f"    state.r[{rd}] = value;")
    elif opcode == 0xA:
        lines.append(f"    const uint32_t value = state.r[{rn}] - {operand};")
        lines.append(
            f"    UpdateSubtractFlags(state, state.r[{rn}], {operand}, value);"
        )
    elif opcode == 0xD:
        lines.append(f"    const uint32_t value = {operand};")
        if set_flags:
            lines.append("    UpdateLogicalFlags(state, value);")
        lines.append(f"    state.r[{rd}] = value;")
    elif opcode == 0xE:
        lines.append(f"    const uint32_t value = state.r[{rn}] & ~({operand});")
        if set_flags:
            lines.append("    UpdateLogicalFlags(state, value);")
        lines.append(f"    state.r[{rd}] = value;")
    else:
        raise ValueError(
            f"data-processing opcode {opcode} at 0x{pc:08X} is unsupported"
        )
    lines.append("}")
    return lines


def _emit_memory(pc: int, raw: int) -> list[str]:
    register_offset = bool(raw & (1 << 25))
    pre_index = bool(raw & (1 << 24))
    add_offset = bool(raw & (1 << 23))
    byte_transfer = bool(raw & (1 << 22))
    writeback = bool(raw & (1 << 21)) or not pre_index
    load = bool(raw & (1 << 20))
    rn = (raw >> 16) & 0xF
    rd = (raw >> 12) & 0xF
    if register_offset:
        if raw & (1 << 4):
            raise ValueError(
                f"register-controlled memory offset at 0x{pc:08X} is unsupported"
            )
        rm = raw & 0xF
        if rm == 15:
            raise ValueError(f"PC memory offset at 0x{pc:08X} is unsupported")
        shift = (raw >> 7) & 0x1F
        shift_type = (raw >> 5) & 0x3
        if shift == 0 and shift_type == 0:
            offset_expression = f"state.r[{rm}]"
        elif shift == 0 and shift_type == 3:
            raise ValueError(f"RRX memory offset at 0x{pc:08X} is unsupported")
        else:
            methods = ("ShiftLsl", "ShiftLsr", "ShiftAsr", "ShiftRor")
            offset_expression = f"{methods[shift_type]}(state.r[{rm}], {shift}U)"
    else:
        offset_expression = f"0x{raw & 0xFFF:X}U"
    if rd == 15 or (load and writeback and rn == rd):
        raise ValueError(f"unsupported memory register use at 0x{pc:08X}")
    operator = "+" if add_offset else "-"
    if rn == 15:
        if not load or not pre_index or writeback:
            raise ValueError(f"unsupported PC-relative memory use at 0x{pc:08X}")
        base = f"0x{pc + 8:08X}U"
    else:
        base = f"state.r[{rn}]"
    updated = f"{base} {operator} {offset_expression}"
    address = updated if pre_index else base
    condition = _condition(raw)
    lines = [f"if (ConditionPassed(0x{condition:X}U, state.cpsr)) {{"]
    lines.append(f"    const uint32_t address = {address};")
    if load:
        value_type = "uint8_t" if byte_transfer else "uint32_t"
        method = "Read8" if byte_transfer else "Read32"
        lines.extend(
            [
                f"    {value_type} value = 0U;",
                f"    if (!memory.{method}(address, &value)) {{",
                f"        return MemoryFault(0x{pc:08X}U, address, state, result, stats);",
                "    }",
                f"    state.r[{rd}] = value;",
            ]
        )
    else:
        method = "Write8" if byte_transfer else "Write32"
        value = (
            f"static_cast<uint8_t>(state.r[{rd}])"
            if byte_transfer
            else f"state.r[{rd}]"
        )
        lines.extend(
            [
                f"    if (!memory.{method}(address, {value})) {{",
                f"        return MemoryFault(0x{pc:08X}U, address, state, result, stats);",
                "    }",
            ]
        )
    if writeback:
        lines.append(f"    state.r[{rn}] = {updated};")
    lines.append("}")
    return lines


def _emit_halfword_memory(pc: int, raw: int) -> list[str]:
    pre_index = bool(raw & (1 << 24))
    add_offset = bool(raw & (1 << 23))
    immediate_offset = bool(raw & (1 << 22))
    writeback = bool(raw & (1 << 21)) or not pre_index
    load = bool(raw & (1 << 20))
    rn = (raw >> 16) & 0xF
    rd = (raw >> 12) & 0xF
    transfer_type = (raw >> 5) & 0x3
    if not immediate_offset or transfer_type == 0 or (
        not load and transfer_type != 0x1
    ):
        raise ValueError(
            f"halfword transfer 0x{raw:08X} at 0x{pc:08X} is unsupported"
        )
    if rn == 15 or rd == 15 or (writeback and rn == rd):
        raise ValueError(f"unsupported halfword register use at 0x{pc:08X}")
    offset = ((raw >> 4) & 0xF0) | (raw & 0xF)
    operator = "+" if add_offset else "-"
    updated = f"state.r[{rn}] {operator} 0x{offset:X}U"
    address = updated if pre_index else f"state.r[{rn}]"
    condition = _condition(raw)
    lines = [f"if (ConditionPassed(0x{condition:X}U, state.cpsr)) {{"]
    lines.append(f"    const uint32_t address = {address};")
    if not load:
        lines.extend(
            [
                f"    if (!memory.Write16(address, static_cast<uint16_t>(state.r[{rd}]))) {{",
                f"        return MemoryFault(0x{pc:08X}U, address, state, result, stats);",
                "    }",
            ]
        )
    elif transfer_type == 0x2:
        lines.extend(
            [
                "    uint8_t value = 0U;",
                "    if (!memory.Read8(address, &value)) {",
                f"        return MemoryFault(0x{pc:08X}U, address, state, result, stats);",
                "    }",
                f"    state.r[{rd}] = static_cast<uint32_t>(",
                "        static_cast<int32_t>(static_cast<int8_t>(value)));",
            ]
        )
    else:
        lines.extend(
            [
                "    uint16_t value = 0U;",
                "    if (!memory.Read16(address, &value)) {",
                f"        return MemoryFault(0x{pc:08X}U, address, state, result, stats);",
                "    }",
            ]
        )
        if transfer_type == 0x3:
            lines.extend(
                [
                    f"    state.r[{rd}] = static_cast<uint32_t>(",
                    "        static_cast<int32_t>(static_cast<int16_t>(value)));",
                ]
            )
        else:
            lines.append(f"    state.r[{rd}] = value;")
    if writeback:
        lines.append(f"    state.r[{rn}] = {updated};")
    lines.append("}")
    return lines


def _emit_block_transfer(pc: int, raw: int) -> list[str]:
    pre_index = bool(raw & (1 << 24))
    add_offset = bool(raw & (1 << 23))
    user_registers = bool(raw & (1 << 22))
    writeback = bool(raw & (1 << 21))
    load = bool(raw & (1 << 20))
    rn = (raw >> 16) & 0xF
    registers = tuple(index for index in range(16) if raw & (1 << index))
    if user_registers or rn == 15 or not registers:
        raise ValueError(f"unsupported block transfer at 0x{pc:08X}")
    if writeback and rn in registers:
        raise ValueError(f"block transfer base aliases register list at 0x{pc:08X}")
    if not load and 15 in registers:
        raise ValueError(f"PC block store at 0x{pc:08X} is unsupported")

    byte_count = len(registers) * 4
    if add_offset:
        first_address = f"state.r[{rn}]" + (" + 4U" if pre_index else "")
        updated = f"state.r[{rn}] + {byte_count}U"
    else:
        adjustment = byte_count if pre_index else byte_count - 4
        first_address = f"state.r[{rn}] - {adjustment}U"
        updated = f"state.r[{rn}] - {byte_count}U"

    condition = _condition(raw)
    lines = [f"if (ConditionPassed(0x{condition:X}U, state.cpsr)) {{"]
    lines.append(f"    const uint32_t firstAddress = {first_address};")
    if load:
        for offset, register in enumerate(registers):
            lines.extend(
                [
                    f"    uint32_t value{register} = 0U;",
                    f"    if (!memory.Read32(firstAddress + {offset * 4}U, &value{register})) {{",
                    f"        return MemoryFault(0x{pc:08X}U, firstAddress + {offset * 4}U, state, result, stats);",
                    "    }",
                ]
            )
        for register in registers:
            if register != 15:
                lines.append(f"    state.r[{register}] = value{register};")
    else:
        for offset, register in enumerate(registers):
            lines.extend(
                [
                    f"    if (!memory.Write32(firstAddress + {offset * 4}U, state.r[{register}])) {{",
                    f"        return MemoryFault(0x{pc:08X}U, firstAddress + {offset * 4}U, state, result, stats);",
                    "    }",
                ]
            )
    if writeback:
        lines.append(f"    state.r[{rn}] = {updated};")
    if load and 15 in registers:
        lines.append("    return BranchTo(value15, state, result, stats);")
    lines.append("}")
    return lines


def _emit_branch_exchange(pc: int, raw: int) -> list[str]:
    register = raw & 0xF
    if register == 15:
        raise ValueError(f"BX PC at 0x{pc:08X} is unsupported")
    condition = _condition(raw)
    return [
        f"if (ConditionPassed(0x{condition:X}U, state.cpsr)) {{",
        f"    return BranchTo(state.r[{register}], state, result, stats);",
        "}",
    ]


def _vfp_lane(raw: int, register_shift: int, extension_bit: int) -> int:
    return ((raw >> register_shift) & 0xF) * 2 + ((raw >> extension_bit) & 1)


def _emit_vfp(pc: int, raw: int) -> list[str]:
    condition = _condition(raw)
    lines = [f"if (ConditionPassed(0x{condition:X}U, state.cpsr)) {{"]
    if (raw & 0x0F000F00) == 0x0D000A00:
        load = bool(raw & (1 << 20))
        add_offset = bool(raw & (1 << 23))
        rn = (raw >> 16) & 0xF
        lane = _vfp_lane(raw, 12, 22)
        offset = (raw & 0xFF) * 4
        base = f"0x{(pc + 8) & ~3:08X}U" if rn == 15 else f"state.r[{rn}]"
        operator = "+" if add_offset else "-"
        lines.append(f"    const uint32_t address = {base} {operator} {offset}U;")
        if load:
            lines.extend(
                [
                    "    uint32_t value = 0U;",
                    "    if (!memory.Read32(address, &value)) {",
                    f"        return MemoryFault(0x{pc:08X}U, address, state, result, stats);",
                    "    }",
                    f"    state.vfp[{lane}] = value;",
                ]
            )
        else:
            lines.extend(
                [
                    f"    if (!memory.Write32(address, state.vfp[{lane}])) {{",
                    f"        return MemoryFault(0x{pc:08X}U, address, state, result, stats);",
                    "    }",
                ]
            )
    elif (raw & 0x0FBF0ED0) == 0x0EB00A40:
        destination = _vfp_lane(raw, 12, 22)
        source = _vfp_lane(raw, 0, 5)
        lines.append(f"    state.vfp[{destination}] = state.vfp[{source}];")
    elif (raw & 0x0FF00F7F) == 0x0E000A10:
        core = (raw >> 12) & 0xF
        if core == 15:
            raise ValueError(f"VMOV from PC at 0x{pc:08X} is unsupported")
        destination = _vfp_lane(raw, 16, 7)
        lines.append(f"    state.vfp[{destination}] = state.r[{core}];")
    elif (raw & 0x0FFF0FFF) == 0x0EF10A10:
        core = (raw >> 12) & 0xF
        if core == 15:
            lines.append(
                "    state.cpsr = (state.cpsr & ~0xF0000000U) | "
                "(state.fpscr & 0xF0000000U);"
            )
        else:
            lines.append(f"    state.r[{core}] = state.fpscr;")
    else:
        unary = raw & 0x0FBF0FD0
        destination = _vfp_lane(raw, 12, 22)
        source = _vfp_lane(raw, 0, 5)
        if unary == 0x0EB80AC0:
            lines.extend(
                [
                    "    const auto converted = a32::VfpBinary32FromSigned(",
                    f"        state.vfp[{source}], state.fpscr);",
                    f"    state.vfp[{destination}] = converted.value;",
                    "    state.fpscr |= converted.exception_flags;",
                ]
            )
        elif unary in (0x0EB40A40, 0x0EB40AC0):
            signal = "true" if unary == 0x0EB40AC0 else "false"
            lines.extend(
                [
                    "    const auto comparison = a32::VfpBinary32Compare(",
                    f"        state.vfp[{destination}], state.vfp[{source}], {signal});",
                    "    state.fpscr = (state.fpscr & ~0xF0000000U) |",
                    "                  (comparison.value & 0xF0000000U);",
                    "    state.fpscr |= comparison.exception_flags;",
                ]
            )
        else:
            raise ValueError(
                f"VFP instruction 0x{raw:08X} at 0x{pc:08X} is unsupported"
            )
    lines.append("}")
    return lines


def _emit_instruction(region: Region, index: int) -> list[str]:
    pc = region.pc + index * 4
    raw = region.words[index]
    if (raw & 0x0FFFFFFF) == 0x0320F000:
        return []
    if (raw & 0x0FFFFFF0) == 0x012FFF10:
        return _emit_branch_exchange(pc, raw)
    if (raw & 0x0E000000) == 0x0A000000 and _condition(raw) != 0xF:
        target = _branch_target(pc, raw)
        condition = _condition(raw)
        if raw & (1 << 24):
            raise ValueError(f"BL at 0x{pc:08X} requires whole-program AOT")
        if region.pc <= target < region.end_pc and target % 4 == 0:
            return [
                f"if (ConditionPassed(0x{condition:X}U, state.cpsr)) {{",
                f"    goto instruction_{target:08X};",
                "}",
            ]
        return [
            f"if (ConditionPassed(0x{condition:X}U, state.cpsr)) {{",
            f"    return BranchTo(0x{target:08X}U, state, result, stats);",
            "}",
        ]
    if (raw & 0x0C000000) == 0x04000000:
        return _emit_memory(pc, raw)
    if (raw & 0x0E000000) == 0x08000000:
        return _emit_block_transfer(pc, raw)
    if (raw & 0x0E000090) == 0x00000090 and ((raw >> 5) & 0x3) != 0:
        return _emit_halfword_memory(pc, raw)
    if (raw & 0x0C000000) == 0:
        return _emit_data_processing(pc, raw)
    if (raw & 0x0C000000) == 0x0C000000:
        return _emit_vfp(pc, raw)
    raise ValueError(f"instruction 0x{raw:08X} at 0x{pc:08X} is unsupported")


def _load_regions(code: bytes, base: int, manifest_path: Path) -> list[Region]:
    document = json.loads(manifest_path.read_text(encoding="utf-8"))
    if document.get("format") != FORMAT:
        raise ValueError("invalid true-AOT block manifest")
    regions: list[Region] = []
    for item in document.get("blocks", []):
        pc = int(item["pc"])
        end_pc = int(item["end_pc"])
        if pc % 4 or end_pc <= pc or end_pc % 4:
            raise ValueError(f"invalid true-AOT region 0x{pc:08X}..0x{end_pc:08X}")
        start = pc - base
        end = end_pc - base
        if start < 0 or end > len(code):
            raise ValueError(f"true-AOT region 0x{pc:08X} is outside code.bin")
        words = tuple(
            int.from_bytes(code[offset : offset + 4], "little")
            for offset in range(start, end, 4)
        )
        entry_points = tuple(int(value) for value in item.get("entry_points", [pc]))
        if not entry_points or len(set(entry_points)) != len(entry_points):
            raise ValueError(f"invalid entry points for region 0x{pc:08X}")
        if any(
            entry < pc or entry >= end_pc or entry % 4
            for entry in entry_points
        ):
            raise ValueError(f"entry point outside region 0x{pc:08X}")
        region = Region(
            pc, end_pc, str(item.get("symbol", "block")), entry_points, words
        )
        for index in range(len(words)):
            _emit_instruction(region, index)
        regions.append(region)
    all_entries = [entry for region in regions for entry in region.entry_points]
    if len(set(all_entries)) != len(all_entries):
        raise ValueError("duplicate true-AOT region entry")
    return sorted(regions, key=lambda region: region.pc)


def _render_header() -> str:
    return """#pragma once

#include \"recomp/a32_runtime.h\"

#include <cstdint>
#include <span>

namespace oot3d::recomp {

struct GeneratedTrueAotRunStats {
    uint64_t Iterations = 0;
    uint64_t MemoryFaults = 0;
};

std::span<const uint32_t> GetA32GeneratedTrueAotEntryPoints() noexcept;
bool ExecuteA32GeneratedTrueAotBlock(
    uint32_t pc, a32::GuestState& state, a32::MemoryBus& memory,
    a32::ExecutionResult* result, GeneratedTrueAotRunStats* stats);

}  // namespace oot3d::recomp
"""


def _render_source(regions: list[Region]) -> str:
    lines = [
        f'#include "{HEADER_NAME}"',
        '#include "recomp/a32_vfp_scalar.h"',
        "",
        "#include <array>",
        "",
        "namespace oot3d::recomp {",
        "namespace {",
        "",
        "bool ConditionPassed(uint32_t condition, uint32_t cpsr) noexcept {",
        "    const bool n = (cpsr & a32::kFlagN) != 0U;",
        "    const bool z = (cpsr & a32::kFlagZ) != 0U;",
        "    const bool c = (cpsr & a32::kFlagC) != 0U;",
        "    const bool v = (cpsr & a32::kFlagV) != 0U;",
        "    switch (condition) {",
        "    case 0x0U: return z; case 0x1U: return !z;",
        "    case 0x2U: return c; case 0x3U: return !c;",
        "    case 0x4U: return n; case 0x5U: return !n;",
        "    case 0x6U: return v; case 0x7U: return !v;",
        "    case 0x8U: return c && !z; case 0x9U: return !c || z;",
        "    case 0xAU: return n == v; case 0xBU: return n != v;",
        "    case 0xCU: return !z && n == v; case 0xDU: return z || n != v;",
        "    case 0xEU: return true; default: return false;",
        "    }",
        "}",
        "",
        "void SetFlags(a32::GuestState& state, uint32_t value, bool carry, bool overflow) noexcept {",
        "    state.cpsr &= ~(a32::kFlagN | a32::kFlagZ | a32::kFlagC | a32::kFlagV);",
        "    state.cpsr |= (value & a32::kFlagN) | (value == 0U ? a32::kFlagZ : 0U) |",
        "                   (carry ? a32::kFlagC : 0U) | (overflow ? a32::kFlagV : 0U);",
        "}",
        "",
        "void UpdateSubtractFlags(a32::GuestState& state, uint32_t left, uint32_t right, uint32_t value) noexcept {",
        "    SetFlags(state, value, left >= right,",
        "             ((left ^ right) & (left ^ value) & a32::kFlagN) != 0U);",
        "}",
        "",
        "void UpdateAddFlags(a32::GuestState& state, uint32_t left, uint32_t right, uint32_t value) noexcept {",
        "    SetFlags(state, value, value < left,",
        "             ((~(left ^ right)) & (left ^ value) & a32::kFlagN) != 0U);",
        "}",
        "",
        "void UpdateLogicalFlags(a32::GuestState& state, uint32_t value) noexcept {",
        "    state.cpsr = (state.cpsr & ~(a32::kFlagN | a32::kFlagZ)) |",
        "                  (value & a32::kFlagN) | (value == 0U ? a32::kFlagZ : 0U);",
        "}",
        "",
        "uint32_t ShiftLsl(uint32_t value, uint32_t amount) noexcept {",
        "    return amount < 32U ? value << amount : 0U;",
        "}",
        "uint32_t ShiftLsr(uint32_t value, uint32_t amount) noexcept {",
        "    amount = amount == 0U ? 32U : amount;",
        "    return amount < 32U ? value >> amount : 0U;",
        "}",
        "uint32_t ShiftAsr(uint32_t value, uint32_t amount) noexcept {",
        "    amount = amount == 0U ? 32U : amount;",
        "    if (amount >= 32U) return (value & 0x80000000U) != 0U ? 0xFFFFFFFFU : 0U;",
        "    return static_cast<uint32_t>(static_cast<int32_t>(value) >> amount);",
        "}",
        "uint32_t ShiftRor(uint32_t value, uint32_t amount) noexcept {",
        "    if (amount == 0U) return value;",
        "    return (value >> amount) | (value << (32U - amount));",
        "}",
        "",
        "bool MemoryFault(uint32_t pc, uint32_t address, a32::GuestState& state,",
        "                 a32::ExecutionResult* result, GeneratedTrueAotRunStats* stats) {",
        "    state.r[15] = pc; ++stats->MemoryFaults;",
        "    *result = {a32::ExitKind::MemoryFault, pc, a32::FallbackReason::None, address};",
        "    return true;",
        "}",
        "",
        "bool BranchTo(uint32_t pc, a32::GuestState& state, a32::ExecutionResult* result,",
        "              GeneratedTrueAotRunStats*) {",
        "    state.r[15] = pc;",
        "    *result = {a32::ExitKind::Branch, pc, a32::FallbackReason::None, 0U};",
        "    return true;",
        "}",
        "",
    ]
    for region in regions:
        name = _symbol(region.symbol, region.pc)
        lines.extend(
            [
                f"bool Execute_{name}(uint32_t entryPc, a32::GuestState& state, a32::MemoryBus& memory,",
                "                     a32::ExecutionResult* result, GeneratedTrueAotRunStats* stats) {",
                "    switch (entryPc) {",
            ]
        )
        for entry in region.entry_points:
            lines.extend(
                [
                    f"    case 0x{entry:08X}U: goto instruction_{entry:08X};",
                ]
            )
        lines.extend(["    default: return false;", "    }"])
        for index in range(len(region.words)):
            pc = region.pc + index * 4
            lines.append(f"instruction_{pc:08X}:")
            if pc == region.pc:
                lines.append("    ++stats->Iterations;")
            for line in _emit_instruction(region, index):
                lines.append("    " + line)
        lines.extend(
            [
                f"    state.r[15] = 0x{region.end_pc:08X}U;",
                f"    *result = {{a32::ExitKind::Fallthrough, 0x{region.end_pc:08X}U,",
                "               a32::FallbackReason::None, 0U};",
                "    return true;",
                "}",
                "",
            ]
        )
    entry_points = [entry for region in regions for entry in region.entry_points]
    entries = ", ".join(f"0x{entry:08X}U" for entry in entry_points)
    lines.extend(
        [
            f"constexpr std::array<uint32_t, {len(entry_points)}> kEntryPoints{{{entries}}};",
            "",
            "}  // namespace",
            "",
            "std::span<const uint32_t> GetA32GeneratedTrueAotEntryPoints() noexcept {",
            "    return kEntryPoints;",
            "}",
            "",
            "bool ExecuteA32GeneratedTrueAotBlock(",
            "    uint32_t pc, a32::GuestState& state, a32::MemoryBus& memory,",
            "    a32::ExecutionResult* result, GeneratedTrueAotRunStats* stats) {",
            "    if (result == nullptr || stats == nullptr) return false;",
            "    switch (pc) {",
        ]
    )
    for region in regions:
        name = _symbol(region.symbol, region.pc)
        for entry in region.entry_points:
            lines.append(f"    case 0x{entry:08X}U:")
        lines.append(
            f"        return Execute_{name}(pc, state, memory, result, stats);"
        )
    lines.extend(
        [
            "    default: return false;",
            "    }",
            "}",
            "",
            "}  // namespace oot3d::recomp",
            "",
        ]
    )
    return "\n".join(lines)


def generate_true_aot(
    code_path: Path, manifest_path: Path, output: Path, base: int = 0x00100000
) -> None:
    code = code_path.read_bytes()
    regions = _load_regions(code, base, manifest_path)
    header = _render_header().encode("utf-8")
    source = _render_source(regions).encode("utf-8")
    manifest = {
        "format": OUTPUT_FORMAT,
        "base": base,
        "code_sha256": _sha256(code),
        "input_manifest_sha256": _sha256(manifest_path.read_bytes()),
        "generator_sha256": _sha256(Path(__file__).read_bytes()),
        "regions": [
            {
                "pc": region.pc,
                "end_pc": region.end_pc,
                "symbol": region.symbol,
                "entry_points": list(region.entry_points),
                "instruction_count": len(region.words),
            }
            for region in regions
        ],
        "files": {
            HEADER_NAME: _sha256(header),
            SOURCE_NAME: _sha256(source),
        },
    }
    output.mkdir(parents=True, exist_ok=True)
    _write_if_different(output / HEADER_NAME, header)
    _write_if_different(output / SOURCE_NAME, source)
    _write_if_different(
        output / MANIFEST_NAME,
        (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8"),
    )
