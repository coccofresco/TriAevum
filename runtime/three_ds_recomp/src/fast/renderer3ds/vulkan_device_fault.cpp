#include "fast/renderer3ds/vulkan_device_fault.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "fast/renderer3ds/vulkan_commands.h"

namespace Fast::Renderer3ds {

namespace {

// Driver text is fixed-size and not guaranteed to be terminated; the line
// must stay one parseable line whatever it contains.
void AppendQuoted(std::string& line, const char* text, size_t capacity) {
    line += '"';
    for (size_t index = 0; index < capacity && text[index] != '\0'; ++index) {
        const auto character = static_cast<unsigned char>(text[index]);
        if (character == '"' || character == '\\') {
            line += '\\';
            line += static_cast<char>(character);
        } else if (character < 0x20U || character == 0x7FU) {
            char escaped[8];
            std::snprintf(escaped, sizeof(escaped), "\\x%02X", character);
            line += escaped;
        } else {
            line += static_cast<char>(character);
        }
    }
    line += '"';
}

void AppendHex(std::string& line, uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof(text), "0x%llX", static_cast<unsigned long long>(value));
    line += text;
}

std::string LinePrefix(const char* operation, VkResult result) {
    std::string line = "TRIAEVUM_VULKAN_DEVICE_FAULT operation=";
    const char* name = operation != nullptr ? operation : "unknown";
    AppendQuoted(line, name, std::strlen(name));
    line += " result=" + std::to_string(static_cast<int>(result));
    return line;
}

} // namespace

bool VulkanDeviceFaultSupported(
    bool extensionAvailable, const VkPhysicalDeviceFaultFeaturesEXT& supported) noexcept {
    return extensionAvailable && supported.deviceFault == VK_TRUE;
}

VkPhysicalDeviceFaultFeaturesEXT VulkanDeviceFaultFeatures() noexcept {
    VkPhysicalDeviceFaultFeaturesEXT enabled{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT};
    enabled.deviceFault = VK_TRUE;
    return enabled;
}

const char* VulkanDeviceFaultAddressTypeName(VkDeviceFaultAddressTypeEXT type) noexcept {
    switch (type) {
        case VK_DEVICE_FAULT_ADDRESS_TYPE_NONE_EXT:
            return "none";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_EXT:
            return "read_invalid";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_WRITE_INVALID_EXT:
            return "write_invalid";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_EXECUTE_INVALID_EXT:
            return "execute_invalid";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_UNKNOWN_EXT:
            return "instruction_pointer_unknown";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_INVALID_EXT:
            return "instruction_pointer_invalid";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_FAULT_EXT:
            return "instruction_pointer_fault";
        default:
            return "unknown";
    }
}

std::string FormatVulkanDeviceFaultLine(
    const char* operation, VkResult result, const VkDeviceFaultCountsEXT& counts,
    const VkDeviceFaultInfoEXT& info) {
    std::string line = LinePrefix(operation, result);
    line += " description=";
    AppendQuoted(line, info.description, VK_MAX_DESCRIPTION_SIZE);
    line += " address_infos=[";
    const uint32_t addressCount = info.pAddressInfos != nullptr ? counts.addressInfoCount : 0U;
    for (uint32_t index = 0; index < addressCount; ++index) {
        const auto& address = info.pAddressInfos[index];
        line += index == 0U ? "{type=" : ",{type=";
        line += VulkanDeviceFaultAddressTypeName(address.addressType);
        line += ",address=";
        AppendHex(line, address.reportedAddress);
        line += ",precision=";
        AppendHex(line, address.addressPrecision);
        line += '}';
    }
    line += "] vendor_infos=[";
    const uint32_t vendorCount = info.pVendorInfos != nullptr ? counts.vendorInfoCount : 0U;
    for (uint32_t index = 0; index < vendorCount; ++index) {
        const auto& vendor = info.pVendorInfos[index];
        line += index == 0U ? "{description=" : ",{description=";
        AppendQuoted(line, vendor.description, VK_MAX_DESCRIPTION_SIZE);
        line += ",code=";
        AppendHex(line, vendor.vendorFaultCode);
        line += ",data=";
        AppendHex(line, vendor.vendorFaultData);
        line += '}';
    }
    line += ']';
    return line;
}

void VulkanDeviceFaultReporter::Bind(VkDevice device, bool featureEnabled) noexcept {
    mDevice = device;
    mGetFaultInfo = featureEnabled && device != VK_NULL_HANDLE
        ? VulkanDeviceCommand<PFN_vkGetDeviceFaultInfoEXT>(device, "vkGetDeviceFaultInfoEXT")
        : nullptr;
    mConsumed.store(false);
}

void VulkanDeviceFaultReporter::Unbind() noexcept {
    mDevice = VK_NULL_HANDLE;
    mGetFaultInfo = nullptr;
}

std::optional<std::string> VulkanDeviceFaultReporter::Consume(VkResult result, const char* operation) {
    if (result != VK_ERROR_DEVICE_LOST || mGetFaultInfo == nullptr || mConsumed.exchange(true))
        return std::nullopt;
    try {
        VkDeviceFaultCountsEXT counts{VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT};
        VkResult query = mGetFaultInfo(mDevice, &counts, nullptr);
        if (query != VK_SUCCESS)
            return LinePrefix(operation, result) + " query_result=" + std::to_string(static_cast<int>(query));
        std::vector<VkDeviceFaultAddressInfoEXT> addresses(counts.addressInfoCount);
        std::vector<VkDeviceFaultVendorInfoEXT> vendors(counts.vendorInfoCount);
        // The vendor binary is not requested (deviceFaultVendorBinary off).
        counts.vendorBinarySize = 0;
        VkDeviceFaultInfoEXT info{VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT};
        info.pAddressInfos = addresses.empty() ? nullptr : addresses.data();
        info.pVendorInfos = vendors.empty() ? nullptr : vendors.data();
        query = mGetFaultInfo(mDevice, &counts, &info);
        // VK_INCOMPLETE still fills what fits; counts then hold what was written.
        if (query != VK_SUCCESS && query != VK_INCOMPLETE)
            return LinePrefix(operation, result) + " query_result=" + std::to_string(static_cast<int>(query));
        counts.addressInfoCount = std::min<uint32_t>(counts.addressInfoCount, uint32_t(addresses.size()));
        counts.vendorInfoCount = std::min<uint32_t>(counts.vendorInfoCount, uint32_t(vendors.size()));
        return FormatVulkanDeviceFaultLine(operation, result, counts, info);
    } catch (...) {
        return LinePrefix(operation, result) + " query_result=exception";
    }
}

} // namespace Fast::Renderer3ds
