#include "fast/oot3d/grass_wind.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Fast::Oot3d {

std::array<float, 2> EvaluateGrassWind(
    const InteractiveGrassSettings& settings,
    const std::array<float, 3>& worldPosition, float anchorPhase,
    double visualSeconds) {
    if (settings.WindStrength <= 0.0F || settings.MaximumBend <= 0.0F)
        return {};

    const float direction = settings.WindDirectionDegrees *
                            std::numbers::pi_v<float> / 180.0F;
    const float windX = std::cos(direction);
    const float windZ = std::sin(direction);
    const float lateralX = -windZ;
    const float lateralZ = windX;
    const float spatial =
        (worldPosition[0] * windX + worldPosition[2] * windZ) *
        0.01F * settings.WindSpatialScale;
    const float time = static_cast<float>(visualSeconds);
    const float randomPhase =
        anchorPhase * std::clamp(settings.WindRandomness, 0.0F, 1.0F);
    const float primary = std::sin(
        time * settings.WindSpeed + spatial + randomPhase);
    const float gustPhase = time * settings.WindGustFrequency *
                                (2.0F * std::numbers::pi_v<float>) +
                            spatial * 0.21F + randomPhase * 0.37F;
    const float gust = 0.5F + 0.5F * std::sin(gustPhase);
    const float turbulence = std::sin(
        time * (settings.WindSpeed * 1.71F + 0.31F) -
        worldPosition[0] * 0.017F + worldPosition[2] * 0.013F +
        randomPhase * 2.13F);

    const float directionalAmount = settings.WindStrength *
        (0.55F + primary * 0.30F +
         gust * settings.WindGustStrength * 0.45F);
    const float lateralAmount = settings.WindStrength *
        settings.WindTurbulence * turbulence * 0.35F;
    float bendX = windX * directionalAmount + lateralX * lateralAmount;
    float bendZ = windZ * directionalAmount + lateralZ * lateralAmount;
    const float length = std::sqrt(bendX * bendX + bendZ * bendZ);
    if (length > settings.MaximumBend && length > 1.0e-6F) {
        const float scale = settings.MaximumBend / length;
        bendX *= scale;
        bendZ *= scale;
    }
    return {bendX, bendZ};
}

} // namespace Fast::Oot3d
