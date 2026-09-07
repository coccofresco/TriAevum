#pragma once

#include <chrono>
#include <cstdint>

namespace Oot3dNativeGame {

struct NativeRealtimeRefreshPacerStats {
    uint64_t Waits = 0;
    uint64_t DeadlineMisses = 0;
    uint64_t CarriedDeadlineDebt = 0;
    uint64_t DeadlineResyncs = 0;
    uint64_t PresentationIntervals = 0;
    double SleepSeconds = 0.0;
    double SpinSeconds = 0.0;
    double TotalIntervalSeconds = 0.0;
    double MinimumIntervalSeconds = 0.0;
    double MaximumIntervalSeconds = 0.0;
    double MaximumLatenessSeconds = 0.0;
    double MaximumIntervalErrorSeconds = 0.0;
    double SquaredIntervalErrorSeconds = 0.0;
};

enum class NativePacerDeadlineAction : uint8_t {
    Wait,
    CarryDebt,
    Resync,
};

// A short overrun is retained so the next inexpensive interpolation sample
// can absorb it. Only a sustained stall rebases the absolute clock.
NativePacerDeadlineAction ResolveNativePacerDeadlineAction(
    std::chrono::nanoseconds lateness,
    std::chrono::nanoseconds presentationPeriod,
    uint32_t maximumRecoverablePeriods = 2U) noexcept;

// One absolute presentation clock owns software pacing. Coarse sleep performs
// most of the wait and a short high-precision tail avoids Windows scheduler
// quantization. Short deadline debt is preserved across the fixed x2/x3
// cadence; long stalls rebase instead of issuing unbounded catch-up frames.
class NativeRealtimeRefreshPacer final {
  public:
    NativeRealtimeRefreshPacer(bool pacingAllowed, uint32_t targetRateHz);
    ~NativeRealtimeRefreshPacer();

    NativeRealtimeRefreshPacer(const NativeRealtimeRefreshPacer&) = delete;
    NativeRealtimeRefreshPacer& operator=(
        const NativeRealtimeRefreshPacer&) = delete;

    void Configure(bool enabled, uint32_t targetRateHz) noexcept;
    void WaitForNextRefresh();
    void ObservePresentation(
        std::chrono::steady_clock::time_point presentationTime) noexcept;
    void ResetDeadline() noexcept;

    [[nodiscard]] bool Enabled() const noexcept;
    [[nodiscard]] bool HighResolutionWaitAvailable() const noexcept;
    [[nodiscard]] uint32_t TargetRateHz() const noexcept;
    [[nodiscard]] double ElapsedSeconds() const noexcept;
    [[nodiscard]] double MeanIntervalSeconds() const noexcept;
    [[nodiscard]] double RmsIntervalErrorSeconds() const noexcept;
    [[nodiscard]] const NativeRealtimeRefreshPacerStats& Stats() const noexcept;

  private:
    std::chrono::nanoseconds NextPeriod() noexcept;
    void SleepUntil(
        std::chrono::steady_clock::time_point deadline) noexcept;

    bool mPacingAllowed = false;
    bool mEnabled = false;
    uint32_t mTargetRateHz = 0;
    bool mStarted = false;
    bool mHasPresentationTime = false;
    uint64_t mNanosecondRemainder = 0;
    std::chrono::steady_clock::time_point mStart;
    std::chrono::steady_clock::time_point mNextDeadline;
    std::chrono::steady_clock::time_point mLastPresentationTime;
    NativeRealtimeRefreshPacerStats mStats;
#ifdef _WIN32
    void* mHighResolutionTimer = nullptr;
#endif
};

} // namespace Oot3dNativeGame
