#pragma once

#include "oot3d_source_overlay_abi.h"

#include <array>
#include <cstdint>

namespace Oot3dSourceOverlay::CsabCurves {

inline constexpr std::uint32_t kS16Entry = 0x003084E8U;
inline constexpr std::uint32_t kF32Entry = 0x003087A4U;

inline constexpr std::array<Oot3dSourceOverlayEntry, 2> kEntries{{
    {kS16Entry, 0U, "animation.csab", "Oot3d_EvalAnimationCurveS16"},
    {kF32Entry, 0U, "animation.csab", "Oot3d_EvalAnimationCurveF32"},
}};

struct Stats {
    std::uint64_t Calls = 0U;
    std::uint64_t Handled = 0U;
    std::uint64_t Rejected = 0U;
    std::uint64_t S16Calls = 0U;
    std::uint64_t F32Calls = 0U;
    std::uint64_t GuestBytesRead = 0U;
    std::uint64_t Fallbacks = 0U;
    std::uint64_t FpscrExceptionUpdates = 0U;
    std::uint64_t DifferentialMismatches = 0U;
};

int Execute(std::uint32_t entry, Oot3dSourceOverlayGuestState* state,
            Oot3dSourceOverlayExecutionResult* result,
            const Oot3dSourceOverlayHostApi* host);
Stats GetStats();

} // namespace Oot3dSourceOverlay::CsabCurves
