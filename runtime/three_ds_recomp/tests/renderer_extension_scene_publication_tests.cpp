#include "fast/renderer/extension_scene_publication.h"

#include <gtest/gtest.h>

#include <array>

namespace {

using namespace Fast::Renderer;

constexpr ExtensionSceneCapabilityIdentity Capability(uint64_t value) {
    return { 0x5343454E45434150ULL, value };
}

ExtensionSceneCapabilityPublication Publication(
    uint64_t capability, const void* payload, uint32_t schema,
    uint64_t frameId = 7U) {
    return {
        Capability(capability),
        { 0x5343454E454F424AULL,
          static_cast<uint64_t>(reinterpret_cast<uintptr_t>(payload)), 1U },
        payload,
        schema,
        3U,
        frameId,
    };
}

constexpr ExtensionSceneCapabilityRequirement Requirement(
    uint64_t capability, uint32_t minimum, uint32_t maximum) {
    return { Capability(capability), minimum, maximum };
}

TEST(RendererExtensionScenePublication,
     ResolvesOneCoherentAllocationFreeSnapshot) {
    const uint32_t drawStream = 11U;
    const uint32_t camera = 13U;
    const std::array publications{
        Publication(1U, &drawStream, 4U),
        Publication(2U, &camera, 2U),
        ExtensionSceneCapabilityPublication{},
    };
    const ExtensionScenePublicationTableView table(7U, publications);
    ASSERT_TRUE(table.Valid());
    EXPECT_EQ(table.FrameId(), 7U);
    EXPECT_EQ(table.PublishedCount(), 2U);
    ASSERT_NE(table.Find(Capability(1U)), nullptr);
    EXPECT_EQ(table.Find(Capability(1U))->Payload, &drawStream);

    const std::array requirements{
        Requirement(1U, 4U, 4U),
        Requirement(2U, 1U, 3U),
    };
    const auto resolution =
        ResolveExtensionSceneCapabilities(table, requirements);
    EXPECT_TRUE(resolution.Complete());
    EXPECT_EQ(resolution.RequiredCapabilityCount, 2U);
    EXPECT_EQ(resolution.ResolvedCapabilityCount, 2U);
}

TEST(RendererExtensionScenePublication,
     DistinguishesMissingAndIncompatibleCapabilities) {
    const uint32_t payload = 17U;
    const std::array publications{ Publication(1U, &payload, 3U) };
    const ExtensionScenePublicationTableView table(7U, publications);

    const std::array missing{ Requirement(2U, 1U, 1U) };
    const auto missingResult =
        ResolveExtensionSceneCapabilities(table, missing);
    EXPECT_EQ(missingResult.Status,
              ExtensionSceneCapabilityResolutionStatus::MissingCapability);
    EXPECT_EQ(missingResult.FailedCapability, Capability(2U));

    const std::array incompatible{ Requirement(1U, 4U, 5U) };
    const auto schemaResult =
        ResolveExtensionSceneCapabilities(table, incompatible);
    EXPECT_EQ(schemaResult.Status,
              ExtensionSceneCapabilityResolutionStatus::IncompatibleSchema);
    EXPECT_EQ(schemaResult.PublishedSchemaVersion, 3U);
}

TEST(RendererExtensionScenePublication,
     RejectsMalformedDuplicateAndMixedFramePublications) {
    const uint32_t first = 19U;
    const uint32_t second = 23U;
    const std::array duplicate{
        Publication(1U, &first, 1U),
        Publication(1U, &second, 1U),
    };
    EXPECT_FALSE(
        ExtensionScenePublicationTableView(7U, duplicate).Valid());

    const std::array mixedFrame{
        Publication(1U, &first, 1U, 7U),
        Publication(2U, &second, 1U, 8U),
    };
    EXPECT_FALSE(
        ExtensionScenePublicationTableView(7U, mixedFrame).Valid());

    auto malformed = Publication(1U, &first, 1U);
    malformed.Payload = nullptr;
    const std::array malformedTable{ malformed };
    EXPECT_FALSE(
        ExtensionScenePublicationTableView(7U, malformedTable).Valid());
}

TEST(RendererExtensionScenePublication,
     TreatsEmptySnapshotAsUnavailableAndRejectsAmbiguousRequirements) {
    const ExtensionScenePublicationTableView empty;
    ASSERT_TRUE(empty.Valid());
    EXPECT_EQ(empty.PublishedCount(), 0U);
    const std::array oneRequirement{ Requirement(1U, 1U, 1U) };
    EXPECT_EQ(ResolveExtensionSceneCapabilities(empty, oneRequirement).Status,
              ExtensionSceneCapabilityResolutionStatus::MissingCapability);

    const uint32_t payload = 29U;
    const std::array publications{ Publication(1U, &payload, 1U) };
    const std::array duplicateRequirements{
        Requirement(1U, 1U, 1U),
        Requirement(1U, 1U, 2U),
    };
    EXPECT_EQ(
        ResolveExtensionSceneCapabilities(
            ExtensionScenePublicationTableView(7U, publications),
            duplicateRequirements)
            .Status,
        ExtensionSceneCapabilityResolutionStatus::InvalidRequirements);
}

} // namespace
