#pragma once

#include "recomp/a32_runtime.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Oot3dNativeGame {

class NativeA32Memory;

struct Oot3dCompiledFunctionStats {
    uint64_t Calls = 0;
    uint64_t BytesProcessed = 0;
    uint64_t RetainedArmFallbacks = 0;
    uint64_t MemcpyOverlapFallbacks = 0;
    uint64_t MemcpyOverlapBytes = 0;
    uint64_t MemcpyDestinationAfterSourceFallbacks = 0;
    uint64_t MemcpyDestinationBeforeSourceFallbacks = 0;
    uint64_t Mtx3x4FastCalls = 0;
    uint64_t Mtx3x4SoftCalls = 0;
    uint64_t Mtx3x4ValidationCalls = 0;
    uint64_t Mtx3x4ValueMismatches = 0;
    uint64_t Mtx3x4FlagMismatches = 0;
    uint64_t WholeAotCalls = 0;
    uint64_t WholeAotDirectCalls = 0;
    uint64_t WholeAotIndirectCalls = 0;
    uint64_t WholeAotResolvedIndirectCalls = 0;
    uint64_t WholeAotExternalCalls = 0;
    uint64_t WholeAotSvcExits = 0;
    uint64_t WholeAotBlockLimitExits = 0;
    uint64_t WholeAotMemoryFaults = 0;
    uint64_t WholeAotUnsupportedExits = 0;
};

struct Oot3dWholeAotExternalTarget {
    uint32_t Entry = 0;
    uint64_t Calls = 0;
    uint64_t ManualCompiledCalls = 0;
    uint64_t TimingSamples = 0;
    uint64_t TimingSampleNanoseconds = 0;
};

bool Oot3dWholeAotAvailable() noexcept;
std::span<const uint32_t> Oot3dCompiledFunctionEntryPoints(
    bool includeWholeAot = true,
    bool includeManualCompiledFunctions = true) noexcept;
void ResetOot3dCompiledFunctionStats() noexcept;
Oot3dCompiledFunctionStats GetOot3dCompiledFunctionStats() noexcept;
void SetOot3dCompiledFunctionProfilingEnabled(bool enabled);
std::vector<Oot3dWholeAotExternalTarget>
GetOot3dWholeAotExternalTargets(size_t maximumTargets = 64U);

bool ExecuteOot3dCompiledFunction(
    uint32_t pc,
    oot3d::recomp::a32::GuestState& state,
    NativeA32Memory& memory,
    oot3d::recomp::a32::ExecutionResult* result,
    uint32_t blockBudget = 1'000'000U,
    uint32_t* blocksConsumed = nullptr,
    oot3d::recomp::a32::BlockEntryCallback blockEntry = nullptr,
    void* blockEntryUser = nullptr,
    const uint32_t* blockEntryPcs = nullptr,
    size_t blockEntryPcCount = 0U,
    bool skipFirstBlockEntry = false,
    bool enableManualCompiledFunctions = true,
    bool enableWholeAot = true,
    uint32_t stopPc = 0U);

bool ExecuteOot3dCompiledFunction(
    uint32_t pc,
    oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory,
    oot3d::recomp::a32::ExecutionResult* result,
    void* user);

} // namespace Oot3dNativeGame
