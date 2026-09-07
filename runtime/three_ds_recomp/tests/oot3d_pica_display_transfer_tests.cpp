#include "fast/oot3d/pica_display_transfer.h"

#include <gtest/gtest.h>

namespace {

TEST(Oot3dPicaDisplayTransfer, CropsWithoutRescaling) {
    const auto plan = Fast::Oot3d::BuildPicaDisplayTransferPlan({
        480U, 400U, 240U, 400U, 480U, 400U, 0x00001004U});
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->DestinationWidth, 240U);
    EXPECT_EQ(plan->DestinationHeight, 400U);
    EXPECT_EQ(plan->HorizontalSamples, 1U);
    EXPECT_EQ(plan->VerticalSamples, 1U);
}

TEST(Oot3dPicaDisplayTransfer, AppliesNativeHorizontalBoxFilter) {
    const auto plan = Fast::Oot3d::BuildPicaDisplayTransferPlan({
        480U, 400U, 240U, 400U, 480U, 400U, 0x01001000U});
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->DestinationWidth, 240U);
    EXPECT_EQ(plan->DestinationHeight, 400U);
    EXPECT_EQ(plan->HorizontalSamples, 2U);
    EXPECT_EQ(plan->VerticalSamples, 1U);
}

TEST(Oot3dPicaDisplayTransfer, PreservesRendererSupersampling) {
    const auto plan = Fast::Oot3d::BuildPicaDisplayTransferPlan({
        480U, 400U, 240U, 200U, 960U, 800U, 0x02001000U});
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->DestinationWidth, 480U);
    EXPECT_EQ(plan->DestinationHeight, 400U);
    EXPECT_EQ(plan->HorizontalSamples, 2U);
    EXPECT_EQ(plan->VerticalSamples, 2U);
}

TEST(Oot3dPicaDisplayTransfer, RejectsSamplingPastSource) {
    std::string error;
    const auto plan = Fast::Oot3d::BuildPicaDisplayTransferPlan(
        {480U, 400U, 300U, 400U, 480U, 400U, 0x01001000U},
        &error);
    EXPECT_FALSE(plan.has_value());
    EXPECT_FALSE(error.empty());
}

} // namespace
