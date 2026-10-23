#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include "oot3d_environment_group.h"

namespace Oot3dNativeGame {

struct SceneEnvironmentTime {
  bool TimeDriven = false;
  uint16_t Clock = 0x8000;
  bool Night = false; // Native flag, diagnostic only; never selects the editor.
  uint32_t Owner = 0; // PlayState identity, for discontinuities.
  std::optional<SceneEnvironmentGroup> Group; // Authored headers, latched per scene visit by the owner.
  uint64_t AuthoredScene = 0; // Scene resource + selected setup; independent of room changes.
};

// EUR Environment_Update (0045DD50), table 00531EFC: nine 6-byte
// {u16 start,end; u8 from,to} records per weather mode (stride 0x36).
// All five modes have the same phase schedule. Phase (setting & 3) == 3
// is night; morning/day/evening are the day profile. Project the native
// component lerp, 1-(end-clock)/(end-start), onto that night membership.
// Weather's second blend does not change this weight: both modes agree.
[[nodiscard]] constexpr float NativeEnvironmentNightWeight(uint16_t clock) noexcept {
  if (clock < 0x2AAC || clock >= 0xD556) return 1.0F;
  if (clock < 0x4000) return float(0x4000 - clock) / float(0x4000 - 0x2AAC);
  if (clock < 0xC001) return 0.0F;
  return 1.0F - float(0xD556 - clock) / float(0xD556 - 0xC001);
}

// Read after Environment_Update. The light-setting lookup consumes the
// effective clock at 00588E58+A8, not necessarily SaveContext+0xC (the
// native update can hold the former). envLightMode/override are at
// PlayState+3190+20/A7. Read-only adapter, also exercised with a fake bus.
template <class Memory>
[[nodiscard]] std::optional<SceneEnvironmentTime> CaptureSceneEnvironmentTime(uint32_t play, Memory& memory) {
  if (!play || play > std::numeric_limits<uint32_t>::max() - 0x3237U) return std::nullopt;
  SceneEnvironmentTime result;
  uint8_t mode = 0, settingOverride = 0;
  uint32_t night = 0;
  if (!memory.Read8(play + 0x31B0U, &mode) || !memory.Read8(play + 0x3237U, &settingOverride) ||
      !memory.Read16(0x00588F00U, &result.Clock) || !memory.Read32(0x00587968U, &night)) return std::nullopt;
  result.TimeDriven = mode == 0 && settingOverride == 0xFF;
  result.Night = night != 0;
  result.Owner = play;
  return result;
}

} // namespace Oot3dNativeGame
