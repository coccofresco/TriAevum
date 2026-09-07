#include "oot3d_native_audio_dsp.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace Oot3dNativeGame {
namespace {

int32_t WrapInt32(int64_t value) {
    return static_cast<int32_t>(static_cast<uint32_t>(value));
}

int32_t MulShift7Symmetric(int32_t value, int32_t gain) {
    const int64_t magnitude = value < 0 ? -static_cast<int64_t>(value) : value;
    const int64_t scaled = static_cast<int64_t>(gain) * magnitude >> 7;
    return WrapInt32(value < 0 ? -scaled : scaled);
}

} // namespace

bool NativeAudioDspEffectChain::EffectEnabled(
    const NativeAudioEffect& effect) {
    if (const auto* delay = std::get_if<NativeAudioDelayEffect>(&effect)) {
        return delay->Enabled;
    }
    if (const auto* reverb = std::get_if<NativeAudioReverbEffect>(&effect)) {
        return reverb->Enabled;
    }
    return false;
}

size_t NativeAudioDspEffectChain::ScaleNativeSamples(
    uint64_t sampleCount) const {
    const uint64_t scaled = sampleCount * mProcessingSampleRate +
                            mNativeSampleRate / 2;
    return std::max<uint64_t>(scaled / mNativeSampleRate, 1);
}

void NativeAudioDspEffectChain::SetSampleRates(
    uint32_t nativeSampleRate, uint32_t processingSampleRate) {
    if (nativeSampleRate == 0 || processingSampleRate == 0) {
        throw std::runtime_error("native audio DSP sample rate is zero");
    }
    if (mNativeSampleRate == nativeSampleRate &&
        mProcessingSampleRate == processingSampleRate) {
        return;
    }
    mNativeSampleRate = nativeSampleRate;
    mProcessingSampleRate = processingSampleRate;
    Reset();
}

void NativeAudioDspEffectChain::InitializeEffectState(BusState& bus) {
    bus.Delay = {};
    bus.Reverb = {};

    if (const auto* delay =
            std::get_if<NativeAudioDelayEffect>(&bus.Settings.Effect)) {
        if (!delay->Enabled) {
            return;
        }
        if (delay->FrameCount == 0) {
            throw std::runtime_error("native audio delay frame count is zero");
        }
        if (delay->ChannelCount != 2 &&
            delay->ChannelCount != kNativeAudioDspChannelCount) {
            throw std::runtime_error(
                "native audio delay channel count is invalid");
        }
        const size_t sampleCount = ScaleNativeSamples(
            static_cast<uint64_t>(delay->FrameCount) *
            kNativeAudioDspFrameSize);
        for (size_t channel = 0; channel < delay->ChannelCount; ++channel) {
            bus.Delay.Ring[channel].assign(sampleCount, 0);
        }
        return;
    }

    if (const auto* reverb =
            std::get_if<NativeAudioReverbEffect>(&bus.Settings.Effect)) {
        if (!reverb->Enabled) {
            return;
        }
        for (size_t tap = 0; tap < reverb->DelayLengths.size(); ++tap) {
            if (reverb->DelayLengths[tap] <
                kNativeAudioDspFrameSize * 2) {
                throw std::runtime_error(
                    "native audio reverb delay is shorter than one stereo DSP pass");
            }
            const size_t sampleCount = ScaleNativeSamples(
                reverb->DelayLengths[tap]);
            for (auto& ring : bus.Reverb.Rings[tap]) {
                ring.assign(sampleCount, 0);
            }
        }
    }
}

void NativeAudioDspEffectChain::ConfigureBus(
    size_t bus, const NativeAudioAuxBusSettings& settings) {
    if (bus >= mBuses.size()) {
        throw std::out_of_range("native audio auxiliary bus");
    }
    mBuses[bus].Settings = settings;
    InitializeEffectState(mBuses[bus]);
}

const NativeAudioAuxBusSettings& NativeAudioDspEffectChain::BusSettings(
    size_t bus) const {
    if (bus >= mBuses.size()) {
        throw std::out_of_range("native audio auxiliary bus");
    }
    return mBuses[bus].Settings;
}

void NativeAudioDspEffectChain::ProcessDelayBlock(
    BusState& bus, std::span<int32_t> interleavedBlock,
    uint8_t channelCount) {
    const auto& config = std::get<NativeAudioDelayEffect>(bus.Settings.Effect);
    const size_t frameCount = interleavedBlock.size() / channelCount;
    for (size_t channel = 0; channel < config.ChannelCount; ++channel) {
        auto& ring = bus.Delay.Ring[channel];
        int32_t& previous = bus.Delay.Previous[channel];
        uint32_t sampleIndex = bus.Delay.SampleIndex;
        for (size_t sample = 0; sample < frameCount; ++sample) {
            const size_t outputIndex = sample * channelCount + channel;
            const int32_t delayed = ring[sampleIndex];
            const int32_t scaledDelayed =
                MulShift7Symmetric(delayed, config.G);
            const int64_t feedback =
                static_cast<int64_t>(config.A) *
                    (static_cast<int64_t>(interleavedBlock[outputIndex]) -
                     scaledDelayed) +
                static_cast<int64_t>(config.B) * previous;
            previous = WrapInt32(feedback >> 7);
            ring[sampleIndex] = previous;
            interleavedBlock[outputIndex] = delayed;
            ++sampleIndex;
            if (sampleIndex == ring.size()) {
                sampleIndex = 0;
            }
        }
    }
    bus.Delay.SampleIndex = static_cast<uint32_t>(
        (bus.Delay.SampleIndex + frameCount) % bus.Delay.Ring[0].size());
}

void NativeAudioDspEffectChain::ProcessReverbBlock(
    BusState& bus, std::span<int32_t> interleavedBlock,
    uint8_t channelCount) {
    const auto& config = std::get<NativeAudioReverbEffect>(
        bus.Settings.Effect);
    const auto initialIndices = bus.Reverb.Indices;
    auto finalIndices = initialIndices;
    const size_t frameCount = interleavedBlock.size() / channelCount;
    for (size_t channel = 0; channel < 2; ++channel) {
        auto channelIndices = initialIndices;
        auto& ring0 = bus.Reverb.Rings[0][channel];
        auto& ring1 = bus.Reverb.Rings[1][channel];
        auto& ring2 = bus.Reverb.Rings[2][channel];
        auto& ring3 = bus.Reverb.Rings[3][channel];
        auto& ring4 = bus.Reverb.Rings[4][channel];
        int32_t& dampingState = bus.Reverb.DampingState[channel];

        for (size_t sample = 0; sample < frameCount; ++sample) {
            const size_t outputIndex = sample * channelCount + channel;
            const int32_t input = interleavedBlock[outputIndex];
            const int32_t delayedInput = ring0[channelIndices[0]];
            ring0[channelIndices[0]] = input;

            const int32_t earlyReflection = ring1[channelIndices[1]];
            ring1[channelIndices[1]] = input;

            const int32_t feedback0 = ring2[channelIndices[2]];
            ring2[channelIndices[2]] = WrapInt32(
                static_cast<int64_t>(earlyReflection) +
                MulShift7Symmetric(feedback0, config.Feedback0));

            const int32_t feedback1 = ring3[channelIndices[3]];
            ring3[channelIndices[3]] = WrapInt32(
                static_cast<int64_t>(earlyReflection) +
                MulShift7Symmetric(feedback1, config.Feedback1));

            const int32_t diffuserDelay = ring4[channelIndices[4]];
            const int32_t diffusionDelayed =
                MulShift7Symmetric(diffuserDelay, config.Diffusion);
            const int32_t diffusion = WrapInt32(
                static_cast<int64_t>(feedback0) - feedback1 +
                diffusionDelayed);
            ring4[channelIndices[4]] = diffusion;
            const int32_t diffusionFeedback =
                MulShift7Symmetric(diffusion, config.Diffusion);
            const int32_t dampInput = WrapInt32(
                static_cast<int64_t>(diffuserDelay) - diffusionFeedback);
            dampingState = WrapInt32(
                static_cast<int64_t>(dampInput) -
                ((static_cast<int64_t>(config.Damping) *
                  (static_cast<int64_t>(dampingState) + dampInput)) >> 7));
            interleavedBlock[outputIndex] = WrapInt32(
                (static_cast<int64_t>(dampingState) * config.WetGain +
                 static_cast<int64_t>(delayedInput) * config.DryGain) >> 7);

            for (size_t tap = 0; tap < channelIndices.size(); ++tap) {
                ++channelIndices[tap];
                if (channelIndices[tap] ==
                    bus.Reverb.Rings[tap][channel].size()) {
                    channelIndices[tap] = 0;
                }
            }
        }
        finalIndices = channelIndices;
    }
    for (size_t tap = 0; tap < finalIndices.size(); ++tap) {
        bus.Reverb.Indices[tap] = finalIndices[tap];
    }
}

std::vector<int32_t> NativeAudioDspEffectChain::ProcessStereoBus(
    size_t busIndex, std::span<const int32_t> interleavedSamples) {
    return ProcessBus(busIndex, interleavedSamples, 2);
}

std::vector<int32_t> NativeAudioDspEffectChain::ProcessBus(
    size_t busIndex, std::span<const int32_t> interleavedSamples,
    uint8_t channelCount) {
    if (busIndex >= mBuses.size()) {
        throw std::out_of_range("native audio auxiliary bus");
    }
    if ((channelCount != 2 &&
         channelCount != kNativeAudioDspChannelCount) ||
        interleavedSamples.size() % channelCount != 0) {
        throw std::runtime_error(
            "native audio auxiliary bus channel layout is invalid");
    }
    BusState& bus = mBuses[busIndex];
    if (!EffectEnabled(bus.Settings.Effect)) {
        return {interleavedSamples.begin(), interleavedSamples.end()};
    }

    std::vector<int32_t> output(
        interleavedSamples.begin(), interleavedSamples.end());
    if (std::holds_alternative<NativeAudioDelayEffect>(
            bus.Settings.Effect)) {
        const auto& delay = std::get<NativeAudioDelayEffect>(
            bus.Settings.Effect);
        if (delay.ChannelCount > channelCount) {
            throw std::runtime_error(
                "native audio delay input lacks configured channels");
        }
        ProcessDelayBlock(bus, output, channelCount);
    } else {
        ProcessReverbBlock(bus, output, channelCount);
    }
    return output;
}

void NativeAudioDspEffectChain::Reset() {
    for (auto& bus : mBuses) {
        InitializeEffectState(bus);
    }
}

} // namespace Oot3dNativeGame
