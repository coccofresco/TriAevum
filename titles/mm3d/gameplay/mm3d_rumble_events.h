#pragma once

// MM3D facts for the shared rumble events (Controls > Rumble), mirroring the
// OoT3D adapter (titles/oot3d/gameplay/oot3d_rumble_events.h): the same
// Damage / HardLanding tracker and pulses, fed from MM3D's own game state.
// The 3DS has no motor and MM3D raises no rumble request; reading never
// writes the guest and Off reads nothing (the frame loop gates the call).
//
// Facts (EUR == USA v1.1 code.bin, SHA 6eec27b1...):
//  - Health: Common(0x7751D8)+0x13C, s16, 16 per heart. Project Restoration
//    PlayerData; mm3d-dekui death_penalties uses the same 0x775314.
//  - Play/Player: CaptureMm3dSceneEnvironment's guarded reads (live Play
//    state type 4, main camera backpointer, Player id 0 / category 2 / calc).
//  - Actor::vel (Vec3) at 0x64, so velocity.y at 0x68; Actor flags_94 bit 0
//    = grounded (Project Restoration game/actor.h, sizeof(Actor)==0x1F8;
//    MM3D carries bgCheckFlags at 0x94, N64 MM at 0x90).
//  - Water bit 0x20 is zeldaret-mm BGCHECKFLAG_WATER semantics (N64); not
//    yet live-proven on MM3D.
#include "mm3d_scene_environment.h"
#include "oot3d_rumble_events.h"

#include <bit>
#include <cstdint>

namespace Oot3dNativeGame {
namespace Mm3dRumbleLayout {
inline constexpr uint32_t Health = Mm3dSceneEnvironmentLayout::Common + 0x13CU;
static_assert(Health == 0x00775314U);
inline constexpr uint32_t ActorVelocityY = 0x68U;
inline constexpr uint32_t ActorBgCheckFlags = 0x94U;
} // namespace Mm3dRumbleLayout

// Fills an Oot3dRumbleSample (title-neutral shape) from MM3D memory. Memory
// needs Read8/16/32 only. Gameplay stays false unless every read succeeds
// in a live play state with the Player actor.
template<class Memory> bool ReadMm3dRumbleSample(Memory& memory, Oot3dRumbleSample* sample) {
  if (sample == nullptr) return false;
  *sample = {};
  const auto capture = CaptureMm3dSceneEnvironment(memory);
  if (!capture.State || !capture.State->Player || capture.Player == 0U) return false;
  uint16_t health = 0U, flags = 0U;
  uint32_t velocityY = 0U;
  if (!memory.Read16(Mm3dRumbleLayout::Health, &health) ||
      !memory.Read32(capture.Player + Mm3dRumbleLayout::ActorVelocityY, &velocityY) ||
      !memory.Read16(capture.Player + Mm3dRumbleLayout::ActorBgCheckFlags, &flags))
    return false;
  sample->Gameplay = true;
  sample->PlayState = capture.Play;
  sample->Player = capture.Player;
  sample->Scene = static_cast<int16_t>(capture.State->SceneId);
  sample->Health = static_cast<int16_t>(health);
  sample->BgCheckFlags = flags;
  sample->VelocityY = std::bit_cast<float>(velocityY);
  return true;
}
} // namespace Oot3dNativeGame
