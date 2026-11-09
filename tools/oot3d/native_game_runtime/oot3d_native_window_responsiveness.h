#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>

namespace Oot3dNativeGame {

// The window's thread must take its messages at least every few seconds or
// the OS marks the window "Not Responding" (Windows: 5 s; the run watchdog
// then reads it as a stall). The session records each time it pumps the
// window's events (the frame's own pump and the pumps inside its waits for
// the renderer) and reports the longest gap: during a long cold renderer
// preparation it stays far below that limit.
class NativeWindowResponsiveness {
  public:
    using Clock = std::chrono::steady_clock;

    explicit NativeWindowResponsiveness(Clock::time_point start) noexcept : mLast(start) {}

    void Pumped(Clock::time_point now) noexcept {
        const double gap = std::chrono::duration<double>(now - mLast).count();
        if (gap > mLongestGapSeconds) mLongestGapSeconds = gap;
        mLast = now;
        ++mPumps;
    }

    [[nodiscard]] double LongestGapSeconds() const noexcept { return mLongestGapSeconds; }
    [[nodiscard]] uint64_t Pumps() const noexcept { return mPumps; }

  private:
    Clock::time_point mLast;
    double mLongestGapSeconds = 0.0;
    uint64_t mPumps = 0;
};

// Waits until `until`, calling `pump` after every `slice` of sleep: the
// preparation screen's wait between two of its frames.
template <typename Pump>
void PumpedSleepUntil(std::chrono::steady_clock::time_point until, std::chrono::steady_clock::duration slice,
                      Pump&& pump) {
    for (;;) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= until) break;
        std::this_thread::sleep_for(std::min<std::chrono::steady_clock::duration>(slice, until - now));
        pump();
    }
}

// A rate-limited progress line while the guest waits for the preparation, so
// a long cold preparation leaves a visible heartbeat in the log.
class NativePreparationHeartbeat {
  public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::seconds kInterval{5};

    explicit NativePreparationHeartbeat(Clock::time_point start) noexcept : mNext(start + kInterval) {}

    [[nodiscard]] bool Due(Clock::time_point now) noexcept {
        if (now < mNext) return false;
        mNext = now + kInterval;
        return true;
    }

  private:
    Clock::time_point mNext;
};

} // namespace Oot3dNativeGame
