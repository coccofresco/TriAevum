#pragma once

#include <array>

namespace Oot3dNativeGame {

// SingleScreen 3.0 0x005CAFCC: the six 2D matrix lanes, in native order.
using TopScreenItemsTransform = std::array<float, 6>;

// 3.0 0x005E3CFC..0x005E3D9C prepares quad 2 for the shortcut column.
// It overlaps the native grid; 0x005CB13C aligns its left edge at draw time.
inline constexpr std::array<float, 4> kTopScreenItemsInsetRect{-44.0F, 0.0F, 100.0F, 200.0F};
inline constexpr std::array<float, 4> kTopScreenItemsInsetAtlas{-1.0F, 0.0F, 100.0F, 200.0F};

inline TopScreenItemsTransform TransformTopScreenItemsModel(
    const TopScreenItemsTransform &matrix, float x, float y,
    bool compact = false) noexcept {
  // 0x005E377C..0x005E37AC: only the compact lower pass uses 0.85 / +36.
  // 0x005E3CE0 calls (top=1, wide=1), whose transform scales are both 1.
  const float scaleX = compact ? 0.85F : 1.0F;
  const float offsetX = compact ? 36.0F : 0.0F;
  return {matrix[0] * scaleX, matrix[1] * scaleX,
          offsetX + (matrix[2] + matrix[0] * x + matrix[1] * y) * scaleX,
          matrix[3], matrix[4], matrix[5] + matrix[3] * x + matrix[4] * y};
}

} // namespace Oot3dNativeGame
