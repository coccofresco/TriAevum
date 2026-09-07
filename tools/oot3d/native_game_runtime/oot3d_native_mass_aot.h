#pragma once

#include "recomp/a32_runtime.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Oot3dNativeGame {

class NativeA32Memory;

struct Oot3dMassAotStats {
    uint64_t Calls = 0;
    uint64_t Blocks = 0;
    uint64_t RegionExits = 0;
    uint64_t SvcExits = 0;
    uint64_t BlockLimitExits = 0;
    uint64_t MemoryFaults = 0;
    uint64_t MissingBlocks = 0;
    uint64_t FallbackExits = 0;
    uint64_t UnsupportedExits = 0;
};

struct Oot3dMassAotPcFilter {
    uint32_t BasePc = 0;
    size_t SlotCount = 0;
    std::vector<uint64_t> Words;
    bool MatchAll = false;
};

bool Oot3dMassAotAvailable() noexcept;
std::span<const uint32_t> Oot3dMassAotBlockEntryPoints() noexcept;
size_t Oot3dMassAotExcludedBoundaryCount() noexcept;
Oot3dMassAotPcFilter BuildOot3dMassAotPcFilter(
    std::span<const uint32_t> pcs, bool matchAllWhenEmpty);
void ResetOot3dMassAotStats() noexcept;
Oot3dMassAotStats GetOot3dMassAotStats() noexcept;

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
    bool skipFirstBlockEntry);

} // namespace Oot3dNativeGame
