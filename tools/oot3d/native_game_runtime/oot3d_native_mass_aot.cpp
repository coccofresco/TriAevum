#include "oot3d_native_mass_aot.h"

#include "oot3d_native_a32_memory.h"
#include "oot3d_typed_gameplay_bridge.h"

#if defined(OOT3D_NATIVE_GENERATED_MASS_AOT)
#include "oot3d_mass_function_boundaries.h"
#include "oot3d_mass_generated.h"
#include "recomp/a32_direct_runtime.h"
#endif
#if defined(OOT3D_NATIVE_GENERATED_TRUE_AOT)
#include "oot3d_a32_generated.h"
#endif

#include <algorithm>
#include <iterator>
#include <vector>

namespace Oot3dNativeGame {
namespace {

Oot3dMassAotStats gStats;

#if defined(OOT3D_NATIVE_GENERATED_MASS_AOT)
struct MassAotEntryCatalog {
    std::vector<uint32_t> Safe;
    std::vector<uint32_t> ExcludedBoundaries;
    Oot3dMassAotPcFilter ExcludedFilter;
};

Oot3dMassAotPcFilter MakePcFilter(
    std::span<const uint32_t> pcs, bool matchAllWhenEmpty) {
    Oot3dMassAotPcFilter result;
    if (pcs.empty()) {
        result.MatchAll = matchAllWhenEmpty;
        return result;
    }
    const auto [minimum, maximum] =
        std::minmax_element(pcs.begin(), pcs.end());
    result.BasePc = *minimum & ~3U;
    result.SlotCount =
        (static_cast<size_t>(*maximum - result.BasePc) >> 2U) + 1U;
    result.Words.assign((result.SlotCount + 63U) >> 6U, 0U);
    for (const uint32_t pc : pcs) {
        if (pc < result.BasePc || ((pc - result.BasePc) & 3U) != 0U) {
            continue;
        }
        const size_t slot = (pc - result.BasePc) >> 2U;
        result.Words[slot >> 6U] |= 1ULL << (slot & 63U);
    }
    return result;
}

oot3d::recomp::a32::direct::PcFilterView ToView(
    const Oot3dMassAotPcFilter& filter) noexcept {
    return {
        filter.BasePc,
        filter.SlotCount,
        filter.Words.data(),
        filter.MatchAll,
    };
}

const oot3d::recomp::mass_metadata::FunctionBoundary*
FindSemanticFunctionBoundary(uint32_t pc) {
    const auto& functions =
        oot3d::recomp::mass_metadata::kFunctionBoundaries;
    auto candidate = std::upper_bound(
        functions.begin(), functions.end(), pc,
        [](uint32_t value, const auto& function) {
            return value < function.entry;
        });
    while (candidate != functions.begin()) {
        --candidate;
        if (pc >= candidate->entry && pc < candidate->end) {
            return &*candidate;
        }
    }
    return nullptr;
}

const MassAotEntryCatalog& GetMassAotEntryCatalog() {
    static const MassAotEntryCatalog catalog = [] {
        MassAotEntryCatalog result;
        const auto& massRegistry = oot3d::recomp::GetA32MassDirectRegistry();
        size_t count = 0U;
        for (uint32_t index = 0U; index < massRegistry.shard_count; ++index) {
            count += massRegistry.shards[index].block_count;
        }
        std::vector<uint32_t> allEntries;
        allEntries.reserve(count);
        for (uint32_t shardIndex = 0U;
             shardIndex < massRegistry.shard_count; ++shardIndex) {
            const auto& shard = massRegistry.shards[shardIndex];
            for (uint32_t blockIndex = 0U;
                 blockIndex < shard.block_count; ++blockIndex) {
                allEntries.push_back(shard.blocks[blockIndex].pc);
            }
        }
        std::sort(allEntries.begin(), allEntries.end());
#if defined(OOT3D_NATIVE_GENERATED_TRUE_AOT)
        const auto& packedRegistry = oot3d::recomp::GetA32GeneratedRegistry();
        for (uint32_t shardIndex = 0U;
             shardIndex < packedRegistry.shard_count; ++shardIndex) {
            const auto& shard = packedRegistry.shards[shardIndex];
            for (uint32_t blockIndex = 0U;
                 blockIndex < shard.block_count; ++blockIndex) {
                const auto& packedBlock = shard.blocks[blockIndex];
                const auto* semanticFunction =
                    FindSemanticFunctionBoundary(packedBlock.pc);
                if (semanticFunction == nullptr ||
                    static_cast<uint64_t>(packedBlock.pc) +
                            static_cast<uint64_t>(packedBlock.op_count) * 4U <=
                        semanticFunction->end) {
                    continue;
                }
                const auto candidate =
                    std::upper_bound(allEntries.begin(), allEntries.end(),
                                     packedBlock.pc);
                if (candidate != allEntries.begin()) {
                    const uint32_t containingMassBlock = *(candidate - 1);
                    if (containingMassBlock >= semanticFunction->entry &&
                        containingMassBlock < semanticFunction->end) {
                        result.ExcludedBoundaries.push_back(
                            containingMassBlock);
                    }
                }
            }
        }
#endif
        std::sort(result.ExcludedBoundaries.begin(),
                  result.ExcludedBoundaries.end());
        result.ExcludedBoundaries.erase(
            std::unique(result.ExcludedBoundaries.begin(),
                        result.ExcludedBoundaries.end()),
            result.ExcludedBoundaries.end());
        const auto typedGameplayEntries =
            Oot3dTypedGameplayEntryPoints();
        result.ExcludedBoundaries.insert(
            result.ExcludedBoundaries.end(), typedGameplayEntries.begin(),
            typedGameplayEntries.end());
        std::sort(result.ExcludedBoundaries.begin(),
                  result.ExcludedBoundaries.end());
        result.ExcludedBoundaries.erase(
            std::unique(result.ExcludedBoundaries.begin(),
                        result.ExcludedBoundaries.end()),
            result.ExcludedBoundaries.end());
        result.Safe.reserve(allEntries.size());
        std::set_difference(
            allEntries.begin(), allEntries.end(),
            result.ExcludedBoundaries.begin(),
            result.ExcludedBoundaries.end(),
            std::back_inserter(result.Safe));
        result.ExcludedFilter =
            MakePcFilter(result.ExcludedBoundaries, false);
        return result;
    }();
    return catalog;
}
#endif

} // namespace

bool Oot3dMassAotAvailable() noexcept {
#if defined(OOT3D_NATIVE_GENERATED_MASS_AOT)
    return true;
#else
    return false;
#endif
}

std::span<const uint32_t> Oot3dMassAotBlockEntryPoints() noexcept {
#if defined(OOT3D_NATIVE_GENERATED_MASS_AOT)
    return GetMassAotEntryCatalog().Safe;
#else
    return {};
#endif
}

size_t Oot3dMassAotExcludedBoundaryCount() noexcept {
#if defined(OOT3D_NATIVE_GENERATED_MASS_AOT)
    return GetMassAotEntryCatalog().ExcludedBoundaries.size();
#else
    return 0U;
#endif
}

Oot3dMassAotPcFilter BuildOot3dMassAotPcFilter(
    std::span<const uint32_t> pcs, bool matchAllWhenEmpty) {
#if defined(OOT3D_NATIVE_GENERATED_MASS_AOT)
    return MakePcFilter(pcs, matchAllWhenEmpty);
#else
    static_cast<void>(pcs);
    Oot3dMassAotPcFilter result;
    result.MatchAll = matchAllWhenEmpty;
    return result;
#endif
}

void ResetOot3dMassAotStats() noexcept {
    gStats = {};
}

Oot3dMassAotStats GetOot3dMassAotStats() noexcept {
    return gStats;
}

bool ExecuteOot3dMassAot(
    uint32_t pc,
    oot3d::recomp::a32::GuestState& state,
    NativeA32Memory& memory,
    oot3d::recomp::a32::ExecutionResult* result,
    uint32_t blockBudget,
    uint32_t* blocksConsumed,
    oot3d::recomp::a32::BlockEntryCallback blockEntry,
    void* blockEntryUser,
    const Oot3dMassAotPcFilter& blockEntryFilter,
    const Oot3dMassAotPcFilter& observableExitFilter,
    bool skipFirstBlockEntry) {
#if defined(OOT3D_NATIVE_GENERATED_MASS_AOT)
    if (result == nullptr || blockBudget == 0U) {
        return false;
    }
    const auto& registry = oot3d::recomp::GetA32MassDirectRegistry();
    if (oot3d::recomp::a32::direct::FindBlock(registry, pc) == nullptr) {
        return false;
    }
    const auto& catalog = GetMassAotEntryCatalog();
    if (std::binary_search(catalog.ExcludedBoundaries.begin(),
                           catalog.ExcludedBoundaries.end(), pc)) {
        return false;
    }
    uint32_t consumed = 0U;
    *result = oot3d::recomp::a32::direct::DispatchObserved(
        registry, pc, state, memory, nullptr, nullptr, blockBudget,
        &consumed, blockEntry, blockEntryUser, ToView(blockEntryFilter),
        skipFirstBlockEntry, ToView(catalog.ExcludedFilter),
        ToView(observableExitFilter));
    if (blocksConsumed != nullptr) {
        *blocksConsumed = std::max(1U, consumed);
    }
    ++gStats.Calls;
    gStats.Blocks += consumed;
    switch (result->kind) {
    case oot3d::recomp::a32::ExitKind::Branch:
        ++gStats.RegionExits;
        break;
    case oot3d::recomp::a32::ExitKind::Svc:
        ++gStats.SvcExits;
        break;
    case oot3d::recomp::a32::ExitKind::BlockLimit:
        ++gStats.BlockLimitExits;
        break;
    case oot3d::recomp::a32::ExitKind::MemoryFault:
        ++gStats.MemoryFaults;
        break;
    case oot3d::recomp::a32::ExitKind::MissingBlock:
        ++gStats.MissingBlocks;
        break;
    case oot3d::recomp::a32::ExitKind::Fallback:
        ++gStats.FallbackExits;
        break;
    case oot3d::recomp::a32::ExitKind::Unsupported:
        ++gStats.UnsupportedExits;
        break;
    default:
        break;
    }
    return true;
#else
    static_cast<void>(pc);
    static_cast<void>(state);
    static_cast<void>(memory);
    static_cast<void>(result);
    static_cast<void>(blockBudget);
    static_cast<void>(blocksConsumed);
    static_cast<void>(blockEntry);
    static_cast<void>(blockEntryUser);
    static_cast<void>(blockEntryFilter);
    static_cast<void>(observableExitFilter);
    static_cast<void>(skipFirstBlockEntry);
    return false;
#endif
}

} // namespace Oot3dNativeGame
