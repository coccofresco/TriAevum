// SPDX-License-Identifier: MIT
//
// TriAevum DLSS module (LICENSE.md in this directory): DLSS Super Resolution
// through NVIDIA's NGX SDK on the host's Vulkan device. The NGX sequence
// (initialization, capability parameters, feature creation and evaluation)
// follows NRI's NGX upscaler, (c) NVIDIA Corporation, MIT
// (Source/Shared/UpscalerInterface.hpp), so the frames match the NRI path the
// module replaces.

#include <triaevum/dlss_module.h>

#include <nvsdk_ngx_helpers_vk.h>
#include <nvsdk_ngx_vk.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <new>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#ifndef TRIAEVUM_NGX_VERSION
#define TRIAEVUM_NGX_VERSION "unknown"
#endif

struct TriaevumDlssDevice {
    VkDevice Device = VK_NULL_HANDLE;
};

struct TriaevumDlssFeature {
    TriaevumDlssDevice* Device = nullptr;
    NVSDK_NGX_Handle* Handle = nullptr;
    NVSDK_NGX_Parameter* Parameters = nullptr;
};

namespace {
// NRI's application id: NGX requires a nonzero one.
constexpr unsigned long long kApplicationId = 0x3143DEC;
constexpr unsigned int kNodeMask = 1;

// NGX functions are not thread safe (nvsdk_ngx.h).
std::mutex& NgxLock() {
    static std::mutex lock;
    return lock;
}

void SetReason(char* reason, size_t reasonBytes, const std::string& text) {
    if (reason == nullptr || reasonBytes == 0)
        return;
    std::snprintf(reason, reasonBytes, "%s", text.c_str());
}

// NGX's own messages are discarded, as NRI does: with the other sinks off,
// NGX needs a callback to accept the logging setup at all.
void NVSDK_CONV DiscardNgxMessage(const char*, NVSDK_NGX_Logging_Level, NVSDK_NGX_Feature) {}

std::string ResultText(const char* what, NVSDK_NGX_Result result) {
    char text[160];
    std::snprintf(text, sizeof(text), "%s failed (NGX 0x%08X)", what, static_cast<unsigned>(result));
    return text;
}

// The module's own directory: NVIDIA's DLSS runtime is installed beside it.
std::filesystem::path ModuleDirectory() {
#if defined(_WIN32)
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&ModuleDirectory), &module))
        return {};
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameW(module, buffer.data(), DWORD(buffer.size()));
        if (length == 0)
            return {};
        if (length < buffer.size())
            return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
        buffer.resize(buffer.size() * 2);
    }
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&ModuleDirectory), &info) == 0 || info.dli_fname == nullptr)
        return {};
    std::error_code error;
    const auto path = std::filesystem::canonical(info.dli_fname, error);
    return error ? std::filesystem::path(info.dli_fname).parent_path() : path.parent_path();
#endif
}

int VulkanRequirements(TriaevumDlssVulkanRequirements* requirements, char* reason, size_t reasonBytes) {
    if (requirements == nullptr) {
        SetReason(reason, reasonBytes, "no requirements to fill");
        return 0;
    }
    std::lock_guard lock(NgxLock());
    unsigned int instanceCount = 0;
    unsigned int deviceCount = 0;
    const char** instances = nullptr;
    const char** devices = nullptr;
    const NVSDK_NGX_Result result =
        NVSDK_NGX_VULKAN_RequiredExtensions(&instanceCount, &instances, &deviceCount, &devices);
    if (NVSDK_NGX_FAILED(result)) {
        SetReason(reason, reasonBytes, ResultText("NVSDK_NGX_VULKAN_RequiredExtensions", result));
        return 0;
    }
    requirements->InstanceExtensionCount = instanceCount;
    requirements->InstanceExtensions = instances;
    requirements->DeviceExtensionCount = deviceCount;
    requirements->DeviceExtensions = devices;
    return 1;
}

TriaevumDlssDevice* CreateDevice(const TriaevumDlssDeviceDesc* desc, char* reason, size_t reasonBytes) {
    if (desc == nullptr || desc->Device == VK_NULL_HANDLE) {
        SetReason(reason, reasonBytes, "no Vulkan device");
        return nullptr;
    }
    std::lock_guard lock(NgxLock());
    // NGX looks for the DLSS runtime in the listed folders: the module's own.
    const std::wstring directory = ModuleDirectory().wstring();
    const wchar_t* paths[] = { directory.c_str() };
    NVSDK_NGX_FeatureCommonInfo common{};
    common.LoggingInfo.LoggingCallback = DiscardNgxMessage;
    common.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_OFF;
    common.LoggingInfo.DisableOtherLoggingSinks = true;
    if (!directory.empty()) {
        common.PathListInfo.Path = paths;
        common.PathListInfo.Length = 1;
    }
    // NGX's folder for its temporary files: the user's temporary directory,
    // writable in every package, the Flatpak included.
    // TRIAEVUM_NGX_LOG_DIRECTORY instead takes NGX's own verbose log there,
    // for a report of why DLSS is unavailable on a machine.
    std::filesystem::path data;
    if (const char* logDirectory = std::getenv("TRIAEVUM_NGX_LOG_DIRECTORY");
        logDirectory != nullptr && *logDirectory != '\0') {
        data = logDirectory;
        common.LoggingInfo.LoggingCallback = nullptr;
        common.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_VERBOSE;
        common.LoggingInfo.DisableOtherLoggingSinks = false;
    } else {
        std::error_code error;
        data = std::filesystem::temp_directory_path(error);
        if (!error)
            data /= "TriAevum-NGX";
    }
    std::error_code created;
    std::filesystem::create_directories(data, created);
    const std::wstring dataPath = data.wstring();
    NVSDK_NGX_Result result =
        NVSDK_NGX_VULKAN_Init(kApplicationId, dataPath.c_str(), desc->Instance, desc->PhysicalDevice,
                              desc->Device, desc->GetInstanceProcAddr, desc->GetDeviceProcAddr, &common);
    if (NVSDK_NGX_FAILED(result)) {
        SetReason(reason, reasonBytes, ResultText("NGX initialization", result));
        return nullptr;
    }
    NVSDK_NGX_Parameter* capabilities = nullptr;
    result = NVSDK_NGX_VULKAN_GetCapabilityParameters(&capabilities);
    std::string unavailable;
    if (NVSDK_NGX_FAILED(result) || capabilities == nullptr) {
        unavailable = ResultText("NGX capability query", result);
    } else {
        int needsDriver = 0;
        unsigned int driverMajor = 0;
        unsigned int driverMinor = 0;
        int available = 0;
        int initResult = 0;
        const bool driverKnown = NVSDK_NGX_SUCCEED(
            capabilities->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needsDriver));
        if (driverKnown && needsDriver != 0) {
            capabilities->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &driverMajor);
            capabilities->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &driverMinor);
            unavailable = "DLSS needs NVIDIA driver " + std::to_string(driverMajor) + "." +
                          std::to_string(driverMinor) + " or newer";
        } else if (NVSDK_NGX_FAILED(capabilities->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &available)) ||
                   available == 0) {
            capabilities->Get(NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &initResult);
            char text[200];
            std::snprintf(text, sizeof(text),
                          "DLSS is not available on this GPU or its runtime library was not found (NGX 0x%08X)",
                          static_cast<unsigned>(initResult));
            unavailable = text;
        }
        NVSDK_NGX_VULKAN_DestroyParameters(capabilities);
    }
    if (!unavailable.empty()) {
        NVSDK_NGX_VULKAN_Shutdown1(desc->Device);
        SetReason(reason, reasonBytes, unavailable);
        return nullptr;
    }
    auto* device = new (std::nothrow) TriaevumDlssDevice{};
    if (device == nullptr) {
        NVSDK_NGX_VULKAN_Shutdown1(desc->Device);
        SetReason(reason, reasonBytes, "out of memory");
        return nullptr;
    }
    device->Device = desc->Device;
    return device;
}

void DestroyDevice(TriaevumDlssDevice* device) {
    if (device == nullptr)
        return;
    {
        std::lock_guard lock(NgxLock());
        NVSDK_NGX_VULKAN_Shutdown1(device->Device);
    }
    delete device;
}

// NRI's GetUpscalerProps: the render extent of each mode.
float ScalingFactor(uint32_t mode) {
    switch (mode) {
        case TRIAEVUM_DLSS_MODE_ULTRA_QUALITY: return 1.3F;
        case TRIAEVUM_DLSS_MODE_QUALITY: return 1.5F;
        case TRIAEVUM_DLSS_MODE_BALANCED: return 1.7F;
        case TRIAEVUM_DLSS_MODE_PERFORMANCE: return 2.0F;
        case TRIAEVUM_DLSS_MODE_ULTRA_PERFORMANCE: return 3.0F;
        default: return 1.0F;
    }
}

TriaevumDlssFeature* CreateFeature(TriaevumDlssDevice* device, VkCommandBuffer commandBuffer,
                                   const TriaevumDlssFeatureDesc* desc, char* reason, size_t reasonBytes) {
    if (device == nullptr || commandBuffer == VK_NULL_HANDLE || desc == nullptr || desc->OutputWidth == 0 ||
        desc->OutputHeight == 0 || desc->Mode > TRIAEVUM_DLSS_MODE_ULTRA_PERFORMANCE) {
        SetReason(reason, reasonBytes, "invalid DLSS feature description");
        return nullptr;
    }
    std::lock_guard lock(NgxLock());
    NVSDK_NGX_Parameter* parameters = nullptr;
    NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_GetCapabilityParameters(&parameters);
    if (NVSDK_NGX_FAILED(result) || parameters == nullptr) {
        SetReason(reason, reasonBytes, ResultText("NGX capability query", result));
        return nullptr;
    }
    NVSDK_NGX_PerfQuality_Value quality = NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    switch (desc->Mode) {
        case TRIAEVUM_DLSS_MODE_NATIVE:
            quality = NVSDK_NGX_PerfQuality_Value_DLAA;
            NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, desc->Preset);
            break;
        case TRIAEVUM_DLSS_MODE_ULTRA_QUALITY:
        case TRIAEVUM_DLSS_MODE_QUALITY:
            quality = NVSDK_NGX_PerfQuality_Value_MaxQuality;
            NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, desc->Preset);
            break;
        case TRIAEVUM_DLSS_MODE_BALANCED:
            quality = NVSDK_NGX_PerfQuality_Value_Balanced;
            NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, desc->Preset);
            break;
        case TRIAEVUM_DLSS_MODE_PERFORMANCE:
            quality = NVSDK_NGX_PerfQuality_Value_MaxPerf;
            NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
                                      desc->Preset);
            break;
        default:
            NVSDK_NGX_Parameter_SetUI(parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance,
                                      desc->Preset);
            break;
    }
    // Automatic exposure, render-resolution motion vectors without jitter.
    int flags = NVSDK_NGX_DLSS_Feature_Flags_AutoExposure | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
    if ((desc->Flags & TRIAEVUM_DLSS_FLAG_HDR) != 0)
        flags |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;
    const float scaling = ScalingFactor(desc->Mode);
    NVSDK_NGX_DLSS_Create_Params create{};
    create.Feature.InWidth = static_cast<unsigned int>(static_cast<float>(desc->OutputWidth) / scaling + 0.5F);
    create.Feature.InHeight = static_cast<unsigned int>(static_cast<float>(desc->OutputHeight) / scaling + 0.5F);
    create.Feature.InTargetWidth = desc->OutputWidth;
    create.Feature.InTargetHeight = desc->OutputHeight;
    create.Feature.InPerfQualityValue = quality;
    create.InFeatureCreateFlags = flags;
    NVSDK_NGX_Handle* handle = nullptr;
    result = NGX_VULKAN_CREATE_DLSS_EXT1(device->Device, commandBuffer, kNodeMask, kNodeMask, &handle, parameters,
                                         &create);
    if (NVSDK_NGX_FAILED(result) || handle == nullptr) {
        NVSDK_NGX_VULKAN_DestroyParameters(parameters);
        SetReason(reason, reasonBytes, ResultText("DLSS feature creation", result));
        return nullptr;
    }
    auto* feature = new (std::nothrow) TriaevumDlssFeature{device, handle, parameters};
    if (feature == nullptr) {
        NVSDK_NGX_VULKAN_ReleaseFeature(handle);
        NVSDK_NGX_VULKAN_DestroyParameters(parameters);
        SetReason(reason, reasonBytes, "out of memory");
    }
    return feature;
}

void DestroyFeature(TriaevumDlssFeature* feature) {
    if (feature == nullptr)
        return;
    {
        std::lock_guard lock(NgxLock());
        NVSDK_NGX_VULKAN_DestroyParameters(feature->Parameters);
        NVSDK_NGX_VULKAN_ReleaseFeature(feature->Handle);
    }
    delete feature;
}

NVSDK_NGX_Resource_VK Resource(const TriaevumDlssImage& image, bool storage) {
    const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    return NVSDK_NGX_Create_ImageView_Resource_VK(image.View, image.Image, range, image.Format, image.Width,
                                                  image.Height, storage);
}

int Evaluate(TriaevumDlssFeature* feature, VkCommandBuffer commandBuffer, const TriaevumDlssEvaluateDesc* desc) {
    if (feature == nullptr || commandBuffer == VK_NULL_HANDLE || desc == nullptr ||
        desc->Color.View == VK_NULL_HANDLE || desc->Output.View == VK_NULL_HANDLE ||
        desc->Depth.View == VK_NULL_HANDLE || desc->Motion.View == VK_NULL_HANDLE)
        return 0;
    NVSDK_NGX_Resource_VK output = Resource(desc->Output, true);
    NVSDK_NGX_Resource_VK color = Resource(desc->Color, false);
    NVSDK_NGX_Resource_VK motion = Resource(desc->Motion, false);
    NVSDK_NGX_Resource_VK depth = Resource(desc->Depth, false);
    NVSDK_NGX_Resource_VK reactive = Resource(desc->Reactive, false);
    NVSDK_NGX_VK_DLSS_Eval_Params evaluate{};
    evaluate.Feature.pInColor = &color;
    evaluate.Feature.pInOutput = &output;
    evaluate.pInMotionVectors = &motion;
    evaluate.pInDepth = &depth;
    evaluate.pInBiasCurrentColorMask = desc->Reactive.Image != VK_NULL_HANDLE ? &reactive : nullptr;
    evaluate.InJitterOffsetX = desc->JitterX;
    evaluate.InJitterOffsetY = desc->JitterY;
    evaluate.InRenderSubrectDimensions = { desc->RenderWidth, desc->RenderHeight };
    evaluate.InReset = desc->ResetHistory != 0 ? 1 : 0;
    evaluate.InMVScaleX = desc->MotionScaleX;
    evaluate.InMVScaleY = desc->MotionScaleY;
    std::lock_guard lock(NgxLock());
    return NVSDK_NGX_SUCCEED(
        NGX_VULKAN_EVALUATE_DLSS_EXT(commandBuffer, feature->Handle, feature->Parameters, &evaluate))
               ? 1
               : 0;
}

const TriaevumDlssModule kModule{
    TRIAEVUM_DLSS_ABI_VERSION,
    TRIAEVUM_NGX_VERSION,
    "Upscaling by NVIDIA DLSS. NVIDIA and DLSS are trademarks and/or registered trademarks of NVIDIA "
    "Corporation.",
    VulkanRequirements,
    CreateDevice,
    DestroyDevice,
    CreateFeature,
    DestroyFeature,
    Evaluate,
};
} // namespace

extern "C" TRIAEVUM_DLSS_EXPORT const TriaevumDlssModule* triaevum_dlss_module(uint32_t abiVersion) {
    return abiVersion == TRIAEVUM_DLSS_ABI_VERSION ? &kModule : nullptr;
}
