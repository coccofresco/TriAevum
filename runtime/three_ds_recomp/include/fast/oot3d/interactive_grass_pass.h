#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/effect_geometry_provider_plan.h"
#include "fast/oot3d/grass_interaction_field.h"
#include "fast/oot3d/grass_placement_cache.h"
#include "fast/oot3d/pica_attachment_contract.h"
#include "fast/renderer3ds/pica_scene_payloads.h"
#include "fast/renderer/shaderc_compiler.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <array>
#include <memory>
#include <string>

namespace Fast::Oot3d {

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
                    VkFormat depthFormat = VK_FORMAT_UNDEFINED);
    bool Prepare(VkCommandBuffer commandBuffer, uint32_t width,
                 uint32_t height,
                 const ::Fast::Renderer3ds::PicaPerspectiveCameraState& view,
                 const InteractiveGrassSettings& settings,
                 uint64_t frameId, uint64_t renderTargetNamespace,
                 uint32_t framebufferColorPhysicalAddress,
                 uint32_t frameSlot,
                 const EffectGeometryProviderPlan& providerPlan,
                 const std::array<float, 2>& jitterPixels = {},
                 bool temporalJitter = false);
    bool DrawPrepared(VkCommandBuffer commandBuffer, uint32_t width,
                      uint32_t height,
                      const EffectGeometryProviderPlan& providerPlan,
                      const PicaAttachmentRequirements& attachments);
    void ResetTemporalState();
    void Shutdown();

    [[nodiscard]] uint32_t LastBladeCount() const;
    [[nodiscard]] uint32_t LastMotionBladeCount() const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d

#endif
