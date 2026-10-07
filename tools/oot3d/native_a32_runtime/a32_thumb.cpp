#include "a32_thumb.h"

namespace oot3d::recomp::a32 {
namespace {

struct ShiftResult {
    std::uint32_t value;
    bool carry;
};

struct AddResult {
    std::uint32_t value;
    bool carry;
    bool overflow;
};

constexpr AddResult AddWithCarry(
    std::uint32_t left,
    std::uint32_t right,
    bool carry_in) noexcept {
    const std::uint64_t sum = static_cast<std::uint64_t>(left) + right +
                              (carry_in ? 1U : 0U);
    const std::uint32_t value = static_cast<std::uint32_t>(sum);
    return {value, (sum >> 32U) != 0U,
            ((~(left ^ right) & (left ^ value)) & 0x80000000U) != 0U};
}

constexpr std::uint32_t SignExtend(std::uint32_t value, unsigned bits) noexcept {
    const std::uint32_t sign = 1U << (bits - 1U);
    return (value ^ sign) - sign;
}

bool Carry(const GuestState& state) noexcept {
    return (state.cpsr & kFlagC) != 0U;
}

void SetNZ(GuestState& state, std::uint32_t value) noexcept {
    state.cpsr = (state.cpsr & ~(kFlagN | kFlagZ)) | (value & kFlagN) |
                 (value == 0U ? kFlagZ : 0U);
}

void SetC(GuestState& state, bool carry) noexcept {
    state.cpsr = carry ? state.cpsr | kFlagC : state.cpsr & ~kFlagC;
}

void SetNZCV(GuestState& state, const AddResult& result) noexcept {
    SetNZ(state, result.value);
    SetC(state, result.carry);
    state.cpsr = result.overflow ? state.cpsr | kFlagV : state.cpsr & ~kFlagV;
}

// Shifts by an immediate field, where an encoded 0 means LSL #0 (carry
// unchanged) or LSR/ASR #32.
ShiftResult ShiftImmediate(
    unsigned type,
    std::uint32_t value,
    unsigned amount,
    bool carry) noexcept {
    switch (type) {
    case 0U:
        return amount == 0U
                   ? ShiftResult{value, carry}
                   : ShiftResult{value << amount,
                                 ((value >> (32U - amount)) & 1U) != 0U};
    case 1U:
        return amount == 0U
                   ? ShiftResult{0U, (value >> 31U) != 0U}
                   : ShiftResult{value >> amount,
                                 ((value >> (amount - 1U)) & 1U) != 0U};
    default:
        if (amount == 0U) {
            return {(value >> 31U) != 0U ? 0xFFFFFFFFU : 0U,
                    (value >> 31U) != 0U};
        }
        return {static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(value) >> amount),
                ((value >> (amount - 1U)) & 1U) != 0U};
    }
}

// Shifts by the bottom byte of a register (data-processing LSL/LSR/ASR/ROR).
ShiftResult ShiftRegister(
    unsigned type,
    std::uint32_t value,
    std::uint32_t amount,
    bool carry) noexcept {
    amount &= 0xFFU;
    if (amount == 0U) {
        return {value, carry};
    }
    switch (type) {
    case 0U:  // LSL
        if (amount < 32U) {
            return {value << amount, ((value >> (32U - amount)) & 1U) != 0U};
        }
        return {0U, amount == 32U && (value & 1U) != 0U};
    case 1U:  // LSR
        if (amount < 32U) {
            return {value >> amount, ((value >> (amount - 1U)) & 1U) != 0U};
        }
        return {0U, amount == 32U && (value >> 31U) != 0U};
    case 2U:  // ASR
        if (amount < 32U) {
            return {static_cast<std::uint32_t>(
                        static_cast<std::int32_t>(value) >> amount),
                    ((value >> (amount - 1U)) & 1U) != 0U};
        }
        return {(value >> 31U) != 0U ? 0xFFFFFFFFU : 0U, (value >> 31U) != 0U};
    default: {  // ROR
        const unsigned rotate = amount & 31U;
        if (rotate == 0U) {
            return {value, (value >> 31U) != 0U};
        }
        const std::uint32_t rotated =
            (value >> rotate) | (value << (32U - rotate));
        return {rotated, (rotated >> 31U) != 0U};
    }
    }
}

class ThumbExecutor {
public:
    ThumbExecutor(GuestState& state, MemoryBus& memory) noexcept
        : mState(state), mMemory(memory) {}

    // Executes one instruction at `address` (even). Returns true when it
    // wrote the PC or stopped; `mResult` then holds the outcome.
    bool Step(std::uint32_t address) {
        mAddress = address;
        std::uint16_t half = 0U;
        if (!mMemory.Read16(address, &half)) {
            return Fault(address);
        }
        mHalf = half;
        switch (half >> 13U) {
        case 0U:
            return ShiftOrAddSubtract();
        case 1U:
            return Immediate8();
        case 2U:
            if ((half & 0xFC00U) == 0x4000U) {
                return DataProcessing();
            }
            if ((half & 0xFC00U) == 0x4400U) {
                return HighRegister();
            }
            if ((half & 0xF800U) == 0x4800U) {
                return Load32(((address + 4U) & ~3U) + ((half & 0xFFU) << 2U),
                              half >> 8U & 7U);
            }
            return RegisterOffset();
        case 3U:
            return ImmediateOffset();
        case 4U:
            return (half & 0x1000U) == 0U ? HalfwordImmediate() : StackRelative();
        case 5U:
            return (half & 0x1000U) == 0U ? AddressGeneration() : Miscellaneous();
        case 6U:
            return (half & 0x1000U) == 0U ? Multiple() : ConditionalBranch();
        default:
            return BranchFamily();
        }
    }

    // Instruction finished without writing the PC.
    void Next() noexcept { mState.r[15] = (mAddress + 2U) | 1U; }

    const ExecutionResult& Result() const noexcept { return mResult; }

private:
    std::uint32_t Read(unsigned reg) const noexcept {
        return reg == 15U ? mAddress + 4U : mState.r[reg];
    }

    bool Branch(std::uint32_t target) noexcept {
        mState.r[15] = target;
        mState.cpsr = IsThumbPc(target) ? mState.cpsr | kCpsrThumb
                                        : mState.cpsr & ~kCpsrThumb;
        mResult = {ExitKind::Branch, target, FallbackReason::None, 0U};
        return true;
    }

    bool Fault(std::uint32_t data_address) noexcept {
        mState.r[15] = mAddress | 1U;
        mResult = {ExitKind::MemoryFault, mAddress | 1U, FallbackReason::None,
                   data_address};
        return true;
    }

    bool Unsupported() noexcept {
        mState.r[15] = mAddress | 1U;
        mResult = {ExitKind::Unsupported, mAddress | 1U, FallbackReason::Core,
                   mHalf};
        return true;
    }

    bool Load32(std::uint32_t address, unsigned rd) {
        std::uint32_t value = 0U;
        if (!mMemory.Read32(address, &value)) {
            return Fault(address);
        }
        mState.r[rd] = value;
        return false;
    }

    // LSL/LSR/ASR #imm5; ADD/SUB register or #imm3.
    bool ShiftOrAddSubtract() noexcept {
        const unsigned rd = mHalf & 7U;
        const unsigned rm = mHalf >> 3U & 7U;
        const unsigned type = mHalf >> 11U & 3U;
        if (type != 3U) {
            const auto shifted = ShiftImmediate(
                type, mState.r[rm], mHalf >> 6U & 31U, Carry(mState));
            mState.r[rd] = shifted.value;
            SetNZ(mState, shifted.value);
            SetC(mState, shifted.carry);
            return false;
        }
        const std::uint32_t operand = (mHalf & 0x0400U) != 0U
                                          ? (mHalf >> 6U & 7U)
                                          : mState.r[mHalf >> 6U & 7U];
        const bool subtract = (mHalf & 0x0200U) != 0U;
        const auto result = subtract ? AddWithCarry(mState.r[rm], ~operand, true)
                                     : AddWithCarry(mState.r[rm], operand, false);
        mState.r[rd] = result.value;
        SetNZCV(mState, result);
        return false;
    }

    // MOV/CMP/ADD/SUB Rd, #imm8.
    bool Immediate8() noexcept {
        const unsigned rd = mHalf >> 8U & 7U;
        const std::uint32_t imm = mHalf & 0xFFU;
        switch (mHalf >> 11U & 3U) {
        case 0U:
            mState.r[rd] = imm;
            SetNZ(mState, imm);
            return false;
        case 1U:
            SetNZCV(mState, AddWithCarry(mState.r[rd], ~imm, true));
            return false;
        case 2U: {
            const auto result = AddWithCarry(mState.r[rd], imm, false);
            mState.r[rd] = result.value;
            SetNZCV(mState, result);
            return false;
        }
        default: {
            const auto result = AddWithCarry(mState.r[rd], ~imm, true);
            mState.r[rd] = result.value;
            SetNZCV(mState, result);
            return false;
        }
        }
    }

    bool DataProcessing() noexcept {
        const unsigned rd = mHalf & 7U;
        const std::uint32_t left = mState.r[rd];
        const std::uint32_t right = mState.r[mHalf >> 3U & 7U];
        const unsigned op = mHalf >> 6U & 15U;
        std::uint32_t value = 0U;
        switch (op) {
        case 0x0U: value = left & right; break;                  // AND
        case 0x1U: value = left ^ right; break;                  // EOR
        case 0x2U:                                                // LSL
        case 0x3U:                                                // LSR
        case 0x4U:                                                // ASR
        case 0x7U: {                                              // ROR
            const auto shifted = ShiftRegister(
                op == 0x7U ? 3U : op - 2U, left, right, Carry(mState));
            mState.r[rd] = shifted.value;
            SetNZ(mState, shifted.value);
            SetC(mState, shifted.carry);
            return false;
        }
        case 0x5U: case 0x6U: case 0x9U: case 0xBU: {  // ADC SBC NEG CMN
            const auto result =
                op == 0x5U ? AddWithCarry(left, right, Carry(mState))
                : op == 0x6U ? AddWithCarry(left, ~right, Carry(mState))
                : op == 0x9U ? AddWithCarry(0U, ~right, true)
                             : AddWithCarry(left, right, false);
            if (op != 0xBU) {
                mState.r[rd] = result.value;
            }
            SetNZCV(mState, result);
            return false;
        }
        case 0x8U:                                                // TST
            SetNZ(mState, left & right);
            return false;
        case 0xAU:                                                // CMP
            SetNZCV(mState, AddWithCarry(left, ~right, true));
            return false;
        case 0xCU: value = left | right; break;                  // ORR
        case 0xDU: value = left * right; break;                  // MUL, C kept
        case 0xEU: value = left & ~right; break;                 // BIC
        default: value = ~right; break;                           // MVN
        }
        mState.r[rd] = value;
        SetNZ(mState, value);
        return false;
    }

    // ADD/CMP/MOV with high registers; BX/BLX register.
    bool HighRegister() noexcept {
        const unsigned rd = (mHalf & 7U) | (mHalf >> 4U & 8U);
        const unsigned rm = mHalf >> 3U & 15U;
        switch (mHalf >> 8U & 3U) {
        case 0U: {
            const std::uint32_t value = Read(rd) + Read(rm);
            if (rd == 15U) {
                return Branch((value & ~1U) | 1U);
            }
            mState.r[rd] = value;
            return false;
        }
        case 1U:
            SetNZCV(mState, AddWithCarry(Read(rd), ~Read(rm), true));
            return false;
        case 2U: {
            const std::uint32_t value = Read(rm);
            if (rd == 15U) {
                return Branch((value & ~1U) | 1U);
            }
            mState.r[rd] = value;
            return false;
        }
        default: {
            const std::uint32_t target = Read(rm);
            if ((mHalf & 0x0080U) != 0U) {
                mState.r[14] = (mAddress + 2U) | 1U;
            }
            return Branch(target);
        }
        }
    }

    bool LoadStore(
        unsigned kind,
        std::uint32_t address,
        unsigned rd) {
        switch (kind) {
        case 0U:  // STR
            return mMemory.Write32(address, mState.r[rd]) ? false : Fault(address);
        case 1U:  // STRH
            return mMemory.Write16(address, static_cast<std::uint16_t>(mState.r[rd]))
                       ? false
                       : Fault(address);
        case 2U:  // STRB
            return mMemory.Write8(address, static_cast<std::uint8_t>(mState.r[rd]))
                       ? false
                       : Fault(address);
        case 4U:  // LDR
            return Load32(address, rd);
        case 3U:    // LDRSB
        case 6U: {  // LDRB
            std::uint8_t value = 0U;
            if (!mMemory.Read8(address, &value)) {
                return Fault(address);
            }
            mState.r[rd] = kind == 3U ? SignExtend(value, 8U) : value;
            return false;
        }
        default: {  // LDRH, LDRSH
            std::uint16_t value = 0U;
            if (!mMemory.Read16(address, &value)) {
                return Fault(address);
            }
            mState.r[rd] = kind == 7U ? SignExtend(value, 16U) : value;
            return false;
        }
        }
    }

    bool RegisterOffset() {
        return LoadStore(mHalf >> 9U & 7U,
                         mState.r[mHalf >> 3U & 7U] + mState.r[mHalf >> 6U & 7U],
                         mHalf & 7U);
    }

    // STR/LDR word #imm5*4, STRB/LDRB #imm5.
    bool ImmediateOffset() {
        const bool byte = (mHalf & 0x1000U) != 0U;
        const bool load = (mHalf & 0x0800U) != 0U;
        const std::uint32_t offset = (mHalf >> 6U & 31U) << (byte ? 0U : 2U);
        return LoadStore(byte ? (load ? 6U : 2U) : (load ? 4U : 0U),
                         mState.r[mHalf >> 3U & 7U] + offset, mHalf & 7U);
    }

    bool HalfwordImmediate() {
        return LoadStore((mHalf & 0x0800U) != 0U ? 5U : 1U,
                         mState.r[mHalf >> 3U & 7U] + ((mHalf >> 6U & 31U) << 1U),
                         mHalf & 7U);
    }

    bool StackRelative() {
        return LoadStore((mHalf & 0x0800U) != 0U ? 4U : 0U,
                         mState.r[13] + ((mHalf & 0xFFU) << 2U), mHalf >> 8U & 7U);
    }

    // ADR (PC-relative, word-aligned) or ADD Rd, SP, #imm8*4.
    bool AddressGeneration() noexcept {
        const std::uint32_t base =
            (mHalf & 0x0800U) != 0U ? mState.r[13] : (mAddress + 4U) & ~3U;
        mState.r[mHalf >> 8U & 7U] = base + ((mHalf & 0xFFU) << 2U);
        return false;
    }

    bool Miscellaneous() {
        if ((mHalf & 0xFF00U) == 0xB000U) {  // ADD/SUB SP, #imm7*4
            const std::uint32_t offset = (mHalf & 0x7FU) << 2U;
            mState.r[13] += (mHalf & 0x0080U) != 0U ? 0U - offset : offset;
            return false;
        }
        if ((mHalf & 0xFF00U) == 0xB200U) {  // SXTH SXTB UXTH UXTB
            const std::uint32_t value = mState.r[mHalf >> 3U & 7U];
            const unsigned rd = mHalf & 7U;
            switch (mHalf >> 6U & 3U) {
            case 0U: mState.r[rd] = SignExtend(value & 0xFFFFU, 16U); break;
            case 1U: mState.r[rd] = SignExtend(value & 0xFFU, 8U); break;
            case 2U: mState.r[rd] = value & 0xFFFFU; break;
            default: mState.r[rd] = value & 0xFFU; break;
            }
            return false;
        }
        if ((mHalf & 0xFF00U) == 0xBA00U) {  // REV REV16 REVSH
            const std::uint32_t value = mState.r[mHalf >> 3U & 7U];
            const unsigned rd = mHalf & 7U;
            switch (mHalf >> 6U & 3U) {
            case 0U:
                mState.r[rd] = (value >> 24U) | (value >> 8U & 0xFF00U) |
                               (value << 8U & 0xFF0000U) | (value << 24U);
                return false;
            case 1U:
                mState.r[rd] = (value >> 8U & 0x00FF00FFU) |
                               (value << 8U & 0xFF00FF00U);
                return false;
            case 3U:
                mState.r[rd] = SignExtend(
                    (value >> 8U & 0xFFU) | (value << 8U & 0xFF00U), 16U);
                return false;
            default:
                return Unsupported();
            }
        }
        if ((mHalf & 0xFE00U) == 0xB400U) {  // PUSH {list[, lr]}
            const unsigned list = (mHalf & 0xFFU) | ((mHalf & 0x0100U) << 6U);
            std::uint32_t address = mState.r[13] - 4U * Count(list);
            const std::uint32_t base = address;
            for (unsigned reg = 0U; reg < 16U; ++reg) {
                if ((list & (1U << reg)) == 0U) {
                    continue;
                }
                if (!mMemory.Write32(address, mState.r[reg])) {
                    return Fault(address);
                }
                address += 4U;
            }
            mState.r[13] = base;
            return false;
        }
        if ((mHalf & 0xFE00U) == 0xBC00U) {  // POP {list[, pc]}
            const unsigned list = (mHalf & 0xFFU) | ((mHalf & 0x0100U) << 7U);
            std::uint32_t loaded[16]{};
            std::uint32_t address = mState.r[13];
            for (unsigned reg = 0U; reg < 16U; ++reg) {
                if ((list & (1U << reg)) == 0U) {
                    continue;
                }
                if (!mMemory.Read32(address, &loaded[reg])) {
                    return Fault(address);
                }
                address += 4U;
            }
            for (unsigned reg = 0U; reg < 8U; ++reg) {
                if ((list & (1U << reg)) != 0U) {
                    mState.r[reg] = loaded[reg];
                }
            }
            mState.r[13] = address;
            // Loads to the PC interwork: bit 0 selects the state.
            return (list & 0x8000U) != 0U ? Branch(loaded[15]) : false;
        }
        return Unsupported();  // BKPT, CPS, SETEND, undefined
    }

    // STMIA/LDMIA Rn!, {list}
    bool Multiple() {
        const unsigned rn = mHalf >> 8U & 7U;
        const unsigned list = mHalf & 0xFFU;
        const bool load = (mHalf & 0x0800U) != 0U;
        std::uint32_t loaded[8]{};
        std::uint32_t address = mState.r[rn];
        for (unsigned reg = 0U; reg < 8U; ++reg) {
            if ((list & (1U << reg)) == 0U) {
                continue;
            }
            if (load ? !mMemory.Read32(address, &loaded[reg])
                     : !mMemory.Write32(address, mState.r[reg])) {
                return Fault(address);
            }
            address += 4U;
        }
        if (load) {
            for (unsigned reg = 0U; reg < 8U; ++reg) {
                if ((list & (1U << reg)) != 0U) {
                    mState.r[reg] = loaded[reg];
                }
            }
        }
        if (!load || (list & (1U << rn)) == 0U) {
            mState.r[rn] = address;
        }
        return false;
    }

    bool ConditionalBranch() noexcept {
        const unsigned condition = mHalf >> 8U & 15U;
        if (condition >= 14U) {
            return Unsupported();  // UDF, SVC
        }
        if (!ConditionPassed(static_cast<Condition>(condition), mState.cpsr)) {
            return false;
        }
        return Branch((mAddress + 4U + (SignExtend(mHalf & 0xFFU, 8U) << 1U)) | 1U);
    }

    // B #imm11 and the BL/BLX prefix/suffix halves, each its own instruction
    // as on ARMv6.
    bool BranchFamily() noexcept {
        const std::uint32_t imm = mHalf & 0x7FFU;
        switch (mHalf >> 11U & 3U) {
        case 0U:
            return Branch((mAddress + 4U + (SignExtend(imm, 11U) << 1U)) | 1U);
        case 2U:
            mState.r[14] = mAddress + 4U + (SignExtend(imm, 11U) << 12U);
            return false;
        case 3U: {
            const std::uint32_t target = mState.r[14] + (imm << 1U);
            mState.r[14] = (mAddress + 2U) | 1U;
            return Branch(target | 1U);
        }
        default: {
            if ((imm & 1U) != 0U) {
                return Unsupported();
            }
            const std::uint32_t target = (mState.r[14] + (imm << 1U)) & ~3U;
            mState.r[14] = (mAddress + 2U) | 1U;
            return Branch(target);
        }
        }
    }

    static unsigned Count(unsigned list) noexcept {
        unsigned count = 0U;
        for (; list != 0U; list &= list - 1U) {
            ++count;
        }
        return count;
    }

    GuestState& mState;
    MemoryBus& mMemory;
    std::uint32_t mAddress{};
    std::uint16_t mHalf{};
    ExecutionResult mResult{};
};

}  // namespace

ExecutionResult ExecuteThumbBlock(
    std::uint32_t pc,
    GuestState& state,
    MemoryBus& memory,
    std::uint32_t instruction_limit) {
    ThumbExecutor executor(state, memory);
    state.cpsr |= kCpsrThumb;
    std::uint32_t address = pc & ~1U;
    for (std::uint32_t executed = 0U; executed < instruction_limit; ++executed) {
        if (executor.Step(address)) {
            return executor.Result();
        }
        executor.Next();
        address += 2U;
    }
    const std::uint32_t next = address | 1U;
    state.r[15] = next;
    return {ExitKind::Branch, next, FallbackReason::None, 0U};
}

}  // namespace oot3d::recomp::a32
