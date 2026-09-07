#include "oot3d_native_player_clip_runtime.h"

#include <cmath>
#include <stdexcept>

namespace {

bool Near(float left, float right) {
    return std::abs(left - right) <= 0.0001f;
}

} // namespace

void RunPlayerClipRuntimeTests() {
    Oot3dNativeGame::PlayerClipRuntime runtime;
    Oot3dNativeGame::PlayerClipCommand idle;
    idle.ClipIndex = 4;
    idle.FramesPerSecond = 30.0f;
    idle.EndFrame = 29.0f;
    idle.InitialFrame = 3.0f;

    auto sample = runtime.Advance(idle, 1.0 / 30.0);
    if (!sample.Restarted || !Near(sample.CurrentFrame, 3.0f) ||
        !Near(sample.NativePlaySpeed, 1.5f)) {
        throw std::runtime_error("player clip runtime did not preserve the native initial frame");
    }
    sample = runtime.Advance(idle, 1.0 / 30.0);
    if (sample.Restarted || !Near(sample.CurrentFrame, 4.0f)) {
        throw std::runtime_error("player clip runtime did not advance through SkelAnime");
    }

    auto run = idle;
    run.ClipIndex = 9;
    run.FramesPerSecond = 20.0f;
    run.EndFrame = 19.0f;
    run.InitialFrame = 7.5f;
    sample = runtime.Advance(run, 1.0 / 30.0);
    if (!sample.Restarted || sample.ClipIndex != 9 || !Near(sample.CurrentFrame, 7.5f)) {
        throw std::runtime_error("player clip runtime lost phase across a native clip change");
    }
    runtime.Reset();
    sample = runtime.Advance(idle, 0.0);
    if (!sample.Restarted || sample.ClipIndex != idle.ClipIndex) {
        throw std::runtime_error("player clip runtime reset did not restart the next clip");
    }


    auto reverse = idle;
    reverse.ClipIndex = 10;
    reverse.Mode = Oot3dNativeGame::SkelAnimeMode::Once;
    reverse.FramesPerSecond = -20.0f;
    reverse.StartFrame = 3.0f;
    reverse.EndFrame = 0.0f;
    reverse.InitialFrame = 3.0f;
    runtime.Reset();
    sample = runtime.Advance(reverse, 0.0);
    if (!sample.Restarted || !Near(sample.CurrentFrame, 3.0f) ||
        !Near(sample.NativePlaySpeed, -1.0f)) {
        throw std::runtime_error("player clip runtime rejected native reverse playback");
    }
}
