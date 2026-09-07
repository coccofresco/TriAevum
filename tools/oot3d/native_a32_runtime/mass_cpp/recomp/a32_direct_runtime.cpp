#include "a32_direct_runtime.h"

#include <algorithm>

namespace oot3d::recomp::a32::direct {

ExecutionResult InvokeFallback(
    FallbackReason reason,
    std::uint32_t raw,
    std::uint32_t pc,
    GuestState& state,
    MemoryBus& memory,
    FallbackCallback fallback,
    void* fallback_user) {
    state.r[15] = pc;
    if (fallback == nullptr) {
        return {
            reason == FallbackReason::Unsupported
                ? ExitKind::Unsupported
                : ExitKind::Fallback,
            pc,
            reason,
            raw,
        };
    }
    const PackedOp transient{
        raw,
        EncodeMetadata(Opcode::Unsupported, Condition::Al),
    };
    ExecutionResult result = fallback(
        reason, pc, transient, state, memory, fallback_user);
    if ((result.kind == ExitKind::Fallback ||
         result.kind == ExitKind::Unsupported) &&
        result.fallback == FallbackReason::None) {
        result.fallback = reason;
    }
    state.r[15] = result.pc;
    return result;
}

const Block* FindBlock(const Registry& registry, std::uint32_t pc) noexcept {
    if (registry.shards == nullptr || registry.shard_count == 0U) {
        return nullptr;
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
        if (shard.blocks[middle].pc < pc) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    return low < shard.block_count && shard.blocks[low].pc == pc
               ? &shard.blocks[low]
               : nullptr;
}

const Function* FindFunction(
    const Registry& registry, std::uint32_t pc) noexcept {
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
    for (std::uint32_t index = low; index != 0U; --index) {
        const Function& function = registry.functions[index - 1U];
        if (pc >= function.entry && pc < function.end) {
            return &function;
        }
    }
    return nullptr;
}

ExecutionResult Dispatch(
    const Registry& registry,
    std::uint32_t entry_pc,
    GuestState& state,
    MemoryBus& memory,
    FallbackCallback fallback,
    void* fallback_user,
    std::uint32_t block_limit) {
    return DispatchObserved(
        registry, entry_pc, state, memory, fallback, fallback_user,
        block_limit, nullptr, nullptr, nullptr, {}, false, {});
}

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
    PcFilterView observable_exit_filter) {
    if (blocks_executed != nullptr) {
        *blocks_executed = 0U;
    }
    const auto finish = [blocks_executed](ExecutionResult result,
                                           std::uint32_t count) {
        if (blocks_executed != nullptr) {
            *blocks_executed = count;
        }
        return result;
    };
    std::uint32_t pc = entry_pc;
    state.r[15] = pc;
    const Block* block = FindBlock(registry, pc);
    Block hinted_block{};
    ExecutionCursor cursor{};
    for (std::uint32_t executed = 0U; executed < block_limit; ++executed) {
        const bool first_block = executed == 0U;
        if (!first_block &&
            (excluded_block_filter.Contains(pc) ||
             observable_exit_filter.Contains(pc))) {
            MaterializeFlags(cursor, state);
            return finish({
                ExitKind::Branch,
                pc,
                FallbackReason::None,
                0U,
            }, executed);
        }
        if (block_entry != nullptr &&
            !(first_block && skip_first_block_entry) &&
            block_entry_filter.Contains(pc)) {
            MaterializeFlags(cursor, state);
            block_entry(pc, state, memory, block_entry_user);
        }
        if (block == nullptr || block->execute == nullptr) {
            MaterializeFlags(cursor, state);
            if (fallback != nullptr) {
                ExecutionResult result = InvokeFallback(
                    FallbackReason::MissingBlock,
                    0U,
                    pc,
                    state,
                    memory,
                    fallback,
                    fallback_user);
                if (result.kind == ExitKind::Fallthrough ||
                    result.kind == ExitKind::Branch) {
                    pc = result.pc;
                    block = FindBlock(registry, pc);
                    continue;
                }
                return finish(result, executed + 1U);
            }
            return finish({
                ExitKind::MissingBlock,
                pc,
                FallbackReason::None,
                0U,
            }, executed + 1U);
        }
        cursor.next_pc = 0U;
        cursor.next = nullptr;
        ExecutionResult result = block->execute(
            state, memory, fallback, fallback_user, cursor);
        if (result.kind != ExitKind::Fallthrough &&
            result.kind != ExitKind::Branch) {
            MaterializeFlags(cursor, state);
            return finish(result, executed + 1U);
        }
        pc = result.pc;
        state.r[15] = pc;
        block = cursor.next != nullptr && cursor.next_pc == pc
                    ? nullptr
                    : FindBlock(registry, pc);
        if (cursor.next != nullptr && cursor.next_pc == pc) {
            // The generated source supplied the exact statically-known
            // successor.  Keep a lightweight synthetic block so the next
            // iteration invokes it without any registry lookup.
            hinted_block = {pc, cursor.next};
            block = &hinted_block;
        } else if (block == nullptr && fallback == nullptr) {
            // A branch outside the generated corpus is a normal region exit.
            // Flush lazy architectural state and let the embedding dispatcher
            // continue from the target with its next available backend.
            MaterializeFlags(cursor, state);
            return finish({
                ExitKind::Branch,
                pc,
                FallbackReason::None,
                0U,
            }, executed + 1U);
        }
    }
    MaterializeFlags(cursor, state);
    return finish({
        ExitKind::BlockLimit,
        pc,
        FallbackReason::None,
        block_limit,
    }, block_limit);
}

}  // namespace oot3d::recomp::a32::direct
