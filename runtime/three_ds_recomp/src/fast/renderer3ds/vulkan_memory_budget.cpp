#include "fast/renderer3ds/vulkan_memory_budget.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

#include "fast/renderer3ds/vulkan_commands.h"

namespace Fast::Renderer3ds {

namespace {

std::atomic<PFN_vkGetPhysicalDeviceMemoryProperties2> gGetMemoryProperties2{nullptr};

} // namespace

void BindVulkanMemoryBudgetInstance(VkInstance instance) noexcept {
    PFN_vkGetPhysicalDeviceMemoryProperties2 command = nullptr;
    if (instance != VK_NULL_HANDLE) {
        command = VulkanInstanceCommand<PFN_vkGetPhysicalDeviceMemoryProperties2>(
            instance, "vkGetPhysicalDeviceMemoryProperties2");
        if (command == nullptr)
            command = VulkanInstanceCommand<PFN_vkGetPhysicalDeviceMemoryProperties2>(
                instance, "vkGetPhysicalDeviceMemoryProperties2KHR");
    }
    gGetMemoryProperties2.store(command, std::memory_order_release);
}

bool VulkanMemoryBudgetSupported(VkPhysicalDevice physicalDevice) noexcept {
    static std::mutex mutex;
    static VkPhysicalDevice cached = VK_NULL_HANDLE;
    static bool supported = false;
    try {
        std::scoped_lock lock(mutex);
        if (cached != physicalDevice) {
            uint32_t count = 0;
            std::vector<VkExtensionProperties> extensions;
            if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr) == VK_SUCCESS) {
                extensions.resize(count);
                if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, extensions.data()) !=
                    VK_SUCCESS)
                    count = 0;
            }
            supported = std::any_of(extensions.begin(), extensions.begin() + count, [](const auto& extension) {
                return std::strcmp(extension.extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0;
            });
            cached = physicalDevice;
        }
        return supported;
    } catch (...) {
        return false;
    }
}

namespace {

struct BudgetProperties {
    VkPhysicalDeviceMemoryBudgetPropertiesEXT Budget{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
    VkPhysicalDeviceMemoryProperties2 Properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2};
};

bool ReadBudget(VkPhysicalDevice physicalDevice, BudgetProperties& properties) noexcept {
    const auto getMemoryProperties2 = gGetMemoryProperties2.load(std::memory_order_acquire);
    if (physicalDevice == VK_NULL_HANDLE || getMemoryProperties2 == nullptr ||
        !VulkanMemoryBudgetSupported(physicalDevice))
        return false;
    properties.Properties.pNext = &properties.Budget;
    getMemoryProperties2(physicalDevice, &properties.Properties);
    return true;
}

std::optional<VulkanDeviceLocalMemory> HeapMemory(const BudgetProperties& properties, uint32_t heap) {
    const auto& memory = properties.Properties.memoryProperties;
    if (heap >= memory.memoryHeapCount || (memory.memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == 0U ||
        properties.Budget.heapBudget[heap] == 0U)
        return std::nullopt;
    return VulkanDeviceLocalMemory{heap, properties.Budget.heapUsage[heap], properties.Budget.heapBudget[heap],
                                   memory.memoryHeaps[heap].size};
}

} // namespace

std::optional<VulkanDeviceLocalMemory> QueryVulkanHeapMemory(VkPhysicalDevice physicalDevice, uint32_t heap) noexcept {
    BudgetProperties properties;
    if (!ReadBudget(physicalDevice, properties)) return std::nullopt;
    return HeapMemory(properties, heap);
}

std::optional<VulkanDeviceLocalMemory> QueryVulkanDeviceLocalMemory(VkPhysicalDevice physicalDevice) noexcept {
    BudgetProperties properties;
    if (!ReadBudget(physicalDevice, properties)) return std::nullopt;
    std::optional<VulkanDeviceLocalMemory> largest;
    for (uint32_t heap = 0; heap < properties.Properties.memoryProperties.memoryHeapCount; ++heap) {
        const auto memory = HeapMemory(properties, heap);
        if (memory && (!largest || memory->Size > largest->Size)) largest = memory;
    }
    return largest;
}

} // namespace Fast::Renderer3ds
