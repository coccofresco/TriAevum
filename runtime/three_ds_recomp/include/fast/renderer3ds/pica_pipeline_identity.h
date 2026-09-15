#pragma once
#include "fast/renderer3ds/nri_pica_pipeline_bridge.h"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace Fast::Renderer3ds {
// Identity of the effective programs, not material/command-state identities.
// Each source identity contains its primary hash, secondary hash and size.
struct PicaPipelineProgramIdentity {
    std::array<uint64_t, 3> Vertex{}, Fragment{}, NriFragment{};
    bool NriDescriptorContract = false;
};

// Uses the same resolved fixed-function state as pipeline creation. The source
// vertex layout is retained too: the Vulkan fallback and normalized NRI layout
// share an owning pipeline entry. Buffer contents and dynamic state are excluded.
template<class Draw>
std::vector<uint8_t> BuildPicaPipelineIdentity(
    const PicaPipelineProgramIdentity& programs, const Draw& draw,
    const NriPicaGraphicsPipelineDesc& state, bool dynamicRendering) {
    if (programs.Vertex[2] == 0 || programs.Fragment[2] == 0 ||
        (programs.NriDescriptorContract && programs.NriFragment[2] == 0))
        throw std::invalid_argument("PICA pipeline requires effective program identities");
    if (state.ColorAttachmentCount > state.Colors.size())
        throw std::invalid_argument("PICA pipeline attachment count exceeds its contract");
    std::vector<uint8_t> key;
    key.reserve(512);
    const auto word = [&](uint32_t value) {
        for (unsigned i=0;i<4;++i) key.push_back(static_cast<uint8_t>(value>>(i*8)));
    };
    const auto source = [&](const auto& identity) {
        for (uint64_t value : identity) { word(uint32_t(value)); word(uint32_t(value>>32)); }
    };
    source(programs.Vertex); source(programs.Fragment);
    word(programs.NriDescriptorContract);
    if (programs.NriDescriptorContract) source(programs.NriFragment);
    word(dynamicRendering);
    word(static_cast<uint32_t>(draw.VertexBindings.size()));
    for (const auto& binding : draw.VertexBindings) {
        word(binding.Binding); word(binding.ByteStride); word(binding.PerInstance);
    }
    word(static_cast<uint32_t>(draw.VertexAttributes.size()));
    for (const auto& attribute : draw.VertexAttributes) {
        word(attribute.Location); word(attribute.Binding);
        word(static_cast<uint32_t>(attribute.Format));
        word(attribute.ComponentCount); word(attribute.ByteOffset);
    }
    word(state.Topology); word(state.CullMode); word(state.FrontFace);
    word(state.Samples); word(state.AlphaToCoverage);
    word(state.DepthTest); word(state.DepthWrite);
    if (state.DepthTest) word(state.DepthCompare);
    word(state.StencilTest);
    const auto stencil = [&](const VkStencilOpState& value) {
        word(value.failOp); word(value.passOp); word(value.depthFailOp);
        word(value.compareOp); word(value.compareMask); word(value.writeMask); word(value.reference);
    };
    // Disabled units do not consume their retained register values. Do not
    // compile new pipelines when only those dormant values change.
    if (state.StencilTest) { stencil(state.FrontStencil); stencil(state.BackStencil); }
    word(state.LogicOpEnabled);
    if (state.LogicOpEnabled) word(state.LogicOp);
    word(state.DepthStencilFormat); word(state.ColorAttachmentCount);
    for (uint32_t i=0;i<state.ColorAttachmentCount;++i) {
        const auto& color=state.Colors[i];
        word(state.ColorFormats[i]); word(color.blendEnable);
        if (color.blendEnable) {
            word(color.srcColorBlendFactor); word(color.dstColorBlendFactor); word(color.colorBlendOp);
            word(color.srcAlphaBlendFactor); word(color.dstAlphaBlendFactor); word(color.alphaBlendOp);
        }
        word(color.colorWriteMask);
    }
    return key;
}
} // namespace Fast::Renderer3ds
