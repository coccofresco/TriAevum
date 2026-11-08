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

ThreeDsRecomp::Input::RumblePulse Oot3dRumblePulse(const Oot3dRumbleEvent &event) noexcept {
  return ThreeDsRecomp::Input::GameplayRumblePulse(event);
}

} // namespace Oot3dNativeGame
