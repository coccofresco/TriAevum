#pragma once

#include "oot3d_source_overlay_abi.h"

#include <array>
#include <cstdint>

namespace Oot3dSourceOverlay::CutsceneFrame {

inline constexpr std::uint32_t kUpdateEntry = 0x00321F50U;

inline constexpr std::array<Oot3dSourceOverlayEntry, 1> kEntries{{
    {kUpdateEntry, 0U, "cutscene.frame", "Cutscene_UpdateFrame"},
}};

struct Stats {
    std::uint64_t OwnerCalls = 0U;
    std::uint64_t OwnerHandled = 0U;
    std::uint64_t NestedGuestCalls = 0U;
    std::uint64_t SourceDependencyCalls = 0U;
    std::uint64_t ClockQueryCalls = 0U;
    std::uint64_t ClockCommitCalls = 0U;
    std::uint64_t ProcessCommandCalls = 0U;
    std::uint64_t Failures = 0U;
};

int Execute(std::uint32_t entry, Oot3dSourceOverlayGuestState* state,
            Oot3dSourceOverlayExecutionResult* result,
            const Oot3dSourceOverlayHostApi* host,
            std::uint32_t blockBudget);
Stats GetStats();

} // namespace Oot3dSourceOverlay::CutsceneFrame
