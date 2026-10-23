#pragma once

#include <string>
#include <vector>

namespace Fast::Oot3d {

struct NriNgxVulkanRequirements {
    bool Available = false;
    std::vector<std::string> InstanceExtensions;
    std::vector<std::string> DeviceExtensions;
    std::string RuntimeVersion;
    std::string Reason;
};

// NGX requirements must be queried before the Vulkan instance/device exist.
// Keeping the query out of the backend prevents SDK details from leaking into
// its instance and device creation logic. They come from the DLSS module
// (dlss_module_loader.h); without it nothing is required and DLSS is not
// offered.
const NriNgxVulkanRequirements& GetNriNgxVulkanRequirements();

} // namespace Fast::Oot3d
