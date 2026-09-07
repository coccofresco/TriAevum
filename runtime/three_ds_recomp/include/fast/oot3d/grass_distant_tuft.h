#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace Fast::Oot3d {

inline float GrassTuftWeight(float normalizedDistance, float start, float end = 1.0F) noexcept {
    const float t = start < end
        ? std::clamp((normalizedDistance - start) / (end - start), 0.0F, 1.0F)
        : (normalizedDistance >= start ? 1.0F : 0.0F);
    return t * t * (3.0F - 2.0F * t);
}

inline float GrassTuftCoverage(float weight, uint8_t blades) noexcept {
    return 1.0F + (std::max<uint8_t>(blades, 1U) - 1U) * weight;
}

inline float GrassTuftMeanGrowth(float weight) noexcept {
    if (weight <= 0) return 0;
    if (weight >= 1) return 1;
    if (weight < 0.01F) return weight*weight*(0.5F+weight*(1.0F/6.0F+weight*(1.0F/12.0F+weight*0.05F)));
    // Integral over stable switch points c in [0,w] of (w-c)/(1-c).
    return weight + (1.0F - weight) * std::log1p(-weight);
}

inline float GrassTuftRetentionScale(float weight, uint8_t blades, float density) noexcept {
    return (1.0F + (density - 1.0F) * weight) /
        (1.0F + (std::max<uint8_t>(blades, 1U) - 1U) * GrassTuftMeanGrowth(weight));
}

inline float GrassVisibilityFade(float retention, float stableVisibility, float softness) noexcept {
    if (!(retention > 0.0F) || stableVisibility > retention) return 0.0F;
    if (softness <= 0.0F || retention >= 1.0F) return 1.0F;
    return GrassTuftWeight(retention - stableVisibility, 0.0F,
        std::min(retention * softness, 1.0F - retention));
}

inline float GrassDistanceFade(float distance, float drawDistance, float fraction) noexcept {
    return 1.0F - GrassTuftWeight(distance, drawDistance * (1.0F - fraction), drawDistance);
}

// A single cutout quad represents several distant silhouettes. The root stays
// at an accepted native-mask anchor; the footprint grows while instance
// retention falls by the same factor. This is a visual LOD, not new placement.
inline constexpr std::string_view kGrassDistantTuftShader = R"glsl(
float grass_tuft_weight(float normalized_distance, float start, float end) {
    float t = start < end ? clamp((normalized_distance-start)/(end-start),0.0,1.0)
                          : step(start, normalized_distance);
    return t*t*(3.0-2.0*t);
}
float grass_tuft_mean_growth(float weight) {
    if (weight<=0.0) return 0.0;
    if (weight>=1.0) return 1.0;
    if (weight<0.01) return weight*weight*(0.5+weight*(1.0/6.0+weight*(1.0/12.0+weight*0.05)));
    return weight+(1.0-weight)*log(1.0-weight);
}
float grass_tuft_retention_scale(float weight, float blades, float density) {
    return (1.0+(density-1.0)*weight)/(1.0+(blades-1.0)*grass_tuft_mean_growth(weight));
}
float grass_lod_choice(float stable_visibility) {
    uint value=uint(stable_visibility*16777216.0);
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return float(value >> 8u)/16777216.0;
}
float grass_density_retention(float normalized_distance, float start, float far_density) {
    if (normalized_distance <= start) return 1.0;
    if (normalized_distance > 1.0) return far_density/(normalized_distance*normalized_distance);
    float remaining=clamp((1.0-normalized_distance)/max(1.0-start,1.0e-6),0.0,1.0);
    float squared=remaining*remaining;
    return far_density+(1.0-far_density)*squared*squared;
}
float grass_visibility_fade(float retention, float stable_visibility, float softness) {
    if (retention<=0.0 || stable_visibility>retention) return 0.0;
    if (softness<=0.0 || retention>=1.0) return 1.0;
    return grass_tuft_weight(retention-stable_visibility,0.0,min(retention*softness,1.0-retention));
}
bool grass_tuft_covered(vec4 sample_data, uint blades, float spread) {
    float weight = (sample_data.z-1.0)/max((float(blades)-1.0)*spread,0.000001);
    float x = sample_data.x * sample_data.z;
    for (uint i=0u; i<blades; ++i) {
        float seed = fract(sin(float(i)*17.17 + sample_data.w*6.2831853)*43758.5453);
        float height = mix(1.0, 0.75+0.25*seed, weight);
        float y = sample_data.y / height;
        float center = (float(i)-0.5*(float(blades)-1.0))*2.0*weight*spread;
        center += (seed-0.5)*y*y*weight;
        if (y <= 1.0 && abs(x-center) <= 1.0-y) return true;
    }
    return false;
}
)glsl";

} // namespace Fast::Oot3d
