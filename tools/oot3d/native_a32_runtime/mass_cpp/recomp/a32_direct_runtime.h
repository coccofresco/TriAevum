#pragma once

#include <cstdint>

#include "a32_core.h"
#include "a32_runtime.h"
#include "a32_vfp_scalar.h"
#include "a32_vfp_transport.h"

namespace oot3d::recomp::a32::direct {

struct ExecutionCursor;

using BlockFunction = ExecutionResult (*)(
    GuestState& state,
    MemoryBus& memory,
    FallbackCallback fallback,
    void* fallback_user,
    ExecutionCursor& cursor);

enum class LazyFlagKind : std::uint8_t {
    None,
    Logical,
    Arithmetic,
};

struct LazyFlags {
    LazyFlagKind kind{LazyFlagKind::None};
    std::uint32_t value{};
    std::uint32_t preserved_cv{};
    bool carry{};
    bool overflow{};
    bool carry_valid{};
};

struct ExecutionCursor {
    std::uint32_t next_pc{};
    BlockFunction next{};
    LazyFlags lazy_flags{};
};

struct Block {
    std::uint32_t pc{};
    BlockFunction execute{};
};

struct Function {
    std::uint32_t entry{};
    std::uint32_t end{};
    const char* name{};
};

struct BlockShard {
    std::uint32_t first_pc{};
    std::uint32_t last_pc{};
    const Block* blocks{};
    std::uint32_t block_count{};
};

struct Registry {
    const BlockShard* shards{};
    std::uint32_t shard_count{};
    const Function* functions{};
    std::uint32_t function_count{};
};

struct PcFilterView {
    std::uint32_t base_pc{};
    std::size_t slot_count{};
    const std::uint64_t* words{};
    bool match_all{};

    bool Contains(std::uint32_t pc) const noexcept {
        if (match_all) {
            return true;
        }
        if (words == nullptr || pc < base_pc) {
            return false;
        }
        const std::uint32_t delta = pc - base_pc;
        if ((delta & 3U) != 0U) {
            return false;
        }
        const std::size_t slot = delta >> 2U;
        return slot < slot_count &&
               (words[slot >> 6U] & (1ULL << (slot & 63U))) != 0U;
    }
};

const Block* FindBlock(const Registry& registry, std::uint32_t pc) noexcept;
const Function* FindFunction(const Registry& registry, std::uint32_t pc) noexcept;

ExecutionResult Dispatch(
    const Registry& registry,
    std::uint32_t entry_pc,
    GuestState& state,
    MemoryBus& memory,
    FallbackCallback fallback = nullptr,
    void* fallback_user = nullptr,
    std::uint32_t block_limit = 1'000'000U);

ExecutionResult DispatchObserved(
    const Registry& registry,
    std::uint32_t entry_pc,
    GuestState& state,
    MemoryBus& memory,
    FallbackCallback fallback,
    void* fallback_user,
    std::uint32_t block_limit,
    std::uint32_t* blocks_executed,
    BlockEntryCallback block_entry,
    void* block_entry_user,
    PcFilterView block_entry_filter,
    bool skip_first_block_entry,
    PcFilterView excluded_block_filter,
    PcFilterView observable_exit_filter = {});

ExecutionResult InvokeFallback(
    FallbackReason reason,
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state,
    MemoryBus& memory,
    FallbackCallback fallback,
    void* fallback_user);

constexpr bool HasFlag(std::uint8_t flags, OpFlag flag) noexcept {
    return (flags & static_cast<std::uint8_t>(flag)) != 0U;
}

constexpr std::uint32_t RotateRight(
    std::uint32_t value, unsigned amount) noexcept {
    amount &= 31U;
    return amount == 0U
               ? value
               : (value >> amount) | (value << (32U - amount));
}

inline std::uint32_t ReadRegister(
    const GuestState& state,
    std::uint8_t index,
    std::uint32_t pc) noexcept {
    return index == 15U ? pc + 8U : state.r[index];
}

inline std::uint32_t ReadCpsr(
    const ExecutionCursor& cursor, const GuestState& state) noexcept {
    const LazyFlags& pending = cursor.lazy_flags;
    if (pending.kind == LazyFlagKind::None) {
        return state.cpsr;
    }
    std::uint32_t value = state.cpsr & ~(kFlagN | kFlagZ | kFlagC | kFlagV);
    if ((pending.value & kFlagN) != 0U) {
        value |= kFlagN;
    }
    if (pending.value == 0U) {
        value |= kFlagZ;
    }
    if (pending.kind == LazyFlagKind::Arithmetic) {
        if (pending.carry) {
            value |= kFlagC;
        }
        if (pending.overflow) {
            value |= kFlagV;
        }
    } else {
        value |= pending.preserved_cv & kFlagV;
        if (pending.carry_valid ? pending.carry
                                : (pending.preserved_cv & kFlagC) != 0U) {
            value |= kFlagC;
        }
    }
    return value;
}

inline bool ReadCarry(
    const ExecutionCursor& cursor, const GuestState& state) noexcept {
    return (ReadCpsr(cursor, state) & kFlagC) != 0U;
}

constexpr bool ImmediateShiftIsRrx(std::uint32_t raw) noexcept {
    return (raw & (1U << 4U)) == 0U &&
           ((raw >> 5U) & 3U) == 3U &&
           ((raw >> 7U) & 0x1FU) == 0U;
}

inline bool DataOperandCarry(
    std::uint32_t raw,
    const ExecutionCursor& cursor,
    const GuestState& state) noexcept {
    // A data-processing immediate never needs carry to form its value.
    // Among register operands only immediate ROR #0 is RRX.
    return (raw & (1U << 25U)) == 0U && ImmediateShiftIsRrx(raw)
               ? ReadCarry(cursor, state)
               : false;
}

inline bool MemoryOffsetCarry(
    std::uint32_t raw,
    const ExecutionCursor& cursor,
    const GuestState& state) noexcept {
    // Single-transfer bit 25 has the inverse role: one selects a shifted
    // register offset.  Again, only its RRX encoding consumes CPSR.C.
    return (raw & (1U << 25U)) != 0U && ImmediateShiftIsRrx(raw)
               ? ReadCarry(cursor, state)
               : false;
}

inline void MaterializeFlags(
    ExecutionCursor& cursor, GuestState& state) noexcept {
    if (cursor.lazy_flags.kind == LazyFlagKind::None) {
        return;
    }
    state.cpsr = ReadCpsr(cursor, state);
    cursor.lazy_flags = {};
}

struct ShiftResult {
    std::uint32_t value{};
    bool carry{};
    bool carry_valid{};
};

inline ShiftResult ShiftImmediate(
    std::uint32_t value,
    unsigned type,
    unsigned amount,
    bool carry_in) noexcept {
    switch (type & 3U) {
    case 0U:
        if (amount == 0U) {
            return {value, carry_in, false};
        }
        return {
            amount < 32U ? value << amount : 0U,
            amount <= 32U && ((value >> (32U - amount)) & 1U) != 0U,
            true,
        };
    case 1U:
        amount = amount == 0U ? 32U : amount;
        return {
            amount < 32U ? value >> amount : 0U,
            amount <= 32U && ((value >> (amount - 1U)) & 1U) != 0U,
            true,
        };
    case 2U:
        amount = amount == 0U ? 32U : amount;
        if (amount >= 32U) {
            const bool sign = (value & kFlagN) != 0U;
            return {sign ? 0xFFFFFFFFU : 0U, sign, true};
        }
        return {
            (value >> amount) |
                ((value & kFlagN) != 0U
                     ? 0xFFFFFFFFU << (32U - amount)
                     : 0U),
            ((value >> (amount - 1U)) & 1U) != 0U,
            true,
        };
    default:
        if (amount == 0U) {
            return {
                (carry_in ? kFlagN : 0U) | (value >> 1U),
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

inline ShiftResult ShiftRegister(
    std::uint32_t value,
    unsigned type,
    unsigned amount,
    bool carry_in) noexcept {
    amount &= 0xFFU;
    if (amount == 0U) {
        return {value, carry_in, false};
    }
    switch (type & 3U) {
    case 0U:
        if (amount < 32U) {
            return {
                value << amount,
                ((value >> (32U - amount)) & 1U) != 0U,
                true,
            };
        }
        return {0U, amount == 32U && (value & 1U) != 0U, true};
    case 1U:
        if (amount < 32U) {
            return {
                value >> amount,
                ((value >> (amount - 1U)) & 1U) != 0U,
                true,
            };
        }
        return {0U, amount == 32U && (value & kFlagN) != 0U, true};
    case 2U:
        if (amount < 32U) {
            return {
                (value >> amount) |
                    ((value & kFlagN) != 0U
                         ? 0xFFFFFFFFU << (32U - amount)
                         : 0U),
                ((value >> (amount - 1U)) & 1U) != 0U,
                true,
            };
        }
        return {
            (value & kFlagN) != 0U ? 0xFFFFFFFFU : 0U,
            (value & kFlagN) != 0U,
            true,
        };
    default: {
        const unsigned rotate = amount & 31U;
        if (rotate == 0U) {
            return {value, (value & kFlagN) != 0U, true};
        }
        return {
            RotateRight(value, rotate),
            ((value >> (rotate - 1U)) & 1U) != 0U,
            true,
        };
    }
    }
}

inline ShiftResult DecodeOperand2(
    std::uint32_t raw,
    std::uint32_t pc,
    const GuestState& state,
    bool carry_in) noexcept {
    if ((raw & (1U << 25U)) != 0U) {
        const unsigned rotate = ((raw >> 8U) & 0xFU) * 2U;
        const std::uint32_t value = RotateRight(raw & 0xFFU, rotate);
        return {
            value,
            rotate == 0U ? carry_in : (value & kFlagN) != 0U,
            rotate != 0U,
        };
    }

    const std::uint8_t rm = static_cast<std::uint8_t>(raw & 0xFU);
    const std::uint32_t value = ReadRegister(state, rm, pc);
    const unsigned type = (raw >> 5U) & 3U;
    if ((raw & (1U << 4U)) == 0U) {
        return ShiftImmediate(value, type, (raw >> 7U) & 0x1FU, carry_in);
    }
    const std::uint8_t rs = static_cast<std::uint8_t>((raw >> 8U) & 0xFU);
    return ShiftRegister(value, type, ReadRegister(state, rs, pc), carry_in);
}

inline ShiftResult DecodeOperand2(
    std::uint32_t raw,
    std::uint32_t pc,
    const GuestState& state) noexcept {
    return DecodeOperand2(raw, pc, state, (state.cpsr & kFlagC) != 0U);
}

inline bool DecodeMemoryOffset(
    std::uint32_t raw,
    std::uint32_t pc,
    const GuestState& state,
    bool carry_in,
    std::uint32_t* offset) noexcept {
    if ((raw & (1U << 25U)) == 0U) {
        *offset = raw & 0xFFFU;
        return true;
    }
    if ((raw & (1U << 4U)) != 0U) {
        return false;
    }
    const std::uint8_t rm = static_cast<std::uint8_t>(raw & 0xFU);
    *offset = ShiftImmediate(
                  ReadRegister(state, rm, pc),
                  (raw >> 5U) & 3U,
                  (raw >> 7U) & 0x1FU,
                  carry_in)
                  .value;
    return true;
}

inline std::uint32_t Add32(
    std::uint32_t left,
    std::uint32_t right,
    bool* carry,
    bool* overflow) noexcept {
    const std::uint64_t wide = static_cast<std::uint64_t>(left) + right;
    const std::uint32_t result = static_cast<std::uint32_t>(wide);
    *carry = (wide >> 32U) != 0U;
    *overflow = ((~(left ^ right) & (left ^ result)) & kFlagN) != 0U;
    return result;
}

inline std::uint32_t Sub32(
    std::uint32_t left,
    std::uint32_t right,
    bool* carry,
    bool* overflow) noexcept {
    const std::uint32_t result = left - right;
    *carry = left >= right;
    *overflow = (((left ^ right) & (left ^ result)) & kFlagN) != 0U;
    return result;
}

inline void DeferLogicalFlags(
    ExecutionCursor& cursor,
    const GuestState& state,
    std::uint32_t value,
    const ShiftResult& shifter) noexcept {
    const std::uint32_t previous = ReadCpsr(cursor, state);
    cursor.lazy_flags = {
        LazyFlagKind::Logical,
        value,
        previous & (kFlagC | kFlagV),
        shifter.carry,
        false,
        shifter.carry_valid,
    };
}

inline void DeferArithmeticFlags(
    ExecutionCursor& cursor,
    std::uint32_t value,
    bool carry,
    bool overflow) noexcept {
    cursor.lazy_flags = {
        LazyFlagKind::Arithmetic,
        value,
        0U,
        carry,
        overflow,
        true,
    };
}

inline ExecutionResult Fallthrough(
    GuestState& state, std::uint32_t next_pc) noexcept {
    (void)state;
    return {ExitKind::Fallthrough, next_pc, FallbackReason::None, 0U};
}

inline ExecutionResult Branch(
    GuestState& state, std::uint32_t target) noexcept {
    state.r[15] = target;
    return {ExitKind::Branch, target, FallbackReason::None, 0U};
}

inline std::uint32_t DecodeBranchTarget(
    std::uint32_t raw, std::uint32_t pc) noexcept {
    std::uint32_t offset = (raw & 0x00FFFFFFU) << 2U;
    if ((offset & (1U << 25U)) != 0U) {
        offset |= 0xFC000000U;
    }
    return pc + 8U + offset;
}

inline ExecutionResult ExecuteMov(
    std::uint32_t raw,
    std::uint32_t pc,
    std::uint8_t flags,
    GuestState& state,
    ExecutionCursor& cursor) noexcept {
    const std::uint8_t rd = static_cast<std::uint8_t>((raw >> 12U) & 0xFU);
    const bool set_flags =
        HasFlag(flags, SetFlags) || (raw & (1U << 20U)) != 0U;
    if (rd == 15U && set_flags) {
        return {
            ExitKind::Unsupported,
            pc,
            FallbackReason::Unsupported,
            raw,
        };
    }
    const ShiftResult operand = DecodeOperand2(
        raw, pc, state, DataOperandCarry(raw, cursor, state));
    if (set_flags) {
        DeferLogicalFlags(cursor, state, operand.value, operand);
    }
    if (rd == 15U) {
        return Branch(state, operand.value);
    }
    state.r[rd] = operand.value;
    return Fallthrough(state, pc + 4U);
}

inline ExecutionResult ExecuteArithmetic(
    Opcode opcode,
    std::uint32_t raw,
    std::uint32_t pc,
    std::uint8_t flags,
    GuestState& state,
    ExecutionCursor& cursor) noexcept {
    const std::uint8_t rn = static_cast<std::uint8_t>((raw >> 16U) & 0xFU);
    const std::uint8_t rd = static_cast<std::uint8_t>((raw >> 12U) & 0xFU);
    const bool set_flags =
        HasFlag(flags, SetFlags) || (raw & (1U << 20U)) != 0U;
    if (opcode != Opcode::Cmp && rd == 15U && set_flags) {
        return {
            ExitKind::Unsupported,
            pc,
            FallbackReason::Unsupported,
            raw,
        };
    }
    const std::uint32_t left = ReadRegister(state, rn, pc);
    const std::uint32_t right = DecodeOperand2(
                                    raw,
                                    pc,
                                    state,
                                    DataOperandCarry(raw, cursor, state))
                                    .value;
    bool carry = false;
    bool overflow = false;
    std::uint32_t value = 0U;
    if (opcode == Opcode::Add) {
        value = set_flags ? Add32(left, right, &carry, &overflow)
                          : left + right;
    } else {
        value = set_flags ? Sub32(left, right, &carry, &overflow)
                          : left - right;
    }
    if (opcode == Opcode::Cmp || set_flags) {
        DeferArithmeticFlags(cursor, value, carry, overflow);
    }
    if (opcode != Opcode::Cmp) {
        if (rd == 15U) {
            return Branch(state, value);
        }
        state.r[rd] = value;
    }
    return Fallthrough(state, pc + 4U);
}

inline ExecutionResult ExecuteAdd(
    std::uint32_t raw,
    std::uint32_t pc,
    std::uint8_t flags,
    GuestState& state,
    ExecutionCursor& cursor) noexcept {
    return ExecuteArithmetic(Opcode::Add, raw, pc, flags, state, cursor);
}

inline ExecutionResult ExecuteSub(
    std::uint32_t raw,
    std::uint32_t pc,
    std::uint8_t flags,
    GuestState& state,
    ExecutionCursor& cursor) noexcept {
    return ExecuteArithmetic(Opcode::Sub, raw, pc, flags, state, cursor);
}

inline ExecutionResult ExecuteCmp(
    std::uint32_t raw,
    std::uint32_t pc,
    std::uint8_t flags,
    GuestState& state,
    ExecutionCursor& cursor) noexcept {
    return ExecuteArithmetic(Opcode::Cmp, raw, pc, flags, state, cursor);
}

inline ExecutionResult ExecuteMemory(
    bool load,
    std::uint32_t raw,
    std::uint32_t pc,
    std::uint8_t flags,
    GuestState& state,
    MemoryBus& memory,
    ExecutionCursor& cursor) noexcept {
    const std::uint8_t rn = static_cast<std::uint8_t>((raw >> 16U) & 0xFU);
    const std::uint8_t rd = static_cast<std::uint8_t>((raw >> 12U) & 0xFU);
    const bool pre_index =
        (raw & (1U << 24U)) != 0U && !HasFlag(flags, PostIndex);
    const bool writeback =
        !pre_index || (raw & (1U << 21U)) != 0U || HasFlag(flags, Writeback);
    if (writeback && rn == 15U) {
        return {
            ExitKind::Unsupported,
            pc,
            FallbackReason::Unsupported,
            raw,
        };
    }
    std::uint32_t offset = 0U;
    if (!DecodeMemoryOffset(
            raw, pc, state, MemoryOffsetCarry(raw, cursor, state), &offset)) {
        return {
            ExitKind::Unsupported,
            pc,
            FallbackReason::Unsupported,
            raw,
        };
    }
    bool add_offset = (raw & (1U << 23U)) != 0U;
    if (HasFlag(flags, SubtractOffset)) {
        add_offset = false;
    }
    const std::uint32_t base = ReadRegister(state, rn, pc);
    const std::uint32_t adjusted = add_offset ? base + offset : base - offset;
    const std::uint32_t address = pre_index ? adjusted : base;
    const std::uint32_t stored = ReadRegister(state, rd, pc);
    std::uint32_t loaded = 0U;
    const bool accessed = load ? memory.Read32(address, &loaded)
                               : memory.Write32(address, stored);
    if (!accessed) {
        state.r[15] = pc;
        return {
            ExitKind::MemoryFault,
            pc,
            FallbackReason::None,
            address,
        };
    }
    if (writeback) {
        state.r[rn] = adjusted;
    }
    if (load) {
        if (rd == 15U) {
            return Branch(state, loaded);
        }
        state.r[rd] = loaded;
    }
    return Fallthrough(state, pc + 4U);
}

inline ExecutionResult ExecuteLdr32(
    std::uint32_t raw,
    std::uint32_t pc,
    std::uint8_t flags,
    GuestState& state,
    MemoryBus& memory,
    ExecutionCursor& cursor) noexcept {
    return ExecuteMemory(true, raw, pc, flags, state, memory, cursor);
}

inline ExecutionResult ExecuteStr32(
    std::uint32_t raw,
    std::uint32_t pc,
    std::uint8_t flags,
    GuestState& state,
    MemoryBus& memory,
    ExecutionCursor& cursor) noexcept {
    return ExecuteMemory(false, raw, pc, flags, state, memory, cursor);
}

inline ExecutionResult ExecuteBranch(
    std::uint32_t raw,
    std::uint32_t pc,
    std::uint8_t flags,
    GuestState& state) noexcept {
    if (HasFlag(flags, Link) || (raw & (1U << 24U)) != 0U) {
        state.r[14] = pc + 4U;
    }
    return Branch(state, DecodeBranchTarget(raw, pc));
}

inline ExecutionResult ExecuteBranchReg(
    std::uint32_t raw,
    std::uint32_t pc,
    std::uint8_t flags,
    GuestState& state) noexcept {
    const std::uint8_t rm = static_cast<std::uint8_t>(raw & 0xFU);
    const std::uint32_t target = ReadRegister(state, rm, pc);
    if (HasFlag(flags, Link) || (raw & (1U << 5U)) != 0U) {
        state.r[14] = pc + 4U;
    }
    return Branch(state, target);
}

inline ExecutionResult ExecuteSvc(
    std::uint32_t raw, std::uint32_t pc, GuestState& state) noexcept {
    state.r[15] = pc;
    return {
        ExitKind::Svc,
        pc,
        FallbackReason::None,
        raw & 0x00FFFFFFU,
    };
}

inline bool IsLinear(
    const ExecutionResult& result, std::uint32_t next_pc) noexcept {
    return result.kind == ExitKind::Fallthrough && result.pc == next_pc;
}

inline ExecutionResult ExecuteCoreExact(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state,
    MemoryBus& memory,
    FallbackCallback fallback,
    void* fallback_user,
    ExecutionCursor& cursor) {
    MaterializeFlags(cursor, state);
    ExecutionResult result = ExecuteCore(raw, pc, state, memory);
    if (result.kind == ExitKind::Unsupported) {
        return InvokeFallback(
            FallbackReason::Core,
            raw,
            pc,
            state,
            memory,
            fallback,
            fallback_user);
    }
    return result;
}

inline ExecutionResult ExecuteVfpTransportExact(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state,
    MemoryBus& memory,
    FallbackCallback fallback,
    void* fallback_user,
    ExecutionCursor& cursor) {
    MaterializeFlags(cursor, state);
    ExecutionResult result = ExecuteVfpTransport(raw, pc, state, memory);
    if (result.kind == ExitKind::Unsupported) {
        return InvokeFallback(
            FallbackReason::Vfp,
            raw,
            pc,
            state,
            memory,
            fallback,
            fallback_user);
    }
    return result;
}

inline ExecutionResult ExecuteVfpScalarExact(
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state,
    MemoryBus& memory,
    FallbackCallback fallback,
    void* fallback_user,
    ExecutionCursor& cursor) {
    MaterializeFlags(cursor, state);
    ExecutionResult result = ExecuteVfpScalar(raw, pc, state);
    if (result.kind == ExitKind::Unsupported) {
        return InvokeFallback(
            FallbackReason::Vfp,
            raw,
            pc,
            state,
            memory,
            fallback,
            fallback_user);
    }
    return result;
}

}  // namespace oot3d::recomp::a32::direct
