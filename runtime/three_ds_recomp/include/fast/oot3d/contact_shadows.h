#pragma once

#include "fast/oot3d/directional_shadows.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace Fast::Oot3d {

// Screen-space contact shadows of the directional shadow light, ported from
// Godot 4.8 (pull request 118045, after Bend Studio's screen space shadows):
// a camera depth of this sample's opaque casters, rays marched toward the
// light in screen space, an R8 visibility the receivers combine with the
// shadow map by min().
//
// The contact-shadow camera keeps the native projection's x and y rows (its
// orientation and aspect) and writes reverse-Z depth near / distance, the
// convention of Godot's shader. Receivers project their view position with
// the same matrix to find their texel.
inline constexpr float kContactShadowNearPlane = 1.0F;
inline constexpr uint32_t kContactShadowWaveSize = 64U;
// Godot's shadow_length 0/1/2: two, three or four reads of 64 samples.
inline constexpr uint32_t kContactShadowLengthCount = 3U;

// The contact shadows' clip space from the native view space: the native
// projection (the inverse of clipToWorld, back through the native view,
// viewToWorld) with reverse-Z depth.
[[nodiscard]] std::optional<DirectionalShadowMatrix>
BuildContactShadowProjection(const DirectionalShadowMatrix& clipToWorld,
                             const DirectionalShadowMatrix& viewToWorld) noexcept;

// Godot's push constants (screen_space_contact_shadows.glsl, std430).
struct ContactShadowPushConstants {
    std::array<int32_t, 2> ScreenSize{};
    std::array<int32_t, 2> LightOffset{};
    std::array<float, 4> LightCoordinate{};
    float SurfaceThickness = 0.01F;
    float Opacity = 1.0F;
    float Blur = 1.0F;
    float FrameCount = 0.0F;
};
static_assert(sizeof(ContactShadowPushConstants) == 48U);

struct ContactShadowDispatch {
    ContactShadowPushConstants Constants;
    // Work groups: one wave of 64 along x, the bound grid along y and z.
    std::array<uint32_t, 3> Groups{};
    [[nodiscard]] bool Valid() const noexcept { return Groups[1] != 0U && Groups[2] != 0U; }
};

// Godot's SSEffects::screen_space_contact_shadows: the light's screen
// coordinate (converging, diverging or parallel rays) and the groups that
// cover the screen from it. The direction is toward the light, in the space
// the projection maps from. The frame is the temporal jitter sequence (0
// without temporal accumulation), the phase of the ray-start noise.
[[nodiscard]] ContactShadowDispatch
ResolveContactShadowDispatch(const DirectionalShadowMatrix& projection,
                             const DirectionalShadowVector& viewDirectionTowardLight, uint32_t width,
                             uint32_t height, const ContactShadowSettings& settings, uint64_t frame) noexcept;

// The compute shader for a length (READ_COUNT 2 + length).
[[nodiscard]] std::string ContactShadowComputeShader(uint32_t length);

} // namespace Fast::Oot3d
