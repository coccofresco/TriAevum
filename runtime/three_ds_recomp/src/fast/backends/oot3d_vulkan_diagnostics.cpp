#include "fast/backends/oot3d_vulkan_diagnostics.h"
#include "fast/oot3d/pica_scene_frame.h"
#include "fast/renderer3ds/pica_composition_schedule.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#include <nlohmann/json.hpp>

namespace Fast {
namespace {

constexpr const char* kOutputPathEnvironment =
    "OOT3D_VULKAN_DIAGNOSTICS_PATH";
constexpr const char* kMaximumFramesEnvironment =
    "OOT3D_VULKAN_DIAGNOSTICS_MAX_FRAMES";
constexpr size_t kMaximumReflectionTextureUsagesPerFrame = 96U;

std::optional<std::string> ReadEnvironment(const char* name) {
#ifdef _WIN32
    const DWORD size = GetEnvironmentVariableA(name, nullptr, 0);
    if (size == 0) return std::nullopt;
    std::string value(size, '\0');
    const DWORD copied = GetEnvironmentVariableA(name, value.data(), size);
    if (copied == 0 || copied >= size) return std::nullopt;
    value.resize(copied);
    return value;
#else
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0'
               ? std::optional<std::string>(value)
               : std::nullopt;
#endif
}

std::optional<double> MillisecondsBetween(
    std::chrono::steady_clock::time_point begin,
    std::chrono::steady_clock::time_point end) {
    if (end < begin) {
        return std::nullopt;
    }
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

void PutOptionalMilliseconds(nlohmann::json& object, const char* key,
                             const std::optional<double>& value) {
    object[key] = value.has_value() ? nlohmann::json(*value)
                                    : nlohmann::json(nullptr);
}

std::string HashToHex(uint64_t value) {
    std::array<char, 16U> digits{};
    const auto converted =
        std::to_chars(digits.data(), digits.data() + digits.size(),
                      value, 16);
    if (converted.ec != std::errc{}) return {};
    const size_t digitCount =
        static_cast<size_t>(converted.ptr - digits.data());
    return std::string(16U - digitCount, '0') +
           std::string(digits.data(), converted.ptr);
}

nlohmann::json FrameToJson(const Oot3dVulkanFrameDiagnostics& frame) {
    nlohmann::json gpu;
    PutOptionalMilliseconds(gpu, "frame_ms", frame.Gpu.FrameMilliseconds);
    PutOptionalMilliseconds(gpu, "native_pica_ms",
                            frame.Gpu.NativePicaMilliseconds);
    PutOptionalMilliseconds(gpu, "toon_raster_ms",
                            frame.Gpu.ToonRasterMilliseconds);
    PutOptionalMilliseconds(gpu, "grass_ms",
                            frame.Gpu.GrassMilliseconds);
    PutOptionalMilliseconds(gpu, "cacao_ms",
                            frame.Gpu.CacaoMilliseconds);
    PutOptionalMilliseconds(gpu, "depth_preparation_ms",
                            frame.Gpu.DepthPreparationMilliseconds);
    PutOptionalMilliseconds(gpu, "reflection_ms",
                            frame.Gpu.ReflectionMilliseconds);
    PutOptionalMilliseconds(gpu, "motion_vectors_ms",
                            frame.Gpu.MotionVectorsMilliseconds);
    PutOptionalMilliseconds(gpu, "scene_composite_ms",
                            frame.Gpu.SceneCompositeMilliseconds);
    PutOptionalMilliseconds(gpu, "anti_aliasing_ms",
                            frame.Gpu.AntiAliasingMilliseconds);
    PutOptionalMilliseconds(gpu, "upscaler_ms",
                            frame.Gpu.UpscalerMilliseconds);
    PutOptionalMilliseconds(gpu, "display_transfer_ms",
                            frame.Gpu.DisplayTransferMilliseconds);
    PutOptionalMilliseconds(gpu, "scanout_ms",
                            frame.Gpu.ScanoutMilliseconds);
    PutOptionalMilliseconds(gpu, "overlay_ms",
                            frame.Gpu.OverlayMilliseconds);

    nlohmann::json reflectionTextureUsages =
        nlohmann::json::array();
    for (const auto& usage : frame.ReflectionTextureUsages) {
        reflectionTextureUsages.push_back({
            { "texture_hash", HashToHex(usage.ContentHash) },
            { "width", usage.Width },
            { "height", usage.Height },
            { "mapper_slot", usage.MapperSlot },
            { "draw_count", usage.DrawCount },
            { "profiled_draw_count", usage.ProfiledDrawCount },
            { "profiled", usage.ProfiledDrawCount != 0U },
            { "rule_id", usage.RuleId },
            { "profile", usage.Profile },
            { "reflectivity", usage.Reflectivity },
            { "roughness", usage.Roughness },
        });
    }

    return {
        { "frame_index", frame.FrameIndex },
        { "render_mode", "authentic" },
        { "cpu_frame_ms", frame.CpuFrameMilliseconds },
        { "gpu", std::move(gpu) },
        { "vulkan_adapter_count", frame.VulkanAdapterCount },
        { "vulkan_adapter_index", frame.VulkanAdapterIndex },
        { "vulkan_adapter_vendor_id", frame.VulkanAdapterVendorId },
        { "vulkan_adapter_device_id", frame.VulkanAdapterDeviceId },
        { "vulkan_graphics_queue_family",
          frame.VulkanGraphicsQueueFamily },
        { "vulkan_present_queue_family",
          frame.VulkanPresentQueueFamily },
        { "vulkan_graphics_queue_count",
          frame.VulkanGraphicsQueueCount },
        { "vulkan_requested_graphics_queue_count",
          frame.VulkanRequestedGraphicsQueueCount },
        { "vulkan_present_queue_index",
          frame.VulkanPresentQueueIndex },
        { "vulkan_async_present_supported",
          frame.VulkanAsyncPresentSupported },
        { "nri_swapchain_queue_eligible",
          frame.NriSwapchainQueueEligible },
        { "nri_swapchain_fallback_reason",
          frame.NriSwapchainFallbackReason },
        { "nri_swapchain_fallback_detail",
          frame.NriSwapchainFallbackDetail },
        { "vulkan_validation_enabled",
          frame.VulkanValidationEnabled },
        { "vulkan_validation_info_count",
          frame.VulkanValidationInfoCount },
        { "vulkan_validation_warning_count",
          frame.VulkanValidationWarningCount },
        { "vulkan_validation_error_count",
          frame.VulkanValidationErrorCount },
        { "nri_validation_enabled",
          frame.NriValidationEnabled },
        { "nri_validation_info_count",
          frame.NriValidationInfoCount },
        { "nri_validation_warning_count",
          frame.NriValidationWarningCount },
        { "nri_validation_error_count",
          frame.NriValidationErrorCount },
        { "native_pica_draw_count", frame.NativePicaDrawCount },
        { "native_pica_alpha_test_draw_count",
          frame.NativePicaAlphaTestDrawCount },
        { "native_pica_alpha_to_coverage_draw_count",
          frame.NativePicaAlphaToCoverageDrawCount },
        { "native_pica_vertex_count", frame.NativePicaVertexCount },
        { "native_pica_indexed_draw_count", frame.NativePicaIndexedDrawCount },
        { "pica_native_fidelity_profile",
          frame.PicaNativeFidelityProfile },
        { "pica_color_attachment_count", frame.PicaColorAttachmentCount },
        { "pica_auxiliary_output_mask", frame.PicaAuxiliaryOutputMask },
        { "effect_graph_pass_count", frame.EffectGraphPassCount },
        { "effect_graph_resource_count", frame.EffectGraphResourceCount },
        { "effect_graph_declared_binding_count",
          frame.EffectGraphDeclaredBindingCount },
        { "effect_graph_declared_barrier_count",
          frame.EffectGraphDeclaredBarrierCount },
        { "effect_graph_guide_image_barrier_count",
          frame.EffectGraphGuideImageBarrierCount },
        { "effect_graph_internal_image_transition_count",
          frame.EffectGraphInternalImageTransitionCount },
        { "effect_graph_elided_image_transition_count",
          frame.EffectGraphElidedImageTransitionCount },
        { "effect_provider_private_image_transition_count",
          frame.EffectProviderPrivateImageTransitionCount },
        { "effect_provider_private_elided_image_transition_count",
          frame.EffectProviderPrivateElidedImageTransitionCount },
        { "effect_geometry_provider_declared",
          frame.EffectGeometryProviderDeclared },
        { "effect_geometry_provider_stage",
          frame.EffectGeometryProviderStage },
        { "effect_geometry_source_inspection_count",
          frame.EffectGeometrySourceInspectionCount },
        { "effect_geometry_source_published_count",
          frame.EffectGeometrySourcePublishedCount },
        { "effect_geometry_declared_boundary_invocation_count",
          frame.EffectGeometryDeclaredBoundaryInvocationCount },
        { "effect_geometry_fallback_invocation_count",
          frame.EffectGeometryFallbackInvocationCount },
        { "effect_geometry_executed_count",
          frame.EffectGeometryExecutedCount },
        { "effect_geometry_reused_count",
          frame.EffectGeometryReusedCount },
        { "effect_geometry_skipped_count",
          frame.EffectGeometrySkippedCount },
        { "effect_geometry_failed_count",
          frame.EffectGeometryFailedCount },
        { "effect_geometry_input_unavailable_count",
          frame.EffectGeometryInputUnavailableCount },
        { "effect_geometry_schedule_rejected_count",
          frame.EffectGeometryScheduleRejectedCount },
        { "display_effect_graph_invocation_count",
          frame.DisplayEffectGraphInvocationCount },
        { "display_effect_graph_declared_pass_count",
          frame.DisplayEffectGraphDeclaredPassCount },
        { "display_effect_graph_executed_pass_count",
          frame.DisplayEffectGraphExecutedPassCount },
        { "display_effect_graph_reused_pass_count",
          frame.DisplayEffectGraphReusedPassCount },
        { "display_effect_graph_fused_pass_count",
          frame.DisplayEffectGraphFusedPassCount },
        { "display_effect_graph_skipped_pass_count",
          frame.DisplayEffectGraphSkippedPassCount },
        { "display_effect_graph_failed_pass_count",
          frame.DisplayEffectGraphFailedPassCount },
        { "display_effect_graph_unresolved_pass_count",
          frame.DisplayEffectGraphUnresolvedPassCount },
        { "display_effect_graph_active_binding_count",
          frame.DisplayEffectGraphActiveBindingCount },
        { "display_effect_graph_undeclared_record_count",
          frame.DisplayEffectGraphUndeclaredRecordCount },
        { "display_effect_graph_duplicate_record_count",
          frame.DisplayEffectGraphDuplicateRecordCount },
        { "display_effect_graph_declared_pass_mask",
          frame.DisplayEffectGraphDeclaredPassMask },
        { "display_effect_graph_executed_pass_mask",
          frame.DisplayEffectGraphExecutedPassMask },
        { "display_effect_graph_reused_pass_mask",
          frame.DisplayEffectGraphReusedPassMask },
        { "display_effect_graph_fused_pass_mask",
          frame.DisplayEffectGraphFusedPassMask },
        { "display_effect_graph_skipped_pass_mask",
          frame.DisplayEffectGraphSkippedPassMask },
        { "display_effect_graph_failed_pass_mask",
          frame.DisplayEffectGraphFailedPassMask },
        { "display_effect_graph_unresolved_pass_mask",
          frame.DisplayEffectGraphUnresolvedPassMask },
        { "display_effect_graph_declared_read_count",
          frame.DisplayEffectGraphDeclaredReadCount },
        { "display_effect_graph_resolved_read_count",
          frame.DisplayEffectGraphResolvedReadCount },
        { "display_effect_graph_missing_read_count",
          frame.DisplayEffectGraphMissingReadCount },
        { "display_effect_graph_missing_read_mask",
          frame.DisplayEffectGraphMissingReadMask },
        { "display_effect_graph_binding_fallback_count",
          frame.DisplayEffectGraphBindingFallbackCount },
        { "display_effect_physical_plan_invocation_count",
          frame.DisplayEffectPhysicalPlanInvocationCount },
        { "display_effect_physical_plan_complete_count",
          frame.DisplayEffectPhysicalPlanCompleteCount },
        { "display_effect_physical_declared_resource_count",
          frame.DisplayEffectPhysicalDeclaredResourceCount },
        { "display_effect_physical_resolved_resource_count",
          frame.DisplayEffectPhysicalResolvedResourceCount },
        { "display_effect_physical_missing_resource_count",
          frame.DisplayEffectPhysicalMissingResourceCount },
        { "display_effect_physical_missing_resource_mask",
          frame.DisplayEffectPhysicalMissingResourceMask },
        { "display_effect_physical_semantic_resource_count",
          frame.DisplayEffectPhysicalSemanticResourceCount },
        { "display_effect_physical_external_image_resource_count",
          frame.DisplayEffectPhysicalExternalImageResourceCount },
        { "display_effect_physical_native_attachment_resource_count",
          frame.DisplayEffectPhysicalNativeAttachmentResourceCount },
        { "display_effect_physical_transient_image_resource_count",
          frame.DisplayEffectPhysicalTransientImageResourceCount },
        { "display_effect_physical_temporal_history_resource_count",
          frame.DisplayEffectPhysicalTemporalHistoryResourceCount },
        { "display_effect_physical_image_count",
          frame.DisplayEffectPhysicalImageCount },
        { "display_effect_physical_alias_eligible_resource_count",
          frame.DisplayEffectPhysicalAliasEligibleResourceCount },
        { "display_effect_physical_alias_slot_count",
          frame.DisplayEffectPhysicalAliasSlotCount },
        { "display_effect_physical_alias_opportunity_count",
          frame.DisplayEffectPhysicalAliasOpportunityCount },
        { "display_effect_physical_peak_live_transient_count",
          frame.DisplayEffectPhysicalPeakLiveTransientCount },
        { "effect_transient_arena_invocation_count",
          frame.EffectTransientArenaInvocationCount },
        { "effect_transient_arena_configured_count",
          frame.EffectTransientArenaConfiguredCount },
        { "effect_transient_arena_resource_count",
          frame.EffectTransientArenaResourceCount },
        { "effect_transient_arena_physical_slot_count",
          frame.EffectTransientArenaPhysicalSlotCount },
        { "effect_transient_arena_alias_opportunity_count",
          frame.EffectTransientArenaAliasOpportunityCount },
        { "effect_transient_arena_peak_live_resource_count",
          frame.EffectTransientArenaPeakLiveResourceCount },
        { "effect_transient_arena_allocated_slot_count",
          frame.EffectTransientArenaAllocatedSlotCount },
        { "effect_transient_arena_reused_configuration_count",
          frame.EffectTransientArenaReusedConfigurationCount },
        { "pica_canonical_shader_draw_count",
          frame.PicaCanonicalShaderDrawCount },
        { "pica_instrumented_shader_draw_count",
          frame.PicaInstrumentedShaderDrawCount },
        { "pica_shader_instrumentation_requested_draw_count",
          frame.PicaShaderInstrumentationRequestedDrawCount },
        { "pica_direct_fragment_hook_draw_count",
          frame.PicaDirectFragmentHookDrawCount },
        { "pica_legacy_fragment_hook_analysis_draw_count",
          frame.PicaLegacyFragmentHookAnalysisDrawCount },
        { "pica_direct_temporal_vertex_program_draw_count",
          frame.PicaDirectTemporalVertexProgramDrawCount },
        { "pica_legacy_temporal_vertex_analysis_draw_count",
          frame.PicaLegacyTemporalVertexAnalysisDrawCount },
        { "pica_canonical_shader_cache_entries",
          frame.PicaCanonicalShaderCacheEntries },
        { "pica_instrumentation_shader_cache_entries",
          frame.PicaInstrumentationShaderCacheEntries },
        { "pica_graphics_pipeline_cache_entries",
          frame.PicaGraphicsPipelineCacheEntries },
        { "pica_pipeline_manifest_entries",
          frame.PicaPipelineManifestEntries },
        { "pica_pipeline_prewarm_enabled",
          frame.PicaPipelinePrewarmEnabled },
        { "pica_pipeline_prewarm_created",
          frame.PicaPipelinePrewarmCreated },
        { "pica_pipeline_prewarm_reused",
          frame.PicaPipelinePrewarmReused },
        { "pica_pipeline_prewarm_skipped",
          frame.PicaPipelinePrewarmSkipped },
        { "pica_canonical_output_audit_count",
          frame.PicaCanonicalOutputAuditCount },
        { "pica_canonical_output_contract_reject_count",
          frame.PicaCanonicalOutputContractRejectCount },
        { "pica_canonical_compatibility_analysis_count",
          frame.PicaCanonicalCompatibilityAnalysisCount },
        { "pica_typed_instrumentation_contract_reject_count",
          frame.PicaTypedInstrumentationContractRejectCount },
        { "native_pica_cpu",
          {
              { "draw_count", frame.NativePicaCpu.DrawCount },
              { "total_ms", frame.NativePicaCpu.TotalMilliseconds },
              { "shader_variant_ms",
                frame.NativePicaCpu.ShaderVariantMilliseconds },
              { "shader_variant_cache_hits",
                frame.NativePicaCpu.ShaderVariantCacheHits },
              { "shader_variant_cache_misses",
                frame.NativePicaCpu.ShaderVariantCacheMisses },
              { "shader_variant_cache_entries",
                frame.NativePicaCpu.ShaderVariantCacheEntries },
              { "shader_cache_ms",
                frame.NativePicaCpu.ShaderCacheMilliseconds },
              { "texture_ms", frame.NativePicaCpu.TextureMilliseconds },
              { "draw_state_ms",
                frame.NativePicaCpu.DrawStateMilliseconds },
              { "grass_surface_ms",
                frame.NativePicaCpu.GrassSurfaceMilliseconds },
              { "scene_state_ms",
                frame.NativePicaCpu.SceneStateMilliseconds },
              { "render_target_state_ms",
                frame.NativePicaCpu.RenderTargetStateMilliseconds },
              { "directional_shadow_ms",
                frame.NativePicaCpu.DirectionalShadowMilliseconds },
              { "grass_render_ms",
                frame.NativePicaCpu.GrassRenderMilliseconds },
              { "reflection_environment_ms",
                frame.NativePicaCpu.ReflectionEnvironmentMilliseconds },
              { "temporal_state_ms",
                frame.NativePicaCpu.TemporalStateMilliseconds },
              { "pipeline_ms", frame.NativePicaCpu.PipelineMilliseconds },
              { "upload_ms", frame.NativePicaCpu.UploadMilliseconds },
              { "descriptor_ms",
                frame.NativePicaCpu.DescriptorMilliseconds },
              { "command_ms", frame.NativePicaCpu.CommandMilliseconds },
              { "geometry_registry_hits", frame.NativePicaCpu.GeometryRegistryHits },
              { "geometry_registry_misses", frame.NativePicaCpu.GeometryRegistryMisses },
              { "geometry_dynamic_builds", frame.NativePicaCpu.GeometryDynamicBuilds },
              { "geometry_registry_entries", frame.NativePicaCpu.GeometryRegistryEntries },
              { "geometry_persistent_draws", frame.NativePicaCpu.GeometryPersistentDraws },
              { "geometry_transient_draws", frame.NativePicaCpu.GeometryTransientDraws },
              { "geometry_persistent_upload_bytes", frame.NativePicaCpu.GeometryPersistentUploadBytes },
              { "geometry_transient_upload_bytes", frame.NativePicaCpu.GeometryTransientUploadBytes },
          } },
        { "native_pica_dynamic_rendering_count", frame.NativePicaDynamicRenderingCount },
        { "nri_pica_rendering_scope_owned", frame.NriPicaRenderingScopeOwned },
        { "nri_pica_global_barrier_count", frame.NriPicaGlobalBarrierCount },
        { "nri_pica_pipeline_bind_count", frame.NriPicaPipelineBindCount },
        { "nri_pica_pipeline_wrapped", frame.NriPicaPipelineWrapped },
        { "nri_pica_shader_contract_count", frame.NriPicaShaderContractCount },
        { "nri_pica_descriptor_layout_owned", frame.NriPicaDescriptorLayoutOwned },
        { "nri_pica_owned_pipeline_draw_count", frame.NriPicaOwnedPipelineDrawCount },
        { "nri_pica_owned_draw_count", frame.NriPicaOwnedDrawCount },
        { "pica_composition_sequence_count",
          frame.PicaCompositionSequenceCount },
        { "pica_composition_published_draw_count",
          frame.PicaCompositionPublishedDrawCount },
        { "pica_composition_consumed_draw_count",
          frame.PicaCompositionConsumedDrawCount },
        { "pica_composition_run_count", frame.PicaCompositionRunCount },
        { "pica_composition_target_count",
          frame.PicaCompositionTargetCount },
        { "pica_composition_stage_anchor_count",
          frame.PicaCompositionStageAnchorCount },
        { "pica_composition_world_stage_target_count",
          frame.PicaCompositionWorldStageTargetCount },
        { "pica_composition_non_scene_target_count",
          frame.PicaCompositionNonSceneTargetCount },
        { "pica_composition_unknown_layer_target_count",
          frame.PicaCompositionUnknownLayerTargetCount },
        { "pica_composition_missing_opaque_world_target_count",
          frame.PicaCompositionMissingOpaqueWorldTargetCount },
        { "pica_composition_noncontiguous_world_target_count",
          frame.PicaCompositionNonContiguousWorldTargetCount },
        { "pica_composition_noncanonical_tail_target_count",
          frame.PicaCompositionNonCanonicalTailTargetCount },
        { "pica_composition_execution_mismatch_count",
          frame.PicaCompositionExecutionMismatchCount },
        { "pica_display_scene_snapshot_count",
          frame.PicaDisplaySceneSnapshotCount },
        { "pica_display_ui_snapshot_count",
          frame.PicaDisplayUiSnapshotCount },
        { "pica_display_unknown_snapshot_count",
          frame.PicaDisplayUnknownSnapshotCount },
        { "pica_display_scene_resolved_snapshot_count",
          frame.PicaDisplaySceneResolvedSnapshotCount },
        { "pica_scene_frame_draw_count", frame.PicaSceneFrameDrawCount },
        { "pica_scene_frame_temporal_sample_count", frame.PicaSceneFrameTemporalSampleCount },
        { "pica_scene_frame_synthetic_temporal_sample_count", frame.PicaSceneFrameSyntheticTemporalSampleCount },
        { "pica_scene_frame_temporal_sample_multiplier", frame.PicaSceneFrameTemporalSampleMultiplier },
        { "pica_scene_frame_temporal_sample_ordinal", frame.PicaSceneFrameTemporalSampleOrdinal },
        { "pica_scene_frame_scene_domain_draw_count",
          frame.PicaSceneFrameSceneDomainDrawCount },
        { "pica_scene_frame_ui_domain_draw_count",
          frame.PicaSceneFrameUiDomainDrawCount },
        { "pica_scene_frame_unknown_domain_draw_count",
          frame.PicaSceneFrameUnknownDomainDrawCount },
        { "pica_scene_frame_opaque_world_layer_draw_count",
          frame.PicaSceneFrameOpaqueWorldLayerDrawCount },
        { "pica_scene_frame_transparent_world_layer_draw_count",
          frame.PicaSceneFrameTransparentWorldLayerDrawCount },
        { "pica_scene_frame_atmosphere_layer_draw_count",
          frame.PicaSceneFrameAtmosphereLayerDrawCount },
        { "pica_scene_frame_ui_layer_draw_count",
          frame.PicaSceneFrameUiLayerDrawCount },
        { "pica_scene_frame_unknown_layer_draw_count",
          frame.PicaSceneFrameUnknownLayerDrawCount },
        { "pica_scene_frame_native_cmb_pass_provenance_draw_count",
          frame.PicaSceneFrameNativeCmbPassProvenanceDrawCount },
        { "pica_scene_frame_native_control_flow_provenance_draw_count",
          frame.PicaSceneFrameNativeControlFlowProvenanceDrawCount },
        { "pica_scene_frame_native_ui_lifecycle_provenance_draw_count",
          frame.PicaSceneFrameNativeUiLifecycleProvenanceDrawCount },
        { "pica_scene_frame_unknown_provenance_draw_count",
          frame.PicaSceneFrameUnknownProvenanceDrawCount },
        { "pica_scene_frame_resolved_raster_state_count",
          frame.PicaSceneFrameResolvedRasterStateCount },
        { "pica_scene_frame_resolved_render_target_state_count",
          frame.PicaSceneFrameResolvedRenderTargetStateCount },
        { "pica_scene_frame_resolved_render_target_gpu_resource_count",
          frame.PicaSceneFrameResolvedRenderTargetGpuResourceCount },
        { "pica_scene_frame_resolved_material_state_count",
          frame.PicaSceneFrameResolvedMaterialStateCount },
        { "pica_scene_frame_native_lighting_state_count",
          frame.PicaSceneFrameNativeLightingStateCount },
        { "pica_scene_frame_native_fragment_lighting_state_count",
          frame.PicaSceneFrameNativeFragmentLightingStateCount },
        { "pica_scene_frame_native_fragment_lighting_enabled_draw_count",
          frame.PicaSceneFrameNativeFragmentLightingEnabledDrawCount },
        { "pica_scene_frame_native_fog_state_count",
          frame.PicaSceneFrameNativeFogStateCount },
        { "pica_scene_frame_native_transform_state_count", frame.PicaSceneFrameNativeTransformStateCount },
        { "pica_scene_frame_previous_native_transform_state_count",
          frame.PicaSceneFramePreviousNativeTransformStateCount },
        { "pica_scene_frame_native_skeleton_state_count", frame.PicaSceneFrameNativeSkeletonStateCount },
        { "pica_scene_frame_previous_native_skeleton_state_count",
          frame.PicaSceneFramePreviousNativeSkeletonStateCount },
        { "pica_scene_frame_rejected_draw_count",
          frame.PicaSceneFrameRejectedDrawCount },
        { "pica_scene_frame_bound_texture_count",
          frame.PicaSceneFrameBoundTextureCount },
        { "pica_scene_frame_shader_catalog_entries",
          frame.PicaSceneFrameShaderCatalogEntries },
        { "pica_scene_frame_vertex_layout_catalog_entries",
          frame.PicaSceneFrameVertexLayoutCatalogEntries },
        { "pica_scene_frame_canonical_pipeline_entries",
          frame.PicaSceneFrameCanonicalPipelineEntries },
        { "pica_scene_frame_canonical_full_register_state_entries",
          frame.PicaSceneFrameCanonicalFullRegisterStateEntries },
        { "nri_pica_descriptors_owned", frame.NriPicaDescriptorsOwned },
        { "nri_pica_uploads_owned", frame.NriPicaUploadsOwned },
        { "nri_pica_owned_upload_draw_count", frame.NriPicaOwnedUploadDrawCount },
        { "nri_pica_upload_bytes", frame.NriPicaUploadBytes },
        { "nri_pica_texture_upload_count", frame.NriPicaTextureUploadCount },
        { "nri_pica_texture_upload_bytes", frame.NriPicaTextureUploadBytes },
        { "nri_pica_texture_upload_owned", frame.NriPicaTextureUploadOwned },
        { "nri_pica_owned_texture_image_count", frame.NriPicaOwnedTextureImageCount },
        { "nri_pica_texture_images_owned", frame.NriPicaTextureImagesOwned },
        { "nri_pica_owned_display_image_count", frame.NriPicaOwnedDisplayImageCount },
        { "nri_pica_display_images_owned", frame.NriPicaDisplayImagesOwned },
        { "nri_pica_owned_render_target_count", frame.NriPicaOwnedRenderTargetCount },
        { "nri_pica_owned_render_target_image_count", frame.NriPicaOwnedRenderTargetImageCount },
        { "nri_pica_render_targets_owned", frame.NriPicaRenderTargetsOwned },
        { "nri_pica_render_target_initialization_count", frame.NriPicaRenderTargetInitializationCount },
        { "nri_pica_initialized_render_target_image_count", frame.NriPicaInitializedRenderTargetImageCount },
        { "nri_pica_render_target_initialization_barrier_count", frame.NriPicaRenderTargetInitializationBarrierCount },
        { "nri_pica_render_target_initialization_owned", frame.NriPicaRenderTargetInitializationOwned },
        { "nri_pica_display_copy_count", frame.NriPicaDisplayCopyCount },
        { "nri_pica_display_copy_barrier_count", frame.NriPicaDisplayCopyBarrierCount },
        { "nri_pica_display_copies_owned", frame.NriPicaDisplayCopiesOwned },
        { "nri_pica_memory_fill_shadow_clear_count", frame.NriPicaMemoryFillShadowClearCount },
        { "nri_pica_memory_fill_attachment_clear_count", frame.NriPicaMemoryFillAttachmentClearCount },
        { "nri_pica_memory_fill_clear_barrier_count", frame.NriPicaMemoryFillClearBarrierCount },
        { "nri_pica_memory_fill_clears_owned", frame.NriPicaMemoryFillClearsOwned },
        { "nri_directional_shadow_pass_count", frame.NriDirectionalShadowPassCount },
        { "nri_directional_shadow_caster_count", frame.NriDirectionalShadowCasterCount },
        { "nri_directional_shadow_native_light", frame.NriDirectionalShadowNativeLight },
        { "nri_directional_shadow_attempt_count", frame.NriDirectionalShadowAttemptCount },
        { "nri_directional_shadow_queued_caster_count", frame.NriDirectionalShadowQueuedCasterCount },
        { "nri_directional_shadow_queued_native_light_count", frame.NriDirectionalShadowQueuedNativeLightCount },
        { "nri_directional_shadow_execution_stage", frame.NriDirectionalShadowExecutionStage },
        { "nri_directional_shadow_light_candidate_count",
          frame.NriDirectionalShadowLightCandidateCount },
        { "nri_directional_shadow_light_cluster_count",
          frame.NriDirectionalShadowLightClusterCount },
        { "nri_directional_shadow_light_geometry_weight",
          frame.NriDirectionalShadowLightGeometryWeight },
        { "nri_directional_shadow_light_direction_x",
          frame.NriDirectionalShadowLightDirectionX },
        { "nri_directional_shadow_light_direction_y",
          frame.NriDirectionalShadowLightDirectionY },
        { "nri_directional_shadow_light_direction_z",
          frame.NriDirectionalShadowLightDirectionZ },
        { "nri_directional_shadow_receiver_eligible_draw_count",
          frame.NriDirectionalShadowReceiverEligibleDrawCount },
        { "nri_directional_shadow_receiver_lighting_available_draw_count",
          frame.NriDirectionalShadowReceiverLightingAvailableDrawCount },
        { "nri_directional_shadow_receiver_lighting_enabled_draw_count",
          frame.NriDirectionalShadowReceiverLightingEnabledDrawCount },
        { "nri_directional_shadow_receiver_ambient_only_draw_count",
          frame.NriDirectionalShadowReceiverAmbientOnlyDrawCount },
        { "nri_directional_shadow_receiver_direct_unmatched_draw_count",
          frame.NriDirectionalShadowReceiverDirectUnmatchedDrawCount },
        { "nri_directional_shadow_receiver_direct_matched_draw_count",
          frame.NriDirectionalShadowReceiverDirectMatchedDrawCount },
        { "nri_directional_shadow_receiver_history_bound_draw_count",
          frame.NriDirectionalShadowReceiverHistoryBoundDrawCount },
        { "nri_directional_shadow_graph_image_transition_count",
          frame.NriDirectionalShadowGraphImageTransitionCount },
        { "nri_directional_shadow_graph_elided_image_transition_count",
          frame.NriDirectionalShadowGraphElidedImageTransitionCount },
        { "nri_directional_shadow_schedule_boundary_count",
          frame.NriDirectionalShadowScheduleBoundaryCount },
        { "nri_directional_shadow_schedule_authorized_count",
          frame.NriDirectionalShadowScheduleAuthorizedCount },
        { "nri_directional_shadow_schedule_rejected_count",
          frame.NriDirectionalShadowScheduleRejectedCount },
        { "nri_directional_shadow_schedule_duplicate_count",
          frame.NriDirectionalShadowScheduleDuplicateCount },
        { "nri_directional_shadow_schedule_surface_end_count",
          frame.NriDirectionalShadowScheduleSurfaceEndCount },
        { "display_transfer_count", frame.DisplayTransferCount },
        { "scanout_count", frame.ScanoutCount },
        { "overlay_count", frame.OverlayCount },
        { "memory_fill_count", frame.MemoryFillCount },
        { "toon_draw_count", frame.ToonDrawCount },
        { "outline_occlusion_draw_count", frame.OutlineOcclusionDrawCount },
        { "pica_material_toon_draw_count",
          frame.PicaMaterialToonDrawCount },
        { "cacao_pass_count", frame.CacaoPassCount },
        { "cacao_output_nri_owned", frame.CacaoOutputNriOwned },
        { "cacao_normal_guide_pass_count",
          frame.CacaoNormalGuidePassCount },
        { "cacao_ambient_guide_draw_count",
          frame.CacaoAmbientGuideDrawCount },
        { "cacao_ambient_rgb_guide_draw_count",
          frame.CacaoAmbientRgbGuideDrawCount },
        { "cacao_ambient_fallback_guide_draw_count",
          frame.CacaoAmbientFallbackGuideDrawCount },
        { "cacao_ambient_composite_count",
          frame.CacaoAmbientCompositeCount },
        { "hiz_pass_count", frame.HiZPassCount },
        { "hiz_mip_count", frame.HiZMipCount },
        { "hiz_depth_convention", frame.HiZDepthConvention },
        { "hiz_output_nri_owned", frame.HiZOutputNriOwned },
        { "hiz_compute_nri_owned", frame.HiZComputeNriOwned },
        { "hiz_barriers_nri_owned", frame.HiZBarriersNriOwned },
        { "hiz_reflection_count", frame.HiZReflectionCount },
        { "hiz_reflection_filter_count", frame.HiZReflectionFilterCount },
        { "reflection_debug_view", frame.ReflectionDebugView },
        { "reflection_nri_wrapped", frame.ReflectionNriWrapped },
        { "reflection_outputs_nri_owned", frame.ReflectionOutputsNriOwned },
        { "reflection_compute_nri_owned", frame.ReflectionComputeNriOwned },
        { "reflection_barriers_nri_owned", frame.ReflectionBarriersNriOwned },
        { "reflection_material_candidate_draw_count",
          frame.ReflectionMaterialCandidateDrawCount },
        { "reflection_material_calibrated_draw_count",
          frame.ReflectionMaterialCalibratedDrawCount },
        { "reflection_material_profiled_draw_count",
          frame.ReflectionMaterialProfiledDrawCount },
        { "reflection_material_no_specular_tev_draw_count",
          frame.ReflectionMaterialNoSpecularTevDrawCount },
        { "reflection_material_unsupported_draw_count",
          frame.ReflectionMaterialUnsupportedDrawCount },
        { "reflection_texture_usages",
          std::move(reflectionTextureUsages) },
        { "reflection_texture_usage_overflow_count",
          frame.ReflectionTextureUsageOverflowCount },
        { "linear_working_color_count", frame.LinearWorkingColorCount },
        { "linear_working_color_source_srgb",
          frame.LinearWorkingColorSourceSrgb },
        { "linear_working_color_output_nri_owned",
          frame.LinearWorkingColorOutputNriOwned },
        { "linear_working_color_compute_nri_owned",
          frame.LinearWorkingColorComputeNriOwned },
        { "linear_working_color_barriers_nri_owned",
          frame.LinearWorkingColorBarriersNriOwned },
        { "fidelityfx_sssr_count", frame.FidelityFxSssrCount },
        { "fidelityfx_sssr_history_valid_count",
          frame.FidelityFxSssrHistoryValidCount },
        { "fidelityfx_sssr_nri_wrapped",
          frame.FidelityFxSssrNriWrapped },
        { "fidelityfx_sssr_output_nri_owned",
          frame.FidelityFxSssrOutputNriOwned },
        { "fidelityfx_sssr_linear_input",
          frame.FidelityFxSssrLinearInput },
        { "fidelityfx_sssr_fallback_count",
          frame.FidelityFxSssrFallbackCount },
        { "fidelityfx_sssr_fallback_reason",
          frame.FidelityFxSssrFallbackReason },
        { "reflection_ibl_count", frame.ReflectionIblCount },
        { "reflection_ibl_profile_update_count",
          frame.ReflectionIblProfileUpdateCount },
        { "reflection_ibl_pica_derived",
          frame.ReflectionIblPicaDerived },
        { "reflection_ibl_environment_mip_count",
          frame.ReflectionIblEnvironmentMipCount },
        { "reflection_ibl_environment_nri_owned",
          frame.ReflectionIblEnvironmentNriOwned },
        { "reflection_ibl_brdf_nri_owned",
          frame.ReflectionIblBrdfNriOwned },
        { "reflection_ibl_compute_nri_owned",
          frame.ReflectionIblComputeNriOwned },
        { "reflection_ibl_barriers_nri_owned",
          frame.ReflectionIblBarriersNriOwned },
        { "reflection_material_resolve_count",
          frame.ReflectionMaterialResolveCount },
        { "reflection_material_resolve_output_nri_owned",
          frame.ReflectionMaterialResolveOutputNriOwned },
        { "reflection_material_resolve_compute_nri_owned",
          frame.ReflectionMaterialResolveComputeNriOwned },
        { "reflection_material_resolve_barriers_nri_owned",
          frame.ReflectionMaterialResolveBarriersNriOwned },
        { "spatial_aa_mode", frame.SpatialAaMode },
        { "msaa_samples", frame.MsaaSamples },
        { "smaa_1x_pass_count", frame.Smaa1xPassCount },
        { "smaa_edge_pass_count", frame.SmaaEdgePassCount },
        { "smaa_blend_weight_pass_count",
          frame.SmaaBlendWeightPassCount },
        { "smaa_neighborhood_pass_count",
          frame.SmaaNeighborhoodPassCount },
        { "smaa_outputs_nri_owned",
          frame.SmaaOutputsNriOwned },
        { "smaa_lookups_nri_owned",
          frame.SmaaLookupsNriOwned },
        { "smaa_compute_nri_owned",
          frame.SmaaComputeNriOwned },
        { "smaa_barriers_nri_owned",
          frame.SmaaBarriersNriOwned },
        { "smaa_lookup_upload_nri_owned",
          frame.SmaaLookupUploadNriOwned },
        { "temporal_prepare_count", frame.TemporalPrepareCount },
        { "temporal_valid_count", frame.TemporalValidCount },
        { "temporal_camera_cut_count", frame.TemporalCameraCutCount },
        { "temporal_reset_reason", frame.TemporalResetReason },
        { "motion_vector_pass_count", frame.MotionVectorPassCount },
        { "motion_history_valid_count", frame.MotionHistoryValidCount },
        { "motion_nri_wrapped", frame.MotionNriWrapped },
        { "motion_outputs_nri_owned", frame.MotionOutputsNriOwned },
        { "motion_compute_nri_owned", frame.MotionComputeNriOwned },
        { "motion_barriers_nri_owned", frame.MotionBarriersNriOwned },
        { "reactive_world_draw_count", frame.ReactiveWorldDrawCount },
        { "temporal_aa_pass_count", frame.TemporalAaPassCount },
        { "temporal_aa_history_valid_count", frame.TemporalAaHistoryValidCount },
        { "temporal_aa_nri_wrapped", frame.TemporalAaNriWrapped },
        { "temporal_aa_history_nri_owned", frame.TemporalAaHistoryNriOwned },
        { "temporal_aa_compute_nri_owned", frame.TemporalAaComputeNriOwned },
        { "temporal_aa_barriers_nri_owned", frame.TemporalAaBarriersNriOwned },
        { "scene_composite_pass_count", frame.SceneCompositePassCount },
        { "scene_composite_output_nri_owned", frame.SceneCompositeOutputNriOwned },
        { "scene_composite_compute_nri_owned", frame.SceneCompositeComputeNriOwned },
        { "scene_composite_barriers_nri_owned", frame.SceneCompositeBarriersNriOwned },
        { "nis_upscale_pass_count", frame.NisUpscalePassCount },
        { "nis_input_width", frame.NisInputWidth },
        { "nis_input_height", frame.NisInputHeight },
        { "nis_output_width", frame.NisOutputWidth },
        { "nis_output_height", frame.NisOutputHeight },
        { "nis_nri_dispatched", frame.NisNriDispatched },
        { "fsr_upscale_pass_count", frame.FsrUpscalePassCount },
        { "fsr_input_width", frame.FsrInputWidth },
        { "fsr_input_height", frame.FsrInputHeight },
        { "fsr_output_width", frame.FsrOutputWidth },
        { "fsr_output_height", frame.FsrOutputHeight },
        { "fsr_history_reset_count", frame.FsrHistoryResetCount },
        { "fsr_nri_dispatched", frame.FsrNriDispatched },
        { "dlss_upscale_pass_count", frame.DlssUpscalePassCount },
        { "dlss_input_width", frame.DlssInputWidth },
        { "dlss_input_height", frame.DlssInputHeight },
        { "dlss_output_width", frame.DlssOutputWidth },
        { "dlss_output_height", frame.DlssOutputHeight },
        { "dlss_history_reset_count", frame.DlssHistoryResetCount },
        { "dlss_nri_dispatched", frame.DlssNriDispatched },
        { "d3d12_ngx_frame_requested", frame.D3d12NgxFrameRequested },
        { "d3d12_ngx_frame_contract_ready",
          frame.D3d12NgxFrameContractReady },
        { "d3d12_ngx_frame_prepared", frame.D3d12NgxFramePrepared },
        { "d3d12_ngx_frame_split_submitted",
          frame.D3d12NgxFrameSplitSubmitted },
        { "d3d12_ngx_frame_queued", frame.D3d12NgxFrameQueued },
        { "d3d12_ngx_output_acquired", frame.D3d12NgxOutputAcquired },
        { "upscaler_output_nri_owned", frame.UpscalerOutputNriOwned },
        { "upscaler_barriers_nri_owned", frame.UpscalerBarriersNriOwned },
        { "nri_scanout_count", frame.NriScanoutCount },
        { "nri_scanout_pipeline_owned", frame.NriScanoutPipelineOwned },
        { "nri_scanout_descriptors_owned", frame.NriScanoutDescriptorsOwned },
        { "nri_scanout_scope_owned", frame.NriScanoutScopeOwned },
        { "linear_scanout_count", frame.LinearScanoutCount },
        { "linear_scanout_srgb_encode_count",
          frame.LinearScanoutSrgbEncodeCount },
        { "nri_swapchain_acquire_count",
          frame.NriSwapchainAcquireCount },
        { "nri_swapchain_present_count",
          frame.NriSwapchainPresentCount },
        { "nri_swapchain_image_count",
          frame.NriSwapchainImageCount },
        { "nri_swapchain_owned", frame.NriSwapchainOwned },
        { "nri_swapchain_synchronization_owned",
          frame.NriSwapchainSynchronizationOwned },
        { "nri_present_worker_bypassed",
          frame.NriPresentWorkerBypassed },
        { "presentation_apply_count",
          frame.PresentationApplyCount },
        { "presentation_rollback_count",
          frame.PresentationRollbackCount },
        { "presentation_apply_failure_count",
          frame.PresentationApplyFailureCount },
        { "presentation_settings_applied",
          frame.PresentationSettingsApplied },
        { "presentation_window_mode",
          frame.PresentationWindowMode },
        { "presentation_requested_width",
          frame.PresentationRequestedWidth },
        { "presentation_requested_height",
          frame.PresentationRequestedHeight },
        { "presentation_swapchain_width",
          frame.PresentationSwapchainWidth },
        { "presentation_swapchain_height",
          frame.PresentationSwapchainHeight },
        { "presentation_vsync",
          frame.PresentationVsync },
        { "internal_resolution_scale", frame.InternalResolutionScale },
        { "output_width", frame.OutputWidth },
        { "output_height", frame.OutputHeight },
        { "internal_target_width", frame.InternalTargetWidth },
        { "internal_target_height", frame.InternalTargetHeight },
        { "temporal_jitter_draw_count", frame.TemporalJitterDrawCount },
        { "rigid_motion_draw_count", frame.RigidMotionDrawCount },
        { "exact_motion_draw_count", frame.ExactMotionDrawCount },
        { "deformed_motion_draw_count", frame.DeformedMotionDrawCount },
        { "grass_motion_blade_count", frame.GrassMotionBladeCount },
        { "first_submission_id", frame.FirstSubmissionId },
        { "last_submission_id", frame.LastSubmissionId },
        { "last_render_target_namespace", frame.LastRenderTargetNamespace },
        { "last_color_physical_address", frame.LastColorPhysicalAddress },
        { "last_depth_physical_address", frame.LastDepthPhysicalAddress },
        { "last_framebuffer_width", frame.LastFramebufferWidth },
        { "last_framebuffer_height", frame.LastFramebufferHeight },
        { "last_depth_range", frame.LastDepthRange },
        { "last_near_plane", frame.LastNearPlane },
        { "saw_depth_test", frame.SawDepthTest },
        { "saw_depth_write", frame.SawDepthWrite },
    };
}

} // namespace

bool Oot3dVulkanDiagnosticsConfig::Enabled() const {
    return !OutputPath.empty() && MaximumFrames != 0;
}

Oot3dVulkanDiagnosticsConfig
Oot3dVulkanDiagnosticsConfig::FromEnvironment() {
    Oot3dVulkanDiagnosticsConfig config;
    if (const auto output = ReadEnvironment(kOutputPathEnvironment)) {
        config.OutputPath = *output;
    }
    if (const auto maximumValue = ReadEnvironment(kMaximumFramesEnvironment)) {
        const char* maximum = maximumValue->c_str();
        size_t parsed = 0;
        const char* end = maximum;
        while (*end != '\0') {
            ++end;
        }
        const auto result = std::from_chars(maximum, end, parsed);
        if (result.ec == std::errc{} && result.ptr == end && parsed > 0) {
            config.MaximumFrames = parsed;
        }
    }
    if (const auto interval = ReadEnvironment("OOT3D_VULKAN_DIAGNOSTICS_FLUSH_INTERVAL")) {
        size_t parsed = 0;
        const auto result = std::from_chars(interval->data(), interval->data() + interval->size(), parsed);
        if (result.ec == std::errc{} && result.ptr == interval->data() + interval->size()) {
            config.FlushIntervalFrames = parsed;
        }
    }
    return config;
}

Oot3dVulkanDiagnostics::Oot3dVulkanDiagnostics(
    Oot3dVulkanDiagnosticsConfig config)
    : mConfig(std::move(config)) {
}

bool Oot3dVulkanDiagnostics::Enabled() const {
    return mConfig.Enabled();
}

void Oot3dVulkanDiagnostics::ReloadFromEnvironment() {
    const auto environment = Oot3dVulkanDiagnosticsConfig::FromEnvironment();
    if (environment.Enabled()) mConfig = environment;
}

void Oot3dVulkanDiagnostics::SetVulkanAdapter(
    Oot3dVulkanAdapterDiagnostics adapter) {
    mVulkanAdapter = adapter;
}

void Oot3dVulkanDiagnostics::SetD3d12NgxProvider(
    Oot3dD3d12NgxProviderDiagnostics provider) {
    mD3d12NgxProvider = std::move(provider);
    Flush();
}

void Oot3dVulkanDiagnostics::SetVulkanPresentationFallback(
    Oot3dVulkanPresentationFallbackReason reason,
    std::string detail) {
    mNriSwapchainFallbackReason = reason;
    mNriSwapchainFallbackDetail = std::move(detail);
}

void Oot3dVulkanDiagnostics::BeginFrame(uint64_t frameIndex) {
    if (!Enabled()) {
        return;
    }
    if (mActiveFrame.has_value()) {
        EndFrame();
    }
    mActiveFrame.emplace();
    mActiveFrame->FrameIndex = frameIndex;
    mActiveFrame->VulkanAdapterCount =
        mVulkanAdapter.AdapterCount;
    mActiveFrame->VulkanAdapterIndex =
        mVulkanAdapter.AdapterIndex;
    mActiveFrame->VulkanAdapterVendorId =
        mVulkanAdapter.VendorId;
    mActiveFrame->VulkanAdapterDeviceId =
        mVulkanAdapter.DeviceId;
    mActiveFrame->VulkanGraphicsQueueFamily =
        mVulkanAdapter.GraphicsQueueFamily;
    mActiveFrame->VulkanPresentQueueFamily =
        mVulkanAdapter.PresentQueueFamily;
    mActiveFrame->VulkanGraphicsQueueCount =
        mVulkanAdapter.GraphicsQueueCount;
    mActiveFrame->VulkanRequestedGraphicsQueueCount =
        mVulkanAdapter.RequestedGraphicsQueueCount;
    mActiveFrame->VulkanPresentQueueIndex =
        mVulkanAdapter.PresentQueueIndex;
    mActiveFrame->VulkanAsyncPresentSupported =
        mVulkanAdapter.AsynchronousPresentSupported;
    mActiveFrame->NriSwapchainQueueEligible =
        mVulkanAdapter.NriSwapchainQueueEligible;
    mActiveFrame->NriSwapchainFallbackReason =
        static_cast<uint32_t>(mNriSwapchainFallbackReason);
    mActiveFrame->NriSwapchainFallbackDetail =
        mNriSwapchainFallbackDetail;
    mActiveFrame->PresentationApplyCount =
        mPendingPresentationApplyCount;
    mActiveFrame->PresentationRollbackCount =
        mPendingPresentationRollbackCount;
    mActiveFrame->PresentationApplyFailureCount =
        mPendingPresentationApplyFailureCount;
    mActiveFrame->PresentationSettingsApplied =
        mPresentationState.Applied;
    mActiveFrame->PresentationWindowMode =
        mPresentationState.WindowMode;
    mActiveFrame->PresentationRequestedWidth =
        mPresentationState.RequestedWidth;
    mActiveFrame->PresentationRequestedHeight =
        mPresentationState.RequestedHeight;
    mActiveFrame->PresentationSwapchainWidth =
        mPresentationState.SwapchainWidth;
    mActiveFrame->PresentationSwapchainHeight =
        mPresentationState.SwapchainHeight;
    mActiveFrame->PresentationVsync =
        mPresentationState.VSync;
    mPendingPresentationApplyCount = 0;
    mPendingPresentationRollbackCount = 0;
    mPendingPresentationApplyFailureCount = 0;
    mFrameStart = std::chrono::steady_clock::now();
}

void Oot3dVulkanDiagnostics::RecordRendererValidation(
    const Oot3d::RendererValidationSnapshot& snapshot) {
    if (!mActiveFrame.has_value()) {
        return;
    }
    mActiveFrame->VulkanValidationEnabled =
        snapshot.VulkanEnabled;
    mActiveFrame->VulkanValidationInfoCount =
        snapshot.VulkanInfoCount;
    mActiveFrame->VulkanValidationWarningCount =
        snapshot.VulkanWarningCount;
    mActiveFrame->VulkanValidationErrorCount =
        snapshot.VulkanErrorCount;
    mActiveFrame->NriValidationEnabled = snapshot.NriEnabled;
    mActiveFrame->NriValidationInfoCount = snapshot.NriInfoCount;
    mActiveFrame->NriValidationWarningCount =
        snapshot.NriWarningCount;
    mActiveFrame->NriValidationErrorCount =
        snapshot.NriErrorCount;
}

void Oot3dVulkanDiagnostics::RecordNativePicaDraw(
    uint64_t submissionId, uint64_t renderTargetNamespace,
    uint32_t vertexCount, bool indexed, uint32_t colorPhysicalAddress,
    uint32_t depthPhysicalAddress, uint16_t framebufferWidth,
    uint16_t framebufferHeight, float depthRange, float nearPlane,
    bool depthTest, bool depthWrite, bool alphaTest, bool alphaToCoverage) {
    if (!mActiveFrame.has_value()) {
        return;
    }
    auto& frame = *mActiveFrame;
    ++frame.NativePicaDrawCount;
    if (alphaTest)
        ++frame.NativePicaAlphaTestDrawCount;
    if (alphaToCoverage)
        ++frame.NativePicaAlphaToCoverageDrawCount;
    frame.NativePicaVertexCount += vertexCount;
    frame.NativePicaIndexedDrawCount += indexed ? 1U : 0U;
    if (frame.FirstSubmissionId == 0U) {
        frame.FirstSubmissionId = submissionId;
    }
    frame.LastSubmissionId = submissionId;
    frame.LastRenderTargetNamespace = renderTargetNamespace;
    frame.LastColorPhysicalAddress = colorPhysicalAddress;
    frame.LastDepthPhysicalAddress = depthPhysicalAddress;
    frame.LastFramebufferWidth = framebufferWidth;
    frame.LastFramebufferHeight = framebufferHeight;
    frame.LastDepthRange = depthRange;
    frame.LastNearPlane = nearPlane;
    frame.SawDepthTest = frame.SawDepthTest || depthTest;
    frame.SawDepthWrite = frame.SawDepthWrite || depthWrite;
}

void Oot3dVulkanDiagnostics::RecordNativePicaCpuTimings(
    const Oot3dVulkanNativePicaCpuTimings& timings) {
    if (!mActiveFrame.has_value()) {
        return;
    }
    auto& destination = mActiveFrame->NativePicaCpu;
    destination.DrawCount += timings.DrawCount;
    destination.TotalMilliseconds += timings.TotalMilliseconds;
    destination.ShaderVariantMilliseconds +=
        timings.ShaderVariantMilliseconds;
    destination.ShaderVariantCacheHits +=
        timings.ShaderVariantCacheHits;
    destination.ShaderVariantCacheMisses +=
        timings.ShaderVariantCacheMisses;
    destination.ShaderVariantCacheEntries = std::max(
        destination.ShaderVariantCacheEntries,
        timings.ShaderVariantCacheEntries);
    destination.ShaderCacheMilliseconds +=
        timings.ShaderCacheMilliseconds;
    destination.TextureMilliseconds += timings.TextureMilliseconds;
    destination.DrawStateMilliseconds += timings.DrawStateMilliseconds;
    destination.GrassSurfaceMilliseconds +=
        timings.GrassSurfaceMilliseconds;
    destination.SceneStateMilliseconds += timings.SceneStateMilliseconds;
    destination.RenderTargetStateMilliseconds +=
        timings.RenderTargetStateMilliseconds;
    destination.DirectionalShadowMilliseconds +=
        timings.DirectionalShadowMilliseconds;
    destination.GrassRenderMilliseconds += timings.GrassRenderMilliseconds;
    destination.ReflectionEnvironmentMilliseconds +=
        timings.ReflectionEnvironmentMilliseconds;
    destination.TemporalStateMilliseconds +=
        timings.TemporalStateMilliseconds;
    destination.PipelineMilliseconds += timings.PipelineMilliseconds;
    destination.UploadMilliseconds += timings.UploadMilliseconds;
    destination.DescriptorMilliseconds += timings.DescriptorMilliseconds;
    destination.CommandMilliseconds += timings.CommandMilliseconds;
    destination.GeometryRegistryHits += timings.GeometryRegistryHits;
    destination.GeometryRegistryMisses += timings.GeometryRegistryMisses;
    destination.GeometryDynamicBuilds += timings.GeometryDynamicBuilds;
    destination.GeometryRegistryEntries =
        std::max(destination.GeometryRegistryEntries, timings.GeometryRegistryEntries);
    destination.GeometryPersistentDraws += timings.GeometryPersistentDraws;
    destination.GeometryTransientDraws += timings.GeometryTransientDraws;
    destination.GeometryPersistentUploadBytes += timings.GeometryPersistentUploadBytes;
    destination.GeometryTransientUploadBytes += timings.GeometryTransientUploadBytes;
}

void Oot3dVulkanDiagnostics::RecordNativePicaDynamicRendering(
    bool nriOwned) {
    if (mActiveFrame.has_value()) {
        ++mActiveFrame->NativePicaDynamicRenderingCount;
        mActiveFrame->NriPicaRenderingScopeOwned |= nriOwned;
    }
}

void Oot3dVulkanDiagnostics::RecordNriPicaGlobalBarriers(
    uint32_t count) {
    if (mActiveFrame.has_value())
        mActiveFrame->NriPicaGlobalBarrierCount += count;
}

void Oot3dVulkanDiagnostics::RecordNriPicaPipelineBind(
    bool wrapped) {
    if (!mActiveFrame.has_value() || !wrapped) return;
    ++mActiveFrame->NriPicaPipelineBindCount;
    mActiveFrame->NriPicaPipelineWrapped = true;
}

void Oot3dVulkanDiagnostics::RecordNriPicaShaderContract(
    bool ready, bool descriptorLayoutOwned) {
    if (!mActiveFrame.has_value() || !ready) return;
    ++mActiveFrame->NriPicaShaderContractCount;
    mActiveFrame->NriPicaDescriptorLayoutOwned =
        mActiveFrame->NriPicaDescriptorLayoutOwned ||
        descriptorLayoutOwned;
}

void Oot3dVulkanDiagnostics::RecordNriPicaOwnedPipeline(bool ready) {
    if (mActiveFrame.has_value() && ready)
        ++mActiveFrame->NriPicaOwnedPipelineDrawCount;
}

void Oot3dVulkanDiagnostics::RecordNriPicaOwnedDraw(
    bool executed, bool descriptorsOwned, bool uploadsOwned,
    uint64_t uploadBytes) {
    if (!mActiveFrame.has_value() || !executed) return;
    ++mActiveFrame->NriPicaOwnedDrawCount;
    mActiveFrame->NriPicaDescriptorsOwned =
        mActiveFrame->NriPicaDescriptorsOwned ||
        descriptorsOwned;
    mActiveFrame->NriPicaUploadsOwned =
        mActiveFrame->NriPicaUploadsOwned || uploadsOwned;
    if (uploadsOwned)
        ++mActiveFrame->NriPicaOwnedUploadDrawCount;
    mActiveFrame->NriPicaUploadBytes += uploadBytes;
}

void Oot3dVulkanDiagnostics::RecordNriPicaTextureUpload(
    bool executed, uint64_t uploadBytes) {
    if (!mActiveFrame.has_value() || !executed) return;
    ++mActiveFrame->NriPicaTextureUploadCount;
    mActiveFrame->NriPicaTextureUploadBytes += uploadBytes;
    mActiveFrame->NriPicaTextureUploadOwned = true;
}

void Oot3dVulkanDiagnostics::RecordNriPicaTextureImage(bool owned) {
    if (!mActiveFrame.has_value() || !owned) return;
    ++mActiveFrame->NriPicaOwnedTextureImageCount;
    mActiveFrame->NriPicaTextureImagesOwned = true;
}

void Oot3dVulkanDiagnostics::RecordNriPicaDisplayImage(bool owned) {
    if (!mActiveFrame.has_value() || !owned) return;
    ++mActiveFrame->NriPicaOwnedDisplayImageCount;
    mActiveFrame->NriPicaDisplayImagesOwned = true;
}

void Oot3dVulkanDiagnostics::RecordNriPicaRenderTarget(
    bool owned, uint32_t imageCount) {
    if (!mActiveFrame.has_value() || !owned || imageCount == 0U)
        return;
    ++mActiveFrame->NriPicaOwnedRenderTargetCount;
    mActiveFrame->NriPicaOwnedRenderTargetImageCount += imageCount;
    mActiveFrame->NriPicaRenderTargetsOwned = true;
}

void Oot3dVulkanDiagnostics::RecordNriPicaRenderTargetInitialization(bool executed, uint32_t imageCount,
                                                                     uint32_t barrierCount) {
    if (!mActiveFrame.has_value() || !executed || imageCount == 0U)
        return;
    ++mActiveFrame->NriPicaRenderTargetInitializationCount;
    mActiveFrame->NriPicaInitializedRenderTargetImageCount += imageCount;
    mActiveFrame->NriPicaRenderTargetInitializationBarrierCount += barrierCount;
    mActiveFrame->NriPicaRenderTargetInitializationOwned = true;
}

void Oot3dVulkanDiagnostics::RecordNriPicaDisplayCopy(bool executed, uint32_t barrierCount) {
    if (!mActiveFrame.has_value() || !executed)
        return;
    ++mActiveFrame->NriPicaDisplayCopyCount;
    mActiveFrame->NriPicaDisplayCopyBarrierCount += barrierCount;
    mActiveFrame->NriPicaDisplayCopiesOwned = true;
}

void Oot3dVulkanDiagnostics::RecordNriPicaMemoryFillClear(bool shadowCleared, bool attachmentsCleared,
                                                          uint32_t barrierCount) {
    if (!mActiveFrame.has_value() || (!shadowCleared && !attachmentsCleared))
        return;
    mActiveFrame->NriPicaMemoryFillShadowClearCount += shadowCleared ? 1U : 0U;
    mActiveFrame->NriPicaMemoryFillAttachmentClearCount += attachmentsCleared ? 1U : 0U;
    mActiveFrame->NriPicaMemoryFillClearBarrierCount += barrierCount;
    mActiveFrame->NriPicaMemoryFillClearsOwned = true;
}

void Oot3dVulkanDiagnostics::RecordPicaCompositionSchedule(
    const Renderer3ds::PicaCompositionScheduleStats& stats) {
    if (!mActiveFrame.has_value()) {
        return;
    }
    ++mActiveFrame->PicaCompositionSequenceCount;
    mActiveFrame->PicaCompositionPublishedDrawCount += stats.DrawCount;
    mActiveFrame->PicaCompositionRunCount += stats.RunCount;
    mActiveFrame->PicaCompositionTargetCount += stats.TargetCount;
    mActiveFrame->PicaCompositionStageAnchorCount += stats.StageAnchorCount;
    mActiveFrame->PicaCompositionWorldStageTargetCount +=
        stats.WorldStageTargetCount;
    mActiveFrame->PicaCompositionNonSceneTargetCount +=
        stats.NonSceneTargetCount;
    mActiveFrame->PicaCompositionUnknownLayerTargetCount +=
        stats.UnknownLayerTargetCount;
    mActiveFrame->PicaCompositionMissingOpaqueWorldTargetCount +=
        stats.MissingOpaqueWorldTargetCount;
    mActiveFrame->PicaCompositionNonContiguousWorldTargetCount +=
        stats.NonContiguousWorldTargetCount;
    mActiveFrame->PicaCompositionNonCanonicalTailTargetCount +=
        stats.NonCanonicalTailTargetCount;
}

void Oot3dVulkanDiagnostics::RecordPicaCompositionScheduleDraw(
    bool matched) {
    if (!mActiveFrame.has_value()) {
        return;
    }
    if (matched) {
        ++mActiveFrame->PicaCompositionConsumedDrawCount;
    } else {
        ++mActiveFrame->PicaCompositionExecutionMismatchCount;
    }
}

void Oot3dVulkanDiagnostics::RecordPicaDisplayComposition(
    ::Oot3d::Renderer::PicaCompositionDomain domain,
    bool sceneResolved) {
    if (!mActiveFrame.has_value()) {
        return;
    }
    switch (domain) {
        case ::Oot3d::Renderer::PicaCompositionDomain::Scene:
            ++mActiveFrame->PicaDisplaySceneSnapshotCount;
            break;
        case ::Oot3d::Renderer::PicaCompositionDomain::Ui:
            ++mActiveFrame->PicaDisplayUiSnapshotCount;
            break;
        case ::Oot3d::Renderer::PicaCompositionDomain::Unknown:
            ++mActiveFrame->PicaDisplayUnknownSnapshotCount;
            break;
    }
    mActiveFrame->PicaDisplaySceneResolvedSnapshotCount +=
        sceneResolved ? 1U : 0U;
}

void Oot3dVulkanDiagnostics::RecordPicaSceneFrame(
    const Oot3d::PicaSceneFrameStats& stats) {
    if (!mActiveFrame.has_value()) {
        return;
    }
    mActiveFrame->PicaSceneFrameDrawCount = stats.DrawCount;
    mActiveFrame->PicaSceneFrameTemporalSampleCount = stats.TemporalSampleCount;
    mActiveFrame->PicaSceneFrameSyntheticTemporalSampleCount = stats.SyntheticTemporalSampleCount;
    mActiveFrame->PicaSceneFrameTemporalSampleMultiplier = stats.TemporalSampleMultiplier;
    mActiveFrame->PicaSceneFrameTemporalSampleOrdinal = stats.TemporalSampleOrdinal;
    mActiveFrame->PicaSceneFrameSceneDomainDrawCount =
        stats.SceneDomainDrawCount;
    mActiveFrame->PicaSceneFrameUiDomainDrawCount =
        stats.UiDomainDrawCount;
    mActiveFrame->PicaSceneFrameUnknownDomainDrawCount =
        stats.UnknownDomainDrawCount;
    mActiveFrame->PicaSceneFrameOpaqueWorldLayerDrawCount =
        stats.OpaqueWorldLayerDrawCount;
    mActiveFrame->PicaSceneFrameTransparentWorldLayerDrawCount =
        stats.TransparentWorldLayerDrawCount;
    mActiveFrame->PicaSceneFrameAtmosphereLayerDrawCount =
        stats.AtmosphereLayerDrawCount;
    mActiveFrame->PicaSceneFrameUiLayerDrawCount =
        stats.UiLayerDrawCount;
    mActiveFrame->PicaSceneFrameUnknownLayerDrawCount =
        stats.UnknownLayerDrawCount;
    mActiveFrame->PicaSceneFrameNativeCmbPassProvenanceDrawCount =
        stats.NativeCmbPassProvenanceDrawCount;
    mActiveFrame->PicaSceneFrameNativeControlFlowProvenanceDrawCount =
        stats.NativeControlFlowProvenanceDrawCount;
    mActiveFrame->PicaSceneFrameNativeUiLifecycleProvenanceDrawCount =
        stats.NativeUiLifecycleProvenanceDrawCount;
    mActiveFrame->PicaSceneFrameUnknownProvenanceDrawCount =
        stats.UnknownProvenanceDrawCount;
    mActiveFrame->PicaSceneFrameResolvedRasterStateCount =
        stats.ResolvedRasterStateCount;
    mActiveFrame->PicaSceneFrameResolvedRenderTargetStateCount =
        stats.ResolvedRenderTargetStateCount;
    mActiveFrame->PicaSceneFrameResolvedRenderTargetGpuResourceCount =
        stats.ResolvedRenderTargetGpuResourceCount;
    mActiveFrame->PicaSceneFrameResolvedMaterialStateCount =
        stats.ResolvedMaterialStateCount;
    mActiveFrame->PicaSceneFrameNativeLightingStateCount =
        stats.NativeLightingStateCount;
    mActiveFrame->PicaSceneFrameNativeFragmentLightingStateCount =
        stats.NativeFragmentLightingStateCount;
    mActiveFrame->PicaSceneFrameNativeFragmentLightingEnabledDrawCount =
        stats.NativeFragmentLightingEnabledDrawCount;
    mActiveFrame->PicaSceneFrameNativeFogStateCount =
        stats.NativeFogStateCount;
    mActiveFrame->PicaSceneFrameNativeTransformStateCount =
        stats.NativeTransformStateCount;
    mActiveFrame->PicaSceneFramePreviousNativeTransformStateCount =
        stats.PreviousNativeTransformStateCount;
    mActiveFrame->PicaSceneFrameNativeSkeletonStateCount =
        stats.NativeSkeletonStateCount;
    mActiveFrame->PicaSceneFramePreviousNativeSkeletonStateCount =
        stats.PreviousNativeSkeletonStateCount;
    mActiveFrame->PicaSceneFrameRejectedDrawCount =
        stats.RejectedDrawCount;
    mActiveFrame->PicaSceneFrameBoundTextureCount =
        stats.BoundTextureCount;
    mActiveFrame->PicaSceneFrameShaderCatalogEntries =
        static_cast<uint32_t>(stats.ShaderCatalogEntries);
    mActiveFrame->PicaSceneFrameVertexLayoutCatalogEntries =
        static_cast<uint32_t>(stats.VertexLayoutCatalogEntries);
    mActiveFrame->PicaSceneFrameCanonicalPipelineEntries =
        static_cast<uint32_t>(stats.CanonicalPipelineEntries);
    mActiveFrame->PicaSceneFrameCanonicalFullRegisterStateEntries =
        static_cast<uint32_t>(
            stats.CanonicalFullRegisterStateEntries);
}

void Oot3dVulkanDiagnostics::RecordPicaShaderProfile(
    bool nativeFidelity) {
    if (!mActiveFrame.has_value()) {
        return;
    }
    mActiveFrame->PicaNativeFidelityProfile = nativeFidelity;
}

void Oot3dVulkanDiagnostics::RecordPicaAttachmentContract(
    uint32_t colorAttachmentCount, uint32_t auxiliaryOutputMask) {
    if (!mActiveFrame.has_value()) {
        return;
    }
    mActiveFrame->PicaColorAttachmentCount = colorAttachmentCount;
    mActiveFrame->PicaAuxiliaryOutputMask = auxiliaryOutputMask;
}

void Oot3dVulkanDiagnostics::RecordPicaPipelinePrewarm(
    uint32_t graphicsPipelineCacheEntries,
    uint32_t manifestEntries, bool enabled,
    uint32_t created, uint32_t reused, uint32_t skipped) {
    if (!mActiveFrame.has_value()) {
        return;
    }
    mActiveFrame->PicaGraphicsPipelineCacheEntries =
        graphicsPipelineCacheEntries;
    mActiveFrame->PicaPipelineManifestEntries = manifestEntries;
    mActiveFrame->PicaPipelinePrewarmEnabled = enabled;
    mActiveFrame->PicaPipelinePrewarmCreated = created;
    mActiveFrame->PicaPipelinePrewarmReused = reused;
    mActiveFrame->PicaPipelinePrewarmSkipped = skipped;
}

void Oot3dVulkanDiagnostics::RecordEffectGraph(
    uint32_t passCount, uint32_t resourceCount,
    uint32_t declaredBindingCount,
    uint32_t declaredBarrierCount) {
    if (!mActiveFrame.has_value()) return;
    mActiveFrame->EffectGraphPassCount =
        std::max(mActiveFrame->EffectGraphPassCount, passCount);
    mActiveFrame->EffectGraphResourceCount =
        std::max(mActiveFrame->EffectGraphResourceCount, resourceCount);
    mActiveFrame->EffectGraphDeclaredBindingCount = std::max(
        mActiveFrame->EffectGraphDeclaredBindingCount,
        declaredBindingCount);
    mActiveFrame->EffectGraphDeclaredBarrierCount = std::max(
        mActiveFrame->EffectGraphDeclaredBarrierCount,
        declaredBarrierCount);
}

void Oot3dVulkanDiagnostics::RecordEffectGraphGuideImageBarriers(
    uint32_t count) {
    if (!mActiveFrame.has_value()) return;
    mActiveFrame->EffectGraphGuideImageBarrierCount += count;
}

void Oot3dVulkanDiagnostics::RecordEffectGraphInternalImageTransitions(
    uint32_t planned, uint32_t emitted,
    uint32_t privatePlanned, uint32_t privateEmitted) {
    if (!mActiveFrame.has_value()) return;
    mActiveFrame->EffectGraphInternalImageTransitionCount += emitted;
    mActiveFrame->EffectGraphElidedImageTransitionCount +=
        planned > emitted ? planned - emitted : 0U;
    mActiveFrame->EffectProviderPrivateImageTransitionCount +=
        privateEmitted;
    mActiveFrame->EffectProviderPrivateElidedImageTransitionCount +=
        privatePlanned > privateEmitted
            ? privatePlanned - privateEmitted
            : 0U;
}

void Oot3dVulkanDiagnostics::RecordEffectGeometryProvider(
    const Oot3d::EffectGeometryProviderExecutionSummary& summary) {
    if (!mActiveFrame.has_value()) return;
    auto& frame = *mActiveFrame;
    frame.EffectGeometryProviderDeclared = summary.Declared;
    frame.EffectGeometryProviderStage =
        static_cast<uint32_t>(summary.Stage);
    frame.EffectGeometrySourceInspectionCount =
        summary.SourceInspectionCount;
    frame.EffectGeometrySourcePublishedCount =
        summary.SourcePublishedCount;
    frame.EffectGeometryDeclaredBoundaryInvocationCount =
        summary.DeclaredBoundaryInvocationCount;
    frame.EffectGeometryFallbackInvocationCount =
        summary.FallbackInvocationCount;
    frame.EffectGeometryExecutedCount = summary.ExecutedCount;
    frame.EffectGeometryReusedCount = summary.ReusedCount;
    frame.EffectGeometrySkippedCount = summary.SkippedCount;
    frame.EffectGeometryFailedCount = summary.FailedCount;
    frame.EffectGeometryInputUnavailableCount =
        summary.InputUnavailableCount;
    frame.EffectGeometryScheduleRejectedCount =
        summary.ScheduleRejectedCount;
}

void Oot3dVulkanDiagnostics::RecordDisplayEffectGraphExecution(
    const Oot3d::DisplayEffectExecutionSummary& summary) {
    if (!mActiveFrame.has_value()) return;
    auto& frame = *mActiveFrame;
    ++frame.DisplayEffectGraphInvocationCount;
    frame.DisplayEffectGraphDeclaredPassCount += summary.DeclaredPassCount;
    frame.DisplayEffectGraphExecutedPassCount += summary.ExecutedPassCount;
    frame.DisplayEffectGraphReusedPassCount += summary.ReusedPassCount;
    frame.DisplayEffectGraphFusedPassCount += summary.FusedPassCount;
    frame.DisplayEffectGraphSkippedPassCount += summary.SkippedPassCount;
    frame.DisplayEffectGraphFailedPassCount += summary.FailedPassCount;
    frame.DisplayEffectGraphUnresolvedPassCount += summary.UnresolvedPassCount;
    frame.DisplayEffectGraphActiveBindingCount += summary.ActiveBindingCount;
    frame.DisplayEffectGraphUndeclaredRecordCount +=
        summary.UndeclaredRecordCount;
    frame.DisplayEffectGraphDuplicateRecordCount +=
        summary.DuplicateRecordCount;
    frame.DisplayEffectGraphDeclaredPassMask |= summary.DeclaredPassMask;
    frame.DisplayEffectGraphExecutedPassMask |= summary.ExecutedPassMask;
    frame.DisplayEffectGraphReusedPassMask |= summary.ReusedPassMask;
    frame.DisplayEffectGraphFusedPassMask |= summary.FusedPassMask;
    frame.DisplayEffectGraphSkippedPassMask |= summary.SkippedPassMask;
    frame.DisplayEffectGraphFailedPassMask |= summary.FailedPassMask;
    frame.DisplayEffectGraphUnresolvedPassMask |= summary.UnresolvedPassMask;
}

void Oot3dVulkanDiagnostics::RecordDisplayEffectGraphBindings(
    const Oot3d::EffectResourceBindingValidation& validation,
    bool compatibilityFallback) {
    if (!mActiveFrame.has_value()) return;
    auto& frame = *mActiveFrame;
    frame.DisplayEffectGraphDeclaredReadCount +=
        validation.DeclaredReadCount;
    frame.DisplayEffectGraphResolvedReadCount +=
        validation.ResolvedReadCount;
    frame.DisplayEffectGraphMissingReadCount +=
        validation.MissingReadCount;
    frame.DisplayEffectGraphMissingReadMask |=
        validation.MissingReadMask;
    if (compatibilityFallback) {
        ++frame.DisplayEffectGraphBindingFallbackCount;
    }
}

void Oot3dVulkanDiagnostics::RecordDisplayEffectPhysicalPlan(
    const Oot3d::EffectPhysicalPlanSummary& summary) {
    if (!mActiveFrame.has_value()) return;
    auto& frame = *mActiveFrame;
    ++frame.DisplayEffectPhysicalPlanInvocationCount;
    if (summary.Complete()) {
        ++frame.DisplayEffectPhysicalPlanCompleteCount;
    }
    frame.DisplayEffectPhysicalDeclaredResourceCount +=
        summary.DeclaredResourceCount;
    frame.DisplayEffectPhysicalResolvedResourceCount +=
        summary.ResolvedResourceCount;
    frame.DisplayEffectPhysicalMissingResourceCount +=
        summary.MissingResourceCount;
    frame.DisplayEffectPhysicalMissingResourceMask |=
        summary.MissingResourceMask;
    frame.DisplayEffectPhysicalSemanticResourceCount +=
        summary.SemanticResourceCount;
    frame.DisplayEffectPhysicalExternalImageResourceCount +=
        summary.ExternalImageResourceCount;
    frame.DisplayEffectPhysicalNativeAttachmentResourceCount +=
        summary.NativeAttachmentResourceCount;
    frame.DisplayEffectPhysicalTransientImageResourceCount +=
        summary.TransientImageResourceCount;
    frame.DisplayEffectPhysicalTemporalHistoryResourceCount +=
        summary.TemporalHistoryResourceCount;
    frame.DisplayEffectPhysicalImageCount +=
        summary.PhysicalImageCount;
    frame.DisplayEffectPhysicalAliasEligibleResourceCount +=
        summary.AliasEligibleResourceCount;
    frame.DisplayEffectPhysicalAliasSlotCount +=
        summary.AliasSlotCount;
    frame.DisplayEffectPhysicalAliasOpportunityCount +=
        summary.AliasOpportunityCount;
    frame.DisplayEffectPhysicalPeakLiveTransientCount = std::max(
        frame.DisplayEffectPhysicalPeakLiveTransientCount,
        summary.PeakLiveTransientCount);
}

void Oot3dVulkanDiagnostics::RecordEffectTransientImageArena(
    const Oot3d::EffectTransientAllocationSummary& summary,
    bool configured, uint32_t allocatedSlotCount,
    bool reusedConfiguration) {
    if (!mActiveFrame.has_value()) return;
    auto& frame = *mActiveFrame;
    ++frame.EffectTransientArenaInvocationCount;
    if (configured) ++frame.EffectTransientArenaConfiguredCount;
    frame.EffectTransientArenaResourceCount +=
        summary.PlannedResourceCount;
    frame.EffectTransientArenaPhysicalSlotCount +=
        summary.PhysicalSlotCount;
    frame.EffectTransientArenaAliasOpportunityCount +=
        summary.AliasOpportunityCount;
    frame.EffectTransientArenaPeakLiveResourceCount = std::max(
        frame.EffectTransientArenaPeakLiveResourceCount,
        summary.PeakLiveResourceCount);
    frame.EffectTransientArenaAllocatedSlotCount += allocatedSlotCount;
    if (reusedConfiguration) {
        ++frame.EffectTransientArenaReusedConfigurationCount;
    }
}

void Oot3dVulkanDiagnostics::RecordPicaShaderSelection(
    bool canonical, bool instrumentationRequested,
    uint32_t canonicalCacheEntries,
    uint32_t instrumentationCacheEntries,
    bool directFragmentHooks, bool temporalVertexRequested,
    bool directTemporalVertexProgram,
    uint32_t canonicalOutputAudits,
    uint32_t canonicalOutputContractRejects,
    uint32_t canonicalCompatibilityAnalyses,
    uint32_t typedInstrumentationContractRejects) {
    if (!mActiveFrame.has_value()) {
        return;
    }
    if (canonical) {
        ++mActiveFrame->PicaCanonicalShaderDrawCount;
    } else {
        ++mActiveFrame->PicaInstrumentedShaderDrawCount;
    }
    if (instrumentationRequested) {
        ++mActiveFrame->PicaShaderInstrumentationRequestedDrawCount;
        if (directFragmentHooks) {
            ++mActiveFrame->PicaDirectFragmentHookDrawCount;
        } else {
            ++mActiveFrame->PicaLegacyFragmentHookAnalysisDrawCount;
        }
    }
    if (temporalVertexRequested) {
        if (directTemporalVertexProgram) {
            ++mActiveFrame->PicaDirectTemporalVertexProgramDrawCount;
        } else {
            ++mActiveFrame->PicaLegacyTemporalVertexAnalysisDrawCount;
        }
    }
    mActiveFrame->PicaCanonicalShaderCacheEntries = std::max(
        mActiveFrame->PicaCanonicalShaderCacheEntries,
        canonicalCacheEntries);
    mActiveFrame->PicaInstrumentationShaderCacheEntries = std::max(
        mActiveFrame->PicaInstrumentationShaderCacheEntries,
        instrumentationCacheEntries);
    mActiveFrame->PicaCanonicalOutputAuditCount = std::max(
        mActiveFrame->PicaCanonicalOutputAuditCount,
        canonicalOutputAudits);
    mActiveFrame->PicaCanonicalOutputContractRejectCount = std::max(
        mActiveFrame->PicaCanonicalOutputContractRejectCount,
        canonicalOutputContractRejects);
    mActiveFrame->PicaCanonicalCompatibilityAnalysisCount = std::max(
        mActiveFrame->PicaCanonicalCompatibilityAnalysisCount,
        canonicalCompatibilityAnalyses);
    mActiveFrame->PicaTypedInstrumentationContractRejectCount = std::max(
        mActiveFrame->PicaTypedInstrumentationContractRejectCount,
        typedInstrumentationContractRejects);
}

void Oot3dVulkanDiagnostics::RecordNriDirectionalShadow(uint32_t casterCount, bool nativeLight) {
    if (!mActiveFrame.has_value()) {
        return;
    }
    ++mActiveFrame->NriDirectionalShadowPassCount;
    mActiveFrame->NriDirectionalShadowCasterCount += casterCount;
    mActiveFrame->NriDirectionalShadowNativeLight |= nativeLight;
}

void Oot3dVulkanDiagnostics::RecordNriDirectionalShadowAttempt(uint32_t queuedCasters,
                                                               uint32_t queuedNativeLights,
                                                               uint32_t executionStage) {
    if (!mActiveFrame.has_value()) {
        return;
    }
    ++mActiveFrame->NriDirectionalShadowAttemptCount;
    mActiveFrame->NriDirectionalShadowQueuedCasterCount =
        (std::max)(mActiveFrame->NriDirectionalShadowQueuedCasterCount,
                   queuedCasters);
    mActiveFrame->NriDirectionalShadowQueuedNativeLightCount =
        (std::max)(
            mActiveFrame->NriDirectionalShadowQueuedNativeLightCount,
            queuedNativeLights);
    mActiveFrame->NriDirectionalShadowExecutionStage =
        (std::max)(mActiveFrame->NriDirectionalShadowExecutionStage,
                   executionStage);
}

void Oot3dVulkanDiagnostics::RecordNriDirectionalShadowLightSelection(
    uint32_t candidateCount, uint32_t clusterCount,
    float geometryWeight, float directionX, float directionY,
    float directionZ) {
    if (!mActiveFrame.has_value()) {
        return;
    }
    mActiveFrame->NriDirectionalShadowLightCandidateCount =
        (std::max)(mActiveFrame->NriDirectionalShadowLightCandidateCount,
                   candidateCount);
    mActiveFrame->NriDirectionalShadowLightClusterCount =
        (std::max)(mActiveFrame->NriDirectionalShadowLightClusterCount,
                   clusterCount);
    if (geometryWeight >=
        mActiveFrame->NriDirectionalShadowLightGeometryWeight) {
        mActiveFrame->NriDirectionalShadowLightGeometryWeight =
            geometryWeight;
        mActiveFrame->NriDirectionalShadowLightDirectionX = directionX;
        mActiveFrame->NriDirectionalShadowLightDirectionY = directionY;
        mActiveFrame->NriDirectionalShadowLightDirectionZ = directionZ;
    }
}

void Oot3dVulkanDiagnostics::RecordNriDirectionalShadowReceiver(
    bool eligible, bool lightingAvailable, bool lightingEnabled,
    bool ambientOnly, bool directUnmatched, bool directMatched,
    bool historyBound) {
    if (!mActiveFrame.has_value()) {
        return;
    }
    if (eligible) {
        ++mActiveFrame->NriDirectionalShadowReceiverEligibleDrawCount;
    }
    mActiveFrame->NriDirectionalShadowReceiverLightingAvailableDrawCount +=
        eligible && lightingAvailable ? 1U : 0U;
    mActiveFrame->NriDirectionalShadowReceiverLightingEnabledDrawCount +=
        eligible && lightingEnabled ? 1U : 0U;
    mActiveFrame->NriDirectionalShadowReceiverAmbientOnlyDrawCount +=
        eligible && ambientOnly ? 1U : 0U;
    mActiveFrame->NriDirectionalShadowReceiverDirectUnmatchedDrawCount +=
        eligible && directUnmatched ? 1U : 0U;
    mActiveFrame->NriDirectionalShadowReceiverDirectMatchedDrawCount +=
        eligible && directMatched ? 1U : 0U;
    if (historyBound) {
        ++mActiveFrame->NriDirectionalShadowReceiverHistoryBoundDrawCount;
    }
}

void Oot3dVulkanDiagnostics::RecordNriDirectionalShadowGraphTransitions(
    uint32_t planned, uint32_t emitted) {
    if (!mActiveFrame.has_value()) {
        return;
    }
    mActiveFrame->NriDirectionalShadowGraphImageTransitionCount += emitted;
    mActiveFrame->NriDirectionalShadowGraphElidedImageTransitionCount +=
        planned > emitted ? planned - emitted : 0U;
}

void Oot3dVulkanDiagnostics::RecordNriDirectionalShadowSchedule(
    bool authorized, bool duplicate, bool surfaceEnd) {
    if (!mActiveFrame.has_value()) {
        return;
    }
    ++mActiveFrame->NriDirectionalShadowScheduleBoundaryCount;
    mActiveFrame->NriDirectionalShadowScheduleAuthorizedCount +=
        authorized ? 1U : 0U;
    mActiveFrame->NriDirectionalShadowScheduleRejectedCount +=
        authorized ? 0U : 1U;
    mActiveFrame->NriDirectionalShadowScheduleDuplicateCount +=
        duplicate ? 1U : 0U;
    mActiveFrame->NriDirectionalShadowScheduleSurfaceEndCount +=
        surfaceEnd ? 1U : 0U;
}

void Oot3dVulkanDiagnostics::RecordDisplayTransfer(bool present, uint32_t spatialAaMode, uint32_t msaaSamples) {
    if (!mActiveFrame.has_value()) {
        return;
    }
    if (present) {
        ++mActiveFrame->ScanoutCount;
        mActiveFrame->SpatialAaMode = (std::max)(
            mActiveFrame->SpatialAaMode, spatialAaMode);
        mActiveFrame->MsaaSamples = msaaSamples;
    } else {
        ++mActiveFrame->DisplayTransferCount;
    }
}

void Oot3dVulkanDiagnostics::RecordSmaa1x(
    bool outputsNriOwned, bool lookupsNriOwned,
    bool computeNriOwned, bool barriersNriOwned,
    bool lookupUploadNriOwned) {
    if (!mActiveFrame.has_value()) return;
    ++mActiveFrame->Smaa1xPassCount;
    ++mActiveFrame->SmaaEdgePassCount;
    ++mActiveFrame->SmaaBlendWeightPassCount;
    ++mActiveFrame->SmaaNeighborhoodPassCount;
    mActiveFrame->SmaaOutputsNriOwned |= outputsNriOwned;
    mActiveFrame->SmaaLookupsNriOwned |= lookupsNriOwned;
    mActiveFrame->SmaaComputeNriOwned |= computeNriOwned;
    mActiveFrame->SmaaBarriersNriOwned |= barriersNriOwned;
    mActiveFrame->SmaaLookupUploadNriOwned |=
        lookupUploadNriOwned;
}

void Oot3dVulkanDiagnostics::RecordOverlay() {
    if (mActiveFrame.has_value()) {
        ++mActiveFrame->OverlayCount;
    }
}

void Oot3dVulkanDiagnostics::RecordRenderResolution(
    float internalScale, uint32_t outputWidth, uint32_t outputHeight,
    uint32_t internalTargetWidth, uint32_t internalTargetHeight) {
    if (!mActiveFrame.has_value()) return;
    mActiveFrame->InternalResolutionScale = internalScale;
    mActiveFrame->OutputWidth = outputWidth;
    mActiveFrame->OutputHeight = outputHeight;
    mActiveFrame->InternalTargetWidth = internalTargetWidth;
    mActiveFrame->InternalTargetHeight = internalTargetHeight;
}

void Oot3dVulkanDiagnostics::RecordTemporalHistory(
    bool valid, bool cameraCut, uint32_t resetReason) {
    if (!mActiveFrame.has_value()) return;
    ++mActiveFrame->TemporalPrepareCount;
    mActiveFrame->TemporalValidCount += valid ? 1U : 0U;
    mActiveFrame->TemporalCameraCutCount += cameraCut ? 1U : 0U;
    mActiveFrame->TemporalResetReason = resetReason;
}

void Oot3dVulkanDiagnostics::RecordMotionVectors(
    bool executed, bool historyValid, bool nriWrapped,
    bool outputsNriOwned, bool computeNriOwned, bool barriersNriOwned) {
    if (!mActiveFrame.has_value() || !executed) return;
    ++mActiveFrame->MotionVectorPassCount;
    mActiveFrame->MotionHistoryValidCount += historyValid ? 1U : 0U;
    mActiveFrame->MotionNriWrapped =
        mActiveFrame->MotionNriWrapped || nriWrapped;
    mActiveFrame->MotionOutputsNriOwned =
        mActiveFrame->MotionOutputsNriOwned || outputsNriOwned;
    mActiveFrame->MotionComputeNriOwned =
        mActiveFrame->MotionComputeNriOwned || computeNriOwned;
    mActiveFrame->MotionBarriersNriOwned =
        mActiveFrame->MotionBarriersNriOwned || barriersNriOwned;
}

void Oot3dVulkanDiagnostics::RecordReactiveWorldDraw() {
    if (mActiveFrame.has_value()) ++mActiveFrame->ReactiveWorldDrawCount;
}

void Oot3dVulkanDiagnostics::RecordTemporalAa(
    bool executed, bool historyValid, bool nriWrapped,
    bool historyNriOwned, bool computeNriOwned, bool barriersNriOwned) {
    if (!mActiveFrame.has_value() || !executed) return;
    ++mActiveFrame->TemporalAaPassCount;
    mActiveFrame->TemporalAaHistoryValidCount += historyValid ? 1U : 0U;
    mActiveFrame->TemporalAaNriWrapped =
        mActiveFrame->TemporalAaNriWrapped || nriWrapped;
    mActiveFrame->TemporalAaHistoryNriOwned =
        mActiveFrame->TemporalAaHistoryNriOwned || historyNriOwned;
    mActiveFrame->TemporalAaComputeNriOwned =
        mActiveFrame->TemporalAaComputeNriOwned || computeNriOwned;
    mActiveFrame->TemporalAaBarriersNriOwned =
        mActiveFrame->TemporalAaBarriersNriOwned || barriersNriOwned;
}

void Oot3dVulkanDiagnostics::RecordSceneCompositePass(
    bool outputNriOwned, bool computeNriOwned, bool barriersNriOwned) {
    if (!mActiveFrame.has_value()) return;
    ++mActiveFrame->SceneCompositePassCount;
    mActiveFrame->SceneCompositeOutputNriOwned =
        mActiveFrame->SceneCompositeOutputNriOwned || outputNriOwned;
    mActiveFrame->SceneCompositeComputeNriOwned =
        mActiveFrame->SceneCompositeComputeNriOwned || computeNriOwned;
    mActiveFrame->SceneCompositeBarriersNriOwned =
        mActiveFrame->SceneCompositeBarriersNriOwned || barriersNriOwned;
}

void Oot3dVulkanDiagnostics::RecordNisUpscale(
    bool executed, uint32_t inputWidth, uint32_t inputHeight,
    uint32_t outputWidth, uint32_t outputHeight) {
    if (!mActiveFrame.has_value()) return;
    if (executed) ++mActiveFrame->NisUpscalePassCount;
    mActiveFrame->NisInputWidth = inputWidth;
    mActiveFrame->NisInputHeight = inputHeight;
    mActiveFrame->NisOutputWidth = outputWidth;
    mActiveFrame->NisOutputHeight = outputHeight;
    mActiveFrame->NisNriDispatched |= executed;
}

void Oot3dVulkanDiagnostics::RecordFsrUpscale(
    bool executed, bool historyReset, uint32_t inputWidth,
    uint32_t inputHeight, uint32_t outputWidth, uint32_t outputHeight) {
    if (!mActiveFrame.has_value()) return;
    if (executed) ++mActiveFrame->FsrUpscalePassCount;
    mActiveFrame->FsrHistoryResetCount +=
        executed && historyReset ? 1U : 0U;
    mActiveFrame->FsrInputWidth = inputWidth;
    mActiveFrame->FsrInputHeight = inputHeight;
    mActiveFrame->FsrOutputWidth = outputWidth;
    mActiveFrame->FsrOutputHeight = outputHeight;
    mActiveFrame->FsrNriDispatched |= executed;
}

void Oot3dVulkanDiagnostics::RecordDlssUpscale(
    bool executed, bool historyReset, uint32_t inputWidth,
    uint32_t inputHeight, uint32_t outputWidth, uint32_t outputHeight) {
    if (!mActiveFrame.has_value()) return;
    if (executed) ++mActiveFrame->DlssUpscalePassCount;
    mActiveFrame->DlssHistoryResetCount +=
        executed && historyReset ? 1U : 0U;
    mActiveFrame->DlssInputWidth = inputWidth;
    mActiveFrame->DlssInputHeight = inputHeight;
    mActiveFrame->DlssOutputWidth = outputWidth;
    mActiveFrame->DlssOutputHeight = outputHeight;
    mActiveFrame->DlssNriDispatched |= executed;
}

void Oot3dVulkanDiagnostics::RecordD3d12NgxFrame(
    bool requested, bool contractReady, bool prepared,
    bool splitSubmitted, bool queued, bool outputAcquired,
    uint64_t dispatchCount) {
    mD3d12NgxProvider.FrameBridgeReady |= contractReady;
    mD3d12NgxProvider.FrameDispatchRequested |= requested;
    mD3d12NgxProvider.FrameDispatchCount = dispatchCount;
    if (!mActiveFrame.has_value()) return;
    mActiveFrame->D3d12NgxFrameRequested |= requested;
    mActiveFrame->D3d12NgxFrameContractReady |= contractReady;
    mActiveFrame->D3d12NgxFramePrepared |= prepared;
    mActiveFrame->D3d12NgxFrameSplitSubmitted |= splitSubmitted;
    mActiveFrame->D3d12NgxFrameQueued |= queued;
    mActiveFrame->D3d12NgxOutputAcquired |= outputAcquired;
}

void Oot3dVulkanDiagnostics::RecordUpscalerOutputOwnership(
    bool executed, bool nriOwned, bool barriersNriOwned) {
    if (mActiveFrame.has_value()) {
        mActiveFrame->UpscalerOutputNriOwned |= executed && nriOwned;
        mActiveFrame->UpscalerBarriersNriOwned |=
            executed && barriersNriOwned;
    }
}

void Oot3dVulkanDiagnostics::RecordNriScanout(
    bool executed, bool pipelineOwned, bool descriptorsOwned,
    bool scopeOwned) {
    if (!mActiveFrame.has_value() || !executed) return;
    ++mActiveFrame->NriScanoutCount;
    mActiveFrame->NriScanoutPipelineOwned |= pipelineOwned;
    mActiveFrame->NriScanoutDescriptorsOwned |= descriptorsOwned;
    mActiveFrame->NriScanoutScopeOwned |= scopeOwned;
}

void Oot3dVulkanDiagnostics::RecordLinearScanout(bool encodeSrgb) {
    if (!mActiveFrame.has_value()) return;
    ++mActiveFrame->LinearScanoutCount;
    mActiveFrame->LinearScanoutSrgbEncodeCount += encodeSrgb ? 1U : 0U;
}

void Oot3dVulkanDiagnostics::RecordNriSwapchainAcquire(
    bool executed, uint32_t imageCount, bool synchronizationOwned,
    bool presentWorkerBypassed) {
    if (!mActiveFrame.has_value() || !executed || imageCount == 0U)
        return;
    ++mActiveFrame->NriSwapchainAcquireCount;
    if (imageCount > mActiveFrame->NriSwapchainImageCount) {
        mActiveFrame->NriSwapchainImageCount = imageCount;
    }
    mActiveFrame->NriSwapchainOwned = true;
    mActiveFrame->NriSwapchainSynchronizationOwned |=
        synchronizationOwned;
    mActiveFrame->NriPresentWorkerBypassed |=
        presentWorkerBypassed;
}

void Oot3dVulkanDiagnostics::RecordNriSwapchainPresent(
    bool executed) {
    if (!mActiveFrame.has_value() || !executed) return;
    ++mActiveFrame->NriSwapchainPresentCount;
    mActiveFrame->NriSwapchainOwned = true;
}

void Oot3dVulkanDiagnostics::RecordPresentationTransaction(
    uint8_t transactionKind, bool success,
    bool automaticRollback) {
    if (!Enabled() || transactionKind == 0U)
        return;
    if (transactionKind == 1U) {
        if (success)
            ++mPendingPresentationApplyCount;
        else
            ++mPendingPresentationApplyFailureCount;
    } else if (transactionKind == 2U && success) {
        ++mPendingPresentationRollbackCount;
    }
    if (automaticRollback)
        ++mPendingPresentationRollbackCount;
}

void Oot3dVulkanDiagnostics::SetPresentationState(
    Oot3dVulkanPresentationStateDiagnostics state) {
    mPresentationState = state;
}

void Oot3dVulkanDiagnostics::RecordTemporalDraw(
    bool rigidMotionValid, bool exactMotionValid, bool deformedMotionValid) {
    if (!mActiveFrame.has_value()) return;
    ++mActiveFrame->TemporalJitterDrawCount;
    mActiveFrame->RigidMotionDrawCount += rigidMotionValid ? 1U : 0U;
    mActiveFrame->ExactMotionDrawCount += exactMotionValid ? 1U : 0U;
    mActiveFrame->DeformedMotionDrawCount +=
        deformedMotionValid ? 1U : 0U;
}

void Oot3dVulkanDiagnostics::RecordGrassMotion(uint32_t bladeCount) {
    if (mActiveFrame.has_value())
        mActiveFrame->GrassMotionBladeCount += bladeCount;
}

void Oot3dVulkanDiagnostics::RecordMemoryFill() {
    if (mActiveFrame.has_value()) {
        ++mActiveFrame->MemoryFillCount;
    }
}

void Oot3dVulkanDiagnostics::RecordOutlineOcclusionDraw() {
    if (mActiveFrame) ++mActiveFrame->OutlineOcclusionDrawCount;
}

void Oot3dVulkanDiagnostics::RecordToonDraw(
    bool picaMaterialPath) {
    if (!mActiveFrame.has_value()) {
        return;
    }
    ++mActiveFrame->ToonDrawCount;
    if (picaMaterialPath) {
        ++mActiveFrame->PicaMaterialToonDrawCount;
    }
}

void Oot3dVulkanDiagnostics::RecordCacaoPass(bool outputNriOwned,
                                              bool picaNormalGuide) {
    if (mActiveFrame.has_value()) {
        ++mActiveFrame->CacaoPassCount;
        mActiveFrame->CacaoOutputNriOwned =
            mActiveFrame->CacaoOutputNriOwned || outputNriOwned;
        if (picaNormalGuide) {
            ++mActiveFrame->CacaoNormalGuidePassCount;
        }
    }
}

void Oot3dVulkanDiagnostics::RecordCacaoAmbientGuideDraw(
    bool exactRgb) {
    if (mActiveFrame.has_value()) {
        ++mActiveFrame->CacaoAmbientGuideDrawCount;
        if (exactRgb) {
            ++mActiveFrame->CacaoAmbientRgbGuideDrawCount;
        } else {
            ++mActiveFrame->CacaoAmbientFallbackGuideDrawCount;
        }
    }
}

void Oot3dVulkanDiagnostics::RecordCacaoAmbientComposite() {
    if (mActiveFrame.has_value()) {
        ++mActiveFrame->CacaoAmbientCompositeCount;
    }
}

void Oot3dVulkanDiagnostics::RecordHiZPass(uint32_t mipCount,
                                           uint32_t depthConvention,
                                           bool outputNriOwned,
                                           bool computeNriOwned,
                                           bool barriersNriOwned) {
    if (mActiveFrame.has_value()) {
        ++mActiveFrame->HiZPassCount;
        mActiveFrame->HiZMipCount = mipCount;
        mActiveFrame->HiZDepthConvention = depthConvention;
        mActiveFrame->HiZOutputNriOwned =
            mActiveFrame->HiZOutputNriOwned || outputNriOwned;
        mActiveFrame->HiZComputeNriOwned =
            mActiveFrame->HiZComputeNriOwned || computeNriOwned;
        mActiveFrame->HiZBarriersNriOwned =
            mActiveFrame->HiZBarriersNriOwned || barriersNriOwned;
    }
}

void Oot3dVulkanDiagnostics::RecordHiZReflection(bool filtered,
                                                  uint32_t debugView,
                                                  bool nriWrapped,
                                                  bool outputsNriOwned,
                                                  bool computeNriOwned,
                                                  bool barriersNriOwned) {
    if (mActiveFrame.has_value()) {
        ++mActiveFrame->HiZReflectionCount;
        mActiveFrame->HiZReflectionFilterCount += filtered ? 1U : 0U;
        mActiveFrame->ReflectionDebugView = debugView;
        mActiveFrame->ReflectionNriWrapped = nriWrapped;
        mActiveFrame->ReflectionOutputsNriOwned =
            mActiveFrame->ReflectionOutputsNriOwned || outputsNriOwned;
        mActiveFrame->ReflectionComputeNriOwned =
            mActiveFrame->ReflectionComputeNriOwned || computeNriOwned;
        mActiveFrame->ReflectionBarriersNriOwned =
            mActiveFrame->ReflectionBarriersNriOwned || barriersNriOwned;
    }
}

void Oot3dVulkanDiagnostics::RecordReflectionMaterialDraw(
    bool calibrated, bool profiled,
    bool noSpecularTevUse,
    bool unsupportedShader) {
    if (!mActiveFrame.has_value()) return;
    ++mActiveFrame->ReflectionMaterialCandidateDrawCount;
    mActiveFrame->ReflectionMaterialCalibratedDrawCount +=
        calibrated ? 1U : 0U;
    mActiveFrame->ReflectionMaterialProfiledDrawCount +=
        profiled ? 1U : 0U;
    mActiveFrame->ReflectionMaterialNoSpecularTevDrawCount +=
        noSpecularTevUse ? 1U : 0U;
    mActiveFrame->ReflectionMaterialUnsupportedDrawCount +=
        unsupportedShader ? 1U : 0U;
}

void Oot3dVulkanDiagnostics::RecordReflectionTextureUsage(
    uint64_t contentHash, uint16_t width, uint16_t height,
    uint8_t mapperSlot, bool profiled, uint32_t ruleId,
    uint8_t profile, float reflectivity, float roughness) {
    if (!mActiveFrame.has_value() || contentHash == 0U) return;
    auto& usages = mActiveFrame->ReflectionTextureUsages;
    const auto match = std::find_if(
        usages.begin(), usages.end(),
        [contentHash, width, height, mapperSlot](const auto& usage) {
            return usage.ContentHash == contentHash &&
                   usage.Width == width &&
                   usage.Height == height &&
                   usage.MapperSlot == mapperSlot;
        });
    if (match != usages.end()) {
        ++match->DrawCount;
        match->ProfiledDrawCount += profiled ? 1U : 0U;
        if (profiled) {
            match->RuleId = ruleId;
            match->Profile = profile;
            match->Reflectivity = reflectivity;
            match->Roughness = roughness;
        }
        return;
    }
    if (usages.size() >= kMaximumReflectionTextureUsagesPerFrame) {
        ++mActiveFrame->ReflectionTextureUsageOverflowCount;
        return;
    }
    usages.push_back({
        contentHash,
        width,
        height,
        mapperSlot,
        1U,
        profiled ? 1U : 0U,
        profiled ? ruleId : 0U,
        profiled ? profile : uint8_t{0},
        profiled ? reflectivity : 0.0F,
        profiled ? roughness : 1.0F,
    });
}

void Oot3dVulkanDiagnostics::RecordLinearWorkingColor(
    bool sourceSrgb, bool outputNriOwned, bool computeNriOwned,
    bool barriersNriOwned) {
    if (mActiveFrame.has_value()) {
        ++mActiveFrame->LinearWorkingColorCount;
        mActiveFrame->LinearWorkingColorSourceSrgb =
            mActiveFrame->LinearWorkingColorSourceSrgb || sourceSrgb;
        mActiveFrame->LinearWorkingColorOutputNriOwned =
            mActiveFrame->LinearWorkingColorOutputNriOwned ||
            outputNriOwned;
        mActiveFrame->LinearWorkingColorComputeNriOwned =
            mActiveFrame->LinearWorkingColorComputeNriOwned ||
            computeNriOwned;
        mActiveFrame->LinearWorkingColorBarriersNriOwned =
            mActiveFrame->LinearWorkingColorBarriersNriOwned ||
            barriersNriOwned;
    }
}

void Oot3dVulkanDiagnostics::RecordFidelityFxSssr(
    bool nriWrapped, bool outputNriOwned, bool historyValid,
    bool linearInput) {
    if (mActiveFrame.has_value()) {
        ++mActiveFrame->FidelityFxSssrCount;
        mActiveFrame->FidelityFxSssrHistoryValidCount +=
            historyValid ? 1U : 0U;
        mActiveFrame->FidelityFxSssrNriWrapped =
            mActiveFrame->FidelityFxSssrNriWrapped || nriWrapped;
        mActiveFrame->FidelityFxSssrOutputNriOwned =
            mActiveFrame->FidelityFxSssrOutputNriOwned ||
            outputNriOwned;
        mActiveFrame->FidelityFxSssrLinearInput =
            mActiveFrame->FidelityFxSssrLinearInput || linearInput;
    }
}

void Oot3dVulkanDiagnostics::RecordFidelityFxSssrFallback(
    std::string reason) {
    if (mActiveFrame.has_value()) {
        ++mActiveFrame->FidelityFxSssrFallbackCount;
        mActiveFrame->FidelityFxSssrFallbackReason = std::move(reason);
    }
}

void Oot3dVulkanDiagnostics::RecordReflectionIbl(
    bool profileUpdated, bool picaDerived,
    uint32_t environmentMipCount, bool environmentNriOwned,
    bool brdfNriOwned, bool computeNriOwned,
    bool barriersNriOwned) {
    if (mActiveFrame.has_value()) {
        ++mActiveFrame->ReflectionIblCount;
        mActiveFrame->ReflectionIblProfileUpdateCount +=
            profileUpdated ? 1U : 0U;
        mActiveFrame->ReflectionIblPicaDerived =
            mActiveFrame->ReflectionIblPicaDerived || picaDerived;
        mActiveFrame->ReflectionIblEnvironmentMipCount =
            environmentMipCount;
        mActiveFrame->ReflectionIblEnvironmentNriOwned =
            mActiveFrame->ReflectionIblEnvironmentNriOwned ||
            environmentNriOwned;
        mActiveFrame->ReflectionIblBrdfNriOwned =
            mActiveFrame->ReflectionIblBrdfNriOwned || brdfNriOwned;
        mActiveFrame->ReflectionIblComputeNriOwned =
            mActiveFrame->ReflectionIblComputeNriOwned ||
            computeNriOwned;
        mActiveFrame->ReflectionIblBarriersNriOwned =
            mActiveFrame->ReflectionIblBarriersNriOwned ||
            barriersNriOwned;
    }
}

void Oot3dVulkanDiagnostics::RecordReflectionMaterialResolve(
    bool outputNriOwned, bool computeNriOwned,
    bool barriersNriOwned) {
    if (mActiveFrame.has_value()) {
        ++mActiveFrame->ReflectionMaterialResolveCount;
        mActiveFrame->ReflectionMaterialResolveOutputNriOwned =
            mActiveFrame->ReflectionMaterialResolveOutputNriOwned ||
            outputNriOwned;
        mActiveFrame->ReflectionMaterialResolveComputeNriOwned =
            mActiveFrame->ReflectionMaterialResolveComputeNriOwned ||
            computeNriOwned;
        mActiveFrame->ReflectionMaterialResolveBarriersNriOwned =
            mActiveFrame->ReflectionMaterialResolveBarriersNriOwned ||
            barriersNriOwned;
    }
}

void Oot3dVulkanDiagnostics::EndFrame() {
    if (!mActiveFrame.has_value()) {
        return;
    }
    const auto milliseconds =
        MillisecondsBetween(mFrameStart, std::chrono::steady_clock::now());
    mActiveFrame->CpuFrameMilliseconds = milliseconds.value_or(0.0);
    mFrames.push_back(std::move(*mActiveFrame));
    mActiveFrame.reset();
    if (mFrames.size() > mConfig.MaximumFrames) {
        mFrames.erase(mFrames.begin(),
                      mFrames.begin() +
                          static_cast<std::ptrdiff_t>(
                              mFrames.size() - mConfig.MaximumFrames));
    }
    // Serializing the complete rolling history on every frame made an enabled
    // profiler dominate render time. Normal captures flush only when requested.
    if (mConfig.FlushIntervalFrames != 0 && ++mFramesSinceFlush >= mConfig.FlushIntervalFrames) {
        Flush();
        mFramesSinceFlush = 0;
    }
}

Oot3dVulkanFrameDiagnostics*
Oot3dVulkanDiagnostics::FindFrame(uint64_t frameIndex) {
    const auto found = std::find_if(
        mFrames.rbegin(), mFrames.rend(),
        [frameIndex](const auto& frame) {
            return frame.FrameIndex == frameIndex;
        });
    return found == mFrames.rend() ? nullptr : &*found;
}

void Oot3dVulkanDiagnostics::SetGpuTimings(
    uint64_t frameIndex, const Oot3dVulkanGpuTimings& timings) {
    if (auto* frame = FindFrame(frameIndex); frame != nullptr) {
        frame->Gpu = timings;
    }
}

void Oot3dVulkanDiagnostics::Flush() const {
    if (!Enabled()) {
        return;
    }
    std::error_code error;
    const auto parent = mConfig.OutputPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            return;
        }
    }

    nlohmann::json frames = nlohmann::json::array();
    for (const auto& frame : mFrames) {
        frames.push_back(FrameToJson(frame));
    }
    const nlohmann::json d3d12NgxProvider = {
        { "adapter_matched", mD3d12NgxProvider.AdapterMatched },
        { "device_ready", mD3d12NgxProvider.DeviceReady },
        { "nri_ready", mD3d12NgxProvider.NriReady },
        { "dlss_feature_ready", mD3d12NgxProvider.DlssFeatureReady },
        { "dlss_contract_resources_ready",
          mD3d12NgxProvider.DlssContractResourcesReady },
        { "dlss_evaluate_ready", mD3d12NgxProvider.DlssEvaluateReady },
        { "external_memory_ready",
          mD3d12NgxProvider.ExternalMemoryReady },
        { "shared_fence_ready", mD3d12NgxProvider.SharedFenceReady },
        { "zero_copy_interop_ready",
          mD3d12NgxProvider.ZeroCopyInteropReady },
        { "vendor_id", mD3d12NgxProvider.VendorId },
        { "device_id", mD3d12NgxProvider.DeviceId },
        { "probe_input_width", mD3d12NgxProvider.ProbeInputWidth },
        { "probe_input_height", mD3d12NgxProvider.ProbeInputHeight },
        { "probe_output_width", mD3d12NgxProvider.ProbeOutputWidth },
        { "probe_output_height", mD3d12NgxProvider.ProbeOutputHeight },
        { "frame_bridge_ready", mD3d12NgxProvider.FrameBridgeReady },
        { "frame_dispatch_requested",
          mD3d12NgxProvider.FrameDispatchRequested },
        { "frame_dispatch_count", mD3d12NgxProvider.FrameDispatchCount },
        { "adapter_name", mD3d12NgxProvider.AdapterName },
        { "detail", mD3d12NgxProvider.Detail },
    };
    const nlohmann::json root = {
        { "format", "oot3d_vulkan_diagnostics_v1" },
        { "render_mode", "authentic" },
        { "effects_enabled", false },
        { "compatibility_contract",
          "original_pica_runtime_owns_scene_rasterization" },
        { "frame_count", frames.size() },
        { "d3d12_ngx_provider", std::move(d3d12NgxProvider) },
        { "frames", std::move(frames) },
    };
    std::ofstream output(mConfig.OutputPath,
                         std::ios::binary | std::ios::trunc);
    if (output) {
        output << root.dump(2) << '\n';
    }
}

const std::vector<Oot3dVulkanFrameDiagnostics>&
Oot3dVulkanDiagnostics::Frames() const {
    return mFrames;
}

} // namespace Fast
