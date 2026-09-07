#pragma once

#include <cstdint>
#include <mutex>

namespace Fast::Oot3d {

struct VisualClockState {
    double Seconds = 0.0;
    float DeltaSeconds = 0.0F;
    uint64_t Sequence = 0;
    uint64_t Epoch = 0;
    bool Paused = false;
    bool Available = false;
};

// Renderer-facing clock fed by the host runtime. Effects consume this clock
// instead of presentation frame counters, so their apparent speed is stable at
// 30, 60 and uncapped presentation rates.
class VisualClock final {
  public:
    static VisualClock& Instance();

    void Publish(double deltaSeconds, bool paused, uint64_t sequence);
    [[nodiscard]] VisualClockState Snapshot() const;
    void Reset();

  private:
    mutable std::mutex mMutex;
    VisualClockState mState;
    bool mHasSequence = false;
};

} // namespace Fast::Oot3d
