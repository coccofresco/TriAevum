#pragma once

#include "oot3d_native_a32_memory.h"

#include <bit>
#include <cstdint>
#include <stdexcept>

namespace Oot3dNativeGame {

inline constexpr std::uint32_t kNativeTrigonometryTable = 0x004DF42CU;

// oot3d_sin_idx8 0x002CFCA0 / oot3d_cos_idx8 0x00338F60: table value plus
// slope x (low byte / 256), each product rounded as VFP vmul/vmla does.
inline float NativeTrigonometry(const NativeA32Memory &memory, std::int32_t angle, bool cosine) {
  const auto index = static_cast<std::uint16_t>(angle);
  const std::uint32_t entry = kNativeTrigonometryTable + (index >> 8U) * 16U;
  std::uint32_t value = 0U, slope = 0U;
  auto &bus = const_cast<NativeA32Memory &>(memory);
  if (!bus.Read32(entry + (cosine ? 4U : 0U), &value) ||
      !bus.Read32(entry + (cosine ? 0xCU : 8U), &slope))
    throw std::runtime_error("TopScreen cannot read the native sine table");
  const float step = static_cast<float>(index & 0xFFU) * (1.0F / 256.0F);
  const float product = std::bit_cast<float>(slope) * step;
  return std::bit_cast<float>(value) + product;
}

} // namespace Oot3dNativeGame
