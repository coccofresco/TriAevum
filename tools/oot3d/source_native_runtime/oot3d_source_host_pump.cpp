#include "oot3d_source_host_pump.h"

#include "oot3d_ctr_host_services.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace Oot3dSourceRuntime {
namespace {

constexpr auto kHidPeriod = std::chrono::nanoseconds(1'000'000'000 / 60);
constexpr auto kDspPeriod = std::chrono::nanoseconds(
    1'000'000'000ULL * Oot3dNativeGame::NativeA32DspHle::SamplesPerFrame /
    Oot3dNativeGame::NativeA32DspHle::NativeSampleRate);

} // namespace

SourceHostPump::SourceHostPump(CtrHidService& hid, SourceDspMixer& dsp,
                               InputProvider inputProvider, AudioSink audioSink)
    : mHid(hid), mDsp(dsp), mInputProvider(std::move(inputProvider)),
      mAudioSink(std::move(audioSink)) {}

SourceHostPump::~SourceHostPump() {
    Stop();
}

void SourceHostPump::Start() {
    if (mThread.joinable()) return;
    mThread = std::jthread([this](std::stop_token stopToken) { Run(stopToken); });
}

void SourceHostPump::Stop() {
    if (!mThread.joinable()) return;
    mThread.request_stop();
    mThread.join();
}

SourceHostPumpStats SourceHostPump::Stats() const noexcept {
    return {
        mHidSamples.load(std::memory_order_relaxed),
        mDspFrames.load(std::memory_order_relaxed),
        mDspFramesUnavailable.load(std::memory_order_relaxed),
    };
}

void SourceHostPump::Run(std::stop_token stopToken) {
    using Clock = std::chrono::steady_clock;
    auto nextHid = Clock::now();
    auto nextDsp = nextHid;
    std::vector<std::int16_t> samples;
    std::string error;

    while (!stopToken.stop_requested()) {
        const auto now = Clock::now();
        if (now >= nextHid) {
            const CtrHidState state = mInputProvider ? mInputProvider()
                                                     : CtrHidState{};
            if (mHid.Submit(state, oot3d_host_target_system_tick())) {
                mHidSamples.fetch_add(1, std::memory_order_relaxed);
            }
            do {
                nextHid += kHidPeriod;
            } while (nextHid <= now);
        }
        if (now >= nextDsp) {
            error.clear();
            if (mDsp.ProcessFrame(samples, &error)) {
                mDspFrames.fetch_add(1, std::memory_order_relaxed);
                if (mAudioSink) mAudioSink(samples);
            } else {
                mDspFramesUnavailable.fetch_add(1, std::memory_order_relaxed);
            }
            do {
                nextDsp += kDspPeriod;
            } while (nextDsp <= now);
        }
        std::this_thread::sleep_until(std::min(nextHid, nextDsp));
    }
}

} // namespace Oot3dSourceRuntime
