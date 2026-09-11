#include "fast/oot3d/graphics_settings.h"
#include "fast/oot3d/grass_blade_geometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Fast::Oot3d {
namespace {

void Correct(std::vector<GraphicsSettingsIssue>& issues, std::string field,
             std::string message) {
    issues.push_back({ std::move(field), std::move(message), true });
}

bool IsMsaaCount(uint8_t samples) {
    return samples == 1 || samples == 2 || samples == 4 || samples == 8;
}

bool IsDirectionalShadowResolution(uint32_t resolution) {
    return resolution == 512U || resolution == 1024U ||
           resolution == 2048U || resolution == 4096U;
}

} // namespace

void ResetToonLightBandProfile(ToonStyleSettings& settings) noexcept {
    const uint8_t bandCount = std::clamp<uint8_t>(
        settings.LightBands, 2U, kMaximumToonLightBands);
    const float span = static_cast<float>(bandCount - 1U);
    for (size_t index = 0; index < settings.LightBandLevels.size(); ++index) {
        settings.LightBandLevels[index] =
            index < bandCount ? static_cast<float>(index) / span : 1.0F;
    }
    for (size_t index = 0; index < settings.LightBandThresholds.size();
         ++index) {
        settings.LightBandThresholds[index] =
            index + 1U < bandCount
                ? (static_cast<float>(index) + 0.5F) / span
                : 1.0F;
    }
}

void ResizeToonLightBandProfile(ToonStyleSettings& settings, uint8_t bands) noexcept {
    bands = std::clamp<uint8_t>(bands, 2U, kMaximumToonLightBands);
    const uint8_t oldCount = std::clamp<uint8_t>(settings.LightBands, 2U, kMaximumToonLightBands);
    if (bands == oldCount) return;
    const auto old = settings;
    settings.LightBands = bands;
    ResetToonLightBandProfile(settings);
    for (size_t index = 0; index < bands; ++index) {
        const float position = static_cast<float>(index) * (oldCount - 1U) / (bands - 1U);
        const size_t left = std::min(static_cast<size_t>(position), size_t(oldCount - 2U));
        settings.LightBandLevels[index] = std::lerp(
            old.LightBandLevels[left], old.LightBandLevels[left + 1U], position - left);
    }
    // Resample the threshold curve with fixed endpoints, preserving its shape
    // and ordering rather than discarding the user's profile on a count change.
    for (size_t index = 0; index + 1U < bands; ++index) {
        const float x = (static_cast<float>(index) + 0.5F) / (bands - 1U);
        float previousX = 0.0F, previousY = 0.0F;
        for (size_t point = 0; point < oldCount; ++point) {
            const float nextX = point + 1U == oldCount ? 1.0F
                : (static_cast<float>(point) + 0.5F) / (oldCount - 1U);
            const float nextY = point + 1U == oldCount ? 1.0F : old.LightBandThresholds[point];
            if (x <= nextX) {
                settings.LightBandThresholds[index] = std::lerp(
                    previousY, nextY, (x - previousX) / (nextX - previousX));
                break;
            }
            previousX = nextX;
            previousY = nextY;
        }
    }
}

void ApplyGrassQualityPreset(
    InteractiveGrassSettings& settings,
    GrassQuality quality) noexcept {
    settings.Quality = quality;
    if (quality == GrassQuality::Off ||
        quality == GrassQuality::Custom) {
        return;
    }

    settings.FrustumCulling = true;
    settings.CullingClusterSize = 300.0F;
    switch (quality) {
        case GrassQuality::Low:
            settings.MaxInstancesPerRoom = 25000U;
            settings.DrawDistance = 600.0F;
            settings.LodStartFraction = 0.35F;
            settings.LodEndFraction = 0.65F;
            settings.FarDensity = 0.15F;
            settings.FarBladeSegments = 1U;
            break;
        case GrassQuality::Medium:
            settings.MaxInstancesPerRoom = 75000U;
            settings.DrawDistance = 1200.0F;
            settings.LodStartFraction = 0.45F;
            settings.LodEndFraction = 0.80F;
            settings.FarDensity = 0.30F;
            settings.FarBladeSegments = 1U;
            break;
        case GrassQuality::High:
            settings.MaxInstancesPerRoom = 150000U;
            settings.DrawDistance = 2000.0F;
            settings.LodStartFraction = 0.60F;
            settings.LodEndFraction = 0.90F;
            settings.FarDensity = 0.55F;
            settings.FarBladeSegments =
                std::min<uint8_t>(
                    2U, settings.Appearance.BladeSegments);
            break;
        case GrassQuality::Off:
        case GrassQuality::Custom:
            break;
    }
    settings.SegmentLodStartDistance = settings.DrawDistance * settings.LodStartFraction;
    settings.LodReferenceDistance = settings.DrawDistance;
    settings.SegmentLodEndDistance = settings.DrawDistance * settings.LodEndFraction;
}

bool GraphicsSettingsValidation::Accepted() const {
    return std::none_of(Issues.begin(), Issues.end(),
                        [](const auto& issue) { return !issue.Corrected; });
}

GraphicsSettingsService::GraphicsSettingsService(GraphicsSettings initial)
    : mCurrent(std::move(initial)), mLastKnownGood(mCurrent) {
}

GraphicsSettings GraphicsSettingsService::Preset(GraphicsPreset preset) {
    GraphicsSettings value;
    value.Preset = preset;
    if (preset == GraphicsPreset::Enhanced) {
        value.AntiAliasing = AntiAliasingMode::Fxaa;
        value.FrameRate = FrameRateMode::Interpolated2x;
        value.Effects.DirectionalShadows.Mode =
            DirectionalShadowMode::SingleCascade;
        value.Effects.AmbientOcclusion = AmbientOcclusionMode::Cacao;
        value.Effects.AoQuality = 1;
        ApplyGrassQualityPreset(
            value.Grass, GrassQuality::Medium);
        value.Grass.WindDirectionDegrees = 35.0F;
        value.Grass.WindStrength = 0.35F;
        value.Grass.WindSpeed = 1.2F;
    } else if (preset == GraphicsPreset::Toon) {
        value = Preset(GraphicsPreset::Enhanced);
        value.Preset = GraphicsPreset::Toon;
        value.Effects.Toon = ToonMode::PostProcessPreview;
    } else if (preset == GraphicsPreset::Custom) {
        value.Preset = GraphicsPreset::Custom;
    }
    return value;
}

GraphicsSettings GraphicsSettingsService::PresetForCurrent(
    GraphicsPreset preset, const GraphicsSettings& current) {
    if (preset == GraphicsPreset::Custom) {
        auto value = current;
        value.Preset = preset;
        return value;
    }
    auto value = Preset(preset);

    // Presets select rendering behavior. Host presentation, camera preference
    // and content identities are independent user-owned state.
    value.Window = current.Window;
    value.DisplayIndex = current.DisplayIndex;
    value.OutputWidth = current.OutputWidth;
    value.OutputHeight = current.OutputHeight;
    value.RefreshRate = current.RefreshRate;
    value.VSync = current.VSync;
    value.FovMultiplier = current.FovMultiplier;
    value.TexturePacks = current.TexturePacks;
    value.Grass.Rules = current.Grass.Rules;
    value.GrassSavedPreset = current.GrassSavedPreset;
    value.Effects.ReflectionMaterials =
        current.Effects.ReflectionMaterials;
    return value;
}

GraphicsSettingsValidation GraphicsSettingsService::Validate(
    GraphicsSettings value, const GraphicsCapabilities& capabilities) {
    GraphicsSettingsValidation result;
    value.OutputWidth = std::max(320U, value.OutputWidth);
    value.OutputHeight = std::max(240U, value.OutputHeight);
    value.RefreshRate = std::clamp(value.RefreshRate, 30U, 1000U);
    value.InternalResolutionScale =
        std::clamp(value.InternalResolutionScale, 0.5F, 2.0F);
    value.FovMultiplier = std::clamp(value.FovMultiplier, 1.0F, 1.5F);
    value.TaaHistoryWeight =
        std::clamp(value.TaaHistoryWeight, 0.0F, 0.98F);
    value.TaaClampExpansion =
        std::clamp(value.TaaClampExpansion, 0.0F, 0.5F);
    value.TaaSharpness = std::clamp(value.TaaSharpness, 0.0F, 1.0F);
    value.UpscalerSharpness =
        std::clamp(value.UpscalerSharpness, 0.0F, 1.0F);
    value.Effects.AoQuality = std::min<uint8_t>(value.Effects.AoQuality, 1U);
    value.Effects.AoRadius = std::clamp(value.Effects.AoRadius, 0.05F, 100.0F);
    value.Effects.AoStrength = std::clamp(value.Effects.AoStrength, 0.0F, 10.0F);
    value.Effects.AoShadowPower =
        std::clamp(value.Effects.AoShadowPower, 0.1F, 10.0F);
    value.Effects.AoShadowClamp =
        std::clamp(value.Effects.AoShadowClamp, 0.0F, 1.0F);
    value.Effects.AoHorizonAngleThreshold =
        std::clamp(value.Effects.AoHorizonAngleThreshold, 0.0F, 1.0F);
    value.Effects.AoFadeOutFrom =
        std::clamp(value.Effects.AoFadeOutFrom, 0.0F, 100000.0F);
    value.Effects.AoFadeOutTo =
        std::clamp(value.Effects.AoFadeOutTo,
                   value.Effects.AoFadeOutFrom + 0.01F, 100000.0F);
    value.Effects.AoBlurPassCount =
        std::min<uint8_t>(value.Effects.AoBlurPassCount, 8U);
    value.Effects.AoSharpness =
        std::clamp(value.Effects.AoSharpness, 0.0F, 1.0F);
    value.Effects.AoDetailStrength =
        std::clamp(value.Effects.AoDetailStrength, 0.0F, 10.0F);
    auto& directionalShadows = value.Effects.DirectionalShadows;
    if (!IsDirectionalShadowResolution(directionalShadows.Resolution)) {
        directionalShadows.Resolution = 1024U;
    }
    directionalShadows.MaximumDistance =
        std::isfinite(directionalShadows.MaximumDistance)
            ? std::clamp(directionalShadows.MaximumDistance, 50.0F, 20000.0F)
            : 2400.0F;
    directionalShadows.DepthPadding =
        std::isfinite(directionalShadows.DepthPadding)
            ? std::clamp(directionalShadows.DepthPadding, 0.0F, 5000.0F)
            : 300.0F;
    directionalShadows.DepthBiasConstant =
        std::isfinite(directionalShadows.DepthBiasConstant)
            ? std::clamp(directionalShadows.DepthBiasConstant, 0.0F, 16.0F)
            : 1.25F;
    directionalShadows.DepthBiasSlope =
        std::isfinite(directionalShadows.DepthBiasSlope)
            ? std::clamp(directionalShadows.DepthBiasSlope, 0.0F, 16.0F)
            : 1.75F;
    directionalShadows.Strength =
        std::isfinite(directionalShadows.Strength)
            ? std::clamp(directionalShadows.Strength, 0.0F, 1.0F)
            : 0.80F;
    if (directionalShadows.Mode ==
            DirectionalShadowMode::SingleCascade &&
        !capabilities.Has(GraphicsCapability::NriDirectionalShadows)) {
        directionalShadows.Mode = DirectionalShadowMode::Off;
        Correct(result.Issues,
                "Graphics.Effects.DirectionalShadows.Mode",
                capabilities.Get(
                    GraphicsCapability::NriDirectionalShadows).Reason);
    }
    directionalShadows.PcfRadius =
        std::min<uint8_t>(directionalShadows.PcfRadius, 2U);
    value.Effects.ReflectionStrength =
        std::clamp(value.Effects.ReflectionStrength, 0.0F, 2.0F);
    value.Effects.ReflectionMaxDistance =
        std::clamp(value.Effects.ReflectionMaxDistance, 10.0F, 10000.0F);
    value.Effects.ReflectionThickness =
        std::clamp(value.Effects.ReflectionThickness, 0.1F, 100.0F);
    value.Effects.ReflectionEdgeFade =
        std::clamp(value.Effects.ReflectionEdgeFade, 0.001F, 0.5F);
    value.Effects.ReflectionMaxSteps = std::clamp<uint8_t>(
        value.Effects.ReflectionMaxSteps, 8U, 64U);
    value.Effects.ReflectionRoughnessBias =
        std::clamp(value.Effects.ReflectionRoughnessBias, -0.5F, 0.5F);
    value.Effects.ReflectionDebugView =
        std::min<uint8_t>(value.Effects.ReflectionDebugView, 4U);
    if (value.Effects.ReflectionMaterials.size() > 256U) {
        value.Effects.ReflectionMaterials.resize(256U);
        Correct(result.Issues,
                "Graphics.Effects.Reflections.Materials",
                "reflection material rules truncated to 256 entries");
    }
    uint32_t nextReflectionRuleId = 1U;
    for (const auto& rule : value.Effects.ReflectionMaterials) {
        if (rule.RuleId >= nextReflectionRuleId &&
            rule.RuleId <
                std::numeric_limits<uint32_t>::max()) {
            nextReflectionRuleId = rule.RuleId + 1U;
        }
    }
    std::vector<uint32_t> reflectionRuleIds;
    reflectionRuleIds.reserve(
        value.Effects.ReflectionMaterials.size());
    for (auto& rule : value.Effects.ReflectionMaterials) {
        if (rule.RuleId == 0U ||
            std::find(reflectionRuleIds.begin(),
                      reflectionRuleIds.end(),
                      rule.RuleId) != reflectionRuleIds.end()) {
            while (std::find(reflectionRuleIds.begin(),
                             reflectionRuleIds.end(),
                             nextReflectionRuleId) !=
                   reflectionRuleIds.end()) {
                nextReflectionRuleId =
                    nextReflectionRuleId ==
                            std::numeric_limits<uint32_t>::max()
                        ? 1U
                        : nextReflectionRuleId + 1U;
            }
            rule.RuleId = nextReflectionRuleId++;
            if (nextReflectionRuleId == 0U) {
                nextReflectionRuleId = 1U;
            }
        }
        reflectionRuleIds.push_back(rule.RuleId);
        rule.Target.MapperSlotMask &= 0x07U;
        if (rule.Target.MapperSlotMask == 0U) {
            rule.Target.MapperSlotMask = 0x07U;
        }
        const auto materialDefaults =
            DefaultReflectionMaterialParameters(rule.Profile);
        rule.Reflectivity = std::isfinite(rule.Reflectivity)
            ? std::clamp(rule.Reflectivity, 0.0F, 1.0F)
            : materialDefaults.Reflectivity;
        rule.Roughness = std::isfinite(rule.Roughness)
            ? std::clamp(rule.Roughness, 0.02F, 1.0F)
            : materialDefaults.Roughness;
    }
    auto& toon = value.Effects.ToonStyle;
    toon.LightBands = std::clamp<uint8_t>(
        toon.LightBands, 2U, kMaximumToonLightBands);
    const float bandSpan = static_cast<float>(toon.LightBands - 1U);
    for (size_t index = 0; index < toon.LightBandLevels.size(); ++index) {
        const float fallback = index < toon.LightBands
            ? static_cast<float>(index) / bandSpan
            : 1.0F;
        toon.LightBandLevels[index] =
            std::isfinite(toon.LightBandLevels[index])
                ? std::clamp(toon.LightBandLevels[index], 0.0F, 1.0F)
                : fallback;
    }
    const size_t thresholdCount =
        static_cast<size_t>(toon.LightBands - 1U);
    for (size_t index = 0; index < toon.LightBandThresholds.size(); ++index) {
        const float fallback = index < thresholdCount
            ? (static_cast<float>(index) + 0.5F) / bandSpan
            : 1.0F;
        toon.LightBandThresholds[index] =
            std::isfinite(toon.LightBandThresholds[index])
                ? std::clamp(toon.LightBandThresholds[index], 0.0F, 1.0F)
                : fallback;
    }
    std::sort(toon.LightBandThresholds.begin(),
              toon.LightBandThresholds.begin() + thresholdCount);
    constexpr float kMinimumThresholdSeparation = 0.001F;
    for (size_t index = 0; index < thresholdCount; ++index) {
        const float minimum = index == 0U
            ? kMinimumThresholdSeparation
            : toon.LightBandThresholds[index - 1U] +
                  kMinimumThresholdSeparation;
        const float maximum =
            1.0F - kMinimumThresholdSeparation *
                       static_cast<float>(thresholdCount - index);
        toon.LightBandThresholds[index] =
            std::clamp(toon.LightBandThresholds[index], minimum, maximum);
    }
    toon.BandSoftness = std::clamp(toon.BandSoftness, 0.0F, 0.5F);
    toon.Saturation = std::clamp(toon.Saturation, 0.0F, 2.0F);
    for (float& component : toon.ShadowTint) {
        component = std::clamp(component, 0.0F, 1.0F);
    }
    toon.ShadowStrength = std::clamp(toon.ShadowStrength, 0.0F, 1.0F);
    toon.RimStrength = std::clamp(toon.RimStrength, 0.0F, 2.0F);
    toon.RimWidth = std::clamp(toon.RimWidth, 0.25F, 12.0F);
    for (float& component : toon.RimTint) {
        component = std::clamp(component, 0.0F, 1.0F);
    }
    toon.OutlineWidth = std::clamp(toon.OutlineWidth, 0.5F, 12.0F);
    toon.OutlineSoftness = std::clamp(toon.OutlineSoftness, 0.0F, 1.0F);
    toon.OutlineDepthSensitivity =
        std::clamp(toon.OutlineDepthSensitivity, 0.0F, 8.0F);
    toon.OutlineNormalSensitivity =
        std::clamp(toon.OutlineNormalSensitivity, 0.0F, 8.0F);
    for (float& component : toon.OutlineTint) {
        component = std::clamp(component, 0.0F, 1.0F);
    }
    toon.OutlineOpacity = std::clamp(toon.OutlineOpacity, 0.0F, 1.0F);
    value.Grass.MaxInstancesPerRoom =
        std::min(value.Grass.MaxInstancesPerRoom, 500000U);
    value.Grass.DrawDistance =
        std::clamp(value.Grass.DrawDistance, 0.0F, 50000.0F);
    value.Grass.LodReferenceDistance = value.Grass.LodReferenceDistance > 0.0F ?
        std::clamp(value.Grass.LodReferenceDistance, 1.0F, 50000.0F) : std::max(1.0F,value.Grass.DrawDistance);
    value.Grass.CullingClusterSize =
        std::clamp(value.Grass.CullingClusterSize, 10.0F, 5000.0F);
    value.Grass.LodStartFraction =
        std::clamp(value.Grass.LodStartFraction, 0.0F, 1.0F);
    value.Grass.LodEndFraction =
        std::clamp(value.Grass.LodEndFraction,
                   value.Grass.LodStartFraction, 1.0F);
    value.Grass.FarDensity =
        std::clamp(value.Grass.FarDensity, 0.01F, 1.0F);
    value.Grass.FarTuftBladeCount = std::clamp<uint8_t>(value.Grass.FarTuftBladeCount, 2U, 9U);
    const auto finiteGrass = [](float v, float fallback, float low, float high) {
        return std::isfinite(v) ? std::clamp(v, low, high) : fallback;
    };
    value.Grass.DrawFadeFraction = finiteGrass(value.Grass.DrawFadeFraction, 0.15F, 0.0F, 1.0F);
    value.Grass.DensityFadeFraction = finiteGrass(value.Grass.DensityFadeFraction, 0.10F, 0.0F, 1.0F);
    value.Grass.TuftTransitionFraction = finiteGrass(value.Grass.TuftTransitionFraction, 0.20F, 0.0F, 1.0F);
    value.Grass.FarTuftDensity = finiteGrass(value.Grass.FarTuftDensity, 1.0F, 0.1F, 4.0F);
    value.Grass.FarTuftSpread = finiteGrass(value.Grass.FarTuftSpread, 1.0F, 0.25F, 4.0F);
    value.Grass.SegmentLodSoftness = finiteGrass(value.Grass.SegmentLodSoftness, 0.5F, 0.0F, 1.0F);
    value.Grass.SegmentLodStartDistance =
        std::clamp(value.Grass.SegmentLodStartDistance, 0.0F, 10000.0F);
    value.Grass.SegmentLodEndDistance =
        std::clamp(value.Grass.SegmentLodEndDistance, value.Grass.SegmentLodStartDistance, 10000.0F);
    for (float& component : value.Grass.Appearance.RootColor) {
        component = std::clamp(component, 0.0F, 1.0F);
    }
    for (float& component : value.Grass.Appearance.TipColor) {
        component = std::clamp(component, 0.0F, 1.0F);
    }
    value.Grass.Appearance.TextureColorInfluence =
        std::clamp(
            value.Grass.Appearance.TextureColorInfluence,
            0.0F, 1.0F);
    value.Grass.Appearance.TextureRootBrightness =
        std::clamp(
            value.Grass.Appearance.TextureRootBrightness,
            0.0F, 4.0F);
    value.Grass.Appearance.TextureTipBrightness =
        std::clamp(
            value.Grass.Appearance.TextureTipBrightness,
            0.0F, 4.0F);
    value.Grass.Appearance.HeightScale =
        std::clamp(value.Grass.Appearance.HeightScale, 0.05F, 8.0F);
    auto& appearance = value.Grass.Appearance;
    appearance.ToonRimFadeStart = std::isfinite(appearance.ToonRimFadeStart)
        ? std::clamp(appearance.ToonRimFadeStart, 0.0F, 100000.0F) : 200.0F;
    appearance.ToonRimFadeEnd = std::isfinite(appearance.ToonRimFadeEnd)
        ? std::clamp(appearance.ToonRimFadeEnd, appearance.ToonRimFadeStart + 1.0F, 100001.0F)
        : std::min(appearance.ToonRimFadeStart + 600.0F, 100001.0F);
    appearance.BladeCurvature = std::clamp(appearance.BladeCurvature, 0.0F, 2.0F);
    appearance.BladeDroop = std::clamp(appearance.BladeDroop, 0.0F, 0.95F);
    appearance.ShapeVariation = std::clamp(appearance.ShapeVariation, 0.0F, 1.0F);
    appearance.BladeTwistDegrees = std::clamp(appearance.BladeTwistDegrees, 0.0F, 180.0F);
    value.Grass.Appearance.BladeSegments =
        std::clamp<uint8_t>(
            value.Grass.Appearance.BladeSegments,
            kMinimumGrassBladeSegments,
            kMaximumGrassBladeSegments);
    value.Grass.FarBladeSegments =
        std::clamp<uint8_t>(
            value.Grass.FarBladeSegments,
            kMinimumGrassBladeSegments,
            value.Grass.Appearance.BladeSegments);
    value.Grass.WindDirectionDegrees =
        std::fmod(std::max(0.0F, value.Grass.WindDirectionDegrees), 360.0F);
    value.Grass.WindStrength =
        std::clamp(value.Grass.WindStrength, 0.0F, 4.0F);
    value.Grass.WindSpeed = std::clamp(value.Grass.WindSpeed, 0.0F, 10.0F);
    value.Grass.WindSpatialScale =
        std::clamp(value.Grass.WindSpatialScale, 0.01F, 100.0F);
    value.Grass.WindGustStrength =
        std::clamp(value.Grass.WindGustStrength, 0.0F, 4.0F);
    value.Grass.WindGustFrequency =
        std::clamp(value.Grass.WindGustFrequency, 0.01F, 4.0F);
    value.Grass.WindTurbulence =
        std::clamp(value.Grass.WindTurbulence, 0.0F, 2.0F);
    value.Grass.WindRandomness =
        std::clamp(value.Grass.WindRandomness, 0.0F, 1.0F);
    value.Grass.CollisionPush =
        std::clamp(value.Grass.CollisionPush, 0.0F, 4.0F);
    value.Grass.CollisionVelocityResponse =
        std::clamp(value.Grass.CollisionVelocityResponse, 0.0F, 3.0F);
    value.Grass.ColliderRadiusMultiplier =
        std::clamp(value.Grass.ColliderRadiusMultiplier, 0.1F, 5.0F);
    value.Grass.ColliderHeightMultiplier =
        std::clamp(value.Grass.ColliderHeightMultiplier, 0.1F, 5.0F);
    value.Grass.RecoverySeconds =
        std::clamp(value.Grass.RecoverySeconds, 0.05F, 30.0F);
    value.Grass.InteractionDamping =
        std::clamp(value.Grass.InteractionDamping, 0.1F, 4.0F);
    value.Grass.MaximumBend =
        std::clamp(value.Grass.MaximumBend, 0.05F, 2.0F);
    value.Grass.InteractionFieldRadius =
        std::clamp(value.Grass.InteractionFieldRadius, 100.0F, 2500.0F);
    constexpr std::array<uint16_t, 4> fieldResolutions{32, 64, 128, 256};
    value.Grass.InteractionFieldResolution = *std::min_element(
        fieldResolutions.begin(), fieldResolutions.end(),
        [&](uint16_t left, uint16_t right) {
            return std::abs(static_cast<int>(left) -
                            static_cast<int>(value.Grass.InteractionFieldResolution)) <
                   std::abs(static_cast<int>(right) -
                            static_cast<int>(value.Grass.InteractionFieldResolution));
        });
    value.Grass.InteractionVerticalMargin =
        std::clamp(value.Grass.InteractionVerticalMargin, 0.0F, 300.0F);
    auto& grassGeneration = value.Grass.Generation;
    grassGeneration.InstancesPerSquareMeter =
        std::clamp(
            grassGeneration.InstancesPerSquareMeter, 0.0F,
            kMaximumGrassInstancesPerSquareMeter);
    grassGeneration.MinimumSpacing =
        std::clamp(
            grassGeneration.MinimumSpacing, 0.0F, 1000.0F);
    grassGeneration.IndividualRandomness =
        std::clamp(
            grassGeneration.IndividualRandomness, 0.0F, 1.0F);
    grassGeneration.ClusterStrength =
        std::clamp(
            grassGeneration.ClusterStrength, 0.0F, 1.0F);
    grassGeneration.ClusterScale =
        std::clamp(
            grassGeneration.ClusterScale, 1.0F, 10000.0F);
    grassGeneration.ClusterCoverage =
        std::clamp(
            grassGeneration.ClusterCoverage, 0.01F, 1.0F);
    grassGeneration.BladeHeightMin =
        std::clamp(
            grassGeneration.BladeHeightMin, 0.01F, 2000.0F);
    grassGeneration.BladeHeightMax =
        std::clamp(
            grassGeneration.BladeHeightMax,
            grassGeneration.BladeHeightMin, 2000.0F);
    grassGeneration.BladeWidthMin =
        std::clamp(
            grassGeneration.BladeWidthMin, 0.01F, 500.0F);
    grassGeneration.BladeWidthMax =
        std::clamp(
            grassGeneration.BladeWidthMax,
            grassGeneration.BladeWidthMin, 500.0F);
    if (value.Grass.Rules.size() > 256U) {
        value.Grass.Rules.resize(256U);
        Correct(result.Issues, "Graphics.Grass.Rules",
                "grass placement rules truncated to 256 entries");
    }
    uint32_t nextGrassRuleId = 1U;
    for (const auto& rule : value.Grass.Rules) {
        if (rule.RuleId >= nextGrassRuleId &&
            rule.RuleId < std::numeric_limits<uint32_t>::max()) {
            nextGrassRuleId = rule.RuleId + 1U;
        }
    }
    std::vector<uint32_t> grassRuleIds;
    grassRuleIds.reserve(value.Grass.Rules.size());
    for (auto& rule : value.Grass.Rules) {
        if (rule.RuleId == 0U ||
            std::find(grassRuleIds.begin(), grassRuleIds.end(),
                      rule.RuleId) != grassRuleIds.end()) {
            while (std::find(grassRuleIds.begin(), grassRuleIds.end(),
                             nextGrassRuleId) != grassRuleIds.end()) {
                nextGrassRuleId =
                    nextGrassRuleId ==
                            std::numeric_limits<uint32_t>::max()
                        ? 1U
                        : nextGrassRuleId + 1U;
            }
            rule.RuleId = nextGrassRuleId++;
            if (nextGrassRuleId == 0U) {
                nextGrassRuleId = 1U;
            }
            Correct(result.Issues, "Graphics.Grass.Rules.RuleId",
                    "missing or duplicate grass rule id regenerated");
        }
        grassRuleIds.push_back(rule.RuleId);
        rule.Target.MapperSlotMask &= 0x07U;
        if (rule.Target.MapperSlotMask == 0U) {
            rule.Target.MapperSlotMask = 0x07U;
        }
        rule.InputBlack =
            std::clamp(rule.InputBlack, 0.0F, 0.999F);
        rule.InputWhite =
            std::clamp(rule.InputWhite,
                       rule.InputBlack + 0.001F, 1.0F);
        rule.ResponseExponent =
            std::clamp(rule.ResponseExponent, 0.05F, 8.0F);
        rule.OutputBlack =
            std::clamp(rule.OutputBlack, 0.0F, 1.0F);
        rule.OutputWhite =
            std::clamp(rule.OutputWhite, 0.0F, 1.0F);
        rule.MaximumSlopeDegrees =
            std::clamp(rule.MaximumSlopeDegrees, 0.0F, 90.0F);
        rule.NormalOffset =
            std::clamp(rule.NormalOffset, -1000.0F, 1000.0F);
    }
    if (value.AntiAliasing == AntiAliasingMode::Taa &&
        (!capabilities.Has(GraphicsCapability::MotionVectors) ||
         !capabilities.Has(GraphicsCapability::TemporalHistory) ||
         !capabilities.Has(GraphicsCapability::ValidViewMetadata))) {
        value.AntiAliasing = AntiAliasingMode::Off;
        Correct(result.Issues, "Graphics.AA.Mode",
                "TAA requires motion vectors, temporal history and valid view metadata");
    }
    if (value.AntiAliasing == AntiAliasingMode::Smaa1x &&
        !capabilities.Has(GraphicsCapability::Smaa1x)) {
        value.AntiAliasing = AntiAliasingMode::Off;
        Correct(result.Issues, "Graphics.AA.Mode",
                "SMAA 1x requires the three-stage NRI compute pipeline");
    }
    if (value.AntiAliasing == AntiAliasingMode::Upscaler) {
        const bool temporal = IsTemporalUpscaler(value.Upscaler);
        const GraphicsCapability providerCapability = [&] {
            switch (value.Upscaler) {
                case UpscalerProvider::Nis:
                    return GraphicsCapability::NriNisUpscaler;
                case UpscalerProvider::Fsr:
                    return GraphicsCapability::NriFsrUpscaler;
                case UpscalerProvider::Xess:
                    return GraphicsCapability::NriXessUpscaler;
                case UpscalerProvider::Dlss:
                    return GraphicsCapability::NriDlssUpscaler;
            }
            return GraphicsCapability::NriNisUpscaler;
        }();
        const bool providerAvailable =
            capabilities.Has(providerCapability);
        const bool temporalInputsAvailable = !temporal ||
            (capabilities.Has(GraphicsCapability::MotionVectors) &&
             capabilities.Has(GraphicsCapability::TemporalHistory) &&
             capabilities.Has(GraphicsCapability::ValidViewMetadata));
        if (!providerAvailable || !temporalInputsAvailable) {
            value.AntiAliasing = AntiAliasingMode::Off;
            Correct(result.Issues, "Graphics.AA.Upscaler",
                    temporal
                        ? "the selected temporal NRI upscaler or its guides are unavailable"
                        : "NRI NIS is unavailable in this build or on this GPU");
        }
    }
    if (!IsMsaaCount(value.MsaaSamples)) {
        value.MsaaSamples = 1;
        Correct(result.Issues, "Graphics.AA.MsaaSamples",
                "unsupported sample count reset to 1");
    }
    if (value.AntiAliasing == AntiAliasingMode::Msaa) {
        const GraphicsCapability sampleCapability =
            value.MsaaSamples >= 8U ? GraphicsCapability::Msaa8x
            : value.MsaaSamples >= 4U ? GraphicsCapability::Msaa4x
                                      : GraphicsCapability::Msaa2x;
        if (value.MsaaSamples < 2U ||
            !capabilities.Has(GraphicsCapability::MultisampledDepthResolve) ||
            !capabilities.Has(sampleCapability)) {
            value.AntiAliasing = AntiAliasingMode::Off;
            value.MsaaSamples = 1;
            Correct(result.Issues, "Graphics.AA.MsaaSamples",
                    "the selected color/depth MSAA resolve is unavailable on this GPU");
        }
    } else {
        value.MsaaSamples = 1;
    }

    if (value.Window == WindowMode::ExclusiveFullscreen &&
        !capabilities.Has(GraphicsCapability::ExclusiveFullscreen)) {
        value.Window = WindowMode::Borderless;
        Correct(result.Issues, "Graphics.Window.Mode",
                capabilities.Get(GraphicsCapability::ExclusiveFullscreen).Reason);
    }
    if (value.FrameRate == FrameRateMode::Uncapped && !value.VSync &&
        !capabilities.Has(GraphicsCapability::PresentTearing)) {
        value.VSync = true;
        Correct(result.Issues, "Graphics.Presentation.VSync",
                capabilities.Get(GraphicsCapability::PresentTearing).Reason);
    }
    if (value.Effects.AmbientOcclusion == AmbientOcclusionMode::Cacao &&
        (!capabilities.Has(GraphicsCapability::NriInterop) ||
         !capabilities.Has(GraphicsCapability::SampledDepth) ||
         !capabilities.Has(GraphicsCapability::ValidViewMetadata))) {
        value.Effects.AmbientOcclusion = AmbientOcclusionMode::Off;
        Correct(result.Issues, "Graphics.Effects.AO.Mode",
                "CACAO requires NRI, sampled depth and valid view metadata");
    }
    if (value.Effects.Reflections != ReflectionMode::Off &&
        (!capabilities.Has(GraphicsCapability::NriInterop) ||
         !capabilities.Has(GraphicsCapability::SampledSceneColor) ||
         !capabilities.Has(GraphicsCapability::SampledDepth) ||
         !capabilities.Has(GraphicsCapability::NormalGuide) ||
         !capabilities.Has(GraphicsCapability::MaterialGuide) ||
         !capabilities.Has(GraphicsCapability::ValidViewMetadata))) {
        value.Effects.Reflections = ReflectionMode::Off;
        Correct(result.Issues, "Graphics.Effects.Reflections.Mode",
                "SSR requires NRI, sampled color/depth, normal/material guides "
                "and view metadata");
    }
    if (value.Effects.Reflections == ReflectionMode::FidelityFxSssr &&
        (!capabilities.Has(GraphicsCapability::FidelityFxSssr) ||
         !capabilities.Has(GraphicsCapability::MotionVectors) ||
         !capabilities.Has(GraphicsCapability::TemporalHistory) ||
         !capabilities.Has(
             GraphicsCapability::LinearHdrWorkingColor))) {
        value.Effects.Reflections = ReflectionMode::HiZ;
        Correct(result.Issues, "Graphics.Effects.Reflections.Mode",
                "FidelityFX SSSR requires its Vulkan provider, motion vectors, "
                "temporal history and linear HDR working color; falling back "
                "to Hi-Z SSR");
    }
    if (value.Preset == GraphicsPreset::Authentic) {
        auto grassRules = std::move(value.Grass.Rules);
        auto reflectionMaterials =
            std::move(value.Effects.ReflectionMaterials);
        value.InternalResolutionScale = 1.0F;
        value.AntiAliasing = AntiAliasingMode::Off;
        value.MsaaSamples = 1;
        value.FrameRate = FrameRateMode::Original30;
        value.FovMultiplier = 1.0F;
        value.Grass = {};
        value.Effects = {};
        value.Grass.Rules = std::move(grassRules);
        value.Effects.ReflectionMaterials =
            std::move(reflectionMaterials);
    }
    result.Value = std::move(value);
    return result;
}

const GraphicsSettings& GraphicsSettingsService::Current() const { return mCurrent; }
const GraphicsSettings& GraphicsSettingsService::LastKnownGood() const { return mLastKnownGood; }
const std::optional<GraphicsSettings>& GraphicsSettingsService::Pending() const { return mPending; }

GraphicsSettingsValidation GraphicsSettingsService::Stage(
    GraphicsSettings candidate, const GraphicsCapabilities& capabilities) {
    auto result = Validate(std::move(candidate), capabilities);
    if (result.Accepted()) mPending = result.Value;
    return result;
}

bool GraphicsSettingsService::ApplyPending() {
    if (!mPending.has_value()) return false;
    mCurrent = *mPending;
    mPending.reset();
    return true;
}

void GraphicsSettingsService::ConfirmCurrent() { mLastKnownGood = mCurrent; }
void GraphicsSettingsService::Rollback() { mCurrent = mLastKnownGood; mPending.reset(); }

} // namespace Fast::Oot3d
