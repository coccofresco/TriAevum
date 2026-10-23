/* SPDX-License-Identifier: MIT
 *
 * TriAevum DLSS module interface.
 *
 * Copyright (c) the TriAevum maintainer.
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this file, to deal in it without restriction, including without
 * limitation the rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of it, subject to the following condition:
 * the above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of it. THIS FILE IS PROVIDED "AS IS",
 * WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 *
 * The boundary between TriAevum (GPL) and the separately licensed DLSS module
 * (src/fast/dlss): the only part of TriAevum that links NVIDIA's NGX SDK. The
 * module is a separate shared library, triaevum_dlss.dll / libtriaevum_dlss.so
 * beside the runtime, next to NVIDIA's DLSS runtime (nvngx_dlss.dll /
 * libnvidia-ngx-dlss.so.<version>). It includes only this header, Vulkan's and
 * the NGX SDK's headers and its own files. TriAevum loads it when present and
 * offers DLSS only when it is; without it the other upscalers are unchanged.
 * Values crossing the boundary are plain C values and Vulkan handles: the host
 * owns every image, view and command buffer, and records every barrier.
 */
#ifndef TRIAEVUM_DLSS_MODULE_H
#define TRIAEVUM_DLSS_MODULE_H

#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TRIAEVUM_DLSS_ABI_VERSION 1u
/* The module's single export: const TriaevumDlssModule* (uint32_t abi). */
#define TRIAEVUM_DLSS_ENTRY_POINT "triaevum_dlss_module"
#if defined(_WIN32)
#define TRIAEVUM_DLSS_LIBRARY "triaevum_dlss.dll"
#define TRIAEVUM_DLSS_EXPORT __declspec(dllexport)
#else
#define TRIAEVUM_DLSS_LIBRARY "libtriaevum_dlss.so"
#define TRIAEVUM_DLSS_EXPORT __attribute__((visibility("default")))
#endif

/* Quality modes, in NRI's UpscalerMode order. */
enum TriaevumDlssMode {
    TRIAEVUM_DLSS_MODE_NATIVE = 0, /* DLAA */
    TRIAEVUM_DLSS_MODE_ULTRA_QUALITY = 1,
    TRIAEVUM_DLSS_MODE_QUALITY = 2,
    TRIAEVUM_DLSS_MODE_BALANCED = 3,
    TRIAEVUM_DLSS_MODE_PERFORMANCE = 4,
    TRIAEVUM_DLSS_MODE_ULTRA_PERFORMANCE = 5
};

/* Feature flags. Motion vectors are always at render resolution and not
 * jittered, depth is not inverted and exposure is automatic. */
enum TriaevumDlssFlags {
    /* The colour is linear HDR rather than display-referred (sRGB). */
    TRIAEVUM_DLSS_FLAG_HDR = 1u
};

/* Extensions NGX needs enabled on the instance and the device. The strings
 * stay valid while the module is loaded. */
typedef struct TriaevumDlssVulkanRequirements {
    uint32_t InstanceExtensionCount;
    const char* const* InstanceExtensions;
    uint32_t DeviceExtensionCount;
    const char* const* DeviceExtensions;
} TriaevumDlssVulkanRequirements;

/* The host's Vulkan device, created with the required extensions. */
typedef struct TriaevumDlssDeviceDesc {
    VkInstance Instance;
    VkPhysicalDevice PhysicalDevice;
    VkDevice Device;
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr GetDeviceProcAddr;
} TriaevumDlssDeviceDesc;

/* One DLSS Super Resolution feature: an output extent and its settings. */
typedef struct TriaevumDlssFeatureDesc {
    uint32_t OutputWidth;
    uint32_t OutputHeight;
    uint32_t Mode;   /* TriaevumDlssMode */
    uint32_t Preset; /* NGX render preset; 0 the runtime's default */
    uint32_t Flags;  /* TriaevumDlssFlags */
} TriaevumDlssFeatureDesc;

/* A 2D colour-aspect image of the host and a view of it: sampled for the
 * inputs, storage for the output. */
typedef struct TriaevumDlssImage {
    VkImage Image;
    VkImageView View;
    VkFormat Format;
    uint32_t Width;
    uint32_t Height;
} TriaevumDlssImage;

/* One evaluation, recorded into the host's command buffer. The inputs are in
 * the shader-read layout and the output in the general layout. */
typedef struct TriaevumDlssEvaluateDesc {
    TriaevumDlssImage Color;
    TriaevumDlssImage Output;
    TriaevumDlssImage Depth;
    TriaevumDlssImage Motion;
    /* Bias-current-colour (reactive) mask, or Image VK_NULL_HANDLE. */
    TriaevumDlssImage Reactive;
    uint32_t RenderWidth;
    uint32_t RenderHeight;
    float JitterX;
    float JitterY;
    float MotionScaleX;
    float MotionScaleY;
    uint32_t ResetHistory;
} TriaevumDlssEvaluateDesc;

typedef struct TriaevumDlssDevice TriaevumDlssDevice;
typedef struct TriaevumDlssFeature TriaevumDlssFeature;

typedef struct TriaevumDlssModule {
    uint32_t AbiVersion;
    /* The NVIDIA NGX SDK version the module was built with. */
    const char* RuntimeVersion;
    /* The attribution NVIDIA's DLSS SDK licence asks the application to show
     * where DLSS is offered. */
    const char* Attribution;
    /* Nonzero on success; otherwise the reason is in `reason`. Callable before
     * any Vulkan object exists. */
    int (*VulkanRequirements)(TriaevumDlssVulkanRequirements* requirements, char* reason, size_t reasonBytes);
    /* Initializes NGX on the device and confirms that DLSS Super Resolution
     * is available on it (supported GPU and driver, runtime library found).
     * NULL otherwise, with the reason in `reason`. */
    TriaevumDlssDevice* (*CreateDevice)(const TriaevumDlssDeviceDesc* desc, char* reason, size_t reasonBytes);
    /* After every feature of the device. */
    void (*DestroyDevice)(TriaevumDlssDevice* device);
    /* Records the feature's creation into `commandBuffer`. NULL on failure,
     * with the reason in `reason`. */
    TriaevumDlssFeature* (*CreateFeature)(TriaevumDlssDevice* device, VkCommandBuffer commandBuffer,
                                          const TriaevumDlssFeatureDesc* desc, char* reason, size_t reasonBytes);
    /* The host has completed the GPU work that used the feature. */
    void (*DestroyFeature)(TriaevumDlssFeature* feature);
    /* Nonzero when the evaluation was recorded into `commandBuffer`. */
    int (*Evaluate)(TriaevumDlssFeature* feature, VkCommandBuffer commandBuffer,
                    const TriaevumDlssEvaluateDesc* desc);
} TriaevumDlssModule;

typedef const TriaevumDlssModule* (*TriaevumDlssEntryPoint)(uint32_t abiVersion);

#ifdef __cplusplus
}
#endif

#endif
