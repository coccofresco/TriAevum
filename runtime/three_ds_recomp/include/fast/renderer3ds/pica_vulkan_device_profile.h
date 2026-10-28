#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <string_view>
#include <vector>

namespace Fast::Renderer3ds {

// Preparation and live rendering must use the same Vulkan application contract.
// Drivers may expose different pipeline-cache UUIDs for different API versions.
inline VkApplicationInfo PicaVulkanApplicationInfo() {
    VkApplicationInfo result{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    result.pApplicationName = "TriAevum";
    result.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    result.pEngineName = "ThreeDsRecomp Runtime";
    result.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    result.apiVersion = VK_API_VERSION_1_2;
    return result;
}

// AMD's Windows driver (amdvlk64) faults the GPU within the first frames when
// monolithic native pipelines run without robust buffer access: a
// page-aligned read_invalid that robustBufferAccess alone removes (issues
// #78/#84, measured on an RX 7900 XT, driver 2.0.395). Library-linked
// pipelines did not fault. The live device and Forge's device preparation
// enable the same features, so prepared pipelines stay cache hits.
inline bool PicaRobustBufferAccessRequired(VkDriverId driver) {
    return driver == VK_DRIVER_ID_AMD_PROPRIETARY;
}

inline VkPhysicalDeviceFeatures PicaVulkanCoreFeatures(const VkPhysicalDeviceFeatures& supported, VkDriverId driver) {
    VkPhysicalDeviceFeatures enabled{};
    if (PicaRobustBufferAccessRequired(driver))
        enabled.robustBufferAccess = supported.robustBufferAccess;
    enabled.independentBlend = supported.independentBlend;
    enabled.multiViewport = supported.multiViewport;
    // Fragment primitive IDs address opt-in per-primitive seam metadata.
    enabled.geometryShader = supported.geometryShader;
    enabled.shaderImageGatherExtended = supported.shaderImageGatherExtended;
    enabled.shaderStorageImageWriteWithoutFormat = supported.shaderStorageImageWriteWithoutFormat;
    enabled.shaderStorageImageExtendedFormats = supported.shaderStorageImageExtendedFormats;
    // Native fragment-operation programs write the storage-image target.
    enabled.fragmentStoresAndAtomics = supported.fragmentStoresAndAtomics;
    enabled.shaderInt16 = supported.shaderInt16;
    // The grass GPU selection writes indirect draws whose firstInstance is
    // the slot's first root; without the feature it must be zero.
    enabled.drawIndirectFirstInstance = supported.drawIndirectFirstInstance;
    return enabled;
}

inline VkPhysicalDeviceVulkan12Features PicaVulkan12Features(const VkPhysicalDeviceVulkan12Features& supported) {
    VkPhysicalDeviceVulkan12Features enabled{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    enabled.bufferDeviceAddress = supported.bufferDeviceAddress;
    enabled.timelineSemaphore = supported.timelineSemaphore;
    enabled.shaderFloat16 = supported.shaderFloat16;
    enabled.descriptorBindingSampledImageUpdateAfterBind = supported.descriptorBindingSampledImageUpdateAfterBind;
    enabled.descriptorBindingStorageImageUpdateAfterBind = supported.descriptorBindingStorageImageUpdateAfterBind;
    // FidelityFX Brixelizer binds its geometry arenas and bricks through
    // partially bound, non-uniformly indexed descriptor arrays.
    enabled.descriptorBindingPartiallyBound = supported.descriptorBindingPartiallyBound;
    enabled.runtimeDescriptorArray = supported.runtimeDescriptorArray;
    enabled.shaderStorageBufferArrayNonUniformIndexing = supported.shaderStorageBufferArrayNonUniformIndexing;
    enabled.shaderSampledImageArrayNonUniformIndexing = supported.shaderSampledImageArrayNonUniformIndexing;
    enabled.shaderStorageImageArrayNonUniformIndexing = supported.shaderStorageImageArrayNonUniformIndexing;
    return enabled;
}

// Graphics pipeline libraries: always-available programs link prepared parts,
// and the extended dynamic state keeps raster and depth state out of them.
// The live device and Forge's device preparation negotiate the same set (it
// changes the pipelines NRI creates, hence their cache identity).
inline constexpr std::array<const char*, 3> kPicaPipelineLibraryExtensions{
    VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME,
    VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
    VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
};

inline bool PicaPipelineLibrariesSupported(
    bool extensionsAvailable,
    const VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT& libraries,
    const VkPhysicalDeviceExtendedDynamicStateFeaturesEXT& dynamicState,
    const VkPhysicalDeviceFeatures& core) {
    return extensionsAvailable && libraries.graphicsPipelineLibrary == VK_TRUE &&
           dynamicState.extendedDynamicState == VK_TRUE && core.fragmentStoresAndAtomics == VK_TRUE;
}

// Extended dynamic state is negotiated whenever the device offers it, with or
// without libraries, so turning libraries off (by driver policy or
// TRIAEVUM_NRI_PIPELINE_LIBRARIES=0) no longer also moves NRI onto its static
// fallback (fixed vertex strides, maximum viewport count). Monolithic
// pipelines then use the same dynamic NRI path as library pipelines.
inline bool PicaExtendedDynamicStateSupported(
    bool extensionAvailable, const VkPhysicalDeviceExtendedDynamicStateFeaturesEXT& dynamicState) {
    return extensionAvailable && dynamicState.extendedDynamicState == VK_TRUE;
}

// The pipeline-state device extensions to enable, in one place for the live
// device, its NRI wrapper and Forge's device preparation (they change the
// pipelines NRI creates, hence the pipeline cache identity). Libraries imply
// extended dynamic state (PicaPipelineLibrariesSupported requires it).
inline std::vector<const char*> PicaPipelineStateExtensions(bool extendedDynamicState, bool libraries) {
    if (libraries)
        return {kPicaPipelineLibraryExtensions.begin(), kPicaPipelineLibraryExtensions.end()};
    if (extendedDynamicState) return {VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME};
    return {};
}

// Driver policy belongs here, shared by the live device and Forge. Capability
// alone says nothing about compilation memory: Intel's Mesa driver (anv)
// grows to tens of GB compiling library parts (issue #72), so it defaults to
// monolithic pipelines. AMD's Windows driver (amdvlk64) crashes linking
// library parts on older releases (#117-#120) and, on a current one
// (2.0.395, RX 7900 XT), spent about a minute compiling library parts at
// startup and stalled play on more; it defaults to monolithic pipelines too
// (with robust buffer access, see PicaRobustBufferAccessRequired). Explicit opt-in still
// requires every capability and is meant for bounded diagnostic runs. An
// invalid value is reported in the policy reason and otherwise treated as
// unset.
inline bool PicaPipelineLibrariesByDefault(VkDriverId driver) {
    return driver != VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA && driver != VK_DRIVER_ID_AMD_PROPRIETARY;
}

struct PicaPipelineLibraryPolicy {
    bool Requested = false;
    bool Enabled = false;
    bool AllPipelines = false;
    const char* Reason = "default";
};

inline PicaPipelineLibraryPolicy ResolvePicaPipelineLibraryPolicy(
    const char* environment, bool supported, bool dynamicRendering,
    bool preparesPrograms, VkDriverId driver) {
    const std::string_view mode = environment ? environment : "";
    const bool valid = mode.empty() || mode == "0" || mode == "1";
    const bool optIn = mode == "1", optOut = mode == "0";
    const bool driverDefault = PicaPipelineLibrariesByDefault(driver);
    PicaPipelineLibraryPolicy result;
    result.Requested = optIn || (!optOut && preparesPrograms && driverDefault);
    result.Enabled = result.Requested && supported && dynamicRendering;
    result.AllPipelines = result.Enabled && optIn;
    result.Reason = optOut ? "environment_disabled" :
        !supported || !dynamicRendering ? "unsupported" :
        optIn ? "explicit_opt_in" :
        !preparesPrograms ? "preparation_mode" :
        !driverDefault ? "driver_policy" :
        !valid ? "invalid_environment_ignored" : "default";
    return result;
}

} // namespace Fast::Renderer3ds
