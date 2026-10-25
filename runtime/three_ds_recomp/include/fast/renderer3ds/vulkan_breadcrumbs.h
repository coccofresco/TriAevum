#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace Fast::Renderer3ds {

// Opt-in GPU breadcrumbs (TRIAEVUM_VULKAN_BREADCRUMBS=1) for device loss:
// each recorded pass boundary writes a monotonic marker id, once when the
// preceding work has started (top of pipe) and once when it has completed
// (bottom of pipe). After VK_ERROR_DEVICE_LOST the last started and completed
// ids name the passes the GPU was executing. VK_AMD_buffer_marker writes them
// to a host-visible buffer (also exposed by other vendors);
// VK_NV_device_diagnostic_checkpoints is the fallback. Unrequested, neither
// extension is enabled and no command is recorded.
enum class VulkanBreadcrumbMechanism : uint8_t {
    Off,
    BufferMarker,
    Checkpoints,
};

[[nodiscard]] const char* VulkanBreadcrumbMechanismName(VulkanBreadcrumbMechanism mechanism) noexcept;
[[nodiscard]] bool VulkanBreadcrumbsRequested(const char* environment) noexcept;
[[nodiscard]] VulkanBreadcrumbMechanism ResolveVulkanBreadcrumbMechanism(
    bool requested, bool bufferMarkerAvailable, bool checkpointsAvailable) noexcept;
// The device extension the mechanism needs, or nullptr for Off.
[[nodiscard]] const char* VulkanBreadcrumbExtension(VulkanBreadcrumbMechanism mechanism) noexcept;

// Host-side names of the most recent marker ids. Labels are not copied: they
// must outlive the history (string literals or interned names).
class VulkanBreadcrumbHistory {
  public:
    static constexpr uint32_t kCapacity = 4096;

    struct Entry {
        uint32_t Id = 0;
        uint64_t Frame = 0;
        const char* Label = nullptr;
    };

    // Returns the new id; ids start at 1, so 0 means "none written".
    uint32_t Push(const char* label, uint64_t frame) noexcept;
    [[nodiscard]] std::optional<Entry> Find(uint32_t id) const noexcept;
    [[nodiscard]] uint32_t LastId() const noexcept { return mLastId; }

  private:
    std::array<Entry, kCapacity> mEntries{};
    uint32_t mLastId = 0;
};

// TRIAEVUM_VULKAN_BREADCRUMBS mechanism=... recorded=N started=S completed=C
//   last_completed={id,frame,pass} in_flight=[{id,frame,pass},...]
// The faulting work follows the last completed marker and precedes the
// marker after the last started one; in_flight lists those (bounded).
[[nodiscard]] std::string FormatVulkanBreadcrumbLine(
    VulkanBreadcrumbMechanism mechanism, const VulkanBreadcrumbHistory& history,
    uint32_t started, uint32_t completed);

class VulkanBreadcrumbTrail {
  public:
    // False (and inactive) when the mechanism is Off or its resources or
    // commands are unavailable. `queue` is the queue whose checkpoints are
    // read after device loss.
    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device, VkQueue queue,
                    VulkanBreadcrumbMechanism mechanism);
    void Shutdown() noexcept;
    [[nodiscard]] bool Active() const noexcept { return mMechanism != VulkanBreadcrumbMechanism::Off; }
    [[nodiscard]] VulkanBreadcrumbMechanism Mechanism() const noexcept { return mMechanism; }

    void SetFrame(uint64_t frame) noexcept { mFrame = frame; }
    // Inert while inactive. `label` follows the history's lifetime rule.
    void Record(VkCommandBuffer commandBuffer, const char* label);
    // After device loss; nullopt while inactive.
    [[nodiscard]] std::optional<std::string> Describe();

  private:
    VulkanBreadcrumbMechanism mMechanism = VulkanBreadcrumbMechanism::Off;
    VkDevice mDevice = VK_NULL_HANDLE;
    VkQueue mQueue = VK_NULL_HANDLE;
    VkBuffer mBuffer = VK_NULL_HANDLE;
    VkDeviceMemory mMemory = VK_NULL_HANDLE;
    volatile uint32_t* mMarkers = nullptr;
    PFN_vkCmdWriteBufferMarkerAMD mWriteBufferMarker = nullptr;
    PFN_vkCmdSetCheckpointNV mSetCheckpoint = nullptr;
    PFN_vkGetQueueCheckpointDataNV mGetCheckpointData = nullptr;
    uint64_t mFrame = 0;
    std::mutex mMutex;
    VulkanBreadcrumbHistory mHistory;
};

} // namespace Fast::Renderer3ds
