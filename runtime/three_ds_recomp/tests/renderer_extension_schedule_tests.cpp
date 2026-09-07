#include "fast/renderer/extension_schedule.h"

#include <gtest/gtest.h>

namespace {

using namespace Fast::Renderer;

TEST(RendererExtensionSchedule,
     AuthorizesOnlyReachedBoundariesAtTheDeclaredStage) {
    const auto plan = BuildExtensionPassSchedulePlan(
        StableExtensionPassId("portable-shadow-map"),
        ExtensionStage::AfterOpaque);
    ASSERT_TRUE(plan.Valid());

    ExtensionScheduleBoundary boundary{
        71U,
        {5U, 0x18000000U, 0xCAFEU},
        ExtensionStage::AfterOpaque,
        ExtensionScheduleBoundaryKind::BeforeSubmission,
        42U,
        false,
    };
    auto decision = plan.Resolve(boundary);
    EXPECT_EQ(decision.Reason,
              ExtensionPassScheduleDecisionReason::BoundaryNotReached);

    boundary.Reached = true;
    boundary.Stage = ExtensionStage::BeforeTransparent;
    decision = plan.Resolve(boundary);
    EXPECT_EQ(decision.Reason,
              ExtensionPassScheduleDecisionReason::StageMismatch);

    boundary.Stage = ExtensionStage::AfterOpaque;
    decision = plan.Resolve(boundary);
    ASSERT_TRUE(decision.Authorized());
    EXPECT_EQ(decision.Authorization.ScheduleId, 71U);
    EXPECT_EQ(decision.Authorization.Surface, boundary.Surface);
    EXPECT_EQ(decision.Authorization.BeforeSubmissionId, 42U);
    EXPECT_TRUE(decision.Authorization.Matches(
        plan.PassId(), ExtensionStage::AfterOpaque, boundary.Surface));
    EXPECT_FALSE(decision.Authorization.Matches(
        plan.PassId() + 1U, ExtensionStage::AfterOpaque, boundary.Surface));
    EXPECT_FALSE(decision.Authorization.Matches(
        plan.PassId(), ExtensionStage::AfterTransparent, boundary.Surface));
    EXPECT_FALSE(decision.Authorization.Matches(
        plan.PassId(), ExtensionStage::AfterOpaque,
        {boundary.Surface.Namespace, boundary.Surface.Resource, 0xBADU}));

    boundary.Kind = ExtensionScheduleBoundaryKind::SurfaceEnd;
    EXPECT_EQ(plan.Resolve(boundary).Reason,
              ExtensionPassScheduleDecisionReason::BoundaryUnavailable);
}

TEST(RendererExtensionSchedule, UsesStableNonTitleSpecificPassIdentifiers) {
    const auto first = StableExtensionPassId("shadow-map");
    const auto repeated = StableExtensionPassId("shadow-map");
    const auto different = StableExtensionPassId("atmosphere");

    EXPECT_NE(first, 0U);
    EXPECT_EQ(first, repeated);
    EXPECT_NE(first, different);
    EXPECT_EQ(StableExtensionPassId({}), 0U);
}

} // namespace
