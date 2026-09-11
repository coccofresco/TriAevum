#include "fast/oot3d/graphics_settings_persistence.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {

Fast::Oot3d::GraphicsSettings CompleteSettings() {
    using namespace Fast::Oot3d;
    auto settings =
        GraphicsSettingsService::Preset(GraphicsPreset::Custom);
    settings.Window = WindowMode::ExclusiveFullscreen;
    settings.DisplayIndex = 2U;
    settings.OutputWidth = 2560U;
    settings.OutputHeight = 1440U;
    settings.RefreshRate = 144U;
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
    grass.Rules.push_back(grassRule);
    GrassPlacementRule secondGrassRule = grassRule;
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
        DirectionalShadowMode::SingleCascade;
    effects.DirectionalShadows.Resolution = 2048U;
    effects.DirectionalShadows.MaximumDistance = 3200.0F;
    effects.DirectionalShadows.DepthPadding = 480.0F;
    effects.DirectionalShadows.DepthBiasConstant = 1.5F;
    effects.DirectionalShadows.DepthBiasSlope = 2.25F;
    effects.DirectionalShadows.Strength = 0.72F;
    effects.DirectionalShadows.PcfRadius = 2U;
    effects.DirectionalShadows.Stabilize = false;
    effects.AmbientOcclusion = AmbientOcclusionMode::Cacao;
    effects.AoQuality = 1U;
    effects.AoRadius = 4.25F;
    effects.AoStrength = 1.35F;
    effects.AoShadowPower = 2.1F;
    effects.AoShadowClamp = 0.91F;
    effects.AoHorizonAngleThreshold = 0.12F;
    effects.AoFadeOutFrom = 75.0F;
    effects.AoFadeOutTo = 1400.0F;
    effects.AoBlurPassCount = 5U;
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

TEST(Oot3dGraphicsSettingsPersistence, MigratesCoupledSegmentLodWithoutMovingItsThresholds) {
    auto settings = CompleteSettings();
    settings.Grass.DrawDistance = 5000.0F;
    settings.Grass.LodStartFraction = 0.3F;
    settings.Grass.LodEndFraction = 0.6F;
    auto document = Fast::Oot3d::SerializeGraphicsSettings(settings);
    auto& performance = document["Grass"]["Performance"];
    performance.erase("SegmentLodStartDistance");
    performance.erase("SegmentLodEndDistance");
    const auto loaded = Fast::Oot3d::DeserializeGraphicsSettings(document);
    EXPECT_FLOAT_EQ(loaded.Value.Grass.SegmentLodStartDistance, 1500.0F);
    EXPECT_FLOAT_EQ(loaded.Value.Grass.SegmentLodEndDistance, 3000.0F);
}

TEST(Oot3dGraphicsSettingsPersistence, RoundTripsCompleteSettings) {
    const auto original = CompleteSettings();
    const auto document =
        Fast::Oot3d::SerializeGraphicsSettings(original);
    const auto loaded =
        Fast::Oot3d::DeserializeGraphicsSettings(document);
    ASSERT_TRUE(loaded.Found);
    EXPECT_FALSE(loaded.NeedsRewrite);
    EXPECT_FALSE(loaded.UnsupportedFutureVersion);
    EXPECT_TRUE(loaded.Issues.empty());
    EXPECT_EQ(Fast::Oot3d::SerializeGraphicsSettings(loaded.Value),
              document);
    ASSERT_EQ(loaded.Value.Grass.Rules.size(), 2U);
    EXPECT_EQ(loaded.Value.Grass.Rules[0].Target.Rgba8Hash,
              0xFEDCBA9876543210ULL);
    EXPECT_EQ(
        loaded.Value.Grass.Rules[1].Channel,
        Fast::Oot3d::GrassSampleChannel::Alpha);
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
}

TEST(Oot3dGraphicsSettingsPersistence, AddsSoftGrassLodDefaultsWithoutMovingExistingDistances) {
    using namespace Fast::Oot3d;
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

TEST(Oot3dGraphicsSettingsPersistence, MigratesLegacyFixed60NameToInterpolated2x) {
    auto document = Fast::Oot3d::SerializeGraphicsSettings(CompleteSettings());
    document["FrameRate"]["Mode"] = "Fixed60";
    const auto loaded = Fast::Oot3d::DeserializeGraphicsSettings(document);
    ASSERT_TRUE(loaded.Found);
    EXPECT_EQ(loaded.Value.FrameRate, Fast::Oot3d::FrameRateMode::Interpolated2x);
    EXPECT_EQ(Fast::Oot3d::SerializeGraphicsSettings(loaded.Value)["FrameRate"]["Mode"], "Interpolated2x");
}

TEST(Oot3dGraphicsSettingsPersistence,
     MigratesVersionSixLiveGrassAsSavedPreset) {
    auto document =
        Fast::Oot3d::SerializeGraphicsSettings(CompleteSettings());
    document["SchemaVersion"] = 6U;
    DowngradeGrassDocumentToVersionNine(document);
    document.erase("GrassSavedPreset");
    document["Grass"]["Budget"]["DrawDistance"] = 2468.0F;
    document["Grass"]["Wind"]["Strength"] = 0.91F;

    const auto loaded =
        Fast::Oot3d::DeserializeGraphicsSettings(document);
    ASSERT_TRUE(loaded.Found);
    EXPECT_TRUE(loaded.NeedsRewrite);
    EXPECT_FLOAT_EQ(loaded.Value.Grass.DrawDistance, 2468.0F);
    EXPECT_FLOAT_EQ(
        loaded.Value.GrassSavedPreset.DrawDistance, 2468.0F);
    EXPECT_FLOAT_EQ(
        loaded.Value.GrassSavedPreset.WindStrength, 0.91F);
    const auto migrated =
        Fast::Oot3d::SerializeGraphicsSettings(loaded.Value);
    EXPECT_EQ(migrated["GrassSavedPreset"], migrated["Grass"]);
}

TEST(Oot3dGraphicsSettingsPersistence,
     MigratesVersionSevenWithTexturePacksDisabled) {
    auto document =
        Fast::Oot3d::SerializeGraphicsSettings(CompleteSettings());
    document["SchemaVersion"] = 7U;
    DowngradeGrassDocumentToVersionNine(document);
    document.erase("TexturePacks");

    const auto loaded =
        Fast::Oot3d::DeserializeGraphicsSettings(document);
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
        Fast::Oot3d::SerializeGraphicsSettings(CompleteSettings());
    document["SchemaVersion"] = 8U;
    DowngradeGrassDocumentToVersionNine(document);
    document["TexturePacks"]["Azahar"].erase("LoadDirectory");
    document["TexturePacks"]["Azahar"].erase("DumpDirectory");

    const auto loaded =
        Fast::Oot3d::DeserializeGraphicsSettings(document);
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
        Fast::Oot3d::SerializeGraphicsSettings(CompleteSettings());
    document["SchemaVersion"] = 9U;
    DowngradeGrassDocumentToVersionNine(document);
    document["Grass"]["Rules"][0]
            ["InstancesPerSquareMeter"] = 777.0F;
    document["Grass"]["Rules"][1]
            ["InstancesPerSquareMeter"] = 12.0F;

    const auto loaded =
        Fast::Oot3d::DeserializeGraphicsSettings(document);
    ASSERT_TRUE(loaded.Found);
    EXPECT_TRUE(loaded.NeedsRewrite);
    ASSERT_EQ(loaded.Value.Grass.Rules.size(), 2U);
    EXPECT_FLOAT_EQ(
        loaded.Value.Grass.Generation
            .InstancesPerSquareMeter,
        777.0F);
    EXPECT_EQ(
        loaded.Value.Grass.Rules[0].Channel,
        Fast::Oot3d::GrassSampleChannel::Green);
    EXPECT_EQ(
        loaded.Value.Grass.Rules[1].Channel,
        Fast::Oot3d::GrassSampleChannel::Alpha);

    const auto migrated =
        Fast::Oot3d::SerializeGraphicsSettings(loaded.Value);
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
        Fast::Oot3d::SerializeGraphicsSettings(CompleteSettings());
    document["SchemaVersion"] = 10U;
    DowngradeGrassDocumentToVersionTen(document);

    const auto loaded =
        Fast::Oot3d::DeserializeGraphicsSettings(document);
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
        Fast::Oot3d::SerializeGraphicsSettings(loaded.Value);
    EXPECT_TRUE(
        migrated["Grass"]["Sources"][0].contains("InputBlack"));
    EXPECT_FALSE(
        migrated["Grass"]["Sources"][0].contains("Minimum"));
}

TEST(Oot3dGraphicsSettingsPersistence,
     MigratesVersionOneToEqualLightBandProfile) {
    auto document =
        Fast::Oot3d::SerializeGraphicsSettings(CompleteSettings());
    document["SchemaVersion"] = 1U;
    DowngradeGrassDocumentToVersionNine(document);
    auto& toon = document["Effects"]["Toon"];
    toon.erase("CustomLightBands");
    toon.erase("LightBandLevels");
    toon.erase("LightBandThresholds");

    const auto loaded =
        Fast::Oot3d::DeserializeGraphicsSettings(document);
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

TEST(Oot3dGraphicsSettingsPersistence,
     MigratesVersionTwoWithDirectionalShadowsDisabled) {
    auto document =
        Fast::Oot3d::SerializeGraphicsSettings(CompleteSettings());
    document["SchemaVersion"] = 2U;
    DowngradeGrassDocumentToVersionNine(document);
    document["Effects"].erase("DirectionalShadows");

    const auto loaded =
        Fast::Oot3d::DeserializeGraphicsSettings(document);
    ASSERT_TRUE(loaded.Found);
    EXPECT_TRUE(loaded.NeedsRewrite);
    EXPECT_EQ(loaded.Value.Effects.DirectionalShadows.Mode,
              Fast::Oot3d::DirectionalShadowMode::Off);
    EXPECT_EQ(loaded.Value.Effects.DirectionalShadows.Resolution,
              1024U);
}

TEST(Oot3dGraphicsSettingsPersistence,
     MigratesVersionThreeGrassAppearanceDefaults) {
    auto document =
        Fast::Oot3d::SerializeGraphicsSettings(CompleteSettings());
    document["SchemaVersion"] = 3U;
    DowngradeGrassDocumentToVersionNine(document);
    document["Grass"].erase("Appearance");
    document["Grass"]["Wind"].erase("Randomness");

    const auto loaded =
        Fast::Oot3d::DeserializeGraphicsSettings(document);
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
        Fast::Oot3d::SerializeGraphicsSettings(CompleteSettings());
    document["SchemaVersion"] = 5U;
    DowngradeGrassDocumentToVersionNine(document);
    auto& rule = document["Grass"]["Rules"][0];
    rule["InstancesPerSquareMeter"] = 20.0F;
    rule["DensityMultiplier"] = 2.0F;
    rule["ClusterSeed"] = 99U;
    rule["MaximumInstancesPerMesh"] = 1234U;

    const auto loaded =
        Fast::Oot3d::DeserializeGraphicsSettings(document);
    ASSERT_TRUE(loaded.Found);
    EXPECT_TRUE(loaded.NeedsRewrite);
    ASSERT_EQ(loaded.Value.Grass.Rules.size(), 2U);
    EXPECT_FLOAT_EQ(
        loaded.Value.Grass.Generation
            .InstancesPerSquareMeter,
        40.0F);

    const auto migrated =
        Fast::Oot3d::SerializeGraphicsSettings(loaded.Value);
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
        Fast::Oot3d::SerializeGraphicsSettings(CompleteSettings());
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
        Fast::Oot3d::DeserializeGraphicsSettings(document);
    EXPECT_TRUE(loaded.NeedsRewrite);
    EXPECT_EQ(loaded.Value.OutputWidth, 320U);
    EXPECT_EQ(loaded.Value.AntiAliasing,
              Fast::Oot3d::AntiAliasingMode::Off);
    EXPECT_EQ(loaded.Value.Grass.Rules.size(), 256U);
    EXPECT_EQ(
        loaded.Value.Effects.ReflectionMaterials.size(), 256U);
    EXPECT_GE(loaded.Issues.size(), 3U);
}

TEST(Oot3dGraphicsSettingsPersistence,
     PreservesUnsupportedFutureSchema) {
    auto fallback = CompleteSettings();
    fallback.OutputWidth = 3440U;
    auto document =
        Fast::Oot3d::SerializeGraphicsSettings(CompleteSettings());
    document["SchemaVersion"] =
        Fast::Oot3d::kGraphicsSettingsSchemaVersion + 1U;
    document["Output"]["Width"] = 640U;

    const auto loaded =
        Fast::Oot3d::DeserializeGraphicsSettings(document,
                                                  fallback);
    EXPECT_TRUE(loaded.Found);
    EXPECT_TRUE(loaded.UnsupportedFutureVersion);
    EXPECT_FALSE(loaded.NeedsRewrite);
    EXPECT_EQ(loaded.Value.OutputWidth, 3440U);
}

TEST(Oot3dGraphicsSettingsPersistence,
     NormalizesOutOfRangeDocumentAndRequestsRewrite) {
    auto document =
        Fast::Oot3d::SerializeGraphicsSettings(CompleteSettings());
    document["Output"]["Width"] = 1U;
    document["Camera"]["FovMultiplier"] = 8.0F;
    const auto loaded =
        Fast::Oot3d::DeserializeGraphicsSettings(document);
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
        Fast::Oot3d::LoadGraphicsSettingsConfig(config);
    ASSERT_TRUE(loaded.Found);
    EXPECT_TRUE(loaded.MigratedLegacy);
    EXPECT_TRUE(loaded.NeedsRewrite);
    EXPECT_EQ(loaded.Value.Preset,
              Fast::Oot3d::GraphicsPreset::Custom);
    EXPECT_EQ(loaded.Value.Window,
              Fast::Oot3d::WindowMode::Borderless);
    EXPECT_EQ(loaded.Value.OutputWidth, 1920U);
    EXPECT_EQ(loaded.Value.OutputHeight, 1080U);
    EXPECT_FLOAT_EQ(loaded.Value.InternalResolutionScale, 1.5F);
    EXPECT_EQ(loaded.Value.AntiAliasing,
              Fast::Oot3d::AntiAliasingMode::Msaa);
    EXPECT_EQ(loaded.Value.MsaaSamples, 4U);
    EXPECT_FALSE(loaded.Value.VSync);
}

TEST(Oot3dGraphicsSettingsPersistence,
     LeavesConfigWithoutGraphicsOrLegacyUntouched) {
    const auto loaded =
        Fast::Oot3d::LoadGraphicsSettingsConfig(
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
        Fast::Oot3d::LoadGraphicsSettingsConfig(config);
    ASSERT_TRUE(loaded.MigratedLegacy);
    EXPECT_EQ(loaded.Value.Window,
              Fast::Oot3d::WindowMode::Windowed);
    EXPECT_EQ(loaded.Value.OutputWidth, 1600U);
    EXPECT_EQ(loaded.Value.OutputHeight, 900U);
    EXPECT_FLOAT_EQ(loaded.Value.InternalResolutionScale, 1.25F);
    EXPECT_EQ(loaded.Value.AntiAliasing,
              Fast::Oot3d::AntiAliasingMode::Msaa);
    EXPECT_EQ(loaded.Value.MsaaSamples, 2U);
    EXPECT_TRUE(loaded.Value.VSync);
}

TEST(Oot3dGraphicsSettingsPersistence,
     SuppressesAutomationAndUnconfirmedPresentationWrites) {
    using Fast::Oot3d::ShouldPersistGraphicsSettings;
    EXPECT_TRUE(ShouldPersistGraphicsSettings(false, false));
    EXPECT_FALSE(ShouldPersistGraphicsSettings(true, false));
    EXPECT_FALSE(ShouldPersistGraphicsSettings(false, true));
    EXPECT_FALSE(ShouldPersistGraphicsSettings(true, true));
}

TEST(Oot3dGraphicsSettingsPersistence, GrassVisibilityMigratesAndDecouplesFalloffDistance) {
    using namespace Fast::Oot3d;
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
