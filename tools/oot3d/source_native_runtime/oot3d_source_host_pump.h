#pragma once

#include "oot3d_ctr_hid_producer.h"
#include "oot3d_source_dsp_mixer.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <span>
#include <thread>

namespace Oot3dSourceRuntime {

struct SourceHostPumpStats {
    std::uint64_t HidSamples = 0;
    std::uint64_t DspFrames = 0;
    std::uint64_t DspFramesUnavailable = 0;
};

class SourceHostPump {
  public:
    using InputProvider = std::function<CtrHidState()>;
    using AudioSink = std::function<void(std::span<const std::int16_t>)>;

    SourceHostPump(CtrHidService& hid, SourceDspMixer& dsp,
                   InputProvider inputProvider = {}, AudioSink audioSink = {});
    ~SourceHostPump();

    SourceHostPump(const SourceHostPump&) = delete;
    SourceHostPump& operator=(const SourceHostPump&) = delete;

    void Start();
    void Stop();
    SourceHostPumpStats Stats() const noexcept;

  private:
    void Run(std::stop_token stopToken);

    CtrHidProducer mHid;
    SourceDspMixer& mDsp;
    InputProvider mInputProvider;
    AudioSink mAudioSink;
    std::jthread mThread;
    std::atomic<std::uint64_t> mHidSamples{0};
    std::atomic<std::uint64_t> mDspFrames{0};
    std::atomic<std::uint64_t> mDspFramesUnavailable{0};
};

} // namespace Oot3dSourceRuntime
