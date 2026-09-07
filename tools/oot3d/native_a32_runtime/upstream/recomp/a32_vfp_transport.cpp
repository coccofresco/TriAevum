#include "a32_vfp_transport.h"

#include <cstddef>
#include <cstdint>

namespace oot3d::recomp::a32 {
namespace {

constexpr std::uint32_t kConditionMask = 0xF0000000U;
constexpr std::uint32_t kNzcvMask = kFlagN | kFlagZ | kFlagC | kFlagV;

constexpr bool Matches(
    std::uint32_t raw,
    std::uint32_t mask,
    std::uint32_t value) noexcept {
    return (raw & mask) == value;
}

constexpr std::uint8_t Bits(
    std::uint32_t raw,
    unsigned shift,
    std::uint32_t mask) noexcept {
    return static_cast<std::uint8_t>((raw >> shift) & mask);
}

ExecutionResult Fallthrough(GuestState& state, std::uint32_t pc) noexcept {
    state.r[15] = pc + 4U;
    return {
        ExitKind::Fallthrough,
        pc + 4U,
        FallbackReason::None,
        0U,
    };
}

ExecutionResult Unsupported(
    GuestState& state,
    std::uint32_t raw,
    std::uint32_t pc) noexcept {
    state.r[15] = pc;
    return {
        ExitKind::Unsupported,
        pc,
        FallbackReason::Unsupported,
        raw,
    };
}

ExecutionResult MemoryFault(
    GuestState& state,
    std::uint32_t pc,
    std::uint32_t address) noexcept {
    state.r[15] = pc;
    return {
        ExitKind::MemoryFault,
        pc,
        FallbackReason::None,
        address,
    };
}

constexpr std::uint32_t ReadCoreRegister(
    const GuestState& state,
    std::uint8_t index,
    std::uint32_t pc) noexcept {
    return index == 15U ? pc + 8U : state.r[index];
}

struct LaneRange {
    std::uint8_t first{};
    std::uint8_t words{};
};

bool DecodeLaneRange(std::uint32_t raw, LaneRange* range) noexcept {
    const std::uint8_t words = Bits(raw, 0U, 0xFFU);
    if (words == 0U) {
        return false;
    }

    const std::uint8_t vd = Bits(raw, 12U, 0xFU);
    const bool high_bit = (raw & (1U << 22U)) != 0U;
    const bool is_double = (raw & (1U << 8U)) != 0U;
    if (is_double) {
        // GuestState models D0-D15 as the exact aliases S[0]-S[31].  The
        // Python reference deliberately rejects D16-D31 and odd word counts.
        if (high_bit || (words & 1U) != 0U) {
            return false;
        }
        const std::uint8_t double_count = words / 2U;
        if (static_cast<unsigned>(vd) + double_count > 16U) {
            return false;
        }
        range->first = static_cast<std::uint8_t>(vd * 2U);
        range->words = words;
        return true;
    }

    const std::uint8_t first = static_cast<std::uint8_t>(
        static_cast<unsigned>(vd) * 2U + (high_bit ? 1U : 0U));
    if (static_cast<unsigned>(first) + words > 32U) {
        return false;
    }
    range->first = first;
    range->words = words;
    return true;
}

ExecutionResult ExecuteSingleMemory(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state,
    MemoryBus& memory,
    bool load) {
    const std::uint8_t rn = Bits(raw, 16U, 0xFU);
    if (!load && rn == 15U) {
        return Unsupported(state, raw, pc);
    }
    const std::uint8_t vd = Bits(raw, 12U, 0xFU);
    const bool is_double = (raw & (1U << 8U)) != 0U;
    const bool high_bit = (raw & (1U << 22U)) != 0U;
    if (is_double && high_bit) {
        // GuestState intentionally stops at D15/S31.
        return Unsupported(state, raw, pc);
    }
    const std::uint32_t displacement =
        static_cast<std::uint32_t>(raw & 0xFFU) * 4U;
    const std::uint32_t base = ReadCoreRegister(state, rn, pc);
    const std::uint32_t address = (raw & (1U << 23U)) != 0U
                                      ? base + displacement
                                      : base - displacement;

    if (is_double) {
        const std::size_t first_lane = static_cast<std::size_t>(vd) * 2U;
        std::uint32_t fault_address = address;
        if (load) {
            std::uint64_t value = 0U;
            if (!memory.Read64(address, &value, &fault_address)) {
                return MemoryFault(state, pc, fault_address);
            }
            state.vfp[first_lane] = static_cast<std::uint32_t>(value);
            state.vfp[first_lane + 1U] =
                static_cast<std::uint32_t>(value >> 32U);
        } else {
            const std::uint64_t value =
                static_cast<std::uint64_t>(state.vfp[first_lane]) |
                (static_cast<std::uint64_t>(state.vfp[first_lane + 1U]) << 32U);
            if (!memory.Write64(address, value, &fault_address)) {
                return MemoryFault(state, pc, fault_address);
            }
        }
        return Fallthrough(state, pc);
    }

    const std::uint8_t lane = static_cast<std::uint8_t>(
        static_cast<unsigned>(vd) * 2U + (high_bit ? 1U : 0U));
    if (load) {
        std::uint32_t value = 0U;
        if (!memory.Read32(address, &value)) {
            return MemoryFault(state, pc, address);
        }
        state.vfp[lane] = value;
    } else if (!memory.Write32(address, state.vfp[lane])) {
        return MemoryFault(state, pc, address);
    }
    return Fallthrough(state, pc);
}

ExecutionResult ExecuteMultipleMemory(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state,
    MemoryBus& memory,
    bool load) {
    // arm_vfp_transport_supported accepts only increment-after VLD/STM forms.
    if ((raw & (1U << 24U)) != 0U || (raw & (1U << 23U)) == 0U) {
        return Unsupported(state, raw, pc);
    }
    const std::uint8_t rn = Bits(raw, 16U, 0xFU);
    if (rn == 15U) {
        return Unsupported(state, raw, pc);
    }
    LaneRange lanes{};
    if (!DecodeLaneRange(raw, &lanes)) {
        return Unsupported(state, raw, pc);
    }

    const std::uint32_t base = state.r[rn];
    std::uint32_t address = base;
    for (std::uint8_t offset = 0U; offset < lanes.words; ++offset) {
        const std::size_t lane = static_cast<std::size_t>(lanes.first) + offset;
        if (load) {
            std::uint32_t value = 0U;
            if (!memory.Read32(address, &value)) {
                return MemoryFault(state, pc, address);
            }
            state.vfp[lane] = value;
        } else if (!memory.Write32(address, state.vfp[lane])) {
            return MemoryFault(state, pc, address);
        }
        address += 4U;
    }
    if ((raw & (1U << 21U)) != 0U) {
        state.r[rn] = base + static_cast<std::uint32_t>(lanes.words) * 4U;
    }
    return Fallthrough(state, pc);
}

ExecutionResult ExecutePushPop(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state,
    MemoryBus& memory,
    bool pop) {
    LaneRange lanes{};
    if (!DecodeLaneRange(raw, &lanes)) {
        return Unsupported(state, raw, pc);
    }

    const std::uint32_t byte_count =
        static_cast<std::uint32_t>(lanes.words) * 4U;
    std::uint32_t address = pop ? state.r[13] : state.r[13] - byte_count;
    if (!pop) {
        // Match the reference executor and architectural writeback ordering.
        state.r[13] = address;
    }
    for (std::uint8_t offset = 0U; offset < lanes.words; ++offset) {
        const std::size_t lane = static_cast<std::size_t>(lanes.first) + offset;
        if (pop) {
            std::uint32_t value = 0U;
            if (!memory.Read32(address, &value)) {
                return MemoryFault(state, pc, address);
            }
            state.vfp[lane] = value;
        } else if (!memory.Write32(address, state.vfp[lane])) {
            return MemoryFault(state, pc, address);
        }
        address += 4U;
    }
    if (pop) {
        state.r[13] += byte_count;
    }
    return Fallthrough(state, pc);
}

}  // namespace

ExecutionResult ExecuteVfpTransport(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state,
    MemoryBus& memory) {
    // cond=0b1111 belongs to the unconditional/ASIMD encoding space and is
    // explicitly outside arm_vfp_transport_supported.
    if ((raw & kConditionMask) == kConditionMask) {
        return Unsupported(state, raw, pc);
    }

    // VMOV.F32 Sd, Sm and VMOV.F64 Dd, Dm (raw bit copies; no FPSCR effects).
    if (Matches(raw, 0x0FBF0ED0U, 0x0EB00A40U)) {
        if ((raw & (1U << 8U)) != 0U) {
            if ((raw & ((1U << 22U) | (1U << 5U))) != 0U) {
                return Unsupported(state, raw, pc);
            }
            const std::size_t destination =
                static_cast<std::size_t>(Bits(raw, 12U, 0xFU)) * 2U;
            const std::size_t source =
                static_cast<std::size_t>(Bits(raw, 0U, 0xFU)) * 2U;
            const std::uint32_t low = state.vfp[source];
            const std::uint32_t high = state.vfp[source + 1U];
            state.vfp[destination] = low;
            state.vfp[destination + 1U] = high;
            return Fallthrough(state, pc);
        }
        const std::uint8_t destination = static_cast<std::uint8_t>(
            Bits(raw, 12U, 0xFU) * 2U + Bits(raw, 22U, 1U));
        const std::uint8_t source = static_cast<std::uint8_t>(
            Bits(raw, 0U, 0xFU) * 2U + Bits(raw, 5U, 1U));
        state.vfp[destination] = state.vfp[source];
        return Fallthrough(state, pc);
    }

    // VMOV Dm, Rt, Rt2 and VMOV Rt, Rt2, Dm.  Both are exact 64-bit
    // transports between a D register and an ordered core-register pair.
    if (Matches(raw, 0x0FF00FD0U, 0x0C400B10U) ||
        Matches(raw, 0x0FF00FD0U, 0x0C500B10U)) {
        const bool to_core = (raw & (1U << 20U)) != 0U;
        const std::uint8_t rt = Bits(raw, 12U, 0xFU);
        const std::uint8_t rt2 = Bits(raw, 16U, 0xFU);
        const std::uint8_t dm = static_cast<std::uint8_t>(
            Bits(raw, 0U, 0xFU) + Bits(raw, 5U, 1U) * 16U);
        if (rt == 15U || rt2 == 15U || dm >= 16U) {
            return Unsupported(state, raw, pc);
        }
        const std::size_t first_lane = static_cast<std::size_t>(dm) * 2U;
        if (to_core) {
            state.r[rt] = state.vfp[first_lane];
            state.r[rt2] = state.vfp[first_lane + 1U];
        } else {
            state.vfp[first_lane] = state.r[rt];
            state.vfp[first_lane + 1U] = state.r[rt2];
        }
        return Fallthrough(state, pc);
    }

    // VMOV Sn, Rt.
    if (Matches(raw, 0x0FF00F7FU, 0x0E000A10U)) {
        const std::uint8_t rt = Bits(raw, 12U, 0xFU);
        if (rt == 15U) {
            return Unsupported(state, raw, pc);
        }
        const std::uint8_t lane = static_cast<std::uint8_t>(
            Bits(raw, 16U, 0xFU) * 2U + Bits(raw, 7U, 1U));
        state.vfp[lane] = state.r[rt];
        return Fallthrough(state, pc);
    }

    // VMOV Rt, Sn.
    if (Matches(raw, 0x0FF00F7FU, 0x0E100A10U)) {
        const std::uint8_t rt = Bits(raw, 12U, 0xFU);
        if (rt == 15U) {
            return Unsupported(state, raw, pc);
        }
        const std::uint8_t lane = static_cast<std::uint8_t>(
            Bits(raw, 16U, 0xFU) * 2U + Bits(raw, 7U, 1U));
        state.r[rt] = state.vfp[lane];
        return Fallthrough(state, pc);
    }

    // VMSR FPSCR, Rt.
    if (Matches(raw, 0x0FFF0FFFU, 0x0EE10A10U)) {
        const std::uint8_t rt = Bits(raw, 12U, 0xFU);
        if (rt == 15U) {
            return Unsupported(state, raw, pc);
        }
        state.fpscr = state.r[rt];
        return Fallthrough(state, pc);
    }

    // VMRS Rt, FPSCR.  Rt=15 is FMSTAT: it copies only NZCV to APSR and does
    // not write PC, so no transport in this exact subset can branch.
    if (Matches(raw, 0x0FFF0FFFU, 0x0EF10A10U)) {
        const std::uint8_t rt = Bits(raw, 12U, 0xFU);
        if (rt == 15U) {
            state.cpsr = (state.cpsr & ~kNzcvMask) | (state.fpscr & kNzcvMask);
        } else {
            state.r[rt] = state.fpscr;
        }
        return Fallthrough(state, pc);
    }

    if (Matches(raw, 0x0FBF0E00U, 0x0D2D0A00U)) {
        return ExecutePushPop(raw, pc, state, memory, false);
    }
    if (Matches(raw, 0x0FBF0E00U, 0x0CBD0A00U)) {
        return ExecutePushPop(raw, pc, state, memory, true);
    }
    if (Matches(raw, 0x0F300E00U, 0x0D100A00U)) {
        return ExecuteSingleMemory(raw, pc, state, memory, true);
    }
    if (Matches(raw, 0x0F300E00U, 0x0D000A00U)) {
        return ExecuteSingleMemory(raw, pc, state, memory, false);
    }
    if (Matches(raw, 0x0E100E00U, 0x0C100A00U)) {
        return ExecuteMultipleMemory(raw, pc, state, memory, true);
    }
    if (Matches(raw, 0x0E100E00U, 0x0C000A00U)) {
        return ExecuteMultipleMemory(raw, pc, state, memory, false);
    }

    return Unsupported(state, raw, pc);
}

}  // namespace oot3d::recomp::a32
