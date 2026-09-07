#pragma once

#include "recomp/a32_runtime.h"

#include <cstdint>
#include <span>

namespace Oot3dNativeGame {

struct Oot3dTrueAotBlockStats {
    uint64_t Calls = 0;
    uint64_t Iterations = 0;
    uint64_t MemoryFaults = 0;
};

std::span<const uint32_t> Oot3dTrueAotBlockEntryPoints() noexcept;
void ResetOot3dTrueAotBlockStats() noexcept;
Oot3dTrueAotBlockStats GetOot3dTrueAotBlockStats() noexcept;

bool ExecuteOot3dTrueAotBlock(
    uint32_t pc,
    oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory,
    oot3d::recomp::a32::ExecutionResult* result,
    void* user);

} // namespace Oot3dNativeGame
