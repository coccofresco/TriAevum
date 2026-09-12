#include "fast/backends/oot3d_vulkan_diagnostics.h"
#include "fast/oot3d/pica_composition_schedule.h"
#include "fast/oot3d/pica_scene_frame.h"

#include <filesystem>
#include <fstream>
#include <cstdlib>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace {

TEST(Oot3dVulkanDiagnostics, ReadsOptInEnvironment) {
#ifdef _WIN32
    ASSERT_TRUE(SetEnvironmentVariableA(
        "OOT3D_VULKAN_DIAGNOSTICS_PATH", "diagnostics-from-environment.json"));
    ASSERT_TRUE(SetEnvironmentVariableA(
        "OOT3D_VULKAN_DIAGNOSTICS_MAX_FRAMES", "17"));
#else
    ASSERT_EQ(setenv("OOT3D_VULKAN_DIAGNOSTICS_PATH", "diagnostics-from-environment.json", 1), 0);
    ASSERT_EQ(setenv("OOT3D_VULKAN_DIAGNOSTICS_MAX_FRAMES", "17", 1), 0);
#endif
    const auto config = Fast::Oot3dVulkanDiagnosticsConfig::FromEnvironment();
    EXPECT_TRUE(config.Enabled());
    EXPECT_EQ(config.OutputPath.filename(), "diagnostics-from-environment.json");
    EXPECT_EQ(config.MaximumFrames, 17U);
#ifdef _WIN32
    SetEnvironmentVariableA("OOT3D_VULKAN_DIAGNOSTICS_PATH", nullptr);
    SetEnvironmentVariableA("OOT3D_VULKAN_DIAGNOSTICS_MAX_FRAMES", nullptr);
#else
    unsetenv("OOT3D_VULKAN_DIAGNOSTICS_PATH");
    unsetenv("OOT3D_VULKAN_DIAGNOSTICS_MAX_FRAMES");
#endif
}

TEST(Oot3dVulkanDiagnostics, DisabledConfigurationIsNoOp) {
    Fast::Oot3dVulkanDiagnostics diagnostics;
    diagnostics.BeginFrame(7);
    diagnostics.RecordDisplayTransfer(false);
    diagnostics.EndFrame();
    EXPECT_TRUE(diagnostics.Frames().empty());
}

TEST(Oot3dVulkanDiagnostics, CaptureAndDelayedGpuTimingDoNotWriteFromTheFrameLoop) {
    const auto output = std::filesystem::temp_directory_path() / "triaevum_diagnostics_deferred.json";
    std::filesystem::remove(output);
    Fast::Oot3dVulkanDiagnostics diagnostics({output, 2});
    for (uint64_t frame = 0; frame < 120; ++frame) {
        diagnostics.BeginFrame(frame);
        diagnostics.EndFrame();
        Fast::Oot3dVulkanGpuTimings timings;
        timings.FrameMilliseconds = 2.5;
        diagnostics.SetGpuTimings(frame, timings);
        EXPECT_FALSE(std::filesystem::exists(output));
    }
    ASSERT_EQ(diagnostics.Frames().size(), 2U);
    diagnostics.SetNriPipelineStatistics(4096, 12, 11, 1234567);
    diagnostics.Flush();
    std::ifstream input(output);
    const auto json = nlohmann::json::parse(input);
    ASSERT_EQ(json.at("frame_count"), 2U);
    EXPECT_EQ(json.at("frames").back().at("gpu").at("frame_ms"), 2.5);
    EXPECT_EQ(json.at("nri_pipeline_compilation"), (nlohmann::json{
        {"initial_cache_bytes", 4096}, {"creation_attempts", 12},
        {"created", 11}, {"creation_nanoseconds", 1234567}}));
    input.close();
    std::filesystem::remove(output);
}

TEST(Oot3dVulkanDiagnostics, LiveSnapshotsRequireExplicitCadence) {
    const auto output = std::filesystem::temp_directory_path() / "triaevum_diagnostics_live.json";
    std::filesystem::remove(output);
    Fast::Oot3dVulkanDiagnostics diagnostics({output, 4, 2});
    diagnostics.BeginFrame(0);
    diagnostics.EndFrame();
    EXPECT_FALSE(std::filesystem::exists(output));
    diagnostics.BeginFrame(1);
    diagnostics.EndFrame();
    EXPECT_TRUE(std::filesystem::exists(output));
    std::filesystem::remove(output);
}

TEST(Oot3dVulkanDiagnostics, WritesBoundedAuthenticFrameTrace) {
    const auto output = std::filesystem::temp_directory_path() /
                        "oot3d_vulkan_diagnostics_test.json";
    std::error_code ignored;
    std::filesystem::remove(output, ignored);

    Fast::Oot3dVulkanDiagnosticsConfig config;
    config.OutputPath = output;
    config.MaximumFrames = 1;
    Fast::Oot3dVulkanDiagnostics diagnostics(config);

    diagnostics.BeginFrame(10);
    diagnostics.RecordNativePicaDraw(
        100, 4, 36, true, 0x1000, 0x2000, 400, 240, 2.0F, 0.1F,
        true, true, false);
    diagnostics.RecordDisplayTransfer(false);
    diagnostics.RecordDisplayTransfer(true, 2U);
    diagnostics.RecordOverlay();
    diagnostics.RecordMemoryFill();
    diagnostics.EndFrame();

    diagnostics.RecordPresentationTransaction(1U, true, false);
    diagnostics.RecordPresentationTransaction(2U, true, false);
    diagnostics.RecordPresentationTransaction(1U, false, true);
    diagnostics.SetVulkanAdapter({
        2U, 1U, 0x10deU, 0x2504U, 3U, 3U,
        8U, 2U, 1U, true, true,
    });
    diagnostics.SetD3d12NgxProvider({
        true, true, true, true, true, true, true, true, true,
        0x10deU, 0x2504U, 853U, 480U, 1280U, 720U,
        "NVIDIA GeForce RTX 3060",
        "official NGX D3D12 DLSR feature and zero-copy interop ready",
        true, true, 17U,
    });
    diagnostics.SetVulkanPresentationFallback(
        Fast::Oot3dVulkanPresentationFallbackReason::None, {});
    diagnostics.SetPresentationState({
        true, 1U, 2560U, 1440U, 3440U, 1440U, false,
    });
    diagnostics.BeginFrame(11);
    diagnostics.RecordPicaShaderProfile(false);
    diagnostics.RecordPicaAttachmentContract(5U, 0x0BU);
    diagnostics.RecordEffectGraph(9U, 12U, 35U, 7U);
    diagnostics.RecordEffectGraphGuideImageBarriers(4U);
    diagnostics.RecordEffectGraphGuideImageBarriers(4U);
    diagnostics.RecordEffectGraphInternalImageTransitions(7U, 5U, 4U, 1U);
    diagnostics.RecordEffectGeometryProvider({
        .Declared = true,
        .Stage = Fast::Oot3d::EffectStage::BeforeTransparent,
        .SourceInspectionCount = 12U,
        .SourcePublishedCount = 3U,
        .DeclaredBoundaryInvocationCount = 1U,
        .FallbackInvocationCount = 1U,
        .ExecutedCount = 1U,
        .ReusedCount = 1U,
        .SkippedCount = 0U,
        .FailedCount = 0U,
        .InputUnavailableCount = 2U,
        .ScheduleRejectedCount = 0U,
    });
    diagnostics.RecordDisplayEffectGraphExecution({
        .DeclaredPassMask = 0x47U,
        .ExecutedPassMask = 0x41U,
        .ReusedPassMask = 0x02U,
        .FusedPassMask = 0x04U,
        .DeclaredPassCount = 4U,
        .ExecutedPassCount = 2U,
        .ReusedPassCount = 1U,
        .FusedPassCount = 1U,
        .ActiveBindingCount = 11U,
    });
    diagnostics.RecordDisplayEffectGraphBindings({
        .DeclaredReadMask = 0x21U,
        .ResolvedReadMask = 0x01U,
        .MissingReadMask = 0x20U,
        .DeclaredReadCount = 2U,
        .ResolvedReadCount = 1U,
        .MissingReadCount = 1U,
    }, true);
    diagnostics.RecordDisplayEffectPhysicalPlan({
        .DeclaredResourceCount = 12U,
        .ResolvedResourceCount = 11U,
        .MissingResourceCount = 1U,
        .MissingResourceMask = 0x20U,
        .SemanticResourceCount = 2U,
        .ExternalImageResourceCount = 3U,
        .NativeAttachmentResourceCount = 3U,
        .TransientImageResourceCount = 3U,
        .TemporalHistoryResourceCount = 1U,
        .PhysicalImageCount = 9U,
        .AliasEligibleResourceCount = 3U,
        .AliasSlotCount = 2U,
        .AliasOpportunityCount = 1U,
        .PeakLiveTransientCount = 2U,
    });
    diagnostics.RecordEffectTransientImageArena({
        .RequestedResourceCount = 3U,
        .PlannedResourceCount = 3U,
        .PhysicalSlotCount = 2U,
        .AliasOpportunityCount = 1U,
        .PeakLiveResourceCount = 2U,
    }, true, 2U, false);
    diagnostics.RecordEffectTransientImageArena({
        .RequestedResourceCount = 3U,
        .PlannedResourceCount = 3U,
        .PhysicalSlotCount = 2U,
        .AliasOpportunityCount = 1U,
        .PeakLiveResourceCount = 2U,
    }, true, 0U, true);
    diagnostics.RecordPicaShaderSelection(true, false, 3U, 0U);
    diagnostics.RecordPicaShaderSelection(
        false, true, 3U, 2U, true, true, true,
        5U, 0U, 0U, 0U);
    diagnostics.RecordPicaPipelinePrewarm(
        39U, 41U, true, 37U, 2U, 1U);
    diagnostics.RecordRenderResolution(
        1.5F, 1920U, 1080U, 720U, 1280U);
    diagnostics.RecordNativePicaDraw(
        101, 5, 12, false, 0x3000, 0x4000, 320, 240, 1.0F, 0.2F,
        true, false, true, true);
    diagnostics.RecordNativePicaCpuTimings({
        .DrawCount = 1U,
        .TotalMilliseconds = 1.8,
        .ShaderVariantMilliseconds = 0.1,
        .ShaderVariantCacheHits = 0U,
        .ShaderVariantCacheMisses = 1U,
        .ShaderVariantCacheEntries = 7U,
        .ShaderCacheMilliseconds = 0.2,
        .TextureMilliseconds = 0.3,
        .DrawStateMilliseconds = 0.15,
        .GrassSurfaceMilliseconds = 0.04,
        .SceneStateMilliseconds = 0.05,
        .RenderTargetStateMilliseconds = 0.01,
        .DirectionalShadowMilliseconds = 0.015,
        .GrassRenderMilliseconds = 0.02,
        .ReflectionEnvironmentMilliseconds = 0.02,
        .TemporalStateMilliseconds = 0.06,
        .PipelineMilliseconds = 0.25,
        .UploadMilliseconds = 0.35,
        .DescriptorMilliseconds = 0.2,
        .CommandMilliseconds = 0.25,
        .GeometryRegistryHits = 0U,
        .GeometryRegistryMisses = 1U,
        .GeometryDynamicBuilds = 0U,
        .GeometryRegistryEntries = 6U,
        .GeometryPersistentDraws = 1U,
        .GeometryTransientDraws = 0U,
        .GeometryPersistentUploadBytes = 3072U,
        .GeometryTransientUploadBytes = 0U,
    });
    diagnostics.RecordNativePicaCpuTimings({
        .DrawCount = 1U,
        .TotalMilliseconds = 2.2,
        .ShaderVariantMilliseconds = 0.2,
        .ShaderVariantCacheHits = 1U,
        .ShaderVariantCacheMisses = 0U,
        .ShaderVariantCacheEntries = 7U,
        .ShaderCacheMilliseconds = 0.1,
        .TextureMilliseconds = 0.4,
        .DrawStateMilliseconds = 0.2,
        .GrassSurfaceMilliseconds = 0.05,
        .SceneStateMilliseconds = 0.06,
        .RenderTargetStateMilliseconds = 0.02,
        .DirectionalShadowMilliseconds = 0.025,
        .GrassRenderMilliseconds = 0.01,
        .ReflectionEnvironmentMilliseconds = 0.03,
        .TemporalStateMilliseconds = 0.09,
        .PipelineMilliseconds = 0.3,
        .UploadMilliseconds = 0.4,
        .DescriptorMilliseconds = 0.3,
        .CommandMilliseconds = 0.3,
        .GeometryRegistryHits = 1U,
        .GeometryRegistryMisses = 0U,
        .GeometryDynamicBuilds = 1U,
        .GeometryRegistryEntries = 7U,
        .GeometryPersistentDraws = 0U,
        .GeometryTransientDraws = 1U,
        .GeometryPersistentUploadBytes = 0U,
        .GeometryTransientUploadBytes = 1536U,
    });
    diagnostics.RecordDisplayTransfer(true, 2U);
    diagnostics.RecordTemporalHistory(true, false, 0U);
    diagnostics.RecordMotionVectors(true, true, true, true, true, true);
    diagnostics.RecordReactiveWorldDraw();
    diagnostics.RecordTemporalAa(true, true, true, true, true, true);
    diagnostics.RecordNisUpscale(true, 1280U, 720U, 1920U, 1080U);
    diagnostics.RecordFsrUpscale(true, true, 1280U, 720U, 1920U, 1080U);
    diagnostics.RecordDlssUpscale(true, true, 1280U, 720U, 1920U, 1080U);
    diagnostics.RecordD3d12NgxFrame(
        true, true, true, true, true, true, 18U);
    diagnostics.RecordUpscalerOutputOwnership(true, true, true);
    diagnostics.RecordNriScanout(true, true, true, true);
    diagnostics.RecordLinearScanout(true);
    diagnostics.RecordNriSwapchainAcquire(true, 3U, true, true);
    diagnostics.RecordNriSwapchainPresent(true);
    diagnostics.RecordNativePicaDynamicRendering(true);
    diagnostics.RecordNriPicaGlobalBarriers(2U);
    diagnostics.RecordNriPicaPipelineBind(true);
    diagnostics.RecordNriPicaShaderContract(true, true);
    diagnostics.RecordNriPicaOwnedPipeline(true);
    diagnostics.RecordNriPicaOwnedDraw(true, true, true, 4096U);
    diagnostics.RecordPicaCompositionSchedule({
        .DrawCount = 17U,
        .RunCount = 6U,
        .TargetCount = 2U,
        .StageAnchorCount = 8U,
        .WorldStageTargetCount = 1U,
        .NonSceneTargetCount = 5U,
        .UnknownLayerTargetCount = 1U,
        .MissingOpaqueWorldTargetCount = 2U,
        .NonContiguousWorldTargetCount = 3U,
        .NonCanonicalTailTargetCount = 4U,
    });
    diagnostics.RecordPicaCompositionScheduleDraw(true);
    diagnostics.RecordPicaCompositionScheduleDraw(false);
    diagnostics.RecordPicaDisplayComposition(
        Oot3d::Renderer::PicaCompositionDomain::Scene, true);
    diagnostics.RecordPicaDisplayComposition(
        Oot3d::Renderer::PicaCompositionDomain::Ui, false);
    diagnostics.RecordPicaDisplayComposition(
        Oot3d::Renderer::PicaCompositionDomain::Unknown, false);
    diagnostics.RecordPicaSceneFrame({
        .DrawCount = 17U,
        .TemporalSampleCount = 1U,
        .SyntheticTemporalSampleCount = 1U,
        .TemporalSampleMultiplier = 3U,
        .TemporalSampleOrdinal = 2U,
        .SceneDomainDrawCount = 12U,
        .UiDomainDrawCount = 4U,
        .UnknownDomainDrawCount = 1U,
        .OpaqueWorldLayerDrawCount = 8U,
        .TransparentWorldLayerDrawCount = 3U,
        .AtmosphereLayerDrawCount = 1U,
        .UiLayerDrawCount = 4U,
        .UnknownLayerDrawCount = 1U,
        .NativeCmbPassProvenanceDrawCount = 11U,
        .NativeControlFlowProvenanceDrawCount = 1U,
        .NativeUiLifecycleProvenanceDrawCount = 4U,
        .UnknownProvenanceDrawCount = 1U,
        .ResolvedRasterStateCount = 17U,
        .ResolvedRenderTargetStateCount = 17U,
        .ResolvedRenderTargetGpuResourceCount = 17U,
        .ResolvedMaterialStateCount = 17U,
        .NativeLightingStateCount = 13U,
        .NativeFragmentLightingStateCount = 9U,
        .NativeFragmentLightingEnabledDrawCount = 4U,
        .NativeFogStateCount = 5U,
        .NativeTransformStateCount = 11U,
        .PreviousNativeTransformStateCount = 7U,
        .NativeSkeletonStateCount = 3U,
        .PreviousNativeSkeletonStateCount = 2U,
        .RejectedDrawCount = 2U,
        .BoundTextureCount = 23U,
        .ShaderCatalogEntries = 7U,
        .VertexLayoutCatalogEntries = 3U,
        .CanonicalPipelineEntries = 11U,
        .CanonicalFullRegisterStateEntries = 19U,
    });
    diagnostics.RecordNriPicaTextureUpload(true, 16384U);
    diagnostics.RecordNriPicaTextureImage(true);
    diagnostics.RecordNriPicaDisplayImage(true);
    diagnostics.RecordNriPicaRenderTarget(true, 11U);
    diagnostics.RecordNriPicaRenderTargetInitialization(
        true, 11U, 12U);
    diagnostics.RecordNriPicaDisplayCopy(true, 4U);
    diagnostics.RecordNriPicaMemoryFillClear(true, true, 2U);
    diagnostics.RecordNriDirectionalShadow(37U, true);
    diagnostics.RecordNriDirectionalShadowAttempt(41U, 19U, 7U);
    diagnostics.RecordNriDirectionalShadowLightSelection(
        19U, 3U, 97.5F, 0.25F, 0.75F, -0.5F);
    diagnostics.RecordNriDirectionalShadowReceiver(
        true, true, true, false, false, true, true);
    diagnostics.RecordNriDirectionalShadowReceiver(
        true, true, true, true, false, false, false);
    diagnostics.RecordNriDirectionalShadowGraphTransitions(4U, 3U);
    diagnostics.RecordNriDirectionalShadowSchedule(
        true, false, false);
    diagnostics.RecordNriDirectionalShadowSchedule(
        true, true, true);
    diagnostics.RecordNriDirectionalShadowSchedule(
        false, false, true);
    diagnostics.RecordSceneCompositePass(true, true, true);
    diagnostics.RecordSmaa1x(true, true, true, true, true);
    diagnostics.RecordCacaoPass(true, true);
    diagnostics.RecordCacaoAmbientGuideDraw(true);
    diagnostics.RecordCacaoAmbientGuideDraw(false);
    diagnostics.RecordCacaoAmbientComposite();
    diagnostics.RecordToonDraw(true);
    Fast::Oot3d::RendererValidationSnapshot validation;
    validation.VulkanEnabled = true;
    validation.NriEnabled = true;
    validation.VulkanWarningCount = 2U;
    validation.NriInfoCount = 3U;
    diagnostics.RecordRendererValidation(validation);
    diagnostics.RecordTemporalDraw(true, true, true);
    diagnostics.RecordGrassMotion(42U);
    diagnostics.RecordHiZPass(10U, 2U, true, true, true);
    diagnostics.RecordHiZReflection(true, 4U, true, true, true, true);
    diagnostics.RecordLinearWorkingColor(true, true, true, true);
    diagnostics.RecordFidelityFxSssr(true, true, true, true);
    diagnostics.RecordFidelityFxSssrFallback("synthetic fallback");
    diagnostics.RecordReflectionIbl(
        true, true, 6U, true, true, true, true);
    diagnostics.RecordReflectionMaterialResolve(true, true, true);
    diagnostics.RecordReflectionMaterialDraw(
        true, true, false, false);
    diagnostics.RecordReflectionMaterialDraw(
        false, false, true, false);
    diagnostics.RecordReflectionTextureUsage(
        0xbe15aff93dfdcd88ULL, 64U, 32U, 1U,
        true, 7U, 2U, 0.58F, 0.32F);
    diagnostics.RecordReflectionTextureUsage(
        0xbe15aff93dfdcd88ULL, 64U, 32U, 1U,
        true, 7U, 2U, 0.58F, 0.32F);
    diagnostics.RecordReflectionTextureUsage(
        0x0123456789abcdefULL, 16U, 16U, 0U);
    diagnostics.EndFrame();

    Fast::Oot3dVulkanGpuTimings timings;
    timings.FrameMilliseconds = 4.5;
    timings.NativePicaMilliseconds = 2.25;
    timings.ToonRasterMilliseconds = 1.5;
    timings.CacaoMilliseconds = 0.75;
    timings.DepthPreparationMilliseconds = 0.25;
    timings.ReflectionMilliseconds = 0.5;
    timings.AntiAliasingMilliseconds = 0.125;
    diagnostics.SetGpuTimings(11, timings);

    ASSERT_EQ(diagnostics.Frames().size(), 1U);
    EXPECT_EQ(diagnostics.Frames().front().FrameIndex, 11U);
    EXPECT_EQ(diagnostics.Frames().front().NativePicaDrawCount, 1U);
    EXPECT_EQ(
        diagnostics.Frames().front().NativePicaAlphaTestDrawCount, 1U);
    EXPECT_EQ(
        diagnostics.Frames().front().NativePicaAlphaToCoverageDrawCount, 1U);
    EXPECT_EQ(diagnostics.Frames().front().NativePicaVertexCount, 12U);
    EXPECT_EQ(diagnostics.Frames().front().PicaCompositionSequenceCount,
              1U);
    EXPECT_EQ(
        diagnostics.Frames().front().PicaCompositionPublishedDrawCount,
        17U);
    EXPECT_EQ(
        diagnostics.Frames().front().PicaCompositionConsumedDrawCount,
        1U);
    EXPECT_EQ(diagnostics.Frames().front().PicaCompositionRunCount, 6U);
    EXPECT_EQ(diagnostics.Frames().front().PicaCompositionTargetCount, 2U);
    EXPECT_EQ(
        diagnostics.Frames().front().PicaCompositionStageAnchorCount, 8U);
    EXPECT_EQ(
        diagnostics.Frames().front().PicaCompositionWorldStageTargetCount,
        1U);
    EXPECT_EQ(
        diagnostics.Frames().front().PicaCompositionNonSceneTargetCount,
        5U);
    EXPECT_EQ(
        diagnostics.Frames().front().PicaCompositionUnknownLayerTargetCount,
        1U);
    EXPECT_EQ(diagnostics.Frames()
                  .front()
                  .PicaCompositionMissingOpaqueWorldTargetCount,
              2U);
    EXPECT_EQ(diagnostics.Frames()
                  .front()
                  .PicaCompositionNonContiguousWorldTargetCount,
              3U);
    EXPECT_EQ(diagnostics.Frames()
                  .front()
                  .PicaCompositionNonCanonicalTailTargetCount,
              4U);
    EXPECT_EQ(
        diagnostics.Frames().front().PicaCompositionExecutionMismatchCount,
        1U);
    EXPECT_EQ(diagnostics.Frames().front().PicaDisplaySceneSnapshotCount,
              1U);
    EXPECT_EQ(diagnostics.Frames().front().PicaDisplayUiSnapshotCount, 1U);
    EXPECT_EQ(diagnostics.Frames().front().PicaDisplayUnknownSnapshotCount,
              1U);
    EXPECT_EQ(
        diagnostics.Frames().front().PicaDisplaySceneResolvedSnapshotCount,
        1U);
    EXPECT_EQ(diagnostics.Frames().front().PicaSceneFrameTemporalSampleCount, 1U);
    EXPECT_EQ(diagnostics.Frames().front().PicaSceneFrameSyntheticTemporalSampleCount, 1U);
    EXPECT_EQ(diagnostics.Frames().front().PicaSceneFrameTemporalSampleMultiplier, 3U);
    EXPECT_EQ(diagnostics.Frames().front().PicaSceneFrameTemporalSampleOrdinal, 2U);
    EXPECT_EQ(
        diagnostics.Frames().front().PicaSceneFrameSceneDomainDrawCount,
        12U);
    EXPECT_EQ(
        diagnostics.Frames().front().PicaSceneFrameUiDomainDrawCount, 4U);
    EXPECT_EQ(
        diagnostics.Frames().front().PicaSceneFrameUnknownDomainDrawCount,
        1U);
    EXPECT_EQ(
        diagnostics.Frames().front().PicaSceneFrameOpaqueWorldLayerDrawCount,
        8U);
    EXPECT_EQ(diagnostics.Frames()
                  .front()
                  .PicaSceneFrameTransparentWorldLayerDrawCount,
              3U);
    EXPECT_EQ(
        diagnostics.Frames().front().PicaSceneFrameAtmosphereLayerDrawCount,
        1U);
    EXPECT_EQ(diagnostics.Frames().front().PicaSceneFrameUiLayerDrawCount,
              4U);
    EXPECT_EQ(
        diagnostics.Frames().front().PicaSceneFrameUnknownLayerDrawCount,
        1U);
    EXPECT_EQ(diagnostics.Frames()
                  .front()
                  .PicaSceneFrameNativeCmbPassProvenanceDrawCount,
              11U);
    EXPECT_EQ(diagnostics.Frames()
                  .front()
                  .PicaSceneFrameResolvedRasterStateCount,
              17U);
    EXPECT_EQ(diagnostics.Frames()
                  .front()
                  .PicaSceneFrameResolvedRenderTargetStateCount,
              17U);
    EXPECT_EQ(diagnostics.Frames()
                  .front()
                  .PicaSceneFrameResolvedRenderTargetGpuResourceCount,
              17U);
    EXPECT_EQ(diagnostics.Frames()
                  .front()
                  .PicaSceneFrameResolvedMaterialStateCount,
              17U);
    EXPECT_EQ(
        diagnostics.Frames().front().PicaSceneFrameNativeLightingStateCount,
        13U);
    EXPECT_EQ(diagnostics.Frames()
                  .front()
                  .PicaSceneFrameNativeFragmentLightingStateCount,
              9U);
    EXPECT_EQ(diagnostics.Frames()
                  .front()
                  .PicaSceneFrameNativeFragmentLightingEnabledDrawCount,
              4U);
    EXPECT_EQ(
        diagnostics.Frames().front().PicaSceneFrameNativeFogStateCount,
        5U);
    EXPECT_EQ(diagnostics.Frames().front().PicaSceneFrameNativeTransformStateCount, 11U);
    EXPECT_EQ(diagnostics.Frames().front().PicaSceneFramePreviousNativeTransformStateCount, 7U);
    EXPECT_EQ(diagnostics.Frames().front().PicaSceneFrameNativeSkeletonStateCount, 3U);
    EXPECT_FALSE(
        diagnostics.Frames().front().PicaNativeFidelityProfile);
    EXPECT_EQ(
        diagnostics.Frames().front().PicaColorAttachmentCount, 5U);
    EXPECT_EQ(
        diagnostics.Frames().front().PicaAuxiliaryOutputMask, 0x0BU);
    EXPECT_EQ(diagnostics.Frames().front().EffectGraphPassCount, 9U);
    EXPECT_EQ(diagnostics.Frames().front().EffectGraphResourceCount, 12U);
    EXPECT_EQ(
        diagnostics.Frames().front().EffectGraphDeclaredBindingCount, 35U);
    EXPECT_EQ(
        diagnostics.Frames().front().EffectGraphDeclaredBarrierCount, 7U);
    EXPECT_EQ(
        diagnostics.Frames().front().EffectGraphGuideImageBarrierCount, 8U);
    EXPECT_EQ(
        diagnostics.Frames().front().EffectGraphInternalImageTransitionCount,
        5U);
    EXPECT_EQ(
        diagnostics.Frames().front().EffectGraphElidedImageTransitionCount,
        2U);
    EXPECT_EQ(
        diagnostics.Frames().front().
            EffectProviderPrivateImageTransitionCount,
        1U);
    EXPECT_EQ(
        diagnostics.Frames().front().
            EffectProviderPrivateElidedImageTransitionCount,
        3U);
    EXPECT_TRUE(
        diagnostics.Frames().front().EffectGeometryProviderDeclared);
    EXPECT_EQ(
        diagnostics.Frames().front().EffectGeometrySourceInspectionCount,
        12U);
    EXPECT_EQ(
        diagnostics.Frames().front().EffectGeometrySourcePublishedCount,
        3U);
    EXPECT_EQ(
        diagnostics.Frames().front().EffectGeometryExecutedCount, 1U);
    EXPECT_EQ(
        diagnostics.Frames().front().EffectGeometryReusedCount, 1U);
    EXPECT_EQ(
        diagnostics.Frames().front().
            DisplayEffectPhysicalPlanInvocationCount,
        1U);
    EXPECT_EQ(
        diagnostics.Frames().front().
            DisplayEffectPhysicalPlanCompleteCount,
        0U);
    EXPECT_EQ(
        diagnostics.Frames().front().
            DisplayEffectPhysicalDeclaredResourceCount,
        12U);
    EXPECT_EQ(
        diagnostics.Frames().front().
            DisplayEffectPhysicalResolvedResourceCount,
        11U);
    EXPECT_EQ(
        diagnostics.Frames().front().
            DisplayEffectPhysicalAliasOpportunityCount,
        1U);
    EXPECT_EQ(
        diagnostics.Frames().front().
            DisplayEffectPhysicalPeakLiveTransientCount,
        2U);
    EXPECT_EQ(
        diagnostics.Frames().front().EffectTransientArenaInvocationCount,
        2U);
    EXPECT_EQ(
        diagnostics.Frames().front().EffectTransientArenaConfiguredCount,
        2U);
    EXPECT_EQ(
        diagnostics.Frames().front().EffectTransientArenaResourceCount,
        6U);
    EXPECT_EQ(
        diagnostics.Frames().front().EffectTransientArenaPhysicalSlotCount,
        4U);
    EXPECT_EQ(
        diagnostics.Frames().front().
            EffectTransientArenaAliasOpportunityCount,
        2U);
    EXPECT_EQ(
        diagnostics.Frames().front().
            EffectTransientArenaPeakLiveResourceCount,
        2U);
    EXPECT_EQ(
        diagnostics.Frames().front().EffectTransientArenaAllocatedSlotCount,
        2U);
    EXPECT_EQ(
        diagnostics.Frames().front().
            EffectTransientArenaReusedConfigurationCount,
        1U);
    EXPECT_EQ(
        diagnostics.Frames().front().PicaCanonicalShaderDrawCount, 1U);
    EXPECT_EQ(
        diagnostics.Frames().front().PicaInstrumentedShaderDrawCount, 1U);
    EXPECT_EQ(
        diagnostics.Frames().front().PicaDirectFragmentHookDrawCount, 1U);
    EXPECT_EQ(
        diagnostics.Frames().front().PicaLegacyFragmentHookAnalysisDrawCount,
        0U);
    EXPECT_EQ(
        diagnostics.Frames().front().PicaDirectTemporalVertexProgramDrawCount,
        1U);
    EXPECT_EQ(
        diagnostics.Frames().front().PicaLegacyTemporalVertexAnalysisDrawCount,
        0U);
    EXPECT_EQ(diagnostics.Frames().front().NativePicaCpu.DrawCount, 2U);
    EXPECT_DOUBLE_EQ(
        diagnostics.Frames().front().NativePicaCpu.TotalMilliseconds, 4.0);
    EXPECT_FALSE(
        diagnostics.Frames().front().Gpu.ScanoutMilliseconds.has_value());
    EXPECT_EQ(
        diagnostics.Frames().front().
            NriDirectionalShadowGraphImageTransitionCount,
        3U);
    EXPECT_EQ(
        diagnostics.Frames().front().
            NriDirectionalShadowGraphElidedImageTransitionCount,
        1U);
    EXPECT_EQ(
        diagnostics.Frames().front().
            NriDirectionalShadowReceiverEligibleDrawCount,
        2U);
    EXPECT_EQ(
        diagnostics.Frames().front().
            NriDirectionalShadowReceiverHistoryBoundDrawCount,
        1U);

    diagnostics.Flush();
    std::ifstream input(output);
    ASSERT_TRUE(input);
    const auto json = nlohmann::json::parse(input);
    EXPECT_EQ(json.at("format"), "oot3d_vulkan_diagnostics_v1");
    EXPECT_EQ(json.at("render_mode"), "authentic");
    EXPECT_FALSE(json.at("effects_enabled").get<bool>());
    const auto& d3d12Ngx = json.at("d3d12_ngx_provider");
    EXPECT_TRUE(d3d12Ngx.at("adapter_matched"));
    EXPECT_TRUE(d3d12Ngx.at("device_ready"));
    EXPECT_TRUE(d3d12Ngx.at("nri_ready"));
    EXPECT_TRUE(d3d12Ngx.at("dlss_feature_ready"));
    EXPECT_TRUE(d3d12Ngx.at("dlss_contract_resources_ready"));
    EXPECT_TRUE(d3d12Ngx.at("dlss_evaluate_ready"));
    EXPECT_TRUE(d3d12Ngx.at("external_memory_ready"));
    EXPECT_TRUE(d3d12Ngx.at("shared_fence_ready"));
    EXPECT_TRUE(d3d12Ngx.at("zero_copy_interop_ready"));
    EXPECT_EQ(d3d12Ngx.at("vendor_id"), 0x10deU);
    EXPECT_EQ(d3d12Ngx.at("device_id"), 0x2504U);
    EXPECT_EQ(d3d12Ngx.at("probe_input_width"), 853U);
    EXPECT_EQ(d3d12Ngx.at("probe_input_height"), 480U);
    EXPECT_EQ(d3d12Ngx.at("probe_output_width"), 1280U);
    EXPECT_EQ(d3d12Ngx.at("probe_output_height"), 720U);
    EXPECT_TRUE(d3d12Ngx.at("frame_bridge_ready"));
    EXPECT_TRUE(d3d12Ngx.at("frame_dispatch_requested"));
    EXPECT_EQ(d3d12Ngx.at("frame_dispatch_count"), 18U);
    EXPECT_EQ(d3d12Ngx.at("adapter_name"), "NVIDIA GeForce RTX 3060");
    ASSERT_EQ(json.at("frames").size(), 1U);
    const auto& frame = json.at("frames").front();
    EXPECT_EQ(frame.at("frame_index"), 11U);
    EXPECT_FALSE(frame.at("pica_native_fidelity_profile"));
    EXPECT_EQ(frame.at("pica_color_attachment_count"), 5U);
    EXPECT_EQ(frame.at("pica_auxiliary_output_mask"), 0x0BU);
    EXPECT_EQ(frame.at("pica_scene_frame_scene_domain_draw_count"), 12U);
    EXPECT_EQ(frame.at("pica_scene_frame_ui_domain_draw_count"), 4U);
    EXPECT_EQ(frame.at("pica_scene_frame_unknown_domain_draw_count"), 1U);
    EXPECT_EQ(frame.at("pica_scene_frame_opaque_world_layer_draw_count"), 8U);
    EXPECT_EQ(
        frame.at("pica_scene_frame_transparent_world_layer_draw_count"), 3U);
    EXPECT_EQ(frame.at("pica_scene_frame_atmosphere_layer_draw_count"), 1U);
    EXPECT_EQ(frame.at("pica_scene_frame_ui_layer_draw_count"), 4U);
    EXPECT_EQ(frame.at("pica_scene_frame_unknown_layer_draw_count"), 1U);
    EXPECT_EQ(
        frame.at("pica_scene_frame_native_cmb_pass_provenance_draw_count"),
        11U);
    EXPECT_EQ(
        frame.at("pica_scene_frame_resolved_raster_state_count"), 17U);
    EXPECT_EQ(
        frame.at("pica_scene_frame_resolved_render_target_state_count"),
        17U);
    EXPECT_EQ(
        frame.at(
            "pica_scene_frame_resolved_render_target_gpu_resource_count"),
        17U);
    EXPECT_EQ(
        frame.at("pica_scene_frame_resolved_material_state_count"), 17U);
    EXPECT_EQ(
        frame.at("pica_scene_frame_native_lighting_state_count"), 13U);
    EXPECT_EQ(frame.at("pica_scene_frame_native_fog_state_count"), 5U);
    EXPECT_EQ(frame.at("pica_scene_frame_native_transform_state_count"), 11U);
    EXPECT_EQ(frame.at("pica_scene_frame_previous_native_transform_state_count"), 7U);
    EXPECT_EQ(frame.at("pica_scene_frame_native_skeleton_state_count"), 3U);
    EXPECT_EQ(frame.at("pica_scene_frame_previous_native_skeleton_state_count"), 2U);
    EXPECT_EQ(frame.at("effect_graph_pass_count"), 9U);
    EXPECT_EQ(frame.at("effect_graph_resource_count"), 12U);
    EXPECT_EQ(frame.at("effect_graph_declared_binding_count"), 35U);
    EXPECT_EQ(frame.at("effect_graph_declared_barrier_count"), 7U);
    EXPECT_EQ(frame.at("effect_graph_guide_image_barrier_count"), 8U);
    EXPECT_TRUE(frame.at("effect_geometry_provider_declared"));
    EXPECT_EQ(frame.at("effect_geometry_provider_stage"),
              static_cast<uint32_t>(
                  Fast::Oot3d::EffectStage::BeforeTransparent));
    EXPECT_EQ(frame.at("effect_geometry_source_inspection_count"), 12U);
    EXPECT_EQ(frame.at("effect_geometry_source_published_count"), 3U);
    EXPECT_EQ(
        frame.at("effect_geometry_declared_boundary_invocation_count"), 1U);
    EXPECT_EQ(frame.at("effect_geometry_fallback_invocation_count"), 1U);
    EXPECT_EQ(frame.at("effect_geometry_executed_count"), 1U);
    EXPECT_EQ(frame.at("effect_geometry_reused_count"), 1U);
    EXPECT_EQ(frame.at("effect_geometry_skipped_count"), 0U);
    EXPECT_EQ(frame.at("effect_geometry_failed_count"), 0U);
    EXPECT_EQ(frame.at("effect_geometry_input_unavailable_count"), 2U);
    EXPECT_EQ(frame.at("effect_geometry_schedule_rejected_count"), 0U);
    EXPECT_EQ(
        frame.at("nri_directional_shadow_graph_image_transition_count"),
        3U);
    EXPECT_EQ(
        frame.at(
            "nri_directional_shadow_graph_elided_image_transition_count"),
        1U);
    EXPECT_EQ(
        frame.at("nri_directional_shadow_schedule_boundary_count"), 3U);
    EXPECT_EQ(
        frame.at("nri_directional_shadow_schedule_authorized_count"), 2U);
    EXPECT_EQ(
        frame.at("nri_directional_shadow_schedule_rejected_count"), 1U);
    EXPECT_EQ(
        frame.at("nri_directional_shadow_schedule_duplicate_count"), 1U);
    EXPECT_EQ(
        frame.at("nri_directional_shadow_schedule_surface_end_count"), 2U);
    EXPECT_EQ(
        frame.at("nri_directional_shadow_receiver_eligible_draw_count"),
        2U);
    EXPECT_EQ(
        frame.at("nri_directional_shadow_light_candidate_count"), 19U);
    EXPECT_EQ(
        frame.at("nri_directional_shadow_light_cluster_count"), 3U);
    EXPECT_FLOAT_EQ(
        frame.at("nri_directional_shadow_light_geometry_weight"), 97.5F);
    EXPECT_FLOAT_EQ(
        frame.at("nri_directional_shadow_light_direction_x"), 0.25F);
    EXPECT_FLOAT_EQ(
        frame.at("nri_directional_shadow_light_direction_y"), 0.75F);
    EXPECT_FLOAT_EQ(
        frame.at("nri_directional_shadow_light_direction_z"), -0.5F);
    EXPECT_EQ(
        frame.at(
            "nri_directional_shadow_receiver_lighting_available_draw_count"),
        2U);
    EXPECT_EQ(
        frame.at(
            "nri_directional_shadow_receiver_lighting_enabled_draw_count"),
        2U);
    EXPECT_EQ(
        frame.at(
            "nri_directional_shadow_receiver_ambient_only_draw_count"),
        1U);
    EXPECT_EQ(
        frame.at(
            "nri_directional_shadow_receiver_direct_unmatched_draw_count"),
        0U);
    EXPECT_EQ(
        frame.at(
            "nri_directional_shadow_receiver_direct_matched_draw_count"),
        1U);
    EXPECT_EQ(
        frame.at(
            "nri_directional_shadow_receiver_history_bound_draw_count"),
        1U);
    EXPECT_EQ(
        frame.at("display_effect_physical_plan_invocation_count"), 1U);
    EXPECT_EQ(
        frame.at("display_effect_physical_plan_complete_count"), 0U);
    EXPECT_EQ(
        frame.at("display_effect_physical_declared_resource_count"), 12U);
    EXPECT_EQ(
        frame.at("display_effect_physical_resolved_resource_count"), 11U);
    EXPECT_EQ(
        frame.at("display_effect_physical_missing_resource_count"), 1U);
    EXPECT_EQ(
        frame.at("display_effect_physical_alias_opportunity_count"), 1U);
    EXPECT_EQ(
        frame.at("display_effect_physical_peak_live_transient_count"), 2U);
    EXPECT_EQ(frame.at("effect_transient_arena_invocation_count"), 2U);
    EXPECT_EQ(frame.at("effect_transient_arena_configured_count"), 2U);
    EXPECT_EQ(frame.at("effect_transient_arena_resource_count"), 6U);
    EXPECT_EQ(frame.at("effect_transient_arena_physical_slot_count"), 4U);
    EXPECT_EQ(
        frame.at("effect_transient_arena_alias_opportunity_count"), 2U);
    EXPECT_EQ(
        frame.at("effect_transient_arena_peak_live_resource_count"), 2U);
    EXPECT_EQ(
        frame.at("effect_transient_arena_allocated_slot_count"), 2U);
    EXPECT_EQ(
        frame.at("effect_transient_arena_reused_configuration_count"), 1U);
    EXPECT_EQ(frame.at("display_effect_graph_invocation_count"), 1U);
    EXPECT_EQ(frame.at("display_effect_graph_declared_pass_count"), 4U);
    EXPECT_EQ(frame.at("display_effect_graph_executed_pass_count"), 2U);
    EXPECT_EQ(frame.at("display_effect_graph_reused_pass_count"), 1U);
    EXPECT_EQ(frame.at("display_effect_graph_fused_pass_count"), 1U);
    EXPECT_EQ(frame.at("display_effect_graph_skipped_pass_count"), 0U);
    EXPECT_EQ(frame.at("display_effect_graph_failed_pass_count"), 0U);
    EXPECT_EQ(frame.at("display_effect_graph_unresolved_pass_count"), 0U);
    EXPECT_EQ(frame.at("display_effect_graph_active_binding_count"), 11U);
    EXPECT_EQ(frame.at("display_effect_graph_declared_pass_mask"), 0x47U);
    EXPECT_EQ(frame.at("display_effect_graph_executed_pass_mask"), 0x41U);
    EXPECT_EQ(frame.at("display_effect_graph_declared_read_count"), 2U);
    EXPECT_EQ(frame.at("display_effect_graph_resolved_read_count"), 1U);
    EXPECT_EQ(frame.at("display_effect_graph_missing_read_count"), 1U);
    EXPECT_EQ(frame.at("display_effect_graph_missing_read_mask"), 0x20U);
    EXPECT_EQ(frame.at("display_effect_graph_binding_fallback_count"), 1U);
    EXPECT_EQ(frame.at("pica_canonical_shader_draw_count"), 1U);
    EXPECT_EQ(frame.at("pica_instrumented_shader_draw_count"), 1U);
    EXPECT_EQ(
        frame.at("pica_shader_instrumentation_requested_draw_count"), 1U);
    EXPECT_EQ(frame.at("pica_direct_fragment_hook_draw_count"), 1U);
    EXPECT_EQ(
        frame.at("pica_legacy_fragment_hook_analysis_draw_count"), 0U);
    EXPECT_EQ(
        frame.at("pica_direct_temporal_vertex_program_draw_count"), 1U);
    EXPECT_EQ(
        frame.at("pica_legacy_temporal_vertex_analysis_draw_count"), 0U);
    EXPECT_EQ(frame.at("pica_canonical_shader_cache_entries"), 3U);
    EXPECT_EQ(
        frame.at("pica_instrumentation_shader_cache_entries"), 2U);
    EXPECT_EQ(frame.at("pica_graphics_pipeline_cache_entries"), 39U);
    EXPECT_EQ(frame.at("pica_pipeline_manifest_entries"), 41U);
    EXPECT_TRUE(frame.at("pica_pipeline_prewarm_enabled"));
    EXPECT_EQ(frame.at("pica_pipeline_prewarm_created"), 37U);
    EXPECT_EQ(frame.at("pica_pipeline_prewarm_reused"), 2U);
    EXPECT_EQ(frame.at("pica_pipeline_prewarm_skipped"), 1U);
    EXPECT_EQ(frame.at("pica_canonical_output_audit_count"), 5U);
    EXPECT_EQ(
        frame.at("pica_canonical_output_contract_reject_count"), 0U);
    EXPECT_EQ(
        frame.at("pica_canonical_compatibility_analysis_count"), 0U);
    EXPECT_EQ(
        frame.at("pica_typed_instrumentation_contract_reject_count"), 0U);
    const auto& nativePicaCpu = frame.at("native_pica_cpu");
    EXPECT_EQ(nativePicaCpu.at("draw_count"), 2U);
    EXPECT_DOUBLE_EQ(nativePicaCpu.at("total_ms").get<double>(), 4.0);
    EXPECT_DOUBLE_EQ(
        nativePicaCpu.at("shader_variant_ms").get<double>(), 0.3);
    EXPECT_EQ(
        nativePicaCpu.at("shader_variant_cache_hits"), 1U);
    EXPECT_EQ(
        nativePicaCpu.at("shader_variant_cache_misses"), 1U);
    EXPECT_EQ(
        nativePicaCpu.at("shader_variant_cache_entries"), 7U);
    EXPECT_DOUBLE_EQ(
        nativePicaCpu.at("shader_cache_ms").get<double>(), 0.3);
    EXPECT_DOUBLE_EQ(nativePicaCpu.at("texture_ms").get<double>(), 0.7);
    EXPECT_DOUBLE_EQ(
        nativePicaCpu.at("draw_state_ms").get<double>(), 0.35);
    EXPECT_DOUBLE_EQ(
        nativePicaCpu.at("grass_surface_ms").get<double>(), 0.09);
    EXPECT_DOUBLE_EQ(
        nativePicaCpu.at("scene_state_ms").get<double>(), 0.11);
    EXPECT_DOUBLE_EQ(
        nativePicaCpu.at("render_target_state_ms").get<double>(), 0.03);
    EXPECT_DOUBLE_EQ(
        nativePicaCpu.at("directional_shadow_ms").get<double>(), 0.04);
    EXPECT_DOUBLE_EQ(
        nativePicaCpu.at("grass_render_ms").get<double>(), 0.03);
    EXPECT_DOUBLE_EQ(
        nativePicaCpu.at("reflection_environment_ms").get<double>(), 0.05);
    EXPECT_DOUBLE_EQ(
        nativePicaCpu.at("temporal_state_ms").get<double>(), 0.15);
    EXPECT_DOUBLE_EQ(nativePicaCpu.at("pipeline_ms").get<double>(), 0.55);
    EXPECT_DOUBLE_EQ(nativePicaCpu.at("upload_ms").get<double>(), 0.75);
    EXPECT_DOUBLE_EQ(
        nativePicaCpu.at("descriptor_ms").get<double>(), 0.5);
    EXPECT_DOUBLE_EQ(nativePicaCpu.at("command_ms").get<double>(), 0.55);
    EXPECT_EQ(nativePicaCpu.at("geometry_registry_hits"), 1U);
    EXPECT_EQ(nativePicaCpu.at("geometry_registry_misses"), 1U);
    EXPECT_EQ(nativePicaCpu.at("geometry_dynamic_builds"), 1U);
    EXPECT_EQ(nativePicaCpu.at("geometry_registry_entries"), 7U);
    EXPECT_EQ(nativePicaCpu.at("geometry_persistent_draws"), 1U);
    EXPECT_EQ(nativePicaCpu.at("geometry_transient_draws"), 1U);
    EXPECT_EQ(nativePicaCpu.at("geometry_persistent_upload_bytes"), 3072U);
    EXPECT_EQ(nativePicaCpu.at("geometry_transient_upload_bytes"), 1536U);
    EXPECT_EQ(frame.at("vulkan_adapter_count"), 2U);
    EXPECT_EQ(frame.at("vulkan_adapter_index"), 1U);
    EXPECT_EQ(frame.at("vulkan_adapter_vendor_id"), 0x10deU);
    EXPECT_EQ(frame.at("vulkan_adapter_device_id"), 0x2504U);
    EXPECT_EQ(frame.at("vulkan_graphics_queue_family"), 3U);
    EXPECT_EQ(frame.at("vulkan_present_queue_family"), 3U);
    EXPECT_EQ(frame.at("vulkan_graphics_queue_count"), 8U);
    EXPECT_EQ(
        frame.at("vulkan_requested_graphics_queue_count"), 2U);
    EXPECT_EQ(frame.at("vulkan_present_queue_index"), 1U);
    EXPECT_TRUE(frame.at("vulkan_async_present_supported"));
    EXPECT_TRUE(frame.at("nri_swapchain_queue_eligible"));
    EXPECT_EQ(frame.at("nri_swapchain_fallback_reason"), 0U);
    EXPECT_TRUE(frame.at("nri_swapchain_fallback_detail")
                    .get<std::string>()
                    .empty());
    EXPECT_EQ(frame.at("spatial_aa_mode"), 2U);
    EXPECT_EQ(frame.at("msaa_samples"), 1U);
    EXPECT_FLOAT_EQ(
        frame.at("internal_resolution_scale").get<float>(), 1.5F);
    EXPECT_EQ(frame.at("output_width"), 1920U);
    EXPECT_EQ(frame.at("output_height"), 1080U);
    EXPECT_EQ(frame.at("internal_target_width"), 720U);
    EXPECT_EQ(frame.at("internal_target_height"), 1280U);
    EXPECT_EQ(frame.at("smaa_1x_pass_count"), 1U);
    EXPECT_EQ(frame.at("smaa_edge_pass_count"), 1U);
    EXPECT_EQ(frame.at("smaa_blend_weight_pass_count"), 1U);
    EXPECT_EQ(frame.at("smaa_neighborhood_pass_count"), 1U);
    EXPECT_TRUE(frame.at("smaa_outputs_nri_owned").get<bool>());
    EXPECT_TRUE(frame.at("smaa_lookups_nri_owned").get<bool>());
    EXPECT_TRUE(frame.at("smaa_compute_nri_owned").get<bool>());
    EXPECT_TRUE(frame.at("smaa_barriers_nri_owned").get<bool>());
    EXPECT_TRUE(
        frame.at("smaa_lookup_upload_nri_owned").get<bool>());
    EXPECT_EQ(frame.at("temporal_prepare_count"), 1U);
    EXPECT_EQ(frame.at("temporal_valid_count"), 1U);
    EXPECT_EQ(frame.at("temporal_camera_cut_count"), 0U);
    EXPECT_EQ(frame.at("motion_vector_pass_count"), 1U);
    EXPECT_EQ(frame.at("motion_history_valid_count"), 1U);
    EXPECT_TRUE(frame.at("motion_nri_wrapped").get<bool>());
    EXPECT_TRUE(frame.at("motion_outputs_nri_owned").get<bool>());
    EXPECT_TRUE(frame.at("motion_compute_nri_owned").get<bool>());
    EXPECT_TRUE(frame.at("motion_barriers_nri_owned").get<bool>());
    EXPECT_EQ(frame.at("reactive_world_draw_count"), 1U);
    EXPECT_EQ(frame.at("temporal_aa_pass_count"), 1U);
    EXPECT_TRUE(frame.at("temporal_aa_history_nri_owned").get<bool>());
    EXPECT_TRUE(frame.at("temporal_aa_compute_nri_owned").get<bool>());
    EXPECT_TRUE(frame.at("temporal_aa_barriers_nri_owned").get<bool>());
    EXPECT_TRUE(frame.at("scene_composite_output_nri_owned").get<bool>());
    EXPECT_TRUE(frame.at("scene_composite_compute_nri_owned").get<bool>());
    EXPECT_TRUE(frame.at("scene_composite_barriers_nri_owned").get<bool>());
    EXPECT_TRUE(frame.at("cacao_output_nri_owned").get<bool>());
    EXPECT_EQ(frame.at("cacao_normal_guide_pass_count"), 1U);
    EXPECT_EQ(frame.at("cacao_ambient_guide_draw_count"), 2U);
    EXPECT_EQ(frame.at("cacao_ambient_rgb_guide_draw_count"), 1U);
    EXPECT_EQ(frame.at("cacao_ambient_fallback_guide_draw_count"), 1U);
    EXPECT_EQ(frame.at("cacao_ambient_composite_count"), 1U);
    EXPECT_EQ(frame.at("toon_draw_count"), 1U);
    EXPECT_EQ(frame.at("pica_material_toon_draw_count"), 1U);
    EXPECT_TRUE(frame.at("vulkan_validation_enabled").get<bool>());
    EXPECT_EQ(frame.at("vulkan_validation_warning_count"), 2U);
    EXPECT_EQ(frame.at("vulkan_validation_error_count"), 0U);
    EXPECT_TRUE(frame.at("nri_validation_enabled").get<bool>());
    EXPECT_EQ(frame.at("nri_validation_info_count"), 3U);
    EXPECT_EQ(frame.at("nri_validation_error_count"), 0U);
    EXPECT_EQ(frame.at("nis_upscale_pass_count"), 1U);
    EXPECT_EQ(frame.at("nis_input_width"), 1280U);
    EXPECT_EQ(frame.at("nis_output_width"), 1920U);
    EXPECT_TRUE(frame.at("nis_nri_dispatched"));
    EXPECT_EQ(frame.at("fsr_upscale_pass_count"), 1U);
    EXPECT_EQ(frame.at("fsr_history_reset_count"), 1U);
    EXPECT_TRUE(frame.at("fsr_nri_dispatched"));
    EXPECT_EQ(frame.at("dlss_upscale_pass_count"), 1U);
    EXPECT_EQ(frame.at("dlss_history_reset_count"), 1U);
    EXPECT_TRUE(frame.at("dlss_nri_dispatched"));
    EXPECT_TRUE(frame.at("d3d12_ngx_frame_requested"));
    EXPECT_TRUE(frame.at("d3d12_ngx_frame_contract_ready"));
    EXPECT_TRUE(frame.at("d3d12_ngx_frame_prepared"));
    EXPECT_TRUE(frame.at("d3d12_ngx_frame_split_submitted"));
    EXPECT_TRUE(frame.at("d3d12_ngx_frame_queued"));
    EXPECT_TRUE(frame.at("d3d12_ngx_output_acquired"));
    EXPECT_TRUE(frame.at("upscaler_output_nri_owned"));
    EXPECT_TRUE(frame.at("upscaler_barriers_nri_owned"));
    EXPECT_EQ(frame.at("nri_scanout_count"), 1U);
    EXPECT_EQ(frame.at("native_pica_dynamic_rendering_count"), 1U);
    EXPECT_TRUE(frame.at("nri_pica_rendering_scope_owned"));
    EXPECT_EQ(frame.at("nri_pica_global_barrier_count"), 2U);
    EXPECT_EQ(frame.at("nri_pica_pipeline_bind_count"), 1U);
    EXPECT_TRUE(frame.at("nri_pica_pipeline_wrapped"));
    EXPECT_EQ(frame.at("nri_pica_shader_contract_count"), 1U);
    EXPECT_TRUE(frame.at("nri_pica_descriptor_layout_owned"));
    EXPECT_EQ(frame.at("nri_pica_owned_pipeline_draw_count"), 1U);
    EXPECT_EQ(frame.at("nri_pica_owned_draw_count"), 1U);
    EXPECT_TRUE(frame.at("nri_pica_descriptors_owned"));
    EXPECT_TRUE(frame.at("nri_pica_uploads_owned"));
    EXPECT_EQ(frame.at("nri_pica_owned_upload_draw_count"), 1U);
    EXPECT_EQ(frame.at("nri_pica_upload_bytes"), 4096U);
    EXPECT_EQ(frame.at("nri_pica_texture_upload_count"), 1U);
    EXPECT_EQ(frame.at("nri_pica_texture_upload_bytes"), 16384U);
    EXPECT_TRUE(frame.at("nri_pica_texture_upload_owned"));
    EXPECT_EQ(frame.at("nri_pica_owned_texture_image_count"), 1U);
    EXPECT_TRUE(frame.at("nri_pica_texture_images_owned"));
    EXPECT_EQ(frame.at("nri_pica_owned_display_image_count"), 1U);
    EXPECT_TRUE(frame.at("nri_pica_display_images_owned"));
    EXPECT_EQ(frame.at("nri_pica_owned_render_target_count"), 1U);
    EXPECT_EQ(frame.at("nri_pica_owned_render_target_image_count"), 11U);
    EXPECT_TRUE(frame.at("nri_pica_render_targets_owned"));
    EXPECT_EQ(
        frame.at("nri_pica_render_target_initialization_count"), 1U);
    EXPECT_EQ(
        frame.at("nri_pica_initialized_render_target_image_count"), 11U);
    EXPECT_EQ(
        frame.at("nri_pica_render_target_initialization_barrier_count"),
        12U);
    EXPECT_TRUE(
        frame.at("nri_pica_render_target_initialization_owned"));
    EXPECT_EQ(frame.at("nri_pica_display_copy_count"), 1U);
    EXPECT_EQ(frame.at("nri_pica_display_copy_barrier_count"), 4U);
    EXPECT_TRUE(frame.at("nri_pica_display_copies_owned"));
    EXPECT_EQ(
        frame.at("nri_pica_memory_fill_shadow_clear_count"), 1U);
    EXPECT_EQ(
        frame.at("nri_pica_memory_fill_attachment_clear_count"), 1U);
    EXPECT_EQ(
        frame.at("nri_pica_memory_fill_clear_barrier_count"), 2U);
    EXPECT_TRUE(frame.at("nri_pica_memory_fill_clears_owned"));
    EXPECT_TRUE(frame.at("nri_scanout_pipeline_owned"));
    EXPECT_TRUE(frame.at("nri_scanout_descriptors_owned"));
    EXPECT_TRUE(frame.at("nri_scanout_scope_owned"));
    EXPECT_EQ(frame.at("linear_scanout_count"), 1U);
    EXPECT_EQ(frame.at("linear_scanout_srgb_encode_count"), 1U);
    EXPECT_EQ(frame.at("nri_swapchain_acquire_count"), 1U);
    EXPECT_EQ(frame.at("nri_swapchain_present_count"), 1U);
    EXPECT_EQ(frame.at("nri_swapchain_image_count"), 3U);
    EXPECT_TRUE(frame.at("nri_swapchain_owned"));
    EXPECT_TRUE(frame.at("nri_swapchain_synchronization_owned"));
    EXPECT_TRUE(frame.at("nri_present_worker_bypassed"));
    EXPECT_EQ(frame.at("presentation_apply_count"), 1U);
    EXPECT_EQ(frame.at("presentation_rollback_count"), 2U);
    EXPECT_EQ(frame.at("presentation_apply_failure_count"), 1U);
    EXPECT_TRUE(frame.at("presentation_settings_applied"));
    EXPECT_EQ(frame.at("presentation_window_mode"), 1U);
    EXPECT_EQ(frame.at("presentation_requested_width"), 2560U);
    EXPECT_EQ(frame.at("presentation_requested_height"), 1440U);
    EXPECT_EQ(frame.at("presentation_swapchain_width"), 3440U);
    EXPECT_EQ(frame.at("presentation_swapchain_height"), 1440U);
    EXPECT_FALSE(frame.at("presentation_vsync"));
    EXPECT_EQ(frame.at("temporal_aa_history_valid_count"), 1U);
    EXPECT_TRUE(frame.at("temporal_aa_nri_wrapped").get<bool>());
    EXPECT_EQ(frame.at("scene_composite_pass_count"), 1U);
    EXPECT_EQ(frame.at("temporal_jitter_draw_count"), 1U);
    EXPECT_EQ(frame.at("rigid_motion_draw_count"), 1U);
    EXPECT_EQ(frame.at("exact_motion_draw_count"), 1U);
    EXPECT_EQ(frame.at("deformed_motion_draw_count"), 1U);
    EXPECT_EQ(frame.at("grass_motion_blade_count"), 42U);
    EXPECT_EQ(frame.at("hiz_pass_count"), 1U);
    EXPECT_EQ(frame.at("hiz_mip_count"), 10U);
    EXPECT_EQ(frame.at("hiz_depth_convention"), 2U);
    EXPECT_TRUE(frame.at("hiz_output_nri_owned").get<bool>());
    EXPECT_TRUE(frame.at("hiz_compute_nri_owned").get<bool>());
    EXPECT_TRUE(frame.at("hiz_barriers_nri_owned").get<bool>());
    EXPECT_EQ(frame.at("hiz_reflection_count"), 1U);
    EXPECT_EQ(frame.at("hiz_reflection_filter_count"), 1U);
    EXPECT_TRUE(frame.at("reflection_outputs_nri_owned").get<bool>());
    EXPECT_TRUE(frame.at("reflection_compute_nri_owned").get<bool>());
    EXPECT_TRUE(frame.at("reflection_barriers_nri_owned").get<bool>());
    EXPECT_EQ(frame.at("reflection_debug_view"), 4U);
    EXPECT_TRUE(frame.at("reflection_nri_wrapped").get<bool>());
    EXPECT_EQ(frame.at("linear_working_color_count"), 1U);
    EXPECT_TRUE(frame.at("linear_working_color_source_srgb").get<bool>());
    EXPECT_TRUE(
        frame.at("linear_working_color_output_nri_owned").get<bool>());
    EXPECT_TRUE(
        frame.at("linear_working_color_compute_nri_owned").get<bool>());
    EXPECT_TRUE(
        frame.at("linear_working_color_barriers_nri_owned").get<bool>());
    EXPECT_EQ(frame.at("fidelityfx_sssr_count"), 1U);
    EXPECT_EQ(frame.at("fidelityfx_sssr_history_valid_count"), 1U);
    EXPECT_TRUE(frame.at("fidelityfx_sssr_nri_wrapped").get<bool>());
    EXPECT_TRUE(
        frame.at("fidelityfx_sssr_output_nri_owned").get<bool>());
    EXPECT_TRUE(frame.at("fidelityfx_sssr_linear_input").get<bool>());
    EXPECT_EQ(frame.at("fidelityfx_sssr_fallback_count"), 1U);
    EXPECT_EQ(frame.at("fidelityfx_sssr_fallback_reason"),
              "synthetic fallback");
    EXPECT_EQ(frame.at("reflection_ibl_count"), 1U);
    EXPECT_EQ(frame.at("reflection_ibl_profile_update_count"), 1U);
    EXPECT_TRUE(frame.at("reflection_ibl_pica_derived").get<bool>());
    EXPECT_EQ(frame.at("reflection_ibl_environment_mip_count"), 6U);
    EXPECT_TRUE(
        frame.at("reflection_ibl_environment_nri_owned").get<bool>());
    EXPECT_TRUE(frame.at("reflection_ibl_brdf_nri_owned").get<bool>());
    EXPECT_TRUE(frame.at("reflection_ibl_compute_nri_owned").get<bool>());
    EXPECT_TRUE(frame.at("reflection_ibl_barriers_nri_owned").get<bool>());
    EXPECT_EQ(frame.at("reflection_material_resolve_count"), 1U);
    EXPECT_TRUE(
        frame.at("reflection_material_resolve_output_nri_owned")
            .get<bool>());
    EXPECT_TRUE(
        frame.at("reflection_material_resolve_compute_nri_owned")
            .get<bool>());
    EXPECT_TRUE(
        frame.at("reflection_material_resolve_barriers_nri_owned")
            .get<bool>());
    EXPECT_EQ(frame.at("reflection_material_candidate_draw_count"), 2U);
    EXPECT_EQ(frame.at("reflection_material_calibrated_draw_count"), 1U);
    EXPECT_EQ(frame.at("reflection_material_profiled_draw_count"), 1U);
    EXPECT_EQ(
        frame.at("reflection_material_no_specular_tev_draw_count"), 1U);
    EXPECT_EQ(frame.at("reflection_material_unsupported_draw_count"), 0U);
    ASSERT_EQ(frame.at("reflection_texture_usages").size(), 2U);
    const auto& profiledTexture =
        frame.at("reflection_texture_usages").front();
    EXPECT_EQ(profiledTexture.at("texture_hash"),
              "be15aff93dfdcd88");
    EXPECT_EQ(profiledTexture.at("width"), 64U);
    EXPECT_EQ(profiledTexture.at("height"), 32U);
    EXPECT_EQ(profiledTexture.at("mapper_slot"), 1U);
    EXPECT_EQ(profiledTexture.at("draw_count"), 2U);
    EXPECT_EQ(profiledTexture.at("profiled_draw_count"), 2U);
    EXPECT_TRUE(profiledTexture.at("profiled").get<bool>());
    EXPECT_EQ(profiledTexture.at("rule_id"), 7U);
    EXPECT_EQ(profiledTexture.at("profile"), 2U);
    EXPECT_FLOAT_EQ(profiledTexture.at("reflectivity"), 0.58F);
    EXPECT_FLOAT_EQ(profiledTexture.at("roughness"), 0.32F);
    EXPECT_EQ(
        frame.at("reflection_texture_usages")[1].at("texture_hash"),
        "0123456789abcdef");
    EXPECT_FALSE(
        frame.at("reflection_texture_usages")[1]
            .at("profiled")
            .get<bool>());
    EXPECT_EQ(
        frame.at("reflection_texture_usage_overflow_count"), 0U);
    EXPECT_EQ(frame.at("last_depth_physical_address"), 0x4000U);
    EXPECT_DOUBLE_EQ(frame.at("gpu").at("frame_ms"), 4.5);
    EXPECT_DOUBLE_EQ(frame.at("gpu").at("toon_raster_ms"), 1.5);
    EXPECT_DOUBLE_EQ(frame.at("gpu").at("cacao_ms"), 0.75);
    EXPECT_DOUBLE_EQ(
        frame.at("gpu").at("depth_preparation_ms"), 0.25);
    EXPECT_DOUBLE_EQ(frame.at("gpu").at("reflection_ms"), 0.5);
    EXPECT_DOUBLE_EQ(
        frame.at("gpu").at("anti_aliasing_ms"), 0.125);
    EXPECT_TRUE(frame.at("gpu").at("grass_ms").is_null());
    EXPECT_TRUE(frame.at("gpu").at("scanout_ms").is_null());

    std::filesystem::remove(output, ignored);
}

TEST(Oot3dVulkanDiagnostics, BoundsReflectionTextureInventory) {
    const auto output = std::filesystem::temp_directory_path() /
                        "oot3d_reflection_texture_inventory_test.json";
    std::error_code ignored;
    std::filesystem::remove(output, ignored);

    Fast::Oot3dVulkanDiagnosticsConfig config;
    config.OutputPath = output;
    config.MaximumFrames = 1U;
    Fast::Oot3dVulkanDiagnostics diagnostics(config);
    diagnostics.BeginFrame(1U);
    for (uint64_t hash = 1U; hash <= 98U; ++hash) {
        diagnostics.RecordReflectionTextureUsage(
            hash, 32U, 32U, 0U);
    }
    diagnostics.EndFrame();

    ASSERT_EQ(diagnostics.Frames().size(), 1U);
    EXPECT_EQ(
        diagnostics.Frames().front().ReflectionTextureUsages.size(),
        96U);
    EXPECT_EQ(
        diagnostics.Frames().front()
            .ReflectionTextureUsageOverflowCount,
        2U);
    std::filesystem::remove(output, ignored);
}

} // namespace
