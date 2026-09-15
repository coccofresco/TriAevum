#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Fast::Renderer {
// Fixed-storage attribution for the worst measured frames. No frame-time I/O.
template<size_t Phases, size_t Capacity = 16> class SlowFrameSamples {
  public:
    struct Sample {
        uint64_t Frame;
        double Milliseconds;
        std::array<double, Phases> PhaseMilliseconds;
    };
    void Record(uint64_t frame, double milliseconds, const std::array<double, Phases>& phases) {
        if (!std::isfinite(milliseconds) || milliseconds < 0) return;
        size_t index = 0;
        while (index < mCount && mSamples[index].Milliseconds >= milliseconds) ++index;
        if (index == Capacity) return;
        if (mCount < Capacity) ++mCount;
        for (size_t i = mCount - 1; i > index; --i) mSamples[i] = mSamples[i - 1];
        mSamples[index] = {frame, milliseconds, phases};
    }
    std::span<const Sample> Samples() const { return {mSamples.data(), mCount}; }
  private:
    static_assert(Capacity > 0);
    std::array<Sample, Capacity> mSamples{};
    size_t mCount = 0;
};
}
