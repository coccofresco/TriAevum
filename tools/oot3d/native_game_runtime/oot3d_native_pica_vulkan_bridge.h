#pragma once

#include "oot3d/renderer/pica_render_backend.h"
#include "fast/renderer/pass_shader_artifact.h"
#include "oot3d_native_pica_submission.h"
#include "oot3d_native_pica_vulkan_plan.h"

#include <string>

namespace Oot3dNativeGame {
std::span<const Fast::Renderer3ds::PicaVertexArtifact> NativePicaVertexProgramFamily();
// Shadow-caster and surface-lighting vertex programs derived from the family.
std::span<const Fast::Renderer::PassShaderArtifact> NativePicaTitlePassPrograms();

struct Oot3dPicaVulkanDrawBridgeTiming {
    double UniformPackingSeconds = 0.0;
    double ViewConstructionSeconds = 0.0;
    double BackendSubmissionSeconds = 0.0;
};

// PICA alpha-test register (0x104): enable bit, compare function, reference.
struct Oot3dPicaAlphaTest {
    bool Enabled = false;
    uint8_t Compare = 1U; // Always
    uint8_t Reference = 0U;
};
Oot3dPicaAlphaTest DecodeOot3dPicaAlphaTest(uint32_t alphaTestRegister);

bool SubmitOot3dPicaVulkanDrawPlan(
    Oot3d::Renderer::PicaRenderBackend& renderingApi,
    const Oot3dPicaVulkanDrawPlan& plan,
    std::string* error = nullptr,
    uint64_t renderTargetNamespace = 0,
    const Oot3dPicaVulkanDrawOverrides* overrides = nullptr,
    Oot3dPicaVulkanDrawBridgeTiming* timing = nullptr);

// Hands a draw of the published sequence to the backend ahead of execution
// (vertex side; fragment uniforms and textures too when the alpha test is
// enabled or the backend asks for them); the draw is still submitted later
// in native order.
bool PrepareOot3dPicaVulkanSequenceDraw(
    Oot3d::Renderer::PicaRenderBackend& renderingApi,
    const Oot3dPicaVulkanDrawPlan& plan,
    std::string* error = nullptr,
    uint64_t renderTargetNamespace = 0,
    const Oot3dPicaVulkanDrawOverrides* overrides = nullptr);

bool SubmitOot3dPicaVulkanDisplayTransfer(
    Oot3d::Renderer::PicaRenderBackend& renderingApi,
    const Oot3dPicaDisplayTransferSubmission& transfer,
    bool present,
    std::string* error = nullptr,
    uint64_t renderTargetNamespace = 0,
    bool materializeGuest = false,
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
