#pragma once

#include <cstdint>

namespace Fast::Extension {

enum class PointShadowMode : uint8_t {
    Off,
    CubeMaps,
};

// Shadows of dynamic point lights (native local lights, Navi and fairies).
// Each selected light renders a cube map whose far plane is its own radius
// scaled by Range, so a light shadows only within its reach.
struct PointShadowSettings {
    PointShadowMode Mode = PointShadowMode::Off;
    // Texels per cube face.
    uint32_t Resolution = 512U;
    // Lights shadowed at once, nearest to the camera first.
    uint8_t MaxLights = 4U;
    // Fraction of each light's radius that its shadows cover.
    float Range = 1.0F;
    // Fraction of the light an occluder removes.
    float Strength = 1.0F;

    bool operator==(const PointShadowSettings&) const = default;
};

[[nodiscard]] constexpr bool IsPointShadowResolution(uint32_t resolution) noexcept {
    return resolution == 256U || resolution == 512U || resolution == 1024U;
}

} // namespace Fast::Extension
