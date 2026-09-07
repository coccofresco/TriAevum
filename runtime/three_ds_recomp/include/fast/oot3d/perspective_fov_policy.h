#pragma once

#include "fast/oot3d/scene_presentation_policy.h"

namespace Fast::Oot3d {

struct PerspectiveFrustum {
    float Left = 0.0F;
    float Right = 0.0F;
    float Bottom = 0.0F;
    float Top = 0.0F;
    float NearPlane = 0.0F;
    float FarPlane = 0.0F;
};

struct PerspectiveFovInput {
    PerspectiveFrustum Frustum{};
    float Multiplier = 1.0F;
    ScenePresentationPolicy Presentation{};
    bool Perspective = true;
};

struct PerspectiveFovResult {
    PerspectiveFrustum Frustum{};
    float PerspectiveScale = 1.0F;
    float HorizontalScale = 1.0F;
    float VerticalScale = 1.0F;
    bool Applied = false;
};

// Applies the general FOV multiplier to both perspective axes, then extends
// only the axis selected by the scene presentation policy. Orthographic input
// is returned unchanged, keeping HUD/UI projection outside this contract. The
// public multiplier is clamped to [1, 1.5] and half-FOV stays below 89 degrees.
[[nodiscard]] PerspectiveFovResult ResolvePerspectiveFov(
    const PerspectiveFovInput& input) noexcept;

} // namespace Fast::Oot3d
