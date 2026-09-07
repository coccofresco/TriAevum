#pragma once

#include <cstddef>

#include "oot3d_native_skel_anime.h"

namespace Oot3dNativeGame {

struct PlayerClipCommand {
    size_t ClipIndex = 0;
    SkelAnimeMode Mode = SkelAnimeMode::Loop;
    float FramesPerSecond = 0.0f;
    float StartFrame = 0.0f;
    float EndFrame = 0.0f;
    float InitialFrame = 0.0f;
};

struct PlayerClipSample {
    bool Available = false;
    bool Restarted = false;
    bool Complete = false;
    size_t ClipIndex = 0;
    float CurrentFrame = 0.0f;
    float NativePlaySpeed = 0.0f;
};

class PlayerClipRuntime {
  public:
    PlayerClipSample Advance(const PlayerClipCommand& command, double deltaSeconds);
    void Reset();
    bool Active() const;
    size_t ClipIndex() const;

  private:
    bool mActive = false;
    size_t mClipIndex = 0;
    SkelAnimeMode mMode = SkelAnimeMode::Loop;
    float mStartFrame = 0.0f;
    float mEndFrame = 0.0f;
    SkelAnimeClock mClock;
};

} // namespace Oot3dNativeGame
