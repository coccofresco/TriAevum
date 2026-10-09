#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/pica_attachment_contract.h"
#include "fast/oot3d/world_debug_lines.h"
#include "fast/renderer/shaderc_compiler.h"

#include <vulkan/vulkan.h>

#include <array>
#include <memory>
#include <string>

namespace Fast::Oot3d {

// Draws WorldDebugLineRuntime's lines into a native PICA scene target, in its
// render pass, against its depth: screen-space quads of a constant width, the
// visible parts at full colour, the hidden ones dashed. Its pipelines are
// built the first time there are lines to draw.
class WorldDebugLinePass final {
  public:
    WorldDebugLinePass();
    ~WorldDebugLinePass();
    WorldDebugLinePass(const WorldDebugLinePass&) = delete;
    WorldDebugLinePass& operator=(const WorldDebugLinePass&) = delete;

    // The same render passes and formats as the other scene geometry passes.
    void Initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                    Renderer::CachedPassShaderCompiler& shaders,
                    VkRenderPass canonicalRenderPass,
                    VkRenderPass instrumentedRenderPass,
                    VkSampleCountFlagBits sampleCount,
                    bool dynamicRendering, VkFormat depthFormat,
                    VkPipelineCache pipelineCache = VK_NULL_HANDLE);
    // Records the lines inside the target's active render pass; false when
    // there was nothing to draw or the pass is unavailable.
    bool Draw(VkCommandBuffer commandBuffer, uint32_t frameSlot, uint64_t frameId,
              uint32_t width, uint32_t height,
              const WorldDebugLineSet& lines,
              const std::array<float, 16>& worldToClip,
              const std::array<float, 2>& jitterNdc,
              const PicaAttachmentRequirements& attachments);
    void Shutdown();

    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d

#endif
