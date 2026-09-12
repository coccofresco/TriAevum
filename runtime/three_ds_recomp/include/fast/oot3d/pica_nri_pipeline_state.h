#pragma once

#include "fast/renderer3ds/nri_pica_pipeline_bridge.h"
#include "fast/renderer3ds/pica_render_backend.h"
#include "fast/oot3d/pica_shader_instrumentation.h"

namespace Fast::Oot3d {
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
    bool writesReactiveMask = false, bool outlineOcclusionOnly = false);
} // namespace Fast::Oot3d
