#include "fast/extension/pica_display_transfer.h"
#include "fast/extension/render_resolution_policy.h"
#include "fast/renderer3ds/pica_framebuffer_encode.h"

#include <gtest/gtest.h>

namespace {

TEST(PicaFramebufferCodec, NativeColourAndDepthRoundTrip) {
    using namespace Fast::Renderer3ds;
    constexpr uint32_t width = 16, height = 8;
    std::vector<uint8_t> rgba(width * height * 4);
    for (size_t i = 0; i < rgba.size(); ++i) rgba[i] = static_cast<uint8_t>(i * 37U + 11U);
    // RGBA8 is exact; packed formats keep their bits.
    for (uint8_t format = 0; format <= 4; ++format) {
        const auto encoded = EncodePicaFramebuffer(rgba, width, height, width, height, format);
        const auto decoded = DecodePicaFramebuffer(encoded, width, height, format);
        EXPECT_EQ(EncodePicaFramebuffer(decoded, width, height, width, height, format), encoded) << int(format);
        if (format == 0) EXPECT_EQ(decoded, rgba);
    }
    std::vector<float> depth(width * height);
    std::vector<uint8_t> stencil(width * height);
    for (size_t i = 0; i < depth.size(); ++i) {
        depth[i] = static_cast<float>(i) / static_cast<float>(depth.size());
        stencil[i] = static_cast<uint8_t>(i * 7U);
    }
    for (const uint8_t format : {uint8_t{0}, uint8_t{2}, uint8_t{3}}) {
        std::vector<float> decodedDepth;
        std::vector<uint8_t> decodedStencil;
        DecodePicaDepthStencil(EncodePicaDepthStencil(depth, stencil, width, height, format), width, height,
                               format, decodedDepth, decodedStencil);
        ASSERT_EQ(decodedDepth.size(), depth.size());
        const float step = format == 0 ? 1.0F / 65535.0F : 1.0F / 16777215.0F;
        for (size_t i = 0; i < depth.size(); ++i) EXPECT_NEAR(decodedDepth[i], depth[i], step);
        EXPECT_EQ(decodedStencil, format == 3 ? stencil : std::vector<uint8_t>{});
    }
}

TEST(Oot3dPicaDisplayTransfer, CropsWithoutRescaling) {
    const auto plan = Fast::Extension::BuildPicaDisplayTransferPlan({
        480U, 400U, 240U, 400U, 480U, 400U, 0x00001004U});
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->DestinationWidth, 240U);
    EXPECT_EQ(plan->DestinationHeight, 400U);
    EXPECT_EQ(plan->HorizontalSamples, 1U);
    EXPECT_EQ(plan->VerticalSamples, 1U);
}

TEST(Oot3dPicaDisplayTransfer, AppliesNativeHorizontalBoxFilter) {
    const auto plan = Fast::Extension::BuildPicaDisplayTransferPlan({
        480U, 400U, 240U, 400U, 480U, 400U, 0x01001000U});
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->DestinationWidth, 240U);
    EXPECT_EQ(plan->DestinationHeight, 400U);
    EXPECT_EQ(plan->HorizontalSamples, 2U);
    EXPECT_EQ(plan->VerticalSamples, 1U);
}

TEST(Oot3dPicaDisplayTransfer, PreservesRendererSupersampling) {
    const auto plan = Fast::Extension::BuildPicaDisplayTransferPlan({
        480U, 400U, 240U, 200U, 960U, 800U, 0x02001000U});
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->DestinationWidth, 480U);
    EXPECT_EQ(plan->DestinationHeight, 400U);
    EXPECT_EQ(plan->HorizontalSamples, 2U);
    EXPECT_EQ(plan->VerticalSamples, 2U);
}

TEST(Oot3dPicaDisplayTransfer, CopiesASourceAtPresentedDensity) {
    // A 4K top-screen target (one sample per output pixel): the guest's 2:1
    // filter becomes a copy of the whole target.
    const auto plan = Fast::Extension::BuildPicaDisplayTransferPlan({
        480U, 400U, 240U, 400U, 2160U, 3840U, 0x01001000U, true});
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->DestinationWidth, 2160U);
    EXPECT_EQ(plan->DestinationHeight, 3840U);
    EXPECT_EQ(plan->HorizontalSamples, 1U);
    EXPECT_EQ(plan->VerticalSamples, 1U);
    EXPECT_TRUE(plan->SamplingFitsSource());
    // A guest crop keeps its native proportion of the source.
    const auto crop = Fast::Extension::BuildPicaDisplayTransferPlan({
        480U, 400U, 120U, 400U, 2160U, 3840U, 0x01001000U, true});
    ASSERT_TRUE(crop.has_value());
    EXPECT_EQ(crop->DestinationWidth, 1080U);
    // A 2x2 reduction of that source (MM3D's half-size copies): X collapses,
    // the 400-line axis is really halved.
    const auto reduced = Fast::Extension::BuildPicaDisplayTransferPlan({
        512U, 400U, 256U, 200U, 2304U, 3840U, 0x02000000U, true});
    ASSERT_TRUE(reduced.has_value());
    EXPECT_EQ(reduced->DestinationWidth, 2304U);
    EXPECT_EQ(reduced->DestinationHeight, 1920U);
    EXPECT_EQ(reduced->HorizontalSamples, 1U);
    EXPECT_EQ(reduced->VerticalSamples, 2U);
    EXPECT_TRUE(reduced->SamplingFitsSource());
}

TEST(Oot3dPicaDisplayTransfer, RejectsSamplingPastSource) {
    std::string error;
    const auto plan = Fast::Extension::BuildPicaDisplayTransferPlan(
        {480U, 400U, 300U, 400U, 480U, 400U, 0x01001000U},
        &error);
    EXPECT_FALSE(plan.has_value());
    EXPECT_FALSE(error.empty());
}

TEST(Oot3dPicaDisplayTransfer, FractionalScaleAndOddOutputKeepValidNativeCommands) {
    using namespace Fast::Extension;
    for (const RenderExtent output : {RenderExtent{1280, 720}, {1024, 768}, {3840, 2160}, {1279, 719}}) {
        for (int percent = 50; percent <= 200; ++percent) {
            const auto extent = ResolveNativePicaRenderExtent({480, 400}, output, percent / 100.0F);
            const auto plan = BuildPicaDisplayTransferPlan(
                {480, 400, 240, 400, extent.Width, extent.Height, 0x01001000, NativeTopScreenAtPresentedDensity(output)});
            ASSERT_TRUE(plan.has_value()) << percent << "% at " << output.Width << "x" << output.Height;
            EXPECT_TRUE(plan->SamplingFitsSource());
            EXPECT_LE(plan->BlitSourceWidth(), extent.Width);
            EXPECT_LE(plan->BlitSourceHeight(), extent.Height);
            EXPECT_EQ(plan->HorizontalSamples, 1U);
            EXPECT_EQ(plan->DestinationWidth, extent.Width);
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
    Fast::Extension::PicaDisplayTransferPlan plan{1051, 935, 526, 935, 2, 1, 1};
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
