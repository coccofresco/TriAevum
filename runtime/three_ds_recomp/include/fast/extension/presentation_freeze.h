#pragma once

#include <cstdint>
#include <cstdlib>

#include "fast/extension/visual_clock.h"

namespace Fast::Extension {

// Diagnostic, default off: TRIAEVUM_FREEZE_FRAME=N stops the guest once it has
// run N refreshes (no further guest frame, input, audio or game time) and the
// host keeps presenting the last guest frame through the held-game path (the
// in-game menu's hold), so two runs or two variants render the same scene
// state while temporal effects converge. Renderer clocks that read the wall
// clock (water time) read a fixed time while frozen.
[[nodiscard]] inline uint64_t PresentationFreezeFrame() noexcept {
    static const uint64_t frame = [] {
        const char* value = std::getenv("TRIAEVUM_FREEZE_FRAME");
        return value != nullptr ? std::strtoull(value, nullptr, 10) : 0ULL;
    }();
    return frame;
}

// The water clock while frozen: N guest refreshes at 30 Hz.
[[nodiscard]] inline float FreezeAwareWaterSeconds(float live) noexcept {
    if (PresentationFreezeFrame() == 0U || !VisualClock::Instance().Snapshot().HostHold) return live;
    return static_cast<float>(PresentationFreezeFrame()) / 30.0F;
}

} // namespace Fast::Extension
