#pragma once

#include "oot3d_source_overlay_abi.h"

#include <array>
#include <cstdint>

namespace Oot3dSourceOverlay::CutsceneCommands {

inline constexpr std::uint32_t kProcessEntry = 0x002C5BA0U;

inline constexpr std::array<Oot3dSourceOverlayEntry, 1> kEntries{{
    {kProcessEntry, 0U, "cutscene.commands", "Cutscene_ProcessCommands"},
}};

struct Stats {
    std::uint64_t OwnerCalls = 0U;
    std::uint64_t OwnerHandled = 0U;
    std::uint64_t NestedGuestCalls = 0U;
    std::uint64_t DirectDependencyCalls = 0U;
    std::uint64_t DynamicCallbackCalls = 0U;
    std::uint64_t GuestReadCalls = 0U;
    std::uint64_t GuestWriteCalls = 0U;
    std::uint64_t ScratchCalls = 0U;
    std::uint64_t HardFloatCalls = 0U;
    std::uint64_t ConversionOperations = 0U;
    std::uint64_t Failures = 0U;
};

int Execute(std::uint32_t entry, Oot3dSourceOverlayGuestState* state,
            Oot3dSourceOverlayExecutionResult* result,
            const Oot3dSourceOverlayHostApi* host,
            std::uint32_t blockBudget);
Stats GetStats();

} // namespace Oot3dSourceOverlay::CutsceneCommands
