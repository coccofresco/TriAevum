#pragma once
#include <cstdint>
#include <algorithm>
#include <bit>

namespace Fast::Oot3d {
inline constexpr uint32_t kGrassMidrangeClusterCapacity = 10000;
inline constexpr uint32_t kGrassDefaultAdaptiveClusterCapacity = 128;
inline constexpr uint32_t kGrassBoundaryClusterCapacity = 50;

// Share a small number of draw calls across occupancies. Unoccupied children
// are rejected before loading root attributes; no accepted root is removed.
inline constexpr uint32_t GrassClusterDrawCapacity(uint32_t members) {
    return std::min(std::bit_ceil(members), kGrassMidrangeClusterCapacity);
}
} // namespace Fast::Oot3d
