#pragma once

#include "fast/oot3d/graphics_settings.h"
#include "fast/oot3d/scene_presentation_policy.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Fast::Oot3d {

struct RenderExtent {
    uint32_t Width = 1;
    uint32_t Height = 1;
};

[[nodiscard]] inline float ResolveInternalResolutionScale(
    const GraphicsSettings& settings) {
    const float requested =
        settings.AntiAliasing == AntiAliasingMode::Upscaler
            ? 1.0F / UpscalerScalingFactor(settings.UpscalerMode)
            : settings.InternalResolutionScale;
    return std::clamp(requested, 0.5F, 2.0F);
}

[[nodiscard]] inline RenderExtent ScaleRenderExtent(
    RenderExtent extent, float scale) {
    const float sanitized =
        std::isfinite(scale) ? std::clamp(scale, 0.5F, 2.0F) : 1.0F;
    return {
        std::max(1U, static_cast<uint32_t>(std::lround(
                         static_cast<float>(extent.Width) * sanitized))),
        std::max(1U, static_cast<uint32_t>(std::lround(
                         static_cast<float>(extent.Height) * sanitized)))};
}

[[nodiscard]] inline RenderExtent ResolveNativePicaRenderExtent(
    RenderExtent guestExtent, RenderExtent outputExtent,
    float internalScale) {
    RenderExtent base = guestExtent;
    if (guestExtent.Width == kNativeTopScreenPhysicalWidth &&
        guestExtent.Height == kNativeTopScreenPhysicalHeight &&
        outputExtent.Width != 0U && outputExtent.Height != 0U) {
        // The top LCD is stored rotated: physical X is twice logical height.
        base = {outputExtent.Height * 2U, outputExtent.Width};
    }
    return ScaleRenderExtent(base, internalScale);
}

} // namespace Fast::Oot3d
