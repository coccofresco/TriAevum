#pragma once

#include "three_ds_input.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace ThreeDsRecomp::Input {
// Input admission generation also detects a disconnect/reconnect or focus
// round trip between two guest samples. Pending pulses must not cross it.
struct GameplayRumbleAdmission {
  uint64_t Revision=0;
  int32_t ControllerInstance=-1;
  bool Allowed=false;
  bool Update(int32_t controller,bool controllerEnabled,bool focused,bool hostUi,RumbleStrength strength) {
    const bool allowed=controller>=0&&controllerEnabled&&focused&&!hostUi&&strength!=RumbleStrength::Off;
    const bool changed=controller!=ControllerInstance||allowed!=Allowed;
    if(changed)++Revision;
    ControllerInstance=controller; Allowed=allowed;
    return changed;
  }
};
// Read-only title facts. No guest addresses, device or configured strength.
struct GameplayRumbleSample {
  bool Gameplay = false;
  uint32_t PlayState = 0;
  uint32_t Player = 0;
  int16_t Scene = 0;
  int16_t Health = 0;
  uint16_t BgCheckFlags = 0; // normalized: ground=1, water=0x20
  float VelocityY = 0.0F;
  // A title may additionally require its native fall-impact predicate.
  bool AllowHardLanding = true;
};
enum class GameplayRumbleEventKind : uint8_t { Damage, HardLanding };
struct GameplayRumbleEvent {
  GameplayRumbleEventKind Kind = GameplayRumbleEventKind::Damage;
  float Amount = 0.0F;
};
inline constexpr float kGameplayHardLandingSpeed = 8.0F;

class GameplayRumbleEventTracker {
 public:
  std::vector<GameplayRumbleEvent> Observe(const GameplayRumbleSample& sample) {
    std::vector<GameplayRumbleEvent> events;
    if (!sample.Gameplay) { Reset(); return events; }
    const bool grounded = (sample.BgCheckFlags & 1U) != 0;
    const bool water = (sample.BgCheckFlags & 0x20U) != 0;
    const float fall = std::isfinite(sample.VelocityY) ? std::max(0.0F, -sample.VelocityY) : 0.0F;
    const bool sameStage = mPrevious && mPrevious->PlayState == sample.PlayState &&
                          mPrevious->Player == sample.Player && mPrevious->Scene == sample.Scene;
    if (!sameStage) {
      mPrevious = sample;
      mAirborne = !grounded && !water;
      mFastestFall = mAirborne ? fall : 0.0F;
      return events;
    }
    if (sample.Health < mPrevious->Health)
      events.push_back({GameplayRumbleEventKind::Damage, static_cast<float>(mPrevious->Health - sample.Health)});
    if (water) {
      mAirborne = false;
      mFastestFall = 0.0F;
    } else if (!grounded) {
      mAirborne = true;
      mFastestFall = std::max(mFastestFall, fall);
    } else if (mAirborne) {
      if (sample.AllowHardLanding && mFastestFall > kGameplayHardLandingSpeed)
        events.push_back({GameplayRumbleEventKind::HardLanding, mFastestFall});
      mAirborne = false;
      mFastestFall = 0.0F;
    }
    mPrevious = sample;
    return events;
  }
  void Reset() noexcept { mPrevious.reset(); mAirborne = false; mFastestFall = 0.0F; }
 private:
  std::optional<GameplayRumbleSample> mPrevious;
  bool mAirborne = false;
  float mFastestFall = 0.0F;
};

inline RumblePulse GameplayRumblePulse(const GameplayRumbleEvent& event) noexcept {
  const float amount = std::isfinite(event.Amount) ? std::max(0.0F, event.Amount) : 0.0F;
  RumblePulse pulse;
  switch (event.Kind) {
  case GameplayRumbleEventKind::Damage:
    pulse.Intensity = std::clamp(0.5F + amount / 64.0F, 0.5F, 1.0F);
    pulse.DurationMilliseconds = static_cast<uint16_t>(std::clamp(150.0F + amount * 5.0F, 150.0F, 400.0F));
    break;
  case GameplayRumbleEventKind::HardLanding: {
    const float over = std::max(0.0F, amount - kGameplayHardLandingSpeed);
    pulse.Intensity = std::clamp(0.3F + over * 0.05F, 0.3F, 0.9F);
    pulse.DurationMilliseconds = static_cast<uint16_t>(std::clamp(120.0F + over * 5.0F, 120.0F, 200.0F));
    break;
  }
  }
  if (amount <= 0.0F) pulse = {};
  return pulse;
}
} // namespace ThreeDsRecomp::Input
