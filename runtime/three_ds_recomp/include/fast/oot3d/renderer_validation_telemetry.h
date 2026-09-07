#pragma once

#include <atomic>
#include <cstdint>

namespace Fast::Oot3d {

enum class RendererValidationSource : uint8_t {
    Vulkan,
    Nri,
};

enum class RendererValidationSeverity : uint8_t {
    Info,
    Warning,
    Error,
};

struct RendererValidationSnapshot {
    bool VulkanEnabled = false;
    bool NriEnabled = false;
    uint64_t VulkanInfoCount = 0;
    uint64_t VulkanWarningCount = 0;
    uint64_t VulkanErrorCount = 0;
    uint64_t NriInfoCount = 0;
    uint64_t NriWarningCount = 0;
    uint64_t NriErrorCount = 0;
};

// Thread-safe cumulative counters shared by the Vulkan debug messenger and
// NRI's callback interface. It deliberately owns no API objects.
class RendererValidationTelemetry final {
  public:
    void SetEnabled(RendererValidationSource source, bool enabled);
    void Record(RendererValidationSource source,
                RendererValidationSeverity severity);
    void Reset();

    [[nodiscard]] RendererValidationSnapshot Snapshot() const;

  private:
    std::atomic_bool mVulkanEnabled = false;
    std::atomic_bool mNriEnabled = false;
    std::atomic_uint64_t mVulkanInfoCount = 0;
    std::atomic_uint64_t mVulkanWarningCount = 0;
    std::atomic_uint64_t mVulkanErrorCount = 0;
    std::atomic_uint64_t mNriInfoCount = 0;
    std::atomic_uint64_t mNriWarningCount = 0;
    std::atomic_uint64_t mNriErrorCount = 0;
};

} // namespace Fast::Oot3d
