#pragma once
#include "fast/oot3d/pica_pipeline_manifest.h"
#include "fast/oot3d/pica_nri_pipeline_state.h"

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
// Shader spans borrow the pack, which must outlive the item.
PicaPipelinePreparationItem ResolvePicaPipelinePreparationItem(
    const PicaGraphicsPipelineManifestEntry& entry,
    const PicaAotShaderPack& pack, VkFormat depthFormat);
} // namespace Fast::Oot3d
