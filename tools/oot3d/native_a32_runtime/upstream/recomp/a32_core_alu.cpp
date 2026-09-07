#include "a32_core_internal.h"

#include <cstdint>
#include <limits>

namespace oot3d::recomp::a32::internal {
namespace {

constexpr std::uint32_t kFlagQ = 1U << 27U;

constexpr bool IsConditionalEncoding(std::uint32_t raw) noexcept {
    return (raw >> 28U) != 0xFU;
}

constexpr std::uint32_t RotateRight(
    std::uint32_t value,
    unsigned amount) noexcept {
    amount &= 31U;
    return amount == 0U
               ? value
               : (value >> amount) | (value << (32U - amount));
}

std::uint32_t ReadRegister(
    const GuestState& state,
    unsigned index,
    std::uint32_t pc) noexcept {
    return index == 15U ? pc + 8U : state.r[index];
}

std::int64_t Signed32(std::uint32_t value) noexcept {
    return (value & 0x80000000U) == 0U
               ? static_cast<std::int64_t>(value)
               : static_cast<std::int64_t>(value) - (INT64_C(1) << 32U);
}

std::int32_t SignedHalf(std::uint32_t value, bool top) noexcept {
    const std::uint32_t half = (value >> (top ? 16U : 0U)) & 0xFFFFU;
    return static_cast<std::int32_t>(
        half < 0x8000U ? half : half - 0x10000U);
}

struct ShiftResult {
    std::uint32_t value{};
    bool carry{};
    bool carry_valid{};
};

ShiftResult ShiftImmediate(
    std::uint32_t value,
    unsigned type,
    unsigned amount,
    bool carry_in) noexcept {
    switch (type & 3U) {
    case 0U:  // LSL
        if (amount == 0U) {
            return {value, carry_in, false};
        }
        if (amount < 32U) {
            return {
                value << amount,
                ((value >> (32U - amount)) & 1U) != 0U,
                true,
            };
        }
        return {0U, amount == 32U && (value & 1U) != 0U, true};
    case 1U:  // LSR; immediate zero denotes 32.
        amount = amount == 0U ? 32U : amount;
        if (amount < 32U) {
            return {
                value >> amount,
                ((value >> (amount - 1U)) & 1U) != 0U,
                true,
            };
        }
        return {
            0U,
            amount == 32U && (value & 0x80000000U) != 0U,
            true,
        };
    case 2U:  // ASR; immediate zero denotes 32.
        amount = amount == 0U ? 32U : amount;
        if (amount >= 32U) {
            const bool sign = (value & 0x80000000U) != 0U;
            return {sign ? 0xFFFFFFFFU : 0U, sign, true};
        }
        return {
            (value >> amount) |
                ((value & 0x80000000U) != 0U
                     ? 0xFFFFFFFFU << (32U - amount)
                     : 0U),
            ((value >> (amount - 1U)) & 1U) != 0U,
            true,
        };
    default:  // ROR; immediate zero denotes RRX.
        if (amount == 0U) {
            return {
                (carry_in ? 0x80000000U : 0U) | (value >> 1U),
                (value & 1U) != 0U,
                true,
            };
        }
        amount &= 31U;
        return {
            RotateRight(value, amount),
            ((value >> (amount - 1U)) & 1U) != 0U,
            true,
        };
    }
}

ShiftResult ShiftRegister(
    std::uint32_t value,
    unsigned type,
    unsigned amount,
    bool carry_in) noexcept {
    amount &= 0xFFU;
    if (amount == 0U) {
        return {value, carry_in, false};
    }
    switch (type & 3U) {
    case 0U:  // LSL
        if (amount < 32U) {
            return {
                value << amount,
                ((value >> (32U - amount)) & 1U) != 0U,
                true,
            };
        }
        return {0U, amount == 32U && (value & 1U) != 0U, true};
    case 1U:  // LSR
        if (amount < 32U) {
            return {
                value >> amount,
                ((value >> (amount - 1U)) & 1U) != 0U,
                true,
            };
        }
        return {
            0U,
            amount == 32U && (value & 0x80000000U) != 0U,
            true,
        };
    case 2U:  // ASR
        if (amount >= 32U) {
            const bool sign = (value & 0x80000000U) != 0U;
            return {sign ? 0xFFFFFFFFU : 0U, sign, true};
        }
        return {
            (value >> amount) |
                ((value & 0x80000000U) != 0U
                     ? 0xFFFFFFFFU << (32U - amount)
                     : 0U),
            ((value >> (amount - 1U)) & 1U) != 0U,
            true,
        };
    default: {  // ROR
        const unsigned rotate = amount & 31U;
        if (rotate == 0U) {
            return {value, (value & 0x80000000U) != 0U, true};
        }
        return {
            RotateRight(value, rotate),
            ((value >> (rotate - 1U)) & 1U) != 0U,
            true,
        };
    }
    }
}

ShiftResult DecodeOperand2(
    std::uint32_t raw,
    std::uint32_t pc,
    const GuestState& state) noexcept {
    const bool carry_in = (state.cpsr & kFlagC) != 0U;
    if ((raw & (1U << 25U)) != 0U) {
        const unsigned rotate = ((raw >> 8U) & 0xFU) * 2U;
        const std::uint32_t value = RotateRight(raw & 0xFFU, rotate);
        return {
            value,
            rotate == 0U ? carry_in : (value & 0x80000000U) != 0U,
            rotate != 0U,
        };
    }

    const unsigned rm = raw & 0xFU;
    const std::uint32_t value = ReadRegister(state, rm, pc);
    const unsigned type = (raw >> 5U) & 3U;
    if ((raw & (1U << 4U)) == 0U) {
        return ShiftImmediate(
            value, type, (raw >> 7U) & 0x1FU, carry_in);
    }
    const unsigned rs = (raw >> 8U) & 0xFU;
    return ShiftRegister(
        value, type, ReadRegister(state, rs, pc), carry_in);
}

void UpdateNz(GuestState& state, std::uint32_t value) noexcept {
    state.cpsr &= ~(kFlagN | kFlagZ);
    if ((value & 0x80000000U) != 0U) {
        state.cpsr |= kFlagN;
    }
    if (value == 0U) {
        state.cpsr |= kFlagZ;
    }
}

void UpdateNz64(GuestState& state, std::uint64_t value) noexcept {
    state.cpsr &= ~(kFlagN | kFlagZ);
    if ((value & (UINT64_C(1) << 63U)) != 0U) {
        state.cpsr |= kFlagN;
    }
    if (value == 0U) {
        state.cpsr |= kFlagZ;
    }
}

void SetCarry(GuestState& state, bool carry) noexcept {
    state.cpsr = carry ? state.cpsr | kFlagC : state.cpsr & ~kFlagC;
}

void SetOverflow(GuestState& state, bool overflow) noexcept {
    state.cpsr = overflow ? state.cpsr | kFlagV : state.cpsr & ~kFlagV;
}

struct AddResult {
    std::uint32_t value{};
    bool carry{};
    bool overflow{};
};

AddResult AddWithCarry(
    std::uint32_t left,
    std::uint32_t right,
    bool carry_in) noexcept {
    const std::uint64_t wide = static_cast<std::uint64_t>(left) +
                               static_cast<std::uint64_t>(right) +
                               static_cast<unsigned>(carry_in);
    const std::uint32_t result = static_cast<std::uint32_t>(wide);
    return {
        result,
        (wide >> 32U) != 0U,
        ((~(left ^ right) & (left ^ result)) & 0x80000000U) != 0U,
    };
}

ExecutionResult Unsupported(std::uint32_t raw, std::uint32_t pc) noexcept {
    return {ExitKind::Unsupported, pc, FallbackReason::Core, raw};
}

ExecutionResult Fallthrough(std::uint32_t pc, GuestState& state) noexcept {
    const std::uint32_t next_pc = pc + 4U;
    state.r[15] = next_pc;
    return {
        ExitKind::Fallthrough,
        next_pc,
        FallbackReason::None,
        0U,
    };
}

ExecutionResult WriteDestination(
    unsigned destination,
    std::uint32_t value,
    std::uint32_t pc,
    GuestState& state) noexcept {
    state.r[destination] = value;
    if (destination == 15U) {
        return {
            ExitKind::Branch,
            value,
            FallbackReason::None,
            0U,
        };
    }
    return Fallthrough(pc, state);
}

std::uint32_t SaturateSigned32(
    std::int64_t value,
    bool* saturated) noexcept {
    constexpr std::int64_t kLow =
        static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min());
    constexpr std::int64_t kHigh =
        static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max());
    if (value < kLow) {
        *saturated = true;
        return 0x80000000U;
    }
    if (value > kHigh) {
        *saturated = true;
        return 0x7FFFFFFFU;
    }
    return static_cast<std::uint32_t>(value);
}

bool ExecuteHint(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state,
    ExecutionResult* result) noexcept {
    if (raw == 0xF57FF01FU) {  // CLREX
        state.exclusive_address = 0U;
        state.exclusive_token = 0U;
        state.exclusive_size = 0U;
        state.exclusive_valid = false;
        *result = Fallthrough(pc, state);
        return true;
    }

    // DSB, DMB and ISB.  The low option nibble is architecturally ignored by
    // this single-threaded AOT executor, but remains part of the raw opcode.
    const std::uint32_t barrier = raw & 0xFFFFFFF0U;
    if (barrier == 0xF57FF040U || barrier == 0xF57FF050U ||
        barrier == 0xF57FF060U) {
        *result = Fallthrough(pc, state);
        return true;
    }

    // A32 reserved hints 0x00-0xEF are decoded by the pinned Capstone oracle
    // as HINT (including NOP/YIELD/WFE/WFI/SEV aliases).  0xF0-0xFF are DBG
    // and deliberately stay outside arm_core_supported.
    if (IsConditionalEncoding(raw) &&
        (raw & 0x0FFFFF00U) == 0x0320F000U &&
        (raw & 0xFFU) < 0xF0U) {
        const unsigned hint = raw & 0xFFU;
        if (hint == 2U || hint == 3U) {
            const std::uint32_t next_pc = pc + 4U;
            state.r[15] = next_pc;
            *result = {
                ExitKind::Wait,
                next_pc,
                FallbackReason::None,
                raw,
            };
        } else {
            *result = Fallthrough(pc, state);
        }
        return true;
    }
    return false;
}

bool ExecuteMultiply(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state,
    ExecutionResult* execution) noexcept {
    if (!IsConditionalEncoding(raw)) {
        return false;
    }

    const unsigned rd = (raw >> 16U) & 0xFU;
    const unsigned ra_or_lo = (raw >> 12U) & 0xFU;
    const unsigned rs = (raw >> 8U) & 0xFU;
    const unsigned rm = raw & 0xFU;

    // MUL/MLA use the permissive fixed-field masks of the pinned decoder: the
    // architecturally unused accumulator field in MUL is ignored by Capstone.
    if ((raw & 0x0FE000F0U) == 0x00000090U) {  // MUL{S}
        const std::uint32_t value = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(ReadRegister(state, rm, pc)) *
            ReadRegister(state, rs, pc));
        if ((raw & (1U << 20U)) != 0U) {
            UpdateNz(state, value);
        }
        *execution = WriteDestination(rd, value, pc, state);
        return true;
    }
    if ((raw & 0x0FE000F0U) == 0x00200090U) {  // MLA{S}
        const std::uint32_t value = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(ReadRegister(state, rm, pc)) *
                ReadRegister(state, rs, pc) +
            ReadRegister(state, ra_or_lo, pc));
        if ((raw & (1U << 20U)) != 0U) {
            UpdateNz(state, value);
        }
        *execution = WriteDestination(rd, value, pc, state);
        return true;
    }
    if ((raw & 0x0FF000F0U) == 0x00600090U) {  // MLS
        const std::uint32_t product = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(ReadRegister(state, rm, pc)) *
            ReadRegister(state, rs, pc));
        const std::uint32_t value =
            ReadRegister(state, ra_or_lo, pc) - product;
        *execution = WriteDestination(rd, value, pc, state);
        return true;
    }

    const std::uint32_t long_form = raw & 0x0FE000F0U;
    if (long_form == 0x00800090U ||  // UMULL{S}
        long_form == 0x00A00090U ||  // UMLAL{S}
        long_form == 0x00C00090U ||  // SMULL{S}
        long_form == 0x00E00090U) {  // SMLAL{S}
        const bool signed_product = (raw & (1U << 22U)) != 0U;
        const bool accumulate = (raw & (1U << 21U)) != 0U;
        std::uint64_t value = 0U;
        if (signed_product) {
            const std::int64_t product = Signed32(ReadRegister(state, rm, pc)) *
                                         Signed32(ReadRegister(state, rs, pc));
            value = static_cast<std::uint64_t>(product);
        } else {
            value = static_cast<std::uint64_t>(ReadRegister(state, rm, pc)) *
                    ReadRegister(state, rs, pc);
        }
        if (accumulate) {
            value += (static_cast<std::uint64_t>(state.r[rd]) << 32U) |
                     state.r[ra_or_lo];
        }
        state.r[ra_or_lo] = static_cast<std::uint32_t>(value);
        state.r[rd] = static_cast<std::uint32_t>(value >> 32U);
        if ((raw & (1U << 20U)) != 0U) {
            UpdateNz64(state, value);
        }
        if (ra_or_lo == 15U || rd == 15U) {
            *execution = {
                ExitKind::Branch,
                state.r[15],
                FallbackReason::None,
                0U,
            };
        } else {
            *execution = Fallthrough(pc, state);
        }
        return true;
    }

    if ((raw & 0x0FF000F0U) == 0x00400090U) {  // UMAAL
        std::uint64_t value =
            static_cast<std::uint64_t>(ReadRegister(state, rm, pc)) *
                ReadRegister(state, rs, pc) +
            state.r[ra_or_lo] + state.r[rd];
        state.r[ra_or_lo] = static_cast<std::uint32_t>(value);
        state.r[rd] = static_cast<std::uint32_t>(value >> 32U);
        if (ra_or_lo == 15U || rd == 15U) {
            *execution = {
                ExitKind::Branch,
                state.r[15],
                FallbackReason::None,
                0U,
            };
        } else {
            *execution = Fallthrough(pc, state);
        }
        return true;
    }

    // Signed halfword forms.  The mnemonic's first suffix follows Rm (bit 5)
    // and its second suffix follows Rs (bit 6).
    const bool x = (raw & (1U << 5U)) != 0U;
    const bool y = (raw & (1U << 6U)) != 0U;
    if ((raw & 0x0FF00090U) == 0x01600080U) {  // SMULxy
        const std::int64_t value =
            static_cast<std::int64_t>(SignedHalf(ReadRegister(state, rm, pc), x)) *
            SignedHalf(ReadRegister(state, rs, pc), y);
        *execution = WriteDestination(
            rd, static_cast<std::uint32_t>(value), pc, state);
        return true;
    }
    if ((raw & 0x0FF00090U) == 0x01000080U) {  // SMLAxy
        const std::int64_t value =
            static_cast<std::int64_t>(SignedHalf(ReadRegister(state, rm, pc), x)) *
                SignedHalf(ReadRegister(state, rs, pc), y) +
            Signed32(ReadRegister(state, ra_or_lo, pc));
        if (value < std::numeric_limits<std::int32_t>::min() ||
            value > std::numeric_limits<std::int32_t>::max()) {
            state.cpsr |= kFlagQ;
        }
        *execution = WriteDestination(
            rd, static_cast<std::uint32_t>(value), pc, state);
        return true;
    }
    if ((raw & 0x0FF00090U) == 0x01400080U) {  // SMLALxy
        const std::int64_t product =
            static_cast<std::int64_t>(SignedHalf(ReadRegister(state, rm, pc), x)) *
            SignedHalf(ReadRegister(state, rs, pc), y);
        std::uint64_t value =
            (static_cast<std::uint64_t>(state.r[rd]) << 32U) |
            state.r[ra_or_lo];
        value += static_cast<std::uint64_t>(product);
        state.r[ra_or_lo] = static_cast<std::uint32_t>(value);
        state.r[rd] = static_cast<std::uint32_t>(value >> 32U);
        if (ra_or_lo == 15U || rd == 15U) {
            *execution = {
                ExitKind::Branch,
                state.r[15],
                FallbackReason::None,
                0U,
            };
        } else {
            *execution = Fallthrough(pc, state);
        }
        return true;
    }

    // Dual signed halfword products accumulated into a 64-bit pair.
    const std::uint32_t dual = raw & 0x0FF000D0U;
    if (dual == 0x07400010U || dual == 0x07400050U) {
        const std::uint32_t left = ReadRegister(state, rm, pc);
        const std::uint32_t right = ReadRegister(state, rs, pc);
        const bool exchange = (raw & (1U << 5U)) != 0U;
        const std::int64_t low_product =
            static_cast<std::int64_t>(SignedHalf(left, false)) *
            SignedHalf(right, exchange);
        const std::int64_t high_product =
            static_cast<std::int64_t>(SignedHalf(left, true)) *
            SignedHalf(right, !exchange);
        const std::int64_t products = dual == 0x07400050U
                                          ? low_product - high_product
                                          : low_product + high_product;
        std::uint64_t value =
            (static_cast<std::uint64_t>(state.r[rd]) << 32U) |
            state.r[ra_or_lo];
        value += static_cast<std::uint64_t>(products);
        state.r[ra_or_lo] = static_cast<std::uint32_t>(value);
        state.r[rd] = static_cast<std::uint32_t>(value >> 32U);
        if (ra_or_lo == 15U || rd == 15U) {
            *execution = {
                ExitKind::Branch,
                state.r[15],
                FallbackReason::None,
                0U,
            };
        } else {
            *execution = Fallthrough(pc, state);
        }
        return true;
    }

    return false;
}

bool ExecuteMedia(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state,
    ExecutionResult* execution) noexcept {
    if (!IsConditionalEncoding(raw)) {
        return false;
    }

    const unsigned rd = (raw >> 12U) & 0xFU;
    const unsigned rn = (raw >> 16U) & 0xFU;
    const unsigned rm = raw & 0xFU;

    if ((raw & 0x0FFF0FF0U) == 0x016F0F10U) {  // CLZ
        std::uint32_t value = ReadRegister(state, rm, pc);
        std::uint32_t count = 0U;
        while (count < 32U && (value & 0x80000000U) == 0U) {
            ++count;
            value <<= 1U;
        }
        *execution = WriteDestination(rd, count, pc, state);
        return true;
    }

    const std::uint32_t reversal = raw & 0x0FFF0FF0U;
    if (reversal == 0x06BF0F30U) {  // REV
        const std::uint32_t value = ReadRegister(state, rm, pc);
        const std::uint32_t result =
            ((value & 0x000000FFU) << 24U) |
            ((value & 0x0000FF00U) << 8U) |
            ((value & 0x00FF0000U) >> 8U) |
            ((value & 0xFF000000U) >> 24U);
        *execution = WriteDestination(rd, result, pc, state);
        return true;
    }
    if (reversal == 0x06BF0FB0U) {  // REV16
        const std::uint32_t value = ReadRegister(state, rm, pc);
        const std::uint32_t result =
            ((value & 0x00FF00FFU) << 8U) |
            ((value & 0xFF00FF00U) >> 8U);
        *execution = WriteDestination(rd, result, pc, state);
        return true;
    }
    if (reversal == 0x06FF0FB0U) {  // REVSH
        const std::uint32_t value = ReadRegister(state, rm, pc);
        const std::uint32_t half =
            ((value & 0xFFU) << 8U) | ((value >> 8U) & 0xFFU);
        const std::uint32_t result =
            (half & 0x8000U) != 0U ? half | 0xFFFF0000U : half;
        *execution = WriteDestination(rd, result, pc, state);
        return true;
    }

    // Sign/zero extension and add variants.  Bits 11:10 encode ROR by
    // 0/8/16/24; bits 9:8 are ignored by the pinned decoder.
    const std::uint32_t extension = raw & 0x0FF000F0U;
    const bool signed_extension =
        extension == 0x06800070U || extension == 0x06A00070U ||
        extension == 0x06B00070U;
    const bool unsigned_extension =
        extension == 0x06C00070U || extension == 0x06E00070U ||
        extension == 0x06F00070U;
    if (signed_extension || unsigned_extension) {
        const bool byte16 =
            extension == 0x06800070U || extension == 0x06C00070U;
        if (byte16 && rn != 15U) {
            // SXTAB16/UXTAB16 are intentionally outside _CORE_MEDIA_OPS.
            return false;
        }
        const bool byte = byte16 || extension == 0x06A00070U ||
                          extension == 0x06E00070U;
        const unsigned rotate = ((raw >> 10U) & 3U) * 8U;
        const std::uint32_t source =
            RotateRight(ReadRegister(state, rm, pc), rotate);
        std::uint32_t value = 0U;
        if (byte16) {
            std::uint32_t low = source & 0xFFU;
            std::uint32_t high = (source >> 16U) & 0xFFU;
            if (signed_extension) {
                low = (low & 0x80U) != 0U ? low | 0xFFFFFF00U : low;
                high = (high & 0x80U) != 0U ? high | 0xFFFFFF00U : high;
            }
            value = (low & 0xFFFFU) | ((high & 0xFFFFU) << 16U);
        } else {
            const unsigned bits = byte ? 8U : 16U;
            const std::uint32_t mask = byte ? 0xFFU : 0xFFFFU;
            value = source & mask;
            if (signed_extension &&
                (value & (1U << (bits - 1U))) != 0U) {
                value |= ~mask;
            }
            if (rn != 15U) {
                value += ReadRegister(state, rn, pc);
            }
        }
        *execution = WriteDestination(rd, value, pc, state);
        return true;
    }

    const std::uint32_t packing = raw & 0x0FF00070U;
    if (packing == 0x06800010U || packing == 0x06800050U) {
        const std::uint32_t left = ReadRegister(state, rn, pc);
        const unsigned amount = (raw >> 7U) & 0x1FU;
        const ShiftResult shifted = ShiftImmediate(
            ReadRegister(state, rm, pc),
            packing == 0x06800010U ? 0U : 2U,
            amount,
            (state.cpsr & kFlagC) != 0U);
        const std::uint32_t value = packing == 0x06800010U
                                        ? (left & 0xFFFFU) |
                                              (shifted.value & 0xFFFF0000U)
                                        : (left & 0xFFFF0000U) |
                                              (shifted.value & 0xFFFFU);
        *execution = WriteDestination(rd, value, pc, state);
        return true;
    }

    const std::uint32_t saturation = raw & 0x0FE00030U;
    if (saturation == 0x06A00010U || saturation == 0x06E00010U) {
        const bool is_signed = saturation == 0x06A00010U;
        const unsigned shift_type =
            (raw & (1U << 6U)) != 0U ? 2U : 0U;
        const ShiftResult shifted = ShiftImmediate(
            ReadRegister(state, rm, pc),
            shift_type,
            (raw >> 7U) & 0x1FU,
            (state.cpsr & kFlagC) != 0U);
        const std::int64_t source = Signed32(shifted.value);
        const unsigned bits =
            ((raw >> 16U) & 0x1FU) + (is_signed ? 1U : 0U);
        std::int64_t low = 0;
        std::int64_t high = 0;
        if (is_signed) {
            if (bits == 32U) {
                low = std::numeric_limits<std::int32_t>::min();
                high = std::numeric_limits<std::int32_t>::max();
            } else {
                low = -(INT64_C(1) << (bits - 1U));
                high = (INT64_C(1) << (bits - 1U)) - 1;
            }
        } else {
            high = bits == 0U ? 0 : (INT64_C(1) << bits) - 1;
        }
        std::int64_t clamped = source;
        if (clamped < low) {
            clamped = low;
        }
        if (clamped > high) {
            clamped = high;
        }
        if (clamped != source) {
            state.cpsr |= kFlagQ;
        }
        *execution = WriteDestination(
            rd, static_cast<std::uint32_t>(clamped), pc, state);
        return true;
    }

    const std::uint32_t qform = raw & 0x0FF000F0U;
    if (qform == 0x01000050U || qform == 0x01200050U ||
        qform == 0x01400050U || qform == 0x01600050U) {
        const std::int64_t left = Signed32(ReadRegister(state, rm, pc));
        std::int64_t right = Signed32(ReadRegister(state, rn, pc));
        bool saturated = false;
        if (qform == 0x01400050U || qform == 0x01600050U) {
            right = Signed32(SaturateSigned32(right * 2, &saturated));
        }
        const bool subtract =
            qform == 0x01200050U || qform == 0x01600050U;
        const std::uint32_t value = SaturateSigned32(
            subtract ? left - right : left + right, &saturated);
        if (saturated) {
            state.cpsr |= kFlagQ;
        }
        *execution = WriteDestination(rd, value, pc, state);
        return true;
    }

    if ((raw & 0x0FF000F0U) == 0x066000F0U) {  // UQSUB8
        const std::uint32_t left = ReadRegister(state, rn, pc);
        const std::uint32_t right = ReadRegister(state, rm, pc);
        std::uint32_t value = 0U;
        for (unsigned lane = 0U; lane < 4U; ++lane) {
            const unsigned shift = lane * 8U;
            const unsigned a = (left >> shift) & 0xFFU;
            const unsigned b = (right >> shift) & 0xFFU;
            value |= (a > b ? a - b : 0U) << shift;
        }
        *execution = WriteDestination(rd, value, pc, state);
        return true;
    }

    return false;
}

bool ExecuteDataProcessing(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state,
    ExecutionResult* execution) noexcept {
    if (!IsConditionalEncoding(raw)) {
        return false;
    }

    if ((raw & 0x0FF00000U) == 0x03000000U ||
        (raw & 0x0FF00000U) == 0x03400000U) {
        const bool top = (raw & (1U << 22U)) != 0U;
        const unsigned rd = (raw >> 12U) & 0xFU;
        const std::uint32_t immediate =
            ((raw >> 4U) & 0xF000U) | (raw & 0xFFFU);
        const std::uint32_t value = top
                                        ? (state.r[rd] & 0xFFFFU) |
                                              (immediate << 16U)
                                        : immediate;
        *execution = WriteDestination(rd, value, pc, state);
        return true;
    }

    // Classic data processing accepts an immediate, an immediate register
    // shift, or a register-specified shift (whose bit 7 must be zero).  The
    // fourth 000...1xx1 class is multiply/extra-load-store and is disjoint.
    const bool immediate = (raw & 0x0E000000U) == 0x02000000U;
    const bool immediate_shift =
        (raw & 0x0E000010U) == 0x00000000U;
    const bool register_shift =
        (raw & 0x0E000090U) == 0x00000010U;
    if (!immediate && !immediate_shift && !register_shift) {
        return false;
    }

    const unsigned opcode = (raw >> 21U) & 0xFU;
    const bool set_flags = (raw & (1U << 20U)) != 0U;
    if (opcode >= 8U && opcode <= 11U && !set_flags) {
        // MRS/MSR/hint/MOVW/MOVT encodings occupy these S=0 spaces.
        return false;
    }

    const unsigned rn = (raw >> 16U) & 0xFU;
    const unsigned rd = (raw >> 12U) & 0xFU;
    const std::uint32_t left = ReadRegister(state, rn, pc);
    const ShiftResult right = DecodeOperand2(raw, pc, state);
    const bool carry_in = (state.cpsr & kFlagC) != 0U;

    std::uint32_t value = 0U;
    bool arithmetic = false;
    AddResult add{};
    switch (opcode) {
    case 0U:  // AND/TST
        value = left & right.value;
        break;
    case 1U:  // EOR/TEQ
        value = left ^ right.value;
        break;
    case 2U:  // SUB/CMP
        arithmetic = true;
        add = AddWithCarry(left, ~right.value, true);
        value = add.value;
        break;
    case 3U:  // RSB
        arithmetic = true;
        add = AddWithCarry(right.value, ~left, true);
        value = add.value;
        break;
    case 4U:  // ADD/CMN (and ADR alias when Rn == PC)
        arithmetic = true;
        add = AddWithCarry(left, right.value, false);
        value = add.value;
        break;
    case 5U:  // ADC
        arithmetic = true;
        add = AddWithCarry(left, right.value, carry_in);
        value = add.value;
        break;
    case 6U:  // SBC
        arithmetic = true;
        add = AddWithCarry(left, ~right.value, carry_in);
        value = add.value;
        break;
    case 7U:  // RSC
        arithmetic = true;
        add = AddWithCarry(right.value, ~left, carry_in);
        value = add.value;
        break;
    case 8U:  // TST
        value = left & right.value;
        break;
    case 9U:  // TEQ
        value = left ^ right.value;
        break;
    case 10U:  // CMP
        arithmetic = true;
        add = AddWithCarry(left, ~right.value, true);
        value = add.value;
        break;
    case 11U:  // CMN
        arithmetic = true;
        add = AddWithCarry(left, right.value, false);
        value = add.value;
        break;
    case 12U:  // ORR
        value = left | right.value;
        break;
    case 13U:  // MOV and LSL/LSR/ASR/ROR/RRX aliases
        value = right.value;
        break;
    case 14U:  // BIC
        value = left & ~right.value;
        break;
    case 15U:  // MVN
        value = ~right.value;
        break;
    default:
        return false;
    }

    const bool test = opcode >= 8U && opcode <= 11U;
    if (set_flags || test) {
        UpdateNz(state, value);
        if (arithmetic) {
            SetCarry(state, add.carry);
            SetOverflow(state, add.overflow);
        } else if (right.carry_valid) {
            SetCarry(state, right.carry);
        }
    }

    if (test) {
        *execution = Fallthrough(pc, state);
    } else {
        *execution = WriteDestination(rd, value, pc, state);
    }
    return true;
}

}  // namespace

ExecutionResult ExecuteCoreAlu(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state,
    MemoryBus& memory) {
    (void)memory;
    ExecutionResult result{};
    if (ExecuteHint(raw, pc, state, &result) ||
        ExecuteMultiply(raw, pc, state, &result) ||
        ExecuteMedia(raw, pc, state, &result) ||
        ExecuteDataProcessing(raw, pc, state, &result)) {
        return result;
    }
    return Unsupported(raw, pc);
}

}  // namespace oot3d::recomp::a32::internal
