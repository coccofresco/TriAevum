#pragma once

#include "fast/oot3d/display_effect_plan.h"
#include "fast/oot3d/display_effect_resources.h"
#include "fast/oot3d/effect_geometry_provider_plan.h"
#include "fast/oot3d/effect_graph_physical_plan.h"
#include "fast/oot3d/renderer_validation_telemetry.h"
#include "oot3d/renderer/pica_render_backend.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Fast {

namespace Oot3d {
struct PicaSceneFrameStats;
}
namespace Renderer3ds {
struct PicaCompositionScheduleStats;
}

struct Oot3dVulkanDiagnosticsConfig {
    std::filesystem::path OutputPath;
    size_t MaximumFrames = 240;
    // Default capture is memory-only until an explicit Flush (also at shutdown).
    // Periodic synchronous snapshots are an opt-in debugging tradeoff.
    size_t FlushIntervalFrames = 0;

    [[nodiscard]] bool Enabled() const;
    static Oot3dVulkanDiagnosticsConfig FromEnvironment();
};

struct Oot3dVulkanGpuTimings {
    std::optional<double> FrameMilliseconds;
    std::optional<double> NativePicaMilliseconds;
    std::optional<double> ToonRasterMilliseconds;
    std::optional<double> GrassMilliseconds;
    std::optional<double> CacaoMilliseconds;
    std::optional<double> DepthPreparationMilliseconds;
    std::optional<double> ReflectionMilliseconds;
    std::optional<double> MotionVectorsMilliseconds;
    std::optional<double> SceneCompositeMilliseconds;
    std::optional<double> AntiAliasingMilliseconds;
    std::optional<double> UpscalerMilliseconds;
    std::optional<double> DisplayTransferMilliseconds;
    std::optional<double> ScanoutMilliseconds;
    std::optional<double> OverlayMilliseconds;
};

struct Oot3dVulkanNativePicaCpuTimings {
    uint32_t DrawCount = 0;
    double TotalMilliseconds = 0.0;
    double ShaderVariantMilliseconds = 0.0;
    uint32_t ShaderVariantCacheHits = 0;
    uint32_t ShaderVariantCacheMisses = 0;
    uint32_t ShaderVariantCacheEntries = 0;
    double ShaderCacheMilliseconds = 0.0;
    double TextureMilliseconds = 0.0;
    double DrawStateMilliseconds = 0.0;
    double GrassSurfaceMilliseconds = 0.0;
    double SceneStateMilliseconds = 0.0;
    double RenderTargetStateMilliseconds = 0.0;
    double DirectionalShadowMilliseconds = 0.0;
    double GrassRenderMilliseconds = 0.0;
    double ReflectionEnvironmentMilliseconds = 0.0;
    double TemporalStateMilliseconds = 0.0;
    double PipelineMilliseconds = 0.0;
    double UploadMilliseconds = 0.0;
    double DescriptorMilliseconds = 0.0;
    double CommandMilliseconds = 0.0;
    uint32_t GeometryRegistryHits = 0;
    uint32_t GeometryRegistryMisses = 0;
    uint32_t GeometryDynamicBuilds = 0;
    uint32_t GeometryRegistryEntries = 0;
    uint32_t GeometryPersistentDraws = 0;
    uint32_t GeometryTransientDraws = 0;
    uint64_t GeometryPersistentUploadBytes = 0;
    uint64_t GeometryTransientUploadBytes = 0;
};

enum class Oot3dVulkanPresentationFallbackReason : uint8_t {
    None = 0,
    MissingSurfaceCapabilities2,
    SeparateQueueFamilies,
    NriInitializationFailed,
    NriCreationFailed,
};

struct Oot3dVulkanAdapterDiagnostics {
    uint32_t AdapterCount = 0;
    uint32_t AdapterIndex = 0;
    uint32_t VendorId = 0;
    uint32_t DeviceId = 0;
    uint32_t GraphicsQueueFamily = 0;
    uint32_t PresentQueueFamily = 0;
    uint32_t GraphicsQueueCount = 0;
    uint32_t RequestedGraphicsQueueCount = 0;
    uint32_t PresentQueueIndex = 0;
    bool AsynchronousPresentSupported = false;
    bool NriSwapchainQueueEligible = false;
};

struct Oot3dD3d12NgxProviderDiagnostics {
    bool AdapterMatched = false;
    bool DeviceReady = false;
    bool NriReady = false;
    bool DlssFeatureReady = false;
    bool DlssContractResourcesReady = false;
    bool DlssEvaluateReady = false;
    bool ExternalMemoryReady = false;
    bool SharedFenceReady = false;
    bool ZeroCopyInteropReady = false;
    uint32_t VendorId = 0;
    uint32_t DeviceId = 0;
    uint32_t ProbeInputWidth = 0;
    uint32_t ProbeInputHeight = 0;
    uint32_t ProbeOutputWidth = 0;
    uint32_t ProbeOutputHeight = 0;
    std::string AdapterName;
    std::string Detail;
    bool FrameBridgeReady = false;
    bool FrameDispatchRequested = false;
    uint64_t FrameDispatchCount = 0;
};

struct Oot3dVulkanPresentationStateDiagnostics {
    bool Applied = false;
    uint8_t WindowMode = 0;
    uint32_t RequestedWidth = 0;
    uint32_t RequestedHeight = 0;
    uint32_t SwapchainWidth = 0;
    uint32_t SwapchainHeight = 0;
    bool VSync = true;
};

struct Oot3dReflectionTextureUsageDiagnostics {
    uint64_t ContentHash = 0;
    uint16_t Width = 0;
    uint16_t Height = 0;
    uint8_t MapperSlot = 0;
    uint32_t DrawCount = 0;
    uint32_t ProfiledDrawCount = 0;
    uint32_t RuleId = 0;
    uint8_t Profile = 0;
    float Reflectivity = 0.0F;
    float Roughness = 1.0F;
};

struct Oot3dVulkanFrameDiagnostics {
    uint64_t FrameIndex = 0;
    double CpuFrameMilliseconds = 0.0;
    Oot3dVulkanGpuTimings Gpu;
    uint32_t VulkanAdapterCount = 0;
    uint32_t VulkanAdapterIndex = 0;
    uint32_t VulkanAdapterVendorId = 0;
    uint32_t VulkanAdapterDeviceId = 0;
    uint32_t VulkanGraphicsQueueFamily = 0;
    uint32_t VulkanPresentQueueFamily = 0;
    uint32_t VulkanGraphicsQueueCount = 0;
    uint32_t VulkanRequestedGraphicsQueueCount = 0;
    uint32_t VulkanPresentQueueIndex = 0;
    bool VulkanAsyncPresentSupported = false;
    bool NriSwapchainQueueEligible = false;
    uint32_t NriSwapchainFallbackReason = 0;
    std::string NriSwapchainFallbackDetail;
    bool VulkanValidationEnabled = false;
    uint64_t VulkanValidationInfoCount = 0;
    uint64_t VulkanValidationWarningCount = 0;
    uint64_t VulkanValidationErrorCount = 0;
    bool NriValidationEnabled = false;
    uint64_t NriValidationInfoCount = 0;
    uint64_t NriValidationWarningCount = 0;
    uint64_t NriValidationErrorCount = 0;
    uint32_t NativePicaDrawCount = 0;
    uint32_t NativePicaAlphaTestDrawCount = 0;
    uint32_t NativePicaAlphaToCoverageDrawCount = 0;
    uint64_t NativePicaVertexCount = 0;
    uint32_t NativePicaIndexedDrawCount = 0;
    bool PicaNativeFidelityProfile = false;
    uint32_t PicaColorAttachmentCount = 0;
    uint32_t PicaAuxiliaryOutputMask = 0;
    uint32_t EffectGraphPassCount = 0;
    uint32_t EffectGraphResourceCount = 0;
    uint32_t EffectGraphDeclaredBindingCount = 0;
    uint32_t EffectGraphDeclaredBarrierCount = 0;
    uint32_t EffectGraphGuideImageBarrierCount = 0;
    uint32_t EffectGraphInternalImageTransitionCount = 0;
    uint32_t EffectGraphElidedImageTransitionCount = 0;
    uint32_t EffectProviderPrivateImageTransitionCount = 0;
    uint32_t EffectProviderPrivateElidedImageTransitionCount = 0;
    bool EffectGeometryProviderDeclared = false;
    uint32_t EffectGeometryProviderStage = 0U;
    uint32_t EffectGeometrySourceInspectionCount = 0U;
    uint32_t EffectGeometrySourcePublishedCount = 0U;
    uint32_t EffectGeometryDeclaredBoundaryInvocationCount = 0U;
    uint32_t EffectGeometryFallbackInvocationCount = 0U;
    uint32_t EffectGeometryExecutedCount = 0U;
    uint32_t EffectGeometryReusedCount = 0U;
    uint32_t EffectGeometrySkippedCount = 0U;
    uint32_t EffectGeometryFailedCount = 0U;
    uint32_t EffectGeometryInputUnavailableCount = 0U;
    uint32_t EffectGeometryScheduleRejectedCount = 0U;
    uint32_t DisplayEffectGraphInvocationCount = 0;
    uint32_t DisplayEffectGraphDeclaredPassCount = 0;
    uint32_t DisplayEffectGraphExecutedPassCount = 0;
    uint32_t DisplayEffectGraphReusedPassCount = 0;
    uint32_t DisplayEffectGraphFusedPassCount = 0;
    uint32_t DisplayEffectGraphSkippedPassCount = 0;
    uint32_t DisplayEffectGraphFailedPassCount = 0;
    uint32_t DisplayEffectGraphUnresolvedPassCount = 0;
    uint32_t DisplayEffectGraphActiveBindingCount = 0;
    uint32_t DisplayEffectGraphUndeclaredRecordCount = 0;
    uint32_t DisplayEffectGraphDuplicateRecordCount = 0;
    uint32_t DisplayEffectGraphDeclaredPassMask = 0;
    uint32_t DisplayEffectGraphExecutedPassMask = 0;
    uint32_t DisplayEffectGraphReusedPassMask = 0;
    uint32_t DisplayEffectGraphFusedPassMask = 0;
    uint32_t DisplayEffectGraphSkippedPassMask = 0;
    uint32_t DisplayEffectGraphFailedPassMask = 0;
    uint32_t DisplayEffectGraphUnresolvedPassMask = 0;
    uint32_t DisplayEffectGraphDeclaredReadCount = 0;
    uint32_t DisplayEffectGraphResolvedReadCount = 0;
    uint32_t DisplayEffectGraphMissingReadCount = 0;
    uint32_t DisplayEffectGraphMissingReadMask = 0;
    uint32_t DisplayEffectGraphBindingFallbackCount = 0;
    uint32_t DisplayEffectPhysicalPlanInvocationCount = 0;
    uint32_t DisplayEffectPhysicalPlanCompleteCount = 0;
    uint32_t DisplayEffectPhysicalDeclaredResourceCount = 0;
    uint32_t DisplayEffectPhysicalResolvedResourceCount = 0;
    uint32_t DisplayEffectPhysicalMissingResourceCount = 0;
    uint32_t DisplayEffectPhysicalMissingResourceMask = 0;
    uint32_t DisplayEffectPhysicalSemanticResourceCount = 0;
    uint32_t DisplayEffectPhysicalExternalImageResourceCount = 0;
    uint32_t DisplayEffectPhysicalNativeAttachmentResourceCount = 0;
    uint32_t DisplayEffectPhysicalTransientImageResourceCount = 0;
    uint32_t DisplayEffectPhysicalTemporalHistoryResourceCount = 0;
    uint32_t DisplayEffectPhysicalImageCount = 0;
    uint32_t DisplayEffectPhysicalAliasEligibleResourceCount = 0;
    uint32_t DisplayEffectPhysicalAliasSlotCount = 0;
    uint32_t DisplayEffectPhysicalAliasOpportunityCount = 0;
    uint32_t DisplayEffectPhysicalPeakLiveTransientCount = 0;
    uint32_t EffectTransientArenaInvocationCount = 0;
    uint32_t EffectTransientArenaConfiguredCount = 0;
    uint32_t EffectTransientArenaResourceCount = 0;
    uint32_t EffectTransientArenaPhysicalSlotCount = 0;
    uint32_t EffectTransientArenaAliasOpportunityCount = 0;
    uint32_t EffectTransientArenaPeakLiveResourceCount = 0;
    uint32_t EffectTransientArenaAllocatedSlotCount = 0;
    uint32_t EffectTransientArenaReusedConfigurationCount = 0;
    uint32_t PicaCanonicalShaderDrawCount = 0;
    uint32_t PicaInstrumentedShaderDrawCount = 0;
    uint32_t PicaShaderInstrumentationRequestedDrawCount = 0;
    uint32_t PicaDirectFragmentHookDrawCount = 0;
    uint32_t PicaLegacyFragmentHookAnalysisDrawCount = 0;
    uint32_t PicaDirectTemporalVertexProgramDrawCount = 0;
    uint32_t PicaLegacyTemporalVertexAnalysisDrawCount = 0;
    uint32_t PicaCanonicalShaderCacheEntries = 0;
    uint32_t PicaInstrumentationShaderCacheEntries = 0;
    uint32_t PicaGraphicsPipelineCacheEntries = 0;
    uint32_t PicaPipelineManifestEntries = 0;
    bool PicaPipelinePrewarmEnabled = false;
    uint32_t PicaPipelinePrewarmCreated = 0;
    uint32_t PicaPipelinePrewarmReused = 0;
    uint32_t PicaPipelinePrewarmSkipped = 0;
    uint32_t PicaCanonicalOutputAuditCount = 0;
    uint32_t PicaCanonicalOutputContractRejectCount = 0;
    uint32_t PicaCanonicalCompatibilityAnalysisCount = 0;
    uint32_t PicaTypedInstrumentationContractRejectCount = 0;
    Oot3dVulkanNativePicaCpuTimings NativePicaCpu;
    uint32_t NativePicaDynamicRenderingCount = 0;
    bool NriPicaRenderingScopeOwned = false;
    uint32_t NriPicaGlobalBarrierCount = 0;
    uint32_t NriPicaPipelineBindCount = 0;
    bool NriPicaPipelineWrapped = false;
    uint32_t NriPicaShaderContractCount = 0;
    bool NriPicaDescriptorLayoutOwned = false;
    uint32_t NriPicaOwnedPipelineDrawCount = 0;
    uint32_t NriPicaOwnedDrawCount = 0;
    uint32_t PicaCompositionSequenceCount = 0;
    uint32_t PicaCompositionPublishedDrawCount = 0;
    uint32_t PicaCompositionConsumedDrawCount = 0;
    uint32_t PicaCompositionRunCount = 0;
    uint32_t PicaCompositionTargetCount = 0;
    uint32_t PicaCompositionStageAnchorCount = 0;
    uint32_t PicaCompositionWorldStageTargetCount = 0;
    uint32_t PicaCompositionNonSceneTargetCount = 0;
    uint32_t PicaCompositionUnknownLayerTargetCount = 0;
    uint32_t PicaCompositionMissingOpaqueWorldTargetCount = 0;
    uint32_t PicaCompositionNonContiguousWorldTargetCount = 0;
    uint32_t PicaCompositionNonCanonicalTailTargetCount = 0;
    uint32_t PicaCompositionExecutionMismatchCount = 0;
    uint32_t PicaDisplaySceneSnapshotCount = 0;
    uint32_t PicaDisplayUiSnapshotCount = 0;
    uint32_t PicaDisplayUnknownSnapshotCount = 0;
    uint32_t PicaDisplaySceneResolvedSnapshotCount = 0;
    uint32_t PicaSceneFrameDrawCount = 0;
    uint32_t PicaSceneFrameTemporalSampleCount = 0;
    uint32_t PicaSceneFrameSyntheticTemporalSampleCount = 0;
    uint8_t PicaSceneFrameTemporalSampleMultiplier = 0;
    uint8_t PicaSceneFrameTemporalSampleOrdinal = 0;
    uint32_t PicaSceneFrameSceneDomainDrawCount = 0;
    uint32_t PicaSceneFrameUiDomainDrawCount = 0;
    uint32_t PicaSceneFrameUnknownDomainDrawCount = 0;
    uint32_t PicaSceneFrameOpaqueWorldLayerDrawCount = 0;
    uint32_t PicaSceneFrameTransparentWorldLayerDrawCount = 0;
    uint32_t PicaSceneFrameAtmosphereLayerDrawCount = 0;
    uint32_t PicaSceneFrameUiLayerDrawCount = 0;
    uint32_t PicaSceneFrameUnknownLayerDrawCount = 0;
    uint32_t PicaSceneFrameNativeCmbPassProvenanceDrawCount = 0;
    uint32_t PicaSceneFrameNativeControlFlowProvenanceDrawCount = 0;
    uint32_t PicaSceneFrameNativeUiLifecycleProvenanceDrawCount = 0;
    uint32_t PicaSceneFrameUnknownProvenanceDrawCount = 0;
    uint32_t PicaSceneFrameResolvedRasterStateCount = 0;
    uint32_t PicaSceneFrameResolvedRenderTargetStateCount = 0;
    uint32_t PicaSceneFrameResolvedRenderTargetGpuResourceCount = 0;
    uint32_t PicaSceneFrameResolvedMaterialStateCount = 0;
    uint32_t PicaSceneFrameNativeLightingStateCount = 0;
    uint32_t PicaSceneFrameNativeFragmentLightingStateCount = 0;
    uint32_t PicaSceneFrameNativeFragmentLightingEnabledDrawCount = 0;
    uint32_t PicaSceneFrameNativeFogStateCount = 0;
    uint32_t PicaSceneFrameNativeTransformStateCount = 0;
    uint32_t PicaSceneFramePreviousNativeTransformStateCount = 0;
    uint32_t PicaSceneFrameNativeSkeletonStateCount = 0;
    uint32_t PicaSceneFramePreviousNativeSkeletonStateCount = 0;
    uint32_t PicaSceneFrameRejectedDrawCount = 0;
    uint32_t PicaSceneFrameBoundTextureCount = 0;
    uint32_t PicaSceneFrameShaderCatalogEntries = 0;
    uint32_t PicaSceneFrameVertexLayoutCatalogEntries = 0;
    uint32_t PicaSceneFrameCanonicalPipelineEntries = 0;
    uint32_t PicaSceneFrameCanonicalFullRegisterStateEntries = 0;
    bool NriPicaDescriptorsOwned = false;
    bool NriPicaUploadsOwned = false;
    uint32_t NriPicaOwnedUploadDrawCount = 0;
    uint64_t NriPicaUploadBytes = 0;
    uint32_t NriPicaTextureUploadCount = 0;
    uint64_t NriPicaTextureUploadBytes = 0;
    bool NriPicaTextureUploadOwned = false;
    uint32_t NriPicaOwnedTextureImageCount = 0;
    bool NriPicaTextureImagesOwned = false;
    uint32_t NriPicaOwnedDisplayImageCount = 0;
    bool NriPicaDisplayImagesOwned = false;
    uint32_t NriPicaOwnedRenderTargetCount = 0;
    uint32_t NriPicaOwnedRenderTargetImageCount = 0;
    bool NriPicaRenderTargetsOwned = false;
    uint32_t NriPicaRenderTargetInitializationCount = 0;
    uint32_t NriPicaInitializedRenderTargetImageCount = 0;
    uint32_t NriPicaRenderTargetInitializationBarrierCount = 0;
    bool NriPicaRenderTargetInitializationOwned = false;
    uint32_t NriPicaDisplayCopyCount = 0;
    uint32_t NriPicaDisplayCopyBarrierCount = 0;
    bool NriPicaDisplayCopiesOwned = false;
    uint32_t NriPicaMemoryFillShadowClearCount = 0;
    uint32_t NriPicaMemoryFillAttachmentClearCount = 0;
    uint32_t NriPicaMemoryFillClearBarrierCount = 0;
    bool NriPicaMemoryFillClearsOwned = false;
    uint32_t NriDirectionalShadowPassCount = 0;
    uint32_t NriDirectionalShadowCasterCount = 0;
    bool NriDirectionalShadowNativeLight = false;
    uint32_t NriDirectionalShadowAttemptCount = 0;
    uint32_t NriDirectionalShadowQueuedCasterCount = 0;
    uint32_t NriDirectionalShadowQueuedNativeLightCount = 0;
    uint32_t NriDirectionalShadowExecutionStage = 0;
    uint32_t NriDirectionalShadowLightCandidateCount = 0;
    uint32_t NriDirectionalShadowLightClusterCount = 0;
    float NriDirectionalShadowLightGeometryWeight = 0.0F;
    float NriDirectionalShadowLightDirectionX = 0.0F;
    float NriDirectionalShadowLightDirectionY = 0.0F;
    float NriDirectionalShadowLightDirectionZ = 0.0F;
    uint32_t NriDirectionalShadowReceiverEligibleDrawCount = 0;
    uint32_t NriDirectionalShadowReceiverLightingAvailableDrawCount = 0;
    uint32_t NriDirectionalShadowReceiverLightingEnabledDrawCount = 0;
    uint32_t NriDirectionalShadowReceiverAmbientOnlyDrawCount = 0;
    uint32_t NriDirectionalShadowReceiverDirectUnmatchedDrawCount = 0;
    uint32_t NriDirectionalShadowReceiverDirectMatchedDrawCount = 0;
    uint32_t NriDirectionalShadowReceiverHistoryBoundDrawCount = 0;
    uint32_t NriDirectionalShadowGraphImageTransitionCount = 0;
    uint32_t NriDirectionalShadowGraphElidedImageTransitionCount = 0;
    uint32_t NriDirectionalShadowScheduleBoundaryCount = 0;
    uint32_t NriDirectionalShadowScheduleAuthorizedCount = 0;
    uint32_t NriDirectionalShadowScheduleRejectedCount = 0;
    uint32_t NriDirectionalShadowScheduleDuplicateCount = 0;
    uint32_t NriDirectionalShadowScheduleSurfaceEndCount = 0;
    uint32_t DisplayTransferCount = 0;
    uint32_t ScanoutCount = 0;
    uint32_t OverlayCount = 0;
    uint32_t MemoryFillCount = 0;
    uint32_t ToonDrawCount = 0;
    uint32_t OutlineOcclusionDrawCount = 0;
    uint32_t PicaMaterialToonDrawCount = 0;
    uint32_t CacaoPassCount = 0;
    bool CacaoOutputNriOwned = false;
    uint32_t CacaoNormalGuidePassCount = 0;
    uint32_t CacaoAmbientGuideDrawCount = 0;
    uint32_t CacaoAmbientRgbGuideDrawCount = 0;
    uint32_t CacaoAmbientFallbackGuideDrawCount = 0;
    uint32_t CacaoAmbientCompositeCount = 0;
    uint32_t HiZPassCount = 0;
    uint32_t HiZMipCount = 0;
    uint32_t HiZDepthConvention = 0;
    bool HiZOutputNriOwned = false;
    bool HiZComputeNriOwned = false;
    bool HiZBarriersNriOwned = false;
    uint32_t HiZReflectionCount = 0;
    uint32_t HiZReflectionFilterCount = 0;
    uint32_t ReflectionDebugView = 0;
    bool ReflectionNriWrapped = false;
    bool ReflectionOutputsNriOwned = false;
    bool ReflectionComputeNriOwned = false;
    bool ReflectionBarriersNriOwned = false;
    uint32_t ReflectionMaterialCandidateDrawCount = 0;
    uint32_t ReflectionMaterialCalibratedDrawCount = 0;
    uint32_t ReflectionMaterialProfiledDrawCount = 0;
    uint32_t ReflectionMaterialNoSpecularTevDrawCount = 0;
    uint32_t ReflectionMaterialUnsupportedDrawCount = 0;
    std::vector<Oot3dReflectionTextureUsageDiagnostics>
        ReflectionTextureUsages;
    uint32_t ReflectionTextureUsageOverflowCount = 0;
    uint32_t LinearWorkingColorCount = 0;
    bool LinearWorkingColorSourceSrgb = false;
    bool LinearWorkingColorOutputNriOwned = false;
    bool LinearWorkingColorComputeNriOwned = false;
    bool LinearWorkingColorBarriersNriOwned = false;
    uint32_t FidelityFxSssrCount = 0;
    uint32_t FidelityFxSssrHistoryValidCount = 0;
    bool FidelityFxSssrNriWrapped = false;
    bool FidelityFxSssrOutputNriOwned = false;
    bool FidelityFxSssrLinearInput = false;
    uint32_t FidelityFxSssrFallbackCount = 0;
    std::string FidelityFxSssrFallbackReason;
    uint32_t ReflectionIblCount = 0;
    uint32_t ReflectionIblProfileUpdateCount = 0;
    bool ReflectionIblPicaDerived = false;
    uint32_t ReflectionIblEnvironmentMipCount = 0;
    bool ReflectionIblEnvironmentNriOwned = false;
    bool ReflectionIblBrdfNriOwned = false;
    bool ReflectionIblComputeNriOwned = false;
    bool ReflectionIblBarriersNriOwned = false;
    uint32_t ReflectionMaterialResolveCount = 0;
    bool ReflectionMaterialResolveOutputNriOwned = false;
    bool ReflectionMaterialResolveComputeNriOwned = false;
    bool ReflectionMaterialResolveBarriersNriOwned = false;
    uint32_t SpatialAaMode = 0;
    uint32_t MsaaSamples = 1;
    uint32_t Smaa1xPassCount = 0;
    uint32_t SmaaEdgePassCount = 0;
    uint32_t SmaaBlendWeightPassCount = 0;
    uint32_t SmaaNeighborhoodPassCount = 0;
    bool SmaaOutputsNriOwned = false;
    bool SmaaLookupsNriOwned = false;
    bool SmaaComputeNriOwned = false;
    bool SmaaBarriersNriOwned = false;
    bool SmaaLookupUploadNriOwned = false;
    uint32_t TemporalPrepareCount = 0;
    uint32_t TemporalValidCount = 0;
    uint32_t TemporalCameraCutCount = 0;
    uint32_t TemporalResetReason = 0;
    uint32_t MotionVectorPassCount = 0;
    uint32_t MotionHistoryValidCount = 0;
    bool MotionNriWrapped = false;
    bool MotionOutputsNriOwned = false;
    bool MotionComputeNriOwned = false;
    bool MotionBarriersNriOwned = false;
    uint32_t ReactiveWorldDrawCount = 0;
    uint32_t TemporalAaPassCount = 0;
    uint32_t TemporalAaHistoryValidCount = 0;
    bool TemporalAaNriWrapped = false;
    bool TemporalAaHistoryNriOwned = false;
    bool TemporalAaComputeNriOwned = false;
    bool TemporalAaBarriersNriOwned = false;
    uint32_t SceneCompositePassCount = 0;
    bool SceneCompositeOutputNriOwned = false;
    bool SceneCompositeComputeNriOwned = false;
    bool SceneCompositeBarriersNriOwned = false;
    uint32_t NisUpscalePassCount = 0;
    uint32_t NisInputWidth = 0;
    uint32_t NisInputHeight = 0;
    uint32_t NisOutputWidth = 0;
    uint32_t NisOutputHeight = 0;
    bool NisNriDispatched = false;
    uint32_t FsrUpscalePassCount = 0;
    uint32_t FsrInputWidth = 0;
    uint32_t FsrInputHeight = 0;
    uint32_t FsrOutputWidth = 0;
    uint32_t FsrOutputHeight = 0;
    uint32_t FsrHistoryResetCount = 0;
    bool FsrNriDispatched = false;
    uint32_t DlssUpscalePassCount = 0;
    uint32_t DlssInputWidth = 0;
    uint32_t DlssInputHeight = 0;
    uint32_t DlssOutputWidth = 0;
    uint32_t DlssOutputHeight = 0;
    uint32_t DlssHistoryResetCount = 0;
    bool DlssNriDispatched = false;
    bool D3d12NgxFrameRequested = false;
    bool D3d12NgxFrameContractReady = false;
    bool D3d12NgxFramePrepared = false;
    bool D3d12NgxFrameSplitSubmitted = false;
    bool D3d12NgxFrameQueued = false;
    bool D3d12NgxOutputAcquired = false;
    bool UpscalerOutputNriOwned = false;
    bool UpscalerBarriersNriOwned = false;
    uint32_t NriScanoutCount = 0;
    bool NriScanoutPipelineOwned = false;
    bool NriScanoutDescriptorsOwned = false;
    bool NriScanoutScopeOwned = false;
    uint32_t LinearScanoutCount = 0;
    uint32_t LinearScanoutSrgbEncodeCount = 0;
    uint32_t NriSwapchainAcquireCount = 0;
    uint32_t NriSwapchainPresentCount = 0;
    uint32_t NriSwapchainImageCount = 0;
    bool NriSwapchainOwned = false;
    bool NriSwapchainSynchronizationOwned = false;
    bool NriPresentWorkerBypassed = false;
    uint32_t PresentationApplyCount = 0;
    uint32_t PresentationRollbackCount = 0;
    uint32_t PresentationApplyFailureCount = 0;
    bool PresentationSettingsApplied = false;
    uint8_t PresentationWindowMode = 0;
    uint32_t PresentationRequestedWidth = 0;
    uint32_t PresentationRequestedHeight = 0;
    uint32_t PresentationSwapchainWidth = 0;
    uint32_t PresentationSwapchainHeight = 0;
    bool PresentationVsync = true;
    float InternalResolutionScale = 1.0F;
    uint32_t OutputWidth = 0;
    uint32_t OutputHeight = 0;
    uint32_t InternalTargetWidth = 0;
    uint32_t InternalTargetHeight = 0;
    uint32_t TemporalJitterDrawCount = 0;
    uint32_t RigidMotionDrawCount = 0;
    uint32_t ExactMotionDrawCount = 0;
    uint32_t DeformedMotionDrawCount = 0;
    uint32_t GrassMotionBladeCount = 0;
    uint64_t FirstSubmissionId = 0;
    uint64_t LastSubmissionId = 0;
    uint64_t LastRenderTargetNamespace = 0;
    uint32_t LastColorPhysicalAddress = 0;
    uint32_t LastDepthPhysicalAddress = 0;
    uint16_t LastFramebufferWidth = 0;
    uint16_t LastFramebufferHeight = 0;
    float LastDepthRange = 0.0F;
    float LastNearPlane = 0.0F;
    bool SawDepthTest = false;
    bool SawDepthWrite = false;
};

class Oot3dVulkanDiagnostics {
  public:
    explicit Oot3dVulkanDiagnostics(Oot3dVulkanDiagnosticsConfig config = {});

    [[nodiscard]] bool Enabled() const;
    void ReloadFromEnvironment();
    void SetVulkanAdapter(Oot3dVulkanAdapterDiagnostics adapter);
    void SetNriPipelineStatistics(uint64_t initialCacheBytes, uint64_t attempts,
                                  uint64_t created, uint64_t nanoseconds);
    void SetD3d12NgxProvider(Oot3dD3d12NgxProviderDiagnostics provider);
    void SetVulkanPresentationFallback(
        Oot3dVulkanPresentationFallbackReason reason,
        std::string detail);
    void BeginFrame(uint64_t frameIndex);
    void RecordRendererValidation(
        const Oot3d::RendererValidationSnapshot& snapshot);
    void RecordNativePicaDraw(uint64_t submissionId,
                              uint64_t renderTargetNamespace,
                              uint32_t vertexCount, bool indexed,
                              uint32_t colorPhysicalAddress,
                              uint32_t depthPhysicalAddress,
                              uint16_t framebufferWidth,
                              uint16_t framebufferHeight, float depthRange,
                              float nearPlane, bool depthTest,
                              bool depthWrite, bool alphaTest,
                              bool alphaToCoverage = false);
    void RecordNativePicaCpuTimings(
        const Oot3dVulkanNativePicaCpuTimings& timings);
    void RecordPicaShaderProfile(bool nativeFidelity);
    void RecordPicaAttachmentContract(
        uint32_t colorAttachmentCount, uint32_t auxiliaryOutputMask);
    void RecordPicaPipelinePrewarm(
        uint32_t graphicsPipelineCacheEntries,
        uint32_t manifestEntries, bool enabled,
        uint32_t created, uint32_t reused, uint32_t skipped);
    void RecordEffectGraph(uint32_t passCount, uint32_t resourceCount,
                           uint32_t declaredBindingCount,
                           uint32_t declaredBarrierCount);
    void RecordEffectGraphGuideImageBarriers(uint32_t count);
    void RecordEffectGraphInternalImageTransitions(
        uint32_t planned, uint32_t emitted,
        uint32_t privatePlanned = 0U,
        uint32_t privateEmitted = 0U);
    void RecordEffectGeometryProvider(
        const Oot3d::EffectGeometryProviderExecutionSummary& summary);
    void RecordDisplayEffectGraphExecution(
        const Oot3d::DisplayEffectExecutionSummary& summary);
    void RecordDisplayEffectGraphBindings(
        const Oot3d::EffectResourceBindingValidation& validation,
        bool compatibilityFallback);
    void RecordDisplayEffectPhysicalPlan(
        const Oot3d::EffectPhysicalPlanSummary& summary);
    void RecordEffectTransientImageArena(
        const Oot3d::EffectTransientAllocationSummary& summary,
        bool configured, uint32_t allocatedSlotCount,
        bool reusedConfiguration);
    void RecordPicaShaderSelection(
        bool canonical, bool instrumentationRequested,
        uint32_t canonicalCacheEntries,
        uint32_t instrumentationCacheEntries,
        bool directFragmentHooks = false,
        bool temporalVertexRequested = false,
        bool directTemporalVertexProgram = false,
        uint32_t canonicalOutputAudits = 0U,
        uint32_t canonicalOutputContractRejects = 0U,
        uint32_t canonicalCompatibilityAnalyses = 0U,
        uint32_t typedInstrumentationContractRejects = 0U);
    void RecordNativePicaDynamicRendering(bool nriOwned = false);
    void RecordNriPicaGlobalBarriers(uint32_t count);
    void RecordNriPicaPipelineBind(bool wrapped);
    void RecordNriPicaShaderContract(bool ready,
                                     bool descriptorLayoutOwned);
    void RecordNriPicaOwnedPipeline(bool ready);
    void RecordNriPicaOwnedDraw(bool executed,
                                bool descriptorsOwned,
                                bool uploadsOwned = false,
                                uint64_t uploadBytes = 0);
    void RecordPicaCompositionSchedule(
        const Renderer3ds::PicaCompositionScheduleStats& stats);
    void RecordPicaCompositionScheduleDraw(bool matched);
    void RecordPicaDisplayComposition(
        ::Oot3d::Renderer::PicaCompositionDomain domain,
        bool sceneResolved);
    void RecordPicaSceneFrame(
        const Oot3d::PicaSceneFrameStats& stats);
    void RecordNriPicaTextureUpload(bool executed,
                                    uint64_t uploadBytes = 0);
    void RecordNriPicaTextureImage(bool owned);
    void RecordNriPicaDisplayImage(bool owned);
    void RecordNriPicaRenderTarget(bool owned, uint32_t imageCount);
    void RecordNriPicaRenderTargetInitialization(
        bool executed, uint32_t imageCount, uint32_t barrierCount);
    void RecordNriPicaDisplayCopy(bool executed,
                                  uint32_t barrierCount);
    void RecordNriPicaMemoryFillClear(bool shadowCleared,
                                      bool attachmentsCleared,
                                      uint32_t barrierCount);
    void RecordNriDirectionalShadow(uint32_t casterCount,
                                    bool nativeLight);
    void RecordNriDirectionalShadowAttempt(uint32_t queuedCasters,
                                           uint32_t queuedNativeLights,
                                           uint32_t executionStage);
    void RecordNriDirectionalShadowLightSelection(
        uint32_t candidateCount, uint32_t clusterCount,
        float geometryWeight, float directionX, float directionY,
        float directionZ);
    void RecordNriDirectionalShadowReceiver(bool eligible,
                                            bool lightingAvailable,
                                            bool lightingEnabled,
                                            bool ambientOnly,
                                            bool directUnmatched,
                                            bool directMatched,
                                            bool historyBound);
    void RecordNriDirectionalShadowGraphTransitions(uint32_t planned,
                                                     uint32_t emitted);
    void RecordNriDirectionalShadowSchedule(bool authorized,
                                            bool duplicate,
                                            bool surfaceEnd);
    void RecordDisplayTransfer(bool present, uint32_t spatialAaMode = 0U,
                               uint32_t msaaSamples = 1U);
    void RecordSmaa1x(bool outputsNriOwned,
                      bool lookupsNriOwned,
                      bool computeNriOwned,
                      bool barriersNriOwned,
                      bool lookupUploadNriOwned);
    void RecordOverlay();
    void RecordRenderResolution(float internalScale,
                                uint32_t outputWidth,
                                uint32_t outputHeight,
                                uint32_t internalTargetWidth,
                                uint32_t internalTargetHeight);
    void RecordMemoryFill();
    void RecordToonDraw(bool picaMaterialPath);
    void RecordOutlineOcclusionDraw();
    void RecordCacaoPass(bool outputNriOwned = false,
                         bool picaNormalGuide = false);
    void RecordCacaoAmbientGuideDraw(bool exactRgb);
    void RecordCacaoAmbientComposite();
    void RecordHiZPass(uint32_t mipCount, uint32_t depthConvention = 0U,
                       bool outputNriOwned = false,
                       bool computeNriOwned = false,
                       bool barriersNriOwned = false);
    void RecordHiZReflection(bool filtered = true, uint32_t debugView = 0U,
                             bool nriWrapped = false,
                             bool outputsNriOwned = false,
                             bool computeNriOwned = false,
                             bool barriersNriOwned = false);
    void RecordReflectionMaterialDraw(bool calibrated,
                                       bool profiled,
                                       bool noSpecularTevUse,
                                       bool unsupportedShader);
    void RecordReflectionTextureUsage(
        uint64_t contentHash, uint16_t width, uint16_t height,
        uint8_t mapperSlot, bool profiled = false,
        uint32_t ruleId = 0, uint8_t profile = 0,
        float reflectivity = 0.0F, float roughness = 1.0F);
    void RecordLinearWorkingColor(bool sourceSrgb,
                                  bool outputNriOwned = false,
                                  bool computeNriOwned = false,
                                  bool barriersNriOwned = false);
    void RecordFidelityFxSssr(bool nriWrapped = false,
                              bool outputNriOwned = false,
                              bool historyValid = false,
                              bool linearInput = false);
    void RecordFidelityFxSssrFallback(std::string reason);
    void RecordReflectionIbl(bool profileUpdated, bool picaDerived,
                             uint32_t environmentMipCount,
                             bool environmentNriOwned,
                             bool brdfNriOwned,
                             bool computeNriOwned,
                             bool barriersNriOwned);
    void RecordReflectionMaterialResolve(bool outputNriOwned,
                                         bool computeNriOwned,
                                         bool barriersNriOwned);
    void RecordTemporalHistory(bool valid, bool cameraCut,
                               uint32_t resetReason);
    void RecordMotionVectors(bool executed, bool historyValid,
                             bool nriWrapped, bool outputsNriOwned,
                             bool computeNriOwned = false,
                             bool barriersNriOwned = false);
    void RecordReactiveWorldDraw();
    void RecordTemporalAa(bool executed, bool historyValid, bool nriWrapped,
                          bool historyNriOwned = false,
                          bool computeNriOwned = false,
                          bool barriersNriOwned = false);
    void RecordSceneCompositePass(bool outputNriOwned = false,
                                  bool computeNriOwned = false,
                                  bool barriersNriOwned = false);
    void RecordNisUpscale(bool executed, uint32_t inputWidth,
                          uint32_t inputHeight, uint32_t outputWidth,
                          uint32_t outputHeight);
    void RecordFsrUpscale(bool executed, bool historyReset,
                          uint32_t inputWidth, uint32_t inputHeight,
                          uint32_t outputWidth, uint32_t outputHeight);
    void RecordDlssUpscale(bool executed, bool historyReset,
                           uint32_t inputWidth, uint32_t inputHeight,
                           uint32_t outputWidth, uint32_t outputHeight);
    void RecordD3d12NgxFrame(bool requested, bool contractReady,
                             bool prepared, bool splitSubmitted,
                             bool queued, bool outputAcquired,
                             uint64_t dispatchCount);
    void RecordUpscalerOutputOwnership(bool executed, bool nriOwned,
                                       bool barriersNriOwned = false);
    void RecordNriScanout(bool executed, bool pipelineOwned,
                          bool descriptorsOwned, bool scopeOwned = false);
    void RecordLinearScanout(bool encodeSrgb);
    void RecordNriSwapchainAcquire(bool executed, uint32_t imageCount,
                                   bool synchronizationOwned,
                                   bool presentWorkerBypassed);
    void RecordNriSwapchainPresent(bool executed);
    void RecordPresentationTransaction(uint8_t transactionKind,
                                       bool success,
                                       bool automaticRollback);
    void SetPresentationState(
        Oot3dVulkanPresentationStateDiagnostics state);
    void RecordTemporalDraw(bool rigidMotionValid, bool exactMotionValid,
                            bool deformedMotionValid);
    void RecordGrassMotion(uint32_t bladeCount);
    void EndFrame();
    void SetGpuTimings(uint64_t frameIndex,
                       const Oot3dVulkanGpuTimings& timings);
    void Flush() const;

    [[nodiscard]] const std::vector<Oot3dVulkanFrameDiagnostics>&
    Frames() const;

  private:
    Oot3dVulkanFrameDiagnostics* FindFrame(uint64_t frameIndex);

    Oot3dVulkanDiagnosticsConfig mConfig;
    std::vector<Oot3dVulkanFrameDiagnostics> mFrames;
    std::optional<Oot3dVulkanFrameDiagnostics> mActiveFrame;
    std::chrono::steady_clock::time_point mFrameStart{};
    Oot3dVulkanAdapterDiagnostics mVulkanAdapter;
    uint64_t mNriInitialCacheBytes = 0;
    uint64_t mNriPipelineAttempts = 0;
    uint64_t mNriPipelinesCreated = 0;
    uint64_t mNriPipelineCreationNanoseconds = 0;
    Oot3dD3d12NgxProviderDiagnostics mD3d12NgxProvider;
    Oot3dVulkanPresentationFallbackReason
        mNriSwapchainFallbackReason =
            Oot3dVulkanPresentationFallbackReason::None;
    std::string mNriSwapchainFallbackDetail;
    Oot3dVulkanPresentationStateDiagnostics mPresentationState;
    uint32_t mPendingPresentationApplyCount = 0;
    uint32_t mPendingPresentationRollbackCount = 0;
    uint32_t mPendingPresentationApplyFailureCount = 0;
    size_t mFramesSinceFlush = 0;
};

} // namespace Fast
