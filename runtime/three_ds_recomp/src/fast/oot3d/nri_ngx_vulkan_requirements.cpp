#include "fast/oot3d/nri_ngx_vulkan_requirements.h"

#ifdef ENABLE_OOT3D_NGX
#include <vulkan/vulkan.h>
#include <nvsdk_ngx_vk.h>
#endif

namespace Fast::Oot3d {

namespace {
#ifdef OOT3D_NGX_RUNTIME_VERSION
constexpr const char* kNgxRuntimeVersion = OOT3D_NGX_RUNTIME_VERSION;
#else
constexpr const char* kNgxRuntimeVersion = "unknown";
#endif
}

const NriNgxVulkanRequirements& GetNriNgxVulkanRequirements() {
    static const NriNgxVulkanRequirements requirements = [] {
        NriNgxVulkanRequirements result;
        result.RuntimeVersion = kNgxRuntimeVersion;
#ifdef ENABLE_OOT3D_NGX
        unsigned int instanceCount = 0;
        unsigned int deviceCount = 0;
        const char** instances = nullptr;
        const char** devices = nullptr;
        if (NVSDK_NGX_VULKAN_RequiredExtensions(
                &instanceCount, &instances, &deviceCount, &devices) !=
            NVSDK_NGX_Result_Success) {
            result.Reason = "NGX failed to report its Vulkan requirements";
            return result;
        }
        for (unsigned int index = 0; index < instanceCount; ++index)
            result.InstanceExtensions.emplace_back(instances[index]);
        for (unsigned int index = 0; index < deviceCount; ++index)
            result.DeviceExtensions.emplace_back(devices[index]);
        result.Available = true;
#else
        result.Reason = "NGX SDK support is not compiled";
#endif
        return result;
    }();
    return requirements;
}

} // namespace Fast::Oot3d
