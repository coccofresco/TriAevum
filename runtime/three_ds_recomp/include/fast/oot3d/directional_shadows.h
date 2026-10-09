#pragma once

#include "fast/renderer3ds/virtual_shadow_map.h"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace Fast::Oot3d {

enum class DirectionalShadowMode : uint8_t {
    Off,
    VirtualShadowMaps,
};

// Godot 4.8's screen-space contact shadows of the directional shadow light
// (rendering/lights_and_shadows/contact_shadow/{enabled, shadow_length,
// surface_thickness} and the light's shadow_contact_shadows_{opacity, blur}).
struct ContactShadowSettings {
    bool Enabled = false;
    // 0 short, 1 normal, 2 long: 2, 3 or 4 reads of 64 samples along the ray.
    uint8_t Length = 1;
    // The assumed depth thickness of surfaces, a fraction of the depth range.
    float SurfaceThickness = 0.01F;
    float Opacity = 1.0F;
    // Noise added to the ray start: softens the shadow's edges.
    float Blur = 1.0F;
    // Camera depth and rays at half the target's width and height: a quarter
    // of the work; thin occluders may be missed. Godot has full resolution.
    bool HalfResolution = true;
    bool operator==(const ContactShadowSettings&) const = default;
};

struct DirectionalShadowSettings {
    DirectionalShadowMode Mode = DirectionalShadowMode::Off;
    uint32_t Resolution = 2048;
    float MaximumDistance = 65536.0F;
    float DepthBiasConstant = 1.25F;
    float DepthBiasSlope = 1.75F;
    float Strength = 0.80F;
    uint8_t PcfRadius = 1;
    // Godot's directional normal bias: smooth-shaded receivers read the map
    // this many texels (of the level read) along their vertex normal, the
    // full amount where they face the light, none where they face away, and
    // only across the light (never along it).
    float NormalBias = 1.0F;
    ContactShadowSettings ContactShadows;
};

using DirectionalShadowMatrix = std::array<float, 16>;
using DirectionalShadowVector = std::array<float, 3>;

enum class DirectionalShadowPlanStatus : uint8_t {
    Disabled,
    InvalidCamera,
    InvalidLightDirection,
    Ready,
};

struct DirectionalShadowFrameInput {
    DirectionalShadowVector CameraWorld{};
    DirectionalShadowVector LightDirectionTowardSource{};
    DirectionalShadowSettings Settings;
    const ::Fast::Renderer3ds::VirtualShadowLayout* PreviousLayout = nullptr;
    // The casters keep their own face culling (false: both faces draw).
    bool CullCasters = true;
};

struct DirectionalShadowFramePlan {
    DirectionalShadowPlanStatus Status = DirectionalShadowPlanStatus::Disabled;
    ::Fast::Renderer3ds::VirtualShadowLayout Virtual;
    DirectionalShadowVector LightDirectionTowardSource{};
    float DepthBiasConstant = 0.0F;
    float DepthBiasSlope = 0.0F;
    float Strength = 0.0F;
    uint8_t PcfRadius = 0;
    float NormalBias = 0.0F;
    bool CullCasters = true;
    ContactShadowSettings ContactShadows;

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
