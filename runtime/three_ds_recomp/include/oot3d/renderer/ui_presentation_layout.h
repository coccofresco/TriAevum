#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

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

} // namespace Oot3d::Renderer
