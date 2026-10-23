#pragma once
#include "fast/oot3d/pica_pipeline_manifest.h"
#include "fast/oot3d/pica_nri_pipeline_state.h"
#include "fast/renderer3ds/pica_shader_source_identity.h"

namespace Fast::Oot3d {
struct PicaPipelinePreparationItem {
    Renderer3ds::NriPicaGraphicsPipelineDesc Descriptor;
    std::vector<VkVertexInputBindingDescription> Bindings;
    std::vector<VkVertexInputAttributeDescription> Attributes;
    PicaPipelinePreparationItem() = default;
    PicaPipelinePreparationItem(const PicaPipelinePreparationItem&) = delete;
    PicaPipelinePreparationItem& operator=(const PicaPipelinePreparationItem&) = delete;
    PicaPipelinePreparationItem(PicaPipelinePreparationItem&&) = default;
    PicaPipelinePreparationItem& operator=(PicaPipelinePreparationItem&&) = default;
};
// A specialized fragment program's built-in SPIR-V, consulted before the
// pack (FindPicaBuiltInFragmentSpirv in a host that embeds the families).
using PicaBuiltInFragmentLookup = std::span<const uint32_t> (*)(
    const Renderer3ds::PicaShaderSourceIdentity& source, bool separateSamplers);
// Shader spans borrow the pack and the built-in families, which must outlive
// the item.
PicaPipelinePreparationItem ResolvePicaPipelinePreparationItem(
    const PicaGraphicsPipelineManifestEntry& entry,
    const PicaAotShaderPack& pack, VkFormat depthFormat,
    PicaBuiltInFragmentLookup builtIn = nullptr);
} // namespace Fast::Oot3d
