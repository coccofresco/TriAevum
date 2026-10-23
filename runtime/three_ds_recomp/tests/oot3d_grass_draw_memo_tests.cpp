#include "fast/oot3d/grass_draw_memo.h"
#include "fast/oot3d/pica_uniform_layout.h"
#include <gtest/gtest.h>
#include <cstring>
#include <functional>
#include <limits>

namespace {
using namespace Fast::Oot3d;
using namespace Fast::Renderer3ds;
struct DrawFixture : testing::Test {
    std::vector<uint8_t> Vertex = std::vector<uint8_t>(80 + 96 * 16), Fragment = std::vector<uint8_t>(2304);
    std::array<PicaTextureView, 1> Textures{};
    std::array<PicaVertexAttributeView, 1> Attributes{};
    std::array<PicaVertexBindingView, 1> Bindings{};
    PicaDrawView Draw;
    std::array<float, 16> Identity{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    void SetUp() override {
        Draw.GeometryIdentityAvailable = true;
        Draw.GeometryIdentity = 4;
        Draw.GeometryContentVersion = 5;
        Draw.VertexUniformBytes = Vertex;
        Draw.FragmentUniformBytes = Fragment;
        Draw.Textures = Textures;
        Draw.VertexAttributes = Attributes;
        Draw.VertexBindings = Bindings;
        Textures[0].NativeContentHashAvailable = true;
        Textures[0].NativeContentHash = 100;
        Textures[0].Width = Textures[0].Height = 32;
        Draw.LightBinding.WorldToView = Identity;
        std::memcpy(Vertex.data() + 80, Identity.data(), 64);
        std::memcpy(Vertex.data() + 80 + 4 * 16, Identity.data(), 48);
        std::memcpy(Vertex.data() + 80 + 20 * 16, Identity.data(), 48);
    }
    auto Key(bool masks = true) {
        return BuildGrassDrawMemoKey(Draw, 6, 7, true, true, true, masks);
    }
};
} // namespace
TEST_F(DrawFixture, EveryCapturedStructuralInputInvalidatesAndNoPayloadIsRead) {
    const auto original = Key();
    ASSERT_TRUE(original);
    const std::vector<std::function<void(PicaDrawView&)>> edits{
        [](auto& d) { d.GeometryIdentity++; },
        [](auto& d) { d.GeometryContentVersion++; },
        [](auto& d) { d.VertexShaderKey++; },
        [](auto& d) { d.FragmentShaderKey++; },
        [](auto& d) { d.PositionAttributeLocation = 0; },
        [](auto& d) { d.TexCoord0AttributeLocation = 1; },
        [](auto& d) { d.FragmentOperationMode++; },
        [](auto& d) { d.ColorWriteMask = 7; },
        [](auto& d) { d.DepthTestEnabled = true; },
        [](auto& d) { d.DepthWriteEnabled = true; },
        [](auto& d) { d.DepthCompare = PicaCompareFunction::Never; },
        [](auto& d) { d.AlphaTestEnabled = true; },
        [](auto& d) { d.AlphaReference++; },
        [](auto& d) { d.AlphaCompare = PicaCompareFunction::Never; },
        [](auto& d) { d.Blend.Enabled = true; },
        [](auto& d) { d.Blend.SourceRgb = NativeBlendFactor::SourceAlpha; },
        [](auto& d) { d.Indexed = true; },
        [](auto& d) { d.IndicesAre16Bit = true; },
        [](auto& d) { d.BaseVertex++; },
        [](auto& d) { d.VertexCount++; },
        [](auto& d) { d.CommandListAddress++; },
        [](auto& d) { d.CommandListOffsetWords++; },
        [](auto& d) { d.RenderTargetNamespace++; },
        [](auto& d) { d.FramebufferColorPhysicalAddress++; },
        [](auto& d) { d.Composition.NativeActorGeometry = true; },
        [](auto& d) { d.Composition.NativeProjectedShadow = true; },
        [](auto& d) { d.Stencil.Enabled = true; }
    };
    for (size_t i = 0; i < edits.size(); ++i) {
        SCOPED_TRACE(i);
        const auto before = Draw;
        edits[i](Draw);
        EXPECT_NE(original, Key());
        Draw = before;
    }
    const auto texture = Textures[0];
    const std::vector<std::function<void(PicaTextureView&)>> texEdits{
        [](auto& t) {t.NativeContentHash++; }, [](auto& t) {t.Width++; },          [](auto& t) {t.Height++; },
        [](auto& t) {t.NativeFormat++; },      [](auto& t) {t.NativeType++; },     [](auto& t) {t.NativeWrapS++; },
        [](auto& t) {t.NativeWrapT++; },       [](auto& t) {t.MinLinear=true; }, [](auto& t) {t.MagLinear=true; }
    };
    for (auto edit : texEdits) {
        edit(Textures[0]);
        EXPECT_NE(original, Key());
        Textures[0] = texture;
    }
    Attributes[0].ByteOffset++;
    EXPECT_NE(original, Key());
    Attributes[0].ByteOffset--;
    Bindings[0].ByteStride++;
    EXPECT_NE(original, Key());
    Bindings[0].ByteStride--;
    EXPECT_NE(original, BuildGrassDrawMemoKey(Draw, 8, 7, true, true, true, true));
    EXPECT_NE(original, BuildGrassDrawMemoKey(Draw, 6, 8, true, true, true, true));
    EXPECT_NE(original, Key(false));
    Draw.GeometryIdentityAvailable = false;
    EXPECT_FALSE(Key());
    Draw.GeometryIdentityAvailable = true;
    Textures[0].NativeContentHashAvailable = false;
    EXPECT_FALSE(Key());
}
TEST_F(DrawFixture, CameraAndLightingRgbDoNotInvalidateButUvAlphaAndTevDo) {
    const auto original = Key();
    ASSERT_TRUE(original);
    for (size_t offset : { size_t(80), size_t(80 + 4 * 16), size_t(80 + 20 * 16) }) {
        Vertex[offset] ^= 1;
        EXPECT_EQ(original, Key());
        Vertex[offset] ^= 1;
    }
    for (size_t offset : { size_t(0), size_t(80 + 10 * 16), size_t(80 + 11 * 16), size_t(80 + 89 * 16),
                           size_t(80 + 91 * 16), size_t(80 + 92 * 16), size_t(80 + 93 * 16), size_t(80 + 95 * 16) }) {
        Vertex[offset] ^= 1;
        EXPECT_NE(original, Key());
        Vertex[offset] ^= 1;
    }
    for (size_t offset : { size_t(0), kPicaPackedFragmentCombinerBufferOffset, kPicaPackedFragmentTevProgramOffset }) {
        Fragment[offset] ^= 1;
        EXPECT_NE(original, Key());
        Fragment[offset] ^= 1;
    }
    Fragment[500] ^= 1;
    EXPECT_EQ(original, Key());
}
TEST_F(DrawFixture, TypedPrimaryAlphaTracksOnlyContributingScalars) {
    auto& hooks = Draw.TemporalVertexProgram.Hooks;
    hooks.SchemaVersion = kPicaShaderHookSchemaVersion;
    hooks.Semantics = PicaVertexShaderSemantic::SurfaceProgram;
    auto& surface = hooks.Surface;
    surface.ViewNormalTemporary = 0;
    surface.LightingBooleanUniform = 3;
    surface.DiffuseUniform = 30;
    surface.AmbientUniform = 31;
    surface.TintBooleanUniform = 4;
    surface.TintInputRegister = 2;
    surface.TintScale = { 90, 0 };
    surface.LightEnabledValue = { 90, 1 };
    surface.LightDirectionUniforms = { 32, 35, 38 };
    ASSERT_TRUE(hooks.SurfaceProgram());
    const auto key = Key();
    for (size_t offset :
         { size_t(80 + 30 * 16 + 12), size_t(80 + 32 * 16 + 12), size_t(80 + 33 * 16 + 12), size_t(80 + 90 * 16) }) {
        Vertex[offset] ^= 1;
        EXPECT_NE(key, Key());
        Vertex[offset] ^= 1;
    }
    Vertex[80 + 32 * 16] ^= 1;
    Vertex[80 + 30 * 16] ^= 1;
    EXPECT_EQ(key, Key());
}
TEST_F(DrawFixture, RefreshesExactClipOnCameraMoveAndRejectsWorldMotionOrNonfiniteData) {
    GrassSceneMesh mesh;
    mesh.ModelToWorld = Identity;
    mesh.Overlay = std::make_shared<GrassOverlayAlpha>();
    ASSERT_TRUE(RefreshGrassMemoDraw(mesh, Draw));
    EXPECT_EQ(mesh.PicaModelToClip, Identity);
    const auto original = Key();
    float move = -25;
    std::memcpy(Vertex.data() + 80 + 4 * 16 + 12, &move, 4);
    auto camera = Identity;
    camera[12] = move;
    Draw.LightBinding.WorldToView = camera;
    EXPECT_EQ(original, Key());
    ASSERT_TRUE(RefreshGrassMemoDraw(mesh, Draw));
    EXPECT_EQ(mesh.PicaModelToClip[12], move);
    EXPECT_EQ(mesh.ModelToWorld, Identity);
    float modelMove = 2;
    std::memcpy(Vertex.data() + 80 + 20 * 16 + 12, &modelMove, 4);
    EXPECT_FALSE(RefreshGrassMemoDraw(mesh, Draw));
    modelMove = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(Vertex.data() + 80 + 20 * 16 + 12, &modelMove, 4);
    EXPECT_FALSE(RefreshGrassMemoDraw(mesh, Draw));
}
TEST_F(DrawFixture, RemembersNeitherAndClearsAtRendererReset) {
    GrassDrawMemo memo;
    const auto key = *Key();
    memo.Store(key, std::nullopt, 1);
    auto* entry = memo.Find(key, 2);
    ASSERT_TRUE(entry);
    EXPECT_FALSE(entry->Mesh);
    EXPECT_EQ(entry->Frame, 2);
    auto changed = key;
    changed.Content++;
    EXPECT_FALSE(memo.Find(changed, 3));
    memo.Clear();
    EXPECT_FALSE(memo.Find(key, 4));
}
TEST(GrassDrawMemoRules, EveryF1SourceAndMaskFieldChangesTheEpoch) {
    InteractiveGrassSettings settings;
    settings.Rules.push_back({});
    settings.Masks.push_back({});
    const auto version = GrassDrawRulesVersion(settings);
    const std::vector<std::function<void(InteractiveGrassSettings&)>> edits{
        [](auto& s) { s.Rules[0].Target.Rgba8Hash++; },
        [](auto& s) { s.Rules[0].Target.Width++; },
        [](auto& s) { s.Rules[0].Target.Height++; },
        [](auto& s) { s.Rules[0].Target.MapperSlotMask = 1; },
        [](auto& s) { s.Rules[0].Channel = GrassSampleChannel::Red; },
        [](auto& s) { s.Rules[0].InputBlack = .2F; },
        [](auto& s) { s.Rules[0].InputWhite = .8F; },
        [](auto& s) { s.Rules[0].ResponseExponent = 2; },
        [](auto& s) { s.Rules[0].OutputBlack = .2F; },
        [](auto& s) { s.Rules[0].OutputWhite = .8F; },
        [](auto& s) { s.Rules[0].Invert = true; },
        [](auto& s) { s.Rules[0].MaximumSlopeDegrees = 12; },
        [](auto& s) { s.Rules[0].NormalOffset = 12; },
        [](auto& s) { s.Rules[0].Wrap = GrassWrapOverride::Clamp; },
        [](auto& s) { s.Rules[0].SampleScale = .5F; },
        [](auto& s) { s.Rules[0].SampleOffset = .1F; },
        [](auto& s) { s.Rules[0].ActiveRects.push_back({ 0, 0, .5F, .5F }); },
        [](auto& s) { s.Masks[0].Mode = GrassMaskMode::Off; },
        [](auto& s) { s.Masks[0].Strength = .5F; },
        [](auto& s) { s.Masks[0].Coverage.Channel = GrassSampleChannel::Red; },
        [](auto& s) { s.Masks[0].Coverage.Target.Rgba8Hash++; },
        [](auto& s) { s.Masks[0].Coverage.InputBlack = .2F; },
        [](auto& s) { s.Masks[0].Coverage.ActiveRects.push_back({ 0, 0, .5F, .5F }); },
        [](auto& s) { s.Generation.Seed++; }
    };
    for (size_t i = 0; i < edits.size(); ++i) {
        SCOPED_TRACE(i);
        auto edited = settings;
        edits[i](edited);
        EXPECT_NE(version, GrassDrawRulesVersion(edited));
    }
}

TEST_F(DrawFixture, BaseLevelIdentityIgnoresMipOnlyChangesAndTracksNativeBaseReplacement) {
    Textures[0].NativeBaseLevelContentHashAvailable = true;
    Textures[0].NativeBaseLevelContentHash = 44;
    const auto key = Key();
    Textures[0].NativeContentHash++;
    EXPECT_EQ(key, Key());
    Textures[0].NativeBaseLevelContentHash++;
    EXPECT_NE(key, Key());
    PicaTextureCopyReference copy;
    Textures[0].CopyReferences = std::span(&copy, 1);
    EXPECT_FALSE(Key());
}

TEST_F(DrawFixture, WorldMotionThresholdSurvivesCameraRotationAndPublishesSubthresholdMovement) {
    constexpr float c = .7071067811865475F;
    std::array<float, 16> view{ c, 0, c, 0, 0, 1, 0, 0, -c, 0, c, 0, 0, 0, 0, 1 }, camera{};
    for (size_t i = 0; i < 4; ++i)
        for (size_t j = 0; j < 4; ++j)
            camera[j * 4 + i] = view[i * 4 + j];
    std::memcpy(Vertex.data() + 80 + 4 * 16, view.data(), 48);
    Draw.LightBinding.WorldToView = camera;
    GrassSceneMesh mesh;
    mesh.ModelToWorld = Identity;
    mesh.Overlay = std::make_shared<GrassOverlayAlpha>();
    float movement = .06F;
    std::memcpy(Vertex.data() + 80 + 20 * 16 + 12, &movement, 4);
    EXPECT_FALSE(RefreshGrassMemoDraw(mesh, Draw)); // each view component is below .05
    movement = .04F;
    std::memcpy(Vertex.data() + 80 + 20 * 16 + 12, &movement, 4);
    ASSERT_TRUE(RefreshGrassMemoDraw(mesh, Draw));
    EXPECT_NEAR(mesh.ModelToWorld[3], movement, 1e-6F);
    GrassDrawMemo memo;
    const auto* inverse = memo.CameraInverse(Draw);
    ASSERT_TRUE(inverse);
    EXPECT_EQ(memo.CameraInverse(Draw), inverse);
}
