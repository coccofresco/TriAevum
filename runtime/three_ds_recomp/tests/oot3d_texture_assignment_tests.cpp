#include "fast/oot3d/texture_assignment_model.h"
#include "fast/oot3d/texture_catalog_selection.h"

#include <gtest/gtest.h>

#include <array>
#include <vector>

TEST(Oot3dTextureAssignments,
     SharesOneSelectionAcrossGrassAndReflection) {
    using namespace Fast::Oot3d;
    auto settings =
        GraphicsSettingsService::Preset(GraphicsPreset::Custom);
    const TextureCatalogEntry texture{
        0xBE15AFF93DFDCD88ULL, 0x12340000U,
        128U, 64U, 4U, 27U};

    EXPECT_TRUE(AssignTextureToGrass(settings.Grass, texture));
    EXPECT_TRUE(AssignTextureReflectionProfile(
        settings.Effects, texture,
        ReflectionMaterialProfile::Water));
    ASSERT_EQ(settings.Grass.Rules.size(), 1U);
    ASSERT_EQ(
        settings.Effects.ReflectionMaterials.size(), 1U);
    const auto summary = DescribeTextureAssignments(
        settings, texture);
    EXPECT_TRUE(summary.Grass);
    ASSERT_TRUE(summary.Reflection.has_value());
    EXPECT_EQ(*summary.Reflection,
              ReflectionMaterialProfile::Water);
    EXPECT_EQ(settings.Grass.Rules[0].Target.Width, 128U);
    EXPECT_EQ(
        settings.Effects.ReflectionMaterials[0].Target.Height,
        64U);
    EXPECT_FLOAT_EQ(
        settings.Effects.ReflectionMaterials[0].Reflectivity,
        0.78F);
}

TEST(Oot3dTextureAssignments,
     ReassignsProfileWithoutDuplicatingOrRemovingGrass) {
    using namespace Fast::Oot3d;
    GraphicsSettings settings;
    settings.Preset = GraphicsPreset::Custom;
    const TextureCatalogEntry texture{
        0x1122334455667788ULL, 0U, 32U, 32U, 0U, 1U};
    ASSERT_TRUE(AssignTextureToGrass(settings.Grass, texture));
    ASSERT_TRUE(AssignTextureReflectionProfile(
        settings.Effects, texture,
        ReflectionMaterialProfile::Water));
    ASSERT_TRUE(AssignTextureReflectionProfile(
        settings.Effects, texture,
        ReflectionMaterialProfile::Metal));
    EXPECT_EQ(
        settings.Effects.ReflectionMaterials.size(), 1U);
    EXPECT_EQ(
        settings.Effects.ReflectionMaterials[0].Profile,
        ReflectionMaterialProfile::Metal);
    EXPECT_FLOAT_EQ(
        settings.Effects.ReflectionMaterials[0].Roughness,
        0.22F);

    EXPECT_TRUE(RemoveTextureReflectionProfile(
        settings.Effects, texture));
    EXPECT_TRUE(DescribeTextureAssignments(
                    settings, texture)
                    .Grass);
    EXPECT_TRUE(RemoveTextureFromGrass(
        settings.Grass, texture));
    EXPECT_FALSE(DescribeTextureAssignments(
                     settings, texture)
                     .Assigned());
}

TEST(Oot3dTextureAssignments,
     RefreshesExistingGrassMetadataWithoutDuplicatingRule) {
    using namespace Fast::Oot3d;
    InteractiveGrassSettings grass;
    GrassPlacementRule rule;
    rule.RuleId = 7U;
    rule.Target.Rgba8Hash = 0x55U;
    grass.Rules.push_back(rule);
    const TextureCatalogEntry texture{
        0x55U, 0U, 128U, 32U, 0U, 3U};

    EXPECT_TRUE(AssignTextureToGrass(grass, texture));
    ASSERT_EQ(grass.Rules.size(), 1U);
    EXPECT_EQ(grass.Rules[0].RuleId, 7U);
    EXPECT_EQ(grass.Rules[0].Target.Width, 128U);
    EXPECT_EQ(grass.Rules[0].Target.Height, 32U);
    EXPECT_EQ(grass.Rules[0].Target.MapperSlotMask, 0x07U);
    EXPECT_FALSE(AssignTextureToGrass(grass, texture));
}

TEST(Oot3dTextureAssignments,
     AllocatesStableRuleIdsIntoAvailableGaps) {
    using namespace Fast::Oot3d;
    InteractiveGrassSettings grass;
    GrassPlacementRule first;
    first.RuleId = 1U;
    first.Target.Rgba8Hash = 1U;
    GrassPlacementRule third;
    third.RuleId = 3U;
    third.Target.Rgba8Hash = 3U;
    grass.Rules = {first, third};
    const TextureCatalogEntry texture{
        2U, 0U, 16U, 16U, 0U, 1U};
    ASSERT_TRUE(AssignTextureToGrass(grass, texture));
    ASSERT_EQ(grass.Rules.size(), 3U);
    EXPECT_EQ(grass.Rules.back().RuleId, 2U);
}

TEST(Oot3dTextureAssignments,
     DistinguishesEqualHashesWithDifferentDimensions) {
    using namespace Fast::Oot3d;
    GraphicsSettings settings;
    const TextureCatalogEntry small{
        0xABCDEFU, 0U, 32U, 32U, 4U, 1U};
    const TextureCatalogEntry large{
        0xABCDEFU, 0U, 128U, 64U, 4U, 1U};
    settings.Grass.Generation.InstancesPerSquareMeter =
        96.0F;

    ASSERT_TRUE(AssignTextureToGrass(settings.Grass, small));
    ASSERT_TRUE(AssignTextureToGrass(settings.Grass, large));
    auto* smallGrass =
        FindGrassTextureRule(settings.Grass, small);
    auto* largeGrass =
        FindGrassTextureRule(settings.Grass, large);
    ASSERT_NE(smallGrass, nullptr);
    ASSERT_NE(largeGrass, nullptr);
    smallGrass->Channel = GrassSampleChannel::Red;
    smallGrass->InputBlack = 0.2F;
    largeGrass->Channel = GrassSampleChannel::Blue;
    largeGrass->Invert = true;
    ASSERT_TRUE(AssignTextureReflectionProfile(
        settings.Effects, small,
        ReflectionMaterialProfile::Water));
    ASSERT_TRUE(AssignTextureReflectionProfile(
        settings.Effects, large,
        ReflectionMaterialProfile::Metal));
    ASSERT_EQ(settings.Grass.Rules.size(), 2U);
    EXPECT_EQ(
        FindGrassTextureRule(settings.Grass, small)->Channel,
        GrassSampleChannel::Red);
    EXPECT_EQ(
        FindGrassTextureRule(settings.Grass, large)->Channel,
        GrassSampleChannel::Blue);
    EXPECT_TRUE(
        FindGrassTextureRule(settings.Grass, large)->Invert);
    EXPECT_FLOAT_EQ(
        settings.Grass.Generation.InstancesPerSquareMeter,
        96.0F);
    ASSERT_EQ(
        settings.Effects.ReflectionMaterials.size(), 2U);
    EXPECT_EQ(
        *DescribeTextureAssignments(settings, small).Reflection,
        ReflectionMaterialProfile::Water);
    EXPECT_EQ(
        *DescribeTextureAssignments(settings, large).Reflection,
        ReflectionMaterialProfile::Metal);

    EXPECT_TRUE(RemoveTextureReflectionProfile(
        settings.Effects, large));
    EXPECT_TRUE(
        DescribeTextureAssignments(settings, small)
            .Reflection.has_value());
    EXPECT_FALSE(
        DescribeTextureAssignments(settings, large)
            .Reflection.has_value());
}

TEST(Oot3dTextureCatalogSelection,
     SortsAssignedFirstAndKeepsAStablePreferredSelection) {
    using namespace Fast::Oot3d;
    std::vector<TextureCatalogEntry> entries{
        {30U, 0U, 16U, 16U, 0U, 100U},
        {10U, 0U, 8U, 8U, 0U, 1U},
        {20U, 0U, 64U, 64U, 0U, 5U},
    };
    constexpr std::array<uint64_t, 1> assigned{10U};
    entries = SortTextureCatalogEntries(
        std::move(entries),
        TextureCatalogSortMode::AssignedFirst, assigned);
    ASSERT_EQ(entries.size(), 3U);
    EXPECT_EQ(entries[0].ContentHash, 10U);
    EXPECT_EQ(entries[1].ContentHash, 30U);

    TextureCatalogSelectionState state;
    const auto selected = ResolveTextureCatalogSelection(
        state, entries, assigned);
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected->ContentHash, 10U);
    SelectTextureCatalogEntry(state, entries[2]);
    const auto retained = ResolveTextureCatalogSelection(
        state, entries, assigned);
    ASSERT_TRUE(retained.has_value());
    EXPECT_EQ(retained->ContentHash, 20U);
}

TEST(Oot3dTextureCatalogSelection,
     ActivatesRowsByCompleteIdentityAndRetainsTheExactEntry) {
    using namespace Fast::Oot3d;
    const std::array<TextureCatalogEntry, 3> entries{{
        {7U, 0U, 32U, 32U, 1U, 4U},
        {7U, 0U, 64U, 32U, 1U, 3U},
        {9U, 0U, 16U, 16U, 2U, 2U},
    }};
    TextureCatalogSelectionState state;

    SelectTextureCatalogEntry(state, entries[1]);
    EXPECT_FALSE(IsTextureCatalogEntrySelected(
        state, entries[0]));
    EXPECT_TRUE(IsTextureCatalogEntrySelected(
        state, entries[1]));
    const auto retained = ResolveTextureCatalogSelection(
        state, entries);
    ASSERT_TRUE(retained.has_value());
    EXPECT_EQ(retained->ContentHash, 7U);
    EXPECT_EQ(retained->Width, 64U);

    SelectTextureCatalogEntry(state, entries[2]);
    const auto changed = ResolveTextureCatalogSelection(
        state, entries);
    ASSERT_TRUE(changed.has_value());
    EXPECT_EQ(changed->ContentHash, 9U);
    EXPECT_EQ(changed->NativeFormat, 2U);
}

TEST(Oot3dTextureCatalogSelection,
     NavigatesForwardAndBackwardWithWraparound) {
    using namespace Fast::Oot3d;
    const std::array<TextureCatalogEntry, 3> entries{{
        {1U, 0U, 16U, 16U, 1U, 1U},
        {2U, 0U, 32U, 16U, 1U, 1U},
        {3U, 0U, 64U, 16U, 1U, 1U},
    }};
    TextureCatalogSelectionState state;
    SelectTextureCatalogEntry(state, entries[0]);

    const auto previous = MoveTextureCatalogSelection(
        state, entries, -1);
    ASSERT_TRUE(previous.has_value());
    EXPECT_EQ(previous->ContentHash, 3U);
    const auto next = MoveTextureCatalogSelection(
        state, entries, 1);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->ContentHash, 1U);
    const auto skip = MoveTextureCatalogSelection(
        state, entries, 4);
    ASSERT_TRUE(skip.has_value());
    EXPECT_EQ(skip->ContentHash, 2U);
}
