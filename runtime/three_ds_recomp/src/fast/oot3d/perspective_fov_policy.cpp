#include "fast/oot3d/perspective_fov_policy.h"

#include <algorithm>
#include <cmath>

namespace Fast::Oot3d {

PerspectiveFovResult ResolvePerspectiveFov(
    const PerspectiveFovInput& input) noexcept {
    PerspectiveFovResult result;
    result.Frustum = input.Frustum;
    if (!input.Perspective)
        return result;

    const auto& source = input.Frustum;
    const float multiplier =
        std::clamp(input.Multiplier, 1.0F, 1.5F);
    const auto sanitizeExpansion = [](float expansion) {
        return std::isfinite(expansion)
            ? std::max(expansion, 1.0F)
            : 1.0F;
    };
    const float horizontalExpansion = sanitizeExpansion(
        input.Presentation.HorizontalFovExpansion);
    const float verticalExpansion = sanitizeExpansion(
        input.Presentation.VerticalFovExpansion);
    const float verticalHalfExtent =
        std::abs(source.Top - source.Bottom) * 0.5F;
    if (std::isfinite(source.Bottom) &&
        std::isfinite(source.Top) &&
        std::isfinite(source.NearPlane) &&
        std::abs(source.NearPlane) > 1.0e-6F &&
        verticalHalfExtent > 1.0e-6F &&
        multiplier > 1.0F) {
        const float baseHalfFov = std::atan(
            verticalHalfExtent / std::abs(source.NearPlane));
        constexpr float kMaximumHalfFov = 1.55334306F;
        const float targetHalfFov = std::min(
            baseHalfFov * multiplier, kMaximumHalfFov);
        result.PerspectiveScale =
            std::tan(targetHalfFov) / std::tan(baseHalfFov);
        result.Applied = true;
    }
    result.HorizontalScale =
        result.PerspectiveScale * horizontalExpansion;
    result.VerticalScale =
        result.PerspectiveScale * verticalExpansion;

    if (std::isfinite(source.Left) && std::isfinite(source.Right) &&
        source.Left != source.Right) {
        const float horizontalCenter =
            (source.Left + source.Right) * 0.5F;
        result.Frustum.Left = horizontalCenter +
            (source.Left - horizontalCenter) * result.HorizontalScale;
        result.Frustum.Right = horizontalCenter +
            (source.Right - horizontalCenter) * result.HorizontalScale;
    }
    if (std::isfinite(source.Bottom) && std::isfinite(source.Top) &&
        source.Bottom != source.Top) {
        const float verticalCenter =
            (source.Bottom + source.Top) * 0.5F;
        result.Frustum.Bottom = verticalCenter +
            (source.Bottom - verticalCenter) * result.VerticalScale;
        result.Frustum.Top = verticalCenter +
            (source.Top - verticalCenter) * result.VerticalScale;
    }
    result.Applied = result.Applied ||
        horizontalExpansion != 1.0F || verticalExpansion != 1.0F;
    return result;
}

} // namespace Fast::Oot3d
