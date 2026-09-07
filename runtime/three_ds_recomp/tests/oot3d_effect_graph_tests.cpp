#include "fast/oot3d/display_effect_plan.h"
#include "fast/oot3d/display_effect_resources.h"
#include "fast/oot3d/effect_geometry_provider_plan.h"
#include "fast/oot3d/effect_graph.h"
#include "fast/oot3d/effect_graph_pass_barriers.h"
#include "fast/oot3d/effect_graph_physical_plan.h"
#include "fast/oot3d/hiz_depth_pyramid.h"
#include "fast/oot3d/pica_composition_schedule.h"
#include "fast/oot3d/pica_display_composition.h"
#include "fast/oot3d/pica_extension_schedule.h"
#include "fast/oot3d/pica_guide_sampling_barriers.h"
#include "fast/oot3d/pica_scene_publication_adapter.h"
#include "fast/oot3d/upscaler_contract.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <type_traits>

namespace {

using namespace Fast::Oot3d;

static_assert(std::is_same_v<
              Fast::Oot3d::PicaCompositionSchedule,
              Fast::Renderer3ds::PicaCompositionSchedule>);
static_assert(std::is_same_v<
              Fast::Oot3d::PicaDisplayComposition,
              Fast::Renderer3ds::PicaDisplayComposition>);

[[nodiscard]] EffectResourceUse Read(EffectResource resource) {
    return { resource, EffectResourceAccess::Read };
}

[[nodiscard]] EffectResourceUse Write(EffectResource resource) {
    return { resource, EffectResourceAccess::Write };
}

struct GeometryProviderSceneFixture {
    uint32_t DrawPayload = 1U;
    uint32_t ViewPayload = 2U;
    uint32_t CameraPayload = 3U;
    uint64_t FrameId = 17U;
    std::array<::Fast::Renderer::ExtensionSceneCapabilityPublication, 3U>
        Publications{};

    void Publish(bool draws = true, bool view = true, bool camera = true,
                 uint32_t drawSchema =
                     ::Fast::Renderer3ds::
                         kPicaResolvedDrawStreamSchemaVersion) {
        Publications.fill({});
        if (draws) {
            Publications[0] =
                ::Fast::Renderer3ds::BuildPicaSceneCapabilityPublication(
                    ::Fast::Renderer3ds::PicaSceneCapability::ResolvedDrawStream,
                    &DrawPayload, drawSchema, 1U, FrameId);
        }
        if (view) {
            Publications[1] =
                ::Fast::Renderer3ds::BuildPicaSceneCapabilityPublication(
                    ::Fast::Renderer3ds::PicaSceneCapability::SemanticSceneView,
                    &ViewPayload,
                    ::Fast::Renderer3ds::
                        kPicaSemanticSceneViewSchemaVersion,
                    2U, FrameId);
        }
        if (camera) {
            Publications[2] =
                ::Fast::Renderer3ds::BuildPicaSceneCapabilityPublication(
                    ::Fast::Renderer3ds::PicaSceneCapability::PerspectiveCamera,
                    &CameraPayload,
                    ::Fast::Renderer3ds::kPicaPerspectiveCameraSchemaVersion,
                    2U, FrameId);
        }
    }

    [[nodiscard]] ::Fast::Renderer::ExtensionScenePublicationTableView
    View() const {
        return ::Fast::Renderer::ExtensionScenePublicationTableView{
            FrameId, Publications };
    }

    [[nodiscard]] EffectGeometryProviderInputs Inputs() const {
        return { View() };
    }
};

[[nodiscard]] ::Oot3d::Renderer::PicaCompositionDrawReference
CompositionDraw(
    uint64_t submissionId,
    const ::Oot3d::Renderer::PicaCompositionTargetReference& target,
    ::Oot3d::Renderer::PicaCompositionDomain domain,
    ::Oot3d::Renderer::PicaCompositionLayer layer,
    ::Oot3d::Renderer::PicaCompositionProvenance provenance,
    uint32_t sourcePc, uint32_t nativeValue = 0U) {
    return {
        submissionId,
        target,
        domain,
        {layer, provenance, sourcePc, nativeValue},
    };
}

[[nodiscard]] ::Oot3d::Renderer::PicaDrawView DrawViewFor(
    const ::Oot3d::Renderer::PicaCompositionDrawReference& draw) {
    ::Oot3d::Renderer::PicaDrawView view;
    view.SubmissionId = draw.SubmissionId;
    view.RenderTargetNamespace = draw.Target.RenderTargetNamespace;
    view.FramebufferColorPhysicalAddress =
        draw.Target.ColorPhysicalAddress;
    view.FramebufferDepthPhysicalAddress =
        draw.Target.DepthPhysicalAddress;
    view.FramebufferWidth = draw.Target.FramebufferWidth;
    view.FramebufferHeight = draw.Target.FramebufferHeight;
    view.FramebufferColorFormat = draw.Target.ColorFormat;
    view.FramebufferDepthFormat = draw.Target.DepthFormat;
    view.CompositionDomain = draw.Domain;
    view.Composition = draw.Composition;
    return view;
}

TEST(Oot3dEffectGraph, PreservesDependencyOrderForUntypedConsumers) {
    EffectGraph graph;
    EXPECT_TRUE(graph.Add({ "depth", {} }));
    EXPECT_TRUE(graph.Add({ "ao", { "depth" } }));
    EXPECT_TRUE(graph.Add({ "composite", { "ao" } }));
    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.Valid()) << compiled.Error;
    EXPECT_EQ(compiled.Order, (std::vector<std::string>{ "depth", "ao", "composite" }));

    graph.Clear();
    graph.Add({ "a", { "b" } });
    graph.Add({ "b", { "a" } });
    EXPECT_FALSE(graph.Compile().Valid());
}

TEST(Oot3dEffectGraph, CompilesTypedStagesResourcesAndLifetimes) {
    EffectGraph graph;
    graph.Add({ .Name = "guides",
                .Stage = EffectStage::NativeLighting,
                .Contract = EffectContractKind::AuxiliaryOutput,
                .Resources = { Write(EffectResource::NormalGuide) } });
    graph.Add({ .Name = "ao",
                .DependsOn = { "guides" },
                .Stage = EffectStage::AfterOpaque,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = { Read(EffectResource::NativeDepth), Read(EffectResource::NormalGuide),
                               Write(EffectResource::AmbientOcclusion) } });
    graph.Add({ .Name = "composite",
                .DependsOn = { "ao" },
                .Stage = EffectStage::AfterTransparent,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = { Read(EffectResource::SceneColor), Read(EffectResource::AmbientOcclusion),
                               Write(EffectResource::CompositeColor) } });

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.Valid()) << compiled.Error;
    EXPECT_EQ(compiled.Order, (std::vector<std::string>{ "guides", "ao", "composite" }));
    EXPECT_TRUE(compiled.Attachments.Requires(PicaAuxiliaryOutput::NormalGuide));
    EXPECT_EQ(compiled.Attachments.ColorAttachmentCount(), 7U);
    ASSERT_NE(compiled.FindPass("ao"), nullptr);
    EXPECT_EQ(compiled.FindPass("ao")->Stage, EffectStage::AfterOpaque);
    const auto lifetime = compiled.FindLifetime(EffectResource::AmbientOcclusion);
    ASSERT_TRUE(lifetime.has_value());
    EXPECT_EQ(lifetime->ReadCount, 1U);
    EXPECT_EQ(lifetime->WriteCount, 1U);
    EXPECT_LT(lifetime->FirstUse, lifetime->LastUse);
    ASSERT_EQ(compiled.Barriers.size(), 4U);
    const auto guideBarrier =
        std::find_if(compiled.Barriers.begin(), compiled.Barriers.end(), [](const EffectResourceBarrier& barrier) {
            return barrier.Resource == EffectResource::NormalGuide;
        });
    ASSERT_NE(guideBarrier, compiled.Barriers.end());
    ASSERT_TRUE(guideBarrier->ProducerPass.has_value());
    EXPECT_EQ(*guideBarrier->ProducerPass, 0U);
    EXPECT_EQ(guideBarrier->ConsumerPass, 1U);
    EXPECT_EQ(guideBarrier->Before, EffectResourceAccess::Write);
    EXPECT_EQ(guideBarrier->After, EffectResourceAccess::Read);

    const auto depthBarrier =
        std::find_if(compiled.Barriers.begin(), compiled.Barriers.end(), [](const EffectResourceBarrier& barrier) {
            return barrier.Resource == EffectResource::NativeDepth;
        });
    ASSERT_NE(depthBarrier, compiled.Barriers.end());
    EXPECT_FALSE(depthBarrier->ProducerPass.has_value());
    EXPECT_EQ(depthBarrier->ConsumerPass, 1U);
}

TEST(Oot3dEffectGraph, RejectsContractAndResourceHazardViolations) {
    EffectGraph observer;
    observer.Add({ .Name = "observer",
                   .Stage = EffectStage::SceneResolved,
                   .Contract = EffectContractKind::Observer,
                   .Resources = { Write(EffectResource::SceneColor) } });
    EXPECT_FALSE(observer.Compile().Valid());

    EffectGraph auxiliary;
    auxiliary.Add({ .Name = "badAux",
                    .Stage = EffectStage::NativeLighting,
                    .Contract = EffectContractKind::AuxiliaryOutput,
                    .Resources = { Write(EffectResource::SceneColor) } });
    EXPECT_FALSE(auxiliary.Compile().Valid());

    EffectGraph missingProducer;
    missingProducer.Add({ .Name = "consumer",
                          .Stage = EffectStage::AfterOpaque,
                          .Contract = EffectContractKind::ComposerPass,
                          .Resources = { Read(EffectResource::ReflectionColor) } });
    EXPECT_FALSE(missingProducer.Compile().Valid());

    EffectGraph backwardsDependency;
    backwardsDependency.Add({ .Name = "late", .Stage = EffectStage::AfterTransparent });
    backwardsDependency.Add({ .Name = "early", .DependsOn = { "late" }, .Stage = EffectStage::AfterOpaque });
    EXPECT_FALSE(backwardsDependency.Compile().Valid());

    EffectGraph missingExport;
    EXPECT_TRUE(missingExport.Export(EffectResource::ReflectionColor));
    EXPECT_FALSE(missingExport.Compile().Valid());

    EffectGraph geometryWithoutOutput;
    geometryWithoutOutput.Add({
        .Name = "geometry",
        .Stage = EffectStage::BeforeTransparent,
        .Contract = EffectContractKind::GeometryProvider,
        .Resources = { Read(EffectResource::PicaSceneFrame) },
    });
    EXPECT_FALSE(geometryWithoutOutput.Compile().Valid());

    EffectGraph geometryWritingImage;
    geometryWritingImage.Add({
        .Name = "geometry",
        .Stage = EffectStage::BeforeTransparent,
        .Contract = EffectContractKind::GeometryProvider,
        .Resources = { Write(EffectResource::ExtensionGeometry),
                       Write(EffectResource::CompositeColor) },
    });
    EXPECT_FALSE(geometryWritingImage.Compile().Valid());

    EffectGraph lightingWritingImage;
    lightingWritingImage.Add({
        .Name = "lighting",
        .Stage = EffectStage::NativeLighting,
        .Contract = EffectContractKind::LightingContributor,
        .Resources = { Read(EffectResource::DirectionalShadowHistory),
                       Write(EffectResource::CompositeColor) },
    });
    EXPECT_FALSE(lightingWritingImage.Compile().Valid());
}

TEST(Oot3dUpscalerContract, PreservesTheActualPicaInputDomain) {
    const auto displayDomain = ResolveUpscalerContractFromRender(
        UpscalerProvider::Nis, UpscalerQuality::Quality, 480U, 853U);
    EXPECT_EQ(displayDomain.Render, (UpscalerExtent{480U, 853U}));
    EXPECT_EQ(displayDomain.Output, (UpscalerExtent{720U, 1280U}));

    const auto guideDomain = ResolveUpscalerContractFromRender(
        UpscalerProvider::Nis, UpscalerQuality::Quality, 960U, 853U);
    EXPECT_EQ(guideDomain.Render, (UpscalerExtent{960U, 853U}));
    EXPECT_EQ(guideDomain.Output, (UpscalerExtent{1440U, 1280U}));
}

TEST(Oot3dEffectGraph, ExportsExtensionOwnedAuxiliaryOutputs) {
    EffectGraph graph;
    graph.Add({ .Name = "shadow-map",
                .Stage = EffectStage::AfterOpaque,
                .Contract = EffectContractKind::AuxiliaryOutput,
                .Resources = { Read(EffectResource::PicaSceneFrame),
                               Write(EffectResource::DirectionalShadowMap) } });
    EXPECT_TRUE(graph.Export(EffectResource::DirectionalShadowMap));
    EXPECT_FALSE(graph.Export(EffectResource::DirectionalShadowMap));

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.Valid()) << compiled.Error;
    EXPECT_TRUE(compiled.ExportsResource(
        EffectResource::DirectionalShadowMap));
    const auto lifetime = compiled.FindLifetime(
        EffectResource::DirectionalShadowMap);
    ASSERT_TRUE(lifetime.has_value());
    EXPECT_EQ(lifetime->WriteCount, 1U);
    EXPECT_EQ(lifetime->ReadCount, 1U);
    EXPECT_EQ(lifetime->LastUse, compiled.Passes.size());
}

TEST(Oot3dEffectGraph, CompiledPassReportsOnlyDeclaredReads) {
    EffectGraph graph;
    graph.Add({ .Name = "producer",
                .Stage = EffectStage::AfterOpaque,
                .Contract = EffectContractKind::AuxiliaryOutput,
                .Resources = {
                    Write(EffectResource::AmbientOcclusion) } });
    graph.Add({ .Name = "consumer",
                .DependsOn = { "producer" },
                .Stage = EffectStage::AfterTransparent,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = {
                    Read(EffectResource::AmbientOcclusion),
                    Write(EffectResource::CompositeColor) } });

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.Valid()) << compiled.Error;
    const auto* consumer = compiled.FindPass("consumer");
    ASSERT_NE(consumer, nullptr);
    EXPECT_TRUE(consumer->ReadsResource(
        EffectResource::AmbientOcclusion));
    EXPECT_FALSE(consumer->ReadsResource(
        EffectResource::CompositeColor));
    EXPECT_FALSE(consumer->ReadsResource(
        EffectResource::NativeDepth));
}

TEST(Oot3dEffectGraph, DerivesFrameAttachmentsFromExtensionContracts) {
    const auto canonical = BuildPicaExtensionGraph({});
    ASSERT_TRUE(canonical.Valid()) << canonical.Error;
    EXPECT_TRUE(canonical.Order.empty());
    EXPECT_TRUE(canonical.Attachments.NativeColorOnly());

    const PicaAttachmentFeatureRequests requests{
        .DirectionalShadows = true,
        .AmbientOcclusion = true,
        .ToonOutline = true,
        .Reflections = true,
        .TemporalReconstruction = true,
        .InteractiveGrass = true,
    };
    const auto extended = BuildPicaExtensionGraph(requests);
    ASSERT_TRUE(extended.Valid()) << extended.Error;
    EXPECT_EQ(extended.Attachments.AuxiliaryOutputs, kAllPicaAuxiliaryOutputs);
    EXPECT_TRUE(PicaExtensionPassEnabled(
        extended, PicaExtensionPass::Guides));
    EXPECT_TRUE(PicaExtensionPassEnabled(
        extended, PicaExtensionPass::DirectionalShadowMap));
    EXPECT_TRUE(PicaExtensionPassEnabled(
        extended, PicaExtensionPass::DirectionalShadowLighting));
    EXPECT_TRUE(PicaExtensionPassEnabled(
        extended, PicaExtensionPass::InteractiveGrass));
    EXPECT_EQ(extended.FindPass(PicaExtensionPassName(
                  PicaExtensionPass::InteractiveGrass))->Contract,
              EffectContractKind::GeometryProvider);
    EXPECT_TRUE(extended.ExportsResource(
        EffectResource::DirectionalShadowMap));
    EXPECT_TRUE(extended.ExportsResource(
        EffectResource::ExtensionGeometry));

    const auto shadows = BuildPicaExtensionGraph({ .DirectionalShadows = true });
    ASSERT_TRUE(shadows.Valid()) << shadows.Error;
    EXPECT_TRUE(shadows.Attachments.NativeColorOnly());
    const auto* shadowLighting = shadows.FindPass(PicaExtensionPassName(
        PicaExtensionPass::DirectionalShadowLighting));
    ASSERT_NE(shadowLighting, nullptr);
    EXPECT_EQ(shadowLighting->Stage, EffectStage::NativeLighting);
    EXPECT_EQ(shadowLighting->Contract,
              EffectContractKind::LightingContributor);
    EXPECT_EQ(shadowLighting->Resources,
              (std::vector<EffectResourceUse>{
                  Read(EffectResource::NativeSceneView),
                  Read(EffectResource::DirectionalShadowHistory) }));
    const auto* shadowMap = shadows.FindPass(PicaExtensionPassName(
        PicaExtensionPass::DirectionalShadowMap));
    ASSERT_NE(shadowMap, nullptr);
    EXPECT_EQ(shadowMap->Stage, EffectStage::AfterOpaque);
    EXPECT_EQ(shadowMap->Contract, EffectContractKind::AuxiliaryOutput);
    EXPECT_TRUE(shadows.ExportsResource(
        EffectResource::DirectionalShadowMap));

    const auto reflections = BuildPicaExtensionGraph({ .Reflections = true });
    ASSERT_TRUE(reflections.Valid()) << reflections.Error;
    EXPECT_EQ(reflections.Attachments.AuxiliaryOutputs,
              PicaAuxiliaryOutput::NormalGuide | PicaAuxiliaryOutput::MaterialGuide);

    const auto temporal = BuildPicaExtensionGraph({ .TemporalReconstruction = true });
    ASSERT_TRUE(temporal.Valid()) << temporal.Error;
    EXPECT_EQ(temporal.Attachments.AuxiliaryOutputs,
              PicaAuxiliaryOutput::MaterialGuide | PicaAuxiliaryOutput::RigidMotionGuide);

    const auto outline = BuildPicaExtensionGraph({ .ToonOutline = true });
    ASSERT_TRUE(outline.Valid()) << outline.Error;
    EXPECT_EQ(outline.Attachments.AuxiliaryOutputs,
              PicaAuxiliaryOutput::NormalGuide | PicaAuxiliaryOutput::RigidMotionGuide | PicaAuxiliaryOutput::FogGuide |
                  PicaAuxiliaryOutput::OutlineGeometryGuide);
    EXPECT_TRUE(outline.FindPass(PicaExtensionPassName(
                    PicaExtensionPass::ToonOutline))
                    ->ReadsResource(EffectResource::RigidMotionGuide));
    EXPECT_TRUE(outline.FindPass(PicaExtensionPassName(PicaExtensionPass::ToonOutline))
                    ->ReadsResource(EffectResource::NormalGuide));

    const auto grass = BuildPicaExtensionGraph({ .InteractiveGrass = true });
    ASSERT_TRUE(grass.Valid()) << grass.Error;
    EXPECT_TRUE(grass.Attachments.NativeColorOnly());
    EXPECT_FALSE(PicaExtensionPassEnabled(
        grass, PicaExtensionPass::Guides));
    const auto* grassPass = grass.FindPass(PicaExtensionPassName(
        PicaExtensionPass::InteractiveGrass));
    ASSERT_NE(grassPass, nullptr);
    EXPECT_EQ(grassPass->Stage, EffectStage::BeforeTransparent);
    EXPECT_EQ(grassPass->Resources,
              (std::vector<EffectResourceUse>{
                  Read(EffectResource::PicaSceneFrame),
                  Read(EffectResource::NativeSceneView),
                  Write(EffectResource::ExtensionGeometry) }));
    EXPECT_TRUE(grass.ExportsResource(
        EffectResource::ExtensionGeometry));
}

TEST(Oot3dGeometryProviderPlan,
     AuthorizesOnlyTheCompiledStageAndDeclaredInputs) {
    const auto graph = BuildPicaExtensionGraph({
        .InteractiveGrass = true,
    });
    ASSERT_TRUE(graph.Valid()) << graph.Error;
    const auto plan = BuildEffectGeometryProviderPlan(
        graph, PicaExtensionPassName(
                   PicaExtensionPass::InteractiveGrass));
    ASSERT_TRUE(plan.Valid());
    EXPECT_EQ(plan.Stage(), EffectStage::BeforeTransparent);
    EXPECT_TRUE(plan.Requires(EffectResource::PicaSceneFrame));
    EXPECT_TRUE(plan.Requires(EffectResource::NativeSceneView));
    EXPECT_FALSE(plan.Requires(EffectResource::NormalGuide));
    EXPECT_FALSE(plan.Requires(EffectResource::MaterialGuide));
    EXPECT_FALSE(plan.Requires(EffectResource::AmbientGuide));
    EXPECT_TRUE(plan.ProducesExtensionGeometry());
    EXPECT_TRUE(plan.ExportsExtensionGeometry());

    GeometryProviderSceneFixture scene;
    scene.Publish();
    EffectGeometryProviderInvocation invocation{
        .Kind = EffectGeometryProviderInvocationKind::DeclaredBoundary,
        .Stage = EffectStage::BeforeTransparent,
        .Inputs = scene.Inputs(),
        .WorldGeometryObserved = true,
    };
    EXPECT_TRUE(plan.ResolveInvocation(invocation).Authorized());

    invocation.Stage = EffectStage::AfterTransparent;
    EXPECT_EQ(plan.ResolveInvocation(invocation).Reason,
              EffectGeometryProviderDecisionReason::StageMismatch);
    invocation.Stage = EffectStage::BeforeTransparent;
    scene.Publish(false, true, true);
    invocation.Inputs = scene.Inputs();
    EXPECT_EQ(plan.ResolveInvocation(invocation).Reason,
              EffectGeometryProviderDecisionReason::PicaSceneFrameUnavailable);
    scene.Publish(true, false, true);
    invocation.Inputs = scene.Inputs();
    EXPECT_EQ(plan.ResolveInvocation(invocation).Reason,
              EffectGeometryProviderDecisionReason::NativeSceneViewUnavailable);
    scene.Publish();
    invocation.Inputs = scene.Inputs();
    invocation.WorldGeometryObserved = false;
    EXPECT_EQ(plan.ResolveInvocation(invocation).Reason,
              EffectGeometryProviderDecisionReason::WorldGeometryUnavailable);

    scene.Publish(
        true, true, true,
        ::Fast::Renderer3ds::kPicaResolvedDrawStreamSchemaVersion + 1U);
    invocation.Inputs = scene.Inputs();
    invocation.WorldGeometryObserved = true;
    EXPECT_EQ(plan.ResolveInvocation(invocation).Reason,
              EffectGeometryProviderDecisionReason::SceneCapabilitySchemaMismatch);
}

TEST(Oot3dGeometryProviderPlan,
     DisabledSettingsCannotAuthorizeAnAbsentCompiledPass) {
    const auto graph = BuildPicaExtensionGraph({});
    ASSERT_TRUE(graph.Valid()) << graph.Error;
    const auto plan = BuildEffectGeometryProviderPlan(
        graph, PicaExtensionPassName(
                   PicaExtensionPass::InteractiveGrass));
    EXPECT_FALSE(plan.Valid());
    GeometryProviderSceneFixture scene;
    scene.Publish();
    EXPECT_EQ(plan.ResolveInputs(scene.Inputs()).Reason,
              EffectGeometryProviderDecisionReason::PlanUnavailable);
    EXPECT_FALSE(plan.ResolveInvocation({
        .Kind = EffectGeometryProviderInvocationKind::DeclaredBoundary,
        .Stage = EffectStage::BeforeTransparent,
        .Inputs = scene.Inputs(),
        .WorldGeometryObserved = true,
    }).Authorized());
}

TEST(Oot3dGeometryProviderPlan,
     AccountsDeclaredAndFallbackExecutionWithoutAllocationState) {
    const auto graph = BuildPicaExtensionGraph({
        .InteractiveGrass = true,
    });
    const auto plan = BuildEffectGeometryProviderPlan(
        graph, PicaExtensionPassName(
                   PicaExtensionPass::InteractiveGrass));
    ASSERT_TRUE(plan.Valid());

    EffectGeometryProviderExecutionLedger ledger;
    ledger.Reset(plan);
    GeometryProviderSceneFixture scene;
    scene.Publish();
    const auto inputs = plan.ResolveInputs(scene.Inputs());
    ledger.RecordSourceInspection(inputs, true);
    ledger.RecordSourceInspection(inputs, false);

    const EffectGeometryProviderInvocation declared{
        .Kind = EffectGeometryProviderInvocationKind::DeclaredBoundary,
        .Stage = EffectStage::BeforeTransparent,
        .Inputs = scene.Inputs(),
        .WorldGeometryObserved = true,
    };
    ledger.RecordInvocation(
        declared.Kind, plan.ResolveInvocation(declared),
        EffectGeometryProviderExecutionOutcome::Executed);
    auto fallback = declared;
    fallback.Kind =
        EffectGeometryProviderInvocationKind::NativeComposerFallback;
    ledger.RecordInvocation(
        fallback.Kind, plan.ResolveInvocation(fallback),
        EffectGeometryProviderExecutionOutcome::Reused);

    const auto& summary = ledger.Summary();
    EXPECT_TRUE(summary.Declared);
    EXPECT_EQ(summary.SourceInspectionCount, 2U);
    EXPECT_EQ(summary.SourcePublishedCount, 1U);
    EXPECT_EQ(summary.DeclaredBoundaryInvocationCount, 1U);
    EXPECT_EQ(summary.FallbackInvocationCount, 1U);
    EXPECT_EQ(summary.ExecutedCount, 1U);
    EXPECT_EQ(summary.ReusedCount, 1U);
    EXPECT_EQ(summary.SkippedCount, 0U);
    EXPECT_EQ(summary.FailedCount, 0U);
    EXPECT_EQ(summary.InputUnavailableCount, 0U);
    EXPECT_EQ(summary.ScheduleRejectedCount, 0U);
    EXPECT_TRUE(summary.Successful());
}

TEST(Oot3dDisplayEffectPlan, DeclaresItsActualTypedInputs) {
    DisplayEffectPlanInput input;
    input.WorldSurface = true;
    input.PerspectiveAvailable = true;
    input.CameraAvailable = true;
    input.AmbientOcclusion = AmbientOcclusionMode::Cacao;
    input.Reflections = ReflectionMode::FidelityFxSssr;
    input.Toon = ToonMode::PicaMaterial;
    input.OutlineEnabled = true;
    input.AntiAliasing = AntiAliasingMode::Taa;

    const auto plan = BuildDisplayEffectPlan(input);
    ASSERT_TRUE(plan.Graph.Valid()) << plan.Graph.Error;
    EXPECT_TRUE(plan.RequiresGuideTarget);
    EXPECT_TRUE(plan.Enabled(DisplayEffectPass::Cacao));
    EXPECT_TRUE(plan.Enabled(DisplayEffectPass::WorkingColor));
    EXPECT_TRUE(plan.Enabled(DisplayEffectPass::Reflections));
    EXPECT_TRUE(plan.Enabled(DisplayEffectPass::Motion));
    EXPECT_TRUE(plan.Enabled(DisplayEffectPass::Taa));
    EXPECT_FALSE(plan.Enabled(DisplayEffectPass::Nis));
    EXPECT_EQ(plan.ReflectionProvider, ReflectionMode::FidelityFxSssr);
    EXPECT_EQ(plan.Graph.Attachments.AuxiliaryOutputs, kAllPicaAuxiliaryOutputs);
    EXPECT_EQ(plan.Graph.FindPass("Guides")->Contract, EffectContractKind::AuxiliaryOutput);
    EXPECT_EQ(plan.Graph.FindPass("Scanout")->Stage, EffectStage::AfterUi);
    ASSERT_EQ(plan.Bindings(DisplayEffectPass::Cacao).size(), 4U);
    EXPECT_EQ(plan.Bindings(DisplayEffectPass::Cacao).front().Resource,
              EffectResource::NativeSceneView);
    const auto reflections = plan.Bindings(
        DisplayEffectPass::Reflections);
    EXPECT_EQ(reflections.size(), 9U);
    const auto declares = [reflections](EffectResource resource,
                                       EffectResourceAccess access) {
        return std::find(reflections.begin(), reflections.end(),
                         EffectResourceUse{resource, access}) !=
               reflections.end();
    };
    EXPECT_TRUE(declares(EffectResource::PicaSceneFrame,
                         EffectResourceAccess::Read));
    EXPECT_TRUE(declares(EffectResource::NativeSceneView,
                         EffectResourceAccess::Read));
    EXPECT_TRUE(declares(EffectResource::NativeDepth,
                         EffectResourceAccess::Read));
    EXPECT_TRUE(declares(EffectResource::MotionVectors,
                         EffectResourceAccess::Read));
    EXPECT_TRUE(declares(EffectResource::LinearWorkingColor,
                         EffectResourceAccess::Read));
    EXPECT_TRUE(declares(EffectResource::ReflectionColor,
                         EffectResourceAccess::Write));
    const auto composite = plan.Bindings(DisplayEffectPass::Composite);
    EXPECT_NE(std::find(composite.begin(), composite.end(),
                        Read(EffectResource::AmbientGuide)),
              composite.end());
    EXPECT_NE(std::find(composite.begin(), composite.end(),
                        Read(EffectResource::MaterialGuide)),
              composite.end());
    EXPECT_NE(std::find(composite.begin(), composite.end(),
                        Read(EffectResource::NativeDepth)),
              composite.end());
    EXPECT_NE(std::find(composite.begin(), composite.end(), Read(EffectResource::OutlineGeometryGuide)),
              composite.end());
    EXPECT_NE(std::find(composite.begin(), composite.end(), Read(EffectResource::NormalGuide)),
              composite.end());
    EXPECT_EQ(std::find(composite.begin(), composite.end(),
                        Read(EffectResource::OutlineColor)),
              composite.end());
    size_t bindingCount = 0U;
    for (const auto& pass : plan.Graph.Passes) {
        bindingCount += pass.Resources.size();
    }
    EXPECT_EQ(plan.DeclaredBindingCount(), bindingCount);

    input.AmbientOcclusion = AmbientOcclusionMode::Off;
    input.Toon = ToonMode::Off;
    input.OutlineEnabled = false;
    input.AntiAliasing = AntiAliasingMode::Off;
    const auto directSssr = BuildDisplayEffectPlan(input);
    ASSERT_TRUE(directSssr.Graph.Valid()) << directSssr.Graph.Error;
    EXPECT_TRUE(directSssr.Enabled(DisplayEffectPass::WorkingColor));
    EXPECT_EQ(directSssr.Bindings(DisplayEffectPass::Scanout).front(),
              Read(EffectResource::LinearWorkingColor));

    input.Reflections = ReflectionMode::HiZ;
    const auto hiZ = BuildDisplayEffectPlan(input);
    ASSERT_TRUE(hiZ.Graph.Valid()) << hiZ.Graph.Error;
    EXPECT_FALSE(hiZ.Enabled(DisplayEffectPass::WorkingColor));
    const auto hiZReflections = hiZ.Bindings(
        DisplayEffectPass::Reflections);
    EXPECT_EQ(hiZReflections.size(), 6U);
    EXPECT_NE(std::find(hiZReflections.begin(), hiZReflections.end(),
                        Read(EffectResource::NativeSceneView)),
              hiZReflections.end());
    EXPECT_EQ(std::find(hiZReflections.begin(), hiZReflections.end(),
                        Read(EffectResource::MotionVectors)),
              hiZReflections.end());
    EXPECT_EQ(std::find(hiZReflections.begin(), hiZReflections.end(),
                        Write(EffectResource::LinearWorkingColor)),
              hiZReflections.end());

    input.Reflections = ReflectionMode::Off;
    input.AmbientOcclusion = AmbientOcclusionMode::Cacao;
    const auto directCacao = BuildDisplayEffectPlan(input);
    ASSERT_TRUE(directCacao.Graph.Valid()) << directCacao.Graph.Error;
    const auto directCacaoScanout = directCacao.Bindings(
        DisplayEffectPass::Scanout);
    EXPECT_NE(std::find(directCacaoScanout.begin(),
                        directCacaoScanout.end(),
                        Read(EffectResource::AmbientOcclusion)),
              directCacaoScanout.end());
    EXPECT_NE(std::find(directCacaoScanout.begin(),
                        directCacaoScanout.end(),
                        Read(EffectResource::AmbientGuide)),
              directCacaoScanout.end());

    input = {};
    input.AlphaOverlay = true;
    const auto overlay = BuildDisplayEffectPlan(input);
    ASSERT_TRUE(overlay.Graph.Valid()) << overlay.Graph.Error;
    EXPECT_TRUE(overlay.Graph.Attachments.NativeColorOnly());
    EXPECT_FALSE(overlay.RequiresGuideTarget);
    EXPECT_FALSE(overlay.Enabled(DisplayEffectPass::Cacao));
    EXPECT_FALSE(overlay.Enabled(DisplayEffectPass::WorkingColor));
    EXPECT_TRUE(overlay.Enabled(DisplayEffectPass::Scanout));
    EXPECT_TRUE(overlay.Bindings(DisplayEffectPass::Cacao).empty());
}

TEST(Oot3dDisplayEffectPlan, AccountsForEveryDeclaredExecutionOutcome) {
    DisplayEffectPlanInput input;
    input.WorldSurface = true;
    input.PerspectiveAvailable = true;
    input.CameraAvailable = true;
    input.AntiAliasing = AntiAliasingMode::Taa;
    const auto plan = BuildDisplayEffectPlan(input);
    ASSERT_TRUE(plan.Graph.Valid()) << plan.Graph.Error;
    const auto workingBindings =
        plan.Bindings(DisplayEffectPass::WorkingColor);
    ASSERT_EQ(workingBindings.size(), 2U);
    EXPECT_EQ(workingBindings[0], Read(EffectResource::SceneColor));
    EXPECT_EQ(workingBindings[1],
              Write(EffectResource::LinearWorkingColor));
    EXPECT_EQ(plan.Bindings(DisplayEffectPass::Taa).front(),
              Read(EffectResource::LinearWorkingColor));

    DisplayEffectExecutionLedger ledger(plan);
    EXPECT_TRUE(ledger.Record(DisplayEffectPass::Guides,
                              DisplayEffectExecutionOutcome::Executed));
    EXPECT_TRUE(ledger.Record(DisplayEffectPass::Motion,
                              DisplayEffectExecutionOutcome::Executed));
    EXPECT_TRUE(ledger.Record(DisplayEffectPass::WorkingColor,
                              DisplayEffectExecutionOutcome::Executed));
    EXPECT_TRUE(ledger.Record(DisplayEffectPass::Taa,
                              DisplayEffectExecutionOutcome::Reused));
    EXPECT_TRUE(ledger.Record(DisplayEffectPass::Scanout,
                              DisplayEffectExecutionOutcome::Fused));

    const auto summary = ledger.Summary();
    EXPECT_TRUE(summary.FullyAccounted());
    EXPECT_TRUE(summary.Successful());
    EXPECT_EQ(summary.DeclaredPassCount, 5U);
    EXPECT_EQ(summary.ExecutedPassCount, 3U);
    EXPECT_EQ(summary.ReusedPassCount, 1U);
    EXPECT_EQ(summary.FusedPassCount, 1U);
    EXPECT_EQ(summary.SkippedPassCount, 0U);
    EXPECT_EQ(summary.FailedPassCount, 0U);
    EXPECT_EQ(summary.UnresolvedPassCount, 0U);
    EXPECT_EQ(summary.UndeclaredRecordCount, 0U);
    EXPECT_EQ(summary.DuplicateRecordCount, 0U);
    EXPECT_GT(summary.ActiveBindingCount, 0U);

    DisplayEffectExecutionLedger invalidLedger(plan);
    EXPECT_FALSE(invalidLedger.Record(
        DisplayEffectPass::Nis,
        DisplayEffectExecutionOutcome::Executed));
    EXPECT_TRUE(invalidLedger.Record(
        DisplayEffectPass::Taa,
        DisplayEffectExecutionOutcome::Executed));
    EXPECT_FALSE(invalidLedger.Record(
        DisplayEffectPass::Taa,
        DisplayEffectExecutionOutcome::Executed));
    const auto invalid = invalidLedger.Summary();
    EXPECT_FALSE(invalid.FullyAccounted());
    EXPECT_EQ(invalid.UndeclaredRecordCount, 1U);
    EXPECT_EQ(invalid.DuplicateRecordCount, 1U);
}

TEST(Oot3dDisplayEffectPlan, DeclaresFsrDepthPreparation) {
    DisplayEffectPlanInput input;
    input.WorldSurface = true;
    input.PerspectiveAvailable = true;
    input.CameraAvailable = true;
    input.AntiAliasing = AntiAliasingMode::Upscaler;
    input.Upscaler = UpscalerProvider::Fsr;
    const auto plan = BuildDisplayEffectPlan(input);
    ASSERT_TRUE(plan.Graph.Valid()) << plan.Graph.Error;
    EXPECT_TRUE(plan.Enabled(DisplayEffectPass::HiZ));
    EXPECT_TRUE(plan.Enabled(DisplayEffectPass::WorkingColor));
    EXPECT_TRUE(plan.Enabled(DisplayEffectPass::TemporalUpscaler));
    const auto bindings = plan.Bindings(DisplayEffectPass::TemporalUpscaler);
    EXPECT_NE(std::find(bindings.begin(), bindings.end(),
                        Read(EffectResource::HierarchicalDepth)),
              bindings.end());
    EXPECT_EQ(std::find(bindings.begin(), bindings.end(),
                        Read(EffectResource::NativeDepth)),
              bindings.end());
    EXPECT_NE(std::find(bindings.begin(), bindings.end(),
                        Read(EffectResource::NativeSceneView)),
              bindings.end());
    EXPECT_NE(std::find(bindings.begin(), bindings.end(),
                        Read(EffectResource::MotionVectors)),
              bindings.end());
    EXPECT_NE(std::find(bindings.begin(), bindings.end(),
                        Read(EffectResource::ReactiveMask)),
              bindings.end());
    EXPECT_NE(std::find(bindings.begin(), bindings.end(),
                        Read(EffectResource::LinearWorkingColor)),
              bindings.end());
}

TEST(Oot3dDisplayEffectPlan, KeepsNonWorldDisplaysCanonical) {
    DisplayEffectPlanInput input;
    input.PerspectiveAvailable = true;
    input.CameraAvailable = true;
    input.ForceTaa = true;
    input.AmbientOcclusion = AmbientOcclusionMode::Cacao;
    input.Reflections = ReflectionMode::HiZ;
    input.Toon = ToonMode::PicaMaterial;
    input.OutlineEnabled = true;
    input.AntiAliasing = AntiAliasingMode::Upscaler;
    input.Upscaler = UpscalerProvider::Fsr;
    const auto plan = BuildDisplayEffectPlan(input);
    ASSERT_TRUE(plan.Graph.Valid()) << plan.Graph.Error;
    EXPECT_EQ(plan.Graph.Order,
              (std::vector<std::string>{ "Scanout" }));
    EXPECT_TRUE(plan.Graph.Attachments.NativeColorOnly());
}

TEST(Oot3dDisplayEffectResources, ValidatesConcretePassReads) {
    DisplayEffectPlanInput input;
    input.WorldSurface = true;
    input.PerspectiveAvailable = true;
    input.CameraAvailable = true;
    input.AntiAliasing = AntiAliasingMode::Taa;
    const auto plan = BuildDisplayEffectPlan(input);
    ASSERT_TRUE(plan.Graph.Valid()) << plan.Graph.Error;

    DisplayEffectResourceTable resources;
    EXPECT_TRUE(resources.BindImage(
        EffectResource::SceneColor, 0x100U, 0x101U, 37U,
        400U, 240U, 1U, SceneColorEncoding::Srgb));
    EXPECT_TRUE(resources.BindImage(
        EffectResource::NativeDepth, 0x200U, 0x201U, 126U,
        400U, 240U, 1U));
    EXPECT_TRUE(resources.BindImage(
        EffectResource::MotionVectors, 0x300U, 0x301U, 97U,
        400U, 240U, 2U));
    EXPECT_TRUE(resources.BindImage(
        EffectResource::LinearWorkingColor, 0x350U, 0x351U, 97U,
        400U, 240U, 2U, SceneColorEncoding::Linear));
    ASSERT_NE(resources.Find(EffectResource::SceneColor), nullptr);
    EXPECT_TRUE(resources.Find(EffectResource::SceneColor)->SrgbColor());
    EXPECT_FALSE(resources.Find(EffectResource::SceneColor)->LinearColor());
    ASSERT_NE(resources.Find(EffectResource::NativeDepth), nullptr);
    EXPECT_EQ(resources.Find(EffectResource::NativeDepth)->ColorEncoding,
              SceneColorEncoding::Unknown);

    auto validation = resources.ValidateReads(
        plan.Bindings(DisplayEffectPass::Taa));
    EXPECT_TRUE(validation.Complete());
    EXPECT_EQ(validation.DeclaredReadCount, 3U);
    EXPECT_EQ(validation.ResolvedReadCount, 3U);
    EXPECT_EQ(validation.MissingReadCount, 0U);

    resources.Unbind(EffectResource::MotionVectors);
    validation = resources.ValidateReads(
        plan.Bindings(DisplayEffectPass::Taa));
    EXPECT_FALSE(validation.Complete());
    EXPECT_EQ(validation.MissingReadCount, 1U);
    EXPECT_NE(validation.MissingReadMask &
                  (1U << static_cast<uint32_t>(
                      EffectResource::MotionVectors)),
              0U);

    EXPECT_TRUE(resources.BindImage(
        EffectResource::TemporalColor, 0x400U, 0x401U, 97U,
        400U, 240U, 3U, SceneColorEncoding::Linear));
    const auto scanout = resources.ValidateReads(
        plan.Bindings(DisplayEffectPass::Scanout));
    EXPECT_TRUE(scanout.Complete());
    EXPECT_EQ(resources.BoundCount(), 4U);
}

TEST(Oot3dDisplayEffectResources,
     ResolvedReadSetExposesOnlyDeclaredInputs) {
    DisplayEffectResourceTable resources;
    ASSERT_TRUE(resources.BindImage(
        EffectResource::SceneColor, 0x100U, 0x101U, 37U,
        400U, 240U, 1U, SceneColorEncoding::Srgb));
    ASSERT_TRUE(resources.BindImage(
        EffectResource::NativeDepth, 0x200U, 0x201U, 126U,
        400U, 240U, 1U));
    ASSERT_TRUE(resources.BindImage(
        EffectResource::NormalGuide, 0x300U, 0x301U, 37U,
        400U, 240U, 1U));

    const std::array uses{
        EffectResourceUse{EffectResource::SceneColor,
                          EffectResourceAccess::Read},
        EffectResourceUse{EffectResource::NativeDepth,
                          EffectResourceAccess::Read},
        EffectResourceUse{EffectResource::NormalGuide,
                          EffectResourceAccess::Write},
    };
    auto resolved = resources.ResolveReads(uses);
    ASSERT_TRUE(resolved.Complete());
    EXPECT_TRUE(resolved.Declares(EffectResource::SceneColor));
    EXPECT_TRUE(resolved.Declares(EffectResource::NativeDepth));
    EXPECT_FALSE(resolved.Declares(EffectResource::NormalGuide));
    EXPECT_NE(resolved.Find(EffectResource::SceneColor), nullptr);
    EXPECT_NE(resolved.Find(EffectResource::NativeDepth), nullptr);
    EXPECT_EQ(resolved.Find(EffectResource::NormalGuide), nullptr);

    resources.Unbind(EffectResource::NativeDepth);
    resolved = resources.ResolveReads(uses);
    EXPECT_FALSE(resolved.Complete());
    EXPECT_EQ(resolved.Find(EffectResource::NativeDepth), nullptr);
    EXPECT_EQ(resolved.Validation.MissingReadCount, 1U);
}

TEST(Oot3dCompositionSchedule,
     PreservesNativeRunsAndPublishesOnlyProvenWorldBoundaries) {
    using namespace ::Oot3d::Renderer;
    const PicaCompositionTargetReference world{
        7U, 0x18000000U, 0x18100000U, 400U, 240U, 0U, 3U};
    const PicaCompositionTargetReference ui{
        7U, 0x18200000U, 0x18300000U, 320U, 240U, 0U, 3U};
    const std::vector<PicaCompositionDrawReference> draws{
        CompositionDraw(10U, world, PicaCompositionDomain::Scene,
                        PicaCompositionLayer::Atmosphere,
                        PicaCompositionProvenance::NativeControlFlow,
                        0x003FB5ECU),
        CompositionDraw(11U, world, PicaCompositionDomain::Scene,
                        PicaCompositionLayer::TransparentWorld,
                        PicaCompositionProvenance::NativeCmbDrawPass,
                        0x0030F4D0U, 1U),
        CompositionDraw(12U, world, PicaCompositionDomain::Scene,
                        PicaCompositionLayer::Atmosphere,
                        PicaCompositionProvenance::NativeControlFlow,
                        0x003FB5ECU),
        CompositionDraw(13U, world, PicaCompositionDomain::Scene,
                        PicaCompositionLayer::OpaqueWorld,
                        PicaCompositionProvenance::NativeCmbDrawPass,
                        0x0030F4D0U),
        CompositionDraw(14U, world, PicaCompositionDomain::Scene,
                        PicaCompositionLayer::OpaqueWorld,
                        PicaCompositionProvenance::NativeCmbDrawPass,
                        0x0030F4D0U),
        CompositionDraw(15U, world, PicaCompositionDomain::Scene,
                        PicaCompositionLayer::TransparentWorld,
                        PicaCompositionProvenance::NativeCmbDrawPass,
                        0x0030F4D0U, 1U),
        CompositionDraw(16U, world, PicaCompositionDomain::Scene,
                        PicaCompositionLayer::TransparentWorld,
                        PicaCompositionProvenance::NativeCmbDrawPass,
                        0x0030F4D0U, 1U),
        CompositionDraw(17U, world, PicaCompositionDomain::Scene,
                        PicaCompositionLayer::Atmosphere,
                        PicaCompositionProvenance::NativeControlFlow,
                        0x003FB5ECU),
        CompositionDraw(18U, ui, PicaCompositionDomain::Ui,
                        PicaCompositionLayer::Ui,
                        PicaCompositionProvenance::NativeUiLifecycle,
                        0x00400000U),
    };

    PicaCompositionSchedule schedule;
    std::string error;
    ASSERT_TRUE(schedule.Compile({
        .SequenceId = 91U,
        .Draws = draws,
    }, &error)) << error;
    EXPECT_TRUE(schedule.Valid());
    EXPECT_EQ(schedule.SequenceId(), 91U);
    ASSERT_EQ(schedule.Runs().size(), 7U);
    EXPECT_EQ(schedule.Runs()[3].FirstDrawIndex, 3U);
    EXPECT_EQ(schedule.Runs()[3].DrawCount, 2U);
    EXPECT_EQ(schedule.Runs()[3].Composition.Layer,
              PicaCompositionLayer::OpaqueWorld);

    const auto* beforeOpaque =
        schedule.FindAnchor(EffectStage::BeforeOpaque, world);
    const auto* beforeTransparent =
        schedule.FindAnchor(EffectStage::BeforeTransparent, world);
    const auto* afterTransparent =
        schedule.FindAnchor(EffectStage::AfterTransparent, world);
    const auto* atmosphere =
        schedule.FindAnchor(EffectStage::Atmosphere, world);
    const auto* beforeUi =
        schedule.FindAnchor(EffectStage::BeforeUi, world);
    ASSERT_NE(beforeOpaque, nullptr);
    ASSERT_NE(beforeTransparent, nullptr);
    ASSERT_NE(afterTransparent, nullptr);
    ASSERT_NE(atmosphere, nullptr);
    ASSERT_NE(beforeUi, nullptr);
    EXPECT_EQ(beforeOpaque->BeforeSubmissionId, 13U);
    EXPECT_EQ(beforeTransparent->BeforeSubmissionId, 15U);
    EXPECT_EQ(afterTransparent->BeforeSubmissionId, 17U);
    EXPECT_EQ(atmosphere->BeforeSubmissionId, 17U);
    EXPECT_EQ(beforeUi->BeforeSubmissionId, 18U);
    ASSERT_NE(schedule.FindTarget(world), nullptr);
    EXPECT_EQ(schedule.FindTarget(world)->WorldStageIssue,
              PicaCompositionScheduleIssue::None);
    ASSERT_NE(schedule.FindTarget(ui), nullptr);
    EXPECT_EQ(schedule.FindTarget(ui)->WorldStageIssue,
              PicaCompositionScheduleIssue::NoSceneDomain);
    EXPECT_EQ(schedule.FindAnchorBeforeDraw(
                  EffectStage::BeforeTransparent, world, 15U),
              beforeTransparent);

    const auto prematureWorld = ResolvePicaDisplayComposition(
        schedule, world);
    EXPECT_EQ(prematureWorld.Domain, PicaCompositionDomain::Unknown);
    EXPECT_FALSE(prematureWorld.SceneResolved);
    const auto uiDisplay = ResolvePicaDisplayComposition(schedule, ui);
    EXPECT_EQ(uiDisplay.Domain, PicaCompositionDomain::Ui);
    EXPECT_FALSE(uiDisplay.SceneResolved);
    EXPECT_FALSE(schedule.Reached(*beforeUi));
    for (size_t index = 0U; index + 1U < draws.size(); ++index) {
        EXPECT_TRUE(schedule.ConsumeDraw(
            BuildPicaCompositionDrawReference(DrawViewFor(draws[index])),
            &error))
            << error;
    }
    EXPECT_TRUE(schedule.Reached(*beforeUi));
    const auto resolvedWorld = ResolvePicaDisplayComposition(
        schedule, world);
    EXPECT_EQ(resolvedWorld.SequenceId, 91U);
    EXPECT_EQ(resolvedWorld.SourceTarget, world);
    EXPECT_EQ(resolvedWorld.Domain, PicaCompositionDomain::Scene);
    EXPECT_TRUE(resolvedWorld.SceneResolved);
    EXPECT_TRUE(schedule.ConsumeDraw(
        BuildPicaCompositionDrawReference(DrawViewFor(draws.back())),
        &error))
        << error;
    const auto stats = schedule.Stats();
    EXPECT_EQ(stats.DrawCount, draws.size());
    EXPECT_EQ(stats.ConsumedDrawCount, draws.size());
    EXPECT_EQ(stats.TargetCount, 2U);
    EXPECT_EQ(stats.WorldStageTargetCount, 1U);
    EXPECT_EQ(stats.NonSceneTargetCount, 1U);
    EXPECT_EQ(stats.MissingOpaqueWorldTargetCount, 0U);
    EXPECT_EQ(stats.ExecutionMismatchCount, 0U);
}

TEST(Oot3dCompositionSchedule,
     WithholdsAmbiguousWorldAnchorsInsteadOfInferringFromRenderState) {
    using namespace ::Oot3d::Renderer;
    const PicaCompositionTargetReference target{
        3U, 0x19000000U, 0x19100000U, 400U, 240U, 0U, 3U};
    std::vector<PicaCompositionDrawReference> draws{
        CompositionDraw(20U, target, PicaCompositionDomain::Scene,
                        PicaCompositionLayer::OpaqueWorld,
                        PicaCompositionProvenance::NativeCmbDrawPass,
                        0x0030F4D0U),
        CompositionDraw(21U, target, PicaCompositionDomain::Scene,
                        PicaCompositionLayer::TransparentWorld,
                        PicaCompositionProvenance::NativeCmbDrawPass,
                        0x0030F4D0U, 1U),
        CompositionDraw(22U, target, PicaCompositionDomain::Scene,
                        PicaCompositionLayer::OpaqueWorld,
                        PicaCompositionProvenance::NativeCmbDrawPass,
                        0x0030F4D0U),
    };
    PicaCompositionSchedule schedule;
    std::string error;
    ASSERT_TRUE(schedule.Compile({
        .SequenceId = 92U,
        .Draws = draws,
    }, &error)) << error;
    ASSERT_EQ(schedule.Targets().size(), 1U);
    EXPECT_EQ(schedule.Targets()[0].WorldStageIssue,
              PicaCompositionScheduleIssue::NonContiguousOpaqueWorld);
    EXPECT_EQ(schedule.FindAnchor(
                  EffectStage::BeforeTransparent, target),
              nullptr);

    const auto* prefix = schedule.FindPublishedGeometryAnchor(target);
    ASSERT_NE(prefix, nullptr);
    EXPECT_EQ(prefix->BeforeSubmissionId, 21U);
    EXPECT_EQ(prefix->Stage, EffectStage::BeforeTransparent);
    EXPECT_FALSE(prefix->AtTargetEnd);
    EXPECT_FALSE(schedule.Reached(*prefix));
    EXPECT_TRUE(schedule.ConsumeDraw(draws.front(), &error)) << error;
    EXPECT_TRUE(schedule.Reached(*prefix));
    EXPECT_EQ(schedule.FindAnchor(EffectStage::AfterOpaque, target), nullptr);

    EXPECT_FALSE(ResolvePicaDisplayComposition(schedule, target).SceneResolved);
    ASSERT_TRUE(schedule.ConsumeDraw(draws[1], &error));
    EXPECT_FALSE(ResolvePicaDisplayComposition(schedule, target).SceneResolved);
    ASSERT_TRUE(schedule.ConsumeDraw(draws[2], &error));
    EXPECT_TRUE(ResolvePicaDisplayComposition(schedule, target).SceneResolved);
    EXPECT_EQ(schedule.FindAnchor(EffectStage::AfterOpaque, target), nullptr);

    auto withUi = draws;
    withUi.push_back(CompositionDraw(23U, target, PicaCompositionDomain::Ui,
                                    PicaCompositionLayer::Ui,
                                    PicaCompositionProvenance::NativeCmbDrawPass, 0x0030F4D0U));
    ASSERT_TRUE(schedule.Compile({.SequenceId = 94U, .Draws = withUi}, &error));
    for (const auto& draw : withUi) ASSERT_TRUE(schedule.ConsumeDraw(draw, &error));
    EXPECT_FALSE(ResolvePicaDisplayComposition(schedule, target).SceneResolved);

    draws[1].Composition = {};
    ASSERT_TRUE(schedule.Compile({
        .SequenceId = 93U,
        .Draws = draws,
    }, &error)) << error;
    EXPECT_EQ(schedule.Targets()[0].WorldStageIssue,
              PicaCompositionScheduleIssue::UnknownLayer);
    EXPECT_EQ(schedule.FindPublishedGeometryAnchor(target), nullptr);
    EXPECT_EQ(schedule.FindAnchor(
                  EffectStage::BeforeTransparent, target),
              nullptr);
}

TEST(Oot3dCompositionSchedule,
     RepresentsAProvenBoundaryAtTargetEndWithoutFabricatingANextDraw) {
    using namespace ::Oot3d::Renderer;
    const PicaCompositionTargetReference target{
        5U, 0x1A000000U, 0x1A100000U, 400U, 240U, 0U, 3U};
    const std::vector<PicaCompositionDrawReference> draws{
        CompositionDraw(30U, target, PicaCompositionDomain::Scene,
                        PicaCompositionLayer::OpaqueWorld,
                        PicaCompositionProvenance::NativeCmbDrawPass,
                        0x0030F4D0U),
    };
    PicaCompositionSchedule schedule;
    std::string error;
    ASSERT_TRUE(schedule.Compile({
        .SequenceId = 94U,
        .Draws = draws,
    }, &error)) << error;
    const auto* anchor = schedule.FindTargetEndAnchor(
        EffectStage::BeforeTransparent, target);
    ASSERT_NE(anchor, nullptr);
    EXPECT_TRUE(anchor->AtTargetEnd);
    EXPECT_EQ(anchor->BeforeSubmissionId, 0U);
    EXPECT_EQ(anchor->BeforeDrawIndex, draws.size());
    const auto* geometryEnd = schedule.FindPublishedGeometryAnchor(target);
    ASSERT_NE(geometryEnd, nullptr);
    EXPECT_TRUE(geometryEnd->AtTargetEnd);
    EXPECT_EQ(geometryEnd->BeforeDrawIndex, anchor->BeforeDrawIndex);

    auto wrong = DrawViewFor(draws.front());
    wrong.Composition.Layer = PicaCompositionLayer::Atmosphere;
    EXPECT_FALSE(schedule.ConsumeDraw(
        BuildPicaCompositionDrawReference(wrong), &error));
    EXPECT_EQ(schedule.Stats().ExecutionMismatchCount, 1U);
    EXPECT_EQ(ResolvePicaDisplayComposition(schedule, target).Domain,
              PicaCompositionDomain::Unknown);
    EXPECT_TRUE(schedule.ConsumeDraw(
        BuildPicaCompositionDrawReference(DrawViewFor(draws.front())),
        &error))
        << error;
    const auto resolved = ResolvePicaDisplayComposition(schedule, target);
    EXPECT_EQ(resolved.Domain, PicaCompositionDomain::Scene);
    EXPECT_TRUE(resolved.SceneResolved);
}

TEST(Oot3dCompositionSchedule,
     AdaptsOnlyTheExactNativeAnchorToThePortablePassContract) {
    using namespace ::Oot3d::Renderer;
    const PicaCompositionTargetReference target{
        9U, 0x1B000000U, 0x1B100000U, 400U, 240U, 0U, 3U};
    const std::vector<PicaCompositionDrawReference> draws{
        CompositionDraw(40U, target, PicaCompositionDomain::Scene,
                        PicaCompositionLayer::OpaqueWorld,
                        PicaCompositionProvenance::NativeCmbDrawPass,
                        0x0030F4D0U),
        CompositionDraw(41U, target, PicaCompositionDomain::Scene,
                        PicaCompositionLayer::TransparentWorld,
                        PicaCompositionProvenance::NativeCmbDrawPass,
                        0x0030F4D0U, 1U),
    };
    PicaCompositionSchedule schedule;
    std::string error;
    ASSERT_TRUE(schedule.Compile({.SequenceId = 95U, .Draws = draws},
                                 &error))
        << error;
    const auto* anchor = schedule.FindAnchorBeforeDraw(
        EffectStage::AfterOpaque, target, 41U);
    ASSERT_NE(anchor, nullptr);

    const auto graph = BuildPicaExtensionGraph({.DirectionalShadows = true});
    const auto plan = BuildPicaEffectPassSchedulePlan(
        graph,
        PicaExtensionPassName(PicaExtensionPass::DirectionalShadowMap),
        EffectContractKind::AuxiliaryOutput);
    ASSERT_TRUE(plan.Valid());
    EXPECT_EQ(plan.Stage(), EffectStage::AfterOpaque);

    auto boundary = BuildPicaExtensionScheduleBoundary(schedule, *anchor);
    EXPECT_FALSE(boundary.Reached);
    EXPECT_EQ(plan.Resolve(boundary).Reason,
              ::Fast::Renderer::ExtensionPassScheduleDecisionReason::
                  BoundaryNotReached);

    EXPECT_TRUE(schedule.ConsumeDraw(
        BuildPicaCompositionDrawReference(DrawViewFor(draws.front())),
        &error))
        << error;
    boundary = BuildPicaExtensionScheduleBoundary(schedule, *anchor);
    ASSERT_TRUE(plan.Resolve(boundary).Authorized());
    EXPECT_EQ(boundary.Surface,
              BuildPicaExtensionSurfaceIdentity(target));

    auto fabricated = *anchor;
    ++fabricated.BeforeSubmissionId;
    EXPECT_FALSE(BuildPicaExtensionScheduleBoundary(schedule, fabricated)
                     .StructurallyValid());
}

TEST(Oot3dDisplayEffectResources,
     ResolvedReadSetPreservesDeclaredColorPriority) {
    DisplayEffectResourceTable resources;
    ASSERT_TRUE(resources.BindImage(
        EffectResource::ReflectionColor, 0x100U, 0x101U, 97U,
        400U, 240U, 1U, SceneColorEncoding::Linear));
    ASSERT_TRUE(resources.BindImage(
        EffectResource::LinearWorkingColor, 0x200U, 0x201U, 97U,
        400U, 240U, 1U, SceneColorEncoding::Linear));

    const std::array uses{
        EffectResourceUse{EffectResource::LinearWorkingColor,
                          EffectResourceAccess::Read},
        EffectResourceUse{EffectResource::ReflectionColor,
                          EffectResourceAccess::Read},
    };
    const auto resolved = resources.ResolveReads(uses);
    ASSERT_TRUE(resolved.Complete());
    const auto* color = resolved.FindFirstColorRead(uses);
    ASSERT_NE(color, nullptr);
    EXPECT_EQ(color->Resource, EffectResource::LinearWorkingColor);
}

TEST(Oot3dDisplayEffectResources, ResolvesMotionImageAndSceneViewReads) {
    DisplayEffectPlanInput input;
    input.WorldSurface = true;
    input.PerspectiveAvailable = true;
    input.CameraAvailable = true;
    input.ForceMotion = true;
    const auto plan = BuildDisplayEffectPlan(input);
    ASSERT_TRUE(plan.Graph.Valid()) << plan.Graph.Error;

    DisplayEffectResourceTable resources;
    EXPECT_TRUE(resources.BindSemantic(
        EffectResource::NativeSceneView, 0x1000U, 17U));
    EXPECT_TRUE(resources.BindImage(
        EffectResource::NativeDepth, 0x2000U, 0x2001U, 126U,
        400U, 240U, 17U));
    EXPECT_TRUE(resources.BindImage(
        EffectResource::MaterialGuide, 0x3000U, 0x3001U, 37U,
        400U, 240U, 17U));
    EXPECT_TRUE(resources.BindImage(
        EffectResource::RigidMotionGuide, 0x4000U, 0x4001U, 97U,
        400U, 240U, 17U));

    const auto validation = resources.ValidateReads(
        plan.Bindings(DisplayEffectPass::Motion));
    EXPECT_TRUE(validation.Complete());
    EXPECT_EQ(validation.DeclaredReadCount, 4U);
    EXPECT_EQ(validation.ResolvedReadCount, 4U);
    EXPECT_EQ(validation.MissingReadCount, 0U);
    const auto* sceneView = resources.Find(
        EffectResource::NativeSceneView);
    ASSERT_NE(sceneView, nullptr);
    EXPECT_EQ(sceneView->Kind, EffectResourceBindingKind::Semantic);
    EXPECT_EQ(sceneView->Identity, 0x1000U);
    EXPECT_EQ(sceneView->Generation, 17U);

    input.AntiAliasing = AntiAliasingMode::Upscaler;
    input.Upscaler = UpscalerProvider::Fsr;
    input.ForceMotion = false;
    const auto fsrPlan = BuildDisplayEffectPlan(input);
    ASSERT_TRUE(fsrPlan.Graph.Valid()) << fsrPlan.Graph.Error;
    const auto hiZValidation = resources.ValidateReads(
        fsrPlan.Bindings(DisplayEffectPass::HiZ));
    EXPECT_TRUE(hiZValidation.Complete());
    EXPECT_EQ(hiZValidation.DeclaredReadCount, 1U);
    EXPECT_EQ(hiZValidation.ResolvedReadCount, 1U);

    EXPECT_TRUE(resources.BindImage(
        EffectResource::SceneColor, 0x5000U, 0x5001U, 37U,
        400U, 240U, 17U, SceneColorEncoding::Srgb));
    EXPECT_TRUE(resources.BindImage(
        EffectResource::LinearWorkingColor,
        0x5500U, 0x5501U, 97U, 400U, 240U, 17U,
        SceneColorEncoding::Linear));
    EXPECT_TRUE(resources.BindImage(
        EffectResource::HierarchicalDepth, 0x6000U, 0x6001U, 100U,
        400U, 240U, 17U));
    EXPECT_TRUE(resources.BindImage(
        EffectResource::MotionVectors, 0x7000U, 0x7001U, 97U,
        400U, 240U, 17U));
    EXPECT_TRUE(resources.BindImage(
        EffectResource::ReactiveMask, 0x8000U, 0x8001U, 76U,
        400U, 240U, 17U));
    const auto fsrValidation = resources.ValidateReads(
        fsrPlan.Bindings(DisplayEffectPass::TemporalUpscaler));
    EXPECT_TRUE(fsrValidation.Complete());
    EXPECT_EQ(fsrValidation.DeclaredReadCount, 5U);
    EXPECT_EQ(fsrValidation.ResolvedReadCount, 5U);
}

TEST(Oot3dDisplayEffectResources, ResolvesSssrContractAndLinearOutput) {
    DisplayEffectPlanInput input;
    input.WorldSurface = true;
    input.PerspectiveAvailable = true;
    input.CameraAvailable = true;
    input.Reflections = ReflectionMode::FidelityFxSssr;
    const auto plan = BuildDisplayEffectPlan(input);
    ASSERT_TRUE(plan.Graph.Valid()) << plan.Graph.Error;

    DisplayEffectResourceTable resources;
    EXPECT_TRUE(resources.BindSemantic(
        EffectResource::PicaSceneFrame, 0x1000U, 21U));
    EXPECT_TRUE(resources.BindSemantic(
        EffectResource::NativeSceneView, 0x2000U, 34U));
    const auto bindImage = [&resources](
                               EffectResource resource,
                               uintptr_t handle, uint32_t format,
                               SceneColorEncoding encoding =
                                   SceneColorEncoding::Unknown) {
        return resources.BindImage(
            resource, handle, handle + 1U, format,
            400U, 240U, 34U, encoding);
    };
    EXPECT_TRUE(bindImage(
        EffectResource::SceneColor, 0x3000U, 37U,
        SceneColorEncoding::Srgb));
    EXPECT_TRUE(bindImage(EffectResource::NativeDepth, 0x4000U, 126U));
    EXPECT_TRUE(bindImage(EffectResource::NormalGuide, 0x5000U, 37U));
    EXPECT_TRUE(bindImage(EffectResource::MaterialGuide, 0x6000U, 37U));
    EXPECT_TRUE(bindImage(
        EffectResource::HierarchicalDepth, 0x7000U, 100U));
    EXPECT_TRUE(bindImage(EffectResource::MotionVectors, 0x8000U, 97U));
    EXPECT_TRUE(resources.BindImage(
        EffectResource::LinearWorkingColor,
        0x9000U, 0x9001U, 97U, 400U, 240U, 35U,
        SceneColorEncoding::Linear));

    const auto reflectionValidation = resources.ValidateReads(
        plan.Bindings(DisplayEffectPass::Reflections));
    EXPECT_TRUE(reflectionValidation.Complete());
    EXPECT_EQ(reflectionValidation.DeclaredReadCount, 8U);
    EXPECT_EQ(reflectionValidation.ResolvedReadCount, 8U);

    EXPECT_TRUE(resources.BindImage(
        EffectResource::ReflectionColor,
        0xA000U, 0xA001U, 97U, 400U, 240U, 35U,
        SceneColorEncoding::Linear));
    const auto scanoutValidation = resources.ValidateReads(
        plan.Bindings(DisplayEffectPass::Scanout));
    EXPECT_TRUE(scanoutValidation.Complete());
    EXPECT_EQ(scanoutValidation.ResolvedReadCount, 3U);
}

TEST(Oot3dEffectGraphPhysicalPlan, ClassifiesResolvedTaaResources) {
    DisplayEffectPlanInput input;
    input.WorldSurface = true;
    input.PerspectiveAvailable = true;
    input.CameraAvailable = true;
    input.AntiAliasing = AntiAliasingMode::Taa;
    const auto displayPlan = BuildDisplayEffectPlan(input);
    ASSERT_TRUE(displayPlan.Graph.Valid()) << displayPlan.Graph.Error;

    DisplayEffectResourceTable resources;
    const auto bindImage = [&resources](
                               EffectResource resource,
                               uintptr_t image, uint32_t format) {
        const SceneColorEncoding encoding =
            resource == EffectResource::SceneColor
                ? SceneColorEncoding::Srgb
                : EffectResourceCarriesColor(resource)
                    ? SceneColorEncoding::Linear
                    : SceneColorEncoding::Unknown;
        return resources.BindImage(
            resource, image, image + 1U, format,
            400U, 240U, 7U, encoding);
    };
    EXPECT_TRUE(resources.BindSemantic(
        EffectResource::NativeSceneView, 0x1000U, 7U));
    EXPECT_TRUE(bindImage(EffectResource::SceneColor, 0x2000U, 37U));
    EXPECT_TRUE(bindImage(EffectResource::NativeDepth, 0x3000U, 126U));
    EXPECT_TRUE(bindImage(EffectResource::MaterialGuide, 0x4000U, 37U));
    EXPECT_TRUE(bindImage(
        EffectResource::RigidMotionGuide, 0x5000U, 97U));
    EXPECT_TRUE(bindImage(EffectResource::MotionVectors, 0x6000U, 97U));
    EXPECT_TRUE(bindImage(EffectResource::ReactiveMask, 0x7000U, 76U));
    EXPECT_TRUE(bindImage(EffectResource::TemporalColor, 0x8000U, 97U));
    EXPECT_TRUE(bindImage(
        EffectResource::PresentationOutput, 0x9000U, 44U));
    EXPECT_TRUE(bindImage(
        EffectResource::LinearWorkingColor, 0xA000U, 97U));

    const auto physical = BuildEffectGraphPhysicalPlan(
        displayPlan.Graph, resources);
    const auto& summary = physical.Summary();
    EXPECT_TRUE(physical.Complete());
    EXPECT_EQ(summary.DeclaredResourceCount, 10U);
    EXPECT_EQ(summary.ResolvedResourceCount, 10U);
    EXPECT_EQ(summary.MissingResourceCount, 0U);
    EXPECT_EQ(summary.SemanticResourceCount, 1U);
    EXPECT_EQ(summary.ExternalImageResourceCount, 3U);
    EXPECT_EQ(summary.NativeAttachmentResourceCount, 2U);
    EXPECT_EQ(summary.TransientImageResourceCount, 3U);
    EXPECT_EQ(summary.TemporalHistoryResourceCount, 1U);
    EXPECT_EQ(summary.PhysicalImageCount, 9U);
    EXPECT_EQ(summary.AliasEligibleResourceCount, 3U);
    EXPECT_EQ(summary.AliasSlotCount, 3U);
    EXPECT_EQ(summary.AliasOpportunityCount, 0U);
    EXPECT_EQ(summary.PeakLiveTransientCount, 3U);

    const auto* history = physical.Find(EffectResource::TemporalColor);
    ASSERT_NE(history, nullptr);
    EXPECT_EQ(history->Residency,
              EffectPhysicalResidency::TemporalHistory);
    EXPECT_EQ(history->AliasSlot, kNoEffectAliasSlot);
}

TEST(Oot3dEffectGraphPhysicalPlan, ProposesOnlyCompatibleNonOverlappingAliases) {
    EffectGraph graph;
    graph.Add({ .Name = "reflection-write",
                .Stage = EffectStage::AfterOpaque,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = { Write(EffectResource::ReflectionColor) } });
    graph.Add({ .Name = "reflection-read",
                .DependsOn = { "reflection-write" },
                .Stage = EffectStage::AfterOpaque,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = { Read(EffectResource::ReflectionColor) } });
    graph.Add({ .Name = "composite-write",
                .DependsOn = { "reflection-read" },
                .Stage = EffectStage::AfterTransparent,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = { Write(EffectResource::CompositeColor) } });
    graph.Add({ .Name = "composite-read",
                .DependsOn = { "composite-write" },
                .Stage = EffectStage::BeforeUi,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = { Read(EffectResource::CompositeColor) } });
    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.Valid()) << compiled.Error;

    DisplayEffectResourceTable resources;
    EXPECT_TRUE(resources.BindImage(
        EffectResource::ReflectionColor, 0x100U, 0x101U, 97U,
        400U, 240U, 1U, SceneColorEncoding::Linear));
    EXPECT_TRUE(resources.BindImage(
        EffectResource::CompositeColor, 0x200U, 0x201U, 97U,
        400U, 240U, 1U, SceneColorEncoding::Linear));
    auto physical = BuildEffectGraphPhysicalPlan(compiled, resources);
    ASSERT_TRUE(physical.Complete());
    ASSERT_EQ(physical.AliasSlotCount(), 1U);
    EXPECT_EQ(physical.Summary().AliasEligibleResourceCount, 2U);
    EXPECT_EQ(physical.Summary().AliasOpportunityCount, 1U);
    EXPECT_EQ(physical.AliasSlotAt(0).ResourceCount, 2U);
    EXPECT_EQ(physical.AliasSlotAt(0).ResourceMask,
              (1U << static_cast<uint32_t>(
                   EffectResource::ReflectionColor)) |
                  (1U << static_cast<uint32_t>(
                       EffectResource::CompositeColor)));

    resources.Unbind(EffectResource::CompositeColor);
    physical = BuildEffectGraphPhysicalPlan(compiled, resources);
    EXPECT_FALSE(physical.Complete());
    EXPECT_EQ(physical.Summary().MissingResourceCount, 1U);
    EXPECT_NE(physical.Summary().MissingResourceMask &
                  (1U << static_cast<uint32_t>(
                       EffectResource::CompositeColor)),
              0U);
    EXPECT_EQ(physical.Summary().AliasEligibleResourceCount, 1U);
}

TEST(Oot3dDisplayEffectResources, RejectsAmbiguousColorBindings) {
    DisplayEffectResourceTable resources;
    EXPECT_FALSE(resources.BindImage(
        EffectResource::SceneColor, 0x100U, 0x101U, 37U,
        400U, 240U, 1U));
    EXPECT_TRUE(resources.BindImage(
        EffectResource::SceneColor, 0x100U, 0x101U, 37U,
        400U, 240U, 1U, SceneColorEncoding::Srgb));
    EXPECT_FALSE(resources.BindImage(
        EffectResource::NativeDepth, 0x200U, 0x201U, 126U,
        400U, 240U, 1U, SceneColorEncoding::Linear));
    EXPECT_TRUE(resources.BindImage(
        EffectResource::NativeDepth, 0x200U, 0x201U, 126U,
        400U, 240U, 1U));
}

TEST(Oot3dEffectGraphPhysicalPlan,
     BuildsPreDispatchAllocationSlotsFromDeclaredLifetimes) {
    EffectGraph graph;
    graph.Add({ .Name = "reflection-write",
                .Stage = EffectStage::AfterOpaque,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = { Write(EffectResource::ReflectionColor) } });
    graph.Add({ .Name = "reflection-read",
                .DependsOn = { "reflection-write" },
                .Stage = EffectStage::AfterOpaque,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = { Read(EffectResource::ReflectionColor) } });
    graph.Add({ .Name = "composite-write",
                .DependsOn = { "reflection-read" },
                .Stage = EffectStage::AfterTransparent,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = { Write(EffectResource::CompositeColor) } });
    graph.Add({ .Name = "composite-read",
                .DependsOn = { "composite-write" },
                .Stage = EffectStage::BeforeUi,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = { Read(EffectResource::CompositeColor) } });
    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.Valid()) << compiled.Error;

    const std::array<EffectTransientImageRequirement, 2> requirements{{
        {EffectResource::ReflectionColor, 97U, 400U, 240U,
         1U, 1U, 1U, 0x1U},
        {EffectResource::CompositeColor, 97U, 400U, 240U,
         1U, 1U, 1U, 0x2U},
    }};
    const auto plan = BuildEffectTransientAllocationPlan(
        compiled, requirements);
    ASSERT_TRUE(plan.Complete());
    ASSERT_EQ(plan.ResourceCount(), 2U);
    ASSERT_EQ(plan.SlotCount(), 1U);
    EXPECT_EQ(plan.Summary().RequestedResourceCount, 2U);
    EXPECT_EQ(plan.Summary().PlannedResourceCount, 2U);
    EXPECT_EQ(plan.Summary().AliasOpportunityCount, 1U);
    EXPECT_EQ(plan.Summary().PeakLiveResourceCount, 1U);
    EXPECT_EQ(plan.SlotAt(0).Usage, 0x3U);
    EXPECT_EQ(plan.SlotAt(0).ResourceCount, 2U);
    ASSERT_NE(plan.Find(EffectResource::ReflectionColor), nullptr);
    ASSERT_NE(plan.Find(EffectResource::CompositeColor), nullptr);
    EXPECT_EQ(plan.Find(EffectResource::ReflectionColor)->Slot,
              plan.Find(EffectResource::CompositeColor)->Slot);

    const std::array<EffectTransientImageRequirement, 3> invalid{{
        requirements[0],
        requirements[0],
        {EffectResource::TemporalColor, 97U, 400U, 240U,
         1U, 1U, 1U, 0x1U},
    }};
    const auto rejected = BuildEffectTransientAllocationPlan(
        compiled, invalid);
    EXPECT_FALSE(rejected.Complete());
    EXPECT_EQ(rejected.Summary().PlannedResourceCount, 1U);
    EXPECT_EQ(rejected.Summary().InvalidRequirementCount, 2U);
    EXPECT_NE(rejected.Summary().InvalidRequirementMask &
                  (1U << static_cast<uint32_t>(
                       EffectResource::ReflectionColor)),
              0U);
    EXPECT_NE(rejected.Summary().InvalidRequirementMask &
                  (1U << static_cast<uint32_t>(
                       EffectResource::TemporalColor)),
              0U);
}

TEST(Oot3dEffectGraphPhysicalPlan,
     AcceptsAnEmptyPlanForCanonicalPresentation) {
    const auto display = BuildDisplayEffectPlan({
        .WorldSurface = true,
    });
    ASSERT_TRUE(display.Graph.Valid()) << display.Graph.Error;

    const auto plan = BuildEffectTransientAllocationPlan(
        display.Graph, {});
    EXPECT_TRUE(plan.Complete());
    EXPECT_EQ(plan.ResourceCount(), 0U);
    EXPECT_EQ(plan.SlotCount(), 0U);
    EXPECT_EQ(plan.Summary().RequestedResourceCount, 0U);
    EXPECT_EQ(plan.Summary().PlannedResourceCount, 0U);
    EXPECT_EQ(plan.Summary().PhysicalSlotCount, 0U);
}

TEST(Oot3dEffectGraphPhysicalPlan,
     PreservesTheCompleteHiZMipContract) {
    DisplayEffectPlanInput input;
    input.WorldSurface = true;
    input.PerspectiveAvailable = true;
    input.Reflections = ReflectionMode::HiZ;
    const auto display = BuildDisplayEffectPlan(input);
    ASSERT_TRUE(display.Graph.Valid()) << display.Graph.Error;

    constexpr uint32_t width = 960U;
    constexpr uint32_t height = 853U;
    const uint32_t mipCount = ResolveHiZMipCount(width, height);
    ASSERT_EQ(mipCount, 10U);
    const std::array<EffectTransientImageRequirement, 1U> requirements{{
        {EffectResource::HierarchicalDepth, 100U, width, height,
         mipCount, 1U, 1U, 0x3U},
    }};
    const auto plan = BuildEffectTransientAllocationPlan(
        display.Graph, requirements);
    ASSERT_TRUE(plan.Complete());
    ASSERT_EQ(plan.ResourceCount(), 1U);
    ASSERT_EQ(plan.SlotCount(), 1U);
    const auto* hiZ = plan.Find(EffectResource::HierarchicalDepth);
    ASSERT_NE(hiZ, nullptr);
    EXPECT_EQ(hiZ->StorageClass,
              EffectTransientStorageClass::DepthPyramid);
    EXPECT_EQ(hiZ->Requirement.MipLevels, mipCount);
    EXPECT_EQ(plan.SlotAt(hiZ->Slot).MipLevels, mipCount);

    const auto layout = BuildHiZPyramidLayout(width, height);
    EXPECT_EQ(layout.Mips.size(), mipCount);
    EXPECT_EQ(layout.Mips.back().Width, 1U);
    EXPECT_EQ(layout.Mips.back().Height, 1U);
}

TEST(Oot3dEffectGraphPhysicalPlan,
     KeepsOverlappingImagesSeparateAndReportsMissingLifetimes) {
    EffectGraph graph;
    graph.Add({ .Name = "write-both",
                .Stage = EffectStage::AfterOpaque,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = {
                    Write(EffectResource::ReflectionColor),
                    Write(EffectResource::CompositeColor),
                } });
    graph.Add({ .Name = "read-both",
                .DependsOn = { "write-both" },
                .Stage = EffectStage::BeforeUi,
                .Contract = EffectContractKind::ComposerPass,
                .Resources = {
                    Read(EffectResource::ReflectionColor),
                    Read(EffectResource::CompositeColor),
                } });
    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.Valid()) << compiled.Error;

    const std::array<EffectTransientImageRequirement, 3> requirements{{
        {EffectResource::ReflectionColor, 97U, 400U, 240U,
         1U, 1U, 1U, 0x1U},
        {EffectResource::CompositeColor, 97U, 400U, 240U,
         1U, 1U, 1U, 0x1U},
        {EffectResource::MotionVectors, 97U, 400U, 240U,
         1U, 1U, 1U, 0x1U},
    }};
    const auto plan = BuildEffectTransientAllocationPlan(
        compiled, requirements);
    EXPECT_FALSE(plan.Complete());
    EXPECT_EQ(plan.ResourceCount(), 2U);
    EXPECT_EQ(plan.SlotCount(), 2U);
    EXPECT_EQ(plan.Summary().AliasOpportunityCount, 0U);
    EXPECT_EQ(plan.Summary().PeakLiveResourceCount, 2U);
    EXPECT_EQ(plan.Summary().MissingLifetimeCount, 1U);
    EXPECT_NE(plan.Summary().MissingLifetimeMask &
                  (1U << static_cast<uint32_t>(
                       EffectResource::MotionVectors)),
              0U);
}

TEST(Oot3dEffectGraphPhysicalPlan,
     AliasesActualSssrAndSmaaOutputsAcrossDisjointPasses) {
    DisplayEffectPlanInput input;
    input.WorldSurface = true;
    input.PerspectiveAvailable = true;
    input.CameraAvailable = true;
    input.SmaaAvailable = true;
    input.Reflections = ReflectionMode::FidelityFxSssr;
    input.AntiAliasing = AntiAliasingMode::Smaa1x;
    const auto display = BuildDisplayEffectPlan(input);
    ASSERT_TRUE(display.Graph.Valid()) << display.Graph.Error;

    const std::array<EffectTransientImageRequirement, 6> requirements{{
        {EffectResource::ReflectionColor, 97U, 400U, 240U,
         1U, 1U, 1U, 0x3U},
        {EffectResource::LinearWorkingColor, 97U, 400U, 240U,
         1U, 1U, 1U, 0x3U},
        {EffectResource::CompositeColor, 97U, 400U, 240U,
         1U, 1U, 1U, 0x3U},
        {EffectResource::AntiAliasedColor, 97U, 400U, 240U,
         1U, 1U, 1U, 0x3U},
        {EffectResource::MotionVectors, 97U, 400U, 240U,
         1U, 1U, 1U, 0x3U},
        {EffectResource::ReactiveMask, 76U, 400U, 240U,
         1U, 1U, 1U, 0x3U},
    }};
    const auto plan = BuildEffectTransientAllocationPlan(
        display.Graph, requirements);
    ASSERT_TRUE(plan.Complete());
    ASSERT_EQ(plan.ResourceCount(), 6U);
    ASSERT_EQ(plan.SlotCount(), 4U);
    EXPECT_EQ(plan.Summary().AliasOpportunityCount, 2U);
    EXPECT_EQ(plan.Summary().PeakLiveResourceCount, 3U);

    const auto* reflection = plan.Find(EffectResource::ReflectionColor);
    const auto* linear = plan.Find(EffectResource::LinearWorkingColor);
    const auto* composite = plan.Find(EffectResource::CompositeColor);
    const auto* antiAliased = plan.Find(EffectResource::AntiAliasedColor);
    const auto* motion = plan.Find(EffectResource::MotionVectors);
    const auto* reactive = plan.Find(EffectResource::ReactiveMask);
    ASSERT_NE(reflection, nullptr);
    ASSERT_NE(linear, nullptr);
    ASSERT_NE(composite, nullptr);
    ASSERT_NE(antiAliased, nullptr);
    ASSERT_NE(motion, nullptr);
    ASSERT_NE(reactive, nullptr);
    EXPECT_EQ(linear->Slot, antiAliased->Slot);
    EXPECT_EQ(motion->Slot, composite->Slot);
    EXPECT_NE(reflection->Slot, linear->Slot);
    EXPECT_NE(reflection->Slot, composite->Slot);
    EXPECT_NE(linear->Slot, composite->Slot);
    EXPECT_NE(reactive->Slot, motion->Slot);
}

TEST(Oot3dEffectPassBarrierPlan,
     MapsDirectionalShadowHistoryAndDepthProduction) {
    const auto graph = BuildPicaExtensionGraph({
        .DirectionalShadows = true,
    });
    ASSERT_TRUE(graph.Valid()) << graph.Error;

    const auto shadowMap = BuildEffectPassBarrierPlan(
        graph, PicaExtensionPassName(
                   PicaExtensionPass::DirectionalShadowMap));
    ASSERT_TRUE(shadowMap.Valid());
    EXPECT_EQ(shadowMap.OutputCount(), 1U);
    const auto* mapOutput = shadowMap.Find(
        EffectResource::DirectionalShadowMap);
    ASSERT_NE(mapOutput, nullptr);
    EXPECT_EQ(mapOutput->DispatchAccess,
              ResourceAccess::DepthAttachment);
    EXPECT_EQ(mapOutput->CompletionAccess,
              ResourceAccess::ShaderRead);
    EXPECT_TRUE(mapOutput->DownstreamRead);

    const auto lighting = BuildEffectPassBarrierPlan(
        graph, PicaExtensionPassName(
                   PicaExtensionPass::DirectionalShadowLighting));
    ASSERT_TRUE(lighting.Valid());
    EXPECT_EQ(lighting.OutputCount(), 0U);
    EXPECT_EQ(lighting.BeginExecution().PlannedGraphTransitionCount(), 0U);
}

TEST(Oot3dDisplayEffectResources, ResolvesTypedSemanticObjectsBySchema) {
    DisplayEffectResourceTable resources;
    const uint32_t sceneView = 73U;
    EXPECT_TRUE(resources.BindSemanticObject(
        EffectResource::NativeSceneView, &sceneView, 4U, 17U));
    const auto* binding = resources.Find(
        EffectResource::NativeSceneView);
    ASSERT_NE(binding, nullptr);
    EXPECT_EQ(binding->Identity,
              reinterpret_cast<uintptr_t>(&sceneView));
    EXPECT_EQ(binding->SemanticObject,
              reinterpret_cast<uintptr_t>(&sceneView));
    EXPECT_EQ(binding->SemanticSchemaVersion, 4U);
    EXPECT_EQ(resources.FindSemanticObject<uint32_t>(
                  EffectResource::NativeSceneView, 4U),
              &sceneView);
    EXPECT_EQ(resources.FindSemanticObject<uint32_t>(
                  EffectResource::NativeSceneView, 3U),
              nullptr);
    EXPECT_FALSE(resources.BindSemanticObject(
        EffectResource::NativeSceneView, nullptr, 4U, 18U));
    EXPECT_FALSE(resources.BindSemanticObject(
        EffectResource::NativeSceneView, &sceneView, 0U, 18U));
}

TEST(Oot3dEffectPassBarrierPlan, KeepsUnusedReactiveMaskWriteOnlyForTaa) {
    DisplayEffectPlanInput input;
    input.WorldSurface = true;
    input.PerspectiveAvailable = true;
    input.CameraAvailable = true;
    input.AntiAliasing = AntiAliasingMode::Taa;
    const auto displayPlan = BuildDisplayEffectPlan(input);
    ASSERT_TRUE(displayPlan.Graph.Valid()) << displayPlan.Graph.Error;

    const auto barriers = BuildEffectPassBarrierPlan(
        displayPlan.Graph, DisplayEffectPassName(DisplayEffectPass::Motion));
    ASSERT_TRUE(barriers.Valid());
    EXPECT_EQ(barriers.OutputCount(), 2U);
    EXPECT_EQ(barriers.ManagedOutputCount(), 2U);
    EXPECT_EQ(barriers.ShaderReadableOutputCount(), 1U);
    EXPECT_EQ(barriers.WriteOnlyOutputCount(), 1U);

    const auto* motion = barriers.Find(EffectResource::MotionVectors);
    ASSERT_NE(motion, nullptr);
    EXPECT_EQ(motion->DispatchAccess, ResourceAccess::ComputeWrite);
    EXPECT_EQ(motion->CompletionAccess, ResourceAccess::ShaderRead);
    EXPECT_TRUE(motion->DownstreamRead);

    const auto* reactive = barriers.Find(EffectResource::ReactiveMask);
    ASSERT_NE(reactive, nullptr);
    EXPECT_EQ(reactive->DispatchAccess, ResourceAccess::ComputeWrite);
    EXPECT_EQ(reactive->CompletionAccess, ResourceAccess::ComputeWrite);
    EXPECT_FALSE(reactive->DownstreamRead);
}

TEST(Oot3dEffectPassBarrierPlan, MakesBothMotionOutputsReadableForFsr) {
    DisplayEffectPlanInput input;
    input.WorldSurface = true;
    input.PerspectiveAvailable = true;
    input.CameraAvailable = true;
    input.AntiAliasing = AntiAliasingMode::Upscaler;
    input.Upscaler = UpscalerProvider::Fsr;
    const auto displayPlan = BuildDisplayEffectPlan(input);
    ASSERT_TRUE(displayPlan.Graph.Valid()) << displayPlan.Graph.Error;

    const auto barriers = BuildEffectPassBarrierPlan(
        displayPlan.Graph, DisplayEffectPassName(DisplayEffectPass::Motion));
    ASSERT_TRUE(barriers.Valid());
    EXPECT_EQ(barriers.ManagedOutputCount(), 2U);
    EXPECT_EQ(barriers.ShaderReadableOutputCount(), 2U);
    EXPECT_EQ(barriers.WriteOnlyOutputCount(), 0U);
    ASSERT_NE(barriers.Find(EffectResource::MotionVectors), nullptr);
    ASSERT_NE(barriers.Find(EffectResource::ReactiveMask), nullptr);
    EXPECT_EQ(barriers.Find(EffectResource::MotionVectors)->CompletionAccess,
              ResourceAccess::ShaderRead);
    EXPECT_EQ(barriers.Find(EffectResource::ReactiveMask)->CompletionAccess,
              ResourceAccess::ShaderRead);
}

TEST(Oot3dEffectPassBarrierPlan, MapsScanoutOutputToPresentation) {
    const auto displayPlan = BuildDisplayEffectPlan({});
    ASSERT_TRUE(displayPlan.Graph.Valid()) << displayPlan.Graph.Error;

    const auto barriers = BuildEffectPassBarrierPlan(
        displayPlan.Graph, DisplayEffectPassName(DisplayEffectPass::Scanout));
    ASSERT_TRUE(barriers.Valid());
    const auto* output = barriers.Find(EffectResource::PresentationOutput);
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->DispatchAccess, ResourceAccess::ColorAttachment);
    EXPECT_EQ(output->CompletionAccess, ResourceAccess::Present);
}

TEST(Oot3dEffectPassBarrierPlan,
     MapsCompositeAndTemporalOutputsToTheirConsumers) {
    DisplayEffectPlanInput input;
    input.WorldSurface = true;
    input.PerspectiveAvailable = true;
    input.CameraAvailable = true;
    input.AmbientOcclusion = AmbientOcclusionMode::Cacao;
    input.AntiAliasing = AntiAliasingMode::Taa;
    const auto displayPlan = BuildDisplayEffectPlan(input);
    ASSERT_TRUE(displayPlan.Graph.Valid()) << displayPlan.Graph.Error;

    const auto composite = BuildEffectPassBarrierPlan(
        displayPlan.Graph,
        DisplayEffectPassName(DisplayEffectPass::Composite));
    ASSERT_TRUE(composite.Valid());
    const auto* compositeOutput = composite.Find(
        EffectResource::CompositeColor);
    ASSERT_NE(compositeOutput, nullptr);
    EXPECT_EQ(compositeOutput->DispatchAccess,
              ResourceAccess::ComputeWrite);
    EXPECT_EQ(compositeOutput->CompletionAccess,
              ResourceAccess::ShaderRead);

    const auto taa = BuildEffectPassBarrierPlan(
        displayPlan.Graph,
        DisplayEffectPassName(DisplayEffectPass::Taa));
    ASSERT_TRUE(taa.Valid());
    const auto* temporalOutput = taa.Find(
        EffectResource::TemporalColor);
    ASSERT_NE(temporalOutput, nullptr);
    EXPECT_EQ(temporalOutput->DispatchAccess,
              ResourceAccess::ComputeWrite);
    EXPECT_EQ(temporalOutput->CompletionAccess,
              ResourceAccess::ShaderRead);
}

TEST(Oot3dEffectPassBarrierPlan,
     MapsCacaoOutputToFidelityFxProviderContract) {
    DisplayEffectPlanInput input;
    input.WorldSurface = true;
    input.PerspectiveAvailable = true;
    input.CameraAvailable = true;
    input.AmbientOcclusion = AmbientOcclusionMode::Cacao;
    const auto displayPlan = BuildDisplayEffectPlan(input);
    ASSERT_TRUE(displayPlan.Graph.Valid()) << displayPlan.Graph.Error;

    const auto cacao = BuildEffectPassBarrierPlan(
        displayPlan.Graph,
        DisplayEffectPassName(DisplayEffectPass::Cacao));
    ASSERT_TRUE(cacao.Valid());
    EXPECT_EQ(cacao.OutputCount(), 1U);
    EXPECT_EQ(cacao.ManagedOutputCount(), 1U);
    EXPECT_EQ(cacao.ShaderReadableOutputCount(), 1U);
    const auto* output = cacao.Find(EffectResource::AmbientOcclusion);
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->DispatchAccess, ResourceAccess::ComputeWrite);
    EXPECT_EQ(output->CompletionAccess, ResourceAccess::ShaderRead);
    EXPECT_TRUE(output->DownstreamRead);
    EXPECT_EQ(
        cacao.BeginOutputExecution(EffectResource::AmbientOcclusion)
            .PlannedGraphTransitionCount(),
        2U);
}

TEST(Oot3dEffectPassBarrierPlan,
     MapsSpatialAndTemporalReconstructionOutputs) {
    const auto expectReadableOutput = [](
        const DisplayEffectPlan& displayPlan,
        DisplayEffectPass pass, EffectResource resource) {
        const auto barriers = BuildEffectPassBarrierPlan(
            displayPlan.Graph, DisplayEffectPassName(pass));
        ASSERT_TRUE(barriers.Valid());
        const auto* output = barriers.Find(resource);
        ASSERT_NE(output, nullptr);
        EXPECT_EQ(output->DispatchAccess, ResourceAccess::ComputeWrite);
        EXPECT_EQ(output->CompletionAccess, ResourceAccess::ShaderRead);
    };

    DisplayEffectPlanInput nisInput;
    nisInput.WorldSurface = true;
    nisInput.AntiAliasing = AntiAliasingMode::Upscaler;
    nisInput.Upscaler = UpscalerProvider::Nis;
    const auto nisPlan = BuildDisplayEffectPlan(nisInput);
    ASSERT_TRUE(nisPlan.Graph.Valid()) << nisPlan.Graph.Error;
    expectReadableOutput(
        nisPlan, DisplayEffectPass::Nis, EffectResource::UpscaledColor);

    DisplayEffectPlanInput fsrInput = nisInput;
    fsrInput.PerspectiveAvailable = true;
    fsrInput.CameraAvailable = true;
    fsrInput.Upscaler = UpscalerProvider::Fsr;
    const auto fsrPlan = BuildDisplayEffectPlan(fsrInput);
    ASSERT_TRUE(fsrPlan.Graph.Valid()) << fsrPlan.Graph.Error;
    expectReadableOutput(
        fsrPlan, DisplayEffectPass::TemporalUpscaler,
        EffectResource::UpscaledColor);

    DisplayEffectPlanInput smaaInput;
    smaaInput.WorldSurface = true;
    smaaInput.SmaaAvailable = true;
    smaaInput.AntiAliasing = AntiAliasingMode::Smaa1x;
    const auto smaaPlan = BuildDisplayEffectPlan(smaaInput);
    ASSERT_TRUE(smaaPlan.Graph.Valid()) << smaaPlan.Graph.Error;
    expectReadableOutput(
        smaaPlan, DisplayEffectPass::Smaa,
        EffectResource::AntiAliasedColor);
}

TEST(Oot3dEffectPassBarrierPlan,
     MapsHiZSubresourcesAndReflectionProviderOutputs) {
    DisplayEffectPlanInput hiZInput;
    hiZInput.WorldSurface = true;
    hiZInput.PerspectiveAvailable = true;
    hiZInput.Reflections = ReflectionMode::HiZ;
    const auto hiZPlan = BuildDisplayEffectPlan(hiZInput);
    ASSERT_TRUE(hiZPlan.Graph.Valid()) << hiZPlan.Graph.Error;

    const auto pyramid = BuildEffectPassBarrierPlan(
        hiZPlan.Graph, DisplayEffectPassName(DisplayEffectPass::HiZ));
    ASSERT_TRUE(pyramid.Valid());
    const auto* pyramidOutput = pyramid.Find(
        EffectResource::HierarchicalDepth);
    ASSERT_NE(pyramidOutput, nullptr);
    EXPECT_EQ(pyramidOutput->DispatchAccess, ResourceAccess::ComputeWrite);
    EXPECT_EQ(pyramidOutput->CompletionAccess, ResourceAccess::ShaderRead);
    EXPECT_EQ(
        pyramid.BeginOutputExecution(
            EffectResource::HierarchicalDepth, 9U)
            .PlannedGraphTransitionCount(),
        18U);

    const auto reflection = BuildEffectPassBarrierPlan(
        hiZPlan.Graph,
        DisplayEffectPassName(DisplayEffectPass::Reflections));
    ASSERT_TRUE(reflection.Valid());
    const auto* reflectionOutput = reflection.Find(
        EffectResource::ReflectionColor);
    ASSERT_NE(reflectionOutput, nullptr);
    EXPECT_EQ(reflectionOutput->DispatchAccess,
              ResourceAccess::ComputeWrite);
    EXPECT_EQ(reflectionOutput->CompletionAccess,
              ResourceAccess::ShaderRead);

    hiZInput.CameraAvailable = true;
    hiZInput.Reflections = ReflectionMode::FidelityFxSssr;
    const auto sssrPlan = BuildDisplayEffectPlan(hiZInput);
    const auto sssrReflection = BuildEffectPassBarrierPlan(
        sssrPlan.Graph,
        DisplayEffectPassName(DisplayEffectPass::Reflections));
    ASSERT_TRUE(sssrReflection.Valid());
    EXPECT_EQ(sssrReflection.ManagedOutputCount(), 1U);
    EXPECT_EQ(sssrReflection.BeginExecution().PlannedGraphTransitionCount(),
              2U);
    EXPECT_EQ(
        sssrReflection.BeginOutputExecution(
            EffectResource::ReflectionColor)
            .PlannedGraphTransitionCount(),
        2U);
    const auto workingColor = BuildEffectPassBarrierPlan(
        sssrPlan.Graph,
        DisplayEffectPassName(DisplayEffectPass::WorkingColor));
    ASSERT_TRUE(workingColor.Valid());
    EXPECT_EQ(workingColor.ManagedOutputCount(), 1U);
    EXPECT_EQ(
        workingColor.BeginOutputExecution(
            EffectResource::LinearWorkingColor)
            .PlannedGraphTransitionCount(),
        2U);
}

TEST(Oot3dEffectPassBarrierExecution,
     SeparatesGraphAndProviderPrivateTransitions) {
    DisplayEffectPlanInput input;
    input.WorldSurface = true;
    input.PerspectiveAvailable = true;
    input.CameraAvailable = true;
    input.AntiAliasing = AntiAliasingMode::Taa;
    const auto displayPlan = BuildDisplayEffectPlan(input);
    const auto barriers = BuildEffectPassBarrierPlan(
        displayPlan.Graph,
        DisplayEffectPassName(DisplayEffectPass::Motion));
    ASSERT_TRUE(barriers.Valid());

    auto execution = barriers.BeginExecution();
    execution.RecordGraphTransition(
        {1U, {ResourceAccess::Undefined, 0U},
             {ResourceAccess::ComputeWrite, 0U}});
    execution.RecordGraphTransition(
        {1U, {ResourceAccess::ComputeWrite, 0U},
             {ResourceAccess::ShaderRead, 0U}});
    execution.RecordGraphTransition(
        {2U, {ResourceAccess::ComputeWrite, 0U},
             {ResourceAccess::ComputeWrite, 0U}});
    execution.RecordGraphTransition(
        {2U, {ResourceAccess::ComputeWrite, 0U},
             {ResourceAccess::ComputeWrite, 0U}});
    execution.RecordPrivateTransition(
        {3U, {ResourceAccess::Undefined, 0U},
             {ResourceAccess::ShaderRead, 0U}});
    execution.RecordPrivateTransition(
        {3U, {ResourceAccess::ShaderRead, 0U},
             {ResourceAccess::ShaderRead, 0U}});

    EXPECT_EQ(execution.PlannedGraphTransitionCount(), 4U);
    EXPECT_EQ(execution.EmittedGraphTransitionCount(), 2U);
    EXPECT_EQ(execution.ElidedGraphTransitionCount(), 2U);
    EXPECT_EQ(execution.PlannedPrivateTransitionCount(), 2U);
    EXPECT_EQ(execution.EmittedPrivateTransitionCount(), 1U);
    EXPECT_EQ(execution.ElidedPrivateTransitionCount(), 1U);
}

TEST(Oot3dEffectGraph, SelectsOnlyDeclaredPicaGuideSamplingBarriers) {
    DisplayEffectPlanInput input;
    input.WorldSurface = true;
    input.PerspectiveAvailable = true;
    input.AmbientOcclusion = AmbientOcclusionMode::Cacao;
    const auto plan = BuildDisplayEffectPlan(input);
    ASSERT_TRUE(plan.Graph.Valid()) << plan.Graph.Error;
    DisplayEffectResourceTable resources;
    EXPECT_TRUE(resources.BindImage(
        EffectResource::NativeDepth, 1U, 11U, 126U,
        400U, 240U, 1U));
    EXPECT_TRUE(resources.BindImage(
        EffectResource::NormalGuide, 2U, 12U, 37U,
        400U, 240U, 1U));
    EXPECT_TRUE(resources.BindImage(
        EffectResource::AmbientGuide, 5U, 15U, 37U,
        400U, 240U, 1U));
    const auto* cacao = plan.FindPass(DisplayEffectPass::Cacao);
    ASSERT_NE(cacao, nullptr);
    const auto cacaoSampling = BuildPicaGuideSamplingBarriers(
        *cacao, resources, 0U);
    ASSERT_EQ(cacaoSampling.Count, 2U);
    EXPECT_TRUE(cacaoSampling.Complete());
    EXPECT_EQ(cacaoSampling.DeclaredCount, 2U);
    EXPECT_EQ(cacaoSampling.Images[0].image,
              reinterpret_cast<VkImage>(1U));
    EXPECT_EQ(cacaoSampling.Images[1].image,
              reinterpret_cast<VkImage>(2U));
    const auto repeatedCacao = BuildPicaGuideSamplingBarriers(
        *cacao, resources, cacaoSampling.ResourceMask);
    EXPECT_TRUE(repeatedCacao.Complete());
    EXPECT_EQ(repeatedCacao.DeclaredCount, 0U);
    EXPECT_EQ(repeatedCacao.Count, 0U);

    const auto* scanout = plan.FindPass(DisplayEffectPass::Scanout);
    ASSERT_NE(scanout, nullptr);
    const auto scanoutSampling = BuildPicaGuideSamplingBarriers(
        *scanout, resources, cacaoSampling.ResourceMask);
    ASSERT_EQ(scanoutSampling.Count, 1U);
    EXPECT_EQ(scanoutSampling.Images[0].image,
              reinterpret_cast<VkImage>(5U));

    const uint32_t sampledMask = cacaoSampling.ResourceMask |
                                 scanoutSampling.ResourceMask;
    const auto toAttachments = BuildPicaGuideAttachmentBarriers(
        resources, sampledMask);
    ASSERT_EQ(toAttachments.Count, 3U);
    EXPECT_EQ(toAttachments.ResourceMask, sampledMask);
    EXPECT_EQ(toAttachments.Images[0].oldLayout,
              cacaoSampling.Images[0].newLayout);
    EXPECT_EQ(toAttachments.Images[0].newLayout,
              cacaoSampling.Images[0].oldLayout);

    resources.Unbind(EffectResource::AmbientGuide);
    const auto incomplete = BuildPicaGuideSamplingBarriers(
        *scanout, resources, cacaoSampling.ResourceMask);
    EXPECT_FALSE(incomplete.Complete());
    EXPECT_EQ(incomplete.DeclaredCount, 1U);
    EXPECT_EQ(incomplete.Count, 0U);
    EXPECT_EQ(incomplete.MissingCount, 1U);
}

} // namespace
