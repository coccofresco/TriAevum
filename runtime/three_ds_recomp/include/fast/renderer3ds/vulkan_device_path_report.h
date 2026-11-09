#pragma once

#include "fast/renderer3ds/pica_vulkan_device_profile.h"

#include <string>

namespace Fast::Renderer3ds {

// One log line, at warning level so release logs keep it, that says which
// renderer path the device negotiation chose: the pipeline form (library
// parts or monolithic) with the policy's reason, and whether robust buffer
// access is on. Testers' reports (#117-#120, #125, #126 on AMD Windows,
// RDNA1/RDNA2/APU) carried only the device line, so the chosen path had to be
// guessed from the release. The line only describes the decisions the device
// profile already made; it decides nothing.
struct VulkanDevicePath {
    const char* DeviceName = "";
    VkDriverId Driver = VkDriverId(0);
    PicaPipelineLibraryPolicy Libraries;
    bool LibrariesSupported = false;
    bool RobustBufferAccessSupported = false;
    bool RobustBufferAccessEnabled = false;
    bool ExtendedDynamicState = false;
};

inline const char* VulkanDriverLabel(VkDriverId driver) {
    switch (driver) {
    case VK_DRIVER_ID_AMD_PROPRIETARY: return "amd_proprietary";
    case VK_DRIVER_ID_AMD_OPEN_SOURCE: return "amd_open_source";
    case VK_DRIVER_ID_MESA_RADV: return "mesa_radv";
    case VK_DRIVER_ID_NVIDIA_PROPRIETARY: return "nvidia_proprietary";
    case VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS: return "intel_proprietary_windows";
    case VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA: return "intel_mesa_anv";
    case VK_DRIVER_ID_MOLTENVK: return "moltenvk";
    default: return "other";
    }
}

// "required_unsupported" marks a device whose driver needs robust buffer
// access (PicaRobustBufferAccessRequired) but does not offer it: the session
// still starts, and the report names the known fault risk.
inline const char* VulkanRobustBufferAccessState(const VulkanDevicePath& path) {
    if (path.RobustBufferAccessEnabled) return "on";
    if (PicaRobustBufferAccessRequired(path.Driver) && !path.RobustBufferAccessSupported)
        return "required_unsupported";
    return "off";
}

inline std::string DescribeVulkanDevicePath(const VulkanDevicePath& path) {
    std::string line = "TriAevum Vulkan renderer path: device=\"";
    line += path.DeviceName ? path.DeviceName : "";
    line += "\" driver=";
    line += VulkanDriverLabel(path.Driver);
    line += " (id ";
    line += std::to_string(static_cast<unsigned>(path.Driver));
    line += ") pipelines=";
    line += path.Libraries.Enabled ? (path.Libraries.AllPipelines ? "libraries_all" : "libraries") : "monolithic";
    line += " policy=";
    line += path.Libraries.Reason ? path.Libraries.Reason : "default";
    line += " libraries_supported=";
    line += path.LibrariesSupported ? "1" : "0";
    line += " robust_buffer_access=";
    line += VulkanRobustBufferAccessState(path);
    line += " extended_dynamic_state=";
    line += path.ExtendedDynamicState ? "1" : "0";
    return line;
}

} // namespace Fast::Renderer3ds
