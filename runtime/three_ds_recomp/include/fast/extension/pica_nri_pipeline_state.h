#pragma once

#include "fast/renderer3ds/nri_pica_pipeline_bridge.h"
#include "fast/renderer3ds/pica_render_backend.h"
#include "fast/extension/pica_shader_instrumentation.h"

namespace Fast::Extension {
VkFormat FindPicaDepthFormat(VkPhysicalDevice physicalDevice);
VkCompareOp ToNativeVkCompare(Renderer3ds::PicaCompareFunction value);
VkStencilOp ToNativeVkStencil(Renderer3ds::PicaStencilAction value);
VkLogicOp ToNativeVkLogic(Renderer3ds::PicaLogicOperation value);
VkPrimitiveTopology ToNativeVkTopology(Renderer3ds::PicaTopology value);
VkBlendOp ToNativeVkBlendOperation(Renderer3ds::NativeBlendEquation value);
VkBlendFactor ToNativeVkBlendFactor(Renderer3ds::NativeBlendFactor value);
bool UsesTranslucentBlend(const Renderer3ds::NativeBlendState& blend);

// Shared by actual draws and offline device preparation. Shader/vertex storage
// remains with the caller; this resolves only the immutable graphics state.
Renderer3ds::NriPicaGraphicsPipelineDesc BuildPicaNriPipelineState(
    const Renderer3ds::PicaDrawView& draw,
    const PicaFragmentInstrumentationOutputLayout& outputs,
    Renderer3ds::PicaAttachmentRequirements attachments,
    VkSampleCountFlagBits samples, VkFormat depthFormat,
    bool writesReactiveMask = false, bool outlineOcclusionOnly = false,
    bool geometryGuideAtReset = false);

// Developer same-frame A/B (TRIAEVUM_AB_FRAME): switches the pipeline-state
// code consults while one variant of a draw is recorded. Each bit undoes one
// optimization for that recording only (0 outside an A/B recording).
enum PicaAbSwitch : uint32_t {
    // Keep the depth-initializing atmosphere draw's geometry guide write
    // (as TRIAEVUM_ATMOSPHERE_GEOMETRY_WRITE=1).
    kPicaAbAtmosphereGeometryWrite = 1U << 0,
    // Sensitivity probe: no colour write for the A/B'd draw (must differ).
    kPicaAbProbeNoColorWrite = 1U << 1,
};
void SetPicaAbOverride(uint32_t switches);
[[nodiscard]] uint32_t PicaAbOverride();
} // namespace Fast::Extension
