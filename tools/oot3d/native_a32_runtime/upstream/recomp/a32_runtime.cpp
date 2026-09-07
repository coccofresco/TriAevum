#include "a32_runtime.h"
#include "a32_core.h"
#include "a32_core_internal.h"
#include "a32_vfp_scalar.h"
#include "a32_vfp_transport.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>

namespace oot3d::recomp::a32 {
namespace {

constexpr std::uint32_t kMask32 = 0xFFFFFFFFU;

constexpr bool HasFlag(std::uint8_t flags, OpFlag flag) noexcept {
    return (flags & static_cast<std::uint8_t>(flag)) != 0;
}

constexpr std::uint32_t RotateRight(std::uint32_t value, unsigned amount) noexcept {
    amount &= 31U;
    return amount == 0U ? value : (value >> amount) | (value << (32U - amount));
}

std::uint32_t ReadRegister(
    const GuestState& state,
    std::uint8_t index,
    std::uint32_t pc) noexcept {
    return index == 15U ? pc + 8U : state.r[index];
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
        return {
            amount < 32U ? value << amount : 0U,
            amount <= 32U && ((value >> (32U - amount)) & 1U) != 0U,
            true,
        };
    case 1U:  // LSR; an immediate zero encodes 32.
        amount = amount == 0U ? 32U : amount;
        return {
            amount < 32U ? value >> amount : 0U,
            amount <= 32U && ((value >> (amount - 1U)) & 1U) != 0U,
            true,
        };
    case 2U:  // ASR; an immediate zero encodes 32.
        amount = amount == 0U ? 32U : amount;
        if (amount >= 32U) {
            const bool sign = (value & kFlagN) != 0U;
            return {sign ? kMask32 : 0U, sign, true};
        }
        return {
            (value >> amount) |
                ((value & kFlagN) != 0U ? kMask32 << (32U - amount) : 0U),
            ((value >> (amount - 1U)) & 1U) != 0U,
            true,
        };
    default:  // ROR; an immediate zero encodes RRX.
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
            return {value << amount, ((value >> (32U - amount)) & 1U) != 0U, true};
        }
        return {0U, amount == 32U && (value & 1U) != 0U, true};
    case 1U:  // LSR
        if (amount < 32U) {
            return {value >> amount, ((value >> (amount - 1U)) & 1U) != 0U, true};
        }
        return {0U, amount == 32U && (value & kFlagN) != 0U, true};
    case 2U:  // ASR
        if (amount < 32U) {
            return {
                (value >> amount) |
                    ((value & kFlagN) != 0U ? kMask32 << (32U - amount) : 0U),
                ((value >> (amount - 1U)) & 1U) != 0U,
                true,
            };
        }
        return {
            (value & kFlagN) != 0U ? kMask32 : 0U,
            (value & kFlagN) != 0U,
            true,
        };
    default: {  // ROR
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

ShiftResult DecodeOperand2(
    std::uint32_t raw,
    std::uint32_t pc,
    const GuestState& state) noexcept {
    const bool carry_in = (state.cpsr & kFlagC) != 0U;
    if ((raw & (1U << 25)) != 0U) {
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
    if ((raw & (1U << 4)) == 0U) {
        return ShiftImmediate(value, type, (raw >> 7U) & 0x1FU, carry_in);
    }
    const std::uint8_t rs = static_cast<std::uint8_t>((raw >> 8U) & 0xFU);
    return ShiftRegister(value, type, ReadRegister(state, rs, pc), carry_in);
}

bool DecodeMemoryOffset(
    std::uint32_t raw,
    std::uint32_t pc,
    const GuestState& state,
    std::uint32_t* offset) noexcept {
    if ((raw & (1U << 25)) == 0U) {
        *offset = raw & 0xFFFU;
        return true;
    }
    // A32 single-data-transfer register offsets permit only immediate shifts.
    if ((raw & (1U << 4)) != 0U) {
        return false;
    }
    const std::uint8_t rm = static_cast<std::uint8_t>(raw & 0xFU);
    const unsigned type = (raw >> 5U) & 3U;
    const unsigned amount = (raw >> 7U) & 0x1FU;
    *offset = ShiftImmediate(
                  ReadRegister(state, rm, pc),
                  type,
                  amount,
                  (state.cpsr & kFlagC) != 0U)
                  .value;
    return true;
}

void UpdateNz(GuestState& state, std::uint32_t value) noexcept {
    state.cpsr &= ~(kFlagN | kFlagZ);
    if ((value & kFlagN) != 0U) {
        state.cpsr |= kFlagN;
    }
    if (value == 0U) {
        state.cpsr |= kFlagZ;
    }
}

void UpdateLogicalFlags(
    GuestState& state,
    std::uint32_t value,
    const ShiftResult& shifter) noexcept {
    UpdateNz(state, value);
    if (shifter.carry_valid) {
        state.cpsr = shifter.carry
                          ? state.cpsr | kFlagC
                          : state.cpsr & ~kFlagC;
    }
}

std::uint32_t Add32(
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

std::uint32_t Sub32(
    std::uint32_t left,
    std::uint32_t right,
    bool* carry,
    bool* overflow) noexcept {
    const std::uint32_t result = left - right;
    *carry = left >= right;
    *overflow = (((left ^ right) & (left ^ result)) & kFlagN) != 0U;
    return result;
}

void UpdateArithmeticFlags(
    GuestState& state,
    std::uint32_t value,
    bool carry,
    bool overflow) noexcept {
    UpdateNz(state, value);
    state.cpsr = carry ? state.cpsr | kFlagC : state.cpsr & ~kFlagC;
    state.cpsr = overflow ? state.cpsr | kFlagV : state.cpsr & ~kFlagV;
}

ExecutionResult InvokeFallback(
    FallbackReason reason,
    std::uint32_t pc,
    const PackedOp& op,
    GuestState& state,
    MemoryBus& memory,
    FallbackCallback fallback,
    void* fallback_user) {
    if (fallback == nullptr) {
        state.r[15] = pc;
        return {
            reason == FallbackReason::Unsupported
                ? ExitKind::Unsupported
                : ExitKind::Fallback,
            pc,
            reason,
            op.raw,
        };
    }
    state.r[15] = pc;
    ExecutionResult result = fallback(
        reason, pc, op, state, memory, fallback_user);
    if ((result.kind == ExitKind::Fallback ||
         result.kind == ExitKind::Unsupported) &&
        result.fallback == FallbackReason::None) {
        result.fallback = reason;
    }
    state.r[15] = result.pc;
    return result;
}

ExecutionResult BranchResult(
    GuestState& state,
    std::uint32_t target) noexcept {
    state.r[15] = target;
    return {ExitKind::Branch, target, FallbackReason::None, 0U};
}

bool TryExecuteNativeFunction(
    std::uint32_t target,
    GuestState& state,
    MemoryBus& memory,
    NativeFunctionCallback native_function,
    void* native_function_user,
    const std::uint32_t* native_function_pcs,
    std::size_t native_function_pc_count,
    ExecutionResult* result) {
    if (native_function == nullptr || result == nullptr) {
        return false;
    }
    const auto* found = std::lower_bound(
        native_function_pcs,
        native_function_pcs + native_function_pc_count,
        target);
    if (found != native_function_pcs + native_function_pc_count &&
        *found == target) {
        return native_function(target, state, memory, result,
                               native_function_user);
    }
    return false;
}

std::uint32_t DecodeBranchTarget(std::uint32_t raw, std::uint32_t pc) noexcept {
    std::uint32_t offset = (raw & 0x00FFFFFFU) << 2U;
    if ((offset & (1U << 25U)) != 0U) {
        offset |= 0xFC000000U;
    }
    return pc + 8U + offset;
}

}  // namespace

bool MemoryBus::Read8(std::uint32_t address, std::uint8_t* value) {
    std::uint32_t word = 0U;
    if (value == nullptr || !Read32(address & ~3U, &word)) {
        return false;
    }
    *value = static_cast<std::uint8_t>(word >> ((address & 3U) * 8U));
    return true;
}

bool MemoryBus::Read16(std::uint32_t address, std::uint16_t* value) {
    if (value == nullptr) {
        return false;
    }
    std::uint8_t low = 0U;
    std::uint8_t high = 0U;
    if (!Read8(address, &low) || !Read8(address + 1U, &high)) {
        return false;
    }
    *value = static_cast<std::uint16_t>(low) |
             static_cast<std::uint16_t>(high) << 8U;
    return true;
}

bool MemoryBus::Write8(std::uint32_t address, std::uint8_t value) {
    const std::uint32_t aligned = address & ~3U;
    std::uint32_t word = 0U;
    if (!Read32(aligned, &word)) {
        return false;
    }
    const unsigned shift = (address & 3U) * 8U;
    const std::uint32_t mask = 0xFFU << shift;
    return Write32(
        aligned,
        (word & ~mask) | (static_cast<std::uint32_t>(value) << shift));
}

bool MemoryBus::Write16(std::uint32_t address, std::uint16_t value) {
    return Write8(address, static_cast<std::uint8_t>(value)) &&
           Write8(address + 1U, static_cast<std::uint8_t>(value >> 8U));
}

bool MemoryBus::Read64(
    std::uint32_t address,
    std::uint64_t*,
    std::uint32_t* fault_address) {
    if (fault_address != nullptr) {
        *fault_address = address;
    }
    return false;
}

bool MemoryBus::Write64(
    std::uint32_t address,
    std::uint64_t,
    std::uint32_t* fault_address) {
    if (fault_address != nullptr) {
        *fault_address = address;
    }
    return false;
}

bool MemoryBus::LoadExclusive(
    std::uint32_t address,
    std::uint8_t,
    std::uint64_t*,
    std::uint64_t*,
    std::uint32_t* fault_address) {
    if (fault_address != nullptr) {
        *fault_address = address;
    }
    return false;
}

ExclusiveStoreResult MemoryBus::StoreExclusive(
    std::uint32_t address,
    std::uint8_t,
    std::uint64_t,
    std::uint64_t,
    std::uint32_t* fault_address) {
    if (fault_address != nullptr) {
        *fault_address = address;
    }
    return ExclusiveStoreResult::MemoryFault;
}

bool MemoryBus::AtomicSwap(
    std::uint32_t address,
    std::uint8_t,
    std::uint32_t,
    std::uint32_t*,
    std::uint32_t* fault_address) {
    if (fault_address != nullptr) {
        *fault_address = address;
    }
    return false;
}

bool ConditionPassed(Condition condition, std::uint32_t cpsr) noexcept {
    const bool n = (cpsr & kFlagN) != 0U;
    const bool z = (cpsr & kFlagZ) != 0U;
    const bool c = (cpsr & kFlagC) != 0U;
    const bool v = (cpsr & kFlagV) != 0U;
    switch (condition) {
    case Condition::Eq:
        return z;
    case Condition::Ne:
        return !z;
    case Condition::Cs:
        return c;
    case Condition::Cc:
        return !c;
    case Condition::Mi:
        return n;
    case Condition::Pl:
        return !n;
    case Condition::Vs:
        return v;
    case Condition::Vc:
        return !v;
    case Condition::Hi:
        return c && !z;
    case Condition::Ls:
        return !c || z;
    case Condition::Ge:
        return n == v;
    case Condition::Lt:
        return n != v;
    case Condition::Gt:
        return !z && n == v;
    case Condition::Le:
        return z || n != v;
    case Condition::Al:
        return true;
    case Condition::Nv:
        return false;
    }
    return false;
}

const Block* FindBlock(const Registry& registry, std::uint32_t pc) noexcept {
    if (registry.shards == nullptr || registry.shard_count == 0U) {
        return nullptr;
    }

    struct CacheEntry {
        const BlockShard* shards{};
        std::uint32_t shard_count{};
        std::uint32_t pc{};
        const Block* block{};
        bool valid{};
    };
    constexpr std::size_t kCacheSize = 1U << 14U;
    thread_local std::array<CacheEntry, kCacheSize> cache{};
    CacheEntry& cached = cache[(pc >> 2U) & (kCacheSize - 1U)];
    if (cached.valid && cached.shards == registry.shards &&
        cached.shard_count == registry.shard_count && cached.pc == pc) {
        return cached.block;
    }

    std::uint32_t low = 0U;
    std::uint32_t high = registry.shard_count;
    while (low < high) {
        const std::uint32_t middle = low + (high - low) / 2U;
        if (registry.shards[middle].first_pc <= pc) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    if (low == 0U) {
        return nullptr;
    }
    const BlockShard& shard = registry.shards[low - 1U];
    if (pc < shard.first_pc || pc > shard.last_pc ||
        shard.blocks == nullptr || shard.block_count == 0U) {
        return nullptr;
    }

    low = 0U;
    high = shard.block_count;
    while (low < high) {
        const std::uint32_t middle = low + (high - low) / 2U;
        const std::uint32_t block_pc = shard.blocks[middle].pc;
        if (block_pc < pc) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    const Block* block =
        low < shard.block_count && shard.blocks[low].pc == pc
            ? &shard.blocks[low]
            : nullptr;
    cached = {registry.shards, registry.shard_count, pc, block, true};
    return block;
}

const Function* FindFunction(const Registry& registry, std::uint32_t pc) noexcept {
    if (registry.functions == nullptr || registry.function_count == 0U) {
        return nullptr;
    }
    std::uint32_t low = 0U;
    std::uint32_t high = registry.function_count;
    while (low < high) {
        const std::uint32_t middle = low + (high - low) / 2U;
        if (registry.functions[middle].entry <= pc) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    if (low == 0U) {
        return nullptr;
    }
    // Inventory intervals can overlap (for example a narrow internal label
    // nested in a wider public body).  The last entry before PC is therefore
    // not necessarily the interval that contains it.
    for (std::uint32_t index = low; index != 0U; --index) {
        const Function& function = registry.functions[index - 1U];
        if (pc >= function.entry && pc < function.end) {
            return &function;
        }
    }
    return nullptr;
}

ExecutionResult ExecuteBlock(
    const Block& block,
    GuestState& state,
    MemoryBus& memory,
    FallbackCallback fallback,
    void* fallback_user,
    NativeFunctionCallback native_function,
    void* native_function_user,
    const std::uint32_t* native_function_pcs,
    std::size_t native_function_pc_count) {
    if (block.op_count != 0U && block.ops == nullptr) {
        state.r[15] = block.pc;
        return {
            ExitKind::Unsupported,
            block.pc,
            FallbackReason::Unsupported,
            0U,
        };
    }

    std::uint32_t next_pc = block.pc;
    for (std::uint32_t index = 0U; index < block.op_count; ++index) {
        const PackedOp& op = block.ops[index];
        const std::uint32_t pc = block.pc + index * 4U;
        next_pc = pc + 4U;

        const Opcode opcode = DecodeOpcode(op.metadata);
        const Condition condition = DecodeCondition(op.metadata);
        const std::uint8_t flags = DecodeFlags(op.metadata);
        if (!ConditionPassed(condition, state.cpsr)) {
            continue;
        }

        const auto unsupported = [&]() {
            return InvokeFallback(
                FallbackReason::Unsupported,
                pc,
                op,
                state,
                memory,
                fallback,
                fallback_user);
        };

        switch (opcode) {
        case Opcode::MovImm:
        case Opcode::MovReg: {
            const std::uint8_t rd = static_cast<std::uint8_t>((op.raw >> 12U) & 0xFU);
            const bool set_flags = HasFlag(flags, SetFlags) ||
                                   (op.raw & (1U << 20U)) != 0U;
            if (rd == 15U && set_flags) {
                return unsupported();
            }
            const ShiftResult operand = DecodeOperand2(op.raw, pc, state);
            if (set_flags) {
                UpdateLogicalFlags(state, operand.value, operand);
            }
            if (rd == 15U) {
                return BranchResult(state, operand.value);
            }
            state.r[rd] = operand.value;
            break;
        }

        case Opcode::Add:
        case Opcode::Sub:
        case Opcode::Cmp: {
            const std::uint8_t rn = static_cast<std::uint8_t>((op.raw >> 16U) & 0xFU);
            const std::uint8_t rd = static_cast<std::uint8_t>((op.raw >> 12U) & 0xFU);
            const bool set_flags = HasFlag(flags, SetFlags) ||
                                   (op.raw & (1U << 20U)) != 0U;
            if (opcode != Opcode::Cmp && rd == 15U && set_flags) {
                return unsupported();
            }
            const std::uint32_t left = ReadRegister(state, rn, pc);
            const std::uint32_t right = DecodeOperand2(op.raw, pc, state).value;
            bool carry = false;
            bool overflow = false;
            const std::uint32_t value = opcode == Opcode::Add
                                            ? Add32(left, right, &carry, &overflow)
                                            : Sub32(left, right, &carry, &overflow);
            if (opcode == Opcode::Cmp || set_flags) {
                UpdateArithmeticFlags(state, value, carry, overflow);
            }
            if (opcode != Opcode::Cmp) {
                if (rd == 15U) {
                    return BranchResult(state, value);
                }
                state.r[rd] = value;
            }
            break;
        }

        case Opcode::Ldr32:
        case Opcode::Str32: {
            const bool load = opcode == Opcode::Ldr32;
            const std::uint8_t rn = static_cast<std::uint8_t>((op.raw >> 16U) & 0xFU);
            const std::uint8_t rd = static_cast<std::uint8_t>((op.raw >> 12U) & 0xFU);
            const bool pre_index = (op.raw & (1U << 24U)) != 0U &&
                                   !HasFlag(flags, PostIndex);
            const bool writeback = !pre_index ||
                                   (op.raw & (1U << 21U)) != 0U ||
                                   HasFlag(flags, Writeback);
            if (writeback && rn == 15U) {
                return unsupported();
            }
            std::uint32_t offset = 0U;
            if (!DecodeMemoryOffset(op.raw, pc, state, &offset)) {
                return unsupported();
            }
            bool add_offset = (op.raw & (1U << 23U)) != 0U;
            if (HasFlag(flags, SubtractOffset)) {
                add_offset = false;
            }
            const std::uint32_t base = ReadRegister(state, rn, pc);
            const std::uint32_t adjusted = add_offset ? base + offset : base - offset;
            const std::uint32_t address = pre_index ? adjusted : base;
            const std::uint32_t stored = ReadRegister(state, rd, pc);
            std::uint32_t loaded = 0U;
            const bool accessed = load
                                      ? memory.Read32(address, &loaded)
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
                    return BranchResult(state, loaded);
                }
                state.r[rd] = loaded;
            }
            break;
        }

        case Opcode::Branch: {
            const bool link =
                HasFlag(flags, Link) || (op.raw & (1U << 24U)) != 0U;
            if (link) {
                state.r[14] = pc + 4U;
            }
            const std::uint32_t target = DecodeBranchTarget(op.raw, pc);
            ExecutionResult native_result{};
            if (link && TryExecuteNativeFunction(
                            target, state, memory, native_function,
                            native_function_user, native_function_pcs,
                            native_function_pc_count, &native_result)) {
                return native_result;
            }
            return BranchResult(state, target);
        }

        case Opcode::BranchReg: {
            const std::uint8_t rm = static_cast<std::uint8_t>(op.raw & 0xFU);
            const std::uint32_t target = ReadRegister(state, rm, pc);
            const bool link =
                HasFlag(flags, Link) || (op.raw & (1U << 5U)) != 0U;
            if (link) {
                state.r[14] = pc + 4U;
            }
            ExecutionResult native_result{};
            if (link && TryExecuteNativeFunction(
                            target, state, memory, native_function,
                            native_function_user, native_function_pcs,
                            native_function_pc_count, &native_result)) {
                return native_result;
            }
            return BranchResult(state, target);
        }

        case Opcode::Svc:
            state.r[15] = pc;
            return {
                ExitKind::Svc,
                pc,
                FallbackReason::None,
                op.raw & 0x00FFFFFFU,
            };

        case Opcode::VfpTransport: {
            ExecutionResult result =
                ExecuteVfpTransport(op.raw, pc, state, memory);
            if (result.kind == ExitKind::Fallthrough && result.pc == next_pc) {
                break;
            }
            if (result.kind == ExitKind::Unsupported) {
                return InvokeFallback(
                    FallbackReason::Vfp,
                    pc,
                    op,
                    state,
                    memory,
                    fallback,
                    fallback_user);
            }
            return result;
        }

        case Opcode::Core: {
            ExecutionResult result = ExecuteCore(op.raw, pc, state, memory);
            if (result.kind == ExitKind::Fallthrough && result.pc == next_pc) {
                break;
            }
            if (result.kind == ExitKind::Unsupported) {
                return InvokeFallback(
                    FallbackReason::Core,
                    pc,
                    op,
                    state,
                    memory,
                    fallback,
                    fallback_user);
            }
            return result;
        }

        case Opcode::CoreSystem:
        case Opcode::CoreAlu:
        case Opcode::CoreMemory: {
            ExecutionResult result{};
            if (opcode == Opcode::CoreSystem) {
                result = internal::ExecuteCoreSystem(op.raw, pc, state);
            } else if (opcode == Opcode::CoreAlu) {
                result =
                    internal::ExecuteCoreAlu(op.raw, pc, state, memory);
            } else {
                result =
                    internal::ExecuteCoreMemory(op.raw, pc, state, memory);
            }
            if (result.kind == ExitKind::Fallthrough &&
                result.pc == next_pc) {
                break;
            }
            if (result.kind == ExitKind::Unsupported) {
                return InvokeFallback(
                    FallbackReason::Core,
                    pc,
                    op,
                    state,
                    memory,
                    fallback,
                    fallback_user);
            }
            return result;
        }

        case Opcode::VfpScalar: {
            ExecutionResult result = ExecuteVfpScalar(op.raw, pc, state);
            if (result.kind == ExitKind::Fallthrough && result.pc == next_pc) {
                break;
            }
            if (result.kind == ExitKind::Unsupported) {
                return InvokeFallback(
                    FallbackReason::Vfp,
                    pc,
                    op,
                    state,
                    memory,
                    fallback,
                    fallback_user);
            }
            return result;
        }

        case Opcode::Unsupported: {
            ExecutionResult result = InvokeFallback(
                FallbackReason::Unsupported,
                pc,
                op,
                state,
                memory,
                fallback,
                fallback_user);
            if (result.kind == ExitKind::Fallthrough && result.pc == next_pc) {
                break;
            }
            return result;
        }
        }
    }
    state.r[15] = next_pc;
    return {
        ExitKind::Fallthrough,
        next_pc,
        FallbackReason::None,
        0U,
    };
}

ExecutionResult Dispatch(
    const Registry& registry,
    std::uint32_t entry_pc,
    GuestState& state,
    MemoryBus& memory,
    FallbackCallback fallback,
    void* fallback_user,
    std::uint32_t block_limit,
    BlockEntryCallback block_entry,
    void* block_entry_user,
    const std::uint32_t* block_entry_pcs,
    std::size_t block_entry_pc_count,
    NativeFunctionCallback native_function,
    void* native_function_user,
    const std::uint32_t* native_function_pcs,
    std::size_t native_function_pc_count,
    NativeBlockCallback native_block,
    void* native_block_user) {
    std::uint32_t pc = entry_pc;
    state.r[15] = pc;
    for (std::uint32_t executed = 0U; executed < block_limit; ++executed) {
        bool notify_block_entry = block_entry != nullptr &&
                                  block_entry_pc_count == 0U;
        for (std::size_t i = 0U;
             !notify_block_entry && i < block_entry_pc_count; ++i) {
            notify_block_entry = block_entry_pcs[i] == pc;
        }
        if (notify_block_entry) {
            block_entry(pc, state, memory, block_entry_user);
        }
        ExecutionResult native_function_result{};
        if (TryExecuteNativeFunction(
                pc, state, memory, native_function, native_function_user,
                native_function_pcs, native_function_pc_count,
                &native_function_result)) {
            state.r[15] = native_function_result.pc;
            if (native_function_result.kind == ExitKind::Fallthrough ||
                native_function_result.kind == ExitKind::Branch) {
                pc = native_function_result.pc;
                continue;
            }
            return native_function_result;
        }
        const Block* block = FindBlock(registry, pc);
        if (native_block != nullptr &&
            (block == nullptr || block->native_candidate)) {
            ExecutionResult result{};
            const std::uint32_t remaining_blocks = block_limit - executed;
            std::uint32_t blocks_consumed = 1U;
            if (native_block(pc, state, memory, &result, remaining_blocks,
                             &blocks_consumed, native_block_user)) {
                blocks_consumed = std::clamp(
                    blocks_consumed, 1U, remaining_blocks);
                executed += blocks_consumed - 1U;
                state.r[15] = result.pc;
                if (result.kind == ExitKind::Fallthrough ||
                    result.kind == ExitKind::Branch) {
                    pc = result.pc;
                    continue;
                }
                return result;
            }
        }
        if (block == nullptr) {
            if (fallback != nullptr) {
                const PackedOp missing{
                    0U,
                    EncodeMetadata(Opcode::Unsupported, Condition::Al),
                };
                ExecutionResult result = fallback(
                    FallbackReason::MissingBlock,
                    pc,
                    missing,
                    state,
                    memory,
                    fallback_user);
                if ((result.kind == ExitKind::Fallback ||
                     result.kind == ExitKind::Unsupported) &&
                    result.fallback == FallbackReason::None) {
                    result.fallback = FallbackReason::MissingBlock;
                }
                state.r[15] = result.pc;
                if (result.kind == ExitKind::Fallthrough ||
                    result.kind == ExitKind::Branch) {
                    pc = result.pc;
                    continue;
                }
                return result;
            }
            return {
                ExitKind::MissingBlock,
                pc,
                FallbackReason::None,
                0U,
            };
        }
        ExecutionResult result = ExecuteBlock(
            *block, state, memory, fallback, fallback_user,
            native_function, native_function_user, native_function_pcs,
            native_function_pc_count);
        if (result.kind != ExitKind::Fallthrough &&
            result.kind != ExitKind::Branch) {
            return result;
        }
        pc = result.pc;
        state.r[15] = pc;
    }
    return {
        ExitKind::BlockLimit,
        pc,
        FallbackReason::None,
        block_limit,
    };
}

}  // namespace oot3d::recomp::a32
