#pragma once

#include <cstdint>
#include <string_view>

namespace Oot3dNativeGame {

// Input admission makes each host frame wait until the previous one has been
// presented before it samples input (docs/TRIAEVUM_PARALLEL_PRESENTATION.md).
// It removes one presentation of input latency, but it serializes the guest
// refresh with the previous frame's presentation. A host that cannot fit the
// guest refresh and its presentation in one presentation period then misses
// every refresh-carrying deadline (interpolated 60 Hz on a phone).
enum class NativeInputAdmissionMode : uint8_t {
    On,   // always admit (desktop default)
    Off,  // never admit: guest and presentation overlap
    Auto, // admit while serialized refresh frames meet their deadlines
};

[[nodiscard]] bool ParseNativeInputAdmissionMode(
    std::string_view value, NativeInputAdmissionMode* mode) noexcept;
[[nodiscard]] const char* NativeInputAdmissionModeName(
    NativeInputAdmissionMode mode) noexcept;

struct NativeInputAdmissionStats {
    uint64_t AdmittedFrames = 0;
    uint64_t OverlappedFrames = 0;
    uint64_t ObservedRefreshFrames = 0;
    uint64_t LateRefreshFrames = 0;
    uint64_t OverlapEntries = 0;
    uint64_t Probes = 0;
    uint64_t SuccessfulProbes = 0;
};

// Auto policy. Only refresh-carrying frames that were admitted and paced are
// observed. When at least LateThreshold of a Window of them miss their
// presentation deadline, admission is suspended for a hold measured in
// presentations; it is then probed on ProbeWindow refresh frames, failing at
// ProbeLateThreshold late ones. A failed probe doubles the hold up to
// MaximumHoldPresentations. A passed probe restores the initial hold only
// after ConfirmRefreshFrames more admitted refresh frames stay in admission.
struct NativeInputAdmissionTuning {
    uint32_t Window = 30;
    uint32_t LateThreshold = 8;
    uint32_t InitialHoldPresentations = 600;
    uint32_t MaximumHoldPresentations = 7200;
    uint32_t ProbeWindow = 4;
    uint32_t ProbeLateThreshold = 2;
    uint32_t ConfirmRefreshFrames = 120;
};

class NativeInputAdmissionController final {
  public:
    explicit NativeInputAdmissionController(
        NativeInputAdmissionMode mode = NativeInputAdmissionMode::On,
        NativeInputAdmissionTuning tuning = {}) noexcept;

    // Called once per host frame, before the frame decides whether to wait.
    // The counts are the refresh frames observed (admitted, paced) since the
    // previous call and how many of them were late.
    void ObserveRefreshFrames(uint64_t observed, uint64_t late) noexcept;
    // The decision for the host frame about to start; counts it.
    [[nodiscard]] bool BeginFrame() noexcept;

    [[nodiscard]] NativeInputAdmissionMode Mode() const noexcept { return mMode; }
    [[nodiscard]] bool Admitting() const noexcept;
    [[nodiscard]] uint32_t HoldPresentations() const noexcept { return mHold; }
    [[nodiscard]] const NativeInputAdmissionStats& Stats() const noexcept { return mStats; }

  private:
    void ObserveRefreshFrame(bool late) noexcept;

    NativeInputAdmissionMode mMode;
    NativeInputAdmissionTuning mTuning;
    bool mOverlapping = false;
    bool mProbing = false;
    uint32_t mConfirmRemaining = 0;
    uint32_t mWindowObserved = 0;
    uint32_t mWindowLate = 0;
    uint32_t mHold = 0;
    uint32_t mHoldRemaining = 0;
    NativeInputAdmissionStats mStats;
};

// Without admission a refresh frame samples input as soon as the previous
// frame was queued, one presentation earlier than it needs to. This controller
// delays that sample while overlapped refresh frames keep meeting their
// deadline: additive increase per clean window, multiplicative decrease when
// late frames appear. The delay is bounded by one presentation period.
struct NativeInputSampleDelayTuning {
    uint32_t Window = 30;            // overlapped refresh frames per decision
    uint32_t TolerableLate = 1;      // late frames per window that hold the delay
    uint32_t IncreaseMicroseconds = 500;
    uint32_t DecreasePercent = 50;   // kept share of the delay after a bad window
};

struct NativeInputSampleDelayStats {
    uint64_t ObservedRefreshFrames = 0;
    uint64_t LateRefreshFrames = 0;
    uint64_t Increases = 0;
    uint64_t Decreases = 0;
    uint32_t MaximumMicroseconds = 0;
};

class NativeInputSampleDelayController final {
  public:
    explicit NativeInputSampleDelayController(
        NativeInputSampleDelayTuning tuning = {}) noexcept;

    // The upper bound follows the presentation period; zero disables delays.
    void Configure(uint32_t maximumMicroseconds) noexcept;
    void ObserveRefreshFrames(uint64_t observed, uint64_t late) noexcept;
    // Admission resumed or the cadence changed: start again from no delay.
    void Reset() noexcept;

    [[nodiscard]] uint32_t DelayMicroseconds() const noexcept { return mDelay; }
    [[nodiscard]] uint32_t MaximumMicroseconds() const noexcept { return mMaximum; }
    [[nodiscard]] const NativeInputSampleDelayStats& Stats() const noexcept { return mStats; }

  private:
    NativeInputSampleDelayTuning mTuning;
    uint32_t mMaximum = 0;
    uint32_t mDelay = 0;
    uint32_t mWindowObserved = 0;
    uint32_t mWindowLate = 0;
    NativeInputSampleDelayStats mStats;
};

} // namespace Oot3dNativeGame
