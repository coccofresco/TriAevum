#pragma once

#include "recomp/a32_runtime.h"

#include <cstdint>
#include <span>

namespace Oot3dNativeGame {

class NativeA32Memory;

struct Oot3dSourceOverlayStats {
    uint64_t Calls = 0;
    uint64_t HandledCalls = 0;
    uint64_t FallbackCalls = 0;
    uint64_t InvalidResults = 0;
    uint64_t GuestCalls = 0;
};

bool Oot3dSourceOverlayAvailable() noexcept;
std::span<const uint32_t> Oot3dSourceOverlayEntryPoints() noexcept;
bool Oot3dSourceOverlayEntryActive(uint32_t entry) noexcept;
bool ExecuteOot3dSourceOverlay(
    uint32_t entry,
    oot3d::recomp::a32::GuestState& state,
    NativeA32Memory& memory,
    oot3d::recomp::a32::ExecutionResult* result,
    uint32_t blockBudget = 1'000'000U,
    uint32_t* blocksConsumed = nullptr);
void ResetOot3dSourceOverlayStats() noexcept;
Oot3dSourceOverlayStats GetOot3dSourceOverlayStats() noexcept;

} // namespace Oot3dNativeGame

