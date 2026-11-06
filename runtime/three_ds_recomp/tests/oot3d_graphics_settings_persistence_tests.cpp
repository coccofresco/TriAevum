#include "fast/extension/graphics_settings_persistence.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace {

Fast::Extension::GraphicsSettings CompleteSettings() {
    using namespace Fast::Extension;
    auto settings =
        GraphicsSettingsService::Preset(GraphicsPreset::Custom);
    settings.Window = WindowMode::ExclusiveFullscreen;
    settings.DisplayIndex = 2U;
    settings.OutputWidth = 2560U;
    settings.OutputHeight = 1440U;
    settings.RefreshRate = 144U;
    settings.Aspect = AspectRatioMode::Ultrawide21x9;
    settings.InternalResolutionScale = 1.35F;
    settings.AntiAliasing = AntiAliasingMode::Upscaler;
    settings.Upscaler = UpscalerProvider::Fsr;
    settings.UpscalerMode = UpscalerQuality::Balanced;
    settings.UpscalerSharpness = 0.42F;
    settings.TaaHistoryWeight = 0.87F;
    settings.TaaClampExpansion = 0.12F;
    settings.TaaSharpness = 0.28F;
    settings.FrameRate = FrameRateMode::Interpolated3x;
    settings.VSync = false;
    settings.FovMultiplier = 1.32F;
    settings.TexturePacks.Azahar.DumpTextures = true;
    settings.TexturePacks.Azahar.LoadCustomTextures = true;
    settings.TexturePacks.Azahar.LoadDirectory =
        "D:/texture-packs/oot3d";
    settings.TexturePacks.Azahar.DumpDirectory =
        "E:/texture-dumps/oot3d";
    settings.Effects.SurfaceLighting = SurfaceLightingMode::PerPixelPbr;

    auto& grass = settings.Grass;
    grass.Quality = GrassQuality::Custom;
    grass.MaxInstancesPerRoom = 123456U;
    grass.DrawDistance = 1750.0F;
    grass.LodReferenceDistance = 1000.0F;
    grass.SegmentLodStartDistance = 125.0F;
    grass.SegmentLodEndDistance = 675.0F;
    grass.DrawFadeFraction = 0.23F;
    grass.DensityFadeFraction = 0.31F;
    grass.TuftTransitionFraction = 0.42F;
    grass.FarTuftDensity = 2.4F;
    grass.FarTuftSpread = 1.8F;
    grass.SegmentLodSoftness = 0.7F;
    grass.Appearance.RootColor = {0.08F, 0.16F, 0.24F};
    grass.Appearance.TipColor = {0.42F, 0.64F, 0.31F};
    grass.Appearance.HeightScale = 1.35F;
    grass.Generation.InstancesPerSquareMeter = 4096.0F;
    grass.Appearance.BladeCurvature = 1.7F;
    grass.Appearance.BladeDroop = 0.75F;
    grass.Appearance.ShapeVariation = 0.9F;
    grass.Appearance.BladeTwistDegrees = 160.0F;
    grass.Appearance.BladeSegments = 7U;
    grass.Appearance.ToonRimEnabled = false;
    grass.Appearance.ToonRimFadeStart = 350.0F;
    grass.Appearance.ToonRimFadeEnd = 1400.0F;
    grass.WindDirectionDegrees = 271.0F;
    grass.WindStrength = 0.73F;
    grass.WindSpeed = 2.3F;
    grass.WindSpatialScale = 1.7F;
    grass.WindGustStrength = 0.8F;
    grass.WindGustFrequency = 0.45F;
    grass.WindTurbulence = 0.51F;
    grass.WindRandomness = 0.67F;
    grass.CollisionPush = 1.3F;
    grass.CollisionVelocityResponse = 0.82F;
    grass.ColliderRadiusMultiplier = 1.2F;
    grass.ColliderHeightMultiplier = 1.4F;
    grass.RecoverySeconds = 1.7F;
    grass.InteractionDamping = 1.1F;
    grass.MaximumBend = 0.92F;
    grass.InteractionFieldRadius = 820.0F;
    grass.InteractionFieldResolution = 256U;
    grass.InteractionVerticalMargin = 18.0F;
    grass.Generation.InstancesPerSquareMeter = 11.0F;
    grass.Generation.MinimumSpacing = 6.0F;
    grass.Generation.Seed = 42U;
    grass.Generation.IndividualRandomness = 0.72F;
    grass.Generation.ClusterStrength = 0.54F;
    grass.Generation.ClusterScale = 310.0F;
    grass.Generation.ClusterCoverage = 0.43F;
    grass.Generation.BladeHeightMin = 16.0F;
    grass.Generation.BladeHeightMax = 48.0F;
    grass.Generation.BladeWidthMin = 2.0F;
    grass.Generation.BladeWidthMax = 5.0F;
    GrassPlacementRule grassRule;
    grassRule.RuleId = 7U;
    grassRule.Target.AssetName = "field/grass_mask";
    grassRule.Target.Rgba8Hash = 0xFEDCBA9876543210ULL;
    grassRule.Target.Width = 64U;
    grassRule.Target.Height = 32U;
    grassRule.Target.MapperSlotMask = 0x03U;
    grassRule.Channel = GrassSampleChannel::Green;
    grassRule.Wrap = GrassWrapOverride::Mirror;
    grassRule.Invert = true;
    grassRule.InputBlack = 0.21F;
    grassRule.InputWhite = 0.84F;
    grassRule.ResponseExponent = 1.4F;
    grassRule.OutputBlack = 0.08F;
    grassRule.OutputWhite = 0.93F;
    grassRule.MaximumSlopeDegrees = 37.0F;
    grassRule.NormalOffset = 0.7F;
    grassRule.ActiveRects = {{0.1F, 0.2F, 0.5F, 0.6F}, {0.7F, 0.0F, 1.0F, 0.25F}};
    grass.Rules.push_back(grassRule);
    GrassPlacementRule secondGrassRule = grassRule;
    secondGrassRule.ActiveRects.clear();
    secondGrassRule.RuleId = 8U;
    secondGrassRule.Target.AssetName = "field/flowers_mask";
    secondGrassRule.Target.Rgba8Hash = 0x1020304050607080ULL;
    secondGrassRule.Target.Width = 128U;
    secondGrassRule.Target.Height = 64U;
    secondGrassRule.Target.MapperSlotMask = 0x04U;
    secondGrassRule.Channel = GrassSampleChannel::Alpha;
    secondGrassRule.Invert = false;
    secondGrassRule.InputBlack = 0.55F;
    secondGrassRule.InputWhite = 1.0F;
    grass.Rules.push_back(secondGrassRule);
    settings.GrassSavedPreset = grass;
    settings.GrassSavedPreset.DrawDistance = 930.0F;
    settings.GrassSavedPreset.WindStrength = 0.27F;

    auto& effects = settings.Effects;
    effects.DirectionalShadows.Mode =
        DirectionalShadowMode::VirtualShadowMaps;
    effects.DirectionalShadows.Resolution = 2048U;
    effects.DirectionalShadows.MaximumDistance = 3200.0F;
    effects.DirectionalShadows.DepthBiasConstant = 1.5F;
    effects.DirectionalShadows.DepthBiasSlope = 2.25F;
    effects.DirectionalShadows.Strength = 3.25F;
    effects.DirectionalShadows.PcfRadius = 2U;
    effects.DirectionalShadows.NormalBias = 2.5F;
    effects.DirectionalShadows.ContactShadows = {true, 2U, 0.02F, 0.75F, 0.5F, false};
    effects.PointShadows.Mode = PointShadowMode::CubeMaps;
    effects.PointShadows.Resolution = 1024U;
    effects.PointShadows.MaxLights = 6U;
    effects.PointShadows.Range = 0.75F;
    effects.PointShadows.Strength = 0.5F;
    effects.GlobalIllumination.Mode = GlobalIlluminationMode::BrixelizerGi;
    effects.GlobalIllumination.Resolution = GlobalIlluminationResolution::Quarter;
    effects.GlobalIllumination.DiffuseIntensity = 1.75F;
    effects.AmbientOcclusion = AmbientOcclusionMode::Gtao;
    effects.AoQuality = 1U;
    effects.AoRadius = 4.25F;
    effects.AoDetailRadius = 1.25F;
    effects.AoStrength = 1.35F;
    effects.AoShadowPower = 2.1F;
    effects.AoShadowClamp = 0.91F;
    effects.AoFadeOutFrom = 75.0F;
    effects.AoFadeOutTo = 1400.0F;
    effects.AoSharpness = 0.77F;
    effects.AoDetailStrength = 0.68F;
    effects.Reflections = ReflectionMode::FidelityFxSssr;
    effects.ReflectionStrength = 0.88F;
    effects.ReflectionMaxDistance = 2800.0F;
    effects.ReflectionThickness = 4.2F;
    effects.ReflectionEdgeFade = 0.14F;
    effects.ReflectionMaxSteps = 56U;
    effects.ReflectionRoughnessBias = -0.08F;
    effects.ReflectionDebugView = 3U;
    ReflectionMaterialRule reflectionRule;
    reflectionRule.RuleId = 9U;
    reflectionRule.Target.ContentHash =
        0xBE15AFF93DFDCD88ULL;
    reflectionRule.Target.Width = 128U;
    reflectionRule.Target.Height = 64U;
    reflectionRule.Target.MapperSlotMask = 0x05U;
    reflectionRule.Profile = ReflectionMaterialProfile::Custom;
    reflectionRule.Reflectivity = 0.58F;
    reflectionRule.Roughness = 0.32F;
    effects.ReflectionMaterials.push_back(reflectionRule);
    effects.Toon = ToonMode::PicaMaterial;
    effects.ToonStyle.LightBands = 5U;
    effects.ToonStyle.CustomLightBands = true;
    effects.ToonStyle.LightBandLevels =
        {0.05F, 0.2F, 0.45F, 0.72F, 1.0F, 1.0F};
    effects.ToonStyle.LightBandThresholds =
        {0.1F, 0.32F, 0.58F, 0.85F, 1.0F};
    effects.ToonStyle.BandSoftness = 0.19F;
    effects.ToonStyle.Saturation = 1.22F;
    effects.ToonStyle.ShadowTint = {0.31F, 0.42F, 0.57F};
    effects.ToonStyle.ShadowStrength = 0.83F;
    effects.ToonStyle.RimStrength = 0.61F;
    effects.ToonStyle.RimWidth = 4.1F;
    effects.ToonStyle.RimTint = {0.94F, 0.81F, 0.65F};
    effects.ToonStyle.OutlineEnabled = true;
    effects.ToonStyle.OutlineWidth = 2.2F;
    effects.ToonStyle.OutlineSoftness = 0.35F;
    effects.ToonStyle.OutlineDepthSensitivity = 1.7F;
    effects.ToonStyle.OutlineNormalSensitivity = 2.3F;
    effects.ToonStyle.OutlineTint = {0.02F, 0.03F, 0.05F};
    effects.ToonStyle.OutlineOpacity = 0.72F;
    return settings;
}

void DowngradeGrassToVersionNine(nlohmann::json& grass) {
    const auto generation = grass["Generation"];
    auto sources = grass["Sources"];
    for (auto& source : sources) {
        source["Minimum"] = source["InputBlack"];
        source["Maximum"] = source["InputWhite"];
        source["Gamma"] = source["ResponseExponent"];
        source.erase("InputBlack");
        source.erase("InputWhite");
        source.erase("ResponseExponent");
        source.erase("OutputBlack");
        source.erase("OutputWhite");
        for (const auto& [name, value] :
             generation.items()) {
            source[name] = value;
        }
    }
    grass.erase("Generation");
    grass.erase("Sources");
    grass["Rules"] = std::move(sources);
}

void DowngradeGrassDocumentToVersionTen(
    nlohmann::json& document) {
    const auto downgrade = [](nlohmann::json& grass) {
        for (auto& source : grass["Sources"]) {
            source["Minimum"] = source["InputBlack"];
            source["Maximum"] = source["InputWhite"];
            source["Gamma"] = source["ResponseExponent"];
            source.erase("InputBlack");
            source.erase("InputWhite");
            source.erase("ResponseExponent");
            source.erase("OutputBlack");
            source.erase("OutputWhite");
        }
    };
    downgrade(document["Grass"]);
    if (document.contains("GrassSavedPreset")) {
        downgrade(document["GrassSavedPreset"]);
    }
}

void DowngradeGrassDocumentToVersionNine(
    nlohmann::json& document) {
    DowngradeGrassToVersionNine(document["Grass"]);
    if (document.contains("GrassSavedPreset")) {
        DowngradeGrassToVersionNine(
            document["GrassSavedPreset"]);
    }
}

TEST(Oot3dGraphicsSettingsPersistence, PreservesLegacyCacaoAndExistingGtaoChoices) {
    using namespace Fast::Extension;
    auto document = SerializeGraphicsSettings(CompleteSettings());
    document["SchemaVersion"] = 12;
    auto& ao = document["Effects"]["AO"];
    ao["Mode"] = "CACAO";
    ao["Radius"] = 8.0F;
    ao["Strength"] = 2.0F;
    ao["DetailStrength"] = 0.0F;
    ao["HorizonAngleThreshold"] = 0.12F;
    ao["BlurPassCount"] = 5;
    ao.erase("DetailRadius");
    const auto migrated = DeserializeGraphicsSettings(document);
    EXPECT_TRUE(migrated.NeedsRewrite);
    EXPECT_EQ(migrated.Value.Effects.AmbientOcclusion, AmbientOcclusionMode::Cacao);
    EXPECT_FLOAT_EQ(migrated.Value.Effects.AoRadius, 8.0F);
    EXPECT_FLOAT_EQ(migrated.Value.Effects.AoDetailRadius, 2.0F);
    EXPECT_FLOAT_EQ(migrated.Value.Effects.AoStrength, 2.0F);
    EXPECT_FLOAT_EQ(migrated.Value.Effects.AoDetailStrength, 0.0F);
    auto saved = SerializeGraphicsSettings(migrated.Value);
    EXPECT_EQ(saved["Effects"]["AO"]["Mode"], "CACAO");
    EXPECT_FLOAT_EQ(migrated.Value.Effects.AoHorizonAngleThreshold, 0.12F);
    EXPECT_EQ(migrated.Value.Effects.AoBlurPassCount, 5U);
    saved["Effects"]["AO"]["DetailRadius"] = 1.25F;
    const auto reloaded = DeserializeGraphicsSettings(saved);
    EXPECT_FALSE(reloaded.NeedsRewrite);
    EXPECT_EQ(SerializeGraphicsSettings(reloaded.Value)["Effects"]["AO"], saved["Effects"]["AO"]);
    saved["SchemaVersion"] = 13;
    saved["Effects"]["AO"]["Mode"] = "GTAO";
    const auto gtao = DeserializeGraphicsSettings(saved);
    EXPECT_EQ(gtao.Value.Effects.AmbientOcclusion, AmbientOcclusionMode::Gtao);
    EXPECT_FLOAT_EQ(gtao.Value.Effects.AoDetailRadius, 1.25F);
    ao["Mode"] = "Off";
    EXPECT_EQ(DeserializeGraphicsSettings(document).Value.Effects.AmbientOcclusion, AmbientOcclusionMode::Off);
}

TEST(Oot3dGraphicsSettingsPersistence, RoundTripsMxaoAndAddsItsDefaultsToOlderFiles) {
    using namespace Fast::Extension;
    auto settings = CompleteSettings();
    settings.Effects.AmbientOcclusion = AmbientOcclusionMode::Mxao;
    settings.Effects.MxaoMethod = MxaoEstimator::SolidAngle;
    settings.Effects.MxaoQuality = 4;
    settings.Effects.MxaoFilter = 2;
    settings.Effects.MxaoTemporal = true;
    settings.Effects.MxaoNormals = MxaoNormalSource::Renderer;
    settings.Effects.MxaoShadingRate = 2;
    settings.Effects.MxaoRadiusGrowsWithDistance = true;
    settings.Effects.MxaoScreenRadius = 4.5F;
    settings.Effects.MxaoOriginalFade = true;
    settings.Effects.MxaoFadeDistance = 0.4F;
    settings.Effects.MxaoDebug = MxaoDebugView::NormalDisagreement;
    const auto saved = SerializeGraphicsSettings(settings);
    EXPECT_EQ(saved["Effects"]["AO"]["Mxao"]["Normals"], "Renderer");
    EXPECT_EQ(saved["Effects"]["AO"]["Mxao"]["ShadingRate"], 2U);
    EXPECT_FALSE(saved["Effects"]["AO"]["Mxao"].contains("Debug"));
    EXPECT_EQ(saved["Effects"]["AO"]["Mode"], "MXAO");
    EXPECT_EQ(saved["Effects"]["AO"]["Mxao"]["Method"], "SolidAngle");
    EXPECT_EQ(saved["Effects"]["AO"]["Mxao"]["Temporal"], true);
    const auto loaded = DeserializeGraphicsSettings(saved);
    EXPECT_FALSE(loaded.NeedsRewrite);
    EXPECT_EQ(loaded.Value.Effects.AmbientOcclusion, AmbientOcclusionMode::Mxao);
    EXPECT_EQ(loaded.Value.Effects.MxaoMethod, MxaoEstimator::SolidAngle);
    EXPECT_EQ(loaded.Value.Effects.MxaoQuality, 4U);
    EXPECT_EQ(loaded.Value.Effects.MxaoFilter, 2U);
    EXPECT_TRUE(loaded.Value.Effects.MxaoTemporal);
    EXPECT_EQ(loaded.Value.Effects.MxaoNormals, MxaoNormalSource::Renderer);
    EXPECT_EQ(loaded.Value.Effects.MxaoShadingRate, 2U);
    EXPECT_TRUE(loaded.Value.Effects.MxaoRadiusGrowsWithDistance);
    EXPECT_FLOAT_EQ(loaded.Value.Effects.MxaoScreenRadius, 4.5F);
    EXPECT_TRUE(loaded.Value.Effects.MxaoOriginalFade);
    EXPECT_FLOAT_EQ(loaded.Value.Effects.MxaoFadeDistance, 0.4F);
    // Debug views are not saved.
    EXPECT_EQ(loaded.Value.Effects.MxaoDebug, MxaoDebugView::Off);
    // Files written before the original's remaining options get the defaults
    // (face normals) and are rewritten.
    // Face normals round-trip under their own name.
    auto geometry = settings;
    geometry.Effects.MxaoNormals = MxaoNormalSource::Geometry;
    const auto savedGeometry = SerializeGraphicsSettings(geometry);
    EXPECT_EQ(savedGeometry["Effects"]["AO"]["Mxao"]["Normals"], "Geometry");
    EXPECT_EQ(DeserializeGraphicsSettings(savedGeometry).Value.Effects.MxaoNormals, MxaoNormalSource::Geometry);
    auto withoutOptions = saved;
    for (const char* name : { "Normals", "ShadingRate", "IncreaseRadiusWithDistance", "ScreenRadius",
                              "OriginalFade", "FadeDistance" })
        withoutOptions["Effects"]["AO"]["Mxao"].erase(name);
    const auto optionDefaults = DeserializeGraphicsSettings(withoutOptions);
    EXPECT_TRUE(optionDefaults.NeedsRewrite);
    EXPECT_EQ(optionDefaults.Value.Effects.MxaoNormals, MxaoNormalSource::Geometry);
    EXPECT_EQ(optionDefaults.Value.Effects.MxaoShadingRate, 0U);
    EXPECT_FALSE(optionDefaults.Value.Effects.MxaoRadiusGrowsWithDistance);
    EXPECT_FLOAT_EQ(optionDefaults.Value.Effects.MxaoScreenRadius, 2.5F);
    EXPECT_FALSE(optionDefaults.Value.Effects.MxaoOriginalFade);
    EXPECT_FLOAT_EQ(optionDefaults.Value.Effects.MxaoFadeDistance, 0.25F);
    EXPECT_TRUE(optionDefaults.Value.Effects.MxaoTemporal);
    // Files written before temporal accumulation keep it off and are rewritten.
    auto spatial = saved;
    spatial["Effects"]["AO"]["Mxao"].erase("Temporal");
    const auto withoutTemporal = DeserializeGraphicsSettings(spatial);
    EXPECT_TRUE(withoutTemporal.NeedsRewrite);
    EXPECT_FALSE(withoutTemporal.Value.Effects.MxaoTemporal);
    EXPECT_EQ(withoutTemporal.Value.Effects.MxaoQuality, 4U);
    auto older = saved;
    older["Effects"]["AO"].erase("Mxao");
    const auto defaults = DeserializeGraphicsSettings(older);
    EXPECT_TRUE(defaults.NeedsRewrite);
    EXPECT_EQ(defaults.Value.Effects.AmbientOcclusion, AmbientOcclusionMode::Mxao);
    EXPECT_EQ(defaults.Value.Effects.MxaoMethod, MxaoEstimator::VisibilityBitmask);
    EXPECT_EQ(defaults.Value.Effects.MxaoQuality, 1U);
    EXPECT_EQ(defaults.Value.Effects.MxaoFilter, 1U);
    EXPECT_FALSE(defaults.Value.Effects.MxaoTemporal);
}

TEST(Oot3dGraphicsSettingsPersistence, RoundTripsAoTextureReliefAndDefaultsOlderFilesToOff) {
    using namespace Fast::Extension;
    auto settings = CompleteSettings();
    settings.Effects.AmbientOcclusion = AmbientOcclusionMode::Mxao;
    settings.Effects.AoRelief = AoTextureRelief::Strong;
    const auto saved = SerializeGraphicsSettings(settings);
    EXPECT_EQ(saved["Effects"]["AO"]["TextureRelief"], "Strong");
    const auto loaded = DeserializeGraphicsSettings(saved);
    EXPECT_FALSE(loaded.NeedsRewrite);
    EXPECT_EQ(loaded.Value.Effects.AoRelief, AoTextureRelief::Strong);
    auto older = saved;
    older["Effects"]["AO"].erase("TextureRelief");
    const auto defaults = DeserializeGraphicsSettings(older);
    EXPECT_TRUE(defaults.NeedsRewrite);
    EXPECT_TRUE(defaults.Issues.empty());
    EXPECT_EQ(defaults.Value.Effects.AoRelief, AoTextureRelief::Off);
    auto unknown = saved;
    unknown["Effects"]["AO"]["TextureRelief"] = "Extreme";
    const auto corrected = DeserializeGraphicsSettings(unknown);
    EXPECT_EQ(corrected.Value.Effects.AoRelief, AoTextureRelief::Off);
    EXPECT_FALSE(corrected.Issues.empty());
}

TEST(Oot3dGraphicsSettingsPersistence, RoundTripsExtendedDrawDistanceAndDefaultsOlderFilesToNative) {
    using namespace Fast::Extension;
    auto settings = CompleteSettings();
    settings.ExtendedDrawDistance = true;
    const auto saved = SerializeGraphicsSettings(settings);
    EXPECT_EQ(saved["Camera"]["ExtendedDrawDistance"], true);
    const auto loaded = DeserializeGraphicsSettings(saved);
    EXPECT_TRUE(loaded.Issues.empty());
    EXPECT_TRUE(loaded.Value.ExtendedDrawDistance);
    auto older = saved;
    older["Camera"].erase("ExtendedDrawDistance");
    const auto defaults = DeserializeGraphicsSettings(older);
    EXPECT_TRUE(defaults.Issues.empty());
    EXPECT_FALSE(defaults.Value.ExtendedDrawDistance);
    auto invalid = saved;
    invalid["Camera"]["ExtendedDrawDistance"] = "far";
    const auto corrected = DeserializeGraphicsSettings(invalid);
    EXPECT_FALSE(corrected.Value.ExtendedDrawDistance);
    EXPECT_FALSE(corrected.Issues.empty());
}

// Issue #122: anisotropic filtering, 4x in Enhanced; a file from before it
// takes its preset's value; unsupported counts and devices without the
// feature fall back to off.
TEST(Oot3dGraphicsSettingsPersistence, RoundTripsAnisotropyAndDefaultsOlderFilesByPreset) {
    using namespace Fast::Extension;
    EXPECT_EQ(EnhancedPresetSettings().Anisotropy, 4U);
    EXPECT_EQ(GraphicsSettingsService::Preset(GraphicsPreset::Authentic).Anisotropy, 1U);
    auto settings = CompleteSettings();
    settings.Anisotropy = 8U;
    const auto saved = SerializeGraphicsSettings(settings);
    EXPECT_EQ(saved["Textures"]["Anisotropy"], 8);
    const auto loaded = DeserializeGraphicsSettings(saved);
    EXPECT_TRUE(loaded.Issues.empty());
    EXPECT_EQ(loaded.Value.Anisotropy, 8U);
    auto older = saved;
    older.erase("Textures");
    older["Preset"] = "Enhanced";
    EXPECT_EQ(DeserializeGraphicsSettings(older).Value.Anisotropy, 4U);
    older["Preset"] = "Custom";
    EXPECT_EQ(DeserializeGraphicsSettings(older).Value.Anisotropy, 1U);

    GraphicsCapabilities capabilities;
    capabilities.Set(GraphicsCapability::AnisotropicFiltering, true);
    auto candidate = settings;
    candidate.Anisotropy = 3U;
    auto validation = GraphicsSettingsService::Validate(candidate, capabilities);
    EXPECT_EQ(validation.Value.Anisotropy, 1U);
    candidate.Anisotropy = 16U;
    EXPECT_EQ(GraphicsSettingsService::Validate(candidate, capabilities).Value.Anisotropy, 16U);
    // It is honoured in Authentic too (the owner's choice).
    candidate.Preset = GraphicsPreset::Authentic;
    EXPECT_EQ(GraphicsSettingsService::Validate(candidate, capabilities).Value.Anisotropy, 16U);
    capabilities.Set(GraphicsCapability::AnisotropicFiltering, false, "test");
    EXPECT_EQ(GraphicsSettingsService::Validate(candidate, capabilities).Value.Anisotropy, 1U);
}

TEST(Oot3dGraphicsSettingsPersistence, MigratesSingleCascadeWithoutLosingTheOffChoiceOrLightResponse) {
    using namespace Fast::Extension;
    auto document = SerializeGraphicsSettings(CompleteSettings());
    document["SchemaVersion"] = 11;
    auto& shadows = document["Effects"]["DirectionalShadows"];
    shadows["Mode"] = "SingleCascade";
    shadows["Resolution"] = 1024;
    shadows["MaximumDistance"] = 1310;
    shadows.erase("NormalBias");
    shadows.erase("ContactShadows");
    shadows["DepthPadding"] = 30;
    shadows["Stabilize"] = false;
    const auto migrated = DeserializeGraphicsSettings(document);
    EXPECT_TRUE(migrated.NeedsRewrite);
    EXPECT_EQ(migrated.Value.Effects.DirectionalShadows.Mode, DirectionalShadowMode::VirtualShadowMaps);
    EXPECT_EQ(migrated.Value.Effects.DirectionalShadows.Resolution, 2048U);
    EXPECT_FLOAT_EQ(migrated.Value.Effects.DirectionalShadows.MaximumDistance, 65536.0F);
    EXPECT_FLOAT_EQ(migrated.Value.Effects.DirectionalShadows.Strength, 3.25F);
    EXPECT_FLOAT_EQ(migrated.Value.Effects.DirectionalShadows.DepthBiasSlope, 2.25F);
    // Documents from before the normal bias take its default.
    EXPECT_FLOAT_EQ(migrated.Value.Effects.DirectionalShadows.NormalBias, DirectionalShadowSettings{}.NormalBias);
    EXPECT_EQ(migrated.Value.Effects.DirectionalShadows.ContactShadows, ContactShadowSettings{});
    const auto saved = SerializeGraphicsSettings(migrated.Value);
    const auto reloaded = DeserializeGraphicsSettings(saved);
    EXPECT_FALSE(reloaded.NeedsRewrite);
    EXPECT_EQ(SerializeGraphicsSettings(reloaded.Value)["Effects"]["DirectionalShadows"],
              saved["Effects"]["DirectionalShadows"]);
    EXPECT_FALSE(saved["Effects"]["DirectionalShadows"].contains("DepthPadding"));
    EXPECT_FALSE(saved["Effects"]["DirectionalShadows"].contains("Stabilize"));
    shadows["Mode"] = "Off";
    EXPECT_EQ(DeserializeGraphicsSettings(document).Value.Effects.DirectionalShadows.Mode, DirectionalShadowMode::Off);
}

TEST(Oot3dGraphicsSettingsPersistence, MigratesCoupledSegmentLodWithoutMovingItsThresholds) {
    auto settings = CompleteSettings();
    settings.Grass.DrawDistance = 5000.0F;
    settings.Grass.LodStartFraction = 0.3F;
    settings.Grass.LodEndFraction = 0.6F;
    auto document = Fast::Extension::SerializeGraphicsSettings(settings);
    auto& performance = document["Grass"]["Performance"];
    performance.erase("SegmentLodStartDistance");
    performance.erase("SegmentLodEndDistance");
    const auto loaded = Fast::Extension::DeserializeGraphicsSettings(document);
    EXPECT_FLOAT_EQ(loaded.Value.Grass.SegmentLodStartDistance, 1500.0F);
    EXPECT_FLOAT_EQ(loaded.Value.Grass.SegmentLodEndDistance, 3000.0F);
}

TEST(Oot3dGraphicsSettingsPersistence, NullActiveRectsMeanTheWholeMask) {
    // The title's shipped rules write null; it is not a malformed document.
    auto document = Fast::Extension::SerializeGraphicsSettings(CompleteSettings());
    document["Grass"]["Sources"][0]["ActiveRects"] = nullptr;
    const auto loaded = Fast::Extension::DeserializeGraphicsSettings(document);
    EXPECT_TRUE(loaded.Issues.empty());
    ASSERT_EQ(loaded.Value.Grass.Rules.size(), 2U);
    EXPECT_TRUE(loaded.Value.Grass.Rules[0].ActiveRects.empty());
}

TEST(Oot3dGraphicsSettingsPersistence, RoundTripsCompleteSettings) {
    const auto original = CompleteSettings();
    const auto document =
        Fast::Extension::SerializeGraphicsSettings(original);
    const auto loaded =
        Fast::Extension::DeserializeGraphicsSettings(document);
    ASSERT_TRUE(loaded.Found);
    EXPECT_FALSE(loaded.NeedsRewrite);
    EXPECT_FALSE(loaded.UnsupportedFutureVersion);
    EXPECT_TRUE(loaded.Issues.empty());
    EXPECT_EQ(Fast::Extension::SerializeGraphicsSettings(loaded.Value),
              document);
    EXPECT_EQ(loaded.Value.Effects.SurfaceLighting,
              Fast::Extension::SurfaceLightingMode::PerPixelPbr);
    ASSERT_EQ(loaded.Value.Grass.Rules.size(), 2U);
    EXPECT_EQ(loaded.Value.Grass.Rules[0].Target.Rgba8Hash,
              0xFEDCBA9876543210ULL);
    ASSERT_EQ(loaded.Value.Grass.Rules[0].ActiveRects.size(), 2U);
    EXPECT_EQ(loaded.Value.Grass.Rules[0].ActiveRects[1],
              (Fast::Extension::GrassMaskRect{0.7F, 0.0F, 1.0F, 0.25F}));
    EXPECT_TRUE(loaded.Value.Grass.Rules[1].ActiveRects.empty());
    EXPECT_EQ(
        loaded.Value.Grass.Rules[1].Channel,
        Fast::Extension::GrassSampleChannel::Alpha);
    EXPECT_FLOAT_EQ(
        loaded.Value.Grass.Generation
            .InstancesPerSquareMeter,
        11.0F);
    ASSERT_EQ(loaded.Value.Effects.ReflectionMaterials.size(),
              1U);
    EXPECT_EQ(
        loaded.Value.Effects.ReflectionMaterials[0]
            .Target.ContentHash,
        0xBE15AFF93DFDCD88ULL);
    EXPECT_FLOAT_EQ(
        loaded.Value.GrassSavedPreset.DrawDistance, 930.0F);
    EXPECT_FLOAT_EQ(
        loaded.Value.GrassSavedPreset.WindStrength, 0.27F);
    EXPECT_EQ(loaded.Value.Effects.GlobalIllumination.Mode,
              Fast::Extension::GlobalIlluminationMode::BrixelizerGi);
    EXPECT_FLOAT_EQ(loaded.Value.Effects.GlobalIllumination.DiffuseIntensity, 1.75F);
}

TEST(Oot3dGraphicsSettingsPersistence, AddsGlobalIlluminationOffToOlderFiles) {
    using namespace Fast::Extension;
    auto document = SerializeGraphicsSettings(CompleteSettings());
    EXPECT_EQ(document["Effects"]["GlobalIllumination"]["Mode"], "BrixelizerGI");
    EXPECT_EQ(document["Effects"]["GlobalIllumination"]["Resolution"], "Quarter");
    document["Effects"].erase("GlobalIllumination");
    const auto loaded = DeserializeGraphicsSettings(document);
    EXPECT_TRUE(loaded.NeedsRewrite);
    EXPECT_TRUE(loaded.Issues.empty());
    EXPECT_EQ(loaded.Value.Effects.GlobalIllumination.Mode, GlobalIlluminationMode::Off);
    EXPECT_EQ(loaded.Value.Effects.GlobalIllumination.Resolution, GlobalIlluminationResolution::Half);
    EXPECT_FLOAT_EQ(loaded.Value.Effects.GlobalIllumination.DiffuseIntensity, 1.0F);
}

TEST(Oot3dGraphicsSettingsPersistence, AddsSoftGrassLodDefaultsWithoutMovingExistingDistances) {
    using namespace Fast::Extension;
    const auto original = CompleteSettings();
    auto document = SerializeGraphicsSettings(original);
    for (const auto* section : {"Grass", "GrassSavedPreset"}) {
        for (const auto* key : {"DrawFadeFraction", "DensityFadeFraction", "TuftTransitionFraction",
                                "FarTuftDensity", "FarTuftSpread", "SegmentLodSoftness"})
            document[section]["Performance"].erase(key);
    }
    const auto loaded = DeserializeGraphicsSettings(document);
    EXPECT_TRUE(loaded.Issues.empty());
    EXPECT_FLOAT_EQ(loaded.Value.Grass.DrawDistance, original.Grass.DrawDistance);
    EXPECT_FLOAT_EQ(loaded.Value.Grass.LodReferenceDistance, original.Grass.LodReferenceDistance);
    EXPECT_FLOAT_EQ(loaded.Value.Grass.SegmentLodEndDistance, original.Grass.SegmentLodEndDistance);
    EXPECT_FLOAT_EQ(loaded.Value.Grass.DrawFadeFraction, 0.15F);
    EXPECT_FLOAT_EQ(loaded.Value.Grass.FarTuftDensity, 1);
    EXPECT_FLOAT_EQ(loaded.Value.Grass.FarTuftSpread, 1);
    EXPECT_FLOAT_EQ(loaded.Value.Grass.TuftTransitionFraction, 0.2F);
    EXPECT_EQ(loaded.Value.Grass.Rules.size(), original.Grass.Rules.size());
}

TEST(Oot3dGraphicsSettingsPersistence, CrossedBladesDefaultOffAndRoundTrip) {
    using namespace Fast::Extension;
    auto original = CompleteSettings();
    original.Grass.Appearance.CrossedBlades = true;
    const auto roundTrip = DeserializeGraphicsSettings(SerializeGraphicsSettings(original));
    EXPECT_TRUE(roundTrip.Issues.empty());
    EXPECT_TRUE(roundTrip.Value.Grass.Appearance.CrossedBlades);
    auto document = SerializeGraphicsSettings(original);
    for (const auto* section : {"Grass", "GrassSavedPreset"})
        document[section]["Appearance"].erase("CrossedBlades");
    const auto loaded = DeserializeGraphicsSettings(document);
    EXPECT_TRUE(loaded.Issues.empty());
    EXPECT_FALSE(loaded.Value.Grass.Appearance.CrossedBlades);
}

TEST(Oot3dGraphicsSettingsPersistence, AddsDistantCardDefaultsAndRoundTripsThem) {
    using namespace Fast::Extension;
    auto original = CompleteSettings();
    original.Grass.CardLodEnabled = true;
    original.Grass.CardLodBladeDistance = 1200.0F;
    original.Grass.CardLodClumpDistance = 4500.0F;
    original.Grass.CardLodGroupDistance = 16000.0F;
    original.Grass.CardLodBlendFraction = 0.3F;
    const auto roundTrip = DeserializeGraphicsSettings(SerializeGraphicsSettings(original));
    EXPECT_TRUE(roundTrip.Issues.empty());
    EXPECT_TRUE(roundTrip.Value.Grass.CardLodEnabled);
    EXPECT_FLOAT_EQ(roundTrip.Value.Grass.CardLodBladeDistance, 1200.0F);
    EXPECT_FLOAT_EQ(roundTrip.Value.Grass.CardLodClumpDistance, 4500.0F);
    EXPECT_FLOAT_EQ(roundTrip.Value.Grass.CardLodGroupDistance, 16000.0F);
    EXPECT_FLOAT_EQ(roundTrip.Value.Grass.CardLodBlendFraction, 0.3F);

    auto document = SerializeGraphicsSettings(original);
    for (const auto* section : {"Grass", "GrassSavedPreset"}) {
        for (const auto* key : {"CardLodEnabled", "CardLodBladeDistance", "CardLodClumpDistance",
                                "CardLodGroupDistance", "CardLodBlendFraction"})
            document[section]["Performance"].erase(key);
    }
    const auto loaded = DeserializeGraphicsSettings(document);
    EXPECT_TRUE(loaded.Issues.empty());
    EXPECT_FALSE(loaded.Value.Grass.CardLodEnabled);
    EXPECT_FLOAT_EQ(loaded.Value.Grass.CardLodBladeDistance, 1000.0F);
    EXPECT_FLOAT_EQ(loaded.Value.Grass.CardLodClumpDistance, 4000.0F);
    EXPECT_FLOAT_EQ(loaded.Value.Grass.CardLodGroupDistance, 15000.0F);
    EXPECT_FLOAT_EQ(loaded.Value.Grass.CardLodBlendFraction, 0.25F);
    EXPECT_FLOAT_EQ(loaded.Value.Grass.DrawDistance, original.Grass.DrawDistance);

    // Tier ends stay ordered when the file disagrees.
    document = SerializeGraphicsSettings(original);
    document["Grass"]["Performance"]["CardLodClumpDistance"] = 500.0F;
    document["Grass"]["Performance"]["CardLodGroupDistance"] = 50.0F;
    const auto disordered = DeserializeGraphicsSettings(document);
    EXPECT_GE(disordered.Value.Grass.CardLodClumpDistance, disordered.Value.Grass.CardLodBladeDistance);
    EXPECT_GE(disordered.Value.Grass.CardLodGroupDistance, disordered.Value.Grass.CardLodClumpDistance);
}

TEST(Oot3dGraphicsSettingsPersistence, MigratesLegacyFixed60NameToInterpolated2x) {
    auto document = Fast::Extension::SerializeGraphicsSettings(CompleteSettings());
    document["FrameRate"]["Mode"] = "Fixed60";
    const auto loaded = Fast::Extension::DeserializeGraphicsSettings(document);
    ASSERT_TRUE(loaded.Found);
    EXPECT_EQ(loaded.Value.FrameRate, Fast::Extension::FrameRateMode::Interpolated2x);
    EXPECT_EQ(Fast::Extension::SerializeGraphicsSettings(loaded.Value)["FrameRate"]["Mode"], "Interpolated2x");
}

TEST(Oot3dGraphicsSettingsPersistence,
     MigratesVersionSixLiveGrassAsSavedPreset) {
    auto document =
        Fast::Extension::SerializeGraphicsSettings(CompleteSettings());
    document["SchemaVersion"] = 6U;
    DowngradeGrassDocumentToVersionNine(document);
    document.erase("GrassSavedPreset");
    document["Grass"]["Budget"]["DrawDistance"] = 2468.0F;
    document["Grass"]["Wind"]["Strength"] = 0.91F;

    const auto loaded =
        Fast::Extension::DeserializeGraphicsSettings(document);
    ASSERT_TRUE(loaded.Found);
    EXPECT_TRUE(loaded.NeedsRewrite);
    EXPECT_FLOAT_EQ(loaded.Value.Grass.DrawDistance, 2468.0F);
    EXPECT_FLOAT_EQ(
        loaded.Value.GrassSavedPreset.DrawDistance, 2468.0F);
    EXPECT_FLOAT_EQ(
        loaded.Value.GrassSavedPreset.WindStrength, 0.91F);
    const auto migrated =
        Fast::Extension::SerializeGraphicsSettings(loaded.Value);
    EXPECT_EQ(migrated["GrassSavedPreset"], migrated["Grass"]);
}

TEST(Oot3dGraphicsSettingsPersistence,
     MigratesVersionSevenWithTexturePacksDisabled) {
    auto document =
        Fast::Extension::SerializeGraphicsSettings(CompleteSettings());
    document["SchemaVersion"] = 7U;
    DowngradeGrassDocumentToVersionNine(document);
    document.erase("TexturePacks");

    const auto loaded =
        Fast::Extension::DeserializeGraphicsSettings(document);
    ASSERT_TRUE(loaded.Found);
    EXPECT_TRUE(loaded.NeedsRewrite);
    EXPECT_FALSE(
        loaded.Value.TexturePacks.Azahar.DumpTextures);
    EXPECT_FALSE(
        loaded.Value.TexturePacks.Azahar.LoadCustomTextures);
}

TEST(Oot3dGraphicsSettingsPersistence,
     MigratesVersionEightWithDefaultTextureDirectories) {
    auto document =
        Fast::Extension::SerializeGraphicsSettings(CompleteSettings());
    document["SchemaVersion"] = 8U;
    DowngradeGrassDocumentToVersionNine(document);
    document["TexturePacks"]["Azahar"].erase("LoadDirectory");
    document["TexturePacks"]["Azahar"].erase("DumpDirectory");

    const auto loaded =
        Fast::Extension::DeserializeGraphicsSettings(document);
    ASSERT_TRUE(loaded.Found);
    EXPECT_TRUE(loaded.NeedsRewrite);
    EXPECT_TRUE(
        loaded.Value.TexturePacks.Azahar.DumpTextures);
    EXPECT_TRUE(
        loaded.Value.TexturePacks.Azahar.LoadCustomTextures);
    EXPECT_TRUE(
        loaded.Value.TexturePacks.Azahar.LoadDirectory.empty());
    EXPECT_TRUE(
        loaded.Value.TexturePacks.Azahar.DumpDirectory.empty());
}

TEST(Oot3dGraphicsSettingsPersistence,
     MigratesVersionNineToSharedGrassGeneration) {
    auto document =
        Fast::Extension::SerializeGraphicsSettings(CompleteSettings());
    document["SchemaVersion"] = 9U;
    DowngradeGrassDocumentToVersionNine(document);
    document["Grass"]["Rules"][0]
            ["InstancesPerSquareMeter"] = 777.0F;
    document["Grass"]["Rules"][1]
            ["InstancesPerSquareMeter"] = 12.0F;

    const auto loaded =
        Fast::Extension::DeserializeGraphicsSettings(document);
    ASSERT_TRUE(loaded.Found);
    EXPECT_TRUE(loaded.NeedsRewrite);
    ASSERT_EQ(loaded.Value.Grass.Rules.size(), 2U);
    EXPECT_FLOAT_EQ(
        loaded.Value.Grass.Generation
            .InstancesPerSquareMeter,
        777.0F);
    EXPECT_EQ(
        loaded.Value.Grass.Rules[0].Channel,
        Fast::Extension::GrassSampleChannel::Green);
    EXPECT_EQ(
        loaded.Value.Grass.Rules[1].Channel,
        Fast::Extension::GrassSampleChannel::Alpha);

    const auto migrated =
        Fast::Extension::SerializeGraphicsSettings(loaded.Value);
    EXPECT_TRUE(migrated["Grass"].contains("Generation"));
    EXPECT_TRUE(migrated["Grass"].contains("Sources"));
    EXPECT_FALSE(migrated["Grass"].contains("Rules"));
    EXPECT_FALSE(
        migrated["Grass"]["Sources"][0].contains(
            "InstancesPerSquareMeter"));
}

TEST(Oot3dGraphicsSettingsPersistence,
     MigratesVersionTenGrassThresholdsToMaskLevels) {
    auto document =
        Fast::Extension::SerializeGraphicsSettings(CompleteSettings());
    document["SchemaVersion"] = 10U;
    DowngradeGrassDocumentToVersionTen(document);

    const auto loaded =
        Fast::Extension::DeserializeGraphicsSettings(document);
    ASSERT_TRUE(loaded.Found);
    EXPECT_TRUE(loaded.NeedsRewrite);
    ASSERT_EQ(loaded.Value.Grass.Rules.size(), 2U);
    EXPECT_FLOAT_EQ(
        loaded.Value.Grass.Rules[0].InputBlack, 0.21F);
    EXPECT_FLOAT_EQ(
        loaded.Value.Grass.Rules[0].InputWhite, 0.84F);
    EXPECT_FLOAT_EQ(
        loaded.Value.Grass.Rules[0].ResponseExponent, 1.4F);
    EXPECT_FLOAT_EQ(
        loaded.Value.Grass.Rules[0].OutputBlack, 0.0F);
    EXPECT_FLOAT_EQ(
        loaded.Value.Grass.Rules[0].OutputWhite, 1.0F);

    const auto migrated =
        Fast::Extension::SerializeGraphicsSettings(loaded.Value);
    EXPECT_TRUE(
        migrated["Grass"]["Sources"][0].contains("InputBlack"));
    EXPECT_FALSE(
        migrated["Grass"]["Sources"][0].contains("Minimum"));
}

TEST(Oot3dGraphicsSettingsPersistence,
     MigratesVersionOneToEqualLightBandProfile) {
    auto document =
        Fast::Extension::SerializeGraphicsSettings(CompleteSettings());
    document["SchemaVersion"] = 1U;
    DowngradeGrassDocumentToVersionNine(document);
    auto& toon = document["Effects"]["Toon"];
    toon.erase("CustomLightBands");
    toon.erase("LightBandLevels");
    toon.erase("LightBandThresholds");

    const auto loaded =
        Fast::Extension::DeserializeGraphicsSettings(document);
    ASSERT_TRUE(loaded.Found);
    EXPECT_TRUE(loaded.NeedsRewrite);
    const auto& style = loaded.Value.Effects.ToonStyle;
    EXPECT_FALSE(style.CustomLightBands);
    EXPECT_FLOAT_EQ(style.LightBandLevels[0], 0.0F);
    EXPECT_FLOAT_EQ(style.LightBandLevels[1], 0.25F);
    EXPECT_FLOAT_EQ(style.LightBandLevels[4], 1.0F);
    EXPECT_FLOAT_EQ(style.LightBandThresholds[0], 0.125F);
    EXPECT_FLOAT_EQ(style.LightBandThresholds[3], 0.875F);
}

TEST(Oot3dGraphicsSettingsPersistence, PointShadowsRoundTripAndDefaultWhenAbsent) {
    const auto settings = CompleteSettings();
    auto document = Fast::Extension::SerializeGraphicsSettings(settings);
    const auto& saved = document["Effects"]["PointShadows"];
    EXPECT_EQ(saved["Mode"], "CubeMaps");
    EXPECT_EQ(saved["Resolution"], 1024U);
    EXPECT_EQ(saved["MaxLights"], 6U);
    const auto loaded = Fast::Extension::DeserializeGraphicsSettings(document);
    ASSERT_TRUE(loaded.Found);
    EXPECT_EQ(loaded.Value.Effects.PointShadows.Mode, Fast::Extension::PointShadowMode::CubeMaps);
    EXPECT_EQ(loaded.Value.Effects.PointShadows.Resolution, 1024U);
    EXPECT_EQ(loaded.Value.Effects.PointShadows.MaxLights, 6U);
    EXPECT_FLOAT_EQ(loaded.Value.Effects.PointShadows.Range, 0.75F);
    EXPECT_FLOAT_EQ(loaded.Value.Effects.PointShadows.Strength, 0.5F);

    document["Effects"].erase("PointShadows");
    const auto absent = Fast::Extension::DeserializeGraphicsSettings(document);
    ASSERT_TRUE(absent.Found);
    EXPECT_EQ(absent.Value.Effects.PointShadows.Mode, Fast::Extension::PointShadowMode::Off);
    EXPECT_EQ(absent.Value.Effects.PointShadows.Resolution, Fast::Extension::PointShadowSettings{}.Resolution);
}

TEST(Oot3dGraphicsSettingsPersistence,
     MigratesVersionTwoWithDirectionalShadowsDisabled) {
    auto document =
        Fast::Extension::SerializeGraphicsSettings(CompleteSettings());
    document["SchemaVersion"] = 2U;
    DowngradeGrassDocumentToVersionNine(document);
    document["Effects"].erase("DirectionalShadows");

    const auto loaded =
        Fast::Extension::DeserializeGraphicsSettings(document);
    ASSERT_TRUE(loaded.Found);
    EXPECT_TRUE(loaded.NeedsRewrite);
    EXPECT_EQ(loaded.Value.Effects.DirectionalShadows.Mode,
              Fast::Extension::DirectionalShadowMode::Off);
}

TEST(Oot3dGraphicsSettingsPersistence,
     MigratesVersionThreeGrassAppearanceDefaults) {
    auto document =
        Fast::Extension::SerializeGraphicsSettings(CompleteSettings());
    document["SchemaVersion"] = 3U;
    DowngradeGrassDocumentToVersionNine(document);
    document["Grass"].erase("Appearance");
    document["Grass"]["Wind"].erase("Randomness");

    const auto loaded =
        Fast::Extension::DeserializeGraphicsSettings(document);
    ASSERT_TRUE(loaded.Found);
    EXPECT_TRUE(loaded.NeedsRewrite);
    EXPECT_EQ(loaded.Value.Grass.Appearance.BladeSegments, 2U);
    EXPECT_FLOAT_EQ(
        loaded.Value.Grass.Appearance.HeightScale, 1.0F);
    EXPECT_FLOAT_EQ(
        loaded.Value.Grass.WindRandomness, 1.0F);
}

TEST(Oot3dGraphicsSettingsPersistence,
     MigratesVersionFiveGrassControlsToCanonicalDensity) {
    auto document =
        Fast::Extension::SerializeGraphicsSettings(CompleteSettings());
    document["SchemaVersion"] = 5U;
    DowngradeGrassDocumentToVersionNine(document);
    auto& rule = document["Grass"]["Rules"][0];
    rule["InstancesPerSquareMeter"] = 20.0F;
    rule["DensityMultiplier"] = 2.0F;
    rule["ClusterSeed"] = 99U;
    rule["MaximumInstancesPerMesh"] = 1234U;

    const auto loaded =
        Fast::Extension::DeserializeGraphicsSettings(document);
    ASSERT_TRUE(loaded.Found);
    EXPECT_TRUE(loaded.NeedsRewrite);
    ASSERT_EQ(loaded.Value.Grass.Rules.size(), 2U);
    EXPECT_FLOAT_EQ(
        loaded.Value.Grass.Generation
            .InstancesPerSquareMeter,
        40.0F);

    const auto migrated =
        Fast::Extension::SerializeGraphicsSettings(loaded.Value);
    const auto& migratedRule =
        migrated["Grass"]["Sources"][0];
    EXPECT_FALSE(migratedRule.contains("DensityMultiplier"));
    EXPECT_FALSE(migratedRule.contains("ClusterSeed"));
    EXPECT_FALSE(migratedRule.contains(
        "MaximumInstancesPerMesh"));
}

TEST(Oot3dGraphicsSettingsPersistence,
     RepairsMalformedValuesAndBoundsRuleArrays) {
    auto document =
        Fast::Extension::SerializeGraphicsSettings(CompleteSettings());
    document["Output"]["Width"] = 1U;
    document["AA"]["Mode"] = "UnknownAA";
    const auto grassRule = document["Grass"]["Sources"][0];
    const auto reflectionRule =
        document["Effects"]["Reflections"]["Materials"][0];
    document["Grass"]["Sources"] = nlohmann::json::array();
    document["Effects"]["Reflections"]["Materials"] =
        nlohmann::json::array();
    for (uint32_t index = 0; index < 300U; ++index) {
        auto nextGrass = grassRule;
        nextGrass["RuleId"] = index + 1U;
        document["Grass"]["Sources"].push_back(nextGrass);
        auto nextReflection = reflectionRule;
        nextReflection["RuleId"] = index + 1U;
        document["Effects"]["Reflections"]["Materials"]
            .push_back(nextReflection);
    }

    const auto loaded =
        Fast::Extension::DeserializeGraphicsSettings(document);
    EXPECT_TRUE(loaded.NeedsRewrite);
    EXPECT_EQ(loaded.Value.OutputWidth, 320U);
    EXPECT_EQ(loaded.Value.AntiAliasing,
              Fast::Extension::AntiAliasingMode::Off);
    EXPECT_EQ(loaded.Value.Grass.Rules.size(), 256U);
    EXPECT_EQ(
        loaded.Value.Effects.ReflectionMaterials.size(), 256U);
    EXPECT_GE(loaded.Issues.size(), 3U);
}

TEST(Oot3dGraphicsSettingsPersistence,
     Schema15TakesTitleGrassRulesAndKeepsOtherGrassSettings) {
    auto document =
        Fast::Extension::SerializeGraphicsSettings(CompleteSettings());
    ASSERT_FALSE(document["Grass"]["Sources"].empty());
    auto titleRule = document["Grass"]["Sources"][0];
    titleRule["Target"]["Rgba8Hash"] = "a35e3fe6f83ca9c2";
    titleRule["InputWhite"] = 0.5;
    const nlohmann::json titleRules = {
        {"Sources", nlohmann::json::array({titleRule})},
        {"Masks", nlohmann::json::array()}};

    document["SchemaVersion"] = 14U;
    document["Grass"]["Budget"]["DrawDistance"] = 1234.0;
    document["Grass"]["Performance"]["FarDensity"] = 0.25;
    document["Grass"]["Generation"]["Seed"] = 77U;
    document["GrassSavedPreset"] = document["Grass"];
    document["GrassSavedPreset"]["Budget"]["DrawDistance"] = 4321.0;

    Fast::Extension::InstallTitleGrassRuleDefaults(titleRules);
    const auto migrated = Fast::Extension::DeserializeGraphicsSettings(document);
    auto current = document;
    current["SchemaVersion"] = Fast::Extension::kGraphicsSettingsSchemaVersion;
    const auto unchanged = Fast::Extension::DeserializeGraphicsSettings(current);
    Fast::Extension::ClearTitleGrassRuleDefaults();
    const auto withoutTitle = Fast::Extension::DeserializeGraphicsSettings(document);

    EXPECT_TRUE(migrated.NeedsRewrite);
    const auto saved = Fast::Extension::SerializeGraphicsSettings(migrated.Value);
    for (const char* block : {"Grass", "GrassSavedPreset"}) {
        ASSERT_EQ(saved[block]["Sources"].size(), 1U);
        EXPECT_EQ(saved[block]["Sources"][0]["Target"]["Rgba8Hash"], "a35e3fe6f83ca9c2");
        EXPECT_FLOAT_EQ(saved[block]["Sources"][0]["InputWhite"].get<float>(), 0.5F);
        EXPECT_TRUE(saved[block]["Masks"].empty());
        EXPECT_FLOAT_EQ(saved[block]["Performance"]["FarDensity"].get<float>(), 0.25F);
        EXPECT_EQ(saved[block]["Generation"]["Seed"], 77U);
    }
    EXPECT_FLOAT_EQ(saved["Grass"]["Budget"]["DrawDistance"].get<float>(), 1234.0F);
    EXPECT_FLOAT_EQ(saved["GrassSavedPreset"]["Budget"]["DrawDistance"].get<float>(), 4321.0F);

    // A current-schema configuration keeps its own rules, and without
    // installed title rules an old one keeps them too.
    const auto ownHash = document["Grass"]["Sources"][0]["Target"]["Rgba8Hash"];
    EXPECT_EQ(Fast::Extension::SerializeGraphicsSettings(unchanged.Value)["Grass"]["Sources"][0]["Target"]["Rgba8Hash"],
              ownHash);
    EXPECT_EQ(Fast::Extension::SerializeGraphicsSettings(withoutTitle.Value)["Grass"]["Sources"][0]["Target"]["Rgba8Hash"],
              ownHash);
}

TEST(Oot3dGraphicsSettingsPersistence,
     RemovesAnotherTitlesUneditedGrassRulesOnly) {
    // Another title's shipped rules, written into this title's configuration
    // by a build that installed them for every title.
    auto document =
        Fast::Extension::SerializeGraphicsSettings(CompleteSettings());
    ASSERT_FALSE(document["Grass"]["Sources"].empty());
    const nlohmann::json foreign = {
        {"Sources", document["Grass"]["Sources"]},
        {"Masks", document["Grass"].value("Masks", nlohmann::json::array())}};
    document["Grass"]["Budget"]["DrawDistance"] = 1234.0;
    document["GrassSavedPreset"] = document["Grass"];

    // Without foreign sets nothing changes.
    Fast::Extension::ClearTitleGrassRuleDefaults();
    const auto untouched = Fast::Extension::DeserializeGraphicsSettings(document);
    EXPECT_EQ(Fast::Extension::SerializeGraphicsSettings(untouched.Value)["Grass"]["Sources"],
              foreign["Sources"]);

    Fast::Extension::InstallTitleGrassRuleDefaults(
        Fast::Extension::TitleGrassRuleDefaults{std::nullopt, {foreign}});
    const auto cleaned = Fast::Extension::DeserializeGraphicsSettings(document);
    EXPECT_TRUE(cleaned.NeedsRewrite);
    const auto saved = Fast::Extension::SerializeGraphicsSettings(cleaned.Value);
    for (const char* block : {"Grass", "GrassSavedPreset"}) {
        EXPECT_TRUE(saved[block]["Sources"].empty()) << block;
        EXPECT_TRUE(saved[block]["Masks"].empty()) << block;
        EXPECT_FLOAT_EQ(saved[block]["Budget"]["DrawDistance"].get<float>(), 1234.0F) << block;
    }
    EXPECT_TRUE(std::any_of(cleaned.Issues.begin(), cleaned.Issues.end(), [](const auto& issue) {
        return issue.Corrected && issue.Message.find("another title's grass") != std::string::npos;
    }));
    // Loading the cleaned configuration again changes nothing more.
    const auto again = Fast::Extension::DeserializeGraphicsSettings(saved);
    EXPECT_EQ(Fast::Extension::SerializeGraphicsSettings(again.Value)["Grass"], saved["Grass"]);

    // Edited rules are the user's: one changed level keeps every rule.
    auto edited = document;
    edited["Grass"]["Sources"][0]["InputWhite"] = 0.375;
    const auto kept = Fast::Extension::DeserializeGraphicsSettings(edited);
    const auto keptSaved = Fast::Extension::SerializeGraphicsSettings(kept.Value);
    EXPECT_EQ(keptSaved["Grass"]["Sources"].size(), foreign["Sources"].size());
    EXPECT_FLOAT_EQ(keptSaved["Grass"]["Sources"][0]["InputWhite"].get<float>(), 0.375F);
    // The unedited saved preset is still removed on its own.
    EXPECT_TRUE(keptSaved["GrassSavedPreset"]["Sources"].empty());

    // No pre-schema-15 rules installed: an old configuration keeps its own
    // rules unless they are a foreign set.
    auto old = edited;
    old["SchemaVersion"] = 14U;
    const auto oldLoaded = Fast::Extension::DeserializeGraphicsSettings(old);
    EXPECT_EQ(Fast::Extension::SerializeGraphicsSettings(oldLoaded.Value)["Grass"]["Sources"].size(),
              foreign["Sources"].size());
    Fast::Extension::ClearTitleGrassRuleDefaults();
}

TEST(Oot3dGraphicsSettingsPersistence,
     TitlesSharingOneConfigurationFileEachKeepTheirGrassRules) {
    using namespace Fast::Extension;
    // One configuration file, as a developer launch without --config uses
    // for every title: OoT3D's shipped rules in both grass blocks.
    auto settings = CompleteSettings();
    settings.GrassSavedPreset = settings.Grass;
    const auto written = SerializeGraphicsSettings(settings);
    ASSERT_FALSE(written["Grass"]["Sources"].empty());
    const nlohmann::json oot3dRules = {
        {"Sources", written["Grass"]["Sources"]},
        {"Masks", written["Grass"].value("Masks", nlohmann::json::array())}};
    const auto path = std::filesystem::temp_directory_path() /
                      ("triaevum_shared_grass_rules_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
                       ".json");
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << nlohmann::json{{"Graphics", written}, {"Input", {{"Keep", true}}}}.dump(2);
    }
    // What the runtime does with the file: load the root, rewrite the
    // Graphics block when the load asks for it.
    const auto launch = [&](TitleGrassRuleDefaults title) {
        InstallTitleGrassRuleDefaults(std::move(title));
        nlohmann::json root;
        {
            std::ifstream file(path, std::ios::binary);
            file >> root;
        }
        auto loaded = LoadGraphicsSettingsConfig(root);
        if (loaded.NeedsRewrite) {
            root["Graphics"] = SerializeGraphicsSettings(loaded.Value);
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file << root.dump(2);
        }
        return loaded;
    };
    const TitleGrassRuleDefaults mm3d{std::nullopt, {oot3dRules}};
    const TitleGrassRuleDefaults oot3d{std::optional<nlohmann::json>(std::in_place, oot3dRules), {}};
    const auto rulesOf = [](const GraphicsSettingsLoadResult& loaded, const char* block) {
        const auto document = SerializeGraphicsSettings(loaded.Value);
        return nlohmann::json{{"Sources", document[block]["Sources"]}, {"Masks", document[block]["Masks"]}};
    };
    const nlohmann::json none = {{"Sources", nlohmann::json::array()}, {"Masks", nlohmann::json::array()}};

    // MM3D: OoT3D's rules are not its own.
    const auto mm3dFirst = launch(mm3d);
    EXPECT_TRUE(mm3dFirst.NeedsRewrite);
    for (const char* block : {"Grass", "GrassSavedPreset"}) EXPECT_EQ(rulesOf(mm3dFirst, block), none) << block;
    EXPECT_EQ(mm3dFirst.Value.RemovedTitleGrassRules.size(), 2U);

    // OoT3D on the same file gets them back, in both blocks, and the record
    // is settled; the rest of the file is kept.
    const auto oot3dFirst = launch(oot3d);
    EXPECT_TRUE(oot3dFirst.NeedsRewrite);
    for (const char* block : {"Grass", "GrassSavedPreset"}) EXPECT_EQ(rulesOf(oot3dFirst, block), oot3dRules) << block;
    EXPECT_TRUE(oot3dFirst.Value.RemovedTitleGrassRules.empty());
    EXPECT_TRUE(std::any_of(oot3dFirst.Issues.begin(), oot3dFirst.Issues.end(), [](const auto& issue) {
        return issue.Corrected && issue.Message.find("restored") != std::string::npos;
    }));
    {
        nlohmann::json root;
        std::ifstream file(path, std::ios::binary);
        file >> root;
        EXPECT_FALSE(root["Graphics"].contains("RemovedTitleGrassRules"));
        EXPECT_EQ(root["Input"]["Keep"], true);
    }
    // A second OoT3D launch has nothing to change.
    const auto oot3dSecond = launch(oot3d);
    EXPECT_FALSE(oot3dSecond.NeedsRewrite);
    EXPECT_EQ(rulesOf(oot3dSecond, "Grass"), oot3dRules);

    // Alternating titles: each load leaves the rules in its own form.
    const auto mm3dSecond = launch(mm3d);
    EXPECT_EQ(rulesOf(mm3dSecond, "Grass"), none);

    // Rules the user writes while MM3D runs are the user's: OoT3D keeps
    // them; the untouched saved preset still gets OoT3D's back.
    {
        nlohmann::json root;
        {
            std::ifstream file(path, std::ios::binary);
            file >> root;
        }
        auto userRule = oot3dRules["Sources"][0];
        userRule["InputWhite"] = 0.375;
        root["Graphics"]["Grass"]["Sources"] = nlohmann::json::array({userRule});
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << root.dump(2);
    }
    const auto oot3dAfterEdit = launch(oot3d);
    const auto edited = rulesOf(oot3dAfterEdit, "Grass");
    ASSERT_EQ(edited["Sources"].size(), 1U);
    EXPECT_FLOAT_EQ(edited["Sources"][0]["InputWhite"].get<float>(), 0.375F);
    EXPECT_EQ(rulesOf(oot3dAfterEdit, "GrassSavedPreset"), oot3dRules);
    EXPECT_TRUE(oot3dAfterEdit.Value.RemovedTitleGrassRules.empty());

    // Rules a user removed without another title are not brought back.
    auto cleared = written;
    cleared["Grass"]["Sources"] = nlohmann::json::array();
    cleared["Grass"]["Masks"] = nlohmann::json::array();
    InstallTitleGrassRuleDefaults(oot3d);
    const auto userCleared = DeserializeGraphicsSettings(cleared);
    EXPECT_EQ(rulesOf(userCleared, "Grass"), none);

    // The record is host-owned: applying a preset keeps it.
    auto withRecord = mm3dSecond.Value;
    EXPECT_EQ(GraphicsSettingsService::PresetForCurrent(GraphicsPreset::Enhanced, withRecord).RemovedTitleGrassRules,
              withRecord.RemovedTitleGrassRules);

    ClearTitleGrassRuleDefaults();
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST(Oot3dGraphicsSettingsPersistence,
     PreservesUnsupportedFutureSchema) {
    auto fallback = CompleteSettings();
    fallback.OutputWidth = 3440U;
    auto document =
        Fast::Extension::SerializeGraphicsSettings(CompleteSettings());
    document["SchemaVersion"] =
        Fast::Extension::kGraphicsSettingsSchemaVersion + 1U;
    document["Output"]["Width"] = 640U;

    const auto loaded =
        Fast::Extension::DeserializeGraphicsSettings(document,
                                                  fallback);
    EXPECT_TRUE(loaded.Found);
    EXPECT_TRUE(loaded.UnsupportedFutureVersion);
    EXPECT_FALSE(loaded.NeedsRewrite);
    EXPECT_EQ(loaded.Value.OutputWidth, 3440U);
}

TEST(Oot3dGraphicsSettingsPersistence,
     NormalizesOutOfRangeDocumentAndRequestsRewrite) {
    auto document =
        Fast::Extension::SerializeGraphicsSettings(CompleteSettings());
    document["Output"]["Width"] = 1U;
    document["Camera"]["FovMultiplier"] = 8.0F;
    const auto loaded =
        Fast::Extension::DeserializeGraphicsSettings(document);
    EXPECT_TRUE(loaded.NeedsRewrite);
    EXPECT_EQ(loaded.Value.OutputWidth, 320U);
    EXPECT_FLOAT_EQ(loaded.Value.FovMultiplier, 1.5F);
}

TEST(Oot3dGraphicsSettingsPersistence,
     MigratesLegacyWindowAndCvarsOnce) {
    const nlohmann::json config = {
        {"Window",
         {
             {"Fullscreen",
              {
                  {"Enabled", true},
                  {"Width", 1920U},
                  {"Height", 1080U},
              }},
             {"Width", 1280U},
             {"Height", 720U},
         }},
        {"CVars",
         {
             {"gSdlWindowedFullscreen", 1},
             {"gInternalResolution", 1.5F},
             {"gMSAAValue", 4},
             {"gVsyncEnabled", 0},
             {"gAdvancedResolution",
              {
                  {"Enabled", 1},
                  {"VerticalResolutionToggle", 1},
                  {"VerticalPixelCount", 1080},
                  {"AspectRatioX", 16.0F},
                  {"AspectRatioY", 9.0F},
              }},
         }},
    };
    const auto loaded =
        Fast::Extension::LoadGraphicsSettingsConfig(config);
    ASSERT_TRUE(loaded.Found);
    EXPECT_TRUE(loaded.MigratedLegacy);
    EXPECT_TRUE(loaded.NeedsRewrite);
    EXPECT_EQ(loaded.Value.Preset,
              Fast::Extension::GraphicsPreset::Custom);
    EXPECT_EQ(loaded.Value.Window,
              Fast::Extension::WindowMode::Borderless);
    EXPECT_EQ(loaded.Value.OutputWidth, 1920U);
    EXPECT_EQ(loaded.Value.OutputHeight, 1080U);
    EXPECT_FLOAT_EQ(loaded.Value.InternalResolutionScale, 1.5F);
    EXPECT_EQ(loaded.Value.AntiAliasing,
              Fast::Extension::AntiAliasingMode::Msaa);
    EXPECT_EQ(loaded.Value.MsaaSamples, 4U);
    EXPECT_FALSE(loaded.Value.VSync);
}

TEST(Oot3dGraphicsSettingsPersistence,
     LeavesConfigWithoutGraphicsOrLegacyUntouched) {
    const auto loaded =
        Fast::Extension::LoadGraphicsSettingsConfig(
            nlohmann::json{{"Audio", {{"Volume", 0.5F}}}});
    EXPECT_FALSE(loaded.Found);
    EXPECT_FALSE(loaded.NeedsRewrite);
    EXPECT_FALSE(loaded.MigratedLegacy);
}

TEST(Oot3dGraphicsSettingsPersistence,
     MigratesNamespacedHarnessCvars) {
    const nlohmann::json config = {
        {"Window",
         {
             {"Fullscreen", {{"Enabled", false}}},
             {"Width", 1600U},
             {"Height", 900U},
         }},
        {"CVars",
         {{"gSettings",
           {
               {"InternalResolution", 1.25F},
               {"MSAAValue", 2},
               {"VsyncEnabled", 1},
               {"SdlWindowedFullscreen", 0},
           }}}},
    };
    const auto loaded =
        Fast::Extension::LoadGraphicsSettingsConfig(config);
    ASSERT_TRUE(loaded.MigratedLegacy);
    EXPECT_EQ(loaded.Value.Window,
              Fast::Extension::WindowMode::Windowed);
    EXPECT_EQ(loaded.Value.OutputWidth, 1600U);
    EXPECT_EQ(loaded.Value.OutputHeight, 900U);
    EXPECT_FLOAT_EQ(loaded.Value.InternalResolutionScale, 1.25F);
    EXPECT_EQ(loaded.Value.AntiAliasing,
              Fast::Extension::AntiAliasingMode::Msaa);
    EXPECT_EQ(loaded.Value.MsaaSamples, 2U);
    EXPECT_TRUE(loaded.Value.VSync);
}

TEST(Oot3dGraphicsSettingsPersistence,
     SuppressesAutomationAndUnconfirmedPresentationWrites) {
    using Fast::Extension::ShouldPersistGraphicsSettings;
    EXPECT_TRUE(ShouldPersistGraphicsSettings(false, false));
    EXPECT_FALSE(ShouldPersistGraphicsSettings(true, false));
    EXPECT_FALSE(ShouldPersistGraphicsSettings(false, true));
    EXPECT_FALSE(ShouldPersistGraphicsSettings(true, true));
}

TEST(Oot3dGraphicsSettingsPersistence, GrassVisibilityMigratesAndDecouplesFalloffDistance) {
    using namespace Fast::Extension;
    auto document=SerializeGraphicsSettings(CompleteSettings());
    auto& performance=document["Grass"]["Performance"];
    document["Grass"]["Budget"]["DrawDistance"]=20000.0F;
    performance.erase("LodReferenceDistance");
    const auto legacy=DeserializeGraphicsSettings(document);
    EXPECT_FLOAT_EQ(legacy.Value.Grass.DrawDistance,20000.0F);
    EXPECT_FLOAT_EQ(legacy.Value.Grass.LodReferenceDistance,20000.0F);
    performance["LodReferenceDistance"]=5000.0F;
    const auto modern=DeserializeGraphicsSettings(document);
    EXPECT_FLOAT_EQ(modern.Value.Grass.DrawDistance,20000.0F);
    EXPECT_FLOAT_EQ(modern.Value.Grass.LodReferenceDistance,5000.0F);
    const auto roundtrip=DeserializeGraphicsSettings(SerializeGraphicsSettings(modern.Value));
    EXPECT_FLOAT_EQ(roundtrip.Value.Grass.LodReferenceDistance,5000.0F);
}

} // namespace

TEST(Oot3dGraphicsSettingsPersistence, GlobalIlluminationSpecular) {
    using namespace Fast::Extension;
    auto settings = GraphicsSettingsService::Preset(GraphicsPreset::Custom);
    settings.Effects.GlobalIllumination.Specular = true;
    settings.Effects.GlobalIllumination.SpecularIntensity = 7.0F;
    const auto clamped = GraphicsSettingsService::Validate(settings, {});
    EXPECT_FLOAT_EQ(clamped.Value.Effects.GlobalIllumination.SpecularIntensity, 2.0F);

    settings.Effects.GlobalIllumination.SpecularIntensity = 0.5F;
    auto document = SerializeGraphicsSettings(settings);
    const auto loaded = DeserializeGraphicsSettings(document);
    EXPECT_TRUE(loaded.Value.Effects.GlobalIllumination.Specular);
    EXPECT_FLOAT_EQ(loaded.Value.Effects.GlobalIllumination.SpecularIntensity, 0.5F);

    // Written before specular GI existed: defaults, no correction reported.
    document["Effects"]["GlobalIllumination"].erase("Specular");
    document["Effects"]["GlobalIllumination"].erase("SpecularIntensity");
    const auto legacy = DeserializeGraphicsSettings(document);
    EXPECT_FALSE(legacy.Value.Effects.GlobalIllumination.Specular);
    EXPECT_FLOAT_EQ(legacy.Value.Effects.GlobalIllumination.SpecularIntensity, 1.0F);
    EXPECT_TRUE(legacy.NeedsRewrite);
    EXPECT_FLOAT_EQ(legacy.Value.Effects.GlobalIllumination.KeyLight, 1.0F);

    // Source gains round-trip and clamp.
    settings.Effects.GlobalIllumination.SceneLight = 0.4F;
    settings.Effects.GlobalIllumination.KeyLight = 2.0F;
    settings.Effects.GlobalIllumination.DynamicLights = 0.5F;
    settings.Effects.GlobalIllumination.SkyLight = 0.6F;
    const auto composed = DeserializeGraphicsSettings(SerializeGraphicsSettings(settings));
    EXPECT_FLOAT_EQ(composed.Value.Effects.GlobalIllumination.SkyLight, 0.6F);
    EXPECT_FLOAT_EQ(composed.Value.Effects.GlobalIllumination.SceneLight, 0.4F);
    EXPECT_FLOAT_EQ(composed.Value.Effects.GlobalIllumination.KeyLight, 2.0F);
    EXPECT_FLOAT_EQ(composed.Value.Effects.GlobalIllumination.DynamicLights, 0.5F);
    settings.Effects.GlobalIllumination.SceneLight = -1.0F;
    settings.Effects.GlobalIllumination.DynamicLights = 9.0F;
    settings.Effects.GlobalIllumination.DiffuseIntensity = 20.0F;
    const auto limited = GraphicsSettingsService::Validate(settings, {});
    EXPECT_FLOAT_EQ(limited.Value.Effects.GlobalIllumination.SceneLight, 0.0F);
    EXPECT_FLOAT_EQ(limited.Value.Effects.GlobalIllumination.DynamicLights, 4.0F);
    EXPECT_FLOAT_EQ(limited.Value.Effects.GlobalIllumination.DiffuseIntensity, 8.0F);

    // Controls of the retired reference composition are dropped on rewrite.
    auto retired = SerializeGraphicsSettings(composed.Value);
    retired["Effects"]["GlobalIllumination"]["Additive"] = 0.5;
    const auto dropped = DeserializeGraphicsSettings(retired);
    EXPECT_TRUE(dropped.NeedsRewrite);
    EXPECT_FLOAT_EQ(dropped.Value.Effects.GlobalIllumination.KeyLight, 2.0F);
    const auto rewritten = SerializeGraphicsSettings(dropped.Value);
    EXPECT_FALSE(rewritten["Effects"]["GlobalIllumination"].contains("Additive"));
    EXPECT_TRUE(rewritten["Effects"]["GlobalIllumination"].contains("SkyLight"));
    for (const auto& issue : legacy.Issues)
        EXPECT_EQ(issue.Field.find("GlobalIllumination"), std::string::npos) << issue.Field;
}

TEST(Oot3dGraphicsSettingsPersistence, ScreenSpaceGlobalIllumination) {
    using namespace Fast::Extension;
    auto settings = GraphicsSettingsService::Preset(GraphicsPreset::Custom);
    settings.Effects.GlobalIllumination.Mode = GlobalIlluminationMode::ScreenSpace;
    settings.Effects.GlobalIllumination.ScreenSpace = {3, 32, 128, 240.0F, 45.0F, false, 2, true};
    auto document = SerializeGraphicsSettings(settings);
    EXPECT_EQ(document["Effects"]["GlobalIllumination"]["Mode"], "ScreenSpaceGI");
    EXPECT_EQ(document["Effects"]["GlobalIllumination"]["ScreenSpace"]["RadialCandidates"], 32);
    const auto loaded = DeserializeGraphicsSettings(document);
    // Exactly what the same preset reports without the screen-space mode.
    const auto baseline =
        DeserializeGraphicsSettings(SerializeGraphicsSettings(GraphicsSettingsService::Preset(GraphicsPreset::Custom)));
    EXPECT_EQ(loaded.NeedsRewrite, baseline.NeedsRewrite);
    EXPECT_EQ(loaded.Issues.size(), baseline.Issues.size());
    for (const auto& issue : loaded.Issues)
        EXPECT_EQ(issue.Field.find("GlobalIllumination"), std::string::npos) << issue.Field;
    EXPECT_EQ(loaded.Value.Effects.GlobalIllumination.Mode, GlobalIlluminationMode::ScreenSpace);
    EXPECT_EQ(loaded.Value.Effects.GlobalIllumination.ScreenSpace, settings.Effects.GlobalIllumination.ScreenSpace);

    // Written before the screen-space mode existed: defaults, rewritten on
    // the next save, no correction reported.
    document["Effects"]["GlobalIllumination"]["Mode"] = "BrixelizerGI";
    document["Effects"]["GlobalIllumination"].erase("ScreenSpace");
    const auto legacy = DeserializeGraphicsSettings(document);
    EXPECT_TRUE(legacy.NeedsRewrite);
    EXPECT_EQ(legacy.Value.Effects.GlobalIllumination.ScreenSpace, ScreenSpaceGiSettings{});
    for (const auto& issue : legacy.Issues)
        EXPECT_EQ(issue.Field.find("GlobalIllumination"), std::string::npos) << issue.Field;
    EXPECT_TRUE(SerializeGraphicsSettings(legacy.Value)["Effects"]["GlobalIllumination"].contains("ScreenSpace"));
    // A missing field keeps its default.
    document = SerializeGraphicsSettings(settings);
    document["Effects"]["GlobalIllumination"]["ScreenSpace"].erase("Thickness");
    const auto partial = DeserializeGraphicsSettings(document);
    EXPECT_TRUE(partial.NeedsRewrite);
    EXPECT_FLOAT_EQ(partial.Value.Effects.GlobalIllumination.ScreenSpace.Thickness, ScreenSpaceGiSettings{}.Thickness);
    EXPECT_EQ(partial.Value.Effects.GlobalIllumination.ScreenSpace.Sectors, 128U);
}

TEST(GrassMaskPersistence, RoundTripDefaultsValidationAndPresetOwnership) {
    using namespace Fast::Extension;
    GraphicsSettings settings;
    settings.Preset = GraphicsPreset::Custom;
    for (auto mode : { GrassMaskMode::Automatic, GrassMaskMode::Custom, GrassMaskMode::Off }) {
        GrassMaskRule rule;
        rule.Mode = mode;
        rule.Coverage.Target = { "", 0x1234 + uint64_t(mode), 64, 128 };
        rule.Coverage.Channel = GrassSampleChannel::Green;
        rule.Coverage.Invert = true;
        rule.Coverage.InputBlack = .2F;
        rule.Coverage.InputWhite = .8F;
        rule.Coverage.OutputBlack = .1F;
        rule.Coverage.OutputWhite = .7F;
        rule.Coverage.ResponseExponent = 2;
        rule.Coverage.ActiveRects = { { .1F, .2F, .7F, .8F } };
        rule.Strength = .4F;
        settings.Grass.Masks.push_back(rule);
    }
    auto document = SerializeGraphicsSettings(settings);
    const auto loaded = DeserializeGraphicsSettings(document);
    ASSERT_EQ(loaded.Value.Grass.Masks.size(), 3);
    EXPECT_EQ(SerializeGraphicsSettings(loaded.Value)["Grass"]["Masks"], document["Grass"]["Masks"]);
    EXPECT_EQ(loaded.Value.Grass.Masks[1].Coverage.ActiveRects, settings.Grass.Masks[1].Coverage.ActiveRects);
    auto kept = GraphicsSettingsService::KeepHostOwnedSettings(GraphicsSettings{}, settings);
    EXPECT_EQ(kept.Grass.Masks.size(), 3);
    document["Grass"].erase("Masks");
    EXPECT_TRUE(DeserializeGraphicsSettings(document).Value.Grass.Masks.empty());
    document["Grass"]["Masks"] = nlohmann::json::array(
        { { { "Target", { { "Rgba8Hash", "0000000000001234" }, { "Width", 64 }, { "Height", 128 } } },
            { "Mode", "unknown" },
            { "Channel", "unknown" },
            { "InputBlack", "mistyped" },
            { "Strength", 2.0 } } });
    auto defaults = DeserializeGraphicsSettings(document);
    ASSERT_EQ(defaults.Value.Grass.Masks.size(), 1);
    const auto& mask = defaults.Value.Grass.Masks[0];
    EXPECT_EQ(mask.Mode, GrassMaskMode::Automatic);
    EXPECT_EQ(mask.Coverage.Channel, GrassSampleChannel::Alpha);
    EXPECT_EQ(mask.Coverage.InputBlack, 0);
    EXPECT_EQ(mask.Strength, 1);
    EXPECT_TRUE(FindGrassMaskRule(defaults.Value.Grass.Masks, 0x1234, 64, 128));
    EXPECT_FALSE(FindGrassMaskRule(defaults.Value.Grass.Masks, 0x1234, 128, 64));
    document["Grass"]["Masks"].push_back(document["Grass"]["Masks"][0]);
    document["Grass"]["Masks"].push_back(17);
    EXPECT_EQ(DeserializeGraphicsSettings(document).Value.Grass.Masks.size(), 1);
    document["Grass"]["Masks"] = "mistyped";
    EXPECT_TRUE(DeserializeGraphicsSettings(document).Value.Grass.Masks.empty());
}

// The output provenance (UX plan WP-06A): round trip, inference for files
// from before the field (silently, no rewrite issue), and host ownership.
TEST(Oot3dGraphicsSettingsPersistence, RoundTripsAndInfersTheOutputProvenance) {
    using namespace Fast::Extension;
    auto settings = CompleteSettings();
    settings.OutputProvenance = Fast::WindowExtentProvenance::User;
    auto document = SerializeGraphicsSettings(settings);
    EXPECT_EQ(document["Output"]["Provenance"], "user");
    EXPECT_EQ(DeserializeGraphicsSettings(document).Value.OutputProvenance, Fast::WindowExtentProvenance::User);
    settings.OutputProvenance = Fast::WindowExtentProvenance::Default;
    document = SerializeGraphicsSettings(settings);
    EXPECT_EQ(document["Output"]["Provenance"], "default");
    EXPECT_EQ(DeserializeGraphicsSettings(document).Value.OutputProvenance, Fast::WindowExtentProvenance::Default);
    // Before the field: the product's windowed 1280x720 is the fresh
    // default, any other size or a fullscreen mode the user's choice.
    document["Output"].erase("Provenance");
    document["Window"]["Mode"] = "Windowed";
    document["Output"]["Width"] = 1280;
    document["Output"]["Height"] = 720;
    const auto fresh = DeserializeGraphicsSettings(document);
    EXPECT_EQ(fresh.Value.OutputProvenance, Fast::WindowExtentProvenance::Default);
    EXPECT_TRUE(std::none_of(fresh.Issues.begin(), fresh.Issues.end(),
                             [](const auto& issue) { return issue.Field == "Graphics.Output.Provenance"; }));
    document["Output"]["Width"] = 1920;
    document["Output"]["Height"] = 1080;
    EXPECT_EQ(DeserializeGraphicsSettings(document).Value.OutputProvenance, Fast::WindowExtentProvenance::User);
    document["Output"]["Width"] = 1280;
    document["Output"]["Height"] = 720;
    document["Window"]["Mode"] = "Borderless";
    EXPECT_EQ(DeserializeGraphicsSettings(document).Value.OutputProvenance, Fast::WindowExtentProvenance::User);
    // An unknown name falls back to the default and is corrected.
    document["Output"]["Provenance"] = "guessed";
    EXPECT_EQ(DeserializeGraphicsSettings(document).Value.OutputProvenance, Fast::WindowExtentProvenance::Default);
    // Presets keep it, like the size it describes.
    settings.OutputProvenance = Fast::WindowExtentProvenance::User;
    EXPECT_EQ(GraphicsSettingsService::PresetForCurrent(GraphicsPreset::Toon, settings).OutputProvenance,
              Fast::WindowExtentProvenance::User);
    // A size change is the user's choice; the mode alone or a reset is not.
    auto candidate = GraphicsSettings{};
    const GraphicsSettings current{};
    candidate.Window = WindowMode::Borderless;
    MarkUserOutputRequest(candidate, current);
    EXPECT_EQ(candidate.OutputProvenance, Fast::WindowExtentProvenance::Default);
    candidate.OutputWidth = 1600U;
    MarkUserOutputRequest(candidate, current);
    EXPECT_EQ(candidate.OutputProvenance, Fast::WindowExtentProvenance::User);
    ResetDisplaySettings(candidate);
    EXPECT_EQ(candidate.OutputProvenance, Fast::WindowExtentProvenance::Default);
    EXPECT_EQ(candidate.Window, WindowMode::Windowed);
    // The product default, the "Modified" baseline: Enhanced at the default window.
    const auto product = ProductDefaultGraphicsSettings();
    EXPECT_EQ(product.Preset, kProductDefaultGraphicsPreset);
#if defined(__ANDROID__)
    EXPECT_EQ(product.Preset, GraphicsPreset::EnhancedForMobile);
    EXPECT_EQ(product.Aspect, AspectRatioMode::Free);
#else
    EXPECT_EQ(product.Preset, GraphicsPreset::Enhanced);
    EXPECT_EQ(kGraphicsPresetHost, GraphicsPresetHost::Desktop);
#endif
    EXPECT_EQ(product.OutputWidth, 1280U);
    EXPECT_EQ(product.OutputProvenance, Fast::WindowExtentProvenance::Default);
#if defined(__APPLE__)
    EXPECT_FLOAT_EQ(product.InternalResolutionScale, 0.5F);
    EXPECT_FALSE(product.Effects.WaterReflections);
    EXPECT_EQ(product.Effects.AmbientOcclusion, AmbientOcclusionMode::Off);
    const auto enhanced = GraphicsSettingsService::Preset(GraphicsPreset::Enhanced);
    EXPECT_FLOAT_EQ(enhanced.InternalResolutionScale, 1.0F);
    EXPECT_TRUE(enhanced.Effects.WaterReflections);
    EXPECT_EQ(product.Effects.DirectionalShadows.Mode, enhanced.Effects.DirectionalShadows.Mode);
    EXPECT_EQ(product.Effects.PointShadows.Mode, enhanced.Effects.PointShadows.Mode);
    EXPECT_EQ(product.Grass.MaxInstancesPerRoom, enhanced.Grass.MaxInstancesPerRoom);
#endif
}

// Enhanced for Mobile is Enhanced with exactly four values changed: what else
// differs from Enhanced would change the phone's look or cost unnoticed.
TEST(Oot3dGraphicsSettingsPersistence, EnhancedForMobileIsEnhancedAtAPhonesCost) {
    using namespace Fast::Extension;
    const auto mobile = GraphicsSettingsService::Preset(GraphicsPreset::EnhancedForMobile);
    const auto enhanced = GraphicsSettingsService::Preset(GraphicsPreset::Enhanced);
    EXPECT_EQ(mobile.Preset, GraphicsPreset::EnhancedForMobile);
    EXPECT_EQ(mobile.FrameRate, FrameRateMode::Original30);
    EXPECT_FLOAT_EQ(mobile.InternalResolutionScale, 0.8F);
    EXPECT_EQ(mobile.Grass.MaxInstancesPerRoom, 400000U);
    EXPECT_FALSE(mobile.Effects.WaterReflections);
    // Aspect Free is Android's host-owned default, outside the preset.
    EXPECT_EQ(mobile.Aspect, enhanced.Aspect);

    auto document = SerializeGraphicsSettings(mobile);
    EXPECT_EQ(document["Preset"], "EnhancedForMobile");
    auto reference = SerializeGraphicsSettings(enhanced);
    EXPECT_EQ(reference["Preset"], "Enhanced");
    for (const auto* path : {"/Preset", "/FrameRate/Mode", "/RenderScale", "/Grass/Budget/MaxInstancesPerRoom",
                             "/Effects/Reflections/Water"}) {
        const nlohmann::json::json_pointer pointer{path};
        ASSERT_TRUE(document.contains(pointer)) << path;
        EXPECT_NE(document[pointer], reference[pointer]) << path;
        reference[pointer] = document[pointer];
    }
    EXPECT_EQ(document, reference);

    // Choosing it keeps the host-owned state, Android's Free aspect included.
    auto current = enhanced;
    current.Aspect = AspectRatioMode::Free;
    current.OutputWidth = 2340U;
    const auto chosen = GraphicsSettingsService::PresetForCurrent(GraphicsPreset::EnhancedForMobile, current);
    EXPECT_EQ(chosen.Preset, GraphicsPreset::EnhancedForMobile);
    EXPECT_EQ(chosen.Aspect, AspectRatioMode::Free);
    EXPECT_EQ(chosen.OutputWidth, 2340U);
    EXPECT_FLOAT_EQ(chosen.InternalResolutionScale, 0.8F);
    const auto back = GraphicsSettingsService::PresetForCurrent(GraphicsPreset::Enhanced, chosen);
    EXPECT_EQ(back.FrameRate, FrameRateMode::Interpolated2x);
    EXPECT_FLOAT_EQ(back.InternalResolutionScale, 1.0F);
    EXPECT_EQ(back.Aspect, AspectRatioMode::Free);

    // Custom keeps every value; the validator keeps the name.
    const auto custom = GraphicsSettingsService::PresetForCurrent(GraphicsPreset::Custom, chosen);
    EXPECT_EQ(custom.Preset, GraphicsPreset::Custom);
    EXPECT_EQ(custom.FrameRate, FrameRateMode::Original30);
    EXPECT_FLOAT_EQ(custom.InternalResolutionScale, 0.8F);
    EXPECT_EQ(GraphicsSettingsService::Validate(chosen, GraphicsCapabilities{}).Value.Preset,
              GraphicsPreset::EnhancedForMobile);

    // The display reset keeps the preset's scale, so the name stays true.
    auto reset = chosen;
    ResetDisplaySettings(reset);
    EXPECT_EQ(reset.Preset, GraphicsPreset::EnhancedForMobile);
    EXPECT_FLOAT_EQ(reset.InternalResolutionScale, 0.8F);
    reset = back;
    ResetDisplaySettings(reset);
    EXPECT_FLOAT_EQ(reset.InternalResolutionScale, 1.0F);
}

TEST(Oot3dGraphicsSettingsPersistence, LoadsConfigurationsWithAndWithoutEnhancedForMobile) {
    using namespace Fast::Extension;
    // A configuration that names it, on any host: same preset, same values, no
    // correction and no rewrite.
    auto saved = GraphicsSettingsService::Preset(GraphicsPreset::EnhancedForMobile);
    saved.Aspect = AspectRatioMode::Free;
    const auto document = SerializeGraphicsSettings(saved);
    const auto loaded = DeserializeGraphicsSettings(document);
    EXPECT_EQ(loaded.Value.Preset, GraphicsPreset::EnhancedForMobile);
    EXPECT_TRUE(loaded.Issues.empty());
    EXPECT_FALSE(loaded.NeedsRewrite);
    EXPECT_EQ(SerializeGraphicsSettings(loaded.Value), document);

    // A phone's configuration from before the preset: its Enhanced with the
    // reduced values stays exactly that. Existing choices are never relabelled.
    auto older = document;
    older["Preset"] = "Enhanced";
    const auto kept = DeserializeGraphicsSettings(older);
    EXPECT_EQ(kept.Value.Preset, GraphicsPreset::Enhanced);
    EXPECT_FALSE(kept.NeedsRewrite);
    EXPECT_EQ(SerializeGraphicsSettings(kept.Value), older);

    // A document from before anisotropic filtering takes the preset's 4x.
    auto withoutTextures = document;
    withoutTextures.erase("Textures");
    EXPECT_EQ(DeserializeGraphicsSettings(withoutTextures).Value.Anisotropy, 4U);
    // An unknown name is still refused.
    auto unknown = document;
    unknown["Preset"] = "Enhanced for Mobile";
    EXPECT_NE(DeserializeGraphicsSettings(unknown).Value.Preset, GraphicsPreset::EnhancedForMobile);
}

TEST(Oot3dGraphicsSettingsPersistence, OnlyAMobileHostOffersEnhancedForMobile) {
    using namespace Fast::Extension;
    const auto list = [](GraphicsPresetHost host) {
        std::vector<std::string> names;
        for (const auto preset : BuiltInGraphicsPresets(host)) names.emplace_back(GraphicsPresetDisplayName(preset));
        return names;
    };
    EXPECT_EQ(list(GraphicsPresetHost::Desktop), (std::vector<std::string>{"Authentic", "Enhanced", "Toon"}));
    EXPECT_EQ(list(GraphicsPresetHost::Mobile),
              (std::vector<std::string>{"Authentic", "Enhanced", "Enhanced for Mobile", "Toon"}));
    EXPECT_STREQ(GraphicsPresetDisplayName(GraphicsPreset::Custom), "Custom");

    // The numbers reports print stay, and desktop menus keep their rows.
    EXPECT_EQ(static_cast<int>(GraphicsPreset::Custom), 3);
    for (const auto preset : {GraphicsPreset::Authentic, GraphicsPreset::Enhanced, GraphicsPreset::Toon,
                              GraphicsPreset::Custom})
        EXPECT_EQ(GraphicsPresetChoiceIndex(GraphicsPresetHost::Desktop, preset), static_cast<int>(preset));
    // A configuration moved to a desktop shows the Custom row of its menu.
    EXPECT_EQ(GraphicsPresetChoiceIndex(GraphicsPresetHost::Desktop, GraphicsPreset::EnhancedForMobile), 3);
    EXPECT_EQ(GraphicsPresetChoiceIndex(GraphicsPresetHost::Mobile, GraphicsPreset::EnhancedForMobile), 2);
    EXPECT_EQ(GraphicsPresetChoiceIndex(GraphicsPresetHost::Mobile, GraphicsPreset::Toon), 3);
    EXPECT_EQ(GraphicsPresetChoiceIndex(GraphicsPresetHost::Mobile, GraphicsPreset::Custom), 4);
}

TEST(Oot3dGraphicsSettingsPersistence, RoundTripsTheAspectRatioAsHostOwnedOutputState) {
    using namespace Fast::Extension;
    auto settings = CompleteSettings();
    settings.Aspect = AspectRatioMode::Native3ds;
    auto document = SerializeGraphicsSettings(settings);
    EXPECT_EQ(document["Output"]["Aspect"], "Native3ds");
    EXPECT_EQ(DeserializeGraphicsSettings(document).Value.Aspect, AspectRatioMode::Native3ds);
    for (const auto [mode, name] :
         {std::pair{AspectRatioMode::Wide16x9, "Wide16x9"}, std::pair{AspectRatioMode::Ultrawide21x9, "Ultrawide21x9"},
          std::pair{AspectRatioMode::Free, "Free"}}) {
        settings.Aspect = mode;
        const auto serialized = SerializeGraphicsSettings(settings);
        EXPECT_EQ(serialized["Output"]["Aspect"], name);
        EXPECT_EQ(DeserializeGraphicsSettings(serialized).Value.Aspect, mode);
    }
    // An older file without the key, or an unknown name, is 16:9 (the
    // default) and asks for a rewrite.
    document["Output"].erase("Aspect");
    auto older = DeserializeGraphicsSettings(document);
    EXPECT_EQ(older.Value.Aspect, AspectRatioMode::Wide16x9);
    EXPECT_TRUE(older.NeedsRewrite);
    document["Output"]["Aspect"] = "Cinemascope";
    EXPECT_EQ(DeserializeGraphicsSettings(document).Value.Aspect, AspectRatioMode::Wide16x9);
    // A preset keeps the current aspect, like the window and the output size.
    settings.Aspect = AspectRatioMode::Ultrawide21x9;
    const auto enhanced = GraphicsSettingsService::PresetForCurrent(GraphicsPreset::Enhanced, settings);
    EXPECT_EQ(enhanced.Aspect, AspectRatioMode::Ultrawide21x9);
    EXPECT_EQ(GraphicsSettingsService::KeepHostOwnedSettings(GraphicsSettings{}, settings).Aspect,
              AspectRatioMode::Ultrawide21x9);
    // The validator corrects an out-of-range value to the default.
    settings.Aspect = static_cast<AspectRatioMode>(9);
    EXPECT_EQ(GraphicsSettingsService::Validate(settings, GraphicsCapabilities{}).Value.Aspect,
              AspectRatioMode::Wide16x9);
}
