#pragma once

#include <vulkan/vulkan.h>

#include <atomic>
#include <optional>
#include <string>

namespace Fast::Renderer3ds {

// VK_EXT_device_fault: after VK_ERROR_DEVICE_LOST the driver can describe the
// fault (a description, the faulting GPU addresses and vendor codes). The
// device enables only `deviceFault`; vendor binary dumps need vendor tools to
// decode and are not requested. Without the extension and feature the
// reporter stays unbound and every query below is a no-op.
[[nodiscard]] bool VulkanDeviceFaultSupported(
    bool extensionAvailable, const VkPhysicalDeviceFaultFeaturesEXT& supported) noexcept;

// The feature structure the device enables when supported.
[[nodiscard]] VkPhysicalDeviceFaultFeaturesEXT VulkanDeviceFaultFeatures() noexcept;

[[nodiscard]] const char* VulkanDeviceFaultAddressTypeName(VkDeviceFaultAddressTypeEXT type) noexcept;

// One line, for the game log and stderr:
// TRIAEVUM_VULKAN_DEVICE_FAULT operation="..." result=-4 description="..."
//   address_infos=[{type=...,address=0x...,precision=0x...}]
//   vendor_infos=[{description="...",code=0x...,data=0x...}]
// `counts` bounds the arrays `info` points to; text is quoted and escaped.
[[nodiscard]] std::string FormatVulkanDeviceFaultLine(
    const char* operation, VkResult result, const VkDeviceFaultCountsEXT& counts,
    const VkDeviceFaultInfoEXT& info);

// Queries the fault of one device once. Bound after vkCreateDevice when the
// feature was enabled; unbound before the device is destroyed.
class VulkanDeviceFaultReporter {
  public:
    void Bind(VkDevice device, bool featureEnabled) noexcept;
    void Unbind() noexcept;
    [[nodiscard]] bool Enabled() const noexcept { return mGetFaultInfo != nullptr; }

    // For VK_ERROR_DEVICE_LOST, the first call queries the driver and returns
    // the formatted line; other results, later calls and an unbound reporter
    // return nullopt. A failed query is reported in the line.
    [[nodiscard]] std::optional<std::string> Consume(VkResult result, const char* operation);

  private:
    VkDevice mDevice = VK_NULL_HANDLE;
    PFN_vkGetDeviceFaultInfoEXT mGetFaultInfo = nullptr;
    std::atomic<bool> mConsumed{false};
};

} // namespace Fast::Renderer3ds
