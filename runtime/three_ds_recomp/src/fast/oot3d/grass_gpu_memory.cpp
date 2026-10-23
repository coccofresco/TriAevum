#include "fast/oot3d/grass_gpu_memory.h"

#ifdef ENABLE_OOT3D_VULKAN
#include "fast/renderer3ds/vulkan_memory_budget.h"
#endif

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace Fast::Oot3d {
namespace {

std::atomic<int64_t> gDeviceLocal{0};
std::atomic<int64_t> gHostVisible{0};

uint64_t RefusalThreshold() {
    static const uint64_t threshold = [] {
        const char* value = std::getenv("TRIAEVUM_GRASS_FAIL_ALLOCATIONS_ABOVE_MIB");
        return value == nullptr || *value == '\0' ? UINT64_MAX : std::strtoull(value, nullptr, 10) << 20U;
    }();
    return threshold;
}

} // namespace

void TrackGrassGpuMemory(bool deviceLocal, int64_t bytes, const char* owner, uint32_t usage) noexcept {
    (deviceLocal ? gDeviceLocal : gHostVisible).fetch_add(bytes, std::memory_order_relaxed);
    static const bool log = std::getenv("TRIAEVUM_GRASS_GPU_MEMORY_LOG") != nullptr;
    if (log && bytes > 0)
        std::fprintf(stderr, "TRIAEVUM_GRASS_GPU_ALLOC owner=%s device_local=%d usage=%08x bytes=%lld\n",
                     owner != nullptr ? owner : "unknown", deviceLocal ? 1 : 0, usage,
                     static_cast<long long>(bytes));
}

GrassGpuMemoryUse GrassGpuMemoryInUse() noexcept {
    return {uint64_t(std::max<int64_t>(0, gDeviceLocal.load(std::memory_order_relaxed))),
            uint64_t(std::max<int64_t>(0, gHostVisible.load(std::memory_order_relaxed)))};
}

bool GrassGpuAllocationRefused(uint64_t bytes) noexcept {
    const uint64_t threshold = RefusalThreshold();
    if (threshold == UINT64_MAX) return false;
    const auto use = GrassGpuMemoryInUse();
    return use.DeviceLocal + use.HostVisible + bytes > threshold;
}

#ifdef ENABLE_OOT3D_VULKAN
namespace {

std::optional<uint64_t> ReserveOverride() {
    static const std::optional<uint64_t> reserve = []() -> std::optional<uint64_t> {
        const char* value = std::getenv("TRIAEVUM_GRASS_VRAM_RESERVE_MIB");
        if (value == nullptr || *value == '\0') return std::nullopt;
        return std::strtoull(value, nullptr, 10) << 20U;
    }();
    return reserve;
}

} // namespace

bool GrassGpuAllocationRefused(VkPhysicalDevice physicalDevice, uint32_t memoryType, uint64_t bytes) noexcept {
    if (GrassGpuAllocationRefused(bytes)) return true;
    try {
        if (physicalDevice == VK_NULL_HANDLE) return false;
        VkPhysicalDeviceMemoryProperties memory{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memory);
        if (memoryType >= memory.memoryTypeCount) return false;
        const uint32_t heap = memory.memoryTypes[memoryType].heapIndex;
        const auto heapMemory = Renderer3ds::QueryVulkanHeapMemory(physicalDevice, heap);
        if (!heapMemory) return false;
        const uint64_t heapBudget = heapMemory->Budget;
        const uint64_t reserve = ReserveOverride().value_or(heapBudget / 16U);
        const uint64_t usage = heapMemory->Usage;
        static const bool log = std::getenv("TRIAEVUM_GRASS_GPU_MEMORY_LOG") != nullptr;
        if (log)
            std::fprintf(stderr, "TRIAEVUM_GRASS_VRAM_BUDGET heap=%u usage_mib=%llu budget_mib=%llu\n", heap,
                         static_cast<unsigned long long>(usage >> 20U),
                         static_cast<unsigned long long>(heapBudget >> 20U));
        if (usage + bytes + reserve <= heapBudget) return false;
        std::fprintf(stderr, "[grass] VRAM budget: heap %u uses %llu MiB of %llu MiB (reserve %llu MiB); "
                     "refusing a %llu MiB grass buffer\n", heap, static_cast<unsigned long long>(usage >> 20U),
                     static_cast<unsigned long long>(heapBudget >> 20U),
                     static_cast<unsigned long long>(reserve >> 20U),
                     static_cast<unsigned long long>((bytes + (1U << 20U) - 1U) >> 20U));
        return true;
    } catch (...) {
        return false;
    }
}
#endif

} // namespace Fast::Oot3d
