#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace Oot3dNativeGame {

constexpr size_t kNativeAudioDspFrameSize = 160;
constexpr size_t kNativeAudioDspChannelCount = 4;

struct NativeAudioDelayEffect {
    bool Enabled = false;
    uint8_t ChannelCount = 2;
    uint32_t FrameCount = 1;
    int16_t G = 0;
    int16_t A = 0;
    int16_t B = 0;
};

struct NativeAudioReverbEffect {
    bool Enabled = false;
    uint32_t NativeSampleRate = 0;
    std::array<uint32_t, 5> DelayLengths{1, 1, 1, 1, 1};
    int16_t Feedback0 = 0;
    int16_t Feedback1 = 0;
    int16_t Diffusion = 0;
    int16_t Damping = 0;
    int16_t WetGain = 0;
    int16_t DryGain = 0;
};

using NativeAudioEffect =
    std::variant<std::monostate, NativeAudioDelayEffect,
                 NativeAudioReverbEffect>;

struct NativeAudioAuxBusSettings {
    NativeAudioEffect Effect;
    float ReturnVolume = 1.0f;
};

class NativeAudioDspEffectChain {
  public:
    void SetSampleRates(uint32_t nativeSampleRate,
                        uint32_t processingSampleRate);
    void ConfigureBus(size_t bus, const NativeAudioAuxBusSettings& settings);
    const NativeAudioAuxBusSettings& BusSettings(size_t bus) const;
    std::vector<int32_t> ProcessStereoBus(
        size_t bus, std::span<const int32_t> interleavedSamples);
    std::vector<int32_t> ProcessBus(
        size_t bus, std::span<const int32_t> interleavedSamples,
        uint8_t channelCount);
    void Reset();

  private:
    struct DelayState {
        uint32_t SampleIndex = 0;
        std::array<int32_t, kNativeAudioDspChannelCount> Previous{};
        std::array<std::vector<int32_t>, kNativeAudioDspChannelCount> Ring;
    };

    struct ReverbState {
        std::array<uint32_t, 5> Indices{};
        std::array<int32_t, 2> DampingState{};
        std::array<std::array<std::vector<int32_t>, 2>, 5> Rings;
    };

    struct BusState {
        NativeAudioAuxBusSettings Settings;
        DelayState Delay;
        ReverbState Reverb;
    };

    static bool EffectEnabled(const NativeAudioEffect& effect);
    void InitializeEffectState(BusState& bus);
    void ProcessDelayBlock(BusState& bus,
                           std::span<int32_t> interleavedBlock,
                           uint8_t channelCount);
    void ProcessReverbBlock(BusState& bus,
                            std::span<int32_t> interleavedBlock,
                            uint8_t channelCount);
    size_t ScaleNativeSamples(uint64_t sampleCount) const;

    std::array<BusState, 2> mBuses;
    uint32_t mNativeSampleRate = 1;
    uint32_t mProcessingSampleRate = 1;
};

} // namespace Oot3dNativeGame
