#pragma once

#include <array>
#include <cstdint>

namespace Oot3dNativeGame {

// Persist alongside guest memory: the renderer buffers already contain the
// projected positions, so forgetting their originals applies the layout twice.
struct TopScreenPauseProjectionState {
  struct TrackedPosition {
    std::uint32_t Address = 0;
    std::uint32_t OriginalBits = 0;
    std::uint32_t LastWrittenBits = 0;
    bool Valid = false;
    bool operator==(const TrackedPosition&) const = default;
  };

  // Host minimap visibility replaces payload storage 0x005D66AC.
  bool AlternatePage = true;
  float OffsetX = 0.0F;
  float OffsetY = 0.0F;
  bool NativeQuestGate = false;
  bool QuestDrawModelAdjusted = false;
  TrackedPosition MapX;
  TrackedPosition MapY;
  std::array<TrackedPosition, 4U * 64U> IconX;
  bool operator==(const TopScreenPauseProjectionState&) const = default;
};

} // namespace Oot3dNativeGame
