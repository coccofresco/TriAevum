#pragma once

#include <cstdint>
#include <algorithm>
#include <cmath>

#include "fast/renderer3ds/pica_composition.h"
#include "oot3d/renderer/ui_presentation_layout.h"

namespace Fast::Renderer3ds {

struct PicaRasterCanvas {
    float X = 0;
    float Y = 0;
    float ScaleX = 1;
    float ScaleY = 1;
};

struct PicaCanvasClip {
    int32_t X = 0, Y = 0;
    uint32_t Width = 0, Height = 0;
};

inline PicaCanvasClip ClipToPicaCanvas(
    PicaCanvasClip scissor, const PicaRasterCanvas& canvas,
    uint32_t nativeWidth, uint32_t nativeHeight) noexcept {
    const auto x1 = std::max<int64_t>(scissor.X, static_cast<int64_t>(std::ceil(canvas.X)));
    const auto y1 = std::max<int64_t>(scissor.Y, static_cast<int64_t>(std::ceil(canvas.Y)));
    const auto x2 = std::min<int64_t>(int64_t(scissor.X) + scissor.Width,
        static_cast<int64_t>(std::floor(canvas.X + nativeWidth * canvas.ScaleX)));
    const auto y2 = std::min<int64_t>(int64_t(scissor.Y) + scissor.Height,
        static_cast<int64_t>(std::floor(canvas.Y + nativeHeight * canvas.ScaleY)));
    return {static_cast<int32_t>(x1), static_cast<int32_t>(y1),
            static_cast<uint32_t>(std::max<int64_t>(0, x2 - x1)),
            static_cast<uint32_t>(std::max<int64_t>(0, y2 - y1))};
}

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
