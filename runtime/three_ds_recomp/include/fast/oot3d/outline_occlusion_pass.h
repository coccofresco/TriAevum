#pragma once

#include "fast/renderer3ds/pica_attachment_contract.h"
#include <span>
#include <vulkan/vulkan.h>

namespace Fast::Oot3d {

// Shared native/extension coverage: keep the nearest inverse window depth in A.
// RGB remains replacement motion data (or unwritable for coverage-only draws).
inline void ConfigureOutlineCoverageBlend(
    VkPipelineColorBlendAttachmentState& coverage,
    VkColorComponentFlags motionWriteMask = 0) {
    coverage.colorWriteMask = motionWriteMask | VK_COLOR_COMPONENT_A_BIT;
    coverage.blendEnable = VK_TRUE;
    coverage.colorBlendOp = VK_BLEND_OP_ADD;
    coverage.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    coverage.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    coverage.alphaBlendOp = VK_BLEND_OP_MAX;
    coverage.srcAlphaBlendFactor = coverage.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
}

// Native transparent coverage is compared to native geometry by the outline
// kernel. Mixed scene depth (including extension Grass) must not reject its
// fragments first. Reuse the bound native shader/geometry, writing ONLY guide A.
inline void ConfigureOutlineOcclusionPass(
    VkPipelineDepthStencilStateCreateInfo& depth,
    std::span<VkPipelineColorBlendAttachmentState> colors) {
    depth.depthTestEnable = VK_FALSE;
    depth.depthWriteEnable = VK_FALSE;
    for (auto* stencil : {&depth.front, &depth.back}) {
        stencil->writeMask = 0;
        stencil->failOp = stencil->passOp = stencil->depthFailOp = VK_STENCIL_OP_KEEP;
    }
    for (auto& color : colors) color.colorWriteMask = 0;
    auto& coverage = colors[Renderer3ds::PicaAttachmentIndex(Renderer3ds::PicaColorAttachment::RigidMotionGuide)];
    ConfigureOutlineCoverageBlend(coverage);
}

} // namespace Fast::Oot3d
