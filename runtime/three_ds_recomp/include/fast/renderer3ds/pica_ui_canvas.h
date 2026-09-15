#pragma once

#include <cstdint>

#include "fast/renderer3ds/pica_composition.h"
#include "oot3d/renderer/ui_presentation_layout.h"

namespace Fast::Renderer3ds {

struct PicaRasterCanvas {
    float X = 0;
    float Y = 0;
    float ScaleX = 1;
    float ScaleY = 1;
};

// Native framebuffer coordinates include the LCD rotation/sampling convention.
// Fit in that coordinate system, not in host logical screen coordinates.
// Ownership, not an orthographic matrix or shader mode, distinguishes UI.
inline PicaRasterCanvas ResolvePicaRasterCanvas(
    PicaCompositionDomain domain, uint32_t nativeWidth, uint32_t nativeHeight,
    uint32_t targetWidth, uint32_t targetHeight) noexcept {
    if (!nativeWidth || !nativeHeight || !targetWidth || !targetHeight) return {};
    if (domain == PicaCompositionDomain::Ui) {
        const auto fit = ::Oot3d::Renderer::FitUiPresentation(
            targetWidth, targetHeight, static_cast<float>(nativeWidth),
            static_cast<float>(nativeHeight));
        return {fit.X, fit.Y, fit.Width / nativeWidth, fit.Height / nativeHeight};
    }
    return {0, 0, static_cast<float>(targetWidth) / nativeWidth,
            static_cast<float>(targetHeight) / nativeHeight};
}

} // namespace Fast::Renderer3ds
