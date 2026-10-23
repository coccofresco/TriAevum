// Vulkan backend: instance, devices, pipeline cache, the presentation worker, command submission
// and the swapchain.
#ifdef ENABLE_OOT3D_VULKAN
#include "gfx_vulkan_internal.h"
#include "fast/renderer3ds/vulkan_commands.h"

namespace Fast {

namespace {

// Vulkan 1.1 physical-device queries come from the instance
// (renderer3ds/vulkan_commands.h): a loader without them is a reported error.
template <typename Command>
Command RequiredInstanceCommand(VkInstance instance, const char* name) {
    const auto command = Renderer3ds::VulkanInstanceCommand<Command>(instance, name);
    if (command == nullptr)
        throw std::runtime_error(std::string(name) +
                                 " is unavailable: the Vulkan loader predates Vulkan 1.1; update the graphics driver");
    return command;
}

} // namespace

bool GfxRenderingAPIVulkan::QueueFamilies::Complete() const {
    return Graphics.has_value() && Present.has_value();
}

void GfxRenderingAPIVulkan::CreateInstance() {
    auto* window = static_cast<SDL_Window*>(mWindowBackend->GetNativeWindow());
    std::vector<const char*> extensions;
#ifdef _WIN32
    extensions = {VK_KHR_SURFACE_EXTENSION_NAME,
                  VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
#else
    unsigned int extensionCount = 0;
    if (SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, nullptr) != SDL_TRUE) {
        throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError());
    }
    extensions.resize(extensionCount);
    if (SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, extensions.data()) != SDL_TRUE) {
        throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError());
    }
#endif

    const auto& ngxRequirements = Oot3d::GetNriNgxVulkanRequirements();
    mNgxVulkanRequirementsSatisfied = ngxRequirements.Available;
    mNgxVulkanUnavailableReason = ngxRequirements.Reason;
    const auto recordMissingNgxRequirement = [this](
        std::string_view category, const std::string& extension) {
        if (!mNgxVulkanUnavailableReason.empty())
            mNgxVulkanUnavailableReason += "; ";
        mNgxVulkanUnavailableReason += "missing NGX ";
        mNgxVulkanUnavailableReason += category;
        mNgxVulkanUnavailableReason += " extension ";
        mNgxVulkanUnavailableReason += extension;
    };
    uint32_t availableCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, nullptr);
    std::vector<VkExtensionProperties> available(availableCount);
    vkEnumerateInstanceExtensionProperties(
        nullptr, &availableCount, available.data());
    for (const std::string& required : ngxRequirements.InstanceExtensions) {
        const bool supported = std::any_of(
            available.begin(), available.end(), [&](const auto& extension) {
                return required == extension.extensionName;
            });
        mNgxVulkanRequirementsSatisfied &= supported;
        if (!supported)
            recordMissingNgxRequirement("instance", required);
        if (supported && std::none_of(
                extensions.begin(), extensions.end(), [&](const char* name) {
                    return required == name;
                }))
            extensions.push_back(required.c_str());
    }
    constexpr const char* kSurfaceCapabilities2 =
        "VK_KHR_get_surface_capabilities2";
    mNriSwapchainExtensionsEnabled = std::any_of(
        available.begin(), available.end(), [&](const auto& extension) {
            return std::string_view(extension.extensionName) ==
                   kSurfaceCapabilities2;
        });
    if (mNriSwapchainExtensionsEnabled &&
        std::none_of(
            extensions.begin(), extensions.end(), [&](const char* name) {
                return std::string_view(name) == kSurfaceCapabilities2;
            })) {
        extensions.push_back(kSurfaceCapabilities2);
    }
    mVulkanValidation.ConfigureFromEnvironment(
        extensions, mValidationTelemetry);

    const auto applicationInfo = Renderer3ds::PicaVulkanApplicationInfo();

    VkInstanceCreateInfo createInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    createInfo.pApplicationInfo = &applicationInfo;
    createInfo.enabledExtensionCount =
        static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    mVulkanValidation.ApplyTo(createInfo);
    CheckVk(vkCreateInstance(&createInfo, nullptr, &mInstance), "vkCreateInstance");
    mVulkanValidation.Initialize(mInstance);
}

void GfxRenderingAPIVulkan::CreateSurface() {
    auto* window = static_cast<SDL_Window*>(mWindowBackend->GetNativeWindow());
#ifdef _WIN32
    SDL_SysWMinfo windowInfo{};
    SDL_VERSION(&windowInfo.version);
    if (SDL_GetWindowWMInfo(window, &windowInfo) != SDL_TRUE) {
        throw std::runtime_error(std::string("SDL_GetWindowWMInfo failed: ") + SDL_GetError());
    }
    VkWin32SurfaceCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    createInfo.hinstance = GetModuleHandleW(nullptr);
    createInfo.hwnd = windowInfo.info.win.window;
    CheckVk(vkCreateWin32SurfaceKHR(mInstance, &createInfo, nullptr, &mSurface),
            "vkCreateWin32SurfaceKHR");
#else
    if (SDL_Vulkan_CreateSurface(window, mInstance, &mSurface) != SDL_TRUE) {
        throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
    }
#endif
}

void GfxRenderingAPIVulkan::PickPhysicalDevice() {
    uint32_t deviceCount = 0;
    CheckVk(vkEnumeratePhysicalDevices(mInstance, &deviceCount, nullptr), "vkEnumeratePhysicalDevices");
    if (deviceCount == 0) {
        throw std::runtime_error("no Vulkan physical device is available");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    CheckVk(vkEnumeratePhysicalDevices(mInstance, &deviceCount, devices.data()),
            "vkEnumeratePhysicalDevices");

    std::vector<Oot3d::VulkanAdapterCandidate> candidates;
    candidates.reserve(devices.size());
    for (uint32_t index = 0; index < devices.size(); ++index) {
        const VkPhysicalDevice device = devices[index];
        const QueueFamilies queues = FindQueueFamilies(device);
        const bool hasSwapchain = DeviceSupportsSwapchain(device);
        bool hasFormats = false;
        bool hasPresentModes = false;
        if (queues.Complete() && hasSwapchain) {
            const auto support = QuerySwapchainSupport(device);
            hasFormats = !support.Formats.empty();
            hasPresentModes = !support.PresentModes.empty();
        }
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        Oot3d::VulkanAdapterClass adapterClass =
            Oot3d::VulkanAdapterClass::Other;
        switch (properties.deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
                adapterClass =
                    Oot3d::VulkanAdapterClass::Integrated;
                break;
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
                adapterClass =
                    Oot3d::VulkanAdapterClass::Discrete;
                break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
                adapterClass =
                    Oot3d::VulkanAdapterClass::Virtual;
                break;
            case VK_PHYSICAL_DEVICE_TYPE_CPU:
                adapterClass = Oot3d::VulkanAdapterClass::Cpu;
                break;
            default:
                break;
        }
        candidates.push_back({
            index,
            properties.deviceName,
            adapterClass,
            properties.limits.maxImageDimension2D,
            queues.Graphics.has_value(),
            queues.Present.has_value(),
            hasSwapchain,
            hasFormats,
            hasPresentModes,
            VK_VERSION_MAJOR(properties.apiVersion),
            VK_VERSION_MINOR(properties.apiVersion),
        });
        SPDLOG_INFO(
            "OOT3D Vulkan adapter [{}]: {} (suitable={}, score={})",
            index, properties.deviceName,
            Oot3d::IsVulkanAdapterSuitable(candidates.back()),
            Oot3d::ScoreVulkanAdapter(candidates.back()));
    }

    std::optional<uint32_t> requestedAdapter;
    if (const char* requested =
            std::getenv("OOT3D_GRAPHICS_VULKAN_ADAPTER");
        requested != nullptr && *requested != '\0') {
        const auto parsed =
            Oot3d::ParseVulkanAdapterIndex(requested);
        if (!parsed.Valid())
            throw std::runtime_error(parsed.Reason);
        requestedAdapter = parsed.EnumerationIndex;
    }
    const auto selection =
        Oot3d::SelectVulkanAdapter(candidates, requestedAdapter);
    if (!selection.CandidatePosition.has_value()) {
        throw std::runtime_error(selection.Reason);
    }
    const size_t selectedPosition =
        *selection.CandidatePosition;
    mPhysicalDevice = devices[selectedPosition];
    mVulkanAdapterCount =
        static_cast<uint32_t>(devices.size());
    mVulkanAdapterIndex =
        candidates[selectedPosition].EnumerationIndex;

    const auto queues = FindQueueFamilies(mPhysicalDevice);
    mGraphicsQueueFamily = *queues.Graphics;
    mPresentQueueFamily = *queues.Present;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(mPhysicalDevice, &properties);
    mVulkanAdapterVendorId = properties.vendorID;
    mVulkanAdapterDeviceId = properties.deviceID;
    mNativePicaSupportedSampleCounts =
        properties.limits.framebufferColorSampleCounts &
        properties.limits.framebufferDepthSampleCounts;
    VkPhysicalDeviceDepthStencilResolveProperties resolveProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES};
    VkPhysicalDeviceProperties2 properties2{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties2.pNext = &resolveProperties;
    RequiredInstanceCommand<PFN_vkGetPhysicalDeviceProperties2>(mInstance, "vkGetPhysicalDeviceProperties2")(
        mPhysicalDevice, &properties2);
    if ((resolveProperties.supportedDepthResolveModes &
         VK_RESOLVE_MODE_MIN_BIT) != 0U) {
        mNativePicaDepthResolveMode = VK_RESOLVE_MODE_MIN_BIT;
    } else if ((resolveProperties.supportedDepthResolveModes &
                VK_RESOLVE_MODE_SAMPLE_ZERO_BIT) != 0U) {
        mNativePicaDepthResolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
    }
    mStorageBufferAlignment =
        std::max<VkDeviceSize>(16, properties.limits.minStorageBufferOffsetAlignment);
    mUniformBufferAlignment =
        std::max(mStorageBufferAlignment,
                 std::max<VkDeviceSize>(16, properties.limits.minUniformBufferOffsetAlignment));
    // Release logs keep warnings only; the device is what a tester's report
    // needs first, so it is written at that level and to the console.
    SPDLOG_WARN("OOT3D Vulkan device [{}]: {} (api {}.{}.{}, vendor 0x{:04X}, driver 0x{:08X})",
                mVulkanAdapterIndex, properties.deviceName,
                VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion),
                VK_VERSION_PATCH(properties.apiVersion), properties.vendorID, properties.driverVersion);
    std::fprintf(stderr, "TRIAEVUM_VULKAN_DEVICE index=%u name=\"%s\" api=%u.%u.%u vendor=0x%04X driver=0x%08X\n",
                 mVulkanAdapterIndex, properties.deviceName, VK_VERSION_MAJOR(properties.apiVersion),
                 VK_VERSION_MINOR(properties.apiVersion), VK_VERSION_PATCH(properties.apiVersion),
                 properties.vendorID, properties.driverVersion);
}

void GfxRenderingAPIVulkan::CreateLogicalDevice() {
    const std::array<float, 2> priorities = { 1.0f, 1.0f };
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(mPhysicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(mPhysicalDevice, &queueFamilyCount,
                                             queueFamilyProperties.data());
    const uint32_t graphicsQueueCount =
        mGraphicsQueueFamily < queueFamilyProperties.size()
            ? queueFamilyProperties[mGraphicsQueueFamily].queueCount
            : 0U;
    const char* presentDispatch = std::getenv("TRIAEVUM_VULKAN_PRESENT_DISPATCH");
    const char* videoDriver = SDL_GetCurrentVideoDriver();
    const auto queuePlan = Oot3d::ResolveVulkanQueueTopology(
        mGraphicsQueueFamily, mPresentQueueFamily,
        graphicsQueueCount, Oot3d::ParseVulkanPresentDispatchMode(
            presentDispatch ? presentDispatch : ""), videoDriver ? videoDriver : "");
    mNriSwapchainQueueEligible =
        queuePlan.NriSwapchainEligible;
    std::set<uint32_t> families = { mGraphicsQueueFamily, mPresentQueueFamily };
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (uint32_t family : families) {
        VkDeviceQueueCreateInfo queueInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = family == mGraphicsQueueFamily
            ? queuePlan.RequestedGraphicsQueueCount
            : 1U;
        queueInfo.pQueuePriorities = priorities.data();
        queueInfos.push_back(queueInfo);
    }
    uint32_t extensionCount = 0;
    CheckVk(vkEnumerateDeviceExtensionProperties(mPhysicalDevice, nullptr, &extensionCount, nullptr),
            "vkEnumerateDeviceExtensionProperties");
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    CheckVk(vkEnumerateDeviceExtensionProperties(mPhysicalDevice, nullptr, &extensionCount,
                                                  availableExtensions.data()),
            "vkEnumerateDeviceExtensionProperties");
    const auto hasExtension = [&](const char* name) {
        return std::any_of(availableExtensions.begin(), availableExtensions.end(),
                           [&](const VkExtensionProperties& extension) {
                               return std::strcmp(extension.extensionName, name) == 0;
                           });
    };
    std::vector<const char*> extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkPhysicalDeviceProperties deviceProperties{};
    vkGetPhysicalDeviceProperties(mPhysicalDevice, &deviceProperties);
    const auto& limits = deviceProperties.limits;
    std::fprintf(stderr, "TRIAEVUM_VULKAN_LIMITS push_constants=%u color_attachments=%u sampled_images=%u samplers=%u uniform_buffers=%u storage_images=%u uniform_alignment=%llu storage_alignment=%llu\n",
        limits.maxPushConstantsSize, limits.maxColorAttachments,
        limits.maxPerStageDescriptorSampledImages, limits.maxPerStageDescriptorSamplers,
        limits.maxPerStageDescriptorUniformBuffers, limits.maxPerStageDescriptorStorageImages,
        static_cast<unsigned long long>(limits.minUniformBufferOffsetAlignment),
        static_cast<unsigned long long>(limits.minStorageBufferOffsetAlignment));
    const bool hasVulkan12 = deviceProperties.apiVersion >= VK_API_VERSION_1_2;
    const bool hasSynchronization2Extension =
        hasExtension(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    const bool hasDynamicRenderingExtension =
        hasExtension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR
    };
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRendering{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR
    };
    VkPhysicalDeviceVulkan12Features vulkan12{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES
    };
    VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT pipelineLibraries{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT};
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extendedDynamic{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT};
    pipelineLibraries.pNext = &extendedDynamic;
    dynamicRendering.pNext = &pipelineLibraries;
    vulkan12.pNext = &synchronization2;
    synchronization2.pNext = &dynamicRendering;
    VkPhysicalDeviceFeatures2 supportedFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2
    };
    supportedFeatures.pNext = &vulkan12;
    RequiredInstanceCommand<PFN_vkGetPhysicalDeviceFeatures2>(mInstance, "vkGetPhysicalDeviceFeatures2")(
        mPhysicalDevice, &supportedFeatures);
    mSynchronization2Enabled =
        hasVulkan12 && hasSynchronization2Extension &&
        synchronization2.synchronization2 == VK_TRUE;
    mDynamicRenderingEnabled =
        hasVulkan12 && hasDynamicRenderingExtension &&
        dynamicRendering.dynamicRendering == VK_TRUE;
    const bool librarySupported = Renderer3ds::PicaPipelineLibrariesSupported(
        std::all_of(Renderer3ds::kPicaPipelineLibraryExtensions.begin(),
                    Renderer3ds::kPicaPipelineLibraryExtensions.end(), hasExtension),
        pipelineLibraries, extendedDynamic, supportedFeatures.features);
    VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT libraryProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_PROPERTIES_EXT};
    VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    VkPhysicalDeviceDriverProperties driverProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    driverProperties.pNext = &libraryProperties;
    properties2.pNext = &driverProperties;
    RequiredInstanceCommand<PFN_vkGetPhysicalDeviceProperties2>(mInstance, "vkGetPhysicalDeviceProperties2")(
        mPhysicalDevice, &properties2);
    const auto libraryPolicy = Renderer3ds::ResolvePicaPipelineLibraryPolicy(
        std::getenv("TRIAEVUM_NRI_PIPELINE_LIBRARIES"),
        librarySupported, mDynamicRenderingEnabled,
        mNativePicaPreparationMode == NativePicaPreparationMode::Hybrid ||
            mNativePicaPreparationMode == NativePicaPreparationMode::Uber,
        driverProperties.driverID);
    mGraphicsPipelineLibrariesEnabled = libraryPolicy.Enabled;
    mAllNativePipelinesUseLibraries = libraryPolicy.AllPipelines;
    std::fprintf(stderr, "TRIAEVUM_NRI_GPL_CAPABILITY supported=%u requested=%u enabled=%u fast_link=%u independent_interpolation=%u driver_id=%u policy=%s\n",
        unsigned(librarySupported), unsigned(libraryPolicy.Requested), unsigned(libraryPolicy.Enabled),
        unsigned(libraryProperties.graphicsPipelineLibraryFastLinking),
        unsigned(libraryProperties.graphicsPipelineLibraryIndependentInterpolationDecoration),
        unsigned(driverProperties.driverID), libraryPolicy.Reason);
    if (mGraphicsPipelineLibrariesEnabled)
        extensions.insert(extensions.end(), Renderer3ds::kPicaPipelineLibraryExtensions.begin(),
                          Renderer3ds::kPicaPipelineLibraryExtensions.end());
    if (mSynchronization2Enabled)
        extensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    if (mDynamicRenderingEnabled)
        extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
#if defined(_WIN32) && defined(ENABLE_OOT3D_D3D12_NGX_PROVIDER)
    constexpr const char* kExternalMemoryWin32 =
        "VK_KHR_external_memory_win32";
    constexpr const char* kExternalSemaphoreWin32 =
        "VK_KHR_external_semaphore_win32";
    const bool hasExternalMemoryWin32 =
        hasExtension(kExternalMemoryWin32);
    const bool hasExternalSemaphoreWin32 =
        hasExtension(kExternalSemaphoreWin32);
    mD3d12InteropExtensionsEnabled =
        hasExternalMemoryWin32 && hasExternalSemaphoreWin32 &&
        vulkan12.timelineSemaphore == VK_TRUE;
    if (hasExternalMemoryWin32)
        extensions.push_back(kExternalMemoryWin32);
    if (hasExternalSemaphoreWin32)
        extensions.push_back(kExternalSemaphoreWin32);
#endif
    const auto& ngxRequirements = Oot3d::GetNriNgxVulkanRequirements();
    for (const std::string& required : ngxRequirements.DeviceExtensions) {
        const bool promotedBufferDeviceAddress =
            hasVulkan12 &&
            required == VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME;
        const bool supported =
            promotedBufferDeviceAddress ||
            hasExtension(required.c_str());
        mNgxVulkanRequirementsSatisfied &= supported;
        if (!supported) {
            if (!mNgxVulkanUnavailableReason.empty())
                mNgxVulkanUnavailableReason += "; ";
            mNgxVulkanUnavailableReason +=
                "missing NGX device extension " + required;
        }
        if (supported && !promotedBufferDeviceAddress && std::none_of(
                extensions.begin(), extensions.end(), [&](const char* name) {
                    return required == name;
                }))
            extensions.push_back(required.c_str());
    }
    const auto features = Renderer3ds::PicaVulkanCoreFeatures(supportedFeatures.features);
    // NRI's Vulkan helper and NIS implementation create timeline fences,
    // FP16 shader permutations and update-after-bind image descriptors.
    // Advertise to NRI only features that are also enabled on the wrapped
    // device; otherwise IsUpscalerSupported can return a false positive.
    vulkan12 = Renderer3ds::PicaVulkan12Features(vulkan12);
    mNisVulkanFeaturesEnabled =
        features.shaderStorageImageWriteWithoutFormat == VK_TRUE &&
        vulkan12.timelineSemaphore == VK_TRUE &&
        vulkan12.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE &&
        vulkan12.descriptorBindingStorageImageUpdateAfterBind == VK_TRUE;
    mFsrVulkanFeaturesEnabled =
        features.shaderInt16 == VK_TRUE &&
        vulkan12.timelineSemaphore == VK_TRUE;
    VkDeviceCreateInfo createInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.pEnabledFeatures = &features;
    void* featureChain = nullptr;
    vulkan12.pNext = featureChain;
    featureChain = &vulkan12;
    if (mSynchronization2Enabled) {
        synchronization2.synchronization2 = VK_TRUE;
        synchronization2.pNext = featureChain;
        featureChain = &synchronization2;
    }
    if (mDynamicRenderingEnabled) {
        dynamicRendering.dynamicRendering = VK_TRUE;
        dynamicRendering.pNext = featureChain;
        featureChain = &dynamicRendering;
    }
    if (mGraphicsPipelineLibrariesEnabled) {
        extendedDynamic.pNext = featureChain;
        featureChain = &extendedDynamic;
        pipelineLibraries.pNext = featureChain;
        featureChain = &pipelineLibraries;
    }
    createInfo.pNext = featureChain;
    CheckVk(vkCreateDevice(mPhysicalDevice, &createInfo, nullptr, &mDevice), "vkCreateDevice");
    vkGetDeviceQueue(mDevice, mGraphicsQueueFamily, 0, &mGraphicsQueue);
    vkGetDeviceQueue(mDevice, mPresentQueueFamily,
                     queuePlan.PresentQueueIndex, &mPresentQueue);
    mAsyncPresentSupported =
        queuePlan.AsynchronousPresent &&
        mPresentQueue != mGraphicsQueue;
    mDiagnostics.SetVulkanAdapter({
        mVulkanAdapterCount, mVulkanAdapterIndex,
        mVulkanAdapterVendorId, mVulkanAdapterDeviceId,
        mGraphicsQueueFamily, mPresentQueueFamily,
        graphicsQueueCount,
        queuePlan.RequestedGraphicsQueueCount,
        queuePlan.PresentQueueIndex,
        mAsyncPresentSupported,
        mNriSwapchainQueueEligible,
    });
}

void GfxRenderingAPIVulkan::CreatePipelineCache() {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(mPhysicalDevice, &properties);
    const VulkanPipelineCacheHeader expected =
        MakePipelineCacheHeader(properties);
    const std::filesystem::path path = VulkanPipelineCachePath();
    std::vector<uint8_t> cachedData;
    bool loaded = LoadPipelineCacheData(path, expected, cachedData);

    VkPipelineCacheCreateInfo createInfo{
        VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO
    };
    createInfo.initialDataSize = cachedData.size();
    createInfo.pInitialData = cachedData.empty() ? nullptr : cachedData.data();
    VkResult result = vkCreatePipelineCache(mDevice, &createInfo, nullptr,
                                            &mPipelineCache);
    if (result != VK_SUCCESS && !cachedData.empty()) {
        SPDLOG_WARN(
            "OOT3D Vulkan rejected the persisted pipeline cache (VkResult {}); "
            "rebuilding it",
            static_cast<int>(result));
        createInfo.initialDataSize = 0;
        createInfo.pInitialData = nullptr;
        cachedData.clear();
        loaded = false;
        mPipelineCache = VK_NULL_HANDLE;
        result = vkCreatePipelineCache(mDevice, &createInfo, nullptr,
                                       &mPipelineCache);
    }
    CheckVk(result, "vkCreatePipelineCache");
    SPDLOG_INFO("OOT3D Vulkan pipeline cache: {}{}",
                loaded ? "loaded " : "new",
                loaded ? "(" + std::to_string(cachedData.size()) +
                             " bytes)"
                       : "");
}

void GfxRenderingAPIVulkan::StorePipelineCache() {
    const auto statistics = mNriPicaPipelineBridge.PipelineStatistics();
    mDiagnostics.SetNriPipelineStatistics(statistics.InitialCacheBytes,
        statistics.CreationAttempts, statistics.Created, statistics.CreationNanoseconds);
    mDiagnostics.Flush();
    const auto receipt = nlohmann::json({
        {"initial_cache_bytes", statistics.InitialCacheBytes},
        {"creation_attempts", statistics.CreationAttempts}, {"created", statistics.Created},
        {"owned_reuses", statistics.OwnedReuses},
        {"creation_failures", statistics.CreationFailures}, {"library_fallbacks", statistics.LibraryFallbacks},
        {"creation_nanoseconds", statistics.CreationNanoseconds}}).dump();
    // Release/mobile hosts may suppress INFO and have no per-frame diagnostics enabled.
    std::fprintf(stderr, "TRIAEVUM_NRI_PIPELINE_CACHE %s\n", receipt.c_str());
    std::fprintf(stderr, "TRIAEVUM_PICA_VULKAN_PIPELINES created=%llu\n",
                 static_cast<unsigned long long>(mNativePicaVulkanPipelineCreations));
    std::fprintf(stderr, "TRIAEVUM_PICA_VULKAN_SHADER_PAIRS created=%llu\n",
                 static_cast<unsigned long long>(mNativePicaVulkanShaderPairCreations));
    const auto& copySampler = mGpuTextureCopySamplerCost;
    if (copySampler.Resolutions != 0U) {
        std::fprintf(stderr,
                     "TRIAEVUM_GPU_COPY_SAMPLER resolutions=%llu producer=%llu "
                     "gpu_bound=%llu gpu_bytes=%llu gpu_pixels=%llu native_pixels=%llu "
                     "regions=%llu producer_restores=%llu fallback_unknown=%llu "
                     "fallback_uncaptured=%llu fallback_layout=%llu fallback_mips=%llu "
                     "resolve_ms=%.3f bind_ms=%.3f\n",
                     static_cast<unsigned long long>(copySampler.Resolutions),
                     static_cast<unsigned long long>(copySampler.ProducerResolutions),
                     static_cast<unsigned long long>(copySampler.GpuBound),
                     static_cast<unsigned long long>(copySampler.GpuBytes),
                     static_cast<unsigned long long>(copySampler.GpuExtentPixels),
                     static_cast<unsigned long long>(copySampler.NativeExtentPixels),
                     static_cast<unsigned long long>(copySampler.Regions),
                     static_cast<unsigned long long>(copySampler.ProducerRestores),
                     static_cast<unsigned long long>(copySampler.FallbackUnknownProducer),
                     static_cast<unsigned long long>(copySampler.FallbackUncapturedProducer),
                     static_cast<unsigned long long>(copySampler.FallbackLayout),
                     static_cast<unsigned long long>(copySampler.FallbackMipChain),
                     copySampler.ResolveSeconds * 1000.0,
                     copySampler.BindSeconds * 1000.0);
    }
    if (mRawCopyEncodeVerifyBytes != 0U) {
        std::fprintf(stderr,
                     "TRIAEVUM_RAW_COPY_ENCODE_DIFF bytes=%llu mismatches=%llu\n",
                     static_cast<unsigned long long>(mRawCopyEncodeVerifyBytes),
                     static_cast<unsigned long long>(mRawCopyEncodeVerifyMismatches));
    }
    const auto& copyTiming = mRawCopyTiming;
    std::fprintf(stderr,
                 "TRIAEVUM_RAW_COPY copies=%llu spans=%llu invalidated_textures=%llu "
                 "readback_bytes=%llu writeback_bytes=%llu readback_ms=%.3f encode_ms=%.3f "
                 "invalidate_ms=%.3f span_ms=%.3f commit_ms=%.3f total_ms=%.3f\n",
                 static_cast<unsigned long long>(copyTiming.Copies),
                 static_cast<unsigned long long>(copyTiming.Spans),
                 static_cast<unsigned long long>(copyTiming.InvalidatedTextures),
                 static_cast<unsigned long long>(copyTiming.ReadbackBytes),
                 static_cast<unsigned long long>(copyTiming.WritebackBytes),
                 copyTiming.ReadbackSeconds, copyTiming.EncodeSeconds,
                 copyTiming.InvalidateSeconds, copyTiming.SpanSeconds,
                 copyTiming.CommitSeconds,
                 copyTiming.ReadbackSeconds + copyTiming.EncodeSeconds +
                     copyTiming.InvalidateSeconds + copyTiming.SpanSeconds +
                     copyTiming.CommitSeconds);
    std::fprintf(stderr,
                 "TRIAEVUM_DEFERRED_COPY deferred=%llu materializations=%llu prefix_waits=%llu peak_producers=%llu\n",
                 static_cast<unsigned long long>(copyTiming.DeferredCopies),
                 static_cast<unsigned long long>(copyTiming.Materializations),
                 static_cast<unsigned long long>(copyTiming.PrefixWaits),
                 static_cast<unsigned long long>(copyTiming.PeakLiveProducers));
    const auto nriData = mNriPicaPipelineBridge.GetPipelineCacheData();
    if (!nriData.empty()) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(mPhysicalDevice, &properties);
        const auto nriPath = VulkanPipelineCachePath(true);
        std::string error;
        // stderr as well: the logger may already be quiet at shutdown, and a
        // cache that silently stops being stored costs every later session.
        if (nriData.size() > kMaximumNriPipelineCacheBytes) {
            // Too large to keep growing: drop it, so the next session rebuilds
            // a cache of what it actually uses instead of loading a stale one.
            std::error_code removed;
            std::filesystem::remove(nriPath, removed);
            std::fprintf(stderr, "TRIAEVUM_NRI_PIPELINE_CACHE_STORE result=dropped bytes=%zu limit=%llu\n",
                         nriData.size(), static_cast<unsigned long long>(kMaximumNriPipelineCacheBytes));
            SPDLOG_WARN("NRI PICA pipeline cache: {} bytes over the limit, dropped", nriData.size());
        } else if (StorePipelineCacheData(nriPath, MakePipelineCacheHeader(properties), nriData, &error,
                                          kMaximumNriPipelineCacheBytes)) {
            std::fprintf(stderr, "TRIAEVUM_NRI_PIPELINE_CACHE_STORE result=stored bytes=%zu\n", nriData.size());
            SPDLOG_INFO("NRI PICA pipeline cache: stored {} bytes", nriData.size());
        } else {
            std::fprintf(stderr, "TRIAEVUM_NRI_PIPELINE_CACHE_STORE result=failed bytes=%zu error=%s\n",
                         nriData.size(), error.c_str());
            SPDLOG_WARN("NRI PICA pipeline cache: {}", error);
        }
    }
    if (mPipelineCache == VK_NULL_HANDLE) {
        return;
    }

    size_t dataSize = 0;
    VkResult result =
        vkGetPipelineCacheData(mDevice, mPipelineCache, &dataSize, nullptr);
    if (result != VK_SUCCESS || dataSize == 0 ||
        dataSize > kMaximumPipelineCacheBytes) {
        SPDLOG_WARN(
            "OOT3D Vulkan could not size the pipeline cache (VkResult {}, {} "
            "bytes)",
            static_cast<int>(result), dataSize);
        return;
    }

    std::vector<uint8_t> data(dataSize);
    result = vkGetPipelineCacheData(mDevice, mPipelineCache, &dataSize,
                                    data.data());
    if (result != VK_SUCCESS || dataSize == 0 || dataSize > data.size()) {
        SPDLOG_WARN(
            "OOT3D Vulkan could not read the pipeline cache (VkResult {}, {} "
            "bytes)",
            static_cast<int>(result), dataSize);
        return;
    }
    data.resize(dataSize);

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(mPhysicalDevice, &properties);
    const std::filesystem::path path = VulkanPipelineCachePath();
    std::string error;
    if (StorePipelineCacheData(path, MakePipelineCacheHeader(properties), data, &error))
        SPDLOG_INFO("OOT3D Vulkan pipeline cache: stored {} bytes", data.size());
    else
        SPDLOG_WARN("OOT3D Vulkan pipeline cache: {}", error);
}

void GfxRenderingAPIVulkan::StartPresentWorker() {
    if (!Oot3d::ShouldUseVulkanPresentWorker(
            mNriSwapchain.Active(), mAsyncPresentSupported) ||
        mPresentThread.joinable()) {
        return;
    }
    mPresentWorkerStop = false;
    mPresentThread = std::thread(&GfxRenderingAPIVulkan::PresentWorkerMain, this);
}

void GfxRenderingAPIVulkan::StopPresentWorker() {
    if (!mPresentThread.joinable()) {
        return;
    }
    {
        std::lock_guard lock(mPresentMutex);
        mPresentWorkerStop = true;
    }
    mPresentRequestCondition.notify_all();
    mPresentThread.join();
}

void GfxRenderingAPIVulkan::WaitForFramePresent(uint32_t frameIndex) {
    if (!Oot3d::ShouldUseVulkanPresentWorker(
            mNriSwapchain.Active(), mAsyncPresentSupported) ||
        frameIndex >= kFramesInFlight) {
        return;
    }
    std::unique_lock lock(mPresentMutex);
    const uint64_t serial = mFramePresentSerials[frameIndex];
    mPresentCompleteCondition.wait(lock, [&] {
        return mCompletedPresentSerials[frameIndex] >= serial;
    });
}

void GfxRenderingAPIVulkan::WaitForAllPresents() {
    if (!Oot3d::ShouldUseVulkanPresentWorker(
            mNriSwapchain.Active(), mAsyncPresentSupported) ||
        !mPresentThread.joinable()) {
        return;
    }
    std::unique_lock lock(mPresentMutex);
    mPresentCompleteCondition.wait(lock, [&] {
        for (uint32_t frameIndex = 0; frameIndex < kFramesInFlight; ++frameIndex) {
            if (mCompletedPresentSerials[frameIndex] < mFramePresentSerials[frameIndex]) {
                return false;
            }
        }
        return mPresentRequests.empty();
    });
}

void GfxRenderingAPIVulkan::PresentWorkerMain() {
    for (;;) {
        PresentRequest request;
        {
            std::unique_lock lock(mPresentMutex);
            mPresentRequestCondition.wait(lock, [&] {
                return mPresentWorkerStop || !mPresentRequests.empty();
            });
            if (mPresentWorkerStop && mPresentRequests.empty()) {
                return;
            }
            request = mPresentRequests.front();
            mPresentRequests.pop_front();
        }

        VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &request.WaitSemaphore;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &request.Swapchain;
        presentInfo.pImageIndices = &request.ImageIndex;
        VkResult present;
        {
            std::lock_guard swapchainLock(mSwapchainCallMutex);
            present = vkQueuePresentKHR(mPresentQueue, &presentInfo);
        }
        if (present == VK_ERROR_OUT_OF_DATE_KHR) {
            mPresentSwapchainDirty.store(true);
        } else if (present == VK_SUBOPTIMAL_KHR) {
            mSwapchainSuboptimal.store(true);
        } else if (present != VK_SUCCESS) {
            mPresentError.store(static_cast<int32_t>(present));
        }

        {
            std::lock_guard lock(mPresentMutex);
            mCompletedPresentSerials[request.FrameIndex] = request.Serial;
        }
        mPresentCompleteCondition.notify_all();
    }
}

void GfxRenderingAPIVulkan::CreateCommandResources() {
    VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = mGraphicsQueueFamily;
    CheckVk(vkCreateCommandPool(mDevice, &poolInfo, nullptr, &mCommandPool), "vkCreateCommandPool");

    VkCommandBufferAllocateInfo allocateInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    allocateInfo.commandPool = mCommandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = kFramesInFlight;
    CheckVk(vkAllocateCommandBuffers(mDevice, &allocateInfo, mCommandBuffers.data()),
            "vkAllocateCommandBuffers");
    CheckVk(vkAllocateCommandBuffers(
                mDevice, &allocateInfo,
                mContinuationCommandBuffers.data()),
            "vkAllocateCommandBuffers(continuation)");
}

void GfxRenderingAPIVulkan::SubmitFrameCommandBuffer(
    VkCommandBuffer command, VkFence fence,
    VkSemaphore signal, uint64_t signalValue) {
    std::array<VkSemaphore, 2> waits{};
    std::array<VkPipelineStageFlags, 2> stages{};
    std::array<uint64_t, 2> values{};
    uint32_t waitCount = 0U;
    if (mFrameAcquirePending) {
        waits[waitCount] = mImageAvailableSemaphores[mCurrentFrame];
        stages[waitCount++] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
    const bool externalWait = mFrameExternalWaitSemaphore != VK_NULL_HANDLE;
    if (externalWait) {
        waits[waitCount] = mFrameExternalWaitSemaphore;
        values[waitCount] = mFrameExternalWaitValue;
        stages[waitCount++] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
    VkTimelineSemaphoreSubmitInfo timeline{
        VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    timeline.waitSemaphoreValueCount = waitCount;
    timeline.pWaitSemaphoreValues = values.data();
    timeline.signalSemaphoreValueCount = signal != VK_NULL_HANDLE ? 1U : 0U;
    timeline.pSignalSemaphoreValues = &signalValue;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.pNext = externalWait || signalValue != 0U ? &timeline : nullptr;
    submit.waitSemaphoreCount = waitCount;
    submit.pWaitSemaphores = waits.data();
    submit.pWaitDstStageMask = stages.data();
    submit.commandBufferCount = 1U;
    submit.pCommandBuffers = &command;
    submit.signalSemaphoreCount = timeline.signalSemaphoreValueCount;
    submit.pSignalSemaphores = &signal;
    CheckVk(vkQueueSubmit(mGraphicsQueue, 1U, &submit, fence),
            "vkQueueSubmit(frame)");
    // The first prefix owns acquisition; later prefixes and the final submit
    // must not wait a second time on the same binary semaphore.
    mFrameAcquirePending = false;
    mFrameExternalWaitSemaphore = VK_NULL_HANDLE;
    mFrameExternalWaitValue = 0U;
}

VkCommandBuffer GfxRenderingAPIVulkan::SplitFrameForExternalCompute(
    VkSemaphore timelineSemaphore, uint64_t vulkanSignalValue,
    uint64_t externalCompletionValue) {
    if (!mFrameActive || mFrameExternalComputeSplit ||
        mRenderPassActive || mNativePicaRenderPassActive ||
        timelineSemaphore == VK_NULL_HANDLE ||
        vulkanSignalValue == 0U ||
        externalCompletionValue <= vulkanSignalValue) {
        return VK_NULL_HANDLE;
    }

    VkCommandBuffer prefix = mCommandBuffers[mCurrentFrame];
    CheckVk(vkEndCommandBuffer(prefix),
            "vkEndCommandBuffer(external compute prefix)");
    SubmitFrameCommandBuffer(prefix, VK_NULL_HANDLE,
                             timelineSemaphore, vulkanSignalValue);

    std::swap(mCommandBuffers[mCurrentFrame],
              mContinuationCommandBuffers[mCurrentFrame]);
    VkCommandBuffer continuation = mCommandBuffers[mCurrentFrame];
    CheckVk(vkResetCommandBuffer(continuation, 0U),
            "vkResetCommandBuffer(external compute continuation)");
    VkCommandBufferBeginInfo begin{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CheckVk(vkBeginCommandBuffer(continuation, &begin),
            "vkBeginCommandBuffer(external compute continuation)");
    if (mNriInterop.Available() &&
        !mNriInterop.WrapFrameCommandBuffer(
            mCurrentFrame, continuation)) {
        SPDLOG_WARN(
            "OOT3D Vulkan NRI could not wrap external-compute continuation {}",
            mCurrentFrame);
    }
    mFrameExternalComputeSplit = true;
    mFrameExternalWaitSemaphore = timelineSemaphore;
    mFrameExternalWaitValue = externalCompletionValue;
    return continuation;
}

void GfxRenderingAPIVulkan::CreateSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t index = 0; index < kFramesInFlight; ++index) {
        if (!mNriSwapchain.Supported()) {
            CheckVk(vkCreateSemaphore(
                        mDevice, &semaphoreInfo, nullptr,
                        &mImageAvailableSemaphores[index]),
                    "vkCreateSemaphore(image available)");
        }
        CheckVk(vkCreateFence(mDevice, &fenceInfo, nullptr, &mInFlightFences[index]),
                "vkCreateFence");
    }
    CheckVk(vkCreateFence(mDevice, &fenceInfo, nullptr, &mRawCopyFence),
            "vkCreateFence(raw copy)");
}

void GfxRenderingAPIVulkan::CreateNoPresentTargets(VkFormat format,
                                                   VkExtent2D extent) {
    const uint32_t count = kFramesInFlight;
    mSwapchainImages.assign(count, VK_NULL_HANDLE);
    mSwapchainImageViews.assign(count, VK_NULL_HANDLE);
    mNoPresentMemories.assign(count, VK_NULL_HANDLE);
    for (uint32_t index = 0; index < count; ++index) {
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = {extent.width, extent.height, 1U};
        imageInfo.mipLevels = 1U;
        imageInfo.arrayLayers = 1U;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                          VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        CheckVk(vkCreateImage(mDevice, &imageInfo, nullptr,
                              &mSwapchainImages[index]),
                "vkCreateImage(no-present target)");
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(mDevice, mSwapchainImages[index],
                                     &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = FindMemoryType(
            requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        CheckVk(vkAllocateMemory(mDevice, &allocation, nullptr,
                                 &mNoPresentMemories[index]),
                "vkAllocateMemory(no-present target)");
        CheckVk(vkBindImageMemory(mDevice, mSwapchainImages[index],
                                  mNoPresentMemories[index], 0),
                "vkBindImageMemory(no-present target)");
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = mSwapchainImages[index];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        CheckVk(vkCreateImageView(mDevice, &viewInfo, nullptr,
                                  &mSwapchainImageViews[index]),
                "vkCreateImageView(no-present target)");
    }
    // No binary semaphores: nothing acquires or presents these images.
    mRenderFinishedSemaphores.assign(count, VK_NULL_HANDLE);
    mSwapchainFormat = format;
    mSwapchainExtent = extent;
}

void GfxRenderingAPIVulkan::CreateSwapchainResources() {
    const auto support = QuerySwapchainSupport(mPhysicalDevice);
    const VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(support.Formats);
    const VkPresentModeKHR presentMode = ChoosePresentMode(support.PresentModes);
    const VkExtent2D extent = ChooseExtent(support.Capabilities);
    if (extent.width == 0 || extent.height == 0) {
        mSwapchainDirty = true;
        return;
    }

    uint32_t imageCount = support.Capabilities.minImageCount + 1;
    if (support.Capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, support.Capabilities.maxImageCount);
    }
    const char* noPresent = std::getenv("TRIAEVUM_BENCHMARK_NO_PRESENT");
    mBenchmarkNoPresent = noPresent != nullptr && std::string_view(noPresent) == "1";
    const bool nriCreated =
        !mBenchmarkNoPresent && mNriSwapchain.Supported() &&
        mNriSwapchain.Create({
            ResolveNriNativeWindow(mWindowBackend),
            extent.width,
            extent.height,
            imageCount,
            kFramesInFlight,
            mVsyncEnabled,
            !mVsyncEnabled && mWindowBackend->CanDisableVsync(),
        });
    if (nriCreated) {
        mDiagnostics.SetVulkanPresentationFallback(
            Oot3dVulkanPresentationFallbackReason::None, {});
        for (VkSemaphore& semaphore : mImageAvailableSemaphores) {
            if (semaphore != VK_NULL_HANDLE)
                vkDestroySemaphore(mDevice, semaphore, nullptr);
            semaphore = VK_NULL_HANDLE;
        }
        mSwapchainFormat = mNriSwapchain.Format();
        mSwapchainExtent = {
            mNriSwapchain.Width(), mNriSwapchain.Height()};
        const auto images = mNriSwapchain.Images();
        imageCount = static_cast<uint32_t>(images.size());
        mSwapchainImages.reserve(imageCount);
        mSwapchainImageViews.reserve(imageCount);
        for (const auto& image : images) {
            mSwapchainImages.push_back(image.Image);
            mSwapchainImageViews.push_back(
                image.ColorAttachmentView);
        }
        for (uint32_t index = 0; index < kFramesInFlight; ++index)
            mImageAvailableSemaphores[index] =
                mNriSwapchain.AcquireSemaphore(index);
        mRenderFinishedSemaphores.resize(
            imageCount, VK_NULL_HANDLE);
        for (uint32_t index = 0; index < imageCount; ++index)
            mRenderFinishedSemaphores[index] =
                mNriSwapchain.ReleaseSemaphore(index);
    } else if (mBenchmarkNoPresent) {
        SPDLOG_WARN("OOT3D Vulkan benchmark no-present mode: frames are rendered "
                    "but never presented");
        CreateNoPresentTargets(surfaceFormat.format, extent);
        imageCount = static_cast<uint32_t>(mSwapchainImages.size());
    } else {
        if (mNriSwapchain.Supported()) {
            mDiagnostics.SetVulkanPresentationFallback(
                Oot3dVulkanPresentationFallbackReason::
                    NriCreationFailed,
                mNriSwapchain.UnavailableReason());
            SPDLOG_WARN(
                "OOT3D NRI swapchain creation failed; using Vulkan: {}",
                mNriSwapchain.UnavailableReason());
        }
        VkSemaphoreCreateInfo semaphoreInfo{
            VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (VkSemaphore& semaphore : mImageAvailableSemaphores) {
            if (semaphore == VK_NULL_HANDLE) {
                CheckVk(vkCreateSemaphore(
                            mDevice, &semaphoreInfo, nullptr, &semaphore),
                        "vkCreateSemaphore(image available fallback)");
            }
        }

        VkSwapchainCreateInfoKHR createInfo{
            VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        createInfo.surface = mSurface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        const uint32_t queueFamilies[] = {
            mGraphicsQueueFamily, mPresentQueueFamily};
        if (mGraphicsQueueFamily != mPresentQueueFamily) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilies;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        // Scanout is in logical window coordinates, not pre-rotated display
        // coordinates. Let the compositor apply the surface transform.
        createInfo.preTransform =
            (support.Capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                : support.Capabilities.currentTransform;
        SPDLOG_INFO("Vulkan surface: {}x{}, transform {}, preTransform {}",
                    extent.width, extent.height,
                    static_cast<uint32_t>(support.Capabilities.currentTransform),
                    static_cast<uint32_t>(createInfo.preTransform));
        createInfo.compositeAlpha =
            (support.Capabilities.supportedCompositeAlpha &
             VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                : static_cast<VkCompositeAlphaFlagBitsKHR>(
                      support.Capabilities.supportedCompositeAlpha &
                      (~support.Capabilities.supportedCompositeAlpha + 1));
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        CheckVk(vkCreateSwapchainKHR(
                    mDevice, &createInfo, nullptr, &mSwapchain),
                "vkCreateSwapchainKHR");

        CheckVk(vkGetSwapchainImagesKHR(
                    mDevice, mSwapchain, &imageCount, nullptr),
                "vkGetSwapchainImagesKHR(count)");
        mSwapchainImages.resize(imageCount);
        CheckVk(vkGetSwapchainImagesKHR(
                    mDevice, mSwapchain, &imageCount,
                    mSwapchainImages.data()),
                "vkGetSwapchainImagesKHR");
        mRenderFinishedSemaphores.resize(
            imageCount, VK_NULL_HANDLE);
        VkSemaphoreCreateInfo renderFinishedInfo{
            VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (VkSemaphore& semaphore : mRenderFinishedSemaphores) {
            CheckVk(vkCreateSemaphore(
                        mDevice, &renderFinishedInfo, nullptr, &semaphore),
                    "vkCreateSemaphore(render finished image)");
        }
        mSwapchainFormat = surfaceFormat.format;
        mSwapchainExtent = extent;
        mSwapchainImageViews.resize(imageCount);
        for (size_t index = 0;
             index < mSwapchainImages.size(); ++index) {
            VkImageViewCreateInfo viewInfo{
                VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            viewInfo.image = mSwapchainImages[index];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = mSwapchainFormat;
            viewInfo.subresourceRange.aspectMask =
                VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.layerCount = 1;
            CheckVk(vkCreateImageView(
                        mDevice, &viewInfo, nullptr,
                        &mSwapchainImageViews[index]),
                    "vkCreateImageView(swapchain)");
        }
    }

    mDepthFormat = FindDepthFormat();
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = mSwapchainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = mDepthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference colorReference{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthReference{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = dependency.srcStageMask;
    // Discarding depth contents does not discard prior depth writes. Both the
    // clear pass and the load-color overlay reuse this attachment in one frame.
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    const VkAttachmentDescription attachments[] = { colorAttachment, depthAttachment };
    VkRenderPassCreateInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    renderPassInfo.attachmentCount = 2;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    CheckVk(vkCreateRenderPass(mDevice, &renderPassInfo, nullptr, &mRenderPass), "vkCreateRenderPass");

    VkAttachmentDescription overlayColorAttachment = colorAttachment;
    overlayColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    overlayColorAttachment.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentDescription overlayDepthAttachment = depthAttachment;
    overlayDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    overlayDepthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    const VkAttachmentDescription overlayAttachments[] = {
        overlayColorAttachment, overlayDepthAttachment
    };
    VkRenderPassCreateInfo overlayRenderPassInfo = renderPassInfo;
    overlayRenderPassInfo.pAttachments = overlayAttachments;
    CheckVk(vkCreateRenderPass(mDevice, &overlayRenderPassInfo, nullptr,
                               &mOverlayRenderPass),
            "vkCreateRenderPass(overlay)");

    CreateDepthResources();
    mSwapchainFramebuffers.resize(imageCount);
    for (size_t index = 0; index < imageCount; ++index) {
        const VkImageView attachmentsForFramebuffer[] = {
            mSwapchainImageViews[index], mDepthImageViews[index]
        };
        VkFramebufferCreateInfo framebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        framebufferInfo.renderPass = mRenderPass;
        framebufferInfo.attachmentCount = 2;
        framebufferInfo.pAttachments = attachmentsForFramebuffer;
        framebufferInfo.width = mSwapchainExtent.width;
        framebufferInfo.height = mSwapchainExtent.height;
        framebufferInfo.layers = 1;
        CheckVk(vkCreateFramebuffer(mDevice, &framebufferInfo, nullptr,
                                    &mSwapchainFramebuffers[index]),
                "vkCreateFramebuffer");
    }
    mImagesInFlight.assign(imageCount, VK_NULL_HANDLE);
    mSwapchainSurfaceCapabilities = support.Capabilities;
    mSwapchainSurfaceFormat = surfaceFormat;
    mSwapchainSuboptimal.store(false);
    mSwapchainDirty = false;
    SPDLOG_INFO(
        "OOT3D {} swapchain: {}x{}, {} images, format {}, present mode {}",
        nriCreated ? "NRI" : "Vulkan",
        mSwapchainExtent.width, mSwapchainExtent.height, imageCount,
        static_cast<int>(mSwapchainFormat),
        nriCreated ? -1 : static_cast<int>(presentMode));
}

void GfxRenderingAPIVulkan::DestroySwapchainResources() {
    if (mDevice == VK_NULL_HANDLE) {
        return;
    }
    ShutdownImGuiBackend();
    DestroyPresentationPipelines();
    for (VkFramebuffer framebuffer : mSwapchainFramebuffers) {
        vkDestroyFramebuffer(mDevice, framebuffer, nullptr);
    }
    mSwapchainFramebuffers.clear();
    const bool nriOwned = mNriSwapchain.Active();
    if (!nriOwned) {
        for (VkSemaphore semaphore : mRenderFinishedSemaphores) {
            if (semaphore != VK_NULL_HANDLE)
                vkDestroySemaphore(mDevice, semaphore, nullptr);
        }
    }
    mRenderFinishedSemaphores.clear();
    if (mOverlayRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(mDevice, mOverlayRenderPass, nullptr);
        mOverlayRenderPass = VK_NULL_HANDLE;
    }
    if (mRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(mDevice, mRenderPass, nullptr);
        mRenderPass = VK_NULL_HANDLE;
    }
    for (VkImageView view : mDepthImageViews) {
        vkDestroyImageView(mDevice, view, nullptr);
    }
    for (VkImage image : mDepthImages) {
        vkDestroyImage(mDevice, image, nullptr);
    }
    for (VkDeviceMemory memory : mDepthMemories) {
        vkFreeMemory(mDevice, memory, nullptr);
    }
    mDepthImageViews.clear();
    mDepthImages.clear();
    mDepthMemories.clear();
    mNriPicaScanoutPass.ResetTargets();
    for (VkImage image : mSwapchainImages)
        mNriInterop.ForgetTexture(image);
    if (!nriOwned) {
        for (VkImageView view : mSwapchainImageViews)
            vkDestroyImageView(mDevice, view, nullptr);
    }
    if (!mNoPresentMemories.empty()) {
        for (VkImage image : mSwapchainImages)
            vkDestroyImage(mDevice, image, nullptr);
        for (VkDeviceMemory memory : mNoPresentMemories)
            vkFreeMemory(mDevice, memory, nullptr);
        mNoPresentMemories.clear();
    }
    mSwapchainImageViews.clear();
    mSwapchainImages.clear();
    if (mSwapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(mDevice, mSwapchain, nullptr);
        mSwapchain = VK_NULL_HANDLE;
    }
    if (nriOwned) {
        mImageAvailableSemaphores.fill(VK_NULL_HANDLE);
        mNriSwapchain.Destroy();
    }
    mImagesInFlight.clear();
}

bool GfxRenderingAPIVulkan::SwapchainSurfaceChanged() const {
    const auto support = QuerySwapchainSupport(mPhysicalDevice);
    const auto extent = ChooseExtent(support.Capabilities);
    const auto format = ChooseSurfaceFormat(support.Formats);
    // SUBOPTIMAL still permits presentation. In particular, compositor rotation
    // can report it forever; rebuilding an identical configuration cannot help.
    return extent.width != mSwapchainExtent.width || extent.height != mSwapchainExtent.height ||
           format.format != mSwapchainSurfaceFormat.format ||
           format.colorSpace != mSwapchainSurfaceFormat.colorSpace ||
           support.Capabilities.currentTransform != mSwapchainSurfaceCapabilities.currentTransform ||
           support.Capabilities.supportedTransforms != mSwapchainSurfaceCapabilities.supportedTransforms ||
           support.Capabilities.minImageCount != mSwapchainSurfaceCapabilities.minImageCount ||
           support.Capabilities.maxImageCount != mSwapchainSurfaceCapabilities.maxImageCount;
}

void GfxRenderingAPIVulkan::RecreateSwapchain() {
    uint32_t width = 0;
    uint32_t height = 0;
    int32_t x = 0;
    int32_t y = 0;
    mWindowBackend->GetDimensions(&width, &height, &x, &y);
    if (width == 0 || height == 0) {
        mSwapchainDirty = true;
        return;
    }
    WaitForAllPresents();
    StopPresentWorker();
    CheckVk(vkDeviceWaitIdle(mDevice), "vkDeviceWaitIdle(recreate swapchain)");
    // Native PICA targets are offscreen scene resources, not swapchain
    // resources. Preserve them (and AO depth bindings) across presentation
    // mode changes; their own reset path handles genuine scene invalidation.
    DestroySwapchainResources();
    CreateSwapchainResources();
    StartPresentWorker();
    if (mNriPicaScanoutPass.Available() &&
        !mNriPicaScanoutPass.Configure(mSwapchainFormat)) {
        SPDLOG_WARN("OOT3D NRI scanout reconfigure failed: {}",
                    mNriPicaScanoutPass.UnavailableReason());
    }
}

GfxRenderingAPIVulkan::QueueFamilies
GfxRenderingAPIVulkan::FindQueueFamilies(VkPhysicalDevice device) const {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties.data());
    std::vector<Oot3d::VulkanQueueFamilyCandidate> candidates;
    candidates.reserve(properties.size());
    for (uint32_t index = 0; index < count; ++index) {
        VkBool32 present = VK_FALSE;
        CheckVk(vkGetPhysicalDeviceSurfaceSupportKHR(device, index, mSurface, &present),
                "vkGetPhysicalDeviceSurfaceSupportKHR");
        candidates.push_back({
            index,
            properties[index].queueCount,
            (properties[index].queueFlags &
             VK_QUEUE_GRAPHICS_BIT) != 0,
            present == VK_TRUE,
        });
    }
    const auto selected =
        Oot3d::SelectVulkanQueueFamilies(candidates);
    QueueFamilies result;
    result.Graphics = selected.GraphicsFamily;
    result.Present = selected.PresentFamily;
    return result;
}

GfxRenderingAPIVulkan::SwapchainSupport
GfxRenderingAPIVulkan::QuerySwapchainSupport(VkPhysicalDevice device) const {
    SwapchainSupport result;
    CheckVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, mSurface, &result.Capabilities),
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    uint32_t formatCount = 0;
    CheckVk(vkGetPhysicalDeviceSurfaceFormatsKHR(device, mSurface, &formatCount, nullptr),
            "vkGetPhysicalDeviceSurfaceFormatsKHR(count)");
    result.Formats.resize(formatCount);
    if (formatCount > 0) {
        CheckVk(vkGetPhysicalDeviceSurfaceFormatsKHR(device, mSurface, &formatCount,
                                                     result.Formats.data()),
                "vkGetPhysicalDeviceSurfaceFormatsKHR");
    }
    uint32_t modeCount = 0;
    CheckVk(vkGetPhysicalDeviceSurfacePresentModesKHR(device, mSurface, &modeCount, nullptr),
            "vkGetPhysicalDeviceSurfacePresentModesKHR(count)");
    result.PresentModes.resize(modeCount);
    if (modeCount > 0) {
        CheckVk(vkGetPhysicalDeviceSurfacePresentModesKHR(device, mSurface, &modeCount,
                                                          result.PresentModes.data()),
                "vkGetPhysicalDeviceSurfacePresentModesKHR");
    }
    return result;
}

bool GfxRenderingAPIVulkan::DeviceSupportsSwapchain(VkPhysicalDevice device) const {
    uint32_t extensionCount = 0;
    CheckVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr),
            "vkEnumerateDeviceExtensionProperties(count)");
    std::vector<VkExtensionProperties> extensions(extensionCount);
    CheckVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data()),
            "vkEnumerateDeviceExtensionProperties");
    return std::any_of(extensions.begin(), extensions.end(), [](const VkExtensionProperties& extension) {
        return std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
    });
}

VkSurfaceFormatKHR GfxRenderingAPIVulkan::ChooseSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& formats) const {
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats.front();
}

VkPresentModeKHR GfxRenderingAPIVulkan::ChoosePresentMode(
    const std::vector<VkPresentModeKHR>& modes) const {
    if (mVsyncEnabled) {
        return VK_PRESENT_MODE_FIFO_KHR;
    }
    if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end()) {
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end()) {
        return VK_PRESENT_MODE_MAILBOX_KHR;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D GfxRenderingAPIVulkan::ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }
    uint32_t width = 0;
    uint32_t height = 0;
    int32_t x = 0;
    int32_t y = 0;
    mWindowBackend->GetDimensions(&width, &height, &x, &y);
    return {
        std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
        std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
    };
}

VkFormat GfxRenderingAPIVulkan::FindDepthFormat() const {
    const auto format = Oot3d::FindPicaDepthFormat(mPhysicalDevice);
    std::fprintf(stderr, "TRIAEVUM_VULKAN_DEPTH_FORMAT format=%u\n", unsigned(format));
    return format;
}

uint32_t GfxRenderingAPIVulkan::FindMemoryType(uint32_t typeFilter,
                                               VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(mPhysicalDevice, &memoryProperties);
    for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
        if ((typeFilter & (1u << index)) != 0 &&
            (memoryProperties.memoryTypes[index].propertyFlags & properties) == properties) {
            return index;
        }
    }
    throw std::runtime_error("Vulkan memory type was not found");
}

uint32_t GfxRenderingAPIVulkan::FindReadbackMemoryType(uint32_t typeFilter) const {
    // CPU reads of GPU-written staging are the dominant cost of the ordered raw
    // copy when the staging lands in write-combined memory (measured 25.5 ms
    // per copy, ~289 MB/s). Prefer cached host-visible memory for readbacks.
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(mPhysicalDevice, &memoryProperties);
    constexpr VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    const VkMemoryPropertyFlags preferences[] = {
        VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
    for (const VkMemoryPropertyFlags preferred : preferences) {
        for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
            const VkMemoryPropertyFlags flags =
                memoryProperties.memoryTypes[index].propertyFlags;
            if ((typeFilter & (1u << index)) != 0 &&
                (flags & required) == required &&
                (flags & preferred) == preferred) {
                return index;
            }
        }
    }
    throw std::runtime_error("Vulkan readback memory type was not found");
}

GfxRenderingAPIVulkan::BufferAllocation GfxRenderingAPIVulkan::CreateReadbackBuffer(
    VkDeviceSize size, bool* coherent) {
    BufferAllocation buffer{};
    buffer.Size = size;
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    CheckVk(vkCreateBuffer(mDevice, &bufferInfo, nullptr, &buffer.Buffer),
            "vkCreateBuffer(readback)");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(mDevice, buffer.Buffer, &requirements);
    const uint32_t typeIndex = FindReadbackMemoryType(requirements.memoryTypeBits);
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(mPhysicalDevice, &memoryProperties);
    if (coherent != nullptr) {
        *coherent =
            (memoryProperties.memoryTypes[typeIndex].propertyFlags &
             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    }
    VkMemoryAllocateInfo allocationInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex = typeIndex;
    CheckVk(vkAllocateMemory(mDevice, &allocationInfo, nullptr, &buffer.Memory),
            "vkAllocateMemory(readback)");
    CheckVk(vkBindBufferMemory(mDevice, buffer.Buffer, buffer.Memory, 0),
            "vkBindBufferMemory(readback)");
    CheckVk(vkMapMemory(mDevice, buffer.Memory, 0, size, 0, &buffer.Mapped),
            "vkMapMemory(readback)");
    return buffer;
}

void GfxRenderingAPIVulkan::CreateDepthResources() {
    const size_t count = mSwapchainImages.size();
    mDepthImages.resize(count);
    mDepthMemories.resize(count);
    mDepthImageViews.resize(count);
    for (size_t index = 0; index < count; ++index) {
        VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { mSwapchainExtent.width, mSwapchainExtent.height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = mDepthFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        CheckVk(vkCreateImage(mDevice, &imageInfo, nullptr, &mDepthImages[index]),
                "vkCreateImage(depth)");
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(mDevice, mDepthImages[index], &requirements);
        VkMemoryAllocateInfo allocationInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocationInfo.allocationSize = requirements.size;
        allocationInfo.memoryTypeIndex =
            FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        CheckVk(vkAllocateMemory(mDevice, &allocationInfo, nullptr, &mDepthMemories[index]),
                "vkAllocateMemory(depth)");
        CheckVk(vkBindImageMemory(mDevice, mDepthImages[index], mDepthMemories[index], 0),
                "vkBindImageMemory(depth)");
        VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image = mDepthImages[index];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = mDepthFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (HasStencil(mDepthFormat)) {
            viewInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        CheckVk(vkCreateImageView(mDevice, &viewInfo, nullptr, &mDepthImageViews[index]),
                "vkCreateImageView(depth)");
    }
}

void GfxRenderingAPIVulkan::BeginRenderPassIfNeeded() {
    if (!mFrameActive || mRenderPassActive || mOot3dShadow2dPassActive ||
        mSwapchainFramebuffers.empty()) {
        return;
    }
    VkClearValue clearValues[2]{};
    clearValues[0].color.float32[0] = mClearColor[0];
    clearValues[0].color.float32[1] = mClearColor[1];
    clearValues[0].color.float32[2] = mClearColor[2];
    clearValues[0].color.float32[3] = mClearColor[3];
    clearValues[1].depthStencil = { 1.0f, 0 };
    VkRenderPassBeginInfo beginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    beginInfo.renderPass = mRenderPass;
    beginInfo.framebuffer = mSwapchainFramebuffers[mCurrentImage];
    beginInfo.renderArea.extent = mSwapchainExtent;
    beginInfo.clearValueCount = 2;
    beginInfo.pClearValues = clearValues;
    vkCmdBeginRenderPass(mCommandBuffers[mCurrentFrame], &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
    mRenderPassActive = true;
    mOverlayRenderPassActive = false;
    ApplyDynamicViewportAndScissor();
}

void GfxRenderingAPIVulkan::ApplyDynamicViewportAndScissor() {
    if (!mFrameActive || !mRenderPassActive) {
        return;
    }
    if (mViewportSet) {
        vkCmdSetViewport(mCommandBuffers[mCurrentFrame], 0, 1, &mViewport);
    }
    if (mScissorSet) {
        VkRect2D clamped = mScissor;
        clamped.extent.width = std::min(clamped.extent.width, mSwapchainExtent.width);
        clamped.extent.height = std::min(clamped.extent.height, mSwapchainExtent.height);
        vkCmdSetScissor(mCommandBuffers[mCurrentFrame], 0, 1, &clamped);
    }
}

} // namespace Fast

#endif
