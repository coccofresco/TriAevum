#pragma once

#include <array>
#include <string_view>

namespace Fast::Extension {

// Normal-guide alpha reserves [0.5, 1.0] for the fraction of the original
// PICA diffuse lighting that is ambient. The legacy alpha value 1.0 therefore
// keeps full-color AO as a deterministic fallback for shaders that do not
// expose a pre-TEV ambient contribution.
[[nodiscard]] float EncodeAmbientOcclusionResponse(
    float response) noexcept;
[[nodiscard]] float DecodeAmbientOcclusionResponse(
    float normalGuideAlpha) noexcept;
[[nodiscard]] float ResolveAmbientOcclusionVisibility(
    float aoVisibility,
    float normalGuideAlpha) noexcept;
// Alpha marks the dedicated RGB guide as valid. Invalid/missing guide data
// falls back to normal-guide alpha, preserving original frontend shaders.
[[nodiscard]] std::array<float, 3>
ResolveAmbientOcclusionVisibilityRgb(
    float aoVisibility,
    float normalGuideAlpha,
    const std::array<float, 4>& ambientGuide) noexcept;
// Active scene-wide AO contract. Coverage is one for 3D scene fragments,
// zero for clear/HUD pixels, and fractional under translucent overlays.
[[nodiscard]] float ResolveSceneAmbientOcclusionVisibility(
    float aoVisibility, float sceneCoverage) noexcept;

// Shared by direct scanout and the temporal scene composite so both paths
// apply the exact same scene-domain visibility equation.
[[nodiscard]] std::string_view
AmbientOcclusionCompositeShaderLibrary() noexcept;

// AO debug views (MxaoDebugView): the provider writes a scalar in [0, 1] into
// its R32F visibility output, or for a normal view the display colour
// n * 0.5 + 0.5 packed as 8/8/7 bits in the mantissa of a float in [1, 2).
// `mxao_debug_pack_normal(vec3)` packs; `oot3d_ambient_occlusion_debug_color(
// uint view, float value)` gives the colour the scene composite shows.
[[nodiscard]] std::string_view
AmbientOcclusionDebugShaderLibrary() noexcept;

} // namespace Fast::Extension
