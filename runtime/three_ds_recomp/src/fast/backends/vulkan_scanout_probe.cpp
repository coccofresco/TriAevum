#include "fast/backends/vulkan_scanout_probe.h"
#ifdef ENABLE_OOT3D_VULKAN
#include <cstdlib>
#include <stdexcept>

namespace Fast {
namespace {
void Check(VkResult result) {
    if (result != VK_SUCCESS) throw std::runtime_error("Vulkan scanout probe allocation failed");
}
}

void VulkanScanoutProbe::Initialize(VkPhysicalDevice physical, VkDevice device, uint32_t slots) {
    const char* path = std::getenv("TRIAEVUM_VULKAN_SCANOUT_PROBE");
    if (path == nullptr || *path == '\0') return;
    mOutput.open(path);
    if (!mOutput) throw std::runtime_error("Cannot open Vulkan scanout probe output");
    mOutput << "frame,image,width,height,native_presented,samples,black,mean_rgb,hash\n";
    mDevice = device;
    vkGetPhysicalDeviceMemoryProperties(physical, &mMemoryProperties);
    mSlots.resize(slots);
}

void VulkanScanoutProbe::Consume(uint32_t index) {
    if (index >= mSlots.size()) return;
    auto& slot = mSlots[index];
    if (!slot.Pending) return;
    const auto* bytes = static_cast<const uint8_t*>(slot.Mapped);
    uint64_t samples = 0, black = 0, sum = 0, hash = 14695981039346656037ULL;
    for (uint32_t y = 0; y < slot.Extent.height; y += 8) {
        for (uint32_t x = 0; x < slot.Extent.width; x += 8) {
            const auto* pixel = bytes + (static_cast<size_t>(y) * slot.Extent.width + x) * 4;
            ++samples;
            black += pixel[0] <= 4 && pixel[1] <= 4 && pixel[2] <= 4;
            for (uint32_t component = 0; component < 3; ++component) {
                sum += pixel[component];
                hash = (hash ^ pixel[component]) * 1099511628211ULL;
            }
        }
    }
    mOutput << slot.Frame << ',' << slot.ImageIndex << ',' << slot.Extent.width << ','
            << slot.Extent.height << ',' << slot.NativePresented << ',' << samples << ','
            << black << ',' << (samples ? static_cast<double>(sum) / (3 * samples) : 0)
            << ',' << hash << '\n';
    slot.Pending = false;
}

void VulkanScanoutProbe::Record(VkCommandBuffer command, VkImage image, VkFormat format,
                               VkExtent2D extent, uint32_t index, uint32_t imageIndex,
                               uint64_t frame, bool nativePresented) {
    if (index >= mSlots.size() || !extent.width || !extent.height) return;
    if (format != VK_FORMAT_R8G8B8A8_UNORM && format != VK_FORMAT_R8G8B8A8_SRGB &&
        format != VK_FORMAT_B8G8R8A8_UNORM && format != VK_FORMAT_B8G8R8A8_SRGB) return;
    auto& slot = mSlots[index];
    const VkDeviceSize size = static_cast<VkDeviceSize>(extent.width) * extent.height * 4;
    if (slot.Capacity < size) {
        Destroy(slot);
        VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer.size = size;
        buffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        Check(vkCreateBuffer(mDevice, &buffer, nullptr, &slot.Buffer));
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(mDevice, slot.Buffer, &requirements);
        constexpr auto properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        uint32_t type = 0;
        for (; type < mMemoryProperties.memoryTypeCount; ++type) {
            if ((requirements.memoryTypeBits & (1U << type)) &&
                (mMemoryProperties.memoryTypes[type].propertyFlags & properties) == properties) break;
        }
        if (type == mMemoryProperties.memoryTypeCount) throw std::runtime_error("No scanout probe host memory");
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        Check(vkAllocateMemory(mDevice, &allocation, nullptr, &slot.Memory));
        Check(vkBindBufferMemory(mDevice, slot.Buffer, slot.Memory, 0));
        Check(vkMapMemory(mDevice, slot.Memory, 0, size, 0, &slot.Mapped));
        slot.Capacity = size;
    }
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, slot.Buffer, 1, &copy);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = 0;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host.buffer = slot.Buffer;
    host.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 0, nullptr, 1, &host, 0, nullptr);
    slot.Extent = extent;
    slot.Frame = frame;
    slot.ImageIndex = imageIndex;
    slot.NativePresented = nativePresented;
    slot.Pending = true;
}

void VulkanScanoutProbe::Destroy(Slot& slot) {
    if (slot.Mapped) vkUnmapMemory(mDevice, slot.Memory);
    if (slot.Buffer) vkDestroyBuffer(mDevice, slot.Buffer, nullptr);
    if (slot.Memory) vkFreeMemory(mDevice, slot.Memory, nullptr);
    slot = {};
}

void VulkanScanoutProbe::Shutdown() {
    for (uint32_t index = 0; index < mSlots.size(); ++index) {
        Consume(index);
        Destroy(mSlots[index]);
    }
    mSlots.clear();
    mOutput.close();
    mDevice = VK_NULL_HANDLE;
}
} // namespace Fast
#endif
