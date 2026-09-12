#pragma once
#ifdef ENABLE_OOT3D_VULKAN
#include <vulkan/vulkan.h>
#include <cstdint>
#include <fstream>
#include <vector>

namespace Fast {

// Opt-in diagnostics. Consume only after the caller's existing slot fence;
// never wait for the GPU here or alter presentation scheduling.
class VulkanScanoutProbe {
  public:
    void Initialize(VkPhysicalDevice physical, VkDevice device, uint32_t slots);
    void Consume(uint32_t slot);
    void Record(VkCommandBuffer command, VkImage image, VkFormat format,
                VkExtent2D extent, uint32_t slot, uint32_t imageIndex,
                uint64_t frame, bool nativePresented);
    void Shutdown(); // Caller has already waited for device completion.
  private:
    struct Slot {
        VkBuffer Buffer = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        void* Mapped = nullptr;
        VkDeviceSize Capacity = 0;
        VkExtent2D Extent{};
        uint64_t Frame = 0;
        uint32_t ImageIndex = 0;
        bool NativePresented = false;
        bool Pending = false;
    };
    void Destroy(Slot& slot);
    VkDevice mDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties mMemoryProperties{};
    std::vector<Slot> mSlots;
    std::ofstream mOutput;
};
} // namespace Fast
#endif
