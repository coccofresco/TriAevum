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

// The smallest internal scale: an upscaler's Ultra Performance renders a third
// of the output per axis. The user's own render scale stays within 0.5-2.
inline constexpr float kMinimumUpscalerRenderScale = 1.0F / 3.0F;

// An upscaler renders its output divided by its quality's factor, so its
// output is the display extent, never larger.
[[nodiscard]] inline float ResolveInternalResolutionScale(
    const GraphicsSettings& settings) {
    if (settings.AntiAliasing == AntiAliasingMode::Upscaler)
        return std::clamp(1.0F / UpscalerScalingFactor(settings.UpscalerMode),
                          kMinimumUpscalerRenderScale, 1.0F);
    return std::clamp(settings.InternalResolutionScale, 0.5F, 2.0F);
}

[[nodiscard]] inline RenderExtent ScaleRenderExtent(
    RenderExtent extent, float scale) {
    const float sanitized = std::isfinite(scale)
        ? std::clamp(scale, kMinimumUpscalerRenderScale, 2.0F) : 1.0F;
    return {
        std::max(1U, static_cast<uint32_t>(std::lround(
                         static_cast<float>(extent.Width) * sanitized))),
        std::max(1U, static_cast<uint32_t>(std::lround(
                         static_cast<float>(extent.Height) * sanitized)))};
}

// The top LCD target (480x400, stored rotated) holds twice the presented
// lines: the game renders 2x along physical X and its display transfer box
// filters 2:1. At host resolution that second sample per output pixel doubles
// every per-pixel cost. From 480 presented lines (the game's own sample
// count) the host renders the target at the output's density (scale 1 = one
// sample per output pixel on both axes) and the transfer copies
// (SourceAtPresentedDensity); a render scale above 1 supersamples both axes
// evenly. Below 480 lines the game's 2x sampling and filter are kept.
// A title may pad the target's storage width past its 480 samples (MM3D
// allocates 512 and draws a 480-sample viewport): the padding is scaled at the
// density of the visible samples, so the transfer and the draws keep their
// native proportions.
inline constexpr std::uint32_t kNativeTopScreenMaximumStorageWidth = 512U;

[[nodiscard]] inline bool IsNativeTopScreenTarget(RenderExtent guestExtent) noexcept {
    return guestExtent.Width >= kNativeTopScreenPhysicalWidth &&
           guestExtent.Width <= kNativeTopScreenMaximumStorageWidth &&
           guestExtent.Height == kNativeTopScreenPhysicalHeight;
}

// The largest native-aspect (400:240) extent inside an output: the top
// screen of a title that does not widen its projection is presented there.
[[nodiscard]] inline RenderExtent FitNativeTopScreenAspect(RenderExtent output) noexcept {
    if (output.Width == 0U || output.Height == 0U) return output;
    const uint64_t widthAtHeight = (static_cast<uint64_t>(output.Height) * kNativeTopScreenLogicalWidth +
                                    kNativeTopScreenLogicalHeight / 2U) / kNativeTopScreenLogicalHeight;
    if (widthAtHeight <= output.Width)
        return {std::max<uint32_t>(1U, static_cast<uint32_t>(widthAtHeight)), output.Height};
    const uint64_t heightAtWidth = (static_cast<uint64_t>(output.Width) * kNativeTopScreenLogicalHeight +
                                    kNativeTopScreenLogicalWidth / 2U) / kNativeTopScreenLogicalWidth;
    return {output.Width, std::max<uint32_t>(1U, static_cast<uint32_t>(heightAtWidth))};
}

[[nodiscard]] inline bool NativeTopScreenAtPresentedDensity(RenderExtent outputExtent) noexcept {
    return outputExtent.Height >= kNativeTopScreenPhysicalWidth;
}

[[nodiscard]] inline RenderExtent ResolveNativePicaRenderExtent(
    RenderExtent guestExtent, RenderExtent outputExtent,
    float internalScale) {
    RenderExtent base = guestExtent;
    if (IsNativeTopScreenTarget(guestExtent) &&
        outputExtent.Width != 0U && outputExtent.Height != 0U) {
        // Stored rotated: physical X spans the output's height. The visible
        // 480 samples take it; storage padding follows at the same density.
        const uint64_t visibleWidth = static_cast<uint64_t>(outputExtent.Height) *
            (NativeTopScreenAtPresentedDensity(outputExtent) ? 1U : 2U);
        base = {static_cast<uint32_t>(
                    (visibleWidth * guestExtent.Width + kNativeTopScreenPhysicalWidth / 2U) /
                    kNativeTopScreenPhysicalWidth),
                outputExtent.Width};
    }
    return ScaleRenderExtent(base, internalScale);
}

// Host samples per native sample along storage X, relative to the native
// target's own sampling: 0.5 for a top-screen target at the presented density
// (its 480 native columns hold 240 presented lines), otherwise 1. Native
// coordinates fitted with uniform proportions (placed UI, movie pictures)
// correct their X scale by it.
[[nodiscard]] inline float NativeTopScreenStorageSamplingX(
    RenderExtent guestExtent, RenderExtent outputExtent) noexcept {
    return IsNativeTopScreenTarget(guestExtent) && NativeTopScreenAtPresentedDensity(outputExtent) ? 0.5F : 1.0F;
}

} // namespace Fast::Oot3d
