#include "fast/oot3d/pica_display_transfer.h"
#include "fast/oot3d/render_resolution_policy.h"

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

TEST(Oot3dPicaDisplayTransfer, FractionalScaleAndOddOutputKeepValidNativeCommands) {
    using namespace Fast::Oot3d;
    for (const RenderExtent output : {RenderExtent{1280, 720}, {1024, 768}, {3840, 2160}, {1279, 719}}) {
        for (int percent = 50; percent <= 200; ++percent) {
            const auto extent = ResolveNativePicaRenderExtent({480, 400}, output, percent / 100.0F);
            const auto plan = BuildPicaDisplayTransferPlan({480, 400, 240, 400, extent.Width, extent.Height, 0x01001000});
            ASSERT_TRUE(plan.has_value()) << percent << "% at " << output.Width << "x" << output.Height;
            EXPECT_TRUE(plan->SamplingFitsSource());
            EXPECT_LE(plan->BlitSourceWidth(), extent.Width);
            EXPECT_LE(plan->BlitSourceHeight(), extent.Height);
            EXPECT_LE(plan->DestinationWidth * 2U, extent.Width + 1U);
            EXPECT_EQ(plan->DestinationHeight, extent.Height);
            EXPECT_NEAR(double(plan->DestinationHeight) / plan->DestinationWidth,
                        double(output.Width) / output.Height, 0.01);
        }
    }
    const auto bothAxes = BuildPicaDisplayTransferPlan({480, 400, 240, 200, 485, 405, 0x02001000});
    ASSERT_TRUE(bothAxes);
    EXPECT_EQ(bothAxes->DestinationWidth, 243U);
    EXPECT_EQ(bothAxes->DestinationHeight, 203U);
    // Small host extents must not conceal an invalid guest command through rounding.
    EXPECT_FALSE(BuildPicaDisplayTransferPlan({480, 400, 241, 400, 1, 1, 0x01001000}));
}

TEST(Oot3dPicaDisplayTransfer, SharedSamplingValidationOnlyAllowsRoundingAtTheEdge) {
    Fast::Oot3d::PicaDisplayTransferPlan plan{1051, 935, 526, 935, 2, 1, 1};
    EXPECT_TRUE(plan.SamplingFitsSource());
    EXPECT_EQ(plan.BlitSourceWidth(), 1051U);
    ++plan.DestinationWidth;
    EXPECT_FALSE(plan.SamplingFitsSource());
    --plan.DestinationWidth;
    ++plan.DestinationHeight;
    EXPECT_FALSE(plan.SamplingFitsSource());
    plan.DestinationWidth = UINT32_MAX;
    EXPECT_FALSE(plan.SamplingFitsSource());
    EXPECT_LE(plan.BlitSourceWidth(), plan.SourceWidth);
}

} // namespace
