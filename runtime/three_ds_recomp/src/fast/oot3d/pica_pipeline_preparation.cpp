#include "fast/oot3d/pica_pipeline_preparation.h"
#include "fast/oot3d/pica_nri_vertex_input.h"
#include <stdexcept>

namespace Fast::Oot3d {
PicaPipelinePreparationItem ResolvePicaPipelinePreparationItem(
    const PicaGraphicsPipelineManifestEntry& entry,
    const PicaAotShaderPack& pack, VkFormat depthFormat) {
    if (!entry.Valid() || !entry.NriFragmentAvailable ||
        entry.DescriptorSchemaVersion != pack.DescriptorSchemaVersion())
        throw std::runtime_error("incompatible NRI pipeline recipe");
    Renderer3ds::PicaDrawView draw;
    draw.Topology = entry.Topology;
    draw.CullMode = entry.CullMode;
    draw.FramebufferFlipped = entry.FramebufferFlipped;
    draw.ColorWriteMask = entry.ColorWriteMask;
    draw.FragmentOperationMode = entry.FragmentOperationMode;
    draw.LogicOperation = entry.LogicOperation;
    draw.Blend.Enabled = entry.Blend.Enabled;
    draw.Blend.EquationRgb = entry.Blend.EquationRgb;
    draw.Blend.EquationAlpha = entry.Blend.EquationAlpha;
    draw.Blend.SourceRgb = entry.Blend.SourceRgb;
    draw.Blend.DestRgb = entry.Blend.DestRgb;
    draw.Blend.SourceAlpha = entry.Blend.SourceAlpha;
    draw.Blend.DestAlpha = entry.Blend.DestAlpha;
    draw.AlphaTestEnabled = entry.AlphaTestEnabled;
    draw.DepthTestEnabled = entry.DepthTestEnabled;
    draw.DepthWriteEnabled = entry.DepthWriteEnabled;
    draw.DepthCompare = entry.DepthCompare;
    draw.Stencil.Enabled = entry.Stencil.Enabled;
    draw.Stencil.Compare = entry.Stencil.Compare;
    draw.Stencil.Reference = entry.Stencil.Reference;
    draw.Stencil.CompareMask = entry.Stencil.CompareMask;
    draw.Stencil.WriteMask = entry.Stencil.WriteMask;
    draw.Stencil.Fail = entry.Stencil.Fail;
    draw.Stencil.DepthFail = entry.Stencil.DepthFail;
    draw.Stencil.Pass = entry.Stencil.Pass;
    PicaPipelinePreparationItem item;
    item.Descriptor = BuildPicaNriPipelineState(
        draw, entry.ShaderOutputs,
        {static_cast<Renderer3ds::PicaAuxiliaryOutput>(entry.AttachmentRequirementsKey)},
        static_cast<VkSampleCountFlagBits>(entry.SampleCount), depthFormat, entry.WritesReactiveMask);
    item.Descriptor.VertexSpirv = pack.Find(PicaAotShaderStage::Vertex, entry.VertexSource);
    item.Descriptor.FragmentSpirv = pack.Find(PicaAotShaderStage::NriFragment, entry.NriFragmentSource);
    if (item.Descriptor.VertexSpirv.empty() || item.Descriptor.FragmentSpirv.empty())
        throw std::runtime_error("pipeline recipe shader is absent from the portable pack");
    std::vector<PicaNriSourceVertexBinding> bindings;
    std::vector<PicaNriSourceVertexAttribute> attributes;
    for (const auto& b : entry.VertexBindings)
        bindings.push_back({b.Binding, b.ByteStride, b.PerInstance});
    for (const auto& a : entry.VertexAttributes)
        attributes.push_back({a.Location, a.Binding, static_cast<PicaNriVertexScalar>(a.Format),
                              a.ComponentCount, a.ByteOffset});
    const auto layout = BuildPicaNriVertexInputLayout(bindings, attributes);
    if (!layout.Valid()) throw std::runtime_error(layout.Error);
    for (const auto& b : layout.Bindings)
        item.Bindings.push_back({b.Binding, b.ByteStride,
            b.PerInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX});
    for (const auto& a : layout.Attributes)
        item.Attributes.push_back({a.Location, a.Binding, VK_FORMAT_R32G32B32A32_SFLOAT, a.ByteOffset});
    item.Descriptor.VertexBindings = item.Bindings;
    item.Descriptor.VertexAttributes = item.Attributes;
    return item;
}
} // namespace Fast::Oot3d
