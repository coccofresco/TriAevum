#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/extension/effect_geometry_provider_plan.h"
#include "fast/extension/grass_interaction_field.h"
#include "fast/extension/grass_placement_cache.h"
#include "fast/extension/pica_attachment_contract.h"
#include "fast/extension/toon_surface_response.h"
#include "fast/renderer3ds/pica_resolved_draw_stream.h"
#include "fast/renderer3ds/pica_scene_payloads.h"
#include "fast/renderer/shaderc_compiler.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <array>
#include <memory>
#include <filesystem>
#include <string>

namespace Fast {
class Oot3dVulkanGpuProfiler;
}
namespace Fast::Extension {

struct NriDirectionalShadowHistorySnapshot;

class InteractiveGrassPass final {
  public:
    InteractiveGrassPass();
    ~InteractiveGrassPass();
    InteractiveGrassPass(const InteractiveGrassPass&) = delete;
    InteractiveGrassPass& operator=(const InteractiveGrassPass&) = delete;

    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                    Renderer::CachedPassShaderCompiler& shaders,
                    VkRenderPass canonicalRenderPass,
                    VkRenderPass instrumentedRenderPass,
                    VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT,
                    bool dynamicRendering = false,
                    VkFormat depthFormat = VK_FORMAT_UNDEFINED,
                    // The device's persisted pipeline cache (optional).
                    VkPipelineCache pipelineCache = VK_NULL_HANDLE);
    bool Prepare(VkCommandBuffer commandBuffer, uint32_t width, uint32_t height,
                 const ::Fast::Renderer3ds::PicaPerspectiveCameraState& view, const InteractiveGrassSettings& settings,
                 const ToonSurfaceParameters& toon, ::Fast::Renderer3ds::PicaResolvedDrawStreamView scene,
                 uint64_t frameId, uint64_t renderTargetNamespace, uint32_t framebufferColorPhysicalAddress,
                 uint32_t frameSlot, const EffectGeometryProviderPlan& providerPlan,
                 const NriDirectionalShadowHistorySnapshot& shadowHistory,
                 const std::array<float, 2>& jitterPixels = {}, bool temporalJitter = false,
                 bool temporalMotion = false, Fast::Oot3dVulkanGpuProfiler* profiler = nullptr);
    bool DrawPrepared(VkCommandBuffer commandBuffer, uint32_t width,
                      uint32_t height,
                      const EffectGeometryProviderPlan& providerPlan,
                      const PicaAttachmentRequirements& attachments);
    void SetCacheDirectory(std::filesystem::path directory);
    // TRIAEVUM_GRASS_ROOT_LIGHTING_REUSE=1 (experiment, default off): the
    // presentation sample the next Prepare belongs to. Native samples (not an
    // in-between transition sample) evaluate the root lighting and keep it per
    // anchor; in-between samples reuse it.
    void SetRootLightingSample(bool native, uint64_t sourceFrameId);
    void ResetTemporalState();
    void Shutdown();

    [[nodiscard]] uint32_t LastBladeCount() const;
    [[nodiscard]] uint32_t LastMotionBladeCount() const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    void DrawPreparedCards(VkCommandBuffer commandBuffer, uint32_t width, uint32_t height,
                           const PicaAttachmentRequirements& attachments);
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Extension

#endif
