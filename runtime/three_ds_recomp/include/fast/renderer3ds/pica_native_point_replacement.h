#pragma once

#include "fast/renderer3ds/pica_fragment_balance.h"

namespace Fast::Renderer3ds {

// Per-draw admission, not a second light selector. The title has proved each
// candidate's source; the captured acceptance/upload chain proves its native
// hardware slot. No position/colour tolerance, title id or guest address here.
struct PicaNativePointReplacement {
    PicaFragmentBalanceUniforms Balance;
    PicaScenePointLights Points;
    uint32_t ReplacedSlots = 0;
};

[[nodiscard]] inline std::optional<PicaNativePointReplacement> BuildPicaNativePointReplacement(
    bool capability, const PicaFragmentLightingLayout& layout,
    const PicaNativeLightBinding& binding, const PicaEnvironmentLighting& environment,
    const std::optional<PicaFragmentBalanceUniforms>& sceneBalance) noexcept {
    if (!capability || !layout.Valid() || layout.ActiveLightCount == 0 ||
        !binding.ScenePoints || binding.ScenePoints->Count > binding.ScenePoints->Lights.size()) return std::nullopt;
    PicaNativePointReplacement result;
    if (sceneBalance) result.Balance = *sceneBalance;
    else {
        // No proved Scene key (e.g. an interior wall): preserve the complete
        // native scene reference. Local conversion must not invent a profile.
        result.Balance.Control[1] = 8;
        result.Balance.Parameters[0] = 1;
    }
    uint32_t active = 0;
    for (uint32_t slot = 0; slot < layout.ActiveLightCount; ++slot) {
        const uint32_t i = layout.LightPermutation[slot];
        if (i >= binding.Sources.size() || (active & (1U << i))) return std::nullopt;
        active |= 1U << i;
        // An untracked native point could be any candidate. Without its
        // identity, even treating a candidate as unmatched could double it.
        if (!layout.Lights[i].Directional &&
            (binding.Sources[i].Kind != PicaNativeLightSource::Type::Point ||
             !binding.Sources[i].SourceIdentity)) return std::nullopt;
        if (!sceneBalance) result.Balance.Gains[i] = {1, 1, 1, 0};
    }
    result.Balance.Control[0] = active;
    const auto& points = *binding.ScenePoints;
    for (uint32_t p = 0; p < points.Count; ++p) {
        const auto& point = points.Lights[p];
        // An independent emitter is admitted only with its separate title
        // proof. Neither a default supplemental point nor contradictory proof
        // flags can bypass native replacement ownership.
        if (point.ReplacesNativeLocal == point.IndependentEmission || !point.SourceIdentity ||
            point.Group >= environment.PointLights.size()) continue;
        const auto& setting = environment.PointLights[point.Group];
        // Off keeps native; it is not the native-local-gain slider.
        if (!setting.Enabled || !(setting.Intensity > 0) || !(setting.RadiusScale > 0) ||
            !std::isfinite(setting.Intensity) || !std::isfinite(setting.RadiusScale) ||
            !(point.Radius > 0) || !std::isfinite(point.Radius)) continue;
        bool valid = true;
        for (uint32_t c = 0; c < 3; ++c)
            valid = valid && std::isfinite(point.PositionWorld[c]) && std::isfinite(point.Color[c]) &&
                point.Color[c] >= 0 && std::isfinite(setting.ColorTint[c]) && setting.ColorTint[c] >= 0;
        for (uint32_t other = 0; other < points.Count; ++other)
            if (other != p && points.Lights[other].SourceIdentity == point.SourceIdentity) valid = false;
        uint32_t match = 0;
        for (uint32_t i = 0; i < binding.Sources.size(); ++i) {
            if (!(active & (1U << i))) continue;
            const auto& source = binding.Sources[i];
            if (source.SourceIdentity != point.SourceIdentity) continue;
            if (match || layout.Lights[i].Directional || source.Kind != PicaNativeLightSource::Type::Point ||
                source.Scope != PicaNativeLightScope::Local || source.Origin != PicaNativeLightOrigin::Actor)
                valid = false;
            match |= 1U << i;
        }
        // A matching Unknown/Scene/ambiguous slot is not an unmatched new
        // source: omit the renderer light on this draw and keep native.
        if (!valid || (point.IndependentEmission && match)) continue;
        result.Points.Lights[result.Points.Count++] = point;
        result.ReplacedSlots |= match;
        for (uint32_t i = 0; i < binding.Sources.size(); ++i)
            if (match & (1U << i))
                // A fragment-native point already has per-pixel radiance and
                // attenuation. Convert its visibility, not its energy into a
                // separately selected/faded supplemental-light approximation.
                // w >= 4 encodes the exact scene-point index; x/y are the user
                // intensity/radius controls on this path, z retains ambient.
                result.Balance.Gains[i] = {setting.Intensity, setting.RadiusScale, 1,
                    float(4U + result.Points.Count - 1U)};
    }
    if (!result.Points.Count) return std::nullopt;
    result.Balance.Control[3] |= 2U; // proved replacement/independent point term before native clamp
    return result;
}

// Call after material folding: matched point rows carry only a user tint.
// Their authored material/light response stays in the native slot; unmatched
// rows remain ordinary supplemental radiance. No uniform-layout/legacy change.
inline void ApplyPicaNativePointReplacementControls(PicaSurfaceLightingUniforms& surface,
    const PicaNativePointReplacement& replacement, const PicaEnvironmentLighting& environment) noexcept {
    for (const auto& gains : replacement.Balance.Gains) {
        if (gains[3] < 4.0F) continue;
        const auto p = static_cast<uint32_t>(gains[3]) - 4U;
        if (p >= replacement.Points.Count || p >= surface.Control[1]) continue;
        const auto& tint = environment.PointLights[replacement.Points.Lights[p].Group].ColorTint;
        std::copy(tint.begin(), tint.end(), surface.ScenePoints[p].Color.begin());
    }
}

} // namespace Fast::Renderer3ds
