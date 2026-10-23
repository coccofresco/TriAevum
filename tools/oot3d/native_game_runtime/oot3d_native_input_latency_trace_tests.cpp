#include "oot3d_native_input_latency_trace.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace {

using Oot3dNativeGame::NativeInputLatencyTrace;
using Clock = NativeInputLatencyTrace::Clock;

void Expect(bool condition, const char* role) {
    if (!condition) throw std::runtime_error(role);
}

void ExpectNear(double actual, double expected, const char* role) {
    Expect(std::abs(actual - expected) < 1.0e-6, role);
}

} // namespace

void RunNativeInputLatencyTraceTests() {
    const auto at = [origin = Clock::time_point{}](double milliseconds) {
        return origin + std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double, std::milli>(milliseconds));
    };

    NativeInputLatencyTrace disabled(false);
    disabled.ObserveInput(0, at(0), at(1), 0.0F);
    disabled.ObserveFrameEnd(0, at(5), at(10), at(11));
    Expect(disabled.Samples().empty(), "a disabled trace must record nothing");

    NativeInputLatencyTrace trace(true);
    // Frame 0 alone; frame 1 pumps while frame 0 is still waiting (parallel).
    trace.ObserveInput(0, at(0), at(1), 0.0F);
    trace.ObserveInput(1, at(14), at(15), 0.5F);
    trace.ObserveFrameEnd(0, at(6), at(16), at(18));
    trace.ObserveFrameEnd(1, at(25), at(33), at(34));
    // A frame without an end, and an end without input, leave no sample.
    trace.ObserveInput(2, at(40), at(41), 0.0F);
    trace.DiscardPending(2);
    trace.ObserveFrameEnd(2, at(45), at(50), at(51));
    trace.ObserveFrameEnd(7, at(45), at(50), at(51));
    const auto samples = trace.Samples();
    Expect(samples.size() == 2U, "one sample per ended frame with input");
    Expect(!samples[0].OverlappedPreviousFrame && samples[1].OverlappedPreviousFrame,
           "overlap marks input pumped before the previous frame ended");
    ExpectNear(samples[0].PumpToPollMilliseconds, 1.0, "pump to poll");
    ExpectNear(samples[0].PumpToWaitMilliseconds, 6.0, "pump to wait");
    ExpectNear(samples[0].WaitMilliseconds, 10.0, "wait");
    ExpectNear(samples[0].FrameEndMilliseconds, 2.0, "frame end");
    ExpectNear(samples[0].PumpToPresentMilliseconds, 18.0, "pump to present");
    ExpectNear(samples[1].PumpToPresentMilliseconds, 20.0, "overlapped pump to present");

    const auto summary = trace.Summary("parallel", 1U);
    Expect(summary.find("mode=parallel frames=1 overlapped=1 pump_to_present_ms=20.00/20.00") != std::string::npos,
           "summary skips the warm-up frames");
    Expect(summary.find("alpha0=0 alpha_half=1") != std::string::npos, "summary counts the alphas");
}
