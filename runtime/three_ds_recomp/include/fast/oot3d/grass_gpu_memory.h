#pragma once

#include <cstdint>
#include <stdexcept>

#ifdef ENABLE_OOT3D_VULKAN
#include <vulkan/vulkan.h>
#endif

namespace Fast::Oot3d {

// Live GPU memory of the grass passes, counted where their buffers are
// allocated and freed: device-local (VRAM) and host-visible.
struct GrassGpuMemoryUse {
    uint64_t DeviceLocal = 0;
    uint64_t HostVisible = 0;
};

// TRIAEVUM_GRASS_GPU_MEMORY_LOG=1 prints every allocation (owner, usage).
// A grass buffer the device would not allocate (or the fault injection
// refused): recoverable with less memory, unlike a functional failure.
class GrassGpuOutOfMemory final : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

void TrackGrassGpuMemory(bool deviceLocal, int64_t bytes, const char* owner = nullptr,
                         uint32_t usage = 0) noexcept;
[[nodiscard]] GrassGpuMemoryUse GrassGpuMemoryInUse() noexcept;

// Capacity policy of the grass buffers. Small buffers round up to a power of
// two (at least 4 KiB); large ones keep 1/8 headroom on a 1 MiB granule instead
// of doubling, which cost up to half their size again on the static anchors.
[[nodiscard]] constexpr uint64_t GrassBufferCapacity(uint64_t bytes) noexcept {
    constexpr uint64_t kSmall = 1ULL << 20U;
    if (bytes <= kSmall) {
        uint64_t capacity = 4096U;
        while (capacity < bytes) capacity <<= 1U;
        return capacity;
    }
    return (bytes + bytes / 8U + kSmall - 1U) / kSmall * kSmall;
}

// A buffer is rebuilt when it is missing, too small, or a large one holds
// over four times what is asked (a scene left behind keeps no peak buffers).
[[nodiscard]] constexpr bool GrassBufferNeedsResize(bool exists, uint64_t bytes, uint64_t capacity) noexcept {
    return !exists || bytes > capacity || (capacity > (16ULL << 20U) && bytes < capacity / 4U);
}

// Diagnostic fault injection: TRIAEVUM_GRASS_FAIL_ALLOCATIONS_ABOVE_MIB=<n>
// refuses an allocation that would take the grass passes beyond n MiB, as a
// full card would. Unset: never refuses.
[[nodiscard]] bool GrassGpuAllocationRefused(uint64_t bytes) noexcept;

#ifdef ENABLE_OOT3D_VULKAN
// The gate of every grass buffer: the fault injection above, then the
// process's memory budget for a device-local heap (VK_EXT_memory_budget). A
// buffer that would take the heap's usage past its budget less a reserve for
// the rest of the renderer (a sixteenth of the budget, or
// TRIAEVUM_GRASS_VRAM_RESERVE_MIB) is refused, so the grass scales down before
// the scene's own resources are evicted or fail. Without the extension only
// the driver refuses.
[[nodiscard]] bool GrassGpuAllocationRefused(VkPhysicalDevice physicalDevice, uint32_t memoryType,
                                             uint64_t bytes) noexcept;
#endif

} // namespace Fast::Oot3d
