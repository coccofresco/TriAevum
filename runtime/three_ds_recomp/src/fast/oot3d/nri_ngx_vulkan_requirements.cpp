#include "fast/oot3d/nri_ngx_vulkan_requirements.h"

#include "fast/oot3d/dlss_module_loader.h"

#include <array>

namespace Fast::Oot3d {

const NriNgxVulkanRequirements& GetNriNgxVulkanRequirements() {
    static const NriNgxVulkanRequirements requirements = [] {
        NriNgxVulkanRequirements result;
        // NGX is the DLSS module's (triaevum/dlss_module.h); the runtime
        // links nothing of it.
        const TriaevumDlssModule* module = DlssModule();
        if (module == nullptr) {
            result.RuntimeVersion = "unknown";
            result.Reason = DlssModuleUnavailableReason();
            return result;
        }
        result.RuntimeVersion = module->RuntimeVersion != nullptr ? module->RuntimeVersion : "unknown";
        TriaevumDlssVulkanRequirements extensions{};
        std::array<char, 256> reason{};
        if (module->VulkanRequirements(&extensions, reason.data(), reason.size()) == 0) {
            result.Reason = reason.data();
            return result;
        }
        for (uint32_t index = 0; index < extensions.InstanceExtensionCount; ++index)
            result.InstanceExtensions.emplace_back(extensions.InstanceExtensions[index]);
        for (uint32_t index = 0; index < extensions.DeviceExtensionCount; ++index)
            result.DeviceExtensions.emplace_back(extensions.DeviceExtensions[index]);
        result.Available = true;
        return result;
    }();
    return requirements;
}

} // namespace Fast::Oot3d
