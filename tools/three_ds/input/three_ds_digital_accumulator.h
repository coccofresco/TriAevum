#pragma once

#include <cstdint>

namespace ThreeDsRecomp::Input {

struct DigitalInputSample {
    uint32_t Held = 0;
    uint32_t Pressed = 0;
};

// Polling and simulation can run at different rates. A simulation owner takes
// one snapshot and keeps it stable for all consumers of that update. Multiple
// presses before an update coalesce as in a sampled device, not an action queue.
class DigitalInputAccumulator {
  public:
    void Observe(uint32_t held) noexcept {
        PendingPressed |= held & ~Held;
        Held = held;
    }
    DigitalInputSample Consume() noexcept {
        const DigitalInputSample result{Held, PendingPressed};
        PendingPressed = 0;
        return result;
    }
    void Reset() noexcept { Held = PendingPressed = 0; }

  private:
    uint32_t Held = 0;
    uint32_t PendingPressed = 0;
};

} // namespace ThreeDsRecomp::Input
