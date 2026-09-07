#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/backends/oot3d_vulkan_diagnostics.h"
#include "fast/oot3d/gpu_profile_plan.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <optional>

namespace Fast {

// Owns timestamp resources; the renderer only announces lifecycle boundaries.
class Oot3dVulkanGpuProfiler {
  public:
    static constexpr uint32_t kFramesInFlight = 2;

    void Initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                    uint32_t queueFamily, bool enabled);
    void Shutdown();

    std::optional<Oot3dVulkanGpuTimings> BeginFrame(
        uint32_t slot, uint64_t frameIndex, VkCommandBuffer commandBuffer);
    void EndFrame(uint32_t slot, VkCommandBuffer);

    void BeginScope(uint32_t slot, Oot3d::GpuProfileScope scope,
                    VkCommandBuffer commandBuffer);
    void EndScope(uint32_t slot, Oot3d::GpuProfileScope scope,
                  VkCommandBuffer commandBuffer);

  private:
    struct FrameState {
        Oot3d::GpuProfileFramePlan Plan;
        bool Pending = false;
    };

    [[nodiscard]] uint32_t Query(
        uint32_t slot, Oot3d::GpuProfileScope scope,
        bool end) const;
    [[nodiscard]] std::optional<double> ReadMilliseconds(
        uint32_t slot, Oot3d::GpuProfileScope scope) const;

    VkDevice mDevice = VK_NULL_HANDLE;
    VkQueryPool mPool = VK_NULL_HANDLE;
    double mNanosecondsPerTick = 0.0;
    std::array<FrameState, kFramesInFlight> mFrames{};
};

} // namespace Fast

#endif
