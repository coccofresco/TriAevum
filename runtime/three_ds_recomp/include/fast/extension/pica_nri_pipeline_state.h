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
    bool geometryGuideAtReset = false,
    // Features the draw requested and whether its program is the superset
    // (uber) program: the full geometry guide write needs both (see .cpp).
    PicaShaderInstrumentationFeature requestedFeatures = PicaShaderInstrumentationFeature::None,
    bool supersetProgram = true);

// Developer same-frame A/B (TRIAEVUM_AB_FRAME): switches the pipeline-state
// code consults while one variant of a draw is recorded. Each bit undoes one
// optimization for that recording only (0 outside an A/B recording).
enum PicaAbSwitch : uint32_t {
    // Keep the depth-initializing atmosphere draw's geometry guide write
    // (as TRIAEVUM_ATMOSPHERE_GEOMETRY_WRITE=1).
    kPicaAbAtmosphereGeometryWrite = 1U << 0,
    // Sensitivity probe: no colour write for the A/B'd draw (must differ).
    kPicaAbProbeNoColorWrite = 1U << 1,
    // Keep the partial (G|B) geometry guide write of a specialized program
    // instead of the full RGBA write (TRIAEVUM_GEOMETRY_GUIDE_FULL_WRITE=0).
    kPicaAbGeometryGuidePartial = 1U << 2,
    // Keep blending for destination-replacing blends (ONE/ZERO ADD) and the
    // COPY logic op (TRIAEVUM_REPLACE_BLEND_OFF=0).
    kPicaAbReplaceBlend = 1U << 3,
    // Keep the directional shadow light mask the receiver cull cleared
    // (TRIAEVUM_SHADOW_RECEIVER_CULL=0). A uniform, not a pipeline, switch:
    // the draw path records B with the uncleared receiver block.
    kPicaAbShadowReceiverCull = 1U << 4,
};
void SetPicaAbOverride(uint32_t switches);
[[nodiscard]] uint32_t PicaAbOverride();
} // namespace Fast::Extension
