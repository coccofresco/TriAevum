#include "fast/renderer/extension_resource_binding.h"

#include <gtest/gtest.h>

#include <array>

namespace {

using namespace Fast::Renderer;

constexpr uint64_t kResourceNamespace = 0xB17D1EULL;
constexpr uint64_t kObjectNamespace = 0x0B1EC7ULL;
constexpr uint64_t kStorageNamespace = 0x570A6EULL;

constexpr ExtensionResourceIdentity Resource(uint64_t value) {
    return {kResourceNamespace, value, 1U};
}

constexpr ExtensionObjectIdentity Object(uint64_t value) {
    return {kObjectNamespace, value, 1U};
}

constexpr ExtensionStorageClassIdentity Storage(uint64_t value = 1U) {
    return {kStorageNamespace, value, 1U};
}

constexpr ExtensionResourceBinding Image(
    uint64_t resource, uint64_t object, uint32_t format = 97U,
    uint32_t width = 400U, uint32_t height = 240U) {
    return {
        Resource(resource),
        ExtensionResourceBindingKind::Image,
        Object(object),
        1U,
        format,
        width,
        height,
        true,
    };
}

constexpr ExtensionResourceBinding Semantic(
    uint64_t resource, uint64_t object) {
    return {
        Resource(resource),
        ExtensionResourceBindingKind::Semantic,
        Object(object),
        1U,
    };
}

CompiledExtensionResourceGraph Graph(
    size_t passCount,
    std::initializer_list<ExtensionResourceLifetime> lifetimes) {
    CompiledExtensionResourceGraph graph;
    graph.PassCount = passCount;
    graph.Lifetimes = lifetimes;
    return graph;
}

TEST(RendererExtensionResourceBinding,
     ResolvesPortableBindingsAndReadDeclarations) {
    const std::array bindings{
        Image(1U, 10U),
        Semantic(2U, 20U),
        ExtensionResourceBinding{},
    };
    const ExtensionResourceBindingTableView table(bindings);
    ASSERT_TRUE(table.Valid());
    EXPECT_EQ(table.BoundCount(), 2U);
    ASSERT_NE(table.Find(Resource(1U)), nullptr);
    ASSERT_NE(table.Find(Resource(2U)), nullptr);
    EXPECT_EQ(table.Find(Resource(3U)), nullptr);

    const std::array uses{
        ExtensionResourceUse{Resource(1U), ExtensionResourceAccess::Read},
        ExtensionResourceUse{Resource(2U), ExtensionResourceAccess::Read},
        ExtensionResourceUse{Resource(3U), ExtensionResourceAccess::Read},
        ExtensionResourceUse{Resource(4U), ExtensionResourceAccess::Write},
    };
    const auto validation = table.ValidateReads(uses);
    EXPECT_FALSE(validation.Complete());
    EXPECT_EQ(validation.DeclaredReadCount, 3U);
    EXPECT_EQ(validation.ResolvedReadCount, 2U);
    EXPECT_EQ(validation.MissingReadCount, 1U);
}

TEST(RendererExtensionResourceBinding,
     RejectsMalformedAndDuplicatePublications) {
    auto malformed = Semantic(1U, 10U);
    malformed.Sampleable = true;
    const std::array malformedTable{malformed};
    EXPECT_FALSE(
        ExtensionResourceBindingTableView(malformedTable).Valid());

    const std::array duplicateTable{
        Image(1U, 10U),
        Image(1U, 11U),
    };
    EXPECT_FALSE(
        ExtensionResourceBindingTableView(duplicateTable).Valid());
}

TEST(RendererExtensionPhysicalPlan,
     AccountsResidencyImagesMissingBindingsAndAliases) {
    const auto graph = Graph(4U, {
        {Resource(1U), 0U, 3U, 1U, 0U},
        {Resource(2U), 0U, 3U, 1U, 0U},
        {Resource(3U), 0U, 3U, 1U, 0U},
        {Resource(4U), 0U, 1U, 1U, 1U},
        {Resource(5U), 2U, 3U, 1U, 1U},
        {Resource(6U), 0U, 3U, 1U, 1U},
    });
    const std::array policies{
        ExtensionPhysicalResourcePolicy{
            Resource(1U), ExtensionResourceResidency::ExternalImage, {}},
        ExtensionPhysicalResourcePolicy{
            Resource(2U), ExtensionResourceResidency::NativeAttachment, {}},
        ExtensionPhysicalResourcePolicy{
            Resource(3U), ExtensionResourceResidency::Semantic, {}},
        ExtensionPhysicalResourcePolicy{
            Resource(4U), ExtensionResourceResidency::TransientImage,
            Storage()},
        ExtensionPhysicalResourcePolicy{
            Resource(5U), ExtensionResourceResidency::TransientImage,
            Storage()},
        ExtensionPhysicalResourcePolicy{
            Resource(6U), ExtensionResourceResidency::TemporalHistory, {}},
    };
    const std::array bindings{
        Image(1U, 10U),
        Image(2U, 20U),
        Semantic(3U, 30U),
        Image(4U, 40U),
        Image(5U, 40U),
    };
    std::array<ExtensionPhysicalResource, 6U> resources{};
    std::array<ExtensionPhysicalAliasSlot, 6U> slots{};

    const auto result = CompileExtensionPhysicalPlan(
        graph, policies, ExtensionResourceBindingTableView(bindings),
        resources, slots);
    ASSERT_TRUE(result.Valid());
    EXPECT_FALSE(result.Complete());
    EXPECT_EQ(result.ResourceCount, 6U);
    EXPECT_EQ(result.Summary.ResolvedResourceCount, 5U);
    EXPECT_EQ(result.Summary.MissingResourceCount, 1U);
    EXPECT_EQ(result.Summary.SemanticResourceCount, 1U);
    EXPECT_EQ(result.Summary.ExternalImageResourceCount, 1U);
    EXPECT_EQ(result.Summary.NativeAttachmentResourceCount, 1U);
    EXPECT_EQ(result.Summary.TransientImageResourceCount, 2U);
    EXPECT_EQ(result.Summary.TemporalHistoryResourceCount, 1U);
    EXPECT_EQ(result.Summary.PhysicalImageCount, 3U);
    EXPECT_EQ(result.Summary.AliasEligibleResourceCount, 2U);
    EXPECT_EQ(result.Summary.AliasSlotCount, 1U);
    EXPECT_EQ(result.Summary.AliasOpportunityCount, 1U);
    EXPECT_EQ(result.Summary.PeakLiveTransientCount, 1U);
    EXPECT_EQ(slots[0].ResourceCount, 2U);
    EXPECT_EQ(resources[3].AliasSlot, resources[4].AliasSlot);
}

TEST(RendererExtensionPhysicalPlan,
     RejectsWrongKindsPoliciesAndInsufficientStorage) {
    const auto graph = Graph(1U, {
        {Resource(1U), 0U, 0U, 1U, 1U},
    });
    const std::array semanticPolicy{
        ExtensionPhysicalResourcePolicy{
            Resource(1U), ExtensionResourceResidency::Semantic, {}},
    };
    const std::array imageBinding{Image(1U, 10U)};
    std::array<ExtensionPhysicalResource, 1U> resources{};
    std::array<ExtensionPhysicalAliasSlot, 1U> slots{};
    const auto wrongKind = CompileExtensionPhysicalPlan(
        graph, semanticPolicy,
        ExtensionResourceBindingTableView(imageBinding), resources, slots);
    ASSERT_TRUE(wrongKind.Valid());
    EXPECT_FALSE(wrongKind.Complete());
    EXPECT_EQ(wrongKind.Summary.MissingResourceCount, 1U);

    const std::array duplicatePolicies{
        semanticPolicy[0],
        semanticPolicy[0],
    };
    const auto invalidPolicy = CompileExtensionPhysicalPlan(
        graph, duplicatePolicies,
        ExtensionResourceBindingTableView(imageBinding), resources, slots);
    EXPECT_EQ(invalidPolicy.Status,
              ExtensionPhysicalPlanStatus::InvalidPolicy);

    const auto noCapacity = CompileExtensionPhysicalPlan(
        graph, semanticPolicy,
        ExtensionResourceBindingTableView(imageBinding), {}, slots);
    EXPECT_EQ(noCapacity.Status,
              ExtensionPhysicalPlanStatus::InsufficientOutputCapacity);
}

} // namespace
