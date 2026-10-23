#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/grass_gpu_selection.h"
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

// The static anchors of every active source, in source order, without
// concatenating them: global static index = segment base + local index.
struct GrassStaticAnchorSegments {
    std::span<const std::span<const GrassWorldAnchor>> Segments;
    size_t Count = 0U;
    [[nodiscard]] size_t size() const noexcept { return Count; }
    [[nodiscard]] bool empty() const noexcept { return Count == 0U; }
};

struct GrassGpuInstanceCompactionRequest {
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
    uint32_t FrameSlot = 0U;
    // Consecutive frame ids keep each blade's previous interaction bend.
    uint64_t FrameId = 0U;
    uint64_t StaticRevision = 0U;
    GrassStaticAnchorSegments StaticAnchors;
    std::span<const uint32_t> VisibleAnchorIndices;
    const GrassInteractionField* InteractionField = nullptr;
    std::span<const GrassInteractor> Actors;
    const InteractiveGrassSettings* Settings = nullptr;
    uint64_t SelectionRevision = 0;
    bool AllowIdleReuse = false;
    // GPU selection instead of VisibleAnchorIndices: recorded right after the
    // static anchors upload; the visible roots and their count stay on the
    // GPU and the compaction dispatch is indirect.
    GrassGpuSelector* Selector = nullptr;
    GrassGpuSelectionRequest Selection{};
};

// Words per root bend: the interaction bend and the bend the previous frame
// drew, each PackGrassBend (half2).
inline constexpr uint32_t kGrassRootBendWords = 2U;

// The blades read their roots in place: root i is static anchor
// VisibleIndicesBuffer[i] of StaticAnchorBuffer, bent by RootBendBuffer[i].
struct GrassGpuInstanceCompactionResult {
    VkBuffer StaticAnchorBuffer = VK_NULL_HANDLE;
    // kGrassRootBendWords per root: the interaction bend and the bend the
    // previous frame drew, or the current one where the blade has no previous
    // frame.
    VkBuffer RootBendBuffer = VK_NULL_HANDLE;
    uint32_t InstanceCount = 0U;
    uint64_t DynamicUploadedBytes = 0U;
    uint64_t StaticUploadedBytes = 0U;
    bool SelectionUploadReused = false;
    bool Reused = false;
    VkBuffer VisibleIndicesBuffer = VK_NULL_HANDLE;
    // GPU selection: draw arguments and counts; InstanceCount is the capacity.
    VkBuffer SelectionControl = VK_NULL_HANDLE;
    bool GpuSelection = false;
};

class GrassGpuInstanceCompactor final {
  public:
    GrassGpuInstanceCompactor();
    ~GrassGpuInstanceCompactor();
    GrassGpuInstanceCompactor(const GrassGpuInstanceCompactor&) = delete;
    GrassGpuInstanceCompactor& operator=(const GrassGpuInstanceCompactor&) = delete;

    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                    Renderer::CachedPassShaderCompiler& shaders,
                    VkPipelineCache pipelineCache = VK_NULL_HANDLE);
    [[nodiscard]] bool Compact(const GrassGpuInstanceCompactionRequest& request,
                               GrassGpuInstanceCompactionResult& result);
    // Forget every previous bend (savestates, scene transitions).
    void ResetHistory();
    // Frees every data buffer (after the device is idle) and keeps the
    // pipeline: the next compaction uploads and allocates again.
    void ReleaseMemory();
    void Shutdown();

    [[nodiscard]] const std::string& UnavailableReason() const;
    // The last failed compaction could not allocate a buffer (recoverable).
    [[nodiscard]] bool LastFailureOutOfMemory() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d

#endif
