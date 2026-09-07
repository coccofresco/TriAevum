#include "oot3d_native_player_clip_runtime.h"

#include <cmath>
#include <stdexcept>

namespace Oot3dNativeGame {
namespace {

bool SameFrameRange(float left, float right) {
    return std::abs(left - right) <= 0.00001f;
}

} // namespace

PlayerClipSample PlayerClipRuntime::Advance(
    const PlayerClipCommand& command, double deltaSeconds) {
    if (!std::isfinite(command.FramesPerSecond) ||
        !std::isfinite(command.StartFrame) || !std::isfinite(command.EndFrame) ||
        !std::isfinite(command.InitialFrame)) {
        throw std::invalid_argument("invalid OOT3D player clip command");
    }

    const float playSpeed =
        command.FramesPerSecond / kNativeSkelAnimeFramesPerSecondAtUnitPlaySpeed;
    const bool restart = !mActive || mClipIndex != command.ClipIndex ||
                         mMode != command.Mode ||
                         !SameFrameRange(mStartFrame, command.StartFrame) ||
                         !SameFrameRange(mEndFrame, command.EndFrame);
    if (restart) {
        mClock.Change(command.Mode, playSpeed, command.StartFrame, command.EndFrame);
        mClock.Seek(command.InitialFrame);
        mActive = true;
        mClipIndex = command.ClipIndex;
        mMode = command.Mode;
        mStartFrame = command.StartFrame;
        mEndFrame = command.EndFrame;
    } else {
        mClock.SetPlaySpeed(playSpeed);
    }

    const bool complete = restart ? false : mClock.AdvanceSeconds(deltaSeconds);
    return {
        true,
        restart,
        complete,
        mClipIndex,
        mClock.State().CurrentFrame,
        mClock.State().PlaySpeed,
    };
}

void PlayerClipRuntime::Reset() {
    mActive = false;
    mClipIndex = 0;
    mMode = SkelAnimeMode::Loop;
    mStartFrame = 0.0f;
    mEndFrame = 0.0f;
    mClock = {};
}

bool PlayerClipRuntime::Active() const {
    return mActive;
}

size_t PlayerClipRuntime::ClipIndex() const {
    return mClipIndex;
}

} // namespace Oot3dNativeGame
