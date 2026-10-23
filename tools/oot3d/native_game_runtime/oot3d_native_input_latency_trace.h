#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Oot3dNativeGame {

// Host-side input age of each presented host frame, on the steady clock: the
// SDL event pump that refreshed the physical input state and the input poll
// (main thread), then the pacing wait before the frame end and the return of
// the frame end, which submits and presents (presentation thread). The return
// of the present call is not scanout: display and panel terms are outside, and
// so is the interpolation delay (the alpha is recorded for it).
struct NativeInputLatencySample {
    uint64_t Frame = 0;
    double PumpToPollMilliseconds = 0.0;
    // The renderer frame begin (frame slot fence and swapchain image
    // acquisition, where FIFO presentation blocks) and when it ended.
    double BeginMilliseconds = 0.0;
    double PumpToBeginEndMilliseconds = 0.0;
    // From the pump to the start of the pacing wait: the frame's own work and
    // the time it waited for the presentation thread.
    double PumpToWaitMilliseconds = 0.0;
    double WaitMilliseconds = 0.0;
    double FrameEndMilliseconds = 0.0;
    double PumpToPresentMilliseconds = 0.0;
    float InterpolationAlpha = 0.0F;
    // Pumped while an earlier frame had not ended: the frame's input was
    // sampled during the previous frame's wait or work (parallel presentation).
    bool OverlappedPreviousFrame = false;
};

// TRIAEVUM_INPUT_LATENCY=1 enables it; TRIAEVUM_INPUT_LATENCY_CSV=<path> also
// writes every sample. Samples stay in memory until the summary.
class NativeInputLatencyTrace final {
  public:
    using Clock = std::chrono::steady_clock;

    [[nodiscard]] static bool EnabledByEnvironment();

    explicit NativeInputLatencyTrace(bool enabled) noexcept : mEnabled(enabled) {}

    [[nodiscard]] bool Enabled() const noexcept { return mEnabled; }
    // Main thread, in frame order.
    void ObserveInput(uint64_t frame, Clock::time_point pump, Clock::time_point poll, float interpolationAlpha);
    // Thread of the renderer frame begin; after ObserveInput of that frame.
    void ObserveBegin(uint64_t frame, Clock::time_point start, Clock::time_point end);
    // Presentation thread. A frame without input (skipped) is ignored.
    void ObserveFrameEnd(uint64_t frame, Clock::time_point waitStart, Clock::time_point waitEnd,
                         Clock::time_point presented);
    // Drops the pending input of frames that will never end (an inactive
    // renderer frame).
    void DiscardPending(uint64_t frame);

    [[nodiscard]] std::vector<NativeInputLatencySample> Samples() const;
    // "TRIAEVUM_INPUT_LATENCY mode=<mode> frames=..." over the samples after
    // the first skipFrames frames.
    [[nodiscard]] std::string Summary(std::string_view mode, uint64_t skipFrames) const;
    [[nodiscard]] bool WriteCsv(const std::string& path) const;

  private:
    struct Pending {
        Clock::time_point Pump;
        Clock::time_point Poll;
        Clock::time_point BeginStart;
        Clock::time_point BeginEnd;
        float Alpha = 0.0F;
        bool Overlapped = false;
    };

    bool mEnabled = false;
    mutable std::mutex mMutex;
    std::unordered_map<uint64_t, Pending> mPending;
    std::vector<NativeInputLatencySample> mSamples;
};

} // namespace Oot3dNativeGame
