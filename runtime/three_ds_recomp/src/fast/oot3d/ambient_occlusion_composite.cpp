#include "fast/oot3d/ambient_occlusion_composite.h"

#include <algorithm>
#include <cmath>

namespace Fast::Oot3d {
namespace {

float FiniteSaturate(float value, float fallback) noexcept {
    return std::isfinite(value)
        ? std::clamp(value, 0.0F, 1.0F)
        : fallback;
}

} // namespace

float EncodeAmbientOcclusionResponse(
    float response) noexcept {
    return 0.5F +
        FiniteSaturate(response, 1.0F) * 0.5F;
}

float DecodeAmbientOcclusionResponse(
    float normalGuideAlpha) noexcept {
    const float alpha =
        FiniteSaturate(normalGuideAlpha, 1.0F);
    return std::clamp(alpha * 2.0F - 1.0F,
                      0.0F, 1.0F);
}

float ResolveAmbientOcclusionVisibility(
    float cacaoVisibility,
    float normalGuideAlpha) noexcept {
    const float visibility =
        FiniteSaturate(cacaoVisibility, 1.0F);
    const float response =
        DecodeAmbientOcclusionResponse(
            normalGuideAlpha);
    return 1.0F + (visibility - 1.0F) * response;
}

std::array<float, 3>
ResolveAmbientOcclusionVisibilityRgb(
    float cacaoVisibility,
    float normalGuideAlpha,
    const std::array<float, 4>& ambientGuide) noexcept {
    const float visibility =
        FiniteSaturate(cacaoVisibility, 1.0F);
    const float fallback =
        DecodeAmbientOcclusionResponse(normalGuideAlpha);
    const bool exact =
        FiniteSaturate(ambientGuide[3], 0.0F) >= 0.5F;
    std::array<float, 3> result{};
    for (size_t channel = 0; channel < result.size(); ++channel) {
        const float response = exact
            ? FiniteSaturate(ambientGuide[channel], fallback)
            : fallback;
        result[channel] =
            1.0F + (visibility - 1.0F) * response;
    }
    return result;
}

float ResolveSceneAmbientOcclusionVisibility(
    float cacaoVisibility, float sceneCoverage) noexcept {
    const float visibility =
        FiniteSaturate(cacaoVisibility, 1.0F);
    const float coverage =
        FiniteSaturate(sceneCoverage, 0.0F);
    return std::lerp(1.0F, visibility, coverage);
}

std::string_view
AmbientOcclusionCompositeShaderLibrary() noexcept {
    return R"glsl(
float oot3d_ambient_occlusion_response(
    float normal_guide_alpha) {
    return clamp(normal_guide_alpha * 2.0 - 1.0,
                 0.0, 1.0);
}

float oot3d_ambient_occlusion_visibility(
    float cacao_visibility_value,
    float normal_guide_alpha) {
    float response = oot3d_ambient_occlusion_response(
        normal_guide_alpha);
    return mix(1.0, clamp(cacao_visibility_value, 0.0, 1.0),
               response);
}

vec3 oot3d_ambient_occlusion_visibility_rgb(
    float cacao_visibility_value,
    float normal_guide_alpha,
    vec4 ambient_guide) {
    float fallback = oot3d_ambient_occlusion_response(
        normal_guide_alpha);
    vec3 response = ambient_guide.a >= 0.5
        ? clamp(ambient_guide.rgb, vec3(0.0), vec3(1.0))
        : vec3(fallback);
    return mix(vec3(1.0),
               vec3(clamp(cacao_visibility_value, 0.0, 1.0)),
               response);
}

float oot3d_scene_ambient_occlusion_visibility(
    float cacao_visibility_value,
    float scene_coverage) {
    return mix(1.0,
               clamp(cacao_visibility_value, 0.0, 1.0),
               clamp(scene_coverage, 0.0, 1.0));
}
)glsl";
}

} // namespace Fast::Oot3d
