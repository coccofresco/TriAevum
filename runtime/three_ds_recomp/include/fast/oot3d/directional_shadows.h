#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace Fast::Oot3d {

enum class DirectionalShadowMode : uint8_t {
    Off,
    SingleCascade,
};

struct DirectionalShadowSettings {
    DirectionalShadowMode Mode = DirectionalShadowMode::Off;
    uint32_t Resolution = 1024;
    float MaximumDistance = 2400.0F;
    float DepthPadding = 300.0F;
    float DepthBiasConstant = 1.25F;
    float DepthBiasSlope = 1.75F;
    float Strength = 0.80F;
    uint8_t PcfRadius = 1;
    bool Stabilize = true;
};

using DirectionalShadowMatrix = std::array<float, 16>;
using DirectionalShadowVector = std::array<float, 3>;

enum class DirectionalShadowPlanStatus : uint8_t {
    Disabled,
    InvalidProjection,
    InvalidLightDirection,
    DegenerateFrustum,
    Ready,
};

struct DirectionalShadowFrameInput {
    DirectionalShadowMatrix ClipToWorld{};
    DirectionalShadowVector LightDirectionTowardSource{};
    DirectionalShadowSettings Settings;
};

struct DirectionalShadowFramePlan {
    DirectionalShadowPlanStatus Status = DirectionalShadowPlanStatus::Disabled;
    DirectionalShadowMatrix WorldToLightClip{};
    DirectionalShadowMatrix WorldToShadowTexture{};
    DirectionalShadowMatrix ClipToWorld{};
    DirectionalShadowVector LightDirectionTowardSource{};
    uint32_t Resolution = 0;
    float DepthBiasConstant = 0.0F;
    float DepthBiasSlope = 0.0F;
    float Strength = 0.0F;
    uint8_t PcfRadius = 0;

    [[nodiscard]] bool Ready() const noexcept {
        return Status == DirectionalShadowPlanStatus::Ready;
    }
};

[[nodiscard]] DirectionalShadowMatrix IdentityDirectionalShadowMatrix() noexcept;

[[nodiscard]] bool InvertDirectionalShadowMatrix(
    const DirectionalShadowMatrix& matrix,
    DirectionalShadowMatrix& inverse) noexcept;

[[nodiscard]] DirectionalShadowMatrix MultiplyDirectionalShadowMatrices(const DirectionalShadowMatrix& left,
                                                                        const DirectionalShadowMatrix& right) noexcept;

[[nodiscard]] DirectionalShadowFramePlan
BuildDirectionalShadowFramePlan(const DirectionalShadowFrameInput& input) noexcept;

// Native scene draws use a rotated perspective matrix in f[0..3].
// Screen-space/orthographic draws must not be replayed as world occluders.
[[nodiscard]] bool UsesPicaPerspectiveProjection(std::span<const uint8_t> packedVertexUniforms) noexcept;

// Native CMB directions are uploaded in view space for world geometry.
[[nodiscard]] DirectionalShadowVector
TransformPicaLightDirectionToWorld(const DirectionalShadowVector& viewDirectionTowardSource,
                                   const DirectionalShadowVector& eye, const DirectionalShadowVector& at) noexcept;

// Exact renderer path: transform a native CMB view-space direction through
// the draw-local PICA view inverse rather than rebuilding a camera basis.
[[nodiscard]] DirectionalShadowVector
TransformPicaLightDirectionToWorld(
    const DirectionalShadowVector& viewDirectionTowardSource,
    const DirectionalShadowMatrix& viewToWorld) noexcept;

[[nodiscard]] std::string_view DirectionalShadowPlanStatusName(DirectionalShadowPlanStatus status) noexcept;

} // namespace Fast::Oot3d
