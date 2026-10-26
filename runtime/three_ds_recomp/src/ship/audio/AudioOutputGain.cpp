#include "ship/audio/AudioOutputGain.h"

#include <algorithm>

namespace Ship {

void AudioOutputGain::SetTarget(std::uint32_t gainQ16) noexcept {
    mTarget.store(std::min(gainQ16, kUnity), std::memory_order_relaxed);
}

std::uint32_t AudioOutputGain::Target() const noexcept {
    return mTarget.load(std::memory_order_relaxed);
}

bool AudioOutputGain::Inert() const noexcept {
    return mCurrent == kUnity && Target() == kUnity;
}

std::int16_t AudioOutputGain::Scale(std::int16_t sample, std::uint32_t gainQ16) noexcept {
    const std::int64_t scaled =
        (static_cast<std::int64_t>(sample) * static_cast<std::int64_t>(gainQ16) + 32768) >> 16;
    return static_cast<std::int16_t>(std::clamp<std::int64_t>(scaled, -32768, 32767));
}

void AudioOutputGain::BeginRampIfRetargeted(std::uint32_t target, std::uint32_t sampleRate) noexcept {
    if (target == mRampTo) {
        return;
    }
    mRampFrom = mCurrent;
    mRampTo = target;
    mRampDone = 0U;
    mRampLength = std::max<std::uint32_t>(1U, sampleRate * kRampMilliseconds / 1000U);
}

std::size_t AudioOutputGain::Process(std::int16_t* samples, std::size_t frames, std::uint32_t channels,
                                     std::uint32_t sampleRate) noexcept {
    const std::uint32_t target = Target();
    if (samples == nullptr || channels == 0U || frames == 0U) {
        return 0U;
    }
    if (mCurrent == kUnity && target == kUnity && mRampTo == kUnity) {
        return 0U;
    }
    BeginRampIfRetargeted(target, sampleRate);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        if (mRampDone < mRampLength) {
            ++mRampDone;
            const std::int64_t from = mRampFrom;
            const std::int64_t to = mRampTo;
            mCurrent = static_cast<std::uint32_t>(from + (to - from) * mRampDone / mRampLength);
        }
        std::int16_t* sample = samples + frame * channels;
        if (mCurrent == kUnity) {
            continue;
        }
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            sample[channel] = Scale(sample[channel], mCurrent);
        }
    }
    return frames;
}

void AudioOutputGain::Snap() noexcept {
    mCurrent = mRampTo = mRampFrom = Target();
    mRampLength = mRampDone = 0U;
}

} // namespace Ship
