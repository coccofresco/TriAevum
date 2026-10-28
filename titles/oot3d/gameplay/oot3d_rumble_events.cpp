#include "oot3d_rumble_events.h"

#include "recomp/a32_runtime.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace Oot3dNativeGame {

bool ReadOot3dRumbleSample(oot3d::recomp::a32::MemoryBus &memory, uint32_t playState,
                           Oot3dRumbleSample *sample) {
  if (sample == nullptr) return false;
  *sample = {};
  if (playState == 0U) return false;
  uint32_t gameMode = 0U, player = 0U, velocityY = 0U;
  uint16_t health = 0U, scene = 0U, actorId = 0U, flags = 0U;
  if (!memory.Read32(kOot3dRumbleSaveContext + kOot3dRumbleSaveGameMode, &gameMode) || gameMode != 0U ||
      !memory.Read16(kOot3dRumbleSaveContext + kOot3dRumbleSaveHealth, &health) ||
      !memory.Read16(playState + kOot3dRumblePlayScene, &scene) ||
      !memory.Read32(playState + kOot3dRumblePlayPlayerHead, &player) || player == 0U ||
      !memory.Read16(player + kOot3dRumbleActorId, &actorId) || actorId != 0U ||
      !memory.Read32(player + kOot3dRumbleActorVelocityY, &velocityY) ||
      !memory.Read16(player + kOot3dRumbleActorBgCheckFlags, &flags))
    return false;
  sample->Gameplay = true;
  sample->PlayState = playState;
  sample->Player = player;
  sample->Scene = static_cast<int16_t>(scene);
  sample->Health = static_cast<int16_t>(health);
  sample->BgCheckFlags = flags;
  sample->VelocityY = std::bit_cast<float>(velocityY);
  return true;
}

std::vector<Oot3dRumbleEvent> Oot3dRumbleEventTracker::Observe(const Oot3dRumbleSample &sample) {
  std::vector<Oot3dRumbleEvent> events;
  if (!sample.Gameplay) {
    Reset();
    return events;
  }
  const bool grounded = (sample.BgCheckFlags & kOot3dBgCheckGround) != 0U;
  const bool water = (sample.BgCheckFlags & kOot3dBgCheckWater) != 0U;
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
    events.push_back({Oot3dRumbleEventKind::Damage, static_cast<float>(mPrevious->Health - sample.Health)});
  if (water) {
    mAirborne = false;
    mFastestFall = 0.0F;
  } else if (!grounded) {
    mAirborne = true;
    mFastestFall = std::max(mFastestFall, fall);
  } else if (mAirborne) {
    if (mFastestFall > kOot3dHardLandingSpeed)
      events.push_back({Oot3dRumbleEventKind::HardLanding, mFastestFall});
    mAirborne = false;
    mFastestFall = 0.0F;
  }
  mPrevious = sample;
  return events;
}

void Oot3dRumbleEventTracker::Reset() noexcept {
  mPrevious.reset();
  mAirborne = false;
  mFastestFall = 0.0F;
}

ThreeDsRecomp::Input::RumblePulse Oot3dRumblePulse(const Oot3dRumbleEvent &event) noexcept {
  const float amount = std::isfinite(event.Amount) ? std::max(0.0F, event.Amount) : 0.0F;
  ThreeDsRecomp::Input::RumblePulse pulse;
  switch (event.Kind) {
  case Oot3dRumbleEventKind::Damage:
    // Half a heart 0.625, a heart 0.75, two hearts and more 1.0.
    pulse.Intensity = std::clamp(0.5F + amount / 64.0F, 0.5F, 1.0F);
    pulse.DurationMilliseconds = static_cast<uint16_t>(std::clamp(150.0F + amount * 5.0F, 150.0F, 400.0F));
    break;
  case Oot3dRumbleEventKind::HardLanding: {
    // From the threshold (0.3) to terminal speed, 20 (0.9).
    const float over = std::max(0.0F, amount - kOot3dHardLandingSpeed);
    pulse.Intensity = std::clamp(0.3F + over * 0.05F, 0.3F, 0.9F);
    pulse.DurationMilliseconds = static_cast<uint16_t>(std::clamp(120.0F + over * 5.0F, 120.0F, 200.0F));
    break;
  }
  }
  if (amount <= 0.0F) pulse = {};
  return pulse;
}

} // namespace Oot3dNativeGame
