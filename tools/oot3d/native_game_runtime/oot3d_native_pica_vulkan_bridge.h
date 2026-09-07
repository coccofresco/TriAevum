#pragma once

#include "oot3d/renderer/pica_render_backend.h"
#include "oot3d_native_pica_submission.h"
#include "oot3d_native_pica_vulkan_plan.h"

#include <string>

namespace Oot3dNativeGame {

struct Oot3dPicaVulkanDrawBridgeTiming {
    double UniformPackingSeconds = 0.0;
    double ViewConstructionSeconds = 0.0;
    double BackendSubmissionSeconds = 0.0;
};

bool SubmitOot3dPicaVulkanDrawPlan(
    Oot3d::Renderer::PicaRenderBackend& renderingApi,
    const Oot3dPicaVulkanDrawPlan& plan,
    std::string* error = nullptr,
    uint64_t renderTargetNamespace = 0,
    const Oot3dPicaVulkanDrawOverrides* overrides = nullptr,
    Oot3dPicaVulkanDrawBridgeTiming* timing = nullptr);

bool SubmitOot3dPicaVulkanDisplayTransfer(
    Oot3d::Renderer::PicaRenderBackend& renderingApi,
    const Oot3dPicaDisplayTransferSubmission& transfer,
    bool present,
    std::string* error = nullptr,
    uint64_t renderTargetNamespace = 0,
    Oot3d::Renderer::PicaPresentationMode presentationMode =
        Oot3d::Renderer::PicaPresentationMode::Replace);

bool ClearOot3dPicaVulkanRenderTarget(
    Oot3d::Renderer::PicaRenderBackend& renderingApi,
    uint64_t renderTargetNamespace,
    uint32_t colorPhysicalAddress, std::string* error = nullptr);

bool SubmitOot3dPicaVulkanMemoryFill(
    Oot3d::Renderer::PicaRenderBackend& renderingApi,
    const Oot3dPicaMemoryFillSubmission& fill,
    std::string* error = nullptr,
    uint64_t renderTargetNamespace = 0);

} // namespace Oot3dNativeGame
