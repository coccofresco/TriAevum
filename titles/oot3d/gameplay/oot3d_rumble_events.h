#pragma once

// The OoT3D events a pad's rumble follows (Controls > Rumble): Link losing
// health and Link landing after a fast fall. The 3DS has no rumble motor and
// the game raises no rumble request, so the adapter reads them from the
// native game state each guest refresh, and only while Rumble is not Off.
// Input owns the setting and the pad (ThreeDsRecomp::Input::ResolveRumble);
// this file owns which events exist and how strong each one is. Reading
// changes nothing in the guest.

#include "three_ds_input.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace oot3d::recomp::a32 {
class MemoryBus;
} // namespace oot3d::recomp::a32

namespace Oot3dNativeGame {

// gSaveContext (code.bin data) and the fields the events read.
inline constexpr uint32_t kOot3dRumbleSaveContext = 0x00587958U;
inline constexpr uint32_t kOot3dRumbleSaveHealth = 0x44U;      // s16, 16 per heart
inline constexpr uint32_t kOot3dRumbleSaveGameMode = 0x14E4U;  // s32, 0 = gameplay
// PlayState: scene id, and the player list head of the actor context
// (context 0x208C, lists from +0x0C, category 2, head +4).
inline constexpr uint32_t kOot3dRumblePlayScene = 0x104U;       // s16
inline constexpr uint32_t kOot3dRumblePlayPlayerHead = 0x20ACU; // Actor*
// Actor: id (Player is 0), velocity.y and bgCheckFlags (decomp layout).
inline constexpr uint32_t kOot3dRumbleActorId = 0x00U;          // s16
inline constexpr uint32_t kOot3dRumbleActorVelocityY = 0x64U;   // f32, units per update
inline constexpr uint32_t kOot3dRumbleActorBgCheckFlags = 0x90U; // u16
inline constexpr uint16_t kOot3dBgCheckGround = 0x0001U;
inline constexpr uint16_t kOot3dBgCheckWater = 0x0020U;

// A fall faster than this (units per update, downwards) lands hard.
inline constexpr float kOot3dHardLandingSpeed = 8.0F;

struct Oot3dRumbleSample {
  // Normal gameplay (game mode 0) with the Player actor present.
  bool Gameplay = false;
  uint32_t PlayState = 0;
  uint32_t Player = 0;
  int16_t Scene = 0;
  int16_t Health = 0;
  uint16_t BgCheckFlags = 0;
  float VelocityY = 0.0F;
};

enum class Oot3dRumbleEventKind : uint8_t { Damage, HardLanding };

struct Oot3dRumbleEvent {
  Oot3dRumbleEventKind Kind = Oot3dRumbleEventKind::Damage;
  // Damage: health lost (16 per heart). HardLanding: the fastest fall of the
  // airborne stretch (units per update, positive).
  float Amount = 0.0F;
};

// Reads one sample; Gameplay stays false when a read fails or the game is
// not in normal gameplay with a player.
bool ReadOot3dRumbleSample(oot3d::recomp::a32::MemoryBus &memory, uint32_t playState,
                           Oot3dRumbleSample *sample);

// Turns consecutive samples into events. The first sample, and any change of
// PlayState, scene or Player actor, only sets the baseline, so loading a file
// or a savestate, a scene change or a respawn is never an event. Landing in
// water is not a hard landing.
class Oot3dRumbleEventTracker {
 public:
  std::vector<Oot3dRumbleEvent> Observe(const Oot3dRumbleSample &sample);
  void Reset() noexcept;

 private:
  std::optional<Oot3dRumbleSample> mPrevious;
  bool mAirborne = false;
  float mFastestFall = 0.0F;
};

// How strong and long each event's rumble is, before the user's strength.
[[nodiscard]] ThreeDsRecomp::Input::RumblePulse Oot3dRumblePulse(const Oot3dRumbleEvent &event) noexcept;

} // namespace Oot3dNativeGame
