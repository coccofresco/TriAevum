#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/grass_visibility.h"
#include "fast/oot3d/grass_world_placement_cache.h"
#include "fast/renderer/shaderc_compiler.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Fast::Oot3d {

inline constexpr uint32_t kGrassGpuSelectionMaximumSources = 16U;
inline constexpr uint32_t kGrassGpuSelectionMaximumSlots = 7U;
inline constexpr uint32_t kGrassGpuSelectionBinCount = 25U;
// VkDrawIndexedIndirectCommand of source s, slot k in the control buffer.
[[nodiscard]] constexpr VkDeviceSize GrassGpuSelectionDrawOffset(uint32_t source, uint32_t slot) noexcept {
    return (4U + (source * 8U + slot) * 5U) * sizeof(uint32_t);
}
inline constexpr VkDeviceSize kGrassGpuSelectionCountOffset = 3U * sizeof(uint32_t);

struct GrassGpuSelectionSource {
    std::span<const GrassWorldCluster> Clusters;
    uint32_t StaticBase = 0U;
    std::array<float, 16> PositionToClip{};
    std::array<float, 3> Eye{};
};

// One topology slot: a CPU selection bin that the policy can produce, with
// its index range. Slots are in bin order, so the output keeps the CPU's
// per-source bin concatenation.
struct GrassGpuSelectionSlot {
    uint32_t Bin = 0U;
    uint32_t IndexCount = 0U;
    uint32_t FirstIndex = 0U;
};

struct GrassGpuSelectionRequest {
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
    uint32_t FrameSlot = 0U;
    // Identity of the active source set; clusters upload when it changes.
    uint64_t StaticRevision = 0U;
    VkBuffer StaticAnchors = VK_NULL_HANDLE;
    size_t StaticAnchorCount = 0U;
    std::span<const GrassGpuSelectionSource> Sources;
    std::span<const GrassGpuSelectionSlot> Slots;
    GrassLodPolicy Policy{};
    float DrawDistanceSquared = 0.0F;
    float BladeRadiusScale = 1.0F;
    bool FrustumCulling = true;
    uint32_t Budget = 0U;
    bool ReadBackVisible = false;
};

struct GrassGpuSelectionResult {
    VkBuffer Visible = VK_NULL_HANDLE;
    VkBuffer Control = VK_NULL_HANDLE;
    uint32_t Capacity = 0U;
};

// What the GPU selected the last time this frame slot was recorded (read when
// the slot comes back, so it is one frame-slot cycle old).
struct GrassGpuSelectionReadback {
    bool Available = false;
    uint32_t Admitted = 0U;
    std::array<std::array<uint32_t, kGrassGpuSelectionMaximumSlots>, kGrassGpuSelectionMaximumSources> Counts{};
    std::vector<uint32_t> Visible;
};

// Fine grass selection on the GPU: a flat pass over every cluster of every
// active source, a single-workgroup scan with the global budget, and a
// stable scatter into the CPU's layout, plus indirect draw and dispatch
// arguments. Decisions follow the CPU selection; float rounding may differ
// at exact thresholds.
class GrassGpuSelector {
  public:
    GrassGpuSelector();
    ~GrassGpuSelector();
    GrassGpuSelector(const GrassGpuSelector&) = delete;
    GrassGpuSelector& operator=(const GrassGpuSelector&) = delete;

    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device, Renderer::CachedPassShaderCompiler& shaders,
                    VkPipelineCache pipelineCache);
    // Records the three passes; the caller has made StaticAnchors readable by
    // compute shaders. Ends with the outputs readable by compute, vertex and
    // indirect-command stages.
    bool Record(const GrassGpuSelectionRequest& request, GrassGpuSelectionResult& result);
    // Reads the frame slot's previous results; call before Record reuses it.
    [[nodiscard]] GrassGpuSelectionReadback ReadBack(uint32_t frameSlot) const;
    void Shutdown();
    [[nodiscard]] bool Available() const noexcept;
    [[nodiscard]] const std::string& UnavailableReason() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d

#endif
