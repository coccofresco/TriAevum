#include "fast/backends/oot3d_vulkan_gpu_profiler.h"

#ifdef ENABLE_OOT3D_VULKAN

#include <array>
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
        case Oot3d::GpuProfileScope::Cacao:
            timings.CacaoMilliseconds = milliseconds;
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
        case Oot3d::GpuProfileScope::Count:
            break;
    }
}

} // namespace

void Oot3dVulkanGpuProfiler::Initialize(
    VkPhysicalDevice physicalDevice, VkDevice device,
    uint32_t queueFamily, bool enabled) {
    Shutdown();
    if (!enabled || physicalDevice == VK_NULL_HANDLE ||
        device == VK_NULL_HANDLE) {
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
}

void Oot3dVulkanGpuProfiler::Shutdown() {
    if (mDevice != VK_NULL_HANDLE && mPool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(mDevice, mPool, nullptr);
    }
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
    }

    frame = {};
    vkCmdResetQueryPool(
        commandBuffer, mPool,
        slot * Oot3d::kGpuProfileQueriesPerFrame,
        Oot3d::kGpuProfileQueriesPerFrame);
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
