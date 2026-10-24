// M5 (Stage B) tests of the OOT3D side of the screen-space diffuse GI
// (docs/TRIAEVUM_SCREEN_SPACE_GI.md, "M5 Stage B"): the settings mapped onto
// the pass, the declared VRAM budget of the default work scale, the adapter's
// decoding of the renderer's guides (FP32 twin of the adapter kernel), the
// guide camera and the previous-view transform. Lines starting with DGI_M5B
// are quoted in the document.
#include "fast/extension/diffuse_gi_adapter.h"
#include "fast/extension/screen_space_gi_provider.h"
#include "fast/renderer/diffuse_gi_pass_shader.h"
#include "fast/renderer3ds/pica_geometry_guide.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace Fast::Extension;
namespace DGI = Fast::Renderer::DiffuseGi;

namespace {

using Matrix = std::array<double, 16>; // column-major

Matrix Multiply(const Matrix& a, const Matrix& b) {
    Matrix out{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < 4; ++k) out[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k];
    return out;
}

// Rigid view: rotation about Y by `angle`, then translation.
Matrix RigidView(double angle, std::array<double, 3> t) {
    const double c = std::cos(angle), s = std::sin(angle);
    Matrix m{};
    m[0] = c, m[2] = -s, m[5] = 1.0, m[8] = s, m[10] = c, m[15] = 1.0;
    m[12] = t[0], m[13] = t[1], m[14] = t[2];
    return m;
}

Matrix Inverse(const Matrix& m) {
    double a[4][8];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            a[r][c] = m[c * 4 + r];
            a[r][c + 4] = r == c ? 1.0 : 0.0;
        }
    for (int c = 0; c < 4; ++c) {
        int pivot = c;
        for (int r = c + 1; r < 4; ++r)
            if (std::abs(a[r][c]) > std::abs(a[pivot][c])) pivot = r;
        for (int k = 0; k < 8; ++k) std::swap(a[c][k], a[pivot][k]);
        const double inverse = 1.0 / a[c][c];
        for (int k = 0; k < 8; ++k) a[c][k] *= inverse;
        for (int r = 0; r < 4; ++r)
            if (r != c) {
                const double f = a[r][c];
                for (int k = 0; k < 8; ++k) a[r][k] -= f * a[c][k];
            }
    }
    Matrix out{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) out[c * 4 + r] = a[r][c + 4];
    return out;
}

std::array<float, 16> Narrow(const Matrix& m) {
    std::array<float, 16> out{};
    for (size_t i = 0; i < 16; ++i) out[i] = static_cast<float>(m[i]);
    return out;
}

// An off-centre perspective with w = -Z (the sample perspective's form).
Matrix Projection(double nearPlane, double farPlane) {
    Matrix p{};
    p[0] = 1.2, p[5] = 2.0, p[8] = 0.05, p[9] = -0.03;
    p[10] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    p[11] = -1.0;
    p[14] = -2.0 * farPlane * nearPlane / (farPlane - nearPlane);
    return p;
}

float SrgbToLinear(float c) { return c <= 0.04045F ? c / 12.92F : std::pow((c + 0.055F) / 1.055F, 2.4F); }

} // namespace

TEST(ScreenSpaceGi, SettingsMapOntoTheRuntimePass) {
    GlobalIlluminationSettings gi;
    gi.Mode = GlobalIlluminationMode::ScreenSpace;
    const DGI::PassSettings s = ScreenSpaceGiPassSettings(gi);
    EXPECT_STREQ(DGI::ValidatePassSettings(s), "");
    const DGI::PassSettings m4 = DGI::DefaultPassSettings();
    EXPECT_EQ(s.Gather.Radius, 150.0);
    EXPECT_EQ(s.Gather.Thickness, 30.0);
    EXPECT_EQ(s.Gather.Sections, 1U);
    EXPECT_EQ(s.Gather.Sectors, 64U);
    EXPECT_EQ(s.Gather.CandidatesPerSide, 20U);
    EXPECT_TRUE(s.Temporal.Enabled);
    EXPECT_EQ(s.Filter.Levels, 2U);
    // Everything else is the M4 recommendation.
    DGI::PassSettings expected = m4;
    expected.Gather.Radius = 150.0;
    expected.Gather.Thickness = 30.0;
    expected.Gather.CandidatesPerSide = 20;
    expected.Gather.RadialExponent = 1.5;
    EXPECT_EQ(s.Gather.RadialExponent, 1.5);
    // M7: no surface takes the weaker moving-surface history test.
    EXPECT_TRUE(std::isinf(s.Temporal.MovingFlow) && s.Temporal.MovingFlow > 0.0);
    EXPECT_STREQ(DGI::ValidateTemporalSettings(s.Temporal), "");
    // M8: smooth-shaded receivers in every stage (one switch; the M7 path
    // stays available bit for bit as SetPassShadingNormals(false)).
    EXPECT_TRUE(DGI::PassUsesShadingNormals(s));
    EXPECT_TRUE(s.Gather.ShadingNormals && s.Temporal.ShadingNormals && s.Filter.ShadingNormals &&
                s.Upsample.ShadingNormals);
    EXPECT_EQ(s.Gather.HardEdgeRatio, 0.5);
    EXPECT_EQ(s.Upsample.HardEdgeRatio, 0.5);
    DGI::SetPassShadingNormals(expected, true);
    DGI::PassSettings faceReceivers = s;
    DGI::SetPassShadingNormals(faceReceivers, false);
    EXPECT_FALSE(DGI::PassUsesShadingNormals(faceReceivers));
    EXPECT_NE(DGI::PassTransportKey(faceReceivers, DGI::GeometricNormalSource::RendererFaceNormal),
              DGI::PassTransportKey(s, DGI::GeometricNormalSource::RendererFaceNormal));
    EXPECT_EQ(s.OutputLimit, 65504.0);
    GlobalIlluminationSettings clamped = gi;
    clamped.ScreenSpace.HighlightClamp = 1.5F;
    EXPECT_EQ(ScreenSpaceGiPassSettings(clamped).OutputLimit, 1.5);
    clamped.ScreenSpace.Validation = true;
    EXPECT_EQ(ScreenSpaceGiPassSettings(clamped).OutputLimit, 65504.0);
    EXPECT_EQ(DGI::PassTransportKey(s, DGI::GeometricNormalSource::RendererFaceNormal),
              DGI::PassTransportKey(expected, DGI::GeometricNormalSource::RendererFaceNormal));
    // The extremes of every F1 control run in the pass.
    for (const auto& [sections, candidates, sectors, radius, thickness, filter] :
         std::vector<std::tuple<uint8_t, uint16_t, uint16_t, float, float, uint8_t>>{
             {1, 8, 32, 10.0F, 1.0F, 0}, {4, 32, 128, 1000.0F, 200.0F, 4}, {2, 16, 64, 240.0F, 45.0F, 2}}) {
        gi.ScreenSpace = {sections, candidates, sectors, radius, thickness, false, filter, true};
        const DGI::PassSettings v = ScreenSpaceGiPassSettings(gi);
        EXPECT_STREQ(DGI::ValidatePassSettings(v), "") << unsigned(sections) << " " << candidates << " " << sectors;
        EXPECT_FALSE(v.Temporal.Enabled);
        EXPECT_EQ(v.Filter.Levels, filter);
    }
    EXPECT_EQ(ScreenSpaceGiWorkFactor(GlobalIlluminationResolution::Half), 2U);
    EXPECT_EQ(ScreenSpaceGiWorkFactor(GlobalIlluminationResolution::Quarter), 4U);
    EXPECT_TRUE(GlobalIlluminationReadsFaceNormal({GlobalIlluminationMode::ScreenSpace}));
    EXPECT_FALSE(GlobalIlluminationReadsFaceNormal({GlobalIlluminationMode::BrixelizerGi}));
    EXPECT_FALSE(GlobalIlluminationReadsFaceNormal({}));
}

// Open point 24: the default work scale fits the declared budget at the guide
// extents of 720p, 1080p and 1440p output; Quarter and the full-extent pass
// are reported beside it.
TEST(ScreenSpaceGi, DefaultFitsTheDeclaredVramBudget) {
    for (const auto& [w, h] : std::vector<std::pair<uint32_t, uint32_t>>{{1280, 720}, {1920, 1080}, {2560, 1440}}) {
        GlobalIlluminationSettings gi;
        gi.Mode = GlobalIlluminationMode::ScreenSpace;
        const uint64_t texels = static_cast<uint64_t>(w) * h;
        gi.ScreenSpace.WorkScale = GlobalIlluminationResolution::Half;
        const uint64_t half = ScreenSpaceGiSurfaceBytes(w, h, gi);
        gi.ScreenSpace.WorkScale = GlobalIlluminationResolution::Quarter; // the default
        const uint64_t quarter = ScreenSpaceGiSurfaceBytes(w, h, gi);
        const auto fullPlan = DGI::PlanPassResources(DGI::MakeWorkScaleExtent(w, h, 1),
                                                     ScreenSpaceGiPassSettings(gi), {}, 256U);
        const uint64_t full = fullPlan.PassBytes() + fullPlan.OutputBytes();
        EXPECT_LE(quarter, kScreenSpaceGiDefaultBudgetBytesPerTexel * texels);
        EXPECT_EQ(quarter, ScreenSpaceGiSurfaceBytes(w, h, GlobalIlluminationSettings{}));
        EXPECT_LT(quarter, half);
        EXPECT_LT(half, full);
        // Half: the work image holds a quarter of the texels (rounded up).
        const auto work = DGI::MakeWorkScaleExtent(w, h, 2);
        EXPECT_STREQ(DGI::ValidatePassExtent(work), "");
        EXPECT_EQ(work.BufferWidth, (w + 1U) / 2U);
        EXPECT_EQ(work.BufferHeight, (h + 1U) / 2U);
        std::printf("DGI_M5B vram guide=%ux%u half=%.1f MB (%.1f B/texel) quarter=%.1f MB (%.1f B/texel) "
                    "full=%.1f MB (%.1f B/texel) budget=%u B/texel (%.1f MB)\n",
                    w, h, half / 1.0e6, static_cast<double>(half) / texels, quarter / 1.0e6,
                    static_cast<double>(quarter) / texels, full / 1.0e6, static_cast<double>(full) / texels,
                    kScreenSpaceGiDefaultBudgetBytesPerTexel,
                    static_cast<double>(kScreenSpaceGiDefaultBudgetBytesPerTexel) * texels / 1.0e6);
    }
}

// Open point 25: synthetic guide texels decoded against the declared
// conventions (FP32 twin of oot3d_diffuse_gi_inputs.comp).
TEST(ScreenSpaceGi, AdapterDecodesTheDeclaredGuides) {
    DGI::DepthCamera camera;
    camera.Near = 10.0;
    camera.Far = 1000.0;
    const DGI::PassExtent extent = DGI::MakeWorkScaleExtent(9, 7, 2);
    DiffuseGiAdapterParams p = MakeDiffuseGiAdapterParams(extent, camera);
    EXPECT_EQ(p.Extent, (std::array<uint32_t, 4>{9, 7, 5, 4}));
    EXPECT_EQ(p.Scale, (std::array<uint32_t, 4>{2, 0, 1, 1}));
    // Motion: guide UV -> work UV is W / (f w) per axis.
    EXPECT_FLOAT_EQ(p.Motion[0], 9.0F / 10.0F);
    EXPECT_FLOAT_EQ(p.Motion[1], 7.0F / 8.0F);
    const std::array<float, 3> up{0.0F, 0.6F, 0.8F};
    const uint32_t code = Fast::Renderer3ds::EncodePicaGeometryGuideNormal(up);
    const std::array<uint32_t, 4> geometry{0U, code, 1U, 0x3F800000U};
    // Surface guide: high byte albedo 200, low byte direct radiance 128 (sRGB), alpha validity 1.
    const float packed = static_cast<float>((200U << 8U) | 128U) / 65535.0F;
    const std::array<float, 4> surface{packed, packed, packed, 1.0F};
    DiffuseGiAdapterTexel t = DiffuseGiAdapterTexelF32(p, 0.5F, geometry, surface, {0.01F, -0.02F});
    const float z = 10.0F * 1000.0F / (1000.0F - 0.5F * 990.0F);
    EXPECT_EQ(t.Class, 1U);
    EXPECT_NEAR(t.Radiance[0], SrgbToLinear(128.0F / 255.0F), 1.0e-6F);
    EXPECT_NEAR(t.Normal[1], 0.6F, 1.0e-4F);
    EXPECT_NEAR(t.Normal[2], 0.8F, 1.0e-4F);
    EXPECT_FLOAT_EQ(std::bit_cast<float>(t.Guide[0]), z);
    EXPECT_EQ(t.Guide[1], DGI::EncodeDiffuseGiNormal(t.Normal));
    EXPECT_FLOAT_EQ(t.Motion[0], 0.01F * 0.9F);
    EXPECT_FLOAT_EQ(t.Motion[1], -0.02F * 0.875F);
    // Clear depth is sky; zero validity (HUD, clear, replacing surfaces) is
    // unavailable; an unknown face normal is no receiver. None writes a guide.
    t = DiffuseGiAdapterTexelF32(p, 1.0F, geometry, surface, {});
    EXPECT_EQ(t.Class, 3U);
    EXPECT_EQ(t.Guide, (std::array<uint32_t, 2>{0U, 0U}));
    t = DiffuseGiAdapterTexelF32(p, 0.5F, geometry, {packed, packed, packed, 0.0F}, {});
    EXPECT_EQ(t.Class, 0U);
    EXPECT_EQ(t.Guide, (std::array<uint32_t, 2>{0U, 0U}));
    t = DiffuseGiAdapterTexelF32(p, 0.5F, {0U, 0U, 1U, 0x3F800000U}, surface, {});
    EXPECT_EQ(t.Guide, (std::array<uint32_t, 2>{0U, 0U}));
    t = DiffuseGiAdapterTexelF32(p, 0.5F, {0U, code, 0U, 0x3F800000U}, surface, {}); // kind none
    EXPECT_EQ(t.Normal, (std::array<float, 3>{0.0F, 0.0F, 0.0F}));
    // W-buffer: linear depth between the planes.
    camera.Encoding = DGI::DepthEncoding::LinearWBuffer;
    p = MakeDiffuseGiAdapterParams(extent, camera);
    EXPECT_EQ(p.Scale[2], 3U);
    t = DiffuseGiAdapterTexelF32(p, 0.25F, geometry, surface, {});
    EXPECT_FLOAT_EQ(std::bit_cast<float>(t.Guide[0]), 10.0F + 0.25F * 990.0F);
    // Without a work scale no output guide is written.
    EXPECT_EQ(MakeDiffuseGiAdapterParams(DGI::MakeWorkScaleExtent(9, 7, 1), camera).Scale[3], 0U);
    // The kernel text is the precompiled module's lookup key.
    const std::string source = BuildDiffuseGiAdapterShader();
    EXPECT_EQ(source.rfind("#version 450", 0), 0U);
    EXPECT_NE(source.find("pica_geometry_decode_normal"), std::string::npos);
    EXPECT_NE(source.find("dgi_encode_normal"), std::string::npos);
}

// The guide camera is the sample perspective composed with the storage
// orientation; the jitter moves the image.
TEST(ScreenSpaceGi, GuideCameraIsTheSamplePerspective) {
    const Matrix projection = Projection(10.0, 1000.0);
    PerspectiveViewState view;
    view.NearPlane = 10.0F;
    view.FarPlane = 1000.0F;
    view.Projection = Narrow(projection);
    for (const bool flip : {false, true}) {
        const PicaSurfaceCoordinates coordinates{flip};
        for (const auto& jitter : {std::array<float, 2>{0.0F, 0.0F}, std::array<float, 2>{0.003F, -0.002F}}) {
            const DGI::DepthCamera camera = DiffuseGiGuideCamera(view, coordinates, false, jitter);
            EXPECT_EQ(camera.Encoding, DGI::DepthEncoding::PerspectiveZeroToOne);
            for (const auto& uv : {std::array<float, 2>{0.1F, 0.2F}, std::array<float, 2>{0.7F, 0.9F},
                                   std::array<float, 2>{0.5F, 0.5F}}) {
                // The point this texel shows: the unjittered image at uv - jitter.
                const auto ndc = coordinates.StorageToViewNdc({uv[0] - jitter[0], uv[1] - jitter[1]});
                const double xz = (ndc[0] + projection[8]) / projection[0];
                const double yz = (ndc[1] + projection[9]) / projection[5];
                EXPECT_NEAR(camera.ViewX[0] * uv[0] + camera.ViewX[1] * uv[1] + camera.ViewX[2], xz, 1.0e-6);
                EXPECT_NEAR(camera.ViewY[0] * uv[0] + camera.ViewY[1] * uv[1] + camera.ViewY[2], yz, 1.0e-6);
                // And the projection maps that view point back to the same NDC.
                const double z = 37.0;
                const std::array<double, 4> point{xz * z, yz * z, -z, 1.0};
                std::array<double, 4> clip{};
                for (int r = 0; r < 4; ++r)
                    for (int c = 0; c < 4; ++c) clip[r] += projection[c * 4 + r] * point[c];
                EXPECT_NEAR(clip[0] / clip[3], ndc[0], 1.0e-6);
                EXPECT_NEAR(clip[1] / clip[3], ndc[1], 1.0e-6);
            }
        }
    }
    EXPECT_EQ(DiffuseGiGuideCamera(view, {}, true, {}).Encoding, DGI::DepthEncoding::LinearWBuffer);
}

// V_prev V_cur^-1 from the temporal state's clip matrices.
TEST(ScreenSpaceGi, CurrentToPreviousIsTheRigidViewChange) {
    const Matrix projection = Projection(10.0, 1000.0);
    const Matrix current = RigidView(0.3, {5.0, -2.0, -40.0});
    const Matrix previous = RigidView(0.28, {5.5, -2.0, -39.0});
    TemporalViewState temporal;
    temporal.PreviousWorldToClip = Narrow(Multiply(projection, previous));
    temporal.InverseCurrentWorldToClip = Narrow(Inverse(Multiply(projection, current)));
    const auto transform = DiffuseGiCurrentToPrevious(Narrow(projection), temporal);
    ASSERT_TRUE(transform.has_value());
    const Matrix expected = Multiply(previous, Inverse(current));
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) EXPECT_NEAR(transform->R[3 * r + c], expected[c * 4 + r], 2.0e-4);
        EXPECT_NEAR(transform->T[r], expected[12 + r], 2.0e-2);
    }
    // A non-rigid change (a scaled previous view) is refused.
    Matrix scaled = previous;
    for (int i = 0; i < 12; ++i) scaled[i] *= 1.5;
    temporal.PreviousWorldToClip = Narrow(Multiply(projection, scaled));
    EXPECT_FALSE(DiffuseGiCurrentToPrevious(Narrow(projection), temporal).has_value());
}
