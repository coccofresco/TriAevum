#pragma once

#include "fast/renderer3ds/pica_resolved_draw_stream.h"
#include "fast/renderer/shaderc_compiler.h"

#ifdef ENABLE_RENDERER3DS_VULKAN
#include <vulkan/vulkan.h>
#include <memory>

namespace Fast::Renderer3ds {

struct PicaSurfaceLightingRequest {
    uint64_t SubmissionId = 0;
    uint32_t VertexCount = 0;
    uint32_t AtlasBase = 0;
    bool Available = false;
    uint64_t RenderTargetNamespace = 0;
};

// Private producer within an authorized geometry-provider invocation. Replays
// only selected native vertex programs into point texels, never scene pixels.
class PicaSurfaceLightingPass final {
  public:
    PicaSurfaceLightingPass();
    ~PicaSurfaceLightingPass();
    bool Initialize(VkPhysicalDevice physical, VkDevice device, Renderer::CachedPassShaderCompiler& shaders);
    bool Prepare(VkCommandBuffer command, uint32_t frameSlot, uint64_t frameId,
        PicaResolvedDrawStreamView scene, std::span<PicaSurfaceLightingRequest> requests);
    VkImageView View(uint32_t frameSlot) const;
    VkSampler Sampler() const;
    void Shutdown();
  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
} // namespace Fast::Renderer3ds
#endif
