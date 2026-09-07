#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/renderer_validation_telemetry.h"

#include <vulkan/vulkan.h>

#include <vector>

namespace Fast {

// Owns the optional Vulkan debug messenger and its instance-creation
// contract. Validation is enabled only by OOT3D_VULKAN_VALIDATION=1.
class Oot3dVulkanValidation final {
  public:
    Oot3dVulkanValidation() = default;
    ~Oot3dVulkanValidation();
    Oot3dVulkanValidation(const Oot3dVulkanValidation&) = delete;
    Oot3dVulkanValidation& operator=(
        const Oot3dVulkanValidation&) = delete;

    void ConfigureFromEnvironment(
        std::vector<const char*>& instanceExtensions,
        Oot3d::RendererValidationTelemetry& telemetry);
    void ApplyTo(VkInstanceCreateInfo& createInfo);
    void Initialize(VkInstance instance);
    void Shutdown();

    [[nodiscard]] bool Enabled() const;

  private:
    static VKAPI_ATTR VkBool32 VKAPI_CALL Callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT types,
        const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
        void* userData);

    Oot3d::RendererValidationTelemetry* mTelemetry = nullptr;
    VkInstance mInstance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT mMessenger = VK_NULL_HANDLE;
    VkDebugUtilsMessengerCreateInfoEXT mCreateInfo{};
    std::vector<const char*> mLayers;
    bool mEnabled = false;
};

} // namespace Fast

#endif
