#include "oot3d_native_input_latency_trace.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace Oot3dNativeGame {
namespace {

double Milliseconds(NativeInputLatencyTrace::Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

// Nearest rank, as the other TRIAEVUM_* timing summaries.
double Percentile(std::vector<double> values, double q) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values[std::min(values.size() - 1U, static_cast<size_t>(q * static_cast<double>(values.size())))];
}

} // namespace

bool NativeInputLatencyTrace::EnabledByEnvironment() {
    const char* value = std::getenv("TRIAEVUM_INPUT_LATENCY");
    return value != nullptr && *value != '\0' && *value != '0';
}

void NativeInputLatencyTrace::ObserveInput(uint64_t frame, Clock::time_point pump, Clock::time_point poll,
                                           float interpolationAlpha) {
    if (!mEnabled) return;
    std::scoped_lock lock(mMutex);
    const bool overlapped = !mPending.empty();
    mPending[frame] = {pump, poll, {}, {}, interpolationAlpha, overlapped};
}

void NativeInputLatencyTrace::ObserveBegin(uint64_t frame, Clock::time_point start, Clock::time_point end) {
    if (!mEnabled) return;
    std::scoped_lock lock(mMutex);
    const auto found = mPending.find(frame);
    if (found == mPending.end()) return;
    found->second.BeginStart = start;
    found->second.BeginEnd = end;
}

void NativeInputLatencyTrace::ObserveFrameEnd(uint64_t frame, Clock::time_point waitStart,
                                              Clock::time_point waitEnd, Clock::time_point presented) {
    if (!mEnabled) return;
    std::scoped_lock lock(mMutex);
    const auto found = mPending.find(frame);
    if (found == mPending.end()) return;
    const auto& input = found->second;
    const bool begun = input.BeginEnd != Clock::time_point{};
    mSamples.push_back({frame, Milliseconds(input.Poll - input.Pump),
                        begun ? Milliseconds(input.BeginEnd - input.BeginStart) : 0.0,
                        begun ? Milliseconds(input.BeginEnd - input.Pump) : 0.0, Milliseconds(waitStart - input.Pump),
                        Milliseconds(waitEnd - waitStart), Milliseconds(presented - waitEnd),
                        Milliseconds(presented - input.Pump), input.Alpha, input.Overlapped});
    mPending.erase(found);
}

void NativeInputLatencyTrace::DiscardPending(uint64_t frame) {
    if (!mEnabled) return;
    std::scoped_lock lock(mMutex);
    mPending.erase(frame);
}

std::vector<NativeInputLatencySample> NativeInputLatencyTrace::Samples() const {
    std::scoped_lock lock(mMutex);
    return mSamples;
}

std::string NativeInputLatencyTrace::Summary(std::string_view mode, uint64_t skipFrames) const {
    const auto samples = Samples();
    std::vector<double> pumpToPoll, begin, pumpToBeginEnd, pumpToWait, wait, frameEnd, pumpToPresent;
    size_t overlapped = 0, alphaZero = 0, alphaHalf = 0, alphaOther = 0;
    for (const auto& sample : samples) {
        if (sample.Frame < skipFrames) continue;
        pumpToPoll.push_back(sample.PumpToPollMilliseconds);
        begin.push_back(sample.BeginMilliseconds);
        pumpToBeginEnd.push_back(sample.PumpToBeginEndMilliseconds);
        pumpToWait.push_back(sample.PumpToWaitMilliseconds);
        wait.push_back(sample.WaitMilliseconds);
        frameEnd.push_back(sample.FrameEndMilliseconds);
        pumpToPresent.push_back(sample.PumpToPresentMilliseconds);
        overlapped += sample.OverlappedPreviousFrame ? 1U : 0U;
        if (sample.InterpolationAlpha == 0.0F) ++alphaZero;
        else if (sample.InterpolationAlpha == 0.5F) ++alphaHalf;
        else ++alphaOther;
    }
    std::string line = "TRIAEVUM_INPUT_LATENCY mode=" + std::string(mode) +
                       " frames=" + std::to_string(pumpToPresent.size()) +
                       " overlapped=" + std::to_string(overlapped);
    const auto append = [&line](const char* name, const std::vector<double>& values) {
        char field[96];
        std::snprintf(field, sizeof(field), " %s=%.2f/%.2f", name, Percentile(values, 0.5),
                      Percentile(values, 0.95));
        line += field;
    };
    append("pump_to_present_ms", pumpToPresent);
    append("pump_to_poll_ms", pumpToPoll);
    append("begin_ms", begin);
    append("pump_to_begin_end_ms", pumpToBeginEnd);
    append("pump_to_wait_ms", pumpToWait);
    append("wait_ms", wait);
    append("frame_end_ms", frameEnd);
    line += " alpha0=" + std::to_string(alphaZero) + " alpha_half=" + std::to_string(alphaHalf) +
            " alpha_other=" + std::to_string(alphaOther) + " (p50/p95)";
    return line;
}

bool NativeInputLatencyTrace::WriteCsv(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << "frame,pump_to_poll_ms,begin_ms,pump_to_begin_end_ms,pump_to_wait_ms,wait_ms,frame_end_ms,"
           "pump_to_present_ms,alpha,overlapped\n";
    for (const auto& sample : Samples())
        out << sample.Frame << ',' << sample.PumpToPollMilliseconds << ',' << sample.BeginMilliseconds << ','
            << sample.PumpToBeginEndMilliseconds << ',' << sample.PumpToWaitMilliseconds << ','
            << sample.WaitMilliseconds << ',' << sample.FrameEndMilliseconds << ','
            << sample.PumpToPresentMilliseconds << ',' << sample.InterpolationAlpha << ','
            << (sample.OverlappedPreviousFrame ? 1 : 0) << '\n';
    return static_cast<bool>(out);
}

} // namespace Oot3dNativeGame
