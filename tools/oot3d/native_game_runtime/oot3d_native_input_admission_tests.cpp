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
    // A failing probe doubles the hold.
    Frames(controller, 3U, true);
    Expect(!controller.Admitting(), "failed probe kept admitting");
    Expect(controller.HoldPresentations() == 40U, "failed probe did not double the hold");
    while (!controller.BeginFrame()) {}
    Frames(controller, 3U, true);
    Expect(controller.HoldPresentations() == 50U, "hold exceeded its maximum");
    while (!controller.BeginFrame()) {}
    // A passing probe keeps admission and restores the initial hold.
    Frames(controller, 10U, false);
    Expect(controller.Admitting(), "passing probe left admission");
    Expect(controller.Stats().SuccessfulProbes == 1U, "successful probe not counted");
    Expect(controller.HoldPresentations() == 20U, "passing probe kept the long hold");
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

} // namespace

void RunNativeInputAdmissionTests() {
    TestFixedModes();
    TestAutoKeepsAdmissionWhenFramesFit();
    TestAutoOverlapsHoldsAndProbes();
    TestParse();
}
