#pragma once

#include <cstdint>

namespace Fast::Renderer3ds {

// When the renderer's content caches give back device memory. A cached native
// texture or lighting LUT keyed by its guest content is released once no draw
// has sampled it for IdleFrames presentations; while the device-local heap is
// short of its budget, after PressureIdleFrames. A released entry is recreated
// from guest memory on its next use, so this changes residency, never content.
struct GpuMemoryResidencyPolicy {
    uint64_t IdleFrames = 600;
    uint64_t PressureIdleFrames = 30;
    // How often the budget is read and the caches are swept.
    uint64_t SweepIntervalFrames = 30;
};

// The heap is short once its usage passes seven eighths of the budget the
// driver grants this process (VK_EXT_memory_budget). No budget: no pressure.
[[nodiscard]] constexpr bool DeviceLocalMemoryUnderPressure(uint64_t usage, uint64_t budget) noexcept {
    return budget != 0U && usage > budget - budget / 8U;
}

[[nodiscard]] constexpr bool GpuResourceIdle(uint64_t frame, uint64_t lastUsedFrame, bool pressure,
                                             const GpuMemoryResidencyPolicy& policy = {}) noexcept {
    const uint64_t idle = pressure ? policy.PressureIdleFrames : policy.IdleFrames;
    return frame > lastUsedFrame && frame - lastUsedFrame > idle;
}

} // namespace Fast::Renderer3ds
