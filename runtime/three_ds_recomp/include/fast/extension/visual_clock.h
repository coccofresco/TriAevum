#pragma once

#include <cstdint>
#include <mutex>

namespace Fast::Extension {

struct VisualClockState {
    double Seconds = 0.0;
    float DeltaSeconds = 0.0F;
    uint64_t Sequence = 0;
    uint64_t Epoch = 0;
    bool Paused = false;
    bool Available = false;
    // The host holds the game (the in-game menu is open): effects that keep
    // their own time when no clock is published (the grass wind and
    // interaction) stand still too, so the held frame stays the held frame.
    bool HostHold = false;
};

// Renderer-facing clock fed by the host runtime. Effects consume this clock
// instead of presentation frame counters, so their apparent speed is stable at
// 30, 60 and uncapped presentation rates.
class VisualClock final {
  public:
    static VisualClock& Instance();

    void Publish(double deltaSeconds, bool paused, uint64_t sequence);
    // Set by the window host in the render order of each presented frame.
    void SetHostHold(bool held);
    [[nodiscard]] VisualClockState Snapshot() const;
    void Reset();

  private:
    mutable std::mutex mMutex;
    VisualClockState mState;
    bool mHasSequence = false;
};

} // namespace Fast::Extension
