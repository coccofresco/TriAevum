#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/backends/oot3d_vulkan_diagnostics.h"
#include "fast/extension/gpu_profile_plan.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <optional>
#include <string>
#include <vector>

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

    void BeginScope(uint32_t slot, Extension::GpuProfileScope scope,
                    VkCommandBuffer commandBuffer);
    void EndScope(uint32_t slot, Extension::GpuProfileScope scope,
                  VkCommandBuffer commandBuffer);

    // Developer timeline (TRIAEVUM_GPU_TIMELINE=1): a labelled timestamp;
    // the label owns the GPU time until the frame's next mark. `label` must
    // be a string literal. Inert unless the timeline is enabled.
    void Mark(uint32_t slot, const char* label, VkCommandBuffer commandBuffer);
    // Like Mark, but the label owns the GPU time since the previous mark.
    void MarkCompleted(uint32_t slot, const char* label, VkCommandBuffer commandBuffer);
    // A label built at run time (interned); costs a lookup, so it is meant
    // for coarse boundaries such as render-pass begins.
    void MarkNamed(uint32_t slot, const std::string& label, VkCommandBuffer commandBuffer);
    [[nodiscard]] bool TimelineEnabled() const noexcept { return mTimelinePool != VK_NULL_HANDLE; }

  private:
    static constexpr uint32_t kTimelineMarksPerFrame = 1024;

    struct TimelineMark {
        const char* Label = nullptr;
        bool OwnsPrevious = false;
    };

    struct FrameState {
        Extension::GpuProfileFramePlan Plan;
        bool Pending = false;
        std::vector<TimelineMark> TimelineLabels;
    };

    void WriteMark(uint32_t slot, TimelineMark mark, VkCommandBuffer commandBuffer);

    struct TimelineTotal {
        double Milliseconds = 0.0;
        uint64_t Segments = 0;
    };

    void ReadTimeline(uint32_t slot);
    void ReportTimeline();

    [[nodiscard]] uint32_t Query(
        uint32_t slot, Extension::GpuProfileScope scope,
        bool end) const;
    [[nodiscard]] std::optional<double> ReadMilliseconds(
        uint32_t slot, Extension::GpuProfileScope scope) const;

    // Developer trace (TRIAEVUM_GPU_GAP_TRACE=1): the GPU's busy time per
    // frame and its idle time between consecutive frames.
    void TraceFrameGap(uint32_t slot);

    VkDevice mDevice = VK_NULL_HANDLE;
    VkQueryPool mPool = VK_NULL_HANDLE;
    double mNanosecondsPerTick = 0.0;
    std::array<FrameState, kFramesInFlight> mFrames{};
    bool mGapTrace = false;
    uint64_t mGapTraceLastEnd = 0;
    double mGapTraceBusy = 0.0;
    double mGapTraceIdle = 0.0;
    uint32_t mGapTraceFrames = 0;
    double mGapTraceMaxBusy = 0.0;
    uint32_t mGapTraceOver60Hz = 0;
    VkQueryPool mTimelinePool = VK_NULL_HANDLE;
    std::map<std::string, TimelineTotal> mTimelineTotals;
    std::set<std::string> mTimelineNames;
    uint32_t mTimelineFrames = 0;
    uint32_t mTimelineOverflowFrames = 0;
};

} // namespace Fast

#endif
