#pragma once

#include "fast/oot3d/pica_surface_coordinates.h"

#include <array>
#include <cstdint>
#include <string>

namespace Fast::Oot3d {

// PICA surfaces are stored a quarter turn from the screen (and mirrored when
// the display transfer flips Y; PicaSurfaceCoordinates). A temporal upscaler is
// shown the frame the way the screen shows it: its colour and guides are
// turned upright, it reconstructs upright, and its output is turned back onto
// the surface the effect graph declared. Anything layered on the upscaler (a
// model run on its output) sees the picture upright as well.

// The upright extent of a surface: its axes swap.
[[nodiscard]] constexpr std::array<uint32_t, 2> UprightExtent(uint32_t width, uint32_t height) noexcept {
    return {height, width};
}

// The surface texel an upright texel shows: PresentationToStorage at the
// texel centre, exactly, for a surface of `width` x `height` texels.
[[nodiscard]] constexpr std::array<uint32_t, 2> UprightToSurfaceTexel(std::array<uint32_t, 2> upright, uint32_t width,
                                                                      uint32_t height,
                                                                      PicaSurfaceCoordinates coordinates) noexcept {
    return {width - 1U - upright[1], coordinates.FlipY ? height - 1U - upright[0] : upright[0]};
}

// The upright texel that shows surface texel `surface` (the inverse).
[[nodiscard]] constexpr std::array<uint32_t, 2> SurfaceToUprightTexel(std::array<uint32_t, 2> surface, uint32_t width,
                                                                      uint32_t height,
                                                                      PicaSurfaceCoordinates coordinates) noexcept {
    return {coordinates.FlipY ? height - 1U - surface[1] : surface[1], width - 1U - surface[0]};
}

// A displacement on the surface axes (a motion vector in UV, a jitter offset
// in pixels) on the upright axes. UV units carry over: each upright axis is
// as long as the surface axis it came from.
[[nodiscard]] constexpr std::array<float, 2> UprightOffset(std::array<float, 2> surface,
                                                           PicaSurfaceCoordinates coordinates) noexcept {
    return {coordinates.FlipY ? -surface[1] : surface[1], -surface[0]};
}

// Push constants of both orientation shaders: the extent written, the extent
// read and the surface's Y flip.
struct UpscalerOrientationConstants {
    uint32_t Width = 0U;
    uint32_t Height = 0U;
    uint32_t SourceWidth = 0U;
    uint32_t SourceHeight = 0U;
    uint32_t FlipY = 0U;
    uint32_t Padding[3] = {};
};
static_assert(sizeof(UpscalerOrientationConstants) == 32);

// Surface -> upright. Bindings: 0 colour, 1 depth, 2 motion, 3 reactive
// (sampled); 4 RGBA16F colour, 5 R32F depth, 6 RGBA16F motion (turned), 7 R16F
// reactive (storage); 8 sampler.
[[nodiscard]] std::string BuildUpscalerUprightInputShader();
// Upright -> surface. Bindings: 0 upright colour (sampled), 1 RGBA16F surface
// colour (storage), 2 sampler.
[[nodiscard]] std::string BuildUpscalerUprightOutputShader();

} // namespace Fast::Oot3d
