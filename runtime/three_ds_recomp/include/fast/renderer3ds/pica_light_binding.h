#pragma once

#include "fast/renderer3ds/pica_scene_water.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>

namespace Fast::Renderer3ds {

// Supplemental source data for the lights already selected by native code.
// Slot order agrees with the captured vertex-light uniforms. It never adds a
// light, replaces native enable flags, or refers back to live guest memory.
struct PicaNativeLightSource {
    enum class Type : uint8_t { Unavailable, Directional, Point };

    Type Kind = Type::Unavailable;
    uint64_t SourceIdentity = 0;
    std::array<float, 3> PositionWorld{};
    std::array<float, 3> UnattenuatedColor{};
    std::array<float, 3> ReferenceColor{};
    float Radius = 0.0F;

    bool operator==(const PicaNativeLightSource&) const = default;
};

inline constexpr uint32_t kMaximumPicaScenePointLights = 32U;
// Scene point-light groups: 0 Navi, 1 fairies, 2 flames (lights the title
// draws as flames but never registers: torches, fire objects).
inline constexpr uint32_t kPicaScenePointLightGroups = 3U;

struct PicaScenePointLight {
    uint64_t SourceIdentity = 0;
    std::array<float, 3> PositionWorld{};
    std::array<float, 3> Color{};
    float Radius = 0.0F;
    uint32_t Group = 0;
    bool operator==(const PicaScenePointLight&) const = default;
};

// One immutable scene snapshot shared by all draws from a native frame.
// Zero-radius records retain source identity so stale selected native slots
// cannot restore a contribution after that source has been turned off.
struct PicaScenePointLights {
    std::array<PicaScenePointLight, kMaximumPicaScenePointLights> Lights{};
    uint32_t Count = 0;
    bool operator==(const PicaScenePointLights&) const = default;
};

inline constexpr uint32_t kMaximumPicaSceneDirectionalLights = 2U;

struct PicaSceneDirectionalLight {
    uint64_t SourceIdentity = 0;
    std::array<float, 3> DirectionTowardSource{};
    std::array<float, 3> Color{};
    // A sky light (the title's sun or moon, driven by its clock): below the
    // horizon it lights nothing directly. Other lights below the horizon,
    // placed there by the scene, keep their light.
    bool SkyLight = false;
    bool operator==(const PicaSceneDirectionalLight&) const = default;
};

// Native environment sources, independent of each draw's selected lights.
// Separate from moving points so unchanged environment snapshots stay shared.
struct PicaSceneDirectionalLights {
    std::array<PicaSceneDirectionalLight, kMaximumPicaSceneDirectionalLights> Lights{};
    uint32_t Count = 0;
    // The title's world up: every light's horizon.
    std::array<float, 3> WorldUp{ 0.0F, 1.0F, 0.0F };
    // Advances at every discontinuity (scene, environment, light mode, a
    // jump of direction): presented samples never interpolate across it.
    uint64_t ContinuityEpoch = 0;
    // The shadow-casting light and its shadow strength (0-1), decided once
    // per guest frame by PicaSceneShadowCasterTracker. Identity 0: no
    // decision was published; FindPicaSceneMainDirectional's rule applies.
    uint64_t ShadowCasterIdentity = 0;
    float ShadowStrength = 1.0F;
    bool operator==(const PicaSceneDirectionalLights&) const = default;
};

// Rec. 709 luminance of a source's color.
[[nodiscard]] inline float PicaSceneDirectionalEnergy(const PicaSceneDirectionalLight& light) noexcept {
    return 0.2126F * std::max(light.Color[0], 0.0F) + 0.7152F * std::max(light.Color[1], 0.0F) +
           0.0722F * std::max(light.Color[2], 0.0F);
}

// The unit direction of a source that emits light, if it has one.
[[nodiscard]] inline std::optional<std::array<float, 3>> PicaSceneDirectionalDirection(
    const PicaSceneDirectionalLight& light) noexcept {
    constexpr float kMinimum = 1.0e-6F;
    const float energy = PicaSceneDirectionalEnergy(light);
    auto direction = light.DirectionTowardSource;
    const float lengthSquared =
        direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2];
    if (!std::isfinite(energy) || energy <= kMinimum || !std::isfinite(lengthSquared) ||
        lengthSquared <= kMinimum)
        return std::nullopt;
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    for (float& component : direction) component *= inverseLength;
    return direction;
}

// Sine of a unit direction's elevation above the horizon of `up`.
[[nodiscard]] inline float PicaSceneElevationSine(const std::array<float, 3>& direction,
                                                  const std::array<float, 3>& up) noexcept {
    const float length = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
    if (!std::isfinite(length) || length <= 1.0e-6F) return 1.0F;
    return (direction[0] * up[0] + direction[1] * up[1] + direction[2] * up[2]) / length;
}

namespace PicaSceneHorizon {
// Shadows: none up to 1 degree, full from 8. Direct sky light: full at and
// above the horizon, as the title lights its sunrise, none from 4 degrees
// below. sin(1), sin(8) and sin(-4) degrees.
inline constexpr float kShadowStart = 0.0174524F;
inline constexpr float kShadowFull = 0.1391731F;
inline constexpr float kDirectStart = -0.0697565F;
inline constexpr float kDirectFull = 0.0F;

[[nodiscard]] inline float Smoothstep(float edge0, float edge1, float value) noexcept {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}
} // namespace PicaSceneHorizon

// A caster's shadow strength at this elevation: none at or below the
// horizon, fading in over its first degrees so shadows thin out instead of
// ending as the light sets.
[[nodiscard]] inline float PicaSceneShadowHorizon(float elevationSine) noexcept {
    return PicaSceneHorizon::Smoothstep(PicaSceneHorizon::kShadowStart, PicaSceneHorizon::kShadowFull,
                                        elevationSine);
}

// The share of a light's direct light that reaches the scene: a sky light
// keeps all of it above the horizon and fades out within a few degrees
// below; other lights keep all of it.
[[nodiscard]] inline float PicaSceneDirectHorizon(const PicaSceneDirectionalLight& light,
                                                  const std::array<float, 3>& up) noexcept {
    if (!light.SkyLight) return 1.0F;
    const auto direction = PicaSceneDirectionalDirection(light);
    if (!direction) return 1.0F;
    return PicaSceneHorizon::Smoothstep(PicaSceneHorizon::kDirectStart, PicaSceneHorizon::kDirectFull,
                                        PicaSceneElevationSine(*direction, up));
}

// The energy a light competes with to cast the scene's shadow: its
// luminance, after its direct light's horizon; none at or below the
// horizon, where no light casts.
[[nodiscard]] inline float PicaSceneShadowCandidateEnergy(const PicaSceneDirectionalLight& light,
                                                          const std::array<float, 3>& up) noexcept {
    const auto direction = PicaSceneDirectionalDirection(light);
    if (!direction || PicaSceneElevationSine(*direction, up) <= 0.0F) return 0.0F;
    return PicaSceneDirectionalEnergy(light) * PicaSceneDirectHorizon(light, up);
}

// The scene's main directional light, the one the directional shadows and a
// scene-following key light use: the published caster; without a published
// decision, the strongest candidate above the horizon (the lower identity on
// a tie), or else the highest light with a direction.
[[nodiscard]] inline const PicaSceneDirectionalLight* FindPicaSceneMainDirectional(
    const PicaSceneDirectionalLights& lights) noexcept {
    if (lights.Count > lights.Lights.size()) return nullptr;
    if (lights.ShadowCasterIdentity != 0) {
        for (uint32_t i = 0; i < lights.Count; ++i) {
            const auto& light = lights.Lights[i];
            if (light.SourceIdentity == lights.ShadowCasterIdentity && PicaSceneDirectionalDirection(light))
                return &light;
        }
    }
    const PicaSceneDirectionalLight* main = nullptr;
    float best = 0.0F;
    const PicaSceneDirectionalLight* highest = nullptr;
    float highestElevation = 0.0F;
    for (uint32_t i = 0; i < lights.Count; ++i) {
        const auto& light = lights.Lights[i];
        const auto direction = PicaSceneDirectionalDirection(light);
        if (!direction) continue;
        const float elevation = PicaSceneElevationSine(*direction, lights.WorldUp);
        if (highest == nullptr || elevation > highestElevation) {
            highest = &light;
            highestElevation = elevation;
        }
        const float energy = PicaSceneShadowCandidateEnergy(light, lights.WorldUp);
        if (energy <= 0.0F) continue;
        if (energy > best || (energy == best && main != nullptr && light.SourceIdentity < main->SourceIdentity)) {
            main = &light;
            best = energy;
        }
    }
    return main != nullptr ? main : highest;
}

// The shadow strength (0-1) of the main light: the published decision, or
// without one, the main light's horizon.
[[nodiscard]] inline float PicaSceneMainShadowStrength(const PicaSceneDirectionalLights& lights,
                                                       const PicaSceneDirectionalLight& main) noexcept {
    if (lights.ShadowCasterIdentity != 0 && main.SourceIdentity == lights.ShadowCasterIdentity)
        return std::clamp(lights.ShadowStrength, 0.0F, 1.0F);
    const auto direction = PicaSceneDirectionalDirection(main);
    return direction ? PicaSceneShadowHorizon(PicaSceneElevationSine(*direction, lights.WorldUp)) : 0.0F;
}

struct PicaNativeLightBinding {
    std::array<PicaNativeLightSource, 3> Sources{};
    // Column-major world-to-view captured with the native geometry commands.
    // Replayed/interpolated with this draw, never read from a live camera.
    std::optional<std::array<float, 16>> WorldToView;
    std::shared_ptr<const PicaScenePointLights> ScenePoints;
    std::shared_ptr<const PicaSceneDirectionalLights> SceneDirectionals;
    std::shared_ptr<const PicaSceneWaterBodies> Water;

    bool operator==(const PicaNativeLightBinding&) const = default;
};

} // namespace Fast::Renderer3ds
