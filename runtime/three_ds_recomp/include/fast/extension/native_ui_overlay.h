#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace Fast::Extension {

// An upscaler changes the extent, so it cannot resolve the scene before native
// UI the way TAA and SMAA do. When the display also carries its scene before
// that UI, the upscaler reconstructs that scene and the display's UI is laid
// over its output: an output texel takes the display texels of its bilinear
// footprint where the UI changed them (the display differs from its scene
// before UI) and keeps the upscaled scene elsewhere. The UI is never an
// upscaler input, so it neither ghosts with the scene's motion nor shifts with
// its jitter; it keeps the display's resolution.

// The display texel at the top-left of output texel `pixel`'s bilinear
// footprint along one axis, and the weight of the next texel.
struct NativeUiOverlayTap {
    int32_t First = 0;
    float Fraction = 0.0F;
};

[[nodiscard]] NativeUiOverlayTap ResolveNativeUiOverlayTap(
    uint32_t pixel, uint32_t extent, uint32_t displayExtent) noexcept;

// The exact covered pixel centres of a (possibly fractional) viewport.
// SourceOffset/Extent retain its original sampling phase; the final scanout
// uses Viewport, so the UI is sampled once rather than filtered again.
struct NativeUiOverlayGrid {
    bool Direct = false;
    std::array<uint32_t, 2> Extent{}; // rotated physical image dimensions
    std::array<float, 4> Viewport{}; // upright x, y, width, height
    std::array<float, 2> SourceExtent{}, SourceOffset{};
};
[[nodiscard]] NativeUiOverlayGrid ResolveNativeUiOverlayGrid(
    bool preserveSampling, uint32_t upscaledWidth, uint32_t upscaledHeight,
    float x, float y, float width, float height, bool flipY) noexcept;

// Push constants of the overlay shader.
struct NativeUiOverlayConstants {
    uint32_t Width = 0U;
    uint32_t Height = 0U;
    uint32_t DisplayWidth = 0U;
    uint32_t DisplayHeight = 0U;
    // The upscaled colour is linear: the display's UI is decoded from sRGB.
    uint32_t LinearOutput = 0U;
    uint32_t ResampleUpscaled = 0U;
    float SourceExtent[2] = {};
    float SourceOffset[2] = {};
};
static_assert(sizeof(NativeUiOverlayConstants) == 40);

// Bindings: 0 display with UI, 1 display before UI, 2 upscaled colour
// (sampled); 3 RGBA16F composed colour (storage); 4 sampler.
[[nodiscard]] std::string BuildNativeUiOverlayShader();

} // namespace Fast::Extension
