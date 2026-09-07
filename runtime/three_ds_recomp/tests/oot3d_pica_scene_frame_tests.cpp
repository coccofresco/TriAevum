#include "fast/oot3d/native_scene_view.h"
#include "fast/oot3d/pica_directional_shadow_semantics.h"
#include "fast/oot3d/pica_scene_frame.h"
#include "fast/oot3d/pica_scene_publication_adapter.h"
#include "fast/oot3d/pica_uniform_layout.h"
#include "fast/oot3d/title_render_backend.h"
#include "fast/renderer3ds/pica_shader_hooks.h"
#include "oot3d/renderer/pica_render_backend.h"
#include "oot3d/renderer/azahar_texture_pack.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <initializer_list>
#include <string>
#include <type_traits>
#include <vector>

namespace {

TEST(Oot3dTitleRenderBackend,
     KeepsTitleHooksOutsideTheSharedPicaBackendContract) {
    Fast::Oot3d::TitleRenderBackend backend;
    std::string error;
    EXPECT_TRUE(backend.PrepareOverlay(&error));
    EXPECT_FALSE(backend.PublishSceneView({}));
    EXPECT_FALSE(backend.ApplyPresentationSettings({}, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(backend.ResetTitleState());
}

static_assert(std::is_same_v<
              Fast::Oot3d::PerspectiveViewState,
              Fast::Renderer3ds::PicaPerspectiveCameraState>);
static_assert(std::is_same_v<
              Fast::Oot3d::NativeFrameTemporalSample,
              Fast::Renderer3ds::PicaFrameTemporalSample>);
static_assert(std::is_same_v<
              Fast::Oot3d::NativeViewFamily,
              Fast::Renderer3ds::PicaViewFamily>);
static_assert(std::is_same_v<
              Fast::Oot3d::PicaSceneResolvedRasterState,
              Fast::Renderer3ds::PicaSceneResolvedRasterState>);
static_assert(std::is_same_v<
              Fast::Oot3d::PicaSceneRenderTargetState,
              Fast::Renderer3ds::PicaSceneRenderTargetState>);
static_assert(std::is_same_v<
              Fast::Oot3d::PicaSceneResolvedMaterialState,
              Fast::Renderer3ds::PicaSceneResolvedMaterialState>);
static_assert(std::is_same_v<
              Oot3d::Renderer::PicaFragmentFeatureView,
              Fast::Renderer3ds::PicaFragmentFeatureView>);
static_assert(std::is_same_v<
              Oot3d::Renderer::PicaShaderSourceIdentity,
              Fast::Renderer3ds::PicaShaderSourceIdentity>);
static_assert(std::is_same_v<
              Oot3d::Renderer::PicaCompositionDomain,
              Fast::Renderer3ds::PicaCompositionDomain>);
static_assert(std::is_same_v<
              Oot3d::Renderer::PicaCompositionSequenceView,
              Fast::Renderer3ds::PicaCompositionSequenceView>);
static_assert(std::is_same_v<
              Oot3d::Renderer::PicaDrawView,
              Fast::Renderer3ds::PicaDrawView>);
static_assert(std::is_same_v<
              Oot3d::Renderer::PicaDisplayTransferView,
              Fast::Renderer3ds::PicaDisplayTransferView>);
static_assert(std::is_same_v<
              Oot3d::Renderer::PicaPresentationStateSnapshot,
              Fast::Renderer3ds::PicaPresentationStateSnapshot>);
static_assert(std::is_same_v<
              Oot3d::Renderer::PicaRenderBackend,
              Fast::Renderer3ds::PicaRenderBackend>);
static_assert(std::is_same_v<
              Oot3d::Renderer::PicaShaderHookLayout,
              Fast::Renderer3ds::PicaShaderHookLayout>);
static_assert(std::is_same_v<
              Oot3d::Renderer::PicaVertexShaderHookLayout,
              Fast::Renderer3ds::PicaVertexShaderHookLayout>);
static_assert(std::is_same_v<
              Oot3d::Renderer::PicaTemporalVertexProgramView,
              Fast::Renderer3ds::PicaTemporalVertexProgramView>);
static_assert(std::is_same_v<
              Fast::Oot3d::PicaNriPackedVertexBinding,
              Fast::Renderer3ds::PicaNriPackedVertexBinding>);
static_assert(std::is_same_v<
              Fast::Oot3d::PicaNativeLightingState,
              Fast::Renderer3ds::PicaNativeLightingState>);
static_assert(std::is_same_v<
              Fast::Oot3d::PicaSceneDrawRecord,
              Fast::Renderer3ds::PicaResolvedDrawRecord>);

Oot3d::Renderer::PicaVertexShaderHookLayout BuildTransformHooks(size_t sourceSize) {
    using namespace Oot3d::Renderer;
    PicaVertexShaderHookLayout hooks;
    hooks.SchemaVersion = kPicaShaderHookSchemaVersion;
    hooks.SourceSize = sourceSize;
    hooks.Offsets.fill(0U);
    hooks.Semantics = PicaVertexShaderSemantic::TransformProgram |
                      PicaVertexShaderSemantic::SkeletonProgram |
                      PicaVertexShaderSemantic::ViewPositionOutput;
    hooks.Transform = { PicaVertexTransformOperation::ModelViewProjection3x4, 0U, 4U, 4U, 3U, 20U, 3U, 0U, 1U };
    hooks.Skeleton = { PicaVertexSkeletonOperation::MatrixPalette3x4, 2U, 3U, 20U, 3U, 6U, 7U, 4U };
    return hooks;
}

Fast::Oot3d::PicaSceneDrawRecordDesc BuildDraw(
    std::string_view shaderSource,
    std::span<const Fast::Oot3d::PicaNriPackedVertexBinding> layoutBindings,
    std::span<const Fast::Oot3d::PicaNriPackedVertexAttribute> layoutAttributes,
    std::span<const Fast::Oot3d::PicaSceneVertexBufferBinding> vertexBindings,
    std::span<const uint8_t> uniforms = {},
    std::span<const uint8_t> fragmentUniforms = {},
    Oot3d::Renderer::PicaFragmentFeatureView fragmentFeatures = {}) {
    Fast::Oot3d::PicaSceneDrawRecordDesc draw;
    draw.SubmissionId = 17U;
    draw.CommandListAddress = 0x14004000U;
    draw.CommandListOffsetWords = 12U;
    draw.CompositionDomain =
        Oot3d::Renderer::PicaCompositionDomain::Scene;
    draw.Composition = {
        Oot3d::Renderer::PicaCompositionLayer::OpaqueWorld,
        Oot3d::Renderer::PicaCompositionProvenance::NativeCmbDrawPass,
        0x0030F4D0U,
        0U,
    };
    draw.Raster = {
        .SchemaVersion =
            Fast::Oot3d::kPicaSceneResolvedRasterStateSchemaVersion,
        .NativeViewportX = 0.0F,
        .NativeViewportY = 0.0F,
        .NativeViewportWidth = 400.0F,
        .NativeViewportHeight = 240.0F,
        .ResolvedViewportX = 0.0F,
        .ResolvedViewportY = 0.0F,
        .ResolvedViewportWidth = 1200.0F,
        .ResolvedViewportHeight = 720.0F,
        .DepthRange = 1.0F,
        .NearPlane = 0.1F,
        .ScissorMode = 3U,
        .NativeScissorX1 = 8U,
        .NativeScissorY1 = 4U,
        .NativeScissorX2 = 391U,
        .NativeScissorY2 = 235U,
        .ResolvedScissorX = 24,
        .ResolvedScissorY = 12,
        .ResolvedScissorWidth = 1152U,
        .ResolvedScissorHeight = 696U,
        .WBuffering = true,
        .FramebufferFlipped = true,
    };
    draw.RenderTarget = {
        .SchemaVersion =
            Fast::Oot3d::kPicaSceneRenderTargetStateSchemaVersion,
        .RenderTargetNamespace = 23U,
        .ColorPhysicalAddress = 0x18100000U,
        .DepthPhysicalAddress = 0x18200000U,
        .NativeWidth = 400U,
        .NativeHeight = 240U,
        .NativeColorFormat = 0U,
        .NativeDepthFormat = 3U,
        .SampleCount = 1U,
        .ResolvedColor = {
            .NativeHandle = 0x5000U,
            .ResourceGeneration = 5U,
            .Width = 1200U,
            .Height = 720U,
            .Format = 37U,
            .Sampleable = true,
        },
        .ResolvedDepth = {
            .NativeHandle = 0x6000U,
            .ResourceGeneration = 6U,
            .Width = 1200U,
            .Height = 720U,
            .Format = 126U,
            .Sampleable = true,
        },
    };
    draw.CanonicalDescriptorSchemaVersion = 2U;
    draw.CanonicalVertexProgramId = 31U;
    draw.CanonicalFragmentProgramId = 37U;
    draw.CanonicalRasterStateId = 41U;
    draw.CanonicalPipelineId = 43U;
    draw.CanonicalDynamicStateId = 47U;
    draw.CanonicalFullRegisterStateId = 53U;
    draw.VertexShaderKey = 59U;
    draw.VertexShaderSource = shaderSource;
    draw.FragmentShaderKey = 61U;
    draw.FragmentShaderSource = "fragment shader";
    draw.EffectiveVertexShaderKey = 59U;
    draw.EffectiveVertexShaderSource = shaderSource;
    draw.EffectiveFragmentShaderKey = 61U;
    draw.EffectiveFragmentShaderSource = "fragment shader";
    draw.GeometryIdentity = 67U;
    draw.GeometryContentVersion = 71U;
    draw.GeometryIdentityAvailable = true;
    draw.VertexLayoutBindings = layoutBindings;
    draw.VertexLayoutAttributes = layoutAttributes;
    draw.Material.SchemaVersion =
        Fast::Oot3d::kPicaSceneResolvedMaterialStateSchemaVersion;
    draw.Material.Topology =
        Fast::Oot3d::PicaSceneTopology::TriangleList;
    draw.Material.CullMode = Fast::Oot3d::PicaSceneCullMode::Back;
    draw.Material.FrontFace =
        Fast::Oot3d::PicaSceneFrontFace::CounterClockwise;
    draw.UniformBuffer = { 0x1000U, 4096U };
    draw.VertexUniformOffset = 256U;
    draw.VertexUniformSize = 1024U;
    draw.FragmentUniformOffset = 1280U;
    draw.FragmentUniformSize = 512U;
    draw.GeometryBuffer = { 0x2000U, 8192U };
    draw.VertexBindings = vertexBindings;
    draw.Indexed = true;
    draw.IndexOffset = 4096U;
    draw.VertexOrIndexCount = 24U;
    draw.BaseVertex = 3;
    draw.Material.FragmentOperationMode = 0U;
    draw.Material.ColorWriteMask = 0xfU;
    draw.Material.DepthTestEnabled = true;
    draw.Material.DepthWriteEnabled = true;
    draw.PerspectiveProjection = true;
    draw.Material.FragmentFeatures = fragmentFeatures;
    draw.PackedVertexUniforms = uniforms;
    draw.PackedFragmentUniforms = fragmentUniforms;
    return draw;
}

TEST(Oot3dPicaSceneFrame, RecordsGpuResourcesWithoutGeometryPayloadCopies) {
    using namespace Fast::Oot3d;
    std::string shader = "#version 450\nvoid main(){}\n";
    const std::array<PicaNriPackedVertexBinding, 1> layoutBindings{ {
        { 0U, 16U, false },
    } };
    const std::array<PicaNriPackedVertexAttribute, 1> layoutAttributes{ {
        { 0U, 0U, 0U, PicaNriVertexScalar::Float, 4U, 0U },
    } };
    std::array<PicaSceneVertexBufferBinding, 1> vertexBindings{ {
        { 0U, 0U, 4096U, 16U },
    } };

    PicaSceneFrame frame;
    frame.BeginFrame(1U, 9U);
    const NativeFrameCompositionPolicy x3Policy{ NativeVisualInterpolationMode::Fixed3x, 30U, 90U, 3U, true };
    const auto temporalSample =
        BuildNativeFrameTemporalSample(x3Policy, NativeFrameTemporalSampleKind::Transition, 40U, 41U, 3U, 1.0F / 3.0F);
    ASSERT_TRUE(frame.SetTemporalSample(temporalSample));
    EXPECT_EQ(frame.Record(BuildDraw(shader, layoutBindings, layoutAttributes,
                                     vertexBindings)),
              PicaSceneRecordStatus::Recorded);
    shader[0] = '!';
    vertexBindings[0].Offset = 128U;

    ASSERT_EQ(frame.Draws().size(), 1U);
    const auto& draw = frame.Draws().front();
    ASSERT_NE(draw.VertexShader, nullptr);
    ASSERT_NE(draw.VertexLayout, nullptr);
    EXPECT_EQ(draw.VertexShader->Source, "#version 450\nvoid main(){}\n");
    EXPECT_EQ(draw.GeometryBuffer.NativeHandle, 0x2000U);
    EXPECT_EQ(draw.GeometryIdentity, 67U);
    EXPECT_EQ(draw.CommandListAddress, 0x14004000U);
    EXPECT_EQ(draw.CompositionDomain,
              Oot3d::Renderer::PicaCompositionDomain::Scene);
    EXPECT_EQ(draw.Composition.Layer,
              Oot3d::Renderer::PicaCompositionLayer::OpaqueWorld);
    EXPECT_EQ(draw.Composition.Provenance,
              Oot3d::Renderer::PicaCompositionProvenance::NativeCmbDrawPass);
    EXPECT_EQ(draw.Composition.SourcePc, 0x0030F4D0U);
    ASSERT_TRUE(draw.Raster.Available());
    EXPECT_FLOAT_EQ(draw.Raster.NativeViewportWidth, 400.0F);
    EXPECT_FLOAT_EQ(draw.Raster.ResolvedViewportWidth, 1200.0F);
    EXPECT_EQ(draw.Raster.ScissorMode, 3U);
    EXPECT_EQ(draw.Raster.ResolvedScissorWidth, 1152U);
    EXPECT_TRUE(draw.Raster.FramebufferFlipped);
    ASSERT_TRUE(draw.RenderTarget.Available());
    ASSERT_TRUE(draw.RenderTarget.GpuResourcesAvailable());
    EXPECT_EQ(draw.RenderTarget.RenderTargetNamespace, 23U);
    EXPECT_EQ(draw.RenderTarget.DepthPhysicalAddress, 0x18200000U);
    EXPECT_EQ(draw.RenderTarget.ResolvedColor.NativeHandle, 0x5000U);
    EXPECT_EQ(draw.RenderTarget.ResolvedColor.ResourceGeneration, 5U);
    EXPECT_EQ(frame.Stats().SceneDomainDrawCount, 1U);
    EXPECT_EQ(frame.Stats().UiDomainDrawCount, 0U);
    EXPECT_EQ(frame.Stats().UnknownDomainDrawCount, 0U);
    EXPECT_EQ(frame.Stats().OpaqueWorldLayerDrawCount, 1U);
    EXPECT_EQ(frame.Stats().TransparentWorldLayerDrawCount, 0U);
    EXPECT_EQ(frame.Stats().UnknownLayerDrawCount, 0U);
    EXPECT_EQ(frame.Stats().NativeCmbPassProvenanceDrawCount, 1U);
    EXPECT_EQ(frame.Stats().UnknownProvenanceDrawCount, 0U);
    EXPECT_EQ(frame.Stats().ResolvedRasterStateCount, 1U);
    EXPECT_EQ(frame.Stats().ResolvedRenderTargetStateCount, 1U);
    EXPECT_EQ(frame.Stats().ResolvedRenderTargetGpuResourceCount, 1U);
    EXPECT_EQ(frame.Stats().TemporalSampleCount, 1U);
    EXPECT_EQ(frame.Stats().SyntheticTemporalSampleCount, 1U);
    EXPECT_EQ(frame.Stats().TemporalSampleMultiplier, 3U);
    EXPECT_EQ(frame.Stats().TemporalSampleOrdinal, 1U);
    const auto recordedBindings = frame.VertexBindings(draw);
    ASSERT_EQ(recordedBindings.size(), 1U);
    EXPECT_EQ(recordedBindings[0].Offset, 0U);
    EXPECT_EQ(recordedBindings[0].Size, 4096U);
    const auto resolvedStream = frame.ResolvedDrawStream();
    ASSERT_TRUE(resolvedStream.Available());
    EXPECT_EQ(resolvedStream.Draws.data(), frame.Draws().data());
    EXPECT_EQ(resolvedStream.BindingsFor(resolvedStream.Draws.front()).data(),
              recordedBindings.data());
}

TEST(Oot3dPicaSceneFrame,
     InternsCanonicalVertexHooksAndRejectsConflictingMetadata) {
    using namespace Fast::Oot3d;
    constexpr std::string_view shader = "#version 450\nvoid main(){}\n";
    const std::array<PicaNriPackedVertexBinding, 1> layoutBindings{{
        {0U, 16U, false},
    }};
    const std::array<PicaNriPackedVertexAttribute, 1> layoutAttributes{{
        {0U, 0U, 0U, PicaNriVertexScalar::Float, 4U, 0U},
    }};
    const std::array<PicaSceneVertexBufferBinding, 1> vertexBindings{{
        {0U, 0U, 4096U, 16U},
    }};

    PicaSceneFrame frame;
    frame.BeginFrame(0U, 91U);
    auto draw = BuildDraw(shader, layoutBindings, layoutAttributes,
                          vertexBindings);
    draw.VertexShaderHooks = BuildTransformHooks(shader.size());
    ASSERT_EQ(frame.Record(draw), PicaSceneRecordStatus::Recorded);
    ASSERT_TRUE(frame.Draws().front().VertexShader->VertexHooksAvailable());
    EXPECT_EQ(frame.Draws().front().VertexShader->VertexHooks,
              draw.VertexShaderHooks);

    ++draw.VertexShaderHooks.Transform.ModelFirstUniform;
    EXPECT_EQ(frame.Record(draw),
              PicaSceneRecordStatus::ShaderIdentityConflict);
}

TEST(Oot3dPicaSceneFrame, ValidatesPreidentifiedSourcesAndLegacyCallers) {
    using namespace Fast::Oot3d;
    constexpr std::string_view source = "#version 450\nvoid main(){}\n";
    const std::array<PicaNriPackedVertexBinding, 1> bindings{{{0U, 16U, false}}};
    const std::array<PicaNriPackedVertexAttribute, 1> attributes{{
        {0U, 0U, 0U, PicaNriVertexScalar::Float, 4U, 0U}}};
    const std::array<PicaSceneVertexBufferBinding, 1> vertices{{{0U, 0U, 4096U, 16U}}};
    PicaSceneFrame frame;
    frame.BeginFrame(0U, 1U);
    auto draw = BuildDraw(source, bindings, attributes, vertices);
    draw.VertexSourceIdentity = Fast::Renderer3ds::IdentifyPicaShaderSource(source);
    ++draw.VertexSourceIdentity.SecondaryHash;
    EXPECT_EQ(frame.Record(draw), PicaSceneRecordStatus::ShaderIdentityConflict);
    draw.VertexSourceIdentity = Fast::Renderer3ds::IdentifyPicaShaderSource(source);
    ASSERT_EQ(frame.Record(draw), PicaSceneRecordStatus::Recorded);
    const auto* shader = frame.Draws().back().VertexShader;
    frame.BeginFrame(1U, 2U);
    ASSERT_EQ(frame.Record(draw), PicaSceneRecordStatus::Recorded);
    EXPECT_EQ(frame.Draws().back().VertexShader, shader);
    ++draw.VertexSourceIdentity.Size;
    EXPECT_EQ(frame.Record(draw), PicaSceneRecordStatus::ShaderIdentityConflict);
    draw.VertexShaderSource = "#version 450\nvoid main(){;}\n";
    draw.VertexSourceIdentity = Fast::Renderer3ds::IdentifyPicaShaderSource(draw.VertexShaderSource);
    EXPECT_EQ(frame.Record(draw), PicaSceneRecordStatus::ShaderIdentityConflict);
    draw.VertexSourceIdentity = {};
    EXPECT_EQ(frame.Record(draw), PicaSceneRecordStatus::ShaderIdentityConflict);
    draw.VertexShaderSource = source;
    EXPECT_EQ(frame.Record(draw), PicaSceneRecordStatus::Recorded);
}

TEST(Oot3dPicaSceneFrame, UniformVersionsUsePortableContentHashWithoutChangingTexturePackIdentity) {
    using namespace Fast::Oot3d;
    EXPECT_EQ(HashPicaUniformBytes({}), 0U);
    std::array<uint8_t, 4097> storage{};
    for (size_t i = 0; i < storage.size(); ++i) storage[i] = static_cast<uint8_t>(i * 17U);
    for (const size_t length : {1U, 3U, 4U, 8U, 16U, 17U, 32U, 33U, 64U, 65U, 4096U}) {
        const auto bytes = std::span<const uint8_t>(storage).subspan(1U, length);
        const uint64_t version = HashPicaUniformBytes(bytes);
        EXPECT_EQ(version, ::Oot3d::Renderer::AzaharCityHash64(bytes));
        storage[length] ^= 1U;
        EXPECT_NE(version, HashPicaUniformBytes(bytes));
        storage[length] ^= 1U;
        EXPECT_EQ(version, HashPicaUniformBytes(bytes));
    }
}

TEST(Oot3dPicaSceneFrame, InternsShaderAndLayoutAcrossFrames) {
    using namespace Fast::Oot3d;
    constexpr std::string_view shader = "#version 450\nvoid main(){}\n";
    const std::array<PicaNriPackedVertexBinding, 1> layoutBindings{ {
        { 2U, 16U, false },
    } };
    const std::array<PicaNriPackedVertexAttribute, 1> layoutAttributes{ {
        { 3U, 2U, 0U, PicaNriVertexScalar::Float, 4U, 0U },
    } };
    const std::array<PicaSceneVertexBufferBinding, 1> vertexBindings{ {
        { 2U, 0U, 4096U, 16U },
    } };

    PicaSceneFrame frame;
    frame.BeginFrame(0U, 10U);
    ASSERT_EQ(frame.Record(BuildDraw(shader, layoutBindings, layoutAttributes,
                                     vertexBindings)),
              PicaSceneRecordStatus::Recorded);
    ASSERT_EQ(frame.Record(BuildDraw(shader, layoutBindings, layoutAttributes,
                                     vertexBindings)),
              PicaSceneRecordStatus::Recorded);
    ASSERT_EQ(frame.Draws().size(), 2U);
    const auto* shaderIdentity = frame.Draws()[0].VertexShader;
    const auto* layoutIdentity = frame.Draws()[0].VertexLayout;
    EXPECT_EQ(frame.Draws()[1].VertexShader, shaderIdentity);
    EXPECT_EQ(frame.Draws()[1].VertexLayout, layoutIdentity);
    EXPECT_EQ(frame.Stats().ShaderCatalogEntries, 2U);
    EXPECT_EQ(frame.Stats().VertexLayoutCatalogEntries, 1U);

    frame.BeginFrame(1U, 11U);
    ASSERT_TRUE(frame.Draws().empty());
    ASSERT_EQ(frame.Record(BuildDraw(shader, layoutBindings, layoutAttributes,
                                     vertexBindings)),
              PicaSceneRecordStatus::Recorded);
    EXPECT_EQ(frame.Draws()[0].VertexShader, shaderIdentity);
    EXPECT_EQ(frame.Draws()[0].VertexLayout, layoutIdentity);
}

TEST(Oot3dPicaSceneFrame,
     DecodesNativeLightOnlyForDynamicPerspectiveShadowCasters) {
    using namespace Fast::Oot3d;
    constexpr size_t floatOffset = 80U;
    constexpr size_t vec4Bytes = 4U * sizeof(float);
    std::vector<uint8_t> uniforms(floatOffset + 96U * vec4Bytes);
    const uint32_t enabledLights = (1U << 9U) | (1U << 2U);
    std::memcpy(uniforms.data(), &enabledLights, sizeof(enabledLights));
    const std::array<float, 4> direction{ 0.0F, -1.0F, 0.0F, 0.0F };
    const std::array<float, 4> diffuse{ 1.0F, 0.5F, 0.25F, 1.0F };
    const std::array<float, 4> ambient{ 0.2F, 0.1F, 0.05F, 1.0F };
    const std::array<std::array<float, 4>, 4> identity{{
        {1.0F, 0.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, 0.0F, 1.0F},
    }};
    for (size_t row = 0U; row < identity.size(); ++row) {
        std::memcpy(uniforms.data() + floatOffset + row * vec4Bytes,
                    identity[row].data(), vec4Bytes);
    }
    for (size_t row = 0U; row < 3U; ++row) {
        std::memcpy(uniforms.data() + floatOffset +
                        (4U + row) * vec4Bytes,
                    identity[row].data(), vec4Bytes);
        std::memcpy(uniforms.data() + floatOffset +
                        (20U + row) * vec4Bytes,
                    identity[row].data(), vec4Bytes);
    }
    std::memcpy(uniforms.data() + floatOffset + 0x50U * vec4Bytes,
                direction.data(), vec4Bytes);
    std::memcpy(uniforms.data() + floatOffset + 0x51U * vec4Bytes,
                diffuse.data(), vec4Bytes);
    std::memcpy(uniforms.data() + floatOffset + 0x52U * vec4Bytes,
                ambient.data(), vec4Bytes);
    const std::array<PicaNriPackedVertexBinding, 1> layoutBindings{ {
        { 0U, 16U, false },
    } };
    const std::array<PicaNriPackedVertexAttribute, 1> layoutAttributes{ {
        { 0U, 0U, 0U, PicaNriVertexScalar::Float, 4U, 0U },
    } };
    const std::array<PicaSceneVertexBufferBinding, 1> vertexBindings{ {
        { 0U, 0U, 4096U, 16U },
    } };

    PicaSceneFrame frame;
    frame.BeginFrame(0U, 12U);
    auto draw = BuildDraw("shader", layoutBindings, layoutAttributes,
                          vertexBindings, uniforms);
    draw.DirectionalShadowCaster = true;
    draw.VertexShaderHooks = BuildTransformHooks(draw.VertexShaderSource.size());
    ASSERT_EQ(frame.Record(draw), PicaSceneRecordStatus::Recorded);
    ASSERT_EQ(frame.Draws().size(), 1U);
    EXPECT_TRUE(frame.DirectionalShadowCaster(0U));
    EXPECT_TRUE(frame.Draws()[0].NativeTransform.CurrentUsesSkeleton);
    EXPECT_TRUE(frame.Draws()[0].NativeLighting.Enabled);
    EXPECT_EQ(frame.Draws()[0].NativeLighting.ActiveLightCount, 1U);
    EXPECT_EQ(frame.Stats().DirectionalShadowCasterCount, 1U);
    EXPECT_EQ(frame.Stats().DirectionalShadowNativeLightCasterCount, 1U);

    draw.PerspectiveProjection = false;
    ASSERT_EQ(frame.Record(draw), PicaSceneRecordStatus::Recorded);
    EXPECT_FALSE(frame.DirectionalShadowCaster(1U));
    EXPECT_EQ(frame.Stats().DirectionalShadowCasterCount, 1U);
}

TEST(Oot3dPicaSceneFrame, RetainsCanonicalMaterialAndResolvedTextureBindings) {
    using namespace Fast::Oot3d;
    const std::array<PicaNriPackedVertexBinding, 1> layoutBindings{ {
        { 0U, 16U, false },
    } };
    const std::array<PicaNriPackedVertexAttribute, 1> layoutAttributes{ {
        { 0U, 0U, 0U, PicaNriVertexScalar::Float, 4U, 0U },
    } };
    const std::array<PicaSceneVertexBufferBinding, 1> vertexBindings{ {
        { 0U, 0U, 4096U, 16U },
    } };
    std::array<PicaSceneTextureBinding, 1> textures{ {
        {
            .NativeImageHandle = 0x3000U,
            .NativeContentHash = 73U,
            .ReplacementContentHash = 79U,
            .PhysicalAddress = 0x18000000U,
            .ImageWidth = 256U,
            .ImageHeight = 128U,
            .MipLevels = 4U,
            .SourceWidth = 64U,
            .SourceHeight = 32U,
            .LodBiasRaw = -16,
            .Slot = 1U,
            .NativeFormat = 3U,
            .NativeType = 0U,
            .NativeWrapS = 2U,
            .NativeWrapT = 3U,
            .MinMipLevel = 1U,
            .MaxMipLevel = 3U,
            .ImageFormat = PicaSceneTextureImageFormat::Rgba8,
            .Bound = true,
            .NativeContentHashAvailable = true,
            .CustomReplacement = true,
            .MinLinear = true,
            .MagLinear = true,
            .MipLinear = true,
        },
    } };

    PicaSceneFrame frame;
    frame.BeginFrame(0U, 14U);
    auto draw = BuildDraw("native vertex", layoutBindings,
                          layoutAttributes, vertexBindings);
    draw.EffectiveVertexShaderKey = 83U;
    draw.EffectiveVertexShaderSource = "effective vertex";
    draw.EffectiveFragmentShaderKey = 89U;
    draw.EffectiveFragmentShaderSource = "effective fragment";
    draw.Material.LogicOperation =
        Oot3d::Renderer::PicaLogicOperation::Xor;
    draw.Material.Blend.Enabled = true;
    draw.Material.Blend.EquationRgb =
        Oot3d::Renderer::NativeBlendEquation::ReverseSubtract;
    draw.Material.Blend.SourceRgb =
        Oot3d::Renderer::NativeBlendFactor::SourceAlpha;
    draw.Material.Blend.DestRgb =
        Oot3d::Renderer::NativeBlendFactor::OneMinusSourceAlpha;
    draw.Material.Blend.ConstantColor[2] = 0.75F;
    draw.Material.AlphaTestEnabled = true;
    draw.Material.AlphaCompare =
        Oot3d::Renderer::PicaCompareFunction::NotEqual;
    draw.Material.AlphaReference = 0x5aU;
    draw.Material.DepthCompare =
        Oot3d::Renderer::PicaCompareFunction::LessOrEqual;
    draw.Material.Stencil.Enabled = true;
    draw.Material.Stencil.Compare =
        Oot3d::Renderer::PicaCompareFunction::Equal;
    draw.Material.Stencil.Reference = 7U;
    draw.Material.Stencil.CompareMask = 0xf0U;
    draw.Material.Stencil.WriteMask = 0x0fU;
    draw.Material.Stencil.Pass =
        Oot3d::Renderer::PicaStencilAction::Replace;
    draw.Textures = textures;
    ASSERT_EQ(frame.Record(draw), PicaSceneRecordStatus::Recorded);
    textures[0].PhysicalAddress = 0U;

    ASSERT_EQ(frame.Draws().size(), 1U);
    const auto& recorded = frame.Draws().front();
    ASSERT_NE(recorded.FragmentShader, nullptr);
    ASSERT_NE(recorded.EffectiveVertexShader, nullptr);
    ASSERT_NE(recorded.EffectiveFragmentShader, nullptr);
    EXPECT_EQ(recorded.FragmentShader->Stage,
              PicaSceneShaderStage::Fragment);
    EXPECT_EQ(recorded.EffectiveVertexShader->Source,
              "effective vertex");
    EXPECT_EQ(recorded.EffectiveFragmentShader->Source,
              "effective fragment");
    EXPECT_EQ(recorded.FragmentUniformOffset, 1280U);
    EXPECT_EQ(recorded.FragmentUniformSize, 512U);
    EXPECT_TRUE(recorded.Material.Available());
    EXPECT_EQ(recorded.Material.LogicOperation,
              Oot3d::Renderer::PicaLogicOperation::Xor);
    EXPECT_EQ(recorded.Material.Blend.EquationRgb,
              Oot3d::Renderer::NativeBlendEquation::ReverseSubtract);
    EXPECT_FLOAT_EQ(recorded.Material.Blend.ConstantColor[2], 0.75F);
    EXPECT_EQ(recorded.Material.AlphaCompare,
              Oot3d::Renderer::PicaCompareFunction::NotEqual);
    EXPECT_EQ(recorded.Material.AlphaReference, 0x5aU);
    EXPECT_EQ(recorded.Material.DepthCompare,
              Oot3d::Renderer::PicaCompareFunction::LessOrEqual);
    EXPECT_EQ(recorded.Material.Stencil.Pass,
              Oot3d::Renderer::PicaStencilAction::Replace);
    EXPECT_EQ(recorded.BoundTextureMask, 1U << 1U);
    EXPECT_EQ(recorded.Textures[1].PhysicalAddress, 0x18000000U);
    EXPECT_EQ(recorded.Textures[1].NativeImageHandle, 0x3000U);
    EXPECT_TRUE(recorded.Textures[1].CustomReplacement);
    EXPECT_EQ(frame.Stats().BoundTextureCount, 1U);
    EXPECT_EQ(frame.Stats().CanonicalPipelineEntries, 1U);
    EXPECT_EQ(frame.Stats().CanonicalFullRegisterStateEntries, 1U);
    EXPECT_EQ(frame.Stats().ResolvedMaterialStateCount, 1U);
}

TEST(Oot3dPicaSceneFrame,
     PublishesTypedNativeEnvironmentAndVersionedUniformSlices) {
    using namespace Fast::Oot3d;
    constexpr size_t floatOffset = 80U;
    constexpr size_t vec4Bytes = 4U * sizeof(float);
    std::vector<uint8_t> vertexUniforms(floatOffset + 96U * vec4Bytes);
    const uint32_t lightingEnabled = 1U << 9U;
    std::memcpy(vertexUniforms.data(), &lightingEnabled,
                sizeof(lightingEnabled));
    const auto writeVertex = [&](uint32_t slot,
                                 const std::array<float, 4>& value) {
        std::memcpy(vertexUniforms.data() + floatOffset +
                        static_cast<size_t>(slot) * vec4Bytes,
                    value.data(), sizeof(value));
    };
    writeVertex(0x50U, {0.0F, -1.0F, 0.0F, 0.0F});
    writeVertex(0x51U, {0.7F, 0.6F, 0.5F, 1.0F});
    writeVertex(0x52U, {0.2F, 0.2F, 0.2F, 1.0F});

    constexpr size_t fogColorOffset = 128U;
    constexpr size_t fogLutOffset = fogColorOffset + vec4Bytes;
    std::vector<uint8_t> fragmentUniforms(
        kPicaPackedFragmentUniformSize, 0U);
    const float depthScale = 0.75F;
    const float depthOffset = 0.125F;
    const int32_t wBuffering = 1;
    std::memcpy(fragmentUniforms.data() + 116U, &depthScale,
                sizeof(depthScale));
    std::memcpy(fragmentUniforms.data() + 120U, &depthOffset,
                sizeof(depthOffset));
    std::memcpy(fragmentUniforms.data() + 124U, &wBuffering,
                sizeof(wBuffering));
    const std::array<float, 4> fogColor{0.1F, 0.2F, 0.3F, 1.0F};
    std::memcpy(fragmentUniforms.data() + fogColorOffset,
                fogColor.data(), sizeof(fogColor));
    for (size_t pair = 0U; pair < 64U; ++pair) {
        const std::array<float, 4> values{
            static_cast<float>(pair * 2U) / 127.0F, 0.0F,
            static_cast<float>(pair * 2U + 1U) / 127.0F, 0.0F};
        std::memcpy(fragmentUniforms.data() + fogLutOffset +
                        pair * sizeof(values),
                    values.data(), sizeof(values));
    }
    const auto writeFragmentVector =
        [&](size_t arrayOffset, size_t nativeIndex,
            const std::array<float, 4>& value) {
            std::memcpy(fragmentUniforms.data() + arrayOffset +
                            nativeIndex * vec4Bytes,
                        value.data(), sizeof(value));
        };
    const auto writeFragmentLight =
        [&](size_t nativeIndex, float base) {
            writeFragmentVector(kPicaPackedFragmentLightingSpecular0Offset,
                                nativeIndex,
                                {base + 0.01F, base + 0.02F,
                                 base + 0.03F, 1.0F});
            writeFragmentVector(kPicaPackedFragmentLightingSpecular1Offset,
                                nativeIndex,
                                {base + 0.11F, base + 0.12F,
                                 base + 0.13F, 1.0F});
            writeFragmentVector(kPicaPackedFragmentLightingDiffuseOffset,
                                nativeIndex,
                                {base + 0.21F, base + 0.22F,
                                 base + 0.23F, 1.0F});
            writeFragmentVector(kPicaPackedFragmentLightingAmbientOffset,
                                nativeIndex,
                                {base + 0.31F, base + 0.32F,
                                 base + 0.33F, 1.0F});
            writeFragmentVector(kPicaPackedFragmentLightingPositionOffset,
                                nativeIndex,
                                {base + 1.0F, base + 2.0F,
                                 base + 3.0F, 0.0F});
            writeFragmentVector(
                kPicaPackedFragmentLightingSpotDirectionOffset,
                nativeIndex,
                {base + 0.41F, base + 0.42F, base + 0.43F, 0.0F});
            writeFragmentVector(
                kPicaPackedFragmentLightingAttenuationOffset,
                nativeIndex, {base + 0.5F, base + 0.6F, 0.0F, 0.0F});
        };
    writeFragmentLight(3U, 0.1F);
    writeFragmentLight(5U, 0.2F);
    const std::array<float, 4> globalAmbient{0.15F, 0.25F, 0.35F, 1.0F};
    std::memcpy(fragmentUniforms.data() +
                    kPicaPackedFragmentLightingGlobalAmbientOffset,
                globalAmbient.data(), sizeof(globalAmbient));

    const std::array<PicaNriPackedVertexBinding, 1> layoutBindings{{
        {0U, 16U, false},
    }};
    const std::array<PicaNriPackedVertexAttribute, 1> layoutAttributes{{
        {0U, 0U, 0U, PicaNriVertexScalar::Float, 4U, 0U},
    }};
    const std::array<PicaSceneVertexBufferBinding, 1> vertexBindings{{
        {0U, 0U, 4096U, 16U},
    }};
    Oot3d::Renderer::PicaFragmentFeatureView features;
    features.SchemaVersion =
        Oot3d::Renderer::kPicaFragmentFeatureSchemaVersion;
    features.FragmentLightingEnabled = true;
    features.FogEnabled = true;
    features.FogFlip = true;
    features.FogMode = 5U;
    features.FragmentLighting.SchemaVersion =
        Oot3d::Renderer::kPicaFragmentLightingLayoutSchemaVersion;
    features.FragmentLighting.ActiveLightCount = 2U;
    features.FragmentLighting.LightPermutation[0] = 3U;
    features.FragmentLighting.LightPermutation[1] = 5U;
    features.FragmentLighting.Lights[3].Directional = true;
    features.FragmentLighting.Lights[5].SpotAttenuationEnabled = true;

    PicaSceneFrame frame;
    frame.BeginFrame(0U, 18U);
    auto draw = BuildDraw("native", layoutBindings, layoutAttributes,
                          vertexBindings, vertexUniforms,
                          fragmentUniforms, features);
    draw.VertexUniformSize = vertexUniforms.size();
    draw.FragmentUniformOffset =
        draw.VertexUniformOffset + draw.VertexUniformSize;
    draw.FragmentUniformSize = fragmentUniforms.size();
    ASSERT_EQ(frame.Record(draw), PicaSceneRecordStatus::Recorded);
    ASSERT_EQ(frame.Draws().size(), 1U);
    const auto& recorded = frame.Draws().front();
    ASSERT_TRUE(recorded.NativeLighting.Available);
    ASSERT_TRUE(recorded.NativeLighting.Enabled);
    ASSERT_EQ(recorded.NativeLighting.ActiveLightCount, 1U);
    EXPECT_FLOAT_EQ(
        recorded.NativeLighting.Lights[0].DirectionViewTowardSource[1],
        1.0F);
    ASSERT_TRUE(recorded.NativeFragmentLighting.Available);
    ASSERT_TRUE(recorded.NativeFragmentLighting.Enabled);
    EXPECT_EQ(recorded.NativeFragmentLighting.Layout.ActiveLightCount, 2U);
    EXPECT_TRUE(recorded.NativeFragmentLighting.Layout.Lights[3].Directional);
    const auto* firstFragmentLight =
        recorded.NativeFragmentLighting.EvaluationLight(0U);
    const auto* secondFragmentLight =
        recorded.NativeFragmentLighting.EvaluationLight(1U);
    ASSERT_NE(firstFragmentLight, nullptr);
    ASSERT_NE(secondFragmentLight, nullptr);
    EXPECT_FLOAT_EQ(firstFragmentLight->Diffuse[2], 0.33F);
    EXPECT_FLOAT_EQ(firstFragmentLight->PositionOrDirectionView[1], 2.1F);
    EXPECT_FLOAT_EQ(secondFragmentLight->DistanceAttenuationScale, 0.8F);
    EXPECT_FLOAT_EQ(recorded.NativeFragmentLighting.GlobalAmbient[1], 0.25F);
    ASSERT_TRUE(recorded.NativeDepth.Valid);
    EXPECT_FLOAT_EQ(recorded.NativeDepth.Scale, depthScale);
    ASSERT_TRUE(recorded.NativeFog.Available);
    ASSERT_TRUE(recorded.NativeFog.Enabled);
    EXPECT_TRUE(recorded.NativeFog.Flip);
    EXPECT_EQ(recorded.NativeFog.LutEntryCount, 128U);
    EXPECT_NE(recorded.NativeFog.LutContentVersion, 0U);
    EXPECT_NE(recorded.VertexUniformContentVersion, 0U);
    EXPECT_NE(recorded.FragmentUniformContentVersion, 0U);
    EXPECT_EQ(frame.Stats().NativeLightingStateCount, 1U);
    EXPECT_EQ(frame.Stats().NativeFragmentLightingStateCount, 1U);
    EXPECT_EQ(frame.Stats().NativeFragmentLightingEnabledDrawCount, 1U);
    EXPECT_EQ(frame.Stats().NativeFogStateCount, 1U);

    NativeSceneView view;
    ASSERT_TRUE(view.Publish(18U, frame, std::nullopt));
    const auto semantics = view.DescribeDraw(0U);
    ASSERT_TRUE(semantics.has_value());
    EXPECT_EQ(semantics->Lighting, &recorded.NativeLighting);
    EXPECT_EQ(semantics->FragmentLighting,
              &recorded.NativeFragmentLighting);
    EXPECT_EQ(semantics->Depth, &recorded.NativeDepth);
    EXPECT_EQ(semantics->Fog, &recorded.NativeFog);
    EXPECT_TRUE(semantics->VertexUniforms.Available());
    EXPECT_TRUE(semantics->VertexUniforms.ContentVersionAvailable);
    EXPECT_TRUE(semantics->FragmentUniforms.Available());
    EXPECT_TRUE(semantics->FragmentUniforms.ContentVersionAvailable);
    EXPECT_EQ(semantics->NativeLight,
              NativeSceneSemanticAvailability::Available);
    EXPECT_EQ(semantics->NativeFragmentLight,
              NativeSceneSemanticAvailability::Available);
    EXPECT_EQ(semantics->NativeFog,
              NativeSceneSemanticAvailability::Available);
}

TEST(Oot3dPicaSceneFrame, PublishesTypedCurrentAndPreviousTransformAndSkeletonSlices) {
    using namespace Fast::Oot3d;
    constexpr std::string_view shader = "native transform";
    constexpr size_t floatOffset = 80U;
    constexpr size_t vec4Bytes = 4U * sizeof(float);
    std::vector<uint8_t> current(floatOffset + 96U * vec4Bytes, 0U);
    std::vector<uint8_t> previous(current.size(), 0U);
    const uint32_t currentBooleans = (1U << 2U) | (1U << 3U);
    std::memcpy(current.data(), &currentBooleans, sizeof(currentBooleans));
    const int32_t currentFlipViewport = 1;
    std::memcpy(current.data() + sizeof(uint32_t), &currentFlipViewport,
                sizeof(currentFlipViewport));
    const auto writeRows = [&](std::vector<uint8_t>& uniforms,
                               uint32_t first,
                               std::initializer_list<std::array<float, 4>> rows) {
        uint32_t row = 0U;
        for (const auto& values : rows) {
            std::memcpy(uniforms.data() + floatOffset +
                            static_cast<size_t>(first + row) * vec4Bytes,
                        values.data(), sizeof(values));
            ++row;
        }
    };
    writeRows(current, 0U, {{2.0F, 0.0F, 0.0F, 0.0F},
                            {0.0F, 4.0F, 0.0F, 0.0F},
                            {0.0F, 0.0F, 5.0F, 0.0F},
                            {0.0F, 0.0F, 0.0F, 1.0F}});
    writeRows(current, 4U, {{1.0F, 0.0F, 0.0F, 3.0F},
                            {0.0F, 1.0F, 0.0F, -2.0F},
                            {0.0F, 0.0F, 1.0F, 7.0F}});
    writeRows(previous, 0U, {{1.0F, 0.0F, 0.0F, 0.0F},
                             {0.0F, 1.0F, 0.0F, 0.0F},
                             {0.0F, 0.0F, 1.0F, 0.0F},
                             {0.0F, 0.0F, 0.0F, 1.0F}});
    writeRows(previous, 4U, {{1.0F, 0.0F, 0.0F, 0.0F},
                             {0.0F, 1.0F, 0.0F, 0.0F},
                             {0.0F, 0.0F, 1.0F, 0.0F}});
    const std::array<PicaNriPackedVertexBinding, 1> layoutBindings{ {
        { 0U, 16U, false },
    } };
    const std::array<PicaNriPackedVertexAttribute, 1> layoutAttributes{ {
        { 0U, 0U, 0U, PicaNriVertexScalar::Float, 4U, 0U },
    } };
    const std::array<PicaSceneVertexBufferBinding, 1> vertexBindings{ {
        { 0U, 0U, 4096U, 16U },
    } };

    PicaSceneFrame frame;
    frame.BeginFrame(1U, 19U);
    auto draw = BuildDraw(shader, layoutBindings, layoutAttributes, vertexBindings, current);
    draw.VertexShaderHooks = BuildTransformHooks(shader.size());
    draw.VertexUniformSize = current.size();
    draw.PreviousVertexUniformOffset = 2048U;
    draw.PreviousVertexUniformSize = previous.size();
    draw.PreviousVertexUniformHistoryAvailable = true;
    draw.PackedPreviousVertexUniforms = previous;
    ASSERT_EQ(frame.Record(draw), PicaSceneRecordStatus::Recorded);

    ASSERT_EQ(frame.Draws().size(), 1U);
    const auto& recorded = frame.Draws().front();
    EXPECT_TRUE(recorded.NativeTransform.ProgramAvailable);
    EXPECT_TRUE(recorded.NativeTransform.CurrentAvailable);
    EXPECT_TRUE(recorded.NativeTransform.PreviousAvailable);
    EXPECT_TRUE(recorded.NativeTransform.CurrentClipToWorldAvailable);
    EXPECT_TRUE(recorded.NativeTransform.PreviousClipToWorldAvailable);
    EXPECT_TRUE(recorded.NativeTransform.CurrentViewToWorldAvailable);
    EXPECT_TRUE(recorded.NativeTransform.PreviousViewToWorldAvailable);
    EXPECT_FLOAT_EQ(recorded.NativeTransform.CurrentViewToWorld[12], -3.0F);
    EXPECT_FLOAT_EQ(recorded.NativeTransform.CurrentViewToWorld[13], 2.0F);
    EXPECT_FLOAT_EQ(recorded.NativeTransform.CurrentViewToWorld[14], -7.0F);
    EXPECT_FLOAT_EQ(recorded.NativeTransform.CurrentClipToWorld[0], 0.5F);
    EXPECT_FLOAT_EQ(recorded.NativeTransform.CurrentClipToWorld[5], -0.25F);
    EXPECT_FLOAT_EQ(recorded.NativeTransform.CurrentClipToWorld[10], -0.2F);
    EXPECT_FLOAT_EQ(recorded.NativeTransform.CurrentClipToWorld[12], -3.0F);
    EXPECT_FLOAT_EQ(recorded.NativeTransform.CurrentClipToWorld[13], 2.0F);
    EXPECT_FLOAT_EQ(recorded.NativeTransform.CurrentClipToWorld[14], -7.0F);
    EXPECT_FLOAT_EQ(recorded.NativeTransform.PreviousClipToWorld[0], 1.0F);
    EXPECT_FLOAT_EQ(recorded.NativeTransform.PreviousClipToWorld[5], 1.0F);
    EXPECT_FLOAT_EQ(recorded.NativeTransform.PreviousClipToWorld[10], -1.0F);
    EXPECT_FLOAT_EQ(recorded.NativeTransform.PreviousClipToWorld[15], 1.0F);
    EXPECT_TRUE(recorded.NativeTransform.CurrentUsesSkeleton);
    EXPECT_FALSE(recorded.NativeTransform.PreviousUsesSkeleton);
    EXPECT_TRUE(recorded.NativeSkeleton.ProgramAvailable);
    EXPECT_TRUE(recorded.NativeSkeleton.CurrentAvailable);
    EXPECT_FALSE(recorded.NativeSkeleton.PreviousAvailable);
    EXPECT_EQ(recorded.NativeSkeleton.CurrentInfluenceCount, 4U);
    EXPECT_EQ(frame.Stats().NativeTransformStateCount, 1U);
    EXPECT_EQ(frame.Stats().PreviousNativeTransformStateCount, 1U);
    EXPECT_EQ(frame.Stats().NativeSkeletonStateCount, 1U);
    EXPECT_EQ(frame.Stats().PreviousNativeSkeletonStateCount, 0U);

    NativeSceneView view;
    ASSERT_TRUE(view.Publish(19U, frame, std::nullopt));
    const auto semantics = view.DescribeDraw(0U);
    ASSERT_TRUE(semantics.has_value());
    EXPECT_EQ(semantics->Transform, &recorded.NativeTransform);
    EXPECT_EQ(semantics->Skeleton, &recorded.NativeSkeleton);
    EXPECT_TRUE(semantics->PreviousVertexUniforms.Available());
    EXPECT_TRUE(semantics->PreviousVertexUniforms.ContentVersionAvailable);
    EXPECT_EQ(semantics->CurrentTransform, NativeSceneSemanticAvailability::Available);
    EXPECT_EQ(semantics->PreviousTransform, NativeSceneSemanticAvailability::Available);
    EXPECT_EQ(semantics->CurrentSkeleton, NativeSceneSemanticAvailability::Available);
    EXPECT_EQ(semantics->PreviousSkeleton, NativeSceneSemanticAvailability::Unavailable);
}

TEST(Oot3dPicaSceneFrame, RejectsConflictingShaderIdentityAndInvalidRanges) {
    using namespace Fast::Oot3d;
    const std::array<PicaNriPackedVertexBinding, 1> layoutBindings{ {
        { 0U, 16U, false },
    } };
    const std::array<PicaNriPackedVertexAttribute, 1> layoutAttributes{ {
        { 0U, 0U, 0U, PicaNriVertexScalar::Float, 4U, 0U },
    } };
    const std::array<PicaSceneVertexBufferBinding, 1> vertexBindings{ {
        { 0U, 0U, 4096U, 16U },
    } };

    PicaSceneFrame frame;
    auto draw = BuildDraw("first", layoutBindings, layoutAttributes,
                          vertexBindings);
    EXPECT_EQ(frame.Record(draw), PicaSceneRecordStatus::FrameNotActive);
    frame.BeginFrame(0U, 13U);
    ASSERT_EQ(frame.Record(draw), PicaSceneRecordStatus::Recorded);
    draw.VertexShaderSource = "different source with the same key";
    EXPECT_EQ(frame.Record(draw),
              PicaSceneRecordStatus::ShaderIdentityConflict);
    draw.VertexShaderKey = 73U;
    draw.VertexUniformOffset = draw.UniformBuffer.Size;
    EXPECT_EQ(frame.Record(draw), PicaSceneRecordStatus::InvalidUniformRange);
    draw.Raster.SchemaVersion = 0U;
    EXPECT_EQ(frame.Record(draw), PicaSceneRecordStatus::InvalidRasterState);
    draw.Raster.SchemaVersion = kPicaSceneResolvedRasterStateSchemaVersion;
    draw.RenderTarget.SchemaVersion = 0U;
    EXPECT_EQ(frame.Record(draw),
              PicaSceneRecordStatus::InvalidRenderTargetState);
    draw.RenderTarget.SchemaVersion =
        kPicaSceneRenderTargetStateSchemaVersion;
    draw.Material.SchemaVersion = 0U;
    EXPECT_EQ(frame.Record(draw), PicaSceneRecordStatus::InvalidMaterialState);
    EXPECT_EQ(frame.Stats().RejectedDrawCount, 5U);
}

TEST(Oot3dNativeSceneView, ReferencesFrameAndTracksCameraHistory) {
    using namespace Fast::Oot3d;
    const std::array<PicaNriPackedVertexBinding, 1> layoutBindings{ {
        { 0U, 16U, false },
    } };
    const std::array<PicaNriPackedVertexAttribute, 1> layoutAttributes{ {
        { 0U, 0U, 0U, PicaNriVertexScalar::Float, 4U, 0U },
    } };
    const std::array<PicaSceneVertexBufferBinding, 1> vertexBindings{ {
        { 0U, 0U, 4096U, 16U },
    } };

    PicaSceneFrame frame;
    frame.BeginFrame(0U, 20U);
    ASSERT_TRUE(frame.SetTemporalSample(
        BuildNativeFrameTemporalSample({ NativeVisualInterpolationMode::Fixed2x, 30U, 60U, 2U, true },
                                       NativeFrameTemporalSampleKind::Transition, 18U, 19U, 4U, 0.5F)));
    ASSERT_EQ(frame.Record(BuildDraw(
                  "native", layoutBindings, layoutAttributes,
                  vertexBindings)),
              PicaSceneRecordStatus::Recorded);
    PerspectiveViewState firstCamera;
    firstCamera.Serial = 101U;
    firstCamera.CameraAvailable = true;

    NativeSceneView view;
    ASSERT_TRUE(view.Publish(20U, frame, firstCamera));
    EXPECT_TRUE(view.Active());
    EXPECT_EQ(view.FrameId(), 20U);
    EXPECT_EQ(view.PicaFrame(), &frame);
    ASSERT_TRUE(view.TemporalSample().Available());
    EXPECT_EQ(view.TemporalSample().FixedSampleMultiplier, 2U);
    EXPECT_EQ(view.TemporalSample().SampleOrdinal, 1U);
    ASSERT_EQ(view.Draws().size(), 1U);
    EXPECT_EQ(view.Draws().data(), frame.Draws().data());
    ASSERT_TRUE(view.CurrentPerspective().has_value());
    EXPECT_EQ(view.CurrentPerspective()->Serial, 101U);
    EXPECT_FALSE(view.PreviousPerspective().has_value());
    ASSERT_TRUE(view.CurrentViewFamily().has_value());
    EXPECT_TRUE(view.CurrentViewFamily()->Available());
    EXPECT_FALSE(view.CurrentViewFamily()->Stereoscopic());
    EXPECT_EQ(view.CurrentViewFamily()->Views[0].PoseVersion, 101U);
    EXPECT_FALSE(view.PreviousViewFamily().has_value());
    const auto firstSharedView = view.SharedSemanticView();
    EXPECT_TRUE(firstSharedView.Available());
    EXPECT_TRUE(firstSharedView.TemporalSampleAvailable());
    EXPECT_TRUE(firstSharedView.CurrentViewFamilyAvailable());
    EXPECT_FALSE(firstSharedView.ViewFamilyHistoryAvailable());
    EXPECT_EQ(firstSharedView.TemporalSample, &frame.TemporalSample());
    EXPECT_EQ(firstSharedView.CurrentViewFamily,
              &*view.CurrentViewFamily());
    const uint64_t firstGeneration = view.Generation();
    ASSERT_NE(firstGeneration, 0U);

    EXPECT_TRUE(view.Publish(20U, frame, firstCamera));
    EXPECT_EQ(view.Generation(), firstGeneration);

    frame.BeginFrame(1U, 21U);
    ASSERT_EQ(frame.Record(BuildDraw(
                  "native", layoutBindings, layoutAttributes,
                  vertexBindings)),
              PicaSceneRecordStatus::Recorded);
    PerspectiveViewState secondCamera = firstCamera;
    secondCamera.Serial = 102U;
    ASSERT_TRUE(view.Publish(21U, frame, secondCamera));
    EXPECT_GT(view.Generation(), firstGeneration);
    EXPECT_TRUE(view.CameraHistoryAvailable());
    EXPECT_TRUE(view.ViewFamilyHistoryAvailable());
    ASSERT_TRUE(view.PreviousPerspective().has_value());
    EXPECT_EQ(view.PreviousPerspective()->Serial, 101U);
    EXPECT_EQ(view.CurrentPerspective()->Serial, 102U);
    const auto secondSharedView = view.SharedSemanticView();
    EXPECT_TRUE(secondSharedView.ViewFamilyHistoryAvailable());
    EXPECT_EQ(secondSharedView.PreviousViewFamily,
              &*view.PreviousViewFamily());
    EXPECT_EQ(secondSharedView.CurrentViewFamily,
              &*view.CurrentViewFamily());
}

TEST(Oot3dNativeSceneView, SharesOneTemporalSampleAcrossStereoViewFamily) {
    using namespace Fast::Oot3d;

    PicaSceneFrame frame;
    frame.BeginFrame(0U, 40U);
    ASSERT_TRUE(frame.SetTemporalSample(
        BuildNativeFrameTemporalSample({ NativeVisualInterpolationMode::Fixed3x, 30U, 90U, 3U, true },
                                       NativeFrameTemporalSampleKind::Transition, 38U, 39U, 7U, 2.0F / 3.0F)));

    PerspectiveViewState left;
    left.Serial = 201U;
    left.Eye = { -0.03F, 1.0F, 2.0F };
    left.CameraAvailable = true;
    PerspectiveViewState right = left;
    right.Serial = 202U;
    right.Eye[0] = 0.03F;

    NativeViewFamily family;
    family.SchemaVersion = kNativeViewFamilySchemaVersion;
    family.FamilyId = 11U;
    family.ViewCount = 2U;
    family.Views[0] = {
        .SchemaVersion = kNativeFrameViewSchemaVersion,
        .ViewId = 101U,
        .PoseVersion = 201U,
        .ProjectionVersion = 301U,
        .Role = NativeFrameViewRole::StereoLeft,
        .Perspective = left,
    };
    family.Views[1] = {
        .SchemaVersion = kNativeFrameViewSchemaVersion,
        .ViewId = 102U,
        .PoseVersion = 202U,
        .ProjectionVersion = 302U,
        .Role = NativeFrameViewRole::StereoRight,
        .Perspective = right,
    };

    NativeSceneView view;
    ASSERT_TRUE(view.PublishViewFamily(40U, frame, family));
    ASSERT_TRUE(view.CurrentViewFamily().has_value());
    EXPECT_TRUE(view.CurrentViewFamily()->Stereoscopic());
    EXPECT_EQ(view.CurrentViewFamily()->ViewCount, 2U);
    EXPECT_EQ(view.TemporalSample().FixedSampleMultiplier, 3U);
    EXPECT_EQ(view.TemporalSample().SampleOrdinal, 2U);
    EXPECT_FLOAT_EQ(view.TemporalSample().Alpha, 2.0F / 3.0F);
    EXPECT_FALSE(view.ViewFamilyHistoryAvailable());

    frame.BeginFrame(1U, 41U);
    family.Views[0].PoseVersion = 203U;
    family.Views[0].Perspective.Serial = 203U;
    family.Views[1].PoseVersion = 204U;
    family.Views[1].Perspective.Serial = 204U;
    ASSERT_TRUE(view.PublishViewFamily(41U, frame, family));
    EXPECT_TRUE(view.ViewFamilyHistoryAvailable());
    ASSERT_TRUE(view.PreviousViewFamily().has_value());
    EXPECT_EQ(view.PreviousViewFamily()->Views[0].PoseVersion, 201U);
    EXPECT_EQ(view.CurrentViewFamily()->Views[0].PoseVersion, 203U);
}

TEST(Oot3dNativeSceneView, ExposesKnownDataAndMarksMissingSemantics) {
    using namespace Fast::Oot3d;
    const std::array<PicaNriPackedVertexBinding, 1> layoutBindings{ {
        { 0U, 16U, false },
    } };
    const std::array<PicaNriPackedVertexAttribute, 1> layoutAttributes{ {
        { 0U, 0U, 0U, PicaNriVertexScalar::Float, 4U, 0U },
    } };
    const std::array<PicaSceneVertexBufferBinding, 1> vertexBindings{ {
        { 0U, 0U, 4096U, 16U },
    } };
    std::array<PicaSceneTextureBinding, 1> textures{ {
        {
            .NativeImageHandle = 0x3000U,
            .NativeContentHash = 73U,
            .NativeBaseLevelContentHash = 79U,
            .PhysicalAddress = 0x18000000U,
            .ImageWidth = 64U,
            .ImageHeight = 32U,
            .MipLevels = 1U,
            .SourceWidth = 64U,
            .SourceHeight = 32U,
            .Slot = 0U,
            .NativeFormat = 3U,
            .Bound = true,
            .NativeContentHashAvailable = true,
            .NativeBaseLevelContentHashAvailable = true,
        },
    } };

    PicaSceneFrame frame;
    frame.BeginFrame(0U, 30U);
    auto draw = BuildDraw(
        "native", layoutBindings, layoutAttributes, vertexBindings);
    draw.Textures = textures;
    ASSERT_EQ(frame.Record(draw), PicaSceneRecordStatus::Recorded);

    NativeSceneView view;
    ASSERT_TRUE(view.Publish(30U, frame, std::nullopt));
    const auto semantics = view.DescribeDraw(0U);
    ASSERT_TRUE(semantics.has_value());
    EXPECT_EQ(semantics->PicaDraw, &frame.Draws().front());
    ASSERT_NE(semantics->Composition, nullptr);
    EXPECT_EQ(semantics->Composition->Layer,
              Oot3d::Renderer::PicaCompositionLayer::OpaqueWorld);
    EXPECT_EQ(semantics->Composition->Provenance,
              Oot3d::Renderer::PicaCompositionProvenance::NativeCmbDrawPass);
    ASSERT_TRUE(semantics->Raster.Available());
    EXPECT_EQ(semantics->Raster.ResolvedState,
              &frame.Draws().front().Raster);
    ASSERT_TRUE(semantics->RenderTarget.Available());
    EXPECT_TRUE(semantics->RenderTarget.GpuResourcesAvailable());
    EXPECT_EQ(semantics->RenderTarget.ResolvedState,
              &frame.Draws().front().RenderTarget);
    EXPECT_TRUE(semantics->Geometry.Available());
    EXPECT_EQ(semantics->Geometry.Identity, 67U);
    EXPECT_EQ(semantics->Geometry.ContentVersion, 71U);
    EXPECT_TRUE(semantics->Material.Available());
    ASSERT_TRUE(semantics->Material.ResolvedStateAvailable());
    EXPECT_EQ(semantics->Material.ResolvedState,
              &frame.Draws().front().Material);
    EXPECT_TRUE(semantics->Material.ResolvedState->DepthTestEnabled);
    EXPECT_EQ(semantics->Material.FullRegisterStateId, 53U);
    EXPECT_TRUE(semantics->Textures[0].Bound);
    EXPECT_EQ(semantics->Textures[0].Image.NativeHandle, 0x3000U);
    EXPECT_EQ(semantics->Textures[0].Image.ContentVersion, 73U);
    EXPECT_EQ(semantics->Textures[0].NativeBaseLevelContentHash, 79U);
    EXPECT_EQ(semantics->ObjectIdentity,
              NativeSceneSemanticAvailability::Unavailable);
    EXPECT_EQ(semantics->CurrentTransform,
              NativeSceneSemanticAvailability::Unavailable);
    EXPECT_EQ(semantics->PreviousTransform,
              NativeSceneSemanticAvailability::Unavailable);
    EXPECT_EQ(semantics->CurrentSkeleton,
              NativeSceneSemanticAvailability::Unavailable);
    EXPECT_EQ(semantics->PreviousSkeleton,
              NativeSceneSemanticAvailability::Unavailable);
    EXPECT_EQ(semantics->NativeFog,
              NativeSceneSemanticAvailability::Unavailable);
    EXPECT_EQ(semantics->NativeLight,
              NativeSceneSemanticAvailability::Unavailable);
    EXPECT_FALSE(view.DescribeDraw(1U).has_value());
}

TEST(Oot3dPicaScenePublication,
     AdaptsConcretePayloadsToShared3dsCapabilities) {
    using namespace Fast::Oot3d;
    PicaSceneFrame frame;
    frame.BeginFrame(0U, 35U);

    PerspectiveViewState camera;
    camera.Serial = 7U;
    camera.CameraAvailable = true;
    NativeSceneView sceneView;
    ASSERT_TRUE(sceneView.Publish(35U, frame, camera));

    PicaScenePublicationAdapter adapter;
    ASSERT_TRUE(adapter.Publish(35U, frame, sceneView));
    ASSERT_TRUE(adapter.View().Valid());
    EXPECT_EQ(adapter.View().PublishedCount(), 3U);
    const auto* drawStream = adapter.ResolveDrawStream();
    ASSERT_NE(drawStream, nullptr);
    EXPECT_TRUE(drawStream->Available());
    EXPECT_EQ(drawStream->FrameId, frame.FrameId());
    EXPECT_EQ(drawStream->Draws.size(), frame.Draws().size());
    const auto* semanticView = adapter.ResolveSemanticView();
    ASSERT_NE(semanticView, nullptr);
    EXPECT_TRUE(semanticView->Available());
    EXPECT_EQ(semanticView->FrameId, sceneView.FrameId());
    EXPECT_EQ(semanticView->Generation, sceneView.Generation());
    EXPECT_EQ(semanticView->Draws().data(), frame.Draws().data());
    EXPECT_EQ(adapter.ResolvePerspectiveCamera(),
              &*sceneView.CurrentPerspective());
    EXPECT_TRUE(::Fast::Renderer::ResolveExtensionSceneCapabilities(
                    adapter.View(),
                    BuildOot3dGeometryProviderSceneRequirements())
                    .Complete());

    camera.CameraAvailable = false;
    ASSERT_TRUE(sceneView.Publish(35U, frame, camera));
    ASSERT_TRUE(adapter.Publish(35U, frame, sceneView));
    EXPECT_EQ(adapter.View().PublishedCount(), 2U);
    EXPECT_EQ(adapter.ResolvePerspectiveCamera(), nullptr);
    EXPECT_EQ(::Fast::Renderer::ResolveExtensionSceneCapabilities(
                  adapter.View(),
                  BuildOot3dGeometryProviderSceneRequirements())
                  .Status,
              ::Fast::Renderer::ExtensionSceneCapabilityResolutionStatus::
                  MissingCapability);
}

TEST(Oot3dDirectionalShadowSemantics,
     ClustersNativeLightsInWorldSpaceAndWeightsSceneGeometry) {
    using namespace Fast::Oot3d;
    PicaSceneDrawRecord first;
    first.VertexOrIndexCount = 100U;
    first.NativeTransform.CurrentViewToWorld =
        IdentityDirectionalShadowMatrix();
    first.NativeTransform.CurrentViewToWorldAvailable = true;
    first.NativeLighting.Available = true;
    first.NativeLighting.Enabled = true;
    first.NativeLighting.ActiveLightCount = 1U;
    first.NativeLighting.Lights[0] = {
        {0.0F, 1.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, {}, true};

    PicaSceneDrawRecord second;
    second.VertexOrIndexCount = 300U;
    second.NativeTransform.CurrentViewToWorld = {
        0.0F, 1.0F, 0.0F, 0.0F,
        -1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        7.0F, 8.0F, 9.0F, 1.0F,
    };
    second.NativeTransform.CurrentViewToWorldAvailable = true;
    second.NativeLighting.Available = true;
    second.NativeLighting.Enabled = true;
    second.NativeLighting.ActiveLightCount = 2U;
    second.NativeLighting.Lights[0] = {
        {1.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F}, {}, true};
    second.NativeLighting.Lights[1] = {
        {0.0F, 0.0F, 1.0F}, {0.2F, 0.2F, 0.2F}, {}, true};

    const std::array<const PicaSceneDrawRecord*, 2> draws{
        &first, &second};
    const auto selection =
        ResolvePicaDirectionalShadowLightSelection(draws);
    ASSERT_TRUE(selection.Valid());
    EXPECT_EQ(selection.ReferenceDraw, &second);
    EXPECT_EQ(selection.ReferenceLightIndex, 0U);
    EXPECT_EQ(selection.CandidateCount, 3U);
    EXPECT_EQ(selection.ClusterCount, 2U);
    EXPECT_NEAR(selection.WorldDirectionTowardSource[0], 0.0F, 1.0e-5F);
    EXPECT_NEAR(selection.WorldDirectionTowardSource[1], 1.0F, 1.0e-5F);
    EXPECT_NEAR(selection.WorldDirectionTowardSource[2], 0.0F, 1.0e-5F);
}

TEST(Oot3dDirectionalShadowSemantics,
     ReceiverPreservesAmbientAndUnmatchedNativeLights) {
    using namespace Fast::Oot3d;
    PicaNativeLightingState lighting;
    lighting.Available = true;
    lighting.Enabled = true;
    lighting.ActiveLightCount = 2U;
    lighting.Lights[0] = {
        {0.0F, 1.0F, 0.0F}, {0.8F, 0.8F, 0.8F},
        {0.1F, 0.1F, 0.1F}, true};
    lighting.Lights[1] = {
        {1.0F, 0.0F, 0.0F}, {0.2F, 0.2F, 0.2F},
        {0.1F, 0.1F, 0.1F}, true};

    const auto receiver = ResolvePicaDirectionalShadowReceiverLight(
        lighting, IdentityDirectionalShadowMatrix(),
        {0.0F, 1.0F, 0.0F});
    ASSERT_TRUE(receiver.Valid());
    EXPECT_EQ(receiver.Classification,
              PicaDirectionalShadowReceiverClass::DirectMatched);
    EXPECT_EQ(receiver.MatchedLightCount, 1U);
    EXPECT_NEAR(receiver.ShadowedDirectFraction[0], 2.0F / 3.0F,
                1.0e-5F);
    EXPECT_NEAR(receiver.ShadowedDirectFraction[1], 2.0F / 3.0F,
                1.0e-5F);
    EXPECT_NEAR(receiver.ShadowedDirectFraction[2], 2.0F / 3.0F,
                1.0e-5F);

    const auto unmatched = ResolvePicaDirectionalShadowReceiverLight(
        lighting, IdentityDirectionalShadowMatrix(),
        {0.0F, 0.0F, 1.0F});
    EXPECT_FALSE(unmatched.Valid());
    EXPECT_EQ(unmatched.Classification,
              PicaDirectionalShadowReceiverClass::DirectUnmatched);
    EXPECT_EQ(unmatched.MatchedLightCount, 0U);

    lighting.Lights[0].Diffuse = {};
    lighting.Lights[1].Diffuse = {};
    const auto ambientOnly = ResolvePicaDirectionalShadowReceiverLight(
        lighting, IdentityDirectionalShadowMatrix(),
        {0.0F, 1.0F, 0.0F});
    EXPECT_FALSE(ambientOnly.Valid());
    EXPECT_EQ(ambientOnly.Classification,
              PicaDirectionalShadowReceiverClass::AmbientOnly);
}

} // namespace
