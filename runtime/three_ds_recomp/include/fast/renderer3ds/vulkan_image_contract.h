#pragma once
#include <vulkan/vulkan.h>

namespace Fast::Renderer3ds {
// A combined-usage query is required: independent format bits alone do not
// prove that this extent/sample count/usage combination can be allocated.
inline bool VulkanImage2DWithinLimits(const VkImageFormatProperties& limits,
    uint32_t width, uint32_t height, uint32_t mips, uint32_t layers, VkSampleCountFlagBits samples) {
    return width && height && mips && layers &&
        width <= limits.maxExtent.width && height <= limits.maxExtent.height &&
        mips <= limits.maxMipLevels && layers <= limits.maxArrayLayers &&
        (limits.sampleCounts & samples) == samples;
}
inline bool VulkanImage2DSupported(VkPhysicalDevice physical, VkFormat format,
    VkImageUsageFlags usage, uint32_t width, uint32_t height, uint32_t mips,
    uint32_t layers, VkSampleCountFlagBits samples, bool cube = false) {
    VkImageFormatProperties limits{};
    return vkGetPhysicalDeviceImageFormatProperties(physical, format, VK_IMAGE_TYPE_2D,
        VK_IMAGE_TILING_OPTIMAL, usage, cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0,
        &limits) == VK_SUCCESS &&
        VulkanImage2DWithinLimits(limits, width, height, mips, layers, samples);
}
} // namespace Fast::Renderer3ds
