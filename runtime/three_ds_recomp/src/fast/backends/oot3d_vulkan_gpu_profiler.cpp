#include "fast/backends/oot3d_vulkan_gpu_profiler.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/grass_stage_profile.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace Fast {

namespace {

void StoreTiming(Oot3dVulkanGpuTimings& timings,
                 Oot3d::GpuProfileScope scope, double milliseconds) {
    switch (scope) {
        case Oot3d::GpuProfileScope::Frame:
            timings.FrameMilliseconds = milliseconds;
            break;
        case Oot3d::GpuProfileScope::NativePica:
            timings.NativePicaMilliseconds = milliseconds;
            break;
        case Oot3d::GpuProfileScope::ToonRaster:
            timings.ToonRasterMilliseconds = milliseconds;
            break;
        case Oot3d::GpuProfileScope::Grass:
            timings.GrassMilliseconds = milliseconds;
            break;
        case Oot3d::GpuProfileScope::AmbientOcclusion:
            timings.AmbientOcclusionMilliseconds = milliseconds;
            break;
        case Oot3d::GpuProfileScope::DepthPreparation:
            timings.DepthPreparationMilliseconds = milliseconds;
            break;
        case Oot3d::GpuProfileScope::Reflection:
            timings.ReflectionMilliseconds = milliseconds;
            break;
        case Oot3d::GpuProfileScope::MotionVectors:
            timings.MotionVectorsMilliseconds = milliseconds;
            break;
        case Oot3d::GpuProfileScope::SceneComposite:
            timings.SceneCompositeMilliseconds = milliseconds;
            break;
        case Oot3d::GpuProfileScope::AntiAliasing:
            timings.AntiAliasingMilliseconds = milliseconds;
            break;
        case Oot3d::GpuProfileScope::Upscaler:
            timings.UpscalerMilliseconds = milliseconds;
            break;
        case Oot3d::GpuProfileScope::DisplayTransfer:
            timings.DisplayTransferMilliseconds = milliseconds;
            break;
        case Oot3d::GpuProfileScope::Scanout:
            timings.ScanoutMilliseconds = milliseconds;
            break;
        case Oot3d::GpuProfileScope::Overlay:
            timings.OverlayMilliseconds = milliseconds;
            break;
        case Oot3d::GpuProfileScope::GlobalIllumination:
            timings.GlobalIlluminationMilliseconds = milliseconds;
            break;
        case Oot3d::GpuProfileScope::GrassCompaction:
            timings.GrassCompactionMilliseconds = milliseconds;
            break;
        case Oot3d::GpuProfileScope::GrassBlades:
            timings.GrassBladesMilliseconds = milliseconds;
            break;
        case Oot3d::GpuProfileScope::GrassShadows:
            timings.GrassShadowsMilliseconds = milliseconds;
            break;
        case Oot3d::GpuProfileScope::GrassLighting:
            timings.GrassLightingMilliseconds = milliseconds;
            break;
        case Oot3d::GpuProfileScope::Count:
            break;
    }
}

constexpr std::array<const char*, Oot3d::kGpuProfileScopeCount> kScopeBeginLabels{ "frame",
                                                                                   "native_pica",
                                                                                   "toon_raster",
                                                                                   "grass",
                                                                                   "ambient_occlusion",
                                                                                   "depth_preparation",
                                                                                   "reflection",
                                                                                   "motion_vectors",
                                                                                   "scene_composite",
                                                                                   "anti_aliasing",
                                                                                   "upscaler",
                                                                                   "display_transfer",
                                                                                   "scanout",
                                                                                   "overlay",
                                                                                   "global_illumination",
                                                                                   "grass_compaction",
                                                                                   "grass_blades",
                                                                                   "grass_shadows",
                                                                                   "grass_lighting" };
constexpr std::array<const char*, Oot3d::kGpuProfileScopeCount> kScopeEndLabels{ "after frame",
                                                                                 "after native_pica",
                                                                                 "after toon_raster",
                                                                                 "after grass",
                                                                                 "after ambient_occlusion",
                                                                                 "after depth_preparation",
                                                                                 "after reflection",
                                                                                 "after motion_vectors",
                                                                                 "after scene_composite",
                                                                                 "after anti_aliasing",
                                                                                 "after upscaler",
                                                                                 "after display_transfer",
                                                                                 "after scanout",
                                                                                 "after overlay",
                                                                                 "after global_illumination",
                                                                                 "after grass_compaction",
                                                                                 "after grass_blades",
                                                                                 "after grass_shadows",
                                                                                 "after grass_lighting" };

bool EnvironmentFlag(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && std::string_view(value) == "1";
}

} // namespace

void Oot3dVulkanGpuProfiler::Initialize(
    VkPhysicalDevice physicalDevice, VkDevice device,
    uint32_t queueFamily, bool enabled) {
    Shutdown();
    // Either developer trace enables the timestamps by itself.
    const bool timeline = EnvironmentFlag("TRIAEVUM_GPU_TIMELINE");
    if (!(enabled || timeline || EnvironmentFlag("TRIAEVUM_GPU_GAP_TRACE") || Oot3d::GrassStageProfileEnabled()) ||
        physicalDevice == VK_NULL_HANDLE || device == VK_NULL_HANDLE) {
        return;
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(
        physicalDevice, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(
        physicalDevice, &count, families.data());
    if (queueFamily >= families.size() ||
        families[queueFamily].timestampValidBits == 0U ||
        properties.limits.timestampPeriod <= 0.0F) {
        return;
    }

    VkQueryPoolCreateInfo info{
        VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount =
        kFramesInFlight * Oot3d::kGpuProfileQueriesPerFrame;
    if (vkCreateQueryPool(
            device, &info, nullptr, &mPool) != VK_SUCCESS) {
        mPool = VK_NULL_HANDLE;
        return;
    }
    mDevice = device;
    mNanosecondsPerTick = properties.limits.timestampPeriod;
    const char* gapTrace = std::getenv("TRIAEVUM_GPU_GAP_TRACE");
    mGapTrace = gapTrace != nullptr && std::string_view(gapTrace) == "1";
    if (timeline) {
        VkQueryPoolCreateInfo timelineInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        timelineInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        timelineInfo.queryCount = kFramesInFlight * kTimelineMarksPerFrame;
        if (vkCreateQueryPool(device, &timelineInfo, nullptr, &mTimelinePool) != VK_SUCCESS)
            mTimelinePool = VK_NULL_HANDLE;
    }
}

void Oot3dVulkanGpuProfiler::Mark(
    uint32_t slot, const char* label, VkCommandBuffer commandBuffer) {
    WriteMark(slot, {label, false}, commandBuffer);
}

void Oot3dVulkanGpuProfiler::MarkCompleted(
    uint32_t slot, const char* label, VkCommandBuffer commandBuffer) {
    WriteMark(slot, {label, true}, commandBuffer);
}

void Oot3dVulkanGpuProfiler::MarkNamed(
    uint32_t slot, const std::string& label, VkCommandBuffer commandBuffer) {
    if (mTimelinePool == VK_NULL_HANDLE) {
        return;
    }
    WriteMark(slot, {mTimelineNames.insert(label).first->c_str(), false}, commandBuffer);
}

void Oot3dVulkanGpuProfiler::WriteMark(
    uint32_t slot, TimelineMark mark, VkCommandBuffer commandBuffer) {
    if (mTimelinePool == VK_NULL_HANDLE || slot >= kFramesInFlight ||
        commandBuffer == VK_NULL_HANDLE || mark.Label == nullptr) {
        return;
    }
    auto& labels = mFrames[slot].TimelineLabels;
    if (labels.size() >= kTimelineMarksPerFrame) {
        return;
    }
    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, mTimelinePool,
                        slot * kTimelineMarksPerFrame + static_cast<uint32_t>(labels.size()));
    labels.push_back(mark);
}

void Oot3dVulkanGpuProfiler::ReadTimeline(uint32_t slot) {
    const auto& labels = mFrames[slot].TimelineLabels;
    if (labels.size() < 2U) {
        return;
    }
    if (labels.size() >= kTimelineMarksPerFrame) {
        ++mTimelineOverflowFrames;
    }
    std::vector<uint64_t> ticks(labels.size());
    if (vkGetQueryPoolResults(mDevice, mTimelinePool, slot * kTimelineMarksPerFrame,
                              static_cast<uint32_t>(ticks.size()),
                              ticks.size() * sizeof(uint64_t), ticks.data(), sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) {
        return;
    }
    const double milliseconds = mNanosecondsPerTick / 1'000'000.0;
    for (size_t index = 0; index + 1U < ticks.size(); ++index) {
        if (ticks[index + 1U] < ticks[index]) {
            continue;
        }
        auto& total = mTimelineTotals[labels[index + 1U].OwnsPrevious
                                          ? labels[index + 1U].Label : labels[index].Label];
        total.Milliseconds += static_cast<double>(ticks[index + 1U] - ticks[index]) * milliseconds;
        ++total.Segments;
    }
    if (++mTimelineFrames == 300U) {
        ReportTimeline();
    }
}

void Oot3dVulkanGpuProfiler::ReportTimeline() {
    if (mTimelineFrames == 0U) {
        return;
    }
    std::vector<std::pair<double, const std::string*>> rows;
    double sum = 0.0;
    for (const auto& [label, total] : mTimelineTotals) {
        rows.emplace_back(total.Milliseconds / mTimelineFrames, &label);
        sum += total.Milliseconds / mTimelineFrames;
    }
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::fprintf(stderr, "[gpu-timeline] frames=%u total_ms=%.3f overflow_frames=%u\n",
                 mTimelineFrames, sum, mTimelineOverflowFrames);
    for (const auto& [ms, label] : rows) {
        const auto& total = mTimelineTotals[*label];
        std::fprintf(stderr, "[gpu-timeline] %8.3f ms  x%.1f  %s\n", ms,
                     static_cast<double>(total.Segments) / mTimelineFrames, label->c_str());
    }
    mTimelineTotals.clear();
    mTimelineFrames = 0U;
    mTimelineOverflowFrames = 0U;
}

void Oot3dVulkanGpuProfiler::TraceFrameGap(uint32_t slot) {
    std::array<uint64_t, 2> ticks{};
    if (vkGetQueryPoolResults(mDevice, mPool, Query(slot, Oot3d::GpuProfileScope::Frame, false), 2U,
                              sizeof(ticks), ticks.data(), sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT) != VK_SUCCESS || ticks[1] < ticks[0])
        return;
    const double milliseconds = mNanosecondsPerTick / 1'000'000.0;
    if (mGapTraceLastEnd != 0U && ticks[0] >= mGapTraceLastEnd) {
        const double busy = static_cast<double>(ticks[1] - ticks[0]) * milliseconds;
        mGapTraceBusy += busy;
        mGapTraceIdle += static_cast<double>(ticks[0] - mGapTraceLastEnd) * milliseconds;
        mGapTraceMaxBusy = std::max(mGapTraceMaxBusy, busy);
        mGapTraceOver60Hz += busy > 1000.0 / 60.0 ? 1U : 0U;
        if (++mGapTraceFrames == 60U) {
            std::fprintf(stderr, "[gpu-gap] frames=60 busy_ms=%.3f idle_ms=%.3f max_busy_ms=%.3f over_16_7=%u\n",
                         mGapTraceBusy / 60.0, mGapTraceIdle / 60.0, mGapTraceMaxBusy, mGapTraceOver60Hz);
            mGapTraceMaxBusy = 0.0;
            mGapTraceOver60Hz = 0U;
            mGapTraceBusy = mGapTraceIdle = 0.0;
            mGapTraceFrames = 0U;
        }
    }
    mGapTraceLastEnd = ticks[1];
}

void Oot3dVulkanGpuProfiler::Shutdown() {
    if (mDevice != VK_NULL_HANDLE && mPool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(mDevice, mPool, nullptr);
    }
    if (mDevice != VK_NULL_HANDLE && mTimelinePool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(mDevice, mTimelinePool, nullptr);
    }
    mTimelinePool = VK_NULL_HANDLE;
    mTimelineTotals.clear();
    mTimelineFrames = 0U;
    mDevice = VK_NULL_HANDLE;
    mPool = VK_NULL_HANDLE;
    mNanosecondsPerTick = 0.0;
    mFrames = {};
}

uint32_t Oot3dVulkanGpuProfiler::Query(
    uint32_t slot, Oot3d::GpuProfileScope scope, bool end) const {
    const auto range = Oot3d::GpuProfileQueries(scope);
    return slot * Oot3d::kGpuProfileQueriesPerFrame +
           (end ? range.End : range.Begin);
}

std::optional<double> Oot3dVulkanGpuProfiler::ReadMilliseconds(
    uint32_t slot, Oot3d::GpuProfileScope scope) const {
    std::array<uint64_t, 2> ticks{};
    const VkResult result = vkGetQueryPoolResults(
        mDevice, mPool, Query(slot, scope, false),
        static_cast<uint32_t>(ticks.size()), sizeof(ticks),
        ticks.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
    if (result != VK_SUCCESS || ticks[1] < ticks[0]) {
        return std::nullopt;
    }
    constexpr double kNanosecondsPerMillisecond = 1'000'000.0;
    return static_cast<double>(ticks[1] - ticks[0]) *
           mNanosecondsPerTick / kNanosecondsPerMillisecond;
}

std::optional<Oot3dVulkanGpuTimings>
Oot3dVulkanGpuProfiler::BeginFrame(
    uint32_t slot, uint64_t, VkCommandBuffer commandBuffer) {
    if (mPool == VK_NULL_HANDLE || slot >= kFramesInFlight ||
        commandBuffer == VK_NULL_HANDLE) {
        return std::nullopt;
    }

    auto& frame = mFrames[slot];
    std::optional<Oot3dVulkanGpuTimings> result;
    if (frame.Pending) {
        Oot3dVulkanGpuTimings timings;
        bool anyTiming = false;
        for (size_t index = 0;
             index < Oot3d::kGpuProfileScopeCount; ++index) {
            const auto scope =
                static_cast<Oot3d::GpuProfileScope>(index);
            if (!frame.Plan.Written(scope)) {
                continue;
            }
            const auto milliseconds =
                ReadMilliseconds(slot, scope);
            if (milliseconds.has_value()) {
                StoreTiming(timings, scope, *milliseconds);
                anyTiming = true;
            }
        }
        if (anyTiming) {
            result = timings;
        }
        if (mGapTrace && frame.Plan.Written(Oot3d::GpuProfileScope::Frame)) TraceFrameGap(slot);
        if (mTimelinePool != VK_NULL_HANDLE) ReadTimeline(slot);
    }

    frame = {};
    vkCmdResetQueryPool(
        commandBuffer, mPool,
        slot * Oot3d::kGpuProfileQueriesPerFrame,
        Oot3d::kGpuProfileQueriesPerFrame);
    if (mTimelinePool != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(commandBuffer, mTimelinePool, slot * kTimelineMarksPerFrame,
                            kTimelineMarksPerFrame);
    }
    BeginScope(
        slot, Oot3d::GpuProfileScope::Frame, commandBuffer);
    return result;
}

void Oot3dVulkanGpuProfiler::EndFrame(
    uint32_t slot, VkCommandBuffer commandBuffer) {
    if (mPool == VK_NULL_HANDLE || slot >= kFramesInFlight ||
        commandBuffer == VK_NULL_HANDLE) {
        return;
    }
    auto& frame = mFrames[slot];
    for (size_t index = 1;
         index < Oot3d::kGpuProfileScopeCount; ++index) {
        const auto scope =
            static_cast<Oot3d::GpuProfileScope>(index);
        if (frame.Plan.Open(scope)) {
            EndScope(slot, scope, commandBuffer);
        }
    }
    EndScope(slot, Oot3d::GpuProfileScope::Frame, commandBuffer);
    frame.Pending =
        frame.Plan.Written(Oot3d::GpuProfileScope::Frame);
}

void Oot3dVulkanGpuProfiler::BeginScope(
    uint32_t slot, Oot3d::GpuProfileScope scope,
    VkCommandBuffer commandBuffer) {
    if (static_cast<size_t>(scope) < Oot3d::kGpuProfileScopeCount) {
        Mark(slot, kScopeBeginLabels[static_cast<size_t>(scope)], commandBuffer);
    }
    if (mPool == VK_NULL_HANDLE || slot >= kFramesInFlight ||
        commandBuffer == VK_NULL_HANDLE ||
        !mFrames[slot].Plan.Begin(scope)) {
        return;
    }
    vkCmdWriteTimestamp(
        commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        mPool, Query(slot, scope, false));
}

void Oot3dVulkanGpuProfiler::EndScope(
    uint32_t slot, Oot3d::GpuProfileScope scope,
    VkCommandBuffer commandBuffer) {
    if (static_cast<size_t>(scope) < Oot3d::kGpuProfileScopeCount) {
        Mark(slot, kScopeEndLabels[static_cast<size_t>(scope)], commandBuffer);
    }
    if (mPool == VK_NULL_HANDLE || slot >= kFramesInFlight ||
        commandBuffer == VK_NULL_HANDLE ||
        !mFrames[slot].Plan.End(scope)) {
        return;
    }
    vkCmdWriteTimestamp(
        commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        mPool, Query(slot, scope, true));
}

} // namespace Fast

#endif
