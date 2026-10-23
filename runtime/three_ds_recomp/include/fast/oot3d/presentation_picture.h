#pragma once

// The presentation picture: the part of the output (window or screen) the
// game is shown in. The user's aspect ratio choice (GraphicsSettings::Aspect)
// fits a picture of that ratio into the output, centred, with black bars on
// the remaining sides; Free uses the whole output. The scene render targets,
// the scene FOV policy, the scanout, every UI canvas and the pointer mapping
// use the picture, never the output, so a window of any shape shows the same
// composition and the mouse lands where the picture draws.

#include "fast/oot3d/graphics_settings.h"
#include "oot3d/renderer/ui_presentation_layout.h"

#include <cmath>
#include <cstdint>

namespace Fast::Oot3d {

struct PresentationPicture {
    uint32_t X = 0;
    uint32_t Y = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;

    [[nodiscard]] bool Valid() const noexcept {
        return Width > 0U && Height > 0U;
    }
    [[nodiscard]] bool operator==(const PresentationPicture&) const = default;
};

// The ratio a mode asks for, as width / height; 0 for Free (the output's own).
[[nodiscard]] constexpr float AspectRatioValue(AspectRatioMode mode) noexcept {
    switch (mode) {
    case AspectRatioMode::Native3ds:
        return 5.0F / 3.0F;
    case AspectRatioMode::Wide16x9:
        return 16.0F / 9.0F;
    case AspectRatioMode::Ultrawide21x9:
        return 7.0F / 3.0F;
    case AspectRatioMode::Free:
    default:
        return 0.0F;
    }
}

// The picture for an output: the whole output for Free, otherwise the largest
// rectangle of the chosen ratio that fits it, centred (the same fit and
// rounding the UI canvases use). An empty output gives an invalid picture.
[[nodiscard]] inline PresentationPicture ResolvePresentationPicture(
    uint32_t outputWidth, uint32_t outputHeight, AspectRatioMode mode) noexcept {
    if (outputWidth == 0U || outputHeight == 0U) return {};
    const float ratio = AspectRatioValue(mode);
    if (!(ratio > 0.0F)) return {0U, 0U, outputWidth, outputHeight};
    const auto fitted = ::Oot3d::Renderer::FitUiPresentationViewport(
        outputWidth, outputHeight, ratio, 1.0F);
    if (!fitted.Valid()) return {0U, 0U, outputWidth, outputHeight};
    return {fitted.X, fitted.Y, fitted.Width, fitted.Height};
}

// The output size a windowed output takes when an aspect ratio is chosen: the
// width stays, the height follows the ratio (rounded to a whole pixel), so the
// window shows the picture without bars until the user resizes it. Free and
// the fullscreen modes (the display owns their size) leave the size alone.
// Returns true when the size changed.
[[nodiscard]] inline bool SnapWindowedOutputToAspect(
    GraphicsSettings& settings) noexcept {
    if (settings.Window != WindowMode::Windowed) return false;
    const float ratio = AspectRatioValue(settings.Aspect);
    if (!(ratio > 0.0F) || settings.OutputWidth == 0U) return false;
    const auto height = static_cast<uint32_t>(std::lround(
        static_cast<float>(settings.OutputWidth) / ratio));
    if (height < 240U || height == settings.OutputHeight) return false;
    settings.OutputHeight = height;
    return true;
}

} // namespace Fast::Oot3d
