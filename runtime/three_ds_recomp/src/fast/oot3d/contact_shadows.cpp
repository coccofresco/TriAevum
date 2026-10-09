// Screen-space contact shadows, ported from Godot Engine 4.8:
// servers/rendering/renderer_rd/shaders/effects/screen_space_contact_shadows.glsl
// and SSEffects::screen_space_contact_shadows (pull request 118045), which
// follow Bend Studio's screen space shadows. Godot Engine is MIT licensed
// (Copyright (c) 2014-present Godot Engine contributors, (c) 2007-2014 Juan
// Linietsky, Ariel Manzur); see THIRD_PARTY_NOTICES.md.
#include "fast/oot3d/contact_shadows.h"

#include <algorithm>
#include <cmath>

namespace Fast::Oot3d {
namespace {

using Matrix = DirectionalShadowMatrix;

float At(const Matrix& m, size_t row, size_t column) noexcept { return m[column * 4U + row]; }

} // namespace

std::optional<DirectionalShadowMatrix> BuildContactShadowProjection(const DirectionalShadowMatrix& clipToWorld,
                                                                    const DirectionalShadowMatrix& viewToWorld) noexcept {
    DirectionalShadowMatrix worldToClip{};
    if (!InvertDirectionalShadowMatrix(clipToWorld, worldToClip))
        return std::nullopt;
    const auto native = MultiplyDirectionalShadowMatrices(worldToClip, viewToWorld);
    // The native x, y and w rows; z = near * w_in, so depth = near / w_clip
    // (reverse-Z, 1 at the near plane, 0 at infinity).
    DirectionalShadowMatrix result{};
    for (size_t column = 0; column < 4U; ++column) {
        result[column * 4U + 0U] = At(native, 0U, column);
        result[column * 4U + 1U] = At(native, 1U, column);
        result[column * 4U + 2U] = column == 3U ? kContactShadowNearPlane : 0.0F;
        result[column * 4U + 3U] = At(native, 3U, column);
    }
    // Visible geometry has a positive native w (PICA clips w <= 0), so the
    // reverse-Z depth is positive wherever a receiver can be.
    float wScale = 0.0F;
    for (size_t column = 0; column < 3U; ++column)
        wScale += std::abs(At(result, 3U, column));
    for (const float value : result)
        if (!std::isfinite(value))
            return std::nullopt;
    if (wScale <= 0.0F)
        return std::nullopt;
    return result;
}

ContactShadowDispatch ResolveContactShadowDispatch(const DirectionalShadowMatrix& projection,
                                                   const DirectionalShadowVector& viewDirectionTowardLight,
                                                   uint32_t width, uint32_t height,
                                                   const ContactShadowSettings& settings, uint64_t frame) noexcept {
    ContactShadowDispatch result;
    if (width == 0U || height == 0U)
        return result;
    std::array<float, 4> projected{};
    for (size_t row = 0; row < 4U; ++row)
        for (size_t column = 0; column < 3U; ++column)
            projected[row] += At(projection, row, column) * viewDirectionTowardLight[column];
    const float wave = float(kContactShadowWaveSize);
    const float w = float(width), h = float(height);
    std::array<float, 4> light{};
    std::array<int32_t, 2> boundStart{}, boundEnd{};
    if (std::abs(projected[3]) > 1.0e-6F) {
        light = { (projected[0] / projected[3] * 0.5F + 0.5F) * w, (projected[1] / projected[3] * 0.5F + 0.5F) * h,
                  projected[2] / projected[3], projected[3] > 0.0F ? 1.0F : -1.0F };
        boundStart = { int32_t(std::floor(-light[0] / wave)) - 1, int32_t(std::floor(-light[1] / wave)) - 1 };
        boundEnd = { int32_t(std::ceil((w - light[0]) / wave)) + 2, int32_t(std::ceil((h - light[1]) / wave)) + 2 };
    } else {
        // Parallel rays in screen space.
        light = { projected[0] * w * 0.5F, projected[1] * h * 0.5F, projected[2], 0.0F };
        const float scale = std::max(std::abs(light[0]), std::abs(light[1]));
        if (scale <= 0.0F || !std::isfinite(scale))
            return result;
        for (auto& value : light) value /= scale;
        boundStart = { light[0] > 0.0F && light[0] < std::abs(light[1]) ? -1 : 0,
                       light[1] > 0.0F && light[1] < std::abs(light[0]) ? -1 : 0 };
        boundEnd = { int32_t(std::ceil(w / wave)) + (light[0] < 0.0F ? 1 : 0),
                     int32_t(std::ceil(h / wave)) + (light[1] < 0.0F ? 1 : 0) };
    }
    for (const float value : light)
        if (!std::isfinite(value))
            return result;
    const int32_t sizeX = boundEnd[0] - boundStart[0];
    const int32_t sizeY = boundEnd[1] - boundStart[1];
    if (sizeX <= 0 || sizeY <= 0 || sizeX > 4096 || sizeY > 4096)
        return result;
    auto& constants = result.Constants;
    constants.ScreenSize = { int32_t(width), int32_t(height) };
    constants.LightOffset = { boundStart[0] * int32_t(kContactShadowWaveSize),
                              boundStart[1] * int32_t(kContactShadowWaveSize) };
    constants.LightCoordinate = light;
    constants.SurfaceThickness = std::max(settings.SurfaceThickness, 1.0e-5F);
    constants.Opacity = std::clamp(settings.Opacity, 0.0F, 1.0F);
    constants.Blur = std::max(settings.Blur, 0.0F);
    constants.FrameCount = float(frame % 1024U);
    result.Groups = { kContactShadowWaveSize, uint32_t(sizeX), uint32_t(sizeY) };
    return result;
}

} // namespace Fast::Oot3d
