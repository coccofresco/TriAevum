#pragma once

#include "fast/renderer3ds/pica_light_binding.h"
#include "fast/renderer3ds/room_shadow_shell.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Fast::Renderer3ds {

// Where the environment key light (light 0) points.
enum class PicaEnvironmentKeyDirection : uint8_t {
    // Its own authored direction.
    Authored,
    // The scene's main directional light (the one the native directional
    // shadows follow), turned by a fixed azimuth/elevation offset.
    SceneMain,
    // The axis of the scene's other directional light, on the main light's
    // side (positive dot product with it).
    SceneAmbient,
};

struct PicaEnvironmentDirectionalLight {
    std::array<float, 3> DirectionTowardSource{ 0.0F, 1.0F, 0.0F };
    std::array<float, 3> Color{ 1.0F, 1.0F, 1.0F };
    float Intensity = 0.0F;
    bool operator==(const PicaEnvironmentDirectionalLight&) const = default;
};

struct PicaScenePointLightSettings {
    bool Enabled = true;
    float Intensity = 1.0F;
    float RadiusScale = 1.0F;
    std::array<float, 3> ColorTint{1.0F, 1.0F, 1.0F};
    bool operator==(const PicaScenePointLightSettings&) const = default;
};

enum class PicaRimLightScope : uint8_t { Actors, ActorsAndWorld };
enum class PicaRimLightSource : uint8_t { Independent, SceneLights };
// Bounded artistic edge light, camera-relative by default.
// Scope eligibility belongs to the title adapter, not the surface shader.
inline constexpr float kMaximumPicaRimIntensity = 8.0F;
struct PicaRimLightSettings {
    bool Enabled = false;
    float Intensity = 1.0F;
    float Sharpness = 4.0F;
    PicaRimLightSource Source = PicaRimLightSource::Independent;
    std::array<float, 3> Color{1.0F, 0.96F, 0.9F};
    float Azimuth = 0.0F; // Camera-relative degrees: 0 top, 90 right.
    float Coverage = 0.25F; // 0 directional edge, 1 full silhouette.
    PicaRimLightScope Scope = PicaRimLightScope::Actors;
    bool operator==(const PicaRimLightSettings&) const = default;
};

// A light the title placed in the scene. Applies while the environment is
// enabled; a shadowing light (not a sun) competes for a point-shadow cube map
// at its position.
inline constexpr uint32_t kMaximumPicaPlacedLights = 16U;
// The shader's codes (PicaPlacedLightUniforms::AxisKind.w).
enum class PicaPlacedLightKind : uint8_t {
    // Range falloff 1 - d^2/r^2, as the scene point lights.
    Omni = 0,
    // Inverse square (the colour at kPicaPlacedLightReferenceDistance), cut
    // smoothly at the reach.
    Point = 1,
    // Point falloff within a cone around the axis.
    Spot = 2,
    // An emitting rectangle or disc facing the axis: its form factor from the
    // closest point of the shape, cut at the reach.
    Rectangle = 3,
    Disc = 4,
    // Parallel light travelling along the axis; never shadowed.
    Sun = 5,
};
inline constexpr float kPicaPlacedLightReferenceDistance = 100.0F;
struct PicaPlacedLight {
    uint64_t SourceIdentity = 0;
    PicaPlacedLightKind Kind = PicaPlacedLightKind::Omni;
    std::array<float, 3> PositionWorld{};
    // Unit emission direction: the spot's beam, the surface's front face,
    // the sun's light.
    std::array<float, 3> AxisWorld{ 0.0F, -1.0F, 0.0F };
    // Unit width direction of a rectangle (perpendicular to the axis).
    std::array<float, 3> TangentWorld{ 1.0F, 0.0F, 0.0F };
    // Linear RGB already scaled by the light's intensity.
    std::array<float, 3> Color{};
    float Reach = 0.0F;
    // Rectangle half extents; a disc's radius is HalfWidth.
    float HalfWidth = 0.0F;
    float HalfHeight = 0.0F;
    // Spot: cosines of the beam's half angle and of its hard core.
    float CosOuter = 0.0F;
    float CosInner = 0.0F;
    // Rectangle and disc: the full angle (degrees, 1..180) around the axis
    // within which each point of the surface emits. 180 is a diffuse
    // surface; less straightens its light along the axis, so it draws a
    // sharper footprint of its shape, as bright under the surface.
    float Spread = 180.0F;
    bool TwoSided = false;
    bool CastShadows = false;
    bool operator==(const PicaPlacedLight&) const = default;
};

// Explicit extension inputs, never a replacement for native PICA state.
// The title owns profile identity, persistence and world-axis conventions.
struct PicaEnvironmentLighting {
    uint64_t ScopeIdentity = 0;
    // Nonzero: the owner publishes continuous animation within this epoch.
    // Edits, time cuts and scene/load changes advance it. The renderer's
    // shadow keyframe scheduler follows direction motion within an epoch.
    uint64_t ContinuityEpoch = 0;
    bool Enabled = false;
    // Keep the native low-frequency reference independently of source gains.
    bool PreserveSceneBrightness = true;
    float NativeDirectStrength = 0.0F;
    float NativeLocalStrength = 0.0F;
    float NativeAmbientStrength = 0.0F;
    std::array<float, 3> AmbientColor{ 1.0F, 1.0F, 1.0F };
    float AmbientIntensity = 0.2F;
    float FogStrength = 1.0F;
    // One shadow map: light 0 casts shadows; light 1 is unshadowed fill.
    std::array<PicaEnvironmentDirectionalLight, 2> Lights{};
    // Where light 0 points; its authored direction applies while the scene
    // snapshot of a draw has no main directional light.
    PicaEnvironmentKeyDirection KeyDirection = PicaEnvironmentKeyDirection::Authored;
    // SceneMain: a fixed turn of the main light's direction, in radians.
    float KeyAzimuthOffset = 0.0F;
    float KeyElevationOffset = 0.0F;
    // The title's world axes: up, and the azimuth origin; azimuth turns from
    // it toward cross(WorldUp, WorldAzimuthZero).
    std::array<float, 3> WorldUp{ 0.0F, 1.0F, 0.0F };
    std::array<float, 3> WorldAzimuthZero{ 0.0F, 0.0F, 1.0F };
    // Independent of the directional/ambient override; title-owned groups.
    std::array<PicaScenePointLightSettings, kPicaScenePointLightGroups> PointLights{};
    // Lights placed in the scene (their own per-draw slots).
    std::array<PicaPlacedLight, kMaximumPicaPlacedLights> PlacedLights{};
    uint32_t PlacedLightCount = 0;
    PicaRimLightSettings Rim;
    // Independent of the lighting override.
    ShadowCasterFaces CasterFaces = ShadowCasterFaces::Native;
    bool operator==(const PicaEnvironmentLighting&) const = default;
};

[[nodiscard]] inline bool ContinuousPicaEnvironmentChange(const PicaEnvironmentLighting& before,
                                                          const PicaEnvironmentLighting& after) noexcept {
    return before.ContinuityEpoch != 0 && before.ContinuityEpoch == after.ContinuityEpoch &&
           before.ScopeIdentity == after.ScopeIdentity && before.Enabled == after.Enabled &&
           before.CasterFaces == after.CasterFaces && before.KeyDirection == after.KeyDirection;
}

// Light 0's colour for a draw with this scene snapshot. A key that follows
// the scene's lights also takes its main light's colour and brightness (after
// that light's direct horizon), its authored colour tinting it: white and an
// intensity of one give the title's own light, so sunrise, night and every
// native colour change carry over. An authored key, or a draw without a scene
// snapshot, keeps its own colour; a snapshot with no emitting light gives none.
[[nodiscard]] inline std::array<float, 3> ResolvePicaEnvironmentKeyColor(
    const PicaEnvironmentLighting& environment, const PicaSceneDirectionalLights* scene) noexcept {
    const auto authored = environment.Lights[0].Color;
    if (environment.KeyDirection == PicaEnvironmentKeyDirection::Authored || scene == nullptr || scene->Count == 0)
        return authored;
    const auto* main = FindPicaSceneMainDirectional(*scene);
    if (main == nullptr) return {};
    const float horizon = PicaSceneDirectHorizon(*main, scene->WorldUp);
    return { authored[0] * std::max(main->Color[0], 0.0F) * horizon,
             authored[1] * std::max(main->Color[1], 0.0F) * horizon,
             authored[2] * std::max(main->Color[2], 0.0F) * horizon };
}

// Light 0's direction toward its source for a draw with this scene snapshot.
[[nodiscard]] inline std::array<float, 3> ResolvePicaEnvironmentKeyDirection(
    const PicaEnvironmentLighting& environment, const PicaSceneDirectionalLights* scene) noexcept {
    const auto authored = environment.Lights[0].DirectionTowardSource;
    if (environment.KeyDirection == PicaEnvironmentKeyDirection::Authored || scene == nullptr) return authored;
    const auto* main = FindPicaSceneMainDirectional(*scene);
    if (main == nullptr) return authored;
    const auto direction = *PicaSceneDirectionalDirection(*main);
    const auto dot = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };
    if (environment.KeyDirection == PicaEnvironmentKeyDirection::SceneAmbient) {
        // The other environment light's axis, turned toward the main light.
        const PicaSceneDirectionalLight* ambient = nullptr;
        for (uint32_t i = 0; i < scene->Count; ++i) {
            const auto& light = scene->Lights[i];
            if (&light == main || !PicaSceneDirectionalDirection(light)) continue;
            if (ambient == nullptr || PicaSceneDirectionalEnergy(light) > PicaSceneDirectionalEnergy(*ambient))
                ambient = &light;
        }
        if (ambient == nullptr) return direction;
        auto axis = *PicaSceneDirectionalDirection(*ambient);
        if (dot(axis, direction) < 0.0F)
            for (float& component : axis) component = -component;
        return axis;
    }
    if (environment.KeyAzimuthOffset == 0.0F && environment.KeyElevationOffset == 0.0F) return direction;
    const auto normalized = [&](std::array<float, 3> value) {
        const float length = std::sqrt(dot(value, value));
        if (!std::isfinite(length) || length <= 1.0e-6F) return std::array<float, 3>{};
        for (float& component : value) component /= length;
        return value;
    };
    const auto up = normalized(environment.WorldUp);
    const auto forward = normalized(environment.WorldAzimuthZero);
    const std::array<float, 3> right{ up[1] * forward[2] - up[2] * forward[1],
                                      up[2] * forward[0] - up[0] * forward[2],
                                      up[0] * forward[1] - up[1] * forward[0] };
    if (dot(right, right) <= 1.0e-6F) return direction;
    constexpr float kQuarterTurn = 1.57079632679F;
    const float azimuth = std::atan2(dot(direction, right), dot(direction, forward)) + environment.KeyAzimuthOffset;
    const float elevation = std::clamp(std::asin(std::clamp(dot(direction, up), -1.0F, 1.0F)) +
                                           environment.KeyElevationOffset, -kQuarterTurn, kQuarterTurn);
    std::array<float, 3> result{};
    for (size_t axis = 0; axis < 3; ++axis)
        result[axis] = std::cos(elevation) * (std::cos(azimuth) * forward[axis] + std::sin(azimuth) * right[axis]) +
                       std::sin(elevation) * up[axis];
    return result;
}

} // namespace Fast::Renderer3ds
