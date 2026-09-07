#include "fast/oot3d/pica_pipeline_manifest.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>

namespace Fast::Oot3d {
namespace {

PicaGraphicsPipelineManifestEntry MakeEntry() {
    PicaGraphicsPipelineManifestEntry entry;
    entry.DescriptorSchemaVersion = 3U;
    entry.Domain = PicaGraphicsPipelineDomain::Instrumented;
    entry.VertexShaderKey = 0x1111U;
    entry.FragmentShaderKey = 0x2222U;
    entry.VertexSource = {0x10U, 0x11U, 64U};
    entry.FragmentSource = {0x20U, 0x21U, 128U};
    entry.NriFragmentSource = {0x30U, 0x31U, 160U};
    entry.NriFragmentAvailable = true;
    entry.RequestedFeatures =
        PicaShaderInstrumentationFeature::Toon |
        PicaShaderInstrumentationFeature::NormalGuide;
    entry.AppliedFeatures = PicaShaderInstrumentationFeature::Toon;
    entry.AttachmentRequirementsKey =
        static_cast<uint8_t>(PicaAuxiliaryOutput::NormalGuide |
                             PicaAuxiliaryOutput::MaterialGuide);
    entry.SampleCount = 4U;
    entry.WritesReactiveMask = true;
    entry.Topology = ::Oot3d::Renderer::PicaTopology::TriangleStrip;
    entry.CullMode =
        ::Oot3d::Renderer::NativeCullMode::KeepCounterClockwise;
    entry.FramebufferFlipped = true;
    entry.VertexBindings = {{0U, 32U, false}, {1U, 16U, true}};
    entry.VertexAttributes = {
        {0U, 0U, ::Oot3d::Renderer::PicaVertexFormat::Float, 4U, 0U},
        {1U, 1U, ::Oot3d::Renderer::PicaVertexFormat::SignedShort, 2U, 4U},
    };
    entry.ColorWriteMask = 0x0fU;
    entry.FragmentOperationMode = 1U;
    entry.LogicOperation = ::Oot3d::Renderer::PicaLogicOperation::Copy;
    entry.Blend.Enabled = true;
    entry.Blend.SourceRgb =
        ::Oot3d::Renderer::NativeBlendFactor::SourceAlpha;
    entry.Blend.DestRgb =
        ::Oot3d::Renderer::NativeBlendFactor::OneMinusSourceAlpha;
    entry.AlphaTestEnabled = true;
    entry.DepthTestEnabled = true;
    entry.DepthWriteEnabled = true;
    entry.DepthCompare = ::Oot3d::Renderer::PicaCompareFunction::LessOrEqual;
    entry.Stencil.Enabled = true;
    entry.Stencil.Reference = 4U;
    entry.Stencil.CompareMask = 0xffU;
    entry.Stencil.WriteMask = 0x7fU;
    entry.Stencil.Pass = ::Oot3d::Renderer::PicaStencilAction::Replace;
    entry.ShaderOutputs.SceneDomainBlendedOverlay = true;
    entry.ShaderOutputs.WritesAmbientGuide = true;
    return entry;
}

std::filesystem::path TemporaryPath(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count();
    return std::filesystem::temp_directory_path() /
           (std::string(name) + "_" + std::to_string(stamp) + ".json");
}

TEST(PicaPipelineManifest, StructuralIdentityIgnoresEvidence) {
    auto left = MakeEntry();
    auto right = left;
    left.ObservationCount = 2U;
    left.CanonicalPipelineIds.insert(4U);
    right.ObservationCount = 90U;
    right.SettingsRevisions.insert(12U);

    EXPECT_TRUE(left.Valid());
    EXPECT_TRUE(right.Valid());
    EXPECT_EQ(left.StructuralId(), right.StructuralId());
    EXPECT_TRUE(left.StructurallyEquivalent(right));

    right.DepthWriteEnabled = false;
    EXPECT_NE(left.StructuralId(), right.StructuralId());
    EXPECT_FALSE(left.StructurallyEquivalent(right));
}

TEST(PicaPipelineManifest, RoundTripsTypedPipelineState) {
    const auto path = TemporaryPath("oot3d_pica_pipeline_roundtrip");
    auto entry = MakeEntry();
    entry.ObservationCount = 8U;
    entry.CanonicalPipelineIds = {0x1234U, 0x5678U};
    entry.SettingsRevisions = {2U, 4U};
    std::string error;
    ASSERT_TRUE(WritePicaGraphicsPipelineManifest(
        path, entry.DescriptorSchemaVersion,
        std::span<const PicaGraphicsPipelineManifestEntry>(&entry, 1U),
        &error)) << error;

    PicaGraphicsPipelineManifest manifest;
    ASSERT_TRUE(manifest.Load(path, &error)) << error;
    ASSERT_TRUE(manifest.Loaded());
    ASSERT_EQ(manifest.DescriptorSchemaVersion(), 3U);
    ASSERT_EQ(manifest.Entries().size(), 1U);
    const auto& loaded = manifest.Entries().front();
    EXPECT_TRUE(entry.StructurallyEquivalent(loaded));
    EXPECT_EQ(loaded.ObservationCount, 8U);
    EXPECT_EQ(loaded.CanonicalPipelineIds, entry.CanonicalPipelineIds);
    EXPECT_EQ(loaded.SettingsRevisions, entry.SettingsRevisions);

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST(PicaPipelineManifest, RoundTripsNativeFogInstrumentation) {
    auto entry = MakeEntry();
    const auto oldId = entry.StructuralId();
    entry.RequestedFeatures |= PicaShaderInstrumentationFeature::NativeFogGuide;
    entry.AppliedFeatures |= PicaShaderInstrumentationFeature::NativeFogGuide;
    entry.AttachmentRequirementsKey |= static_cast<uint8_t>(PicaAuxiliaryOutput::FogGuide);
    entry.ShaderOutputs.WritesFogGuide = true;
    entry.RequestedFeatures |= PicaShaderInstrumentationFeature::OutlineGeometryGuide;
    entry.AppliedFeatures |= PicaShaderInstrumentationFeature::OutlineGeometryGuide;
    entry.AttachmentRequirementsKey |= static_cast<uint8_t>(PicaAuxiliaryOutput::OutlineGeometryGuide);
    entry.ShaderOutputs.WritesOutlineGeometryGuide = true;
    EXPECT_NE(entry.StructuralId(), oldId);
    const auto path = TemporaryPath("oot3d_pica_fog_roundtrip");
    std::string error;
    ASSERT_TRUE(WritePicaGraphicsPipelineManifest(
        path, entry.DescriptorSchemaVersion,
        std::span<const PicaGraphicsPipelineManifestEntry>(&entry, 1U), &error)) << error;
    PicaGraphicsPipelineManifest manifest;
    ASSERT_TRUE(manifest.Load(path, &error)) << error;
    ASSERT_EQ(manifest.Entries().size(), 1U);
    EXPECT_TRUE(manifest.Entries().front().ShaderOutputs.WritesFogGuide);
    EXPECT_TRUE(manifest.Entries().front().ShaderOutputs.WritesOutlineGeometryGuide);
    EXPECT_TRUE(entry.StructurallyEquivalent(manifest.Entries().front()));
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST(PicaPipelineManifest, InventoryMergesRepeatedObservations) {
    const auto path = TemporaryPath("oot3d_pica_pipeline_inventory");
    PicaGraphicsPipelineInventory inventory;
    std::string error;
    ASSERT_TRUE(inventory.Configure(path, &error)) << error;
    inventory.Observe(MakeEntry(), 0x40U, 1U);
    inventory.Observe(MakeEntry(), 0x50U, 2U);
    EXPECT_EQ(inventory.EntryCount(), 1U);
    ASSERT_TRUE(inventory.Finish(&error)) << error;

    PicaGraphicsPipelineManifest manifest;
    ASSERT_TRUE(manifest.Load(path, &error)) << error;
    ASSERT_EQ(manifest.Entries().size(), 1U);
    EXPECT_EQ(manifest.Entries().front().ObservationCount, 2U);
    EXPECT_EQ(manifest.Entries().front().CanonicalPipelineIds,
              (std::set<uint64_t>{0x40U, 0x50U}));
    EXPECT_EQ(manifest.Entries().front().SettingsRevisions,
              (std::set<uint64_t>{1U, 2U}));

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST(PicaPipelineManifest, FiltersPrewarmByActiveInstrumentationProfile) {
    auto entry = MakeEntry();
    EXPECT_FALSE(entry.MatchesPrewarmProfile(
        PicaShaderInstrumentationFeature::None, false));
    EXPECT_FALSE(entry.MatchesPrewarmProfile(
        PicaShaderInstrumentationFeature::Toon, false));
    EXPECT_TRUE(entry.MatchesPrewarmProfile(
        PicaShaderInstrumentationFeature::Toon |
            PicaShaderInstrumentationFeature::NormalGuide,
        false));
    EXPECT_FALSE(entry.MatchesPrewarmProfile(
        PicaShaderInstrumentationFeature::Toon |
            PicaShaderInstrumentationFeature::NormalGuide,
        true));

    entry.Domain = PicaGraphicsPipelineDomain::Canonical;
    entry.AppliedFeatures = PicaShaderInstrumentationFeature::None;
    entry.RequestedFeatures = PicaShaderInstrumentationFeature::None;
    ASSERT_TRUE(entry.Valid());
    EXPECT_TRUE(entry.MatchesPrewarmProfile(
        PicaShaderInstrumentationFeature::None, true));
}

TEST(PicaPipelineManifest, RejectsIncompleteStructuralState) {
    auto entry = MakeEntry();
    entry.VertexBindings.clear();
    EXPECT_FALSE(entry.Valid());
    entry = MakeEntry();
    entry.SampleCount = 3U;
    EXPECT_FALSE(entry.Valid());
    entry = MakeEntry();
    entry.NriFragmentAvailable = true;
    entry.NriFragmentSource = {};
    EXPECT_FALSE(entry.Valid());
}

} // namespace
} // namespace Fast::Oot3d
