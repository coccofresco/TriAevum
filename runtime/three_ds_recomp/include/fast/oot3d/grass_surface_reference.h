#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Fast::Oot3d {
// Native geometry has at most 65536 vertices. Two byte barycentric weights
// keep this reference at eight bytes; no lighting value is baked into roots.
inline std::array<uint32_t, 2> PackGrassSurfaceReference(
    const std::array<uint32_t, 3>& indices, float u, float v) {
    if (!std::isfinite(u)) u = 0.0F;
    if (!std::isfinite(v)) v = 0.0F;
    const uint32_t pu = static_cast<uint32_t>(std::lround(std::clamp(u, 0.0F, 1.0F) * 255.0F));
    const uint32_t pv = std::min(255U - pu,
        static_cast<uint32_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F)));
    return {indices[0] | (indices[1] << 16U), indices[2] | (pu << 16U) | (pv << 24U)};
}
} // namespace Fast::Oot3d
