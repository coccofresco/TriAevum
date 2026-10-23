#include "oot3d_native_input_admission.h"

#include <stdexcept>

namespace {

using Oot3dNativeGame::NativeInputAdmissionController;
using Oot3dNativeGame::NativeInputAdmissionMode;
using Oot3dNativeGame::NativeInputAdmissionTuning;

void Expect(bool condition, const char* role) {
    if (!condition) throw std::runtime_error(role);
}

// Feeds one refresh-carrying frame result per call, like the frame loop does
// one frame later.
void Frames(NativeInputAdmissionController& controller, uint32_t count,
            bool late) {
    for (uint32_t index = 0; index < count; ++index) {
        controller.ObserveRefreshFrames(1U, late ? 1U : 0U);
        (void)controller.BeginFrame();
    }
}

void TestFixedModes() {
    NativeInputAdmissionController on(NativeInputAdmissionMode::On);
    NativeInputAdmissionController off(NativeInputAdmissionMode::Off);
    for (int index = 0; index < 100; ++index) {
        on.ObserveRefreshFrames(1U, 1U);
        off.ObserveRefreshFrames(1U, 0U);
        Expect(on.BeginFrame(), "on mode stopped admitting");
        Expect(!off.BeginFrame(), "off mode admitted a frame");
    }
    Expect(on.Stats().AdmittedFrames == 100U && on.Stats().OverlapEntries == 0U,
           "on mode counted an overlap");
    Expect(off.Stats().OverlappedFrames == 100U, "off mode frame count");
    Expect(on.Stats().LateRefreshFrames == 100U, "late frames not counted");
}

void TestAutoKeepsAdmissionWhenFramesFit() {
    NativeInputAdmissionController controller(NativeInputAdmissionMode::Auto);
    // An occasional late frame (below the threshold per window) is tolerated.
    for (int window = 0; window < 20; ++window) {
        Frames(controller, 3U, true);
        Frames(controller, 27U, false);
    }
    Expect(controller.Admitting(), "auto left admission for sparse misses");
    Expect(controller.Stats().OverlapEntries == 0U, "sparse misses entered overlap");
}

void TestAutoOverlapsHoldsAndProbes() {
    NativeInputAdmissionTuning tuning;
    tuning.Window = 10U;
    tuning.LateThreshold = 3U;
    tuning.InitialHoldPresentations = 20U;
    tuning.MaximumHoldPresentations = 50U;
    NativeInputAdmissionController controller(NativeInputAdmissionMode::Auto, tuning);
    Frames(controller, 2U, true);
    Expect(controller.Admitting(), "auto overlapped before the threshold");
    controller.ObserveRefreshFrames(1U, 1U);
    Expect(!controller.Admitting(), "auto did not overlap at the threshold");
    Expect(controller.Stats().OverlapEntries == 1U, "overlap entry not counted");
    // During the hold, results of earlier admitted frames do not re-enter it.
    uint32_t overlapped = 0;
    while (!controller.BeginFrame()) {
        controller.ObserveRefreshFrames(1U, 1U);
        ++overlapped;
        Expect(overlapped <= 20U, "hold did not end");
    }
    Expect(overlapped == 19U, "hold length");
    Expect(controller.Stats().Probes == 1U, "probe not counted");
    // A probe fails at its second late refresh frame and doubles the hold.
    Frames(controller, 1U, true);
    Expect(controller.Admitting(), "probe failed at its first late frame");
    Frames(controller, 1U, true);
    Expect(!controller.Admitting(), "failed probe kept admitting");
    Expect(controller.HoldPresentations() == 40U, "failed probe did not double the hold");
    while (!controller.BeginFrame()) {}
    Frames(controller, 2U, true);
    Expect(controller.HoldPresentations() == 50U, "hold exceeded its maximum");
    while (!controller.BeginFrame()) {}
    // A passing probe keeps admission but not the initial hold yet.
    Frames(controller, 4U, false);
    Expect(controller.Admitting(), "passing probe left admission");
    Expect(controller.Stats().SuccessfulProbes == 1U, "successful probe not counted");
    Expect(controller.HoldPresentations() == 50U, "hold restored before confirmation");
    // Leaving admission during the confirmation counts as a failed probe.
    Frames(controller, 3U, true);
    Expect(!controller.Admitting(), "window failure after a probe kept admitting");
    Expect(controller.HoldPresentations() == 50U, "lucky probe reset the hold");
    while (!controller.BeginFrame()) {}
    Frames(controller, 4U, false);
    Frames(controller, 120U, false);
    Expect(controller.Admitting(), "confirmed admission left");
    Expect(controller.HoldPresentations() == 20U, "confirmed probe kept the long hold");
}

void TestLuckyProbeBacksOff() {
    NativeInputAdmissionTuning tuning;
    tuning.Window = 10U;
    tuning.LateThreshold = 3U;
    tuning.InitialHoldPresentations = 10U;
    tuning.MaximumHoldPresentations = 1000U;
    tuning.ConfirmRefreshFrames = 12U;
    NativeInputAdmissionController controller(NativeInputAdmissionMode::Auto, tuning);
    Frames(controller, 3U, true);
    while (!controller.BeginFrame()) {}
    Frames(controller, 4U, false);  // passes
    Frames(controller, 3U, true);   // but fails within the confirmation
    Expect(controller.HoldPresentations() == 20U, "lucky probe did not double the hold");
    while (!controller.BeginFrame()) {}
    Frames(controller, 4U, false);
    Frames(controller, 12U, false);
    Expect(controller.HoldPresentations() == 10U, "confirmation did not restore the hold");
    Expect(controller.Stats().Probes == 2U && controller.Stats().SuccessfulProbes == 2U,
           "probe counts");
}

void TestParse() {
    NativeInputAdmissionMode mode{};
    Expect(Oot3dNativeGame::ParseNativeInputAdmissionMode("auto", &mode) &&
               mode == NativeInputAdmissionMode::Auto,
           "auto not parsed");
    Expect(Oot3dNativeGame::ParseNativeInputAdmissionMode("off", &mode) &&
               mode == NativeInputAdmissionMode::Off,
           "off not parsed");
    Expect(!Oot3dNativeGame::ParseNativeInputAdmissionMode("1", &mode),
           "numeric admission mode accepted");
}

void TestSampleDelayGrowsShrinksAndIsBounded() {
    Oot3dNativeGame::NativeInputSampleDelayTuning tuning;
    tuning.Window = 10U;
    tuning.TolerableLate = 1U;
    tuning.IncreaseMicroseconds = 1000U;
    tuning.DecreasePercent = 50U;
    Oot3dNativeGame::NativeInputSampleDelayController delay(tuning);
    delay.ObserveRefreshFrames(100U, 0U);
    Expect(delay.DelayMicroseconds() == 0U, "unconfigured delay grew");
    delay.Configure(3500U);
    delay.ObserveRefreshFrames(10U, 0U);
    Expect(delay.DelayMicroseconds() == 1000U, "clean window did not grow the delay");
    delay.ObserveRefreshFrames(9U, 0U);
    delay.ObserveRefreshFrames(1U, 1U);
    Expect(delay.DelayMicroseconds() == 1000U, "a tolerable late frame changed the delay");
    delay.ObserveRefreshFrames(50U, 0U);
    Expect(delay.DelayMicroseconds() == 3500U, "delay exceeded its bound");
    delay.ObserveRefreshFrames(2U, 2U);
    Expect(delay.DelayMicroseconds() == 1750U, "late frames did not halve the delay");
    Expect(delay.Stats().Decreases == 1U && delay.Stats().MaximumMicroseconds == 3500U,
           "delay statistics");
    delay.Configure(1000U);
    Expect(delay.DelayMicroseconds() == 1000U, "a lower bound did not clamp the delay");
    delay.Reset();
    Expect(delay.DelayMicroseconds() == 0U, "reset kept the delay");
}

} // namespace

void RunNativeInputAdmissionTests() {
    TestFixedModes();
    TestAutoKeepsAdmissionWhenFramesFit();
    TestAutoOverlapsHoldsAndProbes();
    TestLuckyProbeBacksOff();
    TestSampleDelayGrowsShrinksAndIsBounded();
    TestParse();
}
