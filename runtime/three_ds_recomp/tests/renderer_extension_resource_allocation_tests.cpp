#include "fast/renderer/extension_resource_allocation.h"

#include <gtest/gtest.h>

#include <array>

namespace {

using namespace Fast::Renderer;

constexpr uint64_t kResourceNamespace = 0xA110CA7EU;
constexpr uint64_t kStorageNamespace = 0x570A6EULL;

constexpr ExtensionResourceIdentity Resource(uint64_t value) {
    return {kResourceNamespace, value, 1U};
}

constexpr ExtensionStorageClassIdentity Storage(uint64_t value) {
    return {kStorageNamespace, value, 1U};
}

constexpr ExtensionTransientImageRequirement Requirement(
    uint64_t resource, uint64_t storage = 1U, uint32_t format = 97U,
    uint32_t usage = 1U) {
    return {
        Resource(resource),
        ExtensionResourceResidency::TransientImage,
        Storage(storage),
        format,
        400U,
        240U,
        1U,
        1U,
        1U,
        usage,
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

TEST(RendererExtensionResourceAllocation,
     AliasesCompatibleDisjointLifetimesAndMergesUsage) {
    const auto graph = Graph(4U, {
        {Resource(1U), 0U, 1U, 1U, 1U},
        {Resource(2U), 2U, 3U, 1U, 1U},
    });
    const std::array requirements{
        Requirement(1U, 1U, 97U, 0x1U),
        Requirement(2U, 1U, 97U, 0x4U),
    };

    const auto result = CompileExtensionTransientAllocation(
        graph, requirements);
    ASSERT_TRUE(result.Summary.Complete());
    ASSERT_EQ(result.Resources.size(), 2U);
    ASSERT_EQ(result.Slots.size(), 1U);
    EXPECT_EQ(result.Summary.AliasOpportunityCount, 1U);
    EXPECT_EQ(result.Summary.PeakLiveResourceCount, 1U);
    EXPECT_EQ(result.Slots.front().Usage, 0x5U);
    EXPECT_EQ(result.Slots.front().Resources.size(), 2U);
    EXPECT_EQ(result.Resources[0].Slot, result.Resources[1].Slot);
}

TEST(RendererExtensionResourceAllocation,
     KeepsOverlapsAndIncompatibleImageClassesSeparate) {
    const auto graph = Graph(6U, {
        {Resource(1U), 0U, 2U, 1U, 1U},
        {Resource(2U), 1U, 3U, 1U, 1U},
        {Resource(3U), 4U, 4U, 1U, 1U},
        {Resource(4U), 5U, 5U, 1U, 1U},
    });
    const std::array requirements{
        Requirement(1U),
        Requirement(2U),
        Requirement(3U, 1U, 98U),
        Requirement(4U, 2U, 97U),
    };

    const auto result = CompileExtensionTransientAllocation(
        graph, requirements);
    ASSERT_TRUE(result.Summary.Complete());
    EXPECT_EQ(result.Slots.size(), 4U);
    EXPECT_EQ(result.Summary.AliasOpportunityCount, 0U);
    EXPECT_EQ(result.Summary.PeakLiveResourceCount, 2U);
}

TEST(RendererExtensionResourceAllocation,
     ReportsInvalidRequirementsAndResourcesWithoutWriters) {
    const auto graph = Graph(2U, {
        {Resource(1U), 0U, 1U, 1U, 1U},
        {Resource(2U), 0U, 1U, 1U, 0U},
    });
    auto nonTransient = Requirement(3U);
    nonTransient.Residency = ExtensionResourceResidency::TemporalHistory;
    const std::array requirements{
        Requirement(1U),
        Requirement(1U),
        Requirement(2U),
        nonTransient,
    };

    const auto result = CompileExtensionTransientAllocation(
        graph, requirements);
    EXPECT_FALSE(result.Summary.Complete());
    EXPECT_EQ(result.Summary.RequestedResourceCount, 4U);
    EXPECT_EQ(result.Summary.PlannedResourceCount, 1U);
    EXPECT_EQ(result.Summary.InvalidRequirementCount, 2U);
    EXPECT_EQ(result.Summary.MissingLifetimeCount, 1U);
    ASSERT_EQ(result.InvalidRequirements.size(), 2U);
    ASSERT_EQ(result.MissingLifetimes.size(), 1U);
    EXPECT_EQ(result.MissingLifetimes.front(), Resource(2U));
}

TEST(RendererExtensionResourceAllocation,
     RejectsAnInvalidGraphWithoutPlanningStorage) {
    auto graph = Graph(1U, {
        {Resource(1U), 0U, 0U, 0U, 1U},
    });
    graph.Error = "invalid portable resource graph";
    const std::array requirements{Requirement(1U)};

    const auto result = CompileExtensionTransientAllocation(
        graph, requirements);
    EXPECT_FALSE(result.Summary.Complete());
    EXPECT_EQ(result.Summary.InvalidRequirementCount, 1U);
    EXPECT_TRUE(result.Resources.empty());
    EXPECT_TRUE(result.Slots.empty());
}

} // namespace
