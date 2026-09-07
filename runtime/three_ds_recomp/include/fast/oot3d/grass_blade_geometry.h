#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace Fast::Oot3d {

inline constexpr uint8_t kMinimumGrassBladeSegments = 1U;
inline constexpr uint8_t kMaximumGrassBladeSegments = 12U;

struct GrassBladeGeometryVertex {
    std::array<float, 3> Position{};
    float HeightFactor = 0.0F;
};

struct GrassBladeGeometryInput {
    std::array<float, 3> Base{};
    float HalfWidth = 0.0F;
    float Height = 0.0F;
    std::array<float, 2> WidthAxis{1.0F, 0.0F};
    std::array<float, 2> Bend{};
    uint8_t Segments = 2U;
};

[[nodiscard]] uint32_t GrassBladeVertexCount(uint8_t segments) noexcept;

void AppendGrassBladeGeometry(
    std::vector<GrassBladeGeometryVertex>& vertices,
    const GrassBladeGeometryInput& input);

} // namespace Fast::Oot3d
