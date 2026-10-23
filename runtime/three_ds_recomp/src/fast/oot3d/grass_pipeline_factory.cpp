#include "fast/oot3d/grass_pipeline_factory.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/grass_draw_state.h"
#include "fast/oot3d/outline_occlusion_pass.h"
#include "fast/oot3d/pica_attachment_contract.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace Fast::Oot3d {

VkShaderModule CompileGrassShaderModule(Renderer::CachedPassShaderCompiler& shaders, VkDevice device,
                                        const char* source, Renderer::SpirvStage stage, const char* name,
                                        const char* defineName, const char* defineValue) {
    Renderer::ShaderDefines defines;
    if (defineName != nullptr && defineValue != nullptr) defines.emplace_back(defineName, defineValue);
    const auto words = shaders.Resolve(source, stage, name, defines);
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = words.size() * sizeof(uint32_t);
    info.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS)
        throw std::runtime_error(std::string("cannot create ") + name);
    return module;
}

void CreateGrassPipelineSet(VkDevice device, const GrassPipelineTargets& targets, VkPipelineLayout layout,
                            VkShaderModule vertex, std::span<const VkShaderModule> fragments,
                            std::span<VkPipeline> pipelines) {
    if (fragments.size() % 2U != 0U || pipelines.size() != fragments.size() / 2U * 3U)
        throw std::runtime_error("interactive grass pipeline set does not match its fragments");
    std::fill(pipelines.begin(), pipelines.end(), VK_NULL_HANDLE);
    VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertex, "main",
         nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragments[0],
         "main", nullptr}};
    // No vertex input: each instance reads its record from storage.
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0F;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = targets.SampleCount;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = 0xFU;
    VkPipelineColorBlendAttachmentState guideAttachment{};
    guideAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendAttachmentState motionAttachment{};
    ConfigureOutlineCoverageBlend(motionAttachment,
                                  VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT);
    // Geometry guide: the grass's face normal and kind (G/B); the native
    // normal and depth (R/A) stay the outlines' native surface.
    VkPipelineColorBlendAttachmentState geometryAttachment{};
    geometryAttachment.colorWriteMask = VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    const std::array<VkPipelineColorBlendAttachmentState, kPicaColorAttachmentCount> attachments{
        attachment, guideAttachment, guideAttachment, motionAttachment,
        guideAttachment, VkPipelineColorBlendAttachmentState{}, geometryAttachment};
    static_assert(kPicaColorAttachmentCount == kPicaBaseInstrumentedColorAttachmentCount + 1U);
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.pAttachments = attachments.data();
    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;
    VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline.stageCount = 2;
    pipeline.pStages = stages;
    pipeline.pVertexInputState = &vertexInput;
    pipeline.pInputAssemblyState = &assembly;
    pipeline.pViewportState = &viewport;
    pipeline.pRasterizationState = &raster;
    pipeline.pMultisampleState = &multisample;
    pipeline.pDepthStencilState = &depth;
    pipeline.pColorBlendState = &blend;
    pipeline.pDynamicState = &dynamic;
    pipeline.layout = layout;
    const std::array<VkFormat, kPicaColorAttachmentCount> colorFormats{
        VK_FORMAT_R8G8B8A8_UNORM,      VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_R32G32B32A32_UINT,   VK_FORMAT_R16G16B16A16_UNORM};
    VkPipelineRenderingCreateInfoKHR rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    if (targets.DynamicRendering) {
        if (targets.DepthFormat == VK_FORMAT_UNDEFINED)
            throw std::runtime_error("interactive grass dynamic depth format is invalid");
        rendering.pColorAttachmentFormats = colorFormats.data();
        rendering.depthAttachmentFormat = targets.DepthFormat;
        if (targets.DepthFormat == VK_FORMAT_D16_UNORM_S8_UINT || targets.DepthFormat == VK_FORMAT_D24_UNORM_S8_UINT ||
            targets.DepthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT)
            rendering.stencilAttachmentFormat = targets.DepthFormat;
        pipeline.pNext = &rendering;
        pipeline.renderPass = VK_NULL_HANDLE;
    }
    const std::array<uint32_t, 3> attachmentCounts{1U, static_cast<uint32_t>(kPicaBaseInstrumentedColorAttachmentCount),
                                                   static_cast<uint32_t>(kPicaColorAttachmentCount)};
    const std::array<VkRenderPass, 2> renderPasses{targets.CanonicalRenderPass, targets.InstrumentedRenderPass};
    try {
        for (size_t slot = 0U; slot < pipelines.size(); ++slot) {
            const size_t index = slot % 3U;
            if (index == 2U && !targets.DynamicRendering) continue;
            stages[1].module = fragments[std::min<size_t>(index, 1U) + slot / 3U * 2U];
            blend.attachmentCount = attachmentCounts[index];
            if (targets.DynamicRendering) {
                rendering.colorAttachmentCount = attachmentCounts[index];
            } else {
                if (renderPasses[index] == VK_NULL_HANDLE)
                    throw std::runtime_error("interactive grass render pass is unavailable");
                pipeline.renderPass = renderPasses[index];
            }
            if (vkCreateGraphicsPipelines(device, targets.PipelineCache, 1, &pipeline, nullptr, &pipelines[slot]) !=
                VK_SUCCESS)
                throw std::runtime_error("cannot create interactive grass pipeline");
        }
    } catch (...) {
        DestroyGrassPipelineSet(device, pipelines);
        throw;
    }
}

void DestroyGrassPipelineSet(VkDevice device, std::span<VkPipeline> pipelines) noexcept {
    for (auto& pipeline : pipelines) {
        if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
}

} // namespace Fast::Oot3d

#endif
