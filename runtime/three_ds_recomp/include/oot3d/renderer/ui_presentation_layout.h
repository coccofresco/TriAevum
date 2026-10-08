#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "fast/renderer3ds/pica_composition.h"
#include "fast/renderer3ds/pica_ui_canvas.h"

namespace Oot3d::Renderer {

struct UiPresentationRect {
    float X = 0.0F;
    float Y = 0.0F;
    float Width = 0.0F;
    float Height = 0.0F;

    [[nodiscard]] bool Valid() const noexcept {
        return Width > 0.0F && Height > 0.0F;
    }
};

struct UiPresentationViewport {
    std::uint32_t X = 0U;
    std::uint32_t Y = 0U;
    std::uint32_t Width = 0U;
    std::uint32_t Height = 0U;

    [[nodiscard]] bool Valid() const noexcept {
        return Width > 0U && Height > 0U;
    }
};

struct UiPresentationPoint {
    float X = 0.0F;
    float Y = 0.0F;
    bool Inside = false;
};

[[nodiscard]] inline UiPresentationRect FitUiPresentation(
    std::uint32_t targetWidth, std::uint32_t targetHeight, float logicalWidth,
    float logicalHeight) noexcept {
    if (targetWidth == 0U || targetHeight == 0U ||
        !(logicalWidth > 0.0F) || !(logicalHeight > 0.0F)) {
        return {};
    }

    float presentWidth = static_cast<float>(targetWidth);
    float presentHeight = presentWidth * logicalHeight / logicalWidth;
    if (presentHeight > static_cast<float>(targetHeight)) {
        presentHeight = static_cast<float>(targetHeight);
        presentWidth = presentHeight * logicalWidth / logicalHeight;
    }
    return {
        (static_cast<float>(targetWidth) - presentWidth) * 0.5F,
        (static_cast<float>(targetHeight) - presentHeight) * 0.5F,
        presentWidth,
        presentHeight,
    };
}

[[nodiscard]] inline UiPresentationRect FillUiPresentation(
    std::uint32_t targetWidth, std::uint32_t targetHeight, float logicalWidth,
    float logicalHeight) noexcept {
    if (targetWidth == 0U || targetHeight == 0U ||
        !(logicalWidth > 0.0F) || !(logicalHeight > 0.0F)) {
        return {};
    }

    float presentWidth = static_cast<float>(targetWidth);
    float presentHeight = presentWidth * logicalHeight / logicalWidth;
    if (presentHeight < static_cast<float>(targetHeight)) {
        presentHeight = static_cast<float>(targetHeight);
        presentWidth = presentHeight * logicalWidth / logicalHeight;
    }
    return {
        (static_cast<float>(targetWidth) - presentWidth) * 0.5F,
        (static_cast<float>(targetHeight) - presentHeight) * 0.5F,
        presentWidth,
        presentHeight,
    };
}

[[nodiscard]] inline UiPresentationViewport FitUiPresentationViewport(
    std::uint32_t targetWidth, std::uint32_t targetHeight, float logicalWidth,
    float logicalHeight) noexcept {
    const UiPresentationRect fitted =
        FitUiPresentation(targetWidth, targetHeight, logicalWidth,
                          logicalHeight);
    if (!fitted.Valid()) {
        return {};
    }

    const std::uint32_t width = std::clamp(
        static_cast<std::uint32_t>(std::lround(fitted.Width)), 1U,
        targetWidth);
    const std::uint32_t height = std::clamp(
        static_cast<std::uint32_t>(std::lround(fitted.Height)), 1U,
        targetHeight);
    return {
        (targetWidth - width) / 2U,
        (targetHeight - height) / 2U,
        width,
        height,
    };
}

[[nodiscard]] inline UiPresentationPoint MapUiPresentationPoint(
    std::uint32_t targetWidth, std::uint32_t targetHeight, float logicalWidth,
    float logicalHeight, float targetX, float targetY) noexcept {
    const UiPresentationRect fitted =
        FitUiPresentation(targetWidth, targetHeight, logicalWidth,
                          logicalHeight);
    if (!fitted.Valid() || !std::isfinite(targetX) ||
        !std::isfinite(targetY) || targetX < fitted.X ||
        targetY < fitted.Y || targetX >= fitted.X + fitted.Width ||
        targetY >= fitted.Y + fitted.Height) {
        return {};
    }

    return {
        (targetX - fitted.X) * logicalWidth / fitted.Width,
        (targetY - fitted.Y) * logicalHeight / fitted.Height,
        true,
    };
}

// Native framebuffer coordinates include the LCD rotation/sampling convention.
// Fit in that coordinate system, not in host logical screen coordinates.
// Ownership, not an orthographic matrix or shader mode, distinguishes UI.
// Title-side: it applies this title's presentation convention and placement
// policy, so it stays out of the title-neutral renderer boundary.
// `storageSamplingX` (NativeTopScreenStorageSamplingX): host samples per native sample along storage X relative
// to the native target's own sampling. 1 when the host target keeps it (the
// fit in native coordinates is then proportional); 0.5 for a top-screen
// target rendered at the presented density, whose 480 native columns hold 240
// presented lines (render_resolution_policy.h): the fit then preserves the
// presented proportions and the canvas scales X by half of Y.
[[nodiscard]] inline Fast::Renderer3ds::PicaRasterCanvas ResolvePicaRasterCanvas(
    Fast::Renderer3ds::PicaCompositionDomain domain,
    Fast::Renderer3ds::PicaScreenContent screenContent,
    std::uint32_t nativeWidth, std::uint32_t nativeHeight,
    std::uint32_t targetWidth, std::uint32_t targetHeight,
    float storageSamplingX = 1.0F, float uiFrameExtensionY = 1.0F) noexcept {
    if (!nativeWidth || !nativeHeight || !targetWidth || !targetHeight ||
        !(storageSamplingX > 0.0F) || !(uiFrameExtensionY >= 1.0F)) {
        return {};
    }
    const float fittedNativeWidth = static_cast<float>(nativeWidth) * storageSamplingX;
    // Placed UI, and an opaque picture of its own (a movie frame): the whole
    // native screen at its own proportions, centred. Around a picture the
    // renderer shows black bars (`SurroundPicaCanvas`), never what was drawn
    // behind it. A title whose interface is laid out for a frame longer along
    // native Y than its screen (`uiFrameExtensionY`, > 1) stretches the UI
    // canvas over that frame and narrows the draw's clip space by the same
    // factor (`PicaUiFrameContentCanvas` maps its native coordinates): the
    // native screen keeps its proportions at the frame's centre and the
    // margins beside it stay drawable.
    if (domain == Fast::Renderer3ds::PicaCompositionDomain::Ui ||
        (domain == Fast::Renderer3ds::PicaCompositionDomain::Screen &&
         screenContent == Fast::Renderer3ds::PicaScreenContent::OwnPicture)) {
        const float extension =
            domain == Fast::Renderer3ds::PicaCompositionDomain::Ui ? uiFrameExtensionY : 1.0F;
        const auto fit = FitUiPresentation(
            targetWidth, targetHeight, fittedNativeWidth,
            static_cast<float>(nativeHeight) * extension);
        return {fit.X, fit.Y, fit.Width / nativeWidth, fit.Height / nativeHeight};
    }
    if (domain == Fast::Renderer3ds::PicaCompositionDomain::Screen &&
        screenContent == Fast::Renderer3ds::PicaScreenContent::PictureOverlay) {
        // An overlay with its own transparency (a mask, a glow) covers the
        // target without distortion: native proportions kept, the excess
        // centred, so no part of the frame under it is left uncovered.
        const auto cover = FillUiPresentation(
            targetWidth, targetHeight, fittedNativeWidth,
            static_cast<float>(nativeHeight));
        return {cover.X, cover.Y, cover.Width / nativeWidth,
                cover.Height / nativeHeight};
    }
    // The scene, and screen content that shows the rendered frame: a redraw of
    // the copied frame must land on the pixels it copied (the copy holds the
    // scene as this mapping rasterised it), and a wash reaches every pixel.
    return {0, 0, static_cast<float>(targetWidth) / nativeWidth,
            static_cast<float>(targetHeight) / nativeHeight};
}

// Where native coordinates land inside an extended UI frame (the canvas
// ResolvePicaRasterCanvas stretched over it, `uiFrameExtensionY`): a draw
// whose clip Y is narrowed by 1 / uiFrameExtensionY puts native Y at the
// viewport's centre (`nativeCenterY`) plus its offset at the screen's own
// proportions. Native scissors map through it.
[[nodiscard]] inline Fast::Renderer3ds::PicaRasterCanvas PicaUiFrameContentCanvas(
    const Fast::Renderer3ds::PicaRasterCanvas& frame, float nativeCenterY,
    float uiFrameExtensionY) noexcept {
    if (!(uiFrameExtensionY > 1.0F)) {
        return frame;
    }
    return {frame.X, frame.Y + nativeCenterY * frame.ScaleY * (1.0F - 1.0F / uiFrameExtensionY),
            frame.ScaleX, frame.ScaleY / uiFrameExtensionY};
}

} // namespace Oot3d::Renderer
