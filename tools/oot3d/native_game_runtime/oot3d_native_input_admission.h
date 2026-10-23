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
// presentations; it is then probed again. A failed probe doubles the hold up
// to MaximumHoldPresentations, a passed probe restores the initial hold.
struct NativeInputAdmissionTuning {
    uint32_t Window = 30;
    uint32_t LateThreshold = 8;
    uint32_t InitialHoldPresentations = 600;
    uint32_t MaximumHoldPresentations = 7200;
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
    NativeInputAdmissionMode mMode;
    NativeInputAdmissionTuning mTuning;
    bool mOverlapping = false;
    bool mProbing = false;
    uint32_t mWindowObserved = 0;
    uint32_t mWindowLate = 0;
    uint32_t mHold = 0;
    uint32_t mHoldRemaining = 0;
    NativeInputAdmissionStats mStats;
};

} // namespace Oot3dNativeGame
