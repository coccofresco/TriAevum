#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/grass_interaction_field.h"
#include "fast/oot3d/grass_world_placement_cache.h"
#include "fast/renderer/shaderc_compiler.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace Fast::Oot3d {

struct GrassGpuInstanceCompactionRequest {
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
    uint32_t FrameSlot = 0U;
    uint64_t StaticRevision = 0U;
    std::span<const GrassWorldAnchor> StaticAnchors;
    std::span<const uint32_t> VisibleAnchorIndices;
    const GrassInteractionField* InteractionField = nullptr;
    std::span<const GrassInteractor> Actors;
    const InteractiveGrassSettings* Settings = nullptr;
};

struct GrassGpuInstanceCompactionResult {
    VkBuffer InstanceBuffer = VK_NULL_HANDLE;
    uint32_t InstanceCount = 0U;
    uint64_t DynamicUploadedBytes = 0U;
    uint64_t StaticUploadedBytes = 0U;
};

class GrassGpuInstanceCompactor final {
  public:
    GrassGpuInstanceCompactor();
    ~GrassGpuInstanceCompactor();
    GrassGpuInstanceCompactor(const GrassGpuInstanceCompactor&) = delete;
    GrassGpuInstanceCompactor& operator=(const GrassGpuInstanceCompactor&) = delete;

    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                    Renderer::CachedPassShaderCompiler& shaders);
    [[nodiscard]] bool Compact(const GrassGpuInstanceCompactionRequest& request,
                               GrassGpuInstanceCompactionResult& result);
    void Shutdown();

    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d

#endif
