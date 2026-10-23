/* SPDX-License-Identifier: MIT
 *
 * TriAevum iMMERSE module interface.
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
 * The boundary between TriAevum (GPL) and the separately licensed iMMERSE
 * module (src/fast/immerse, MXAO by Pascal Gilcher, under its own licence).
 * The module is a separate shared library, triaevum_immerse.dll /
 * libtriaevum_immerse.so beside the runtime, that includes only this header,
 * NRI's and Vulkan's headers and its own files. TriAevum loads it when present
 * and runs without MXAO when it is not. The module reaches TriAevum only
 * through the host table below, and TriAevum reaches the module only through
 * the module table: plain C values, Vulkan handles, and NRI objects passed as
 * opaque pointers that the module uses through the NRI core interface it is
 * given.
 */
#ifndef TRIAEVUM_IMMERSE_MODULE_H
#define TRIAEVUM_IMMERSE_MODULE_H

#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TRIAEVUM_IMMERSE_ABI_VERSION 1u
/* The module's single export: const TriaevumImmerseModule* (uint32_t abi). */
#define TRIAEVUM_IMMERSE_ENTRY_POINT "triaevum_immerse_module"
#if defined(_WIN32)
#define TRIAEVUM_IMMERSE_LIBRARY "triaevum_immerse.dll"
#define TRIAEVUM_IMMERSE_EXPORT __declspec(dllexport)
#else
#define TRIAEVUM_IMMERSE_LIBRARY "libtriaevum_immerse.so"
#define TRIAEVUM_IMMERSE_EXPORT __attribute__((visibility("default")))
#endif

/* Image states the module asks the host for. The private images are the
 * module's own scratch textures; the output is the host's graph resource,
 * moved to the access its barrier plan declares for the dispatch and for the
 * readers after it. */
enum TriaevumImmerseImageState {
    TRIAEVUM_IMMERSE_PRIVATE_SHADER_READ = 0,
    TRIAEVUM_IMMERSE_PRIVATE_COMPUTE_WRITE = 1,
    TRIAEVUM_IMMERSE_OUTPUT_DISPATCH = 2,
    TRIAEVUM_IMMERSE_OUTPUT_COMPLETION = 3
};

/* Services of the host. Every call returns nonzero on success. */
typedef struct TriaevumImmerseHost {
    uint32_t AbiVersion;
    void* Context;
    /* nri::CoreInterface*, nri::Device* and nri::PipelineCache* (or NULL). */
    const void* NriCore;
    void* NriDevice;
    void* NriPipelineCache;
    /* GLSL libraries of the host the module's programs include: the geometry
     * guide's decoding, and `float mxao_debug_pack_normal(vec3)`, the
     * display encoding of a normal in the debug views. */
    const char* GeometryGuideGlsl;
    const char* DebugGlsl;
    int (*SupportsSampledStorage)(void* context, VkFormat format);
    /* SPIR-V of a compute program: the module's precompiled one from `pack`
     * (its SPIR-V pack) when it holds this source, otherwise compiled by the
     * host. The words stay valid until the next call. */
    const uint32_t* (*ComputeSpirv)(void* context, const void* pack, size_t packBytes, const char* source,
                                    const char* name, size_t* words);
    /* A 2D sampled+storage texture owned by the host until DestroyTexture. */
    int (*CreateTexture)(void* context, uint32_t width, uint32_t height, VkFormat format, VkImage* image);
    void (*DestroyTexture)(void* context, VkImage image);
    /* Makes an image the host did not create usable for TextureView. */
    int (*WrapTexture)(void* context, VkImage image, VkFormat format, uint32_t width, uint32_t height);
    /* nri::Descriptor* of a sampled (storage 0) or storage (1) view. */
    void* (*TextureView)(void* context, VkImage image, int storage);
    /* Completes recorded GPU work before resources are replaced. */
    int (*CompleteRecordedWork)(void* context);
    /* nri::CommandBuffer* recording the frame slot. */
    void* (*CommandBuffer)(void* context, uint32_t frameSlot);
    /* Starts the output's execution (before its first transition). */
    void (*BeginOutput)(void* context);
    int (*Transition)(void* context, uint32_t frameSlot, VkImage image, uint32_t state);
    /* Forgets the states of the module's private images. */
    void (*ClearPrivateStates)(void* context);
} TriaevumImmerseHost;

/* MXAO's settings, TriAevum's values (graphics_settings.h). */
typedef struct TriaevumMxaoSettings {
    uint32_t Method;        /* 0 ground truth, 1 solid angle, 2 bitmask, 3 bitmask + solid angle */
    uint32_t Quality;       /* preset index */
    uint32_t ShadingRate;   /* 0 full, 1 half, 2 quarter */
    uint32_t Normals;       /* 0 from depth, 1 renderer, 2 face (geometry guide) */
    uint32_t Filter;        /* 0..2 */
    uint32_t DebugView;     /* TriaevumMxaoDebugView */
    uint32_t RadiusGrowsWithDistance;
    uint32_t OriginalFade;
    uint32_t Relief;        /* texture relief level, part of the history's identity */
    float Radius;
    float Strength;
    float FadeFrom;
    float FadeTo;
    float ScreenRadius;
    float FadeDistance;
} TriaevumMxaoSettings;

/* TriAevum's MXAO debug views (MxaoDebugView). */
enum TriaevumMxaoDebugView {
    TRIAEVUM_MXAO_DEBUG_OFF = 0,
    TRIAEVUM_MXAO_DEBUG_AMBIENT_OCCLUSION = 1,
    TRIAEVUM_MXAO_DEBUG_UNFILTERED = 2,
    TRIAEVUM_MXAO_DEBUG_NORMALS = 3,
    TRIAEVUM_MXAO_DEBUG_HISTORY_LENGTH = 10,
    TRIAEVUM_MXAO_DEBUG_COUNT = 13
};

typedef struct TriaevumMxaoTargets {
    /* R32F visibility, sampled+storage, owned by the host's effect graph;
     * OutputIdentity changes when the host's state tracking of it does. */
    VkImage Output;
    uint64_t OutputIdentity;
    uint32_t Width;
    uint32_t Height;
    VkImage Depth;
    VkFormat DepthFormat;
    VkImage Normal;
    VkFormat NormalFormat;
    /* Geometry guide (R32G32B32A32_UINT), or a cleared image of that format. */
    VkImage Geometry;
    /* Motion vectors for temporal accumulation, or VK_NULL_HANDLE. */
    VkImage Motion;
    VkFormat MotionFormat;
    uint32_t MotionWidth;
    uint32_t MotionHeight;
} TriaevumMxaoTargets;

typedef struct TriaevumMxaoFrame {
    uint32_t FrameSlot;
    uint64_t FrameId;
    /* The view: column-major projection, planes, w-buffered depth, and the
     * view NDC of the output's storage UV (0,0), (1,0) and (0,1). */
    float Projection[16];
    float NearPlane;
    float FarPlane;
    uint32_t WBuffer;
    float NdcOrigin[2];
    float NdcX[2];
    float NdcY[2];
    TriaevumMxaoSettings Settings;
    /* Temporal frame: present, its frame, the view's continuity and
     * transforms. */
    uint32_t Temporal;
    uint64_t TemporalFrameId;
    uint32_t ViewHistoryValid;
    uint32_t CameraCut;
    float PreviousWorldToClip[16];
    float InverseCurrentWorldToClip[16];
} TriaevumMxaoFrame;

/* The F1 controls' texts, which come from the original. */
typedef struct TriaevumImmerseChoice {
    const char* Label;
    const char* const* Names;
    uint32_t Count;
    const char* Tooltip;
} TriaevumImmerseChoice;

typedef struct TriaevumImmerseSlider {
    const char* Label;
    float Minimum;
    float Maximum;
    const char* Format;
    const char* Tooltip;
} TriaevumImmerseSlider;

typedef struct TriaevumMxaoUi {
    TriaevumImmerseChoice Quality;
    TriaevumImmerseChoice ShadingRate;
    TriaevumImmerseChoice Method;
    TriaevumImmerseChoice Normals;
    const char* RadiusGrowsWithDistance;
    TriaevumImmerseSlider ScreenRadius;
    TriaevumImmerseSlider Radius; /* logarithmic */
    TriaevumImmerseSlider Amount;
    const char* FilterLabel;
    uint32_t FilterMaximum;
    const char* TemporalLabel;
    const char* TemporalNote;
    const char* ReliefNote;
    const char* AdvancedLabel;
    const char* OriginalFadeLabel;
    const char* OriginalFadeTooltip;
    TriaevumImmerseSlider FadeDistance;
    TriaevumImmerseSlider FadeStart; /* drag, speed 1 */
    TriaevumImmerseSlider FadeEnd;   /* drag, speed 1 */
    TriaevumImmerseChoice DebugView;
    const char* CreditsTitle;
    const char* Credits[2];
} TriaevumMxaoUi;

typedef struct TriaevumImmerseMxao TriaevumImmerseMxao;

typedef struct TriaevumImmerseModule {
    uint32_t AbiVersion;
    const TriaevumMxaoUi* MxaoUi;
    /* NULL on failure, with the reason in `reason`. The host table must
     * outlive the instance. */
    TriaevumImmerseMxao* (*CreateMxao)(const TriaevumImmerseHost* host, char* reason, size_t reasonBytes);
    void (*DestroyMxao)(TriaevumImmerseMxao* mxao);
    int (*Configure)(TriaevumImmerseMxao* mxao, const TriaevumMxaoTargets* targets);
    int (*Execute)(TriaevumImmerseMxao* mxao, const TriaevumMxaoFrame* frame);
    void (*InvalidateScreenResources)(TriaevumImmerseMxao* mxao);
    /* The output the last Configure bound, VK_NULL_HANDLE when none. */
    VkImage (*Output)(const TriaevumImmerseMxao* mxao);
    int (*LastHistoryValid)(const TriaevumImmerseMxao* mxao);
    const char* (*Reason)(const TriaevumImmerseMxao* mxao);
} TriaevumImmerseModule;

typedef const TriaevumImmerseModule* (*TriaevumImmerseEntryPoint)(uint32_t abiVersion);

#ifdef __cplusplus
}
#endif

#endif
