#include "fast/renderer3ds/pica_scene_capabilities.h"
#include "fast/renderer3ds/pica_composition.h"
#include "fast/renderer3ds/pica_composition_schedule.h"
#include "fast/renderer3ds/pica_display_composition.h"
#include "fast/renderer3ds/pica_extension_schedule.h"
#include "fast/renderer3ds/pica_geometry_provider_authorization.h"
#include "fast/renderer3ds/pica_render_backend.h"
#include "fast/renderer3ds/pica_resolved_draw_stream.h"
#include "fast/renderer3ds/pica_semantic_scene_view.h"
#include "fast/renderer3ds/pica_shader_hooks.h"
#include "fast/renderer3ds/pica_shader_source_identity.h"

#include <gtest/gtest.h>

#include <array>
#include <utility>

namespace {

using namespace Fast::Renderer3ds;

class RecordingPicaBackend final : public PicaRenderBackend {
  public:
    bool SubmitPicaDraw(const PicaDrawView& draw,
                        std::string* = nullptr) override {
        SubmittedDraw = &draw;
        return true;
    }

    bool SubmitPicaDisplayTransfer(
        const PicaDisplayTransferView& transfer,
        std::string* = nullptr) override {
        SubmittedTransfer = &transfer;
        return true;
    }

    bool ClearPicaRenderTarget(uint64_t renderTargetNamespace,
                               uint32_t colorPhysicalAddress,
                               std::string* = nullptr) override {
        ClearedTarget = { renderTargetNamespace, colorPhysicalAddress };
        return true;
    }

    bool SubmitPicaMemoryFill(const PicaMemoryFillView& fill,
                              std::string* = nullptr) override {
        SubmittedFill = &fill;
        return true;
    }

    bool QueuePicaCompletion(uint64_t completionId,
                             std::string* = nullptr) override {
        PendingCompletions.push_back(completionId);
        return true;
    }

    std::vector<uint64_t> TakePicaCompletions() override {
        auto completions = std::move(PendingCompletions);
        PendingCompletions.clear();
        return completions;
    }

    bool ResetPicaState(std::string* = nullptr) override {
        PendingCompletions.clear();
        ++ResetCount;
        return true;
    }

    bool PublishPicaFrameTemporalSample(
        const PicaFrameTemporalSample& sample) override {
        TemporalSample = sample;
        return true;
    }

    const PicaDrawView* SubmittedDraw = nullptr;
    const PicaDisplayTransferView* SubmittedTransfer = nullptr;
    std::pair<uint64_t, uint32_t> ClearedTarget{};
    const PicaMemoryFillView* SubmittedFill = nullptr;
    std::vector<uint64_t> PendingCompletions;
    PicaFrameTemporalSample TemporalSample;
    uint32_t ResetCount = 0U;
};

TEST(Renderer3dsPicaSceneCapabilities,
     ExposesATitleNeutralNintendo3dsBackendFacade) {
    RecordingPicaBackend backend;
    PicaDrawView draw;
    draw.SubmissionId = 17U;
    PicaDisplayTransferView transfer;
    transfer.CompletionId = 19U;
    PicaMemoryFillView fill;
    fill.StartPhysicalAddress = 0x18000000U;

    EXPECT_TRUE(backend.SubmitPicaDraw(draw));
    EXPECT_TRUE(backend.SubmitPicaDisplayTransfer(transfer));
    EXPECT_TRUE(backend.ClearPicaRenderTarget(23U, 0x18100000U));
    EXPECT_TRUE(backend.SubmitPicaMemoryFill(fill));
    EXPECT_EQ(backend.SubmittedDraw, &draw);
    EXPECT_EQ(backend.SubmittedTransfer, &transfer);
    EXPECT_EQ(backend.ClearedTarget,
              (std::pair<uint64_t, uint32_t>{ 23U, 0x18100000U }));
    EXPECT_EQ(backend.SubmittedFill, &fill);

    draw.RenderTargetNamespace = 29U;
    draw.FramebufferColorPhysicalAddress = 0x18200000U;
    draw.FramebufferDepthPhysicalAddress = 0x18300000U;
    draw.FramebufferWidth = 400U;
    draw.FramebufferHeight = 240U;
    const auto composition = BuildPicaCompositionDrawReference(draw);
    EXPECT_EQ(composition.SubmissionId, draw.SubmissionId);
    EXPECT_EQ(composition.Target.RenderTargetNamespace,
              draw.RenderTargetNamespace);
    EXPECT_EQ(composition.Target.ColorPhysicalAddress,
              draw.FramebufferColorPhysicalAddress);
    EXPECT_EQ(composition.Target.DepthPhysicalAddress,
              draw.FramebufferDepthPhysicalAddress);

    const auto temporalSample = BuildPicaFrameTemporalSample(
        { PicaVisualInterpolationMode::Fixed2x, 30U, 60U, 2U, true },
        PicaFrameTemporalSampleKind::Transition, 31U, 32U, 4U, 0.5F);
    EXPECT_TRUE(backend.PublishPicaFrameTemporalSample(temporalSample));
    EXPECT_EQ(backend.TemporalSample.CurrentSourceFrameId, 32U);
    EXPECT_TRUE(backend.QueuePicaCompletion(37U));
    EXPECT_TRUE(backend.QueuePicaCompletion(38U));
    EXPECT_EQ(backend.TakePicaCompletions(),
              (std::vector<uint64_t>{ 37U, 38U }));
    EXPECT_TRUE(backend.QueuePicaCompletion(39U));
    EXPECT_TRUE(backend.ResetPicaState());
    EXPECT_EQ(backend.ResetCount, 1U);
    EXPECT_TRUE(backend.TakePicaCompletions().empty());
}

TEST(Renderer3dsPicaSceneCapabilities,
     ValidatesSharedCompositionAndPortableEmptyRestoreState) {
    RecordingPicaBackend backend;
    std::string error;
    PicaCompositionSequenceView sequence;
    sequence.SchemaVersion = kPicaCompositionSequenceSchemaVersion + 1U;
    EXPECT_FALSE(backend.PublishPicaCompositionSequence(sequence, &error));
    EXPECT_FALSE(error.empty());

    error.clear();
    sequence.SchemaVersion = kPicaCompositionSequenceSchemaVersion;
    EXPECT_TRUE(backend.PublishPicaCompositionSequence(sequence, &error));
    EXPECT_TRUE(error.empty());
    EXPECT_TRUE(backend.RestorePicaTextureCache({}, &error));
    EXPECT_TRUE(backend.RestorePicaColorTargets({}, &error));
    EXPECT_TRUE(backend.RestorePicaPresentationState({}, &error));
}

TEST(Renderer3dsPicaSceneCapabilities,
     PublishesStableCrossTitleCapabilityIdentities) {
    const auto draws =
        PicaSceneCapabilityIdentity(PicaSceneCapability::ResolvedDrawStream);
    const auto view =
        PicaSceneCapabilityIdentity(PicaSceneCapability::SemanticSceneView);
    const auto camera =
        PicaSceneCapabilityIdentity(PicaSceneCapability::PerspectiveCamera);
    EXPECT_TRUE(draws.Valid());
    EXPECT_TRUE(view.Valid());
    EXPECT_TRUE(camera.Valid());
    EXPECT_NE(draws, view);
    EXPECT_NE(view, camera);
    EXPECT_EQ(PicaSceneCapabilityName(
                  PicaSceneCapability::ResolvedDrawStream),
              "ResolvedDrawStream");
    EXPECT_FALSE(PicaSceneCapabilityIdentity(
                     PicaSceneCapability::Count)
                     .Valid());
}

TEST(Renderer3dsPicaSceneCapabilities,
     PublishesSharedGeometryProviderRequirements) {
    const auto requirements =
        BuildPicaGeometryProviderSceneRequirements();
    ASSERT_EQ(requirements.size(), 3U);
    EXPECT_EQ(requirements[0].Capability,
              PicaSceneCapabilityIdentity(
                  PicaSceneCapability::ResolvedDrawStream));
    EXPECT_EQ(requirements[0].MinimumSchemaVersion,
              kPicaResolvedDrawStreamSchemaVersion);
    EXPECT_EQ(requirements[1].Capability,
              PicaSceneCapabilityIdentity(
                  PicaSceneCapability::SemanticSceneView));
    EXPECT_EQ(requirements[1].MinimumSchemaVersion,
              kPicaSemanticSceneViewSchemaVersion);
    EXPECT_EQ(requirements[2].Capability,
              PicaSceneCapabilityIdentity(
                  PicaSceneCapability::PerspectiveCamera));
    EXPECT_EQ(requirements[2].MinimumSchemaVersion,
              kPicaPerspectiveCameraSchemaVersion);
}

TEST(Renderer3dsPicaSceneCapabilities,
     WrapsTitlePayloadWithoutCopyingOrInterpretingIt) {
    const uint32_t titlePayload = 31U;
    const auto publication = BuildPicaSceneCapabilityPublication(
        PicaSceneCapability::SemanticSceneView, &titlePayload, 9U, 5U, 7U);
    ASSERT_TRUE(publication.Published());
    EXPECT_EQ(publication.Payload, &titlePayload);
    EXPECT_EQ(publication.PayloadSchemaVersion, 9U);
    EXPECT_EQ(publication.Generation, 5U);
    EXPECT_EQ(publication.FrameId, 7U);

    EXPECT_TRUE(BuildPicaSceneCapabilityPublication(
                    PicaSceneCapability::SemanticSceneView, nullptr,
                    9U, 5U, 7U)
                    .Empty());
}

TEST(Renderer3dsPicaSceneCapabilities,
     PublishesTheSharedPerspectiveCameraSchema) {
    PicaPerspectiveCameraState camera;
    camera.Serial = 4U;
    camera.CameraAvailable = true;
    const auto publication = BuildPicaSceneCapabilityPublication(
        PicaSceneCapability::PerspectiveCamera, &camera,
        kPicaPerspectiveCameraSchemaVersion, camera.Serial, 12U);
    ASSERT_TRUE(publication.Published());
    EXPECT_EQ(publication.Payload, &camera);
    EXPECT_EQ(publication.PayloadSchemaVersion,
              kPicaPerspectiveCameraSchemaVersion);
}

TEST(Renderer3dsPicaSceneCapabilities,
     OwnsVersionedMaterialRasterAndRenderTargetPayloads) {
    PicaSceneResolvedRasterState raster;
    EXPECT_FALSE(raster.Available());
    raster.SchemaVersion = kPicaSceneResolvedRasterStateSchemaVersion;
    EXPECT_TRUE(raster.Available());

    PicaSceneResolvedMaterialState material;
    material.SchemaVersion =
        kPicaSceneResolvedMaterialStateSchemaVersion;
    material.FragmentFeatures.SchemaVersion =
        kPicaFragmentFeatureSchemaVersion;
    EXPECT_TRUE(material.Available());
    EXPECT_TRUE(material.FragmentFeatures.Valid());

    PicaSceneRenderTargetState target;
    target.SchemaVersion = kPicaSceneRenderTargetStateSchemaVersion;
    target.ResolvedColor = { 1U, 2U, 400U, 240U, 37U, true };
    target.ResolvedDepth = { 3U, 4U, 400U, 240U, 126U, true };
    EXPECT_TRUE(target.Available());
    EXPECT_TRUE(target.GpuResourcesAvailable());
}

TEST(Renderer3dsPicaSceneCapabilities,
     OwnsStableShaderSourceIdentityWithoutTitlePolicy) {
    const auto first = IdentifyPicaShaderSource("void main() { }");
    const auto same = IdentifyPicaShaderSource("void main() { }");
    const auto different = IdentifyPicaShaderSource("void main() { return; }");
    EXPECT_TRUE(first.Available());
    EXPECT_EQ(first, same);
    EXPECT_NE(first, different);
}

TEST(Renderer3dsPicaSceneCapabilities,
     OwnsOrderedPicaCompositionMetadata) {
    const PicaCompositionDrawReference draw{
        17U,
        { 3U, 0x1000U, 0x2000U, 400U, 240U, 1U, 2U },
        PicaCompositionDomain::Scene,
        { PicaCompositionLayer::OpaqueWorld,
          PicaCompositionProvenance::NativeControlFlow, 0x1234U, 7U },
    };
    const PicaCompositionSequenceView sequence{
        kPicaCompositionSequenceSchemaVersion, 11U,
        std::span<const PicaCompositionDrawReference>(&draw, 1U),
    };
    ASSERT_EQ(sequence.Draws.size(), 1U);
    EXPECT_EQ(sequence.Draws.front().Target.FramebufferWidth, 400U);
    EXPECT_EQ(sequence.Draws.front().Composition.Layer,
              PicaCompositionLayer::OpaqueWorld);
}

TEST(Renderer3dsPicaSceneCapabilities,
     OwnsTitleNeutralFragmentShaderHookContract) {
    constexpr std::string_view source = "void main() { }";
    PicaShaderHookLayout hooks;
    hooks.SchemaVersion = kPicaShaderHookSchemaVersion;
    hooks.SourceSize = source.size();
    hooks.Offsets[static_cast<size_t>(
        PicaShaderHook::GlobalDeclarations)] = 0U;
    hooks.Offsets[static_cast<size_t>(
        PicaShaderHook::MainEpilogue)] = source.size();
    hooks.Semantics = PicaShaderSemantic::NativeColorOutput |
                      PicaShaderSemantic::CombinerOutput;
    hooks.Outputs = {
        kPicaFragmentOutputContractSchemaVersion,
        1U,
        0U,
        PicaFragmentDepthOutput::FixedFunction,
        false,
        false,
    };

    EXPECT_TRUE(hooks.ValidFor(source));
    EXPECT_TRUE(hooks.Outputs.CanonicalNative());
    EXPECT_TRUE(hooks.Has(PicaShaderSemantic::CombinerOutput));
    EXPECT_FALSE(hooks.SamplesTexture(0U));
}

TEST(Renderer3dsPicaSceneCapabilities,
     OwnsTitleNeutralVertexAndTemporalHookContract) {
    constexpr std::string_view source = "void main() { }";
    PicaVertexShaderHookLayout hooks;
    hooks.SchemaVersion = kPicaShaderHookSchemaVersion;
    hooks.SourceSize = source.size();
    hooks.Offsets.fill(0U);
    hooks.Offsets[static_cast<size_t>(
        PicaVertexShaderHook::MainBodyEnd)] = source.size();
    hooks.Semantics = PicaVertexShaderSemantic::PicaRegisterState |
                      PicaVertexShaderSemantic::VertexUniformState |
                      PicaVertexShaderSemantic::ClipPositionOutput;

    ASSERT_TRUE(hooks.ValidFor(source));
    const PicaTemporalVertexProgramView temporal{
        hooks,
        "previous register state",
        "previous main body",
    };
    EXPECT_TRUE(temporal.ValidFor(source));
}

TEST(Renderer3dsPicaSceneCapabilities,
     PublishesAZeroCopyTitleNeutralResolvedDrawStream) {
    std::array<PicaSceneVertexBufferBinding, 2U> bindings{
        PicaSceneVertexBufferBinding{ 0U, 16U, 128U, 16U },
        PicaSceneVertexBufferBinding{ 1U, 144U, 64U, 8U },
    };
    PicaResolvedDrawRecord draw;
    draw.FirstVertexBinding = 0U;
    draw.VertexBindingCount = 2U;
    std::array<PicaResolvedDrawRecord, 1U> draws{ draw };
    PicaResolvedDrawStreamView stream{
        kPicaResolvedDrawStreamSchemaVersion,
        41U,
        draws,
        bindings,
    };

    ASSERT_TRUE(stream.Available());
    const auto resolved = stream.BindingsFor(stream.Draws.front());
    ASSERT_EQ(resolved.size(), 2U);
    EXPECT_EQ(resolved.data(), bindings.data());

    draws.front().VertexBindingCount = 3U;
    EXPECT_FALSE(stream.Available());
    EXPECT_TRUE(stream.BindingsFor(stream.Draws.front()).empty());
}

TEST(Renderer3dsPicaSceneCapabilities,
     ProjectsSharedPicaSemanticsWithoutTitleIdentityOrPayloadCopies) {
    PicaResolvedDrawRecord draw;
    draw.SubmissionId = 73U;
    draw.GeometryIdentity = 101U;
    draw.GeometryContentVersion = 103U;
    draw.GeometryIdentityAvailable = true;
    draw.GeometryBuffer = { 0x2000U, 4096U };
    draw.UniformBuffer = { 0x3000U, 2048U };
    draw.VertexUniformOffset = 64U;
    draw.VertexUniformSize = 128U;
    draw.VertexUniformContentVersion = 107U;
    draw.CanonicalDescriptorSchemaVersion = 1U;
    draw.CanonicalFullRegisterStateId = 109U;
    draw.Raster.SchemaVersion =
        kPicaSceneResolvedRasterStateSchemaVersion;
    draw.RenderTarget.SchemaVersion =
        kPicaSceneRenderTargetStateSchemaVersion;
    draw.Material.SchemaVersion =
        kPicaSceneResolvedMaterialStateSchemaVersion;
    draw.Textures[0].NativeImageHandle = 0x4000U;
    draw.Textures[0].NativeContentHash = 113U;
    draw.Textures[0].NativeBaseLevelContentHash = 127U;
    draw.Textures[0].PhysicalAddress = 0x18000000U;
    draw.Textures[0].Bound = true;
    draw.Textures[0].NativeContentHashAvailable = true;
    draw.Textures[0].NativeBaseLevelContentHashAvailable = true;
    draw.NativeLighting.Available = true;
    draw.NativeLighting.Enabled = true;
    draw.NativeFragmentLighting.Available = true;
    draw.NativeFog.Available = true;
    draw.NativeTransform.ProgramAvailable = true;
    draw.NativeTransform.CurrentAvailable = true;
    draw.NativeSkeleton.ProgramAvailable = true;
    draw.NativeSkeleton.CurrentAvailable = true;

    std::array<PicaResolvedDrawRecord, 1U> draws{ draw };
    const PicaResolvedDrawStreamView stream{
        kPicaResolvedDrawStreamSchemaVersion,
        41U,
        draws,
        {},
    };
    const auto temporalSample = BuildPicaFrameTemporalSample(
        { PicaVisualInterpolationMode::Fixed3x, 30U, 90U, 3U, true },
        PicaFrameTemporalSampleKind::Transition,
        39U, 40U, 5U, 2.0F / 3.0F);
    PicaPerspectiveCameraState camera;
    camera.Serial = 17U;
    camera.CameraAvailable = true;
    auto currentViewFamily = BuildMonoPicaViewFamily(camera);
    auto previousViewFamily = currentViewFamily;
    previousViewFamily.Views[0].PoseVersion = 16U;
    const PicaSemanticSceneView view{
        kPicaSemanticSceneViewSchemaVersion,
        41U,
        7U,
        stream,
        &temporalSample,
        &currentViewFamily,
        &previousViewFamily,
    };

    ASSERT_TRUE(view.Available());
    EXPECT_TRUE(view.TemporalSampleAvailable());
    EXPECT_TRUE(view.CurrentViewFamilyAvailable());
    EXPECT_TRUE(view.ViewFamilyHistoryAvailable());
    EXPECT_EQ(view.TemporalSample->SampleOrdinal, 2U);
    EXPECT_EQ(view.CurrentViewFamily->Views[0].PoseVersion, 17U);
    EXPECT_EQ(view.Draws().data(), draws.data());
    const auto semantics = view.DescribeDraw(0U);
    ASSERT_TRUE(semantics.has_value());
    EXPECT_EQ(semantics->PicaDraw, draws.data());
    EXPECT_EQ(semantics->SubmissionId, 73U);
    EXPECT_TRUE(semantics->Geometry.Available());
    EXPECT_EQ(semantics->Geometry.Identity, 101U);
    EXPECT_TRUE(semantics->Material.ResolvedStateAvailable());
    EXPECT_TRUE(semantics->Textures[0].Image.Available());
    EXPECT_TRUE(semantics->VertexUniforms.Available());
    EXPECT_EQ(semantics->Lighting, &draws[0].NativeLighting);
    EXPECT_EQ(semantics->FragmentLighting,
              &draws[0].NativeFragmentLighting);
    EXPECT_EQ(semantics->Fog, &draws[0].NativeFog);
    EXPECT_EQ(semantics->Transform, &draws[0].NativeTransform);
    EXPECT_EQ(semantics->Skeleton, &draws[0].NativeSkeleton);
    EXPECT_EQ(semantics->CurrentTransform,
              PicaSemanticAvailability::Available);
    EXPECT_EQ(semantics->CurrentSkeleton,
              PicaSemanticAvailability::Available);
    EXPECT_FALSE(view.DescribeDraw(1U).has_value());

    draws[0].VertexBindingCount = 1U;
    EXPECT_FALSE(view.Available());
    EXPECT_FALSE(view.DescribeDraw(0U).has_value());
}

TEST(Renderer3dsPicaSceneCapabilities,
     CompilesAndAuthorizesTitleNeutralPicaCompositionBoundaries) {
    const PicaCompositionTargetReference target{
        5U, 0x18000000U, 0x18100000U, 400U, 240U, 0U, 3U };
    const std::array draws{
        PicaCompositionDrawReference{
            71U,
            target,
            PicaCompositionDomain::Scene,
            { PicaCompositionLayer::OpaqueWorld,
              PicaCompositionProvenance::NativeCmbDrawPass,
              0x1000U,
              0U },
        },
        PicaCompositionDrawReference{
            72U,
            target,
            PicaCompositionDomain::Scene,
            { PicaCompositionLayer::TransparentWorld,
              PicaCompositionProvenance::NativeCmbDrawPass,
              0x1000U,
              1U },
        },
    };

    PicaCompositionSchedule schedule;
    std::string error;
    ASSERT_TRUE(schedule.Compile(
        { kPicaCompositionSequenceSchemaVersion, 17U, draws }, &error))
        << error;
    const auto* anchor = schedule.FindAnchorBeforeDraw(
        ::Fast::Renderer::ExtensionStage::BeforeTransparent,
        target, 72U);
    ASSERT_NE(anchor, nullptr);

    auto boundary = BuildPicaExtensionScheduleBoundary(schedule, *anchor);
    EXPECT_TRUE(boundary.StructurallyValid());
    EXPECT_FALSE(boundary.Reached);
    EXPECT_EQ(ResolvePicaDisplayComposition(schedule, target).Domain,
              PicaCompositionDomain::Unknown);
    ASSERT_TRUE(schedule.ConsumeDraw(draws.front(), &error)) << error;
    boundary = BuildPicaExtensionScheduleBoundary(schedule, *anchor);
    EXPECT_TRUE(boundary.Reached);
    EXPECT_EQ(boundary.Surface,
              BuildPicaExtensionSurfaceIdentity(target));
    ASSERT_TRUE(schedule.ConsumeDraw(draws.back(), &error)) << error;
    const auto display = ResolvePicaDisplayComposition(schedule, target);
    EXPECT_EQ(display.SequenceId, 17U);
    EXPECT_EQ(display.Domain, PicaCompositionDomain::Scene);
    EXPECT_TRUE(display.SceneResolved);

    auto fabricated = *anchor;
    ++fabricated.BeforeSubmissionId;
    EXPECT_FALSE(BuildPicaExtensionScheduleBoundary(schedule, fabricated)
                     .StructurallyValid());
}

TEST(Renderer3dsPicaSceneCapabilities,
     AuthorizesGeometryProvidersFromSharedPicaCapabilitiesAndStage) {
    const auto requirements =
        BuildPicaGeometryProviderSceneRequirements();
    const auto plan = BuildPicaGeometryProviderAuthorizationPlan(
        ::Fast::Renderer::ExtensionStage::BeforeTransparent,
        requirements);
    ASSERT_TRUE(plan.Valid());
    ASSERT_EQ(plan.Requirements().size(), requirements.size());

    std::array<uint32_t, 3U> payloads{ 1U, 2U, 3U };
    std::array<::Fast::Renderer::ExtensionSceneCapabilityPublication, 3U>
        publications{
            BuildPicaSceneCapabilityPublication(
                PicaSceneCapability::ResolvedDrawStream, &payloads[0],
                requirements[0].MinimumSchemaVersion, 7U, 11U),
            BuildPicaSceneCapabilityPublication(
                PicaSceneCapability::SemanticSceneView, &payloads[1],
                requirements[1].MinimumSchemaVersion, 7U, 11U),
            BuildPicaSceneCapabilityPublication(
                PicaSceneCapability::PerspectiveCamera, &payloads[2],
                requirements[2].MinimumSchemaVersion, 7U, 11U),
        };
    const ::Fast::Renderer::ExtensionScenePublicationTableView scene(
        11U, publications);

    EXPECT_TRUE(plan.ResolveInputs(scene).Authorized());
    EXPECT_EQ(plan.ResolveInvocation(
                  scene,
                  ::Fast::Renderer::ExtensionStage::AfterOpaque,
                  true)
                  .Status,
              PicaGeometryProviderAuthorizationStatus::StageMismatch);
    EXPECT_EQ(plan.ResolveInvocation(
                  scene,
                  ::Fast::Renderer::ExtensionStage::BeforeTransparent,
                  false)
                  .Status,
              PicaGeometryProviderAuthorizationStatus::
                  WorldGeometryUnavailable);
    EXPECT_TRUE(plan.ResolveInvocation(
                        scene,
                        ::Fast::Renderer::ExtensionStage::BeforeTransparent,
                        true)
                    .Authorized());

    publications.back() = {};
    const auto missing = plan.ResolveInputs(
        ::Fast::Renderer::ExtensionScenePublicationTableView(
            11U, publications));
    EXPECT_EQ(missing.Status,
              PicaGeometryProviderAuthorizationStatus::
                  SceneCapabilitiesUnavailable);
    EXPECT_EQ(missing.Capabilities.Status,
              ::Fast::Renderer::
                  ExtensionSceneCapabilityResolutionStatus::
                      MissingCapability);
}

} // namespace
