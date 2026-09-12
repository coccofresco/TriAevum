#pragma once

#include <vulkan/vulkan.h>

namespace Fast::Renderer3ds {

// Preparation and live rendering must use the same Vulkan application contract.
// Drivers may expose different pipeline-cache UUIDs for different API versions.
inline VkApplicationInfo PicaVulkanApplicationInfo() {
    VkApplicationInfo result{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    result.pApplicationName = "TriAevum";
    result.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    result.pEngineName = "ThreeDsRecomp Runtime";
    result.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    result.apiVersion = VK_API_VERSION_1_2;
    return result;
}

inline VkPhysicalDeviceFeatures PicaVulkanCoreFeatures(const VkPhysicalDeviceFeatures& supported) {
    VkPhysicalDeviceFeatures enabled{};
    enabled.independentBlend = supported.independentBlend;
    enabled.multiViewport = supported.multiViewport;
    enabled.shaderImageGatherExtended = supported.shaderImageGatherExtended;
    enabled.shaderStorageImageWriteWithoutFormat = supported.shaderStorageImageWriteWithoutFormat;
    enabled.shaderStorageImageExtendedFormats = supported.shaderStorageImageExtendedFormats;
    enabled.shaderInt16 = supported.shaderInt16;
    return enabled;
}

inline VkPhysicalDeviceVulkan12Features PicaVulkan12Features(const VkPhysicalDeviceVulkan12Features& supported) {
    VkPhysicalDeviceVulkan12Features enabled{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    enabled.bufferDeviceAddress = supported.bufferDeviceAddress;
    enabled.timelineSemaphore = supported.timelineSemaphore;
    enabled.shaderFloat16 = supported.shaderFloat16;
    enabled.descriptorBindingSampledImageUpdateAfterBind = supported.descriptorBindingSampledImageUpdateAfterBind;
    enabled.descriptorBindingStorageImageUpdateAfterBind = supported.descriptorBindingStorageImageUpdateAfterBind;
    return enabled;
}

} // namespace Fast::Renderer3ds
