#include "fast/backends/oot3d_vulkan_validation.h"

#include <cstdio>
#ifdef ENABLE_OOT3D_VULKAN

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Fast {
namespace {

constexpr const char* kValidationEnvironment =
    "OOT3D_VULKAN_VALIDATION";
constexpr const char* kValidationLayer =
    "VK_LAYER_KHRONOS_validation";

bool ValidationRequested() {
    const char* value = std::getenv(kValidationEnvironment);
    return value != nullptr && std::string_view(value) == "1";
}

template <class Property>
bool ContainsName(const std::vector<Property>& properties,
                  std::string_view name) {
    return std::any_of(
        properties.begin(), properties.end(),
        [name](const Property& property) {
            if constexpr (requires { property.layerName; }) {
                return name == property.layerName;
            } else {
                return name == property.extensionName;
            }
        });
}

} // namespace

Oot3dVulkanValidation::~Oot3dVulkanValidation() {
    Shutdown();
}

void Oot3dVulkanValidation::ConfigureFromEnvironment(
    std::vector<const char*>& instanceExtensions,
    Oot3d::RendererValidationTelemetry& telemetry) {
    Shutdown();
    mTelemetry = &telemetry;
    mTelemetry->SetEnabled(
        Oot3d::RendererValidationSource::Vulkan, false);
    if (!ValidationRequested()) {
        return;
    }

    uint32_t layerCount = 0;
    if (vkEnumerateInstanceLayerProperties(
            &layerCount, nullptr) != VK_SUCCESS) {
        throw std::runtime_error(
            "cannot enumerate Vulkan validation layers");
    }
    std::vector<VkLayerProperties> layers(layerCount);
    if (vkEnumerateInstanceLayerProperties(
            &layerCount, layers.data()) != VK_SUCCESS ||
        !ContainsName(layers, kValidationLayer)) {
        throw std::runtime_error(
            "OOT3D_VULKAN_VALIDATION requested but "
            "VK_LAYER_KHRONOS_validation is unavailable");
    }

    uint32_t extensionCount = 0;
    if (vkEnumerateInstanceExtensionProperties(
            nullptr, &extensionCount, nullptr) != VK_SUCCESS) {
        throw std::runtime_error(
            "cannot enumerate Vulkan instance extensions");
    }
    std::vector<VkExtensionProperties> extensions(extensionCount);
    if (vkEnumerateInstanceExtensionProperties(
            nullptr, &extensionCount, extensions.data()) != VK_SUCCESS ||
        !ContainsName(extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
        throw std::runtime_error(
            "OOT3D_VULKAN_VALIDATION requested but "
            "VK_EXT_debug_utils is unavailable");
    }

    if (std::none_of(
            instanceExtensions.begin(), instanceExtensions.end(),
            [](const char* name) {
                return std::string_view(name) ==
                       VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
            })) {
        instanceExtensions.push_back(
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    mLayers = {kValidationLayer};
    mCreateInfo = {
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    mCreateInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    mCreateInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    mCreateInfo.pfnUserCallback = Callback;
    mCreateInfo.pUserData = mTelemetry;
    mEnabled = true;
    mTelemetry->SetEnabled(
        Oot3d::RendererValidationSource::Vulkan, true);
}

void Oot3dVulkanValidation::ApplyTo(
    VkInstanceCreateInfo& createInfo) {
    if (!mEnabled) {
        return;
    }
    createInfo.enabledLayerCount =
        static_cast<uint32_t>(mLayers.size());
    createInfo.ppEnabledLayerNames = mLayers.data();
    createInfo.pNext = &mCreateInfo;
}

void Oot3dVulkanValidation::Initialize(VkInstance instance) {
    if (!mEnabled) {
        return;
    }
    const auto create =
        reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(
                instance, "vkCreateDebugUtilsMessengerEXT"));
    if (create == nullptr ||
        create(instance, &mCreateInfo, nullptr, &mMessenger) !=
            VK_SUCCESS) {
        throw std::runtime_error(
            "cannot create Vulkan validation debug messenger");
    }
    mInstance = instance;
}

void Oot3dVulkanValidation::Shutdown() {
    if (mInstance != VK_NULL_HANDLE &&
        mMessenger != VK_NULL_HANDLE) {
        const auto destroy =
            reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(
                    mInstance,
                    "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy != nullptr) {
            destroy(mInstance, mMessenger, nullptr);
        }
    }
    mInstance = VK_NULL_HANDLE;
    mMessenger = VK_NULL_HANDLE;
    mCreateInfo = {};
    mLayers.clear();
    mEnabled = false;
}

bool Oot3dVulkanValidation::Enabled() const {
    return mEnabled;
}

VKAPI_ATTR VkBool32 VKAPI_CALL Oot3dVulkanValidation::Callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT types,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* userData) {
    auto* telemetry =
        static_cast<Oot3d::RendererValidationTelemetry*>(userData);
    const char* message =
        callbackData != nullptr && callbackData->pMessage != nullptr
            ? callbackData->pMessage : "unspecified validation message";
    const bool validationError =
        (severity &
         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0U &&
        (types &
         VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) != 0U;
    if (validationError) {
        if (telemetry != nullptr) {
            telemetry->Record(
                Oot3d::RendererValidationSource::Vulkan,
                Oot3d::RendererValidationSeverity::Error);
        }
        std::fprintf(stderr, "OOT3D Vulkan validation error: %s\n",
                     message);
        SPDLOG_ERROR("OOT3D Vulkan validation: {}", message);
    } else {
        if (telemetry != nullptr) {
            telemetry->Record(
                Oot3d::RendererValidationSource::Vulkan,
                Oot3d::RendererValidationSeverity::Warning);
        }
        SPDLOG_WARN("OOT3D Vulkan validation: {}", message);
    }
    return VK_FALSE;
}

} // namespace Fast

#endif
