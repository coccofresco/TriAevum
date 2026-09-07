#include "fast/renderer/extension_resource_graph.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace {

using namespace Fast::Renderer;

constexpr uint64_t kNamespace = 0xA110CA7EU;

constexpr ExtensionResourceIdentity Resource(uint64_t value) {
    return {kNamespace, value, 1U};
}

ExtensionResourceDeclaration External(
    uint64_t value, bool managedBarrier = true) {
    return {
        Resource(value),
        ExtensionResourceOrigin::External,
        managedBarrier ? ExtensionInitialBarrierPolicy::Managed
                       : ExtensionInitialBarrierPolicy::None,
    };
}

ExtensionResourceDeclaration Produced(uint64_t value) {
    return {
        Resource(value),
        ExtensionResourceOrigin::GraphProduced,
        ExtensionInitialBarrierPolicy::None,
    };
}

TEST(RendererExtensionResourceGraph,
     CompilesPortableLifetimesHazardsAndExports) {
    ExtensionResourceGraphInput input;
    input.Resources = {External(1U), Produced(2U), Produced(3U)};
    input.Passes = {
        {
            10U,
            "producer",
            {},
            {
                {Resource(1U), ExtensionResourceAccess::Read},
                {Resource(2U), ExtensionResourceAccess::Write},
            },
        },
        {
            20U,
            "consumer",
            {10U},
            {
                {Resource(2U), ExtensionResourceAccess::Read},
                {Resource(3U), ExtensionResourceAccess::Write},
            },
        },
    };
    input.Exports = {Resource(3U)};

    const auto result = CompileExtensionResourceGraph(input);
    ASSERT_TRUE(result.Valid()) << result.Error;
    EXPECT_EQ(result.PassCount, 2U);
    ASSERT_EQ(result.Lifetimes.size(), 3U);
    ASSERT_EQ(result.Barriers.size(), 2U);
    EXPECT_FALSE(result.Barriers[0].ProducerPass.has_value());
    EXPECT_EQ(result.Barriers[0].Resource, Resource(1U));
    ASSERT_TRUE(result.Barriers[1].ProducerPass.has_value());
    EXPECT_EQ(*result.Barriers[1].ProducerPass, 0U);
    EXPECT_EQ(result.Barriers[1].ConsumerPass, 1U);
    ASSERT_EQ(result.Exports.size(), 1U);
    EXPECT_EQ(result.Exports.front(), Resource(3U));
    const auto exported = std::find_if(
        result.Lifetimes.begin(), result.Lifetimes.end(),
        [](const ExtensionResourceLifetime& lifetime) {
            return lifetime.Resource == Resource(3U);
        });
    ASSERT_NE(exported, result.Lifetimes.end());
    EXPECT_EQ(exported->LastUse, input.Passes.size());
    EXPECT_EQ(exported->ReadCount, 1U);
}

TEST(RendererExtensionResourceGraph,
     AcceptsTransitiveProducerDependencies) {
    ExtensionResourceGraphInput input;
    input.Resources = {Produced(1U), Produced(2U)};
    input.Passes = {
        {10U, "producer", {},
         {{Resource(1U), ExtensionResourceAccess::Write}}},
        {20U, "middle", {10U},
         {{Resource(2U), ExtensionResourceAccess::Write}}},
        {30U, "consumer", {20U},
         {{Resource(1U), ExtensionResourceAccess::Read}}},
    };

    const auto result = CompileExtensionResourceGraph(input);
    EXPECT_TRUE(result.Valid()) << result.Error;
}

TEST(RendererExtensionResourceGraph,
     RejectsReadsWithoutProducerDependencyAndUnorderedWriters) {
    ExtensionResourceGraphInput missingDependency;
    missingDependency.Resources = {Produced(1U)};
    missingDependency.Passes = {
        {10U, "producer", {},
         {{Resource(1U), ExtensionResourceAccess::Write}}},
        {20U, "consumer", {},
         {{Resource(1U), ExtensionResourceAccess::Read}}},
    };
    EXPECT_FALSE(CompileExtensionResourceGraph(missingDependency).Valid());

    ExtensionResourceGraphInput unorderedWriters;
    unorderedWriters.Resources = {Produced(1U)};
    unorderedWriters.Passes = {
        {10U, "first", {},
         {{Resource(1U), ExtensionResourceAccess::Write}}},
        {20U, "second", {},
         {{Resource(1U), ExtensionResourceAccess::Write}}},
    };
    EXPECT_FALSE(CompileExtensionResourceGraph(unorderedWriters).Valid());
}

TEST(RendererExtensionResourceGraph,
     RejectsInvalidCatalogsSchedulesAndExports) {
    ExtensionResourceGraphInput invalidBarrier;
    invalidBarrier.Resources = {{
        Resource(1U),
        ExtensionResourceOrigin::GraphProduced,
        ExtensionInitialBarrierPolicy::Managed,
    }};
    EXPECT_FALSE(CompileExtensionResourceGraph(invalidBarrier).Valid());

    ExtensionResourceGraphInput unorderedDependency;
    unorderedDependency.Resources = {External(1U, false)};
    unorderedDependency.Passes = {
        {10U, "first", {20U},
         {{Resource(1U), ExtensionResourceAccess::Read}}},
        {20U, "second", {}, {}},
    };
    EXPECT_FALSE(CompileExtensionResourceGraph(unorderedDependency).Valid());

    ExtensionResourceGraphInput missingExport;
    missingExport.Resources = {Produced(1U)};
    missingExport.Exports = {Resource(1U)};
    EXPECT_FALSE(CompileExtensionResourceGraph(missingExport).Valid());
}

} // namespace
