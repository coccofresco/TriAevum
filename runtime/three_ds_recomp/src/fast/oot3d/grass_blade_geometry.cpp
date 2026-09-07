#include "fast/oot3d/grass_blade_geometry.h"

#include <algorithm>

namespace Fast::Oot3d {
namespace {

GrassBladeGeometryVertex BladeVertexAt(
    const GrassBladeGeometryInput& input, float heightFactor,
    float widthSign) {
    const float bendFactor =
        heightFactor * (0.65F + 0.35F * heightFactor);
    const float widthFactor = heightFactor >= 1.0F
        ? 0.0F
        : 1.0F - 0.75F * heightFactor;
    return {
        {
            input.Base[0] + input.Bend[0] * bendFactor +
                input.WidthAxis[0] * input.HalfWidth * widthFactor *
                    widthSign,
            input.Base[1] + input.Height * heightFactor,
            input.Base[2] + input.Bend[1] * bendFactor +
                input.WidthAxis[1] * input.HalfWidth * widthFactor *
                    widthSign,
        },
        heightFactor,
    };
}

} // namespace

uint32_t GrassBladeVertexCount(uint8_t segments) noexcept {
    const uint32_t clamped = std::clamp<uint32_t>(
        segments, kMinimumGrassBladeSegments,
        kMaximumGrassBladeSegments);
    return 3U + (clamped - 1U) * 6U;
}

void AppendGrassBladeGeometry(
    std::vector<GrassBladeGeometryVertex>& vertices,
    const GrassBladeGeometryInput& input) {
    const uint32_t segments = std::clamp<uint32_t>(
        input.Segments, kMinimumGrassBladeSegments,
        kMaximumGrassBladeSegments);
    vertices.reserve(vertices.size() + GrassBladeVertexCount(
        static_cast<uint8_t>(segments)));
    for (uint32_t segment = 0U; segment < segments; ++segment) {
        const float lowerFactor =
            static_cast<float>(segment) / static_cast<float>(segments);
        const float upperFactor =
            static_cast<float>(segment + 1U) /
            static_cast<float>(segments);
        const auto lowerLeft =
            BladeVertexAt(input, lowerFactor, -1.0F);
        const auto lowerRight =
            BladeVertexAt(input, lowerFactor, 1.0F);
        if (segment + 1U == segments) {
            const auto tip = BladeVertexAt(input, 1.0F, 0.0F);
            vertices.insert(vertices.end(),
                            {lowerLeft, lowerRight, tip});
            continue;
        }
        const auto upperLeft =
            BladeVertexAt(input, upperFactor, -1.0F);
        const auto upperRight =
            BladeVertexAt(input, upperFactor, 1.0F);
        vertices.insert(
            vertices.end(),
            {lowerLeft, lowerRight, upperRight,
             lowerLeft, upperRight, upperLeft});
    }
}

} // namespace Fast::Oot3d
