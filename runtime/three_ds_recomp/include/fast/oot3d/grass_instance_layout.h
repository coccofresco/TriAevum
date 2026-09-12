#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Fast::Oot3d {

// Shared by CPU fallback, compute expansion and instanced vertex input.
struct GrassInstance {
    std::array<float, 4> BaseHeight{};
    std::array<float, 4> BendAndHalfWidth{};
    std::array<float, 2> WidthAxis{1.0F, 0.0F};
    std::array<float, 4> WorldNormal{0.0F, 1.0F, 0.0F, 0.0F};
    uint32_t SurfaceColor = 0U;
    std::array<uint32_t, 2> SurfaceReference{};
};
static_assert(sizeof(GrassInstance) == 68U);
static_assert(offsetof(GrassInstance, SurfaceColor) == 56U);
static_assert(offsetof(GrassInstance, SurfaceReference) == 60U);

} // namespace Fast::Oot3d
