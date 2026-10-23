#include "fast/renderer3ds/pica_surface_calibration_trace.h"
#include "fast/renderer3ds/pica_surface_lighting.h"
#include <gtest/gtest.h>
#include <limits>

using namespace Fast::Renderer3ds;

TEST(SurfaceCalibrationTrace, ExplicitOptInAndDisabledRecorder) {
    EXPECT_FALSE(PicaSurfaceCalibrationTrace::Requested(nullptr));
    for (const char* value : {"", "0", "true", "10", "1 "})
        EXPECT_FALSE(PicaSurfaceCalibrationTrace::Requested(value));
    EXPECT_TRUE(PicaSurfaceCalibrationTrace::Requested("1"));
    PicaSurfaceCalibrationTrace trace(false);
    for (uint64_t frame : {1, 120, 240}) {
        trace.BeginFrame(frame);
        EXPECT_FALSE(trace.Sampling());
        EXPECT_FALSE(trace.WouldKeep(100));
        trace.Count("ignored", 100);
        trace.Keep(100, "ignored");
        EXPECT_TRUE(trace.FinishFrame("ignored").empty());
    }
}

TEST(SurfaceCalibrationTrace, FrameCadenceCountsAndReset) {
    PicaSurfaceCalibrationTrace trace(true);
    for (uint64_t frame = 1; frame <= 240; ++frame) {
        trace.BeginFrame(frame);
        const bool selected = frame == 1 || frame % 120 == 0;
        EXPECT_EQ(trace.Sampling(), selected);
        trace.Count("reason=admitted", 100);
        trace.Count("reason=admitted", 70);
        trace.Count("reason=direct-native-response", 12);
        const auto text = trace.FinishFrame("mode=PerPixelPBR");
        if (selected) {
            EXPECT_NE(text.find("frame=" + std::to_string(frame) + " kind=summary draws=3 largest=0 period=120 mode=PerPixelPBR"), std::string::npos);
            EXPECT_NE(text.find("reason=admitted draws=2 elements=170"), std::string::npos);
            EXPECT_NE(text.find("reason=direct-native-response draws=1 elements=12"), std::string::npos);
        } else EXPECT_TRUE(text.empty());
        EXPECT_TRUE(trace.FinishFrame("duplicate").empty());
    }
}

TEST(SurfaceCalibrationTrace, KeepsLargest24WithStableTiesAndWideCounts) {
    PicaSurfaceCalibrationTrace trace(true);
    trace.BeginFrame(120);
    trace.Count("large", 0xffffffffU);
    trace.Count("large", 2);
    for (uint32_t i = 1; i <= 30; ++i) trace.Keep(i, "elements=" + std::to_string(i));
    EXPECT_FALSE(trace.WouldKeep(7));
    trace.Keep(30, "tie=second");
    const auto text = trace.FinishFrame("");
    EXPECT_NE(text.find("largest=24"), std::string::npos);
    EXPECT_NE(text.find("large draws=2 elements=4294967297"), std::string::npos);
    EXPECT_NE(text.find("rank=1 elements=30\n"), std::string::npos);
    EXPECT_NE(text.find("rank=2 tie=second\n"), std::string::npos);
    EXPECT_NE(text.find("rank=24 elements=8\n"), std::string::npos);
    EXPECT_EQ(text.find("elements=7\n"), std::string::npos);
    trace.BeginFrame(240);
    EXPECT_NE(trace.FinishFrame("").find("draws=0 largest=0"), std::string::npos);
}

namespace {
struct NativeFixture {
    PicaVertexShaderHookLayout Hooks;
    std::array<std::array<float, 4>, 96> Uniforms{};
    NativeFixture() {
        auto& s = Hooks.Surface;
        s.ViewNormalTemporary = 14; s.LightingBooleanUniform = 9;
        s.DiffuseUniform = 8; s.AmbientUniform = 9;
        s.TintBooleanUniform = 5; s.TintInputRegister = 2; s.TintScale = {90, 2};
        s.LightEnabledValue = {93, 1}; s.LightDirectionUniforms = {80, 83, 86};
        Uniforms[8] = {1, 1, 1, 1}; Uniforms[9] = {0.5F, 1, 1, 1};
        Uniforms[80] = {0, -1, 0, 1}; Uniforms[82] = {0.8F, 0.8F, 0.8F, 0};
        Uniforms[93][1] = 1;
    }
    auto Read(uint32_t booleans = 512) {
        return ReadPicaNativeSurfaceLighting(Hooks, booleans,
            {reinterpret_cast<const uint8_t*>(Uniforms.data()), sizeof(Uniforms)});
    }
};
}

TEST(NativeSurfaceLighting, ClassifiesEffectiveEnabledDiffuseBeforeProfilePolicy) {
    NativeFixture f;
    auto raw = f.Read();
    ASSERT_TRUE(raw);
    EXPECT_EQ(raw->Classification, PicaNativeSurfaceLightingClass::AmbientOnly);
    EXPECT_EQ(raw->DirectSlotMask, 0U);
    EXPECT_FLOAT_EQ(raw->ResolvedAmbient[0], 0.4F);
    f.Uniforms[81] = {0.6F, 0, 0, 0};
    raw = f.Read();
    ASSERT_TRUE(raw);
    EXPECT_EQ(raw->Classification, PicaNativeSurfaceLightingClass::DirectLit);
    EXPECT_EQ(raw->DirectSlotMask, 1U);
    EXPECT_FLOAT_EQ(raw->Lights[0].DirectionEnabled[1], 1);
    raw = f.Read(0);
    ASSERT_TRUE(raw);
    EXPECT_EQ(raw->Classification, PicaNativeSurfaceLightingClass::Unlit);
    EXPECT_EQ(raw->DirectSlotMask, 0U);
    EXPECT_FLOAT_EQ(raw->ResolvedAmbient[0], 0);
    f.Uniforms[80][3] = 0; // Diffuse in a disabled slot cannot disqualify it.
    EXPECT_EQ(f.Read()->Classification, PicaNativeSurfaceLightingClass::AmbientOnly);
    f.Uniforms[80][3] = 1;
    f.Uniforms[8] = {0, 1, 0, 1}; // Disjoint material/light colour channels.
    EXPECT_EQ(f.Read()->Classification, PicaNativeSurfaceLightingClass::AmbientOnly);
    f.Uniforms[86][3] = 1; f.Uniforms[87][1] = 0.5F;
    EXPECT_EQ(f.Read()->DirectSlotMask, 4U);
}

TEST(NativeSurfaceLighting, RejectsMissingHooksTruncatedAndNonfiniteInputs) {
    NativeFixture f;
    const auto bytes = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(f.Uniforms.data()), sizeof(f.Uniforms));
    EXPECT_FALSE(ReadPicaNativeSurfaceLighting({}, 512, bytes));
    EXPECT_FALSE(ReadPicaNativeSurfaceLighting(f.Hooks, 512, bytes.first(100)));
    f.Uniforms[81][0] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(f.Read());
}
