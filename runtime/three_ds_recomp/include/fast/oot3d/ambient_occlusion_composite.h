#pragma once

#include <array>
#include <string_view>

namespace Fast::Oot3d {

// Normal-guide alpha reserves [0.5, 1.0] for the fraction of the original
// PICA diffuse lighting that is ambient. The legacy alpha value 1.0 therefore
// keeps full-color CACAO as a deterministic fallback for shaders that do not
// expose a pre-TEV ambient contribution.
[[nodiscard]] float EncodeAmbientOcclusionResponse(
    float response) noexcept;
[[nodiscard]] float DecodeAmbientOcclusionResponse(
    float normalGuideAlpha) noexcept;
[[nodiscard]] float ResolveAmbientOcclusionVisibility(
    float cacaoVisibility,
    float normalGuideAlpha) noexcept;
// Alpha marks the dedicated RGB guide as valid. Invalid/missing guide data
// falls back to normal-guide alpha, preserving original frontend shaders.
[[nodiscard]] std::array<float, 3>
ResolveAmbientOcclusionVisibilityRgb(
    float cacaoVisibility,
    float normalGuideAlpha,
    const std::array<float, 4>& ambientGuide) noexcept;
// Active scene-wide CACAO contract. Coverage is one for 3D scene fragments,
// zero for clear/HUD pixels, and fractional under translucent overlays.
[[nodiscard]] float ResolveSceneAmbientOcclusionVisibility(
    float cacaoVisibility, float sceneCoverage) noexcept;

// Shared by direct scanout and the temporal scene composite so both paths
// apply the exact same scene-domain visibility equation.
[[nodiscard]] std::string_view
AmbientOcclusionCompositeShaderLibrary() noexcept;

} // namespace Fast::Oot3d
