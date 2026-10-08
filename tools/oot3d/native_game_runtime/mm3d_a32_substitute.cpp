#include "mm3d_a32_substitute.h"

#include <bit>

namespace Oot3dNativeGame {

namespace a32 = oot3d::recomp::a32;

namespace {

constexpr std::uint32_t kN = 1U << 31U;
constexpr std::uint32_t kZ = 1U << 30U;
constexpr std::uint32_t kC = 1U << 29U;
constexpr std::uint32_t kV = 1U << 28U;

std::uint32_t WithNz(std::uint32_t cpsr, std::uint32_t result) {
    cpsr &= ~(kN | kZ);
    cpsr |= result & kN;
    cpsr |= result == 0U ? kZ : 0U;
    return cpsr;
}

std::uint32_t Flag(std::uint32_t cpsr, std::uint32_t flag, bool set) {
    return set ? (cpsr | flag) : (cpsr & ~flag);
}

// a + b + carry with the A32 carry and overflow.
std::uint32_t AddWithCarry(std::uint32_t a, std::uint32_t b, bool carry, bool* carryOut,
                           bool* overflow) {
    const std::uint64_t unsignedSum = std::uint64_t{a} + b + (carry ? 1U : 0U);
    const std::uint32_t result = static_cast<std::uint32_t>(unsignedSum);
    *carryOut = (unsignedSum >> 32U) != 0U;
    *overflow = ((~(a ^ b) & (a ^ result)) & kN) != 0U;
    return result;
}

std::optional<std::uint32_t> DataProcessingImmediate(std::uint32_t word, std::uint32_t pc,
                                                     a32::GuestState& state) {
    const std::uint32_t opcode = (word >> 21U) & 15U;
    const bool setFlags = ((word >> 20U) & 1U) != 0U;
    const std::uint32_t rn = (word >> 16U) & 15U;
    const std::uint32_t rd = (word >> 12U) & 15U;
    const std::uint32_t rotation = ((word >> 8U) & 15U) * 2U;
    const std::uint32_t immediate = std::rotr(word & 0xFFU, static_cast<int>(rotation));
    const bool shifterCarry = rotation == 0U ? (state.cpsr & kC) != 0U : (immediate & kN) != 0U;
    if (rd == 15U || (rn == 15U && opcode != 13U && opcode != 15U)) {
        return std::nullopt;
    }
    const std::uint32_t operand = state.r[rn];
    std::uint32_t cpsr = state.cpsr;
    std::uint32_t result = 0;
    bool writes = true;
    bool arithmetic = false;
    bool carry = false;
    bool overflow = false;
    switch (opcode) {
    case 0: result = operand & immediate; break;                    // AND
    case 1: result = operand ^ immediate; break;                    // EOR
    case 2: result = AddWithCarry(operand, ~immediate, true, &carry, &overflow); arithmetic = true; break;  // SUB
    case 3: result = AddWithCarry(immediate, ~operand, true, &carry, &overflow); arithmetic = true; break;  // RSB
    case 4: result = AddWithCarry(operand, immediate, false, &carry, &overflow); arithmetic = true; break;  // ADD
    case 8: result = operand & immediate; writes = false; break;    // TST
    case 9: result = operand ^ immediate; writes = false; break;    // TEQ
    case 10: result = AddWithCarry(operand, ~immediate, true, &carry, &overflow); arithmetic = true; writes = false; break;  // CMP
    case 11: result = AddWithCarry(operand, immediate, false, &carry, &overflow); arithmetic = true; writes = false; break;  // CMN
    case 12: result = operand | immediate; break;                   // ORR
    case 13: result = immediate; break;                             // MOV
    case 14: result = operand & ~immediate; break;                  // BIC
    case 15: result = ~immediate; break;                            // MVN
    default: return std::nullopt;                                   // ADC, SBC, RSC
    }
    if (!writes && !setFlags) {
        return std::nullopt; // not a data-processing form
    }
    if (setFlags) {
        cpsr = WithNz(cpsr, result);
        if (arithmetic) {
            cpsr = Flag(Flag(cpsr, kC, carry), kV, overflow);
        } else {
            cpsr = Flag(cpsr, kC, shifterCarry);
        }
        state.cpsr = cpsr;
    }
    if (writes) {
        state.r[rd] = result;
    }
    return pc + 4U;
}

} // namespace

std::optional<std::uint32_t> ExecuteMm3dSubstitute(std::uint32_t word, std::uint32_t pc,
                                                   a32::GuestState& state,
                                                   a32::MemoryBus& memory) {
    if ((word >> 28U) == 15U) {
        return std::nullopt; // unconditional space
    }
    const bool passed = a32::ConditionPassed(static_cast<a32::Condition>(word >> 28U), state.cpsr);
    // BX Rm.
    if ((word & 0x0FFFFFF0U) == 0x012FFF10U) {
        return passed ? state.r[word & 15U] : pc + 4U;
    }
    // Data processing with an immediate operand.
    if (((word >> 25U) & 7U) == 1U) {
        if (!passed) {
            return pc + 4U;
        }
        return DataProcessingImmediate(word, pc, state);
    }
    // B / BL.
    if (((word >> 25U) & 7U) == 5U) {
        if (!passed) {
            return pc + 4U;
        }
        std::int32_t offset = static_cast<std::int32_t>(word & 0x00FFFFFFU);
        if ((offset & 0x00800000) != 0) {
            offset -= 0x01000000;
        }
        if (((word >> 24U) & 1U) != 0U) {
            state.r[14] = pc + 4U;
        }
        return static_cast<std::uint32_t>(static_cast<std::int64_t>(pc) + 8 + 4 * offset);
    }
    // LDMIA Rn{!}, {list} (LDMFD), the only block transfer substituted.
    if ((word & 0x0FD00000U) == 0x08900000U) {
        const std::uint32_t rn = (word >> 16U) & 15U;
        const std::uint32_t list = word & 0xFFFFU;
        if (rn == 15U || list == 0U || ((word >> 22U) & 1U) != 0U) {
            return std::nullopt;
        }
        if (!passed) {
            return pc + 4U;
        }
        std::uint32_t address = state.r[rn];
        std::uint32_t values[16] = {};
        for (std::uint32_t reg = 0; reg < 16U; ++reg) {
            if ((list >> reg) & 1U) {
                if (!memory.Read32(address, &values[reg])) {
                    return std::nullopt;
                }
                address += 4U;
            }
        }
        if (((word >> 21U) & 1U) != 0U && ((list >> rn) & 1U) == 0U) {
            state.r[rn] = address;
        }
        for (std::uint32_t reg = 0; reg < 15U; ++reg) {
            if ((list >> reg) & 1U) {
                state.r[reg] = values[reg];
            }
        }
        return ((list >> 15U) & 1U) != 0U ? values[15] : pc + 4U;
    }
    return std::nullopt;
}

} // namespace Oot3dNativeGame
