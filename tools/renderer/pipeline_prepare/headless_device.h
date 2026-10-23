#pragma once
#include <cstdio>
#include <cstdlib>

#include "fast/renderer3ds/nri_pica_pipeline_bridge.h"
#include "fast/renderer3ds/pica_vulkan_device_profile.h"
#include "fast/renderer3ds/vulkan_commands.h"
#include <NRI.h>
#include <Extensions/NRIRayTracing.h>
#include <Extensions/NRIWrapperVK.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace TriAevum::Tools {

class HeadlessDevice final : public Fast::Renderer3ds::NriPicaInterop {
  public:
    ~HeadlessDevice() override {
        if (mDevice) nri::nriDestroyDevice(mDevice);
        if (mVkDevice) vkDestroyDevice(mVkDevice, nullptr);
        if (mMessenger) reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(mInstance, "vkDestroyDebugUtilsMessengerEXT"))(mInstance, mMessenger, nullptr);
        if (mInstance) vkDestroyInstance(mInstance, nullptr);
    }
    bool Initialize(uint32_t index, bool validation) {
        uint32_t count = 0;
        if (nri::nriEnumerateAdapters(nullptr, count) != nri::Result::SUCCESS || !count || count > 64 || index >= count)
            return false;
        std::vector<nri::AdapterDesc> adapters(count);
        if (nri::nriEnumerateAdapters(adapters.data(), count) != nri::Result::SUCCESS || index >= count) return false;
        const auto app = Fast::Renderer3ds::PicaVulkanApplicationInfo();
        const char* layer = "VK_LAYER_KHRONOS_validation";
        const char* debugExtension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        VkInstanceCreateInfo instance{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instance.pApplicationInfo = &app;
        instance.enabledLayerCount = validation ? 1 : 0;
        instance.ppEnabledLayerNames = &layer;
        instance.enabledExtensionCount = validation ? 1 : 0;
        instance.ppEnabledExtensionNames = &debugExtension;
        const auto instanceResult = vkCreateInstance(&instance, nullptr, &mInstance);
        if (instanceResult != VK_SUCCESS)
            throw std::runtime_error("vkCreateInstance failed (" + std::to_string(instanceResult) +
                (validation ? "); --validation requires VK_LAYER_KHRONOS_validation" : ")"));
        if (validation) {
            VkDebugUtilsMessengerCreateInfoEXT messenger{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            messenger.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            messenger.pfnUserCallback = VulkanMessage;
            messenger.pUserData = this;
            const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(mInstance, "vkCreateDebugUtilsMessengerEXT"));
            if (!createMessenger || createMessenger(mInstance, &messenger, nullptr, &mMessenger) != VK_SUCCESS) return false;
        }
        const auto getProperties2 = Fast::Renderer3ds::VulkanInstanceCommand<PFN_vkGetPhysicalDeviceProperties2>(
            mInstance, "vkGetPhysicalDeviceProperties2");
        const auto getFeatures2 = Fast::Renderer3ds::VulkanInstanceCommand<PFN_vkGetPhysicalDeviceFeatures2>(
            mInstance, "vkGetPhysicalDeviceFeatures2");
        if (!getProperties2 || !getFeatures2) return false;
        uint32_t physicalCount = 0;
        if (vkEnumeratePhysicalDevices(mInstance, &physicalCount, nullptr) != VK_SUCCESS || physicalCount > 64) return false;
        std::vector<VkPhysicalDevice> physicals(physicalCount);
        if (vkEnumeratePhysicalDevices(mInstance, &physicalCount, physicals.data()) != VK_SUCCESS) return false;
        VkPhysicalDevice physical = VK_NULL_HANDLE;
        for (auto candidate : physicals) {
            VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
            VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
            props.pNext = &id;
            getProperties2(candidate, &props);
            nri::Uid_t uid{};
            std::memcpy(&uid.low, id.deviceLUIDValid ? id.deviceLUID : id.deviceUUID, 8);
            if (!id.deviceLUIDValid) std::memcpy(&uid.high, id.deviceUUID + 8, 8);
            if (uid.low == adapters[index].uid.low && uid.high == adapters[index].uid.high) { physical = candidate; break; }
        }
        if (!physical) return false;
        uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &queueCount, queues.data());
        uint32_t family = 0;
        while (family < queueCount && !(queues[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
        if (family == queueCount) return false;
        float priority = 1;
        VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue.queueFamilyIndex = family; queue.queueCount = 1; queue.pQueuePriorities = &priority;
        VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT libraries{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT};
        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extendedDynamic{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT};
        VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamic{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};
        VkPhysicalDeviceSynchronization2FeaturesKHR sync{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR};
        VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        supported.pNext = &v12; v12.pNext = &sync; sync.pNext = &dynamic;
        dynamic.pNext = &libraries; libraries.pNext = &extendedDynamic;
        getFeatures2(physical, &supported);
        if (!sync.synchronization2 || !dynamic.dynamicRendering) return false;
        uint32_t extensionCount = 0;
        if (vkEnumerateDeviceExtensionProperties(physical, nullptr, &extensionCount, nullptr) != VK_SUCCESS) return false;
        std::vector<VkExtensionProperties> available(extensionCount);
        if (vkEnumerateDeviceExtensionProperties(physical, nullptr, &extensionCount, available.data()) != VK_SUCCESS)
            return false;
        const auto hasExtension = [&](const char* name) {
            for (const auto& extension : available)
                if (std::strcmp(extension.extensionName, name) == 0) return true;
            return false;
        };
        const auto& libraryExtensions = Fast::Renderer3ds::kPicaPipelineLibraryExtensions;
        const bool librarySupported = Fast::Renderer3ds::PicaPipelineLibrariesSupported(
            std::all_of(libraryExtensions.begin(), libraryExtensions.end(), hasExtension),
            libraries, extendedDynamic, supported.features);
        VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT libraryProperties{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_PROPERTIES_EXT};
        VkPhysicalDeviceDriverProperties driverProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
        VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties.pNext = &driverProperties;
        driverProperties.pNext = &libraryProperties;
        getProperties2(physical, &properties);
        const auto policy = Fast::Renderer3ds::ResolvePicaPipelineLibraryPolicy(
            std::getenv("TRIAEVUM_NRI_PIPELINE_LIBRARIES"), librarySupported, dynamic.dynamicRendering,
            true, driverProperties.driverID);
        PipelineLibraries = policy.Enabled;
        AllPipelinesUseLibraries = policy.AllPipelines;
        std::fprintf(stderr, "TRIAEVUM_NRI_GPL_CAPABILITY supported=%u requested=%u enabled=%u fast_link=%u independent_interpolation=%u driver_id=%u policy=%s\n",
            unsigned(librarySupported), unsigned(policy.Requested), unsigned(policy.Enabled),
            unsigned(libraryProperties.graphicsPipelineLibraryFastLinking),
            unsigned(libraryProperties.graphicsPipelineLibraryIndependentInterpolationDecoration),
            unsigned(driverProperties.driverID), policy.Reason);
        GeometryShaders = supported.features.geometryShader == VK_TRUE;
        const auto coreFeatures = Fast::Renderer3ds::PicaVulkanCoreFeatures(supported.features);
        v12 = Fast::Renderer3ds::PicaVulkan12Features(v12);
        v12.pNext = &sync;
        dynamic.pNext = PipelineLibraries ? &libraries : nullptr;
        std::vector<const char*> extensions{VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME};
        if (PipelineLibraries) extensions.insert(extensions.end(), libraryExtensions.begin(), libraryExtensions.end());
        VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        device.pNext = &v12; device.pEnabledFeatures = &coreFeatures;
        device.queueCreateInfoCount = 1; device.pQueueCreateInfos = &queue;
        device.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        device.ppEnabledExtensionNames = extensions.data();
        if (vkCreateDevice(physical, &device, nullptr, &mVkDevice) != VK_SUCCESS) return false;
        nri::QueueFamilyVKDesc nriQueue{1, nri::QueueType::GRAPHICS, family};
        nri::DeviceCreationVKDesc create{};
        create.vkInstance = mInstance; create.vkDevice = mVkDevice; create.vkPhysicalDevice = physical;
        create.minorVersion = VK_API_VERSION_MINOR(app.apiVersion);
        create.queueFamilies = &nriQueue; create.queueFamilyNum = 1;
        create.vkExtensions.deviceExtensions = extensions.data();
        create.vkExtensions.deviceExtensionNum = static_cast<uint32_t>(extensions.size());
        create.enableNRIValidation = validation;
        create.callbackInterface.MessageCallback = Message;
        create.callbackInterface.userArg = this;
        if (nri::nriCreateDeviceFromVKDevice(create, mDevice) != nri::Result::SUCCESS) return false;
        return nri::nriGetInterface(*mDevice, NRI_INTERFACE(nri::CoreInterface), &mCore) == nri::Result::SUCCESS &&
            nri::nriGetInterface(*mDevice, NRI_INTERFACE(nri::WrapperVKInterface), &Wrapper) == nri::Result::SUCCESS;
    }
    bool Available() const override { return mDevice != nullptr; }
    const std::string& UnavailableReason() const override { return Reason; }
    nri::Device* Device() override { return mDevice; }
    nri::CoreInterface* Core() override { return &mCore; }
    bool WrapBuffer(VkBuffer, uint64_t, uint8_t*) override { return false; }
    bool WrapTexture(VkImage, VkFormat, VkImageType, VkImageUsageFlags, uint32_t, uint32_t, uint32_t) override { return false; }
    nri::CommandBuffer* CommandBuffer(uint32_t) override { return nullptr; }
    nri::Descriptor* TextureView(VkImage, bool) override { return nullptr; }
    nri::Buffer* Buffer(VkBuffer) override { return nullptr; }
    nri::Pipeline* WrapGraphicsPipeline(VkPipeline) override { return nullptr; }
    void DestroyPipelineWrapper(nri::Pipeline*) override {}
    bool CmdSetPipeline(uint32_t, nri::Pipeline*) override { return false; }
    uint32_t ValidationErrors() const { return mValidationErrors.load(); }
    nri::WrapperVKInterface Wrapper{};
    // Negotiated as the live renderer does: library parts can be prepared.
    bool PipelineLibraries = false;
    bool AllPipelinesUseLibraries = false;
    bool GeometryShaders = false;
  private:
    static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void* context) {
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            ++static_cast<HeadlessDevice*>(context)->mValidationErrors;
        std::cerr << "Vulkan: " << data->pMessage << '\n';
        return VK_FALSE;
    }
    static void NRI_CALL Message(nri::Message type, const char*, uint32_t, const char* text, void* context) {
        if (type == nri::Message::ERROR) ++static_cast<HeadlessDevice*>(context)->mValidationErrors;
        std::cerr << "NRI: " << text << '\n';
    }
    std::atomic<uint32_t> mValidationErrors{0};
    nri::Device* mDevice = nullptr;
    VkInstance mInstance = VK_NULL_HANDLE;
    VkDevice mVkDevice = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT mMessenger = VK_NULL_HANDLE;
    nri::CoreInterface mCore{};
    std::string Reason = "headless NRI device unavailable";
};

} // namespace TriAevum::Tools
