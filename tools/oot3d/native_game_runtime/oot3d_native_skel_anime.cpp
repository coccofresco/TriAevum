#include "oot3d_native_skel_anime.h"

#include "oot3d_gameplay_time.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Oot3dNativeGame {
namespace {

float WrapInclusiveFrameSpan(float frame, float startFrame, float endFrame) {
    const float span = endFrame - startFrame + 1.0f;
    float wrapped = std::fmod(frame - startFrame, span);
    if (wrapped < 0.0f) {
        wrapped += span;
    }
    return startFrame + wrapped;
}

float WrapPartialFrameSpan(float frame, float startFrame, float endFrame) {
    const float span = endFrame - startFrame;
    if (span <= 0.0f) {
        return startFrame;
    }
    float wrapped = std::fmod(frame - startFrame, span);
    if (wrapped < 0.0f) {
        wrapped += span;
    }
    return startFrame + wrapped;
}

bool IsFullLoop(SkelAnimeMode mode) {
    return mode == SkelAnimeMode::Loop || mode == SkelAnimeMode::LoopInterpolated;
}

bool IsOnce(SkelAnimeMode mode) {
    return mode == SkelAnimeMode::Once || mode == SkelAnimeMode::OnceInterpolated;
}

bool IsPartialLoop(SkelAnimeMode mode) {
    return mode == SkelAnimeMode::PartialLoop ||
           mode == SkelAnimeMode::PartialLoopInterpolated;
}

uint8_t NativeUpdateMode(SkelAnimeMode mode) {
    const auto value = static_cast<uint8_t>(mode);
    return value < 2 ? 4 : (value < 4 ? 6 : 5);
}

} // namespace

void SkelAnimeClock::Change(SkelAnimeMode mode, float playSpeed,
                            float startFrame, float endFrame) {
    const bool knownMode = IsFullLoop(mode) || IsOnce(mode) || IsPartialLoop(mode);
    if (!knownMode || !std::isfinite(playSpeed) || !std::isfinite(startFrame) ||
        !std::isfinite(endFrame) ||
        (!IsOnce(mode) && endFrame < startFrame)) {
        throw std::invalid_argument("invalid OOT3D SkelAnime range or speed");
    }
    const float currentFrame = IsPartialLoop(mode) ? 0.0f : startFrame;
    mState = { mode, currentFrame, playSpeed, startFrame, endFrame,
               NativeUpdateMode(mode), false };
}

void SkelAnimeClock::SetPlaySpeed(float playSpeed) {
    if (!std::isfinite(playSpeed)) {
        throw std::invalid_argument("invalid OOT3D SkelAnime speed");
    }
    mState.PlaySpeed = playSpeed;
}

void SkelAnimeClock::Seek(float currentFrame) {
    if (!std::isfinite(currentFrame)) {
        throw std::invalid_argument("invalid OOT3D SkelAnime frame");
    }
    if (IsFullLoop(mState.Mode)) {
        mState.CurrentFrame = WrapInclusiveFrameSpan(
            currentFrame, mState.StartFrame, mState.EndFrame);
    } else if (IsPartialLoop(mState.Mode)) {
        mState.CurrentFrame = WrapPartialFrameSpan(
            currentFrame, mState.StartFrame, mState.EndFrame);
    } else {
        mState.CurrentFrame = std::clamp(
            currentFrame, std::min(mState.StartFrame, mState.EndFrame),
            std::max(mState.StartFrame, mState.EndFrame));
    }
    mState.Complete = false;
}

bool SkelAnimeClock::AdvanceDisplayTick() {
    return AdvanceNativeUpdateRate(
        static_cast<float>(kNativeSkelAnimeGlobalUpdateRate));
}

bool SkelAnimeClock::AdvanceNativeUpdateRate(float nativeUpdateRate) {
    if (!std::isfinite(nativeUpdateRate) || nativeUpdateRate < 0.0f) {
        throw std::invalid_argument("invalid OOT3D SkelAnime update rate");
    }
    if (IsOnce(mState.Mode) && mState.CurrentFrame == mState.EndFrame) {
        mState.Complete = true;
        return true;
    }

    const float delta =
        mState.PlaySpeed * nativeUpdateRate * kNativeSkelAnimeUpdateScale;
    const float nextFrame = mState.CurrentFrame + delta;
    if (IsFullLoop(mState.Mode)) {
        mState.CurrentFrame = WrapInclusiveFrameSpan(
            nextFrame, mState.StartFrame, mState.EndFrame);
        return false;
    }
    if (IsPartialLoop(mState.Mode)) {
        mState.CurrentFrame = WrapPartialFrameSpan(
            nextFrame, mState.StartFrame, mState.EndFrame);
        return false;
    }

    if ((nextFrame - mState.EndFrame) * mState.PlaySpeed > 0.0f) {
        mState.CurrentFrame = mState.EndFrame;
    } else {
        mState.CurrentFrame = nextFrame;
    }
    return false;
}

bool SkelAnimeClock::AdvanceSeconds(double deltaSeconds) {
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0) {
        throw std::invalid_argument("invalid OOT3D SkelAnime delta time");
    }
    const float nativeUpdateRate = static_cast<float>(
        deltaSeconds * oot3d::gameplay::kNativeTimeUnitsPerSecond);
    return AdvanceNativeUpdateRate(nativeUpdateRate);
}

const SkelAnimeState& SkelAnimeClock::State() const {
    return mState;
}

} // namespace Oot3dNativeGame
