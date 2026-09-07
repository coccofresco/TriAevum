#pragma once

#include <cstdint>

namespace Oot3dNativeGame {

inline constexpr double kNativeDisplayTicksPerSecond = 30.0;
inline constexpr int16_t kNativeSkelAnimeGlobalUpdateRate = 2;
inline constexpr float kNativeSkelAnimeUpdateScale = 0.3333333432674408f;
inline constexpr float kNativeSkelAnimeFramesPerSecondAtUnitPlaySpeed =
    static_cast<float>(kNativeDisplayTicksPerSecond) *
    static_cast<float>(kNativeSkelAnimeGlobalUpdateRate) *
    kNativeSkelAnimeUpdateScale;

enum class SkelAnimeMode : uint8_t {
    Loop = 0,
    LoopInterpolated = 1,
    Once = 2,
    OnceInterpolated = 3,
    PartialLoop = 4,
    PartialLoopInterpolated = 5,
};

struct SkelAnimeState {
    SkelAnimeMode Mode = SkelAnimeMode::Loop;
    float CurrentFrame = 0.0f;
    float PlaySpeed = 1.0f;
    float StartFrame = 0.0f;
    float EndFrame = 0.0f;
    uint8_t NativeUpdateMode = 4;
    bool Complete = false;
};

class SkelAnimeClock {
  public:
    void Change(SkelAnimeMode mode, float playSpeed, float startFrame, float endFrame);
    void SetPlaySpeed(float playSpeed);
    void Seek(float currentFrame);
    bool AdvanceDisplayTick();
    bool AdvanceNativeUpdateRate(float nativeUpdateRate);
    bool AdvanceSeconds(double deltaSeconds);

    const SkelAnimeState& State() const;

  private:
    SkelAnimeState mState;
};

} // namespace Oot3dNativeGame
